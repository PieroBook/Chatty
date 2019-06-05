/*
 * membox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */
/** \file listener.c  
    \author Pietro Libro 545559
    Si dichiara che il contenuto di questo file e' in ogni sua parte opera  
    originale dell'autore  
*/
#include "stuffs.h"
#include "connections.h"
#include <sys/select.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
void * serverListen(void *param){
    // ThreadPool
    pthread_t *pool = (pthread_t*) param;
    // Inizializzazione socket server
    int serverSocket= socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un sa;
    strcpy(sa.sun_path, setupped->UnixPath);
    sa.sun_family = AF_UNIX;
    bind(serverSocket,(struct sockaddr *)&sa,sizeof(sa));
    listen(serverSocket,setupped->MaxConnections);

    // Preparo select
    struct timeval tm;
    int scorri = 0;
    int max_fd = serverSocket;
    fd_set attivi;
    fd_set copia;
    FD_ZERO(&attivi);
    FD_ZERO(&copia);
    // Associo serverSocket al set attivi
    FD_SET(serverSocket,&attivi);
    pthread_mutex_lock(&mutex_pipe);
    // Associo pipe al set attivi
    FD_SET(pipeFD[0], &attivi);
    // Aggiorno max se necessario
    if(pipeFD[0]>max_fd)
        max_fd = pipeFD[0];
    pthread_mutex_unlock(&mutex_pipe);
    while(1){
        pthread_mutex_lock(&mutex_signal);
        if(serverRun == 0)break;
        pthread_mutex_unlock(&mutex_signal);
        copia = attivi;
        int client=0;
        // Select in linux modifica struct timeval
        tm.tv_usec = 125;
        tm.tv_sec = 0;
        select(max_fd+1,&copia,NULL,NULL,&tm);
        // Scorro FD
        for(scorri=3; scorri<=max_fd; scorri++){
            // Se FD fa parte del set
            if(FD_ISSET(scorri,&copia)){
                pthread_mutex_lock(&mutex_pipe);
                if (scorri == serverSocket){
                    pthread_mutex_unlock(&mutex_pipe);
                    // Nuovo client in arrivo
                    client = accept(serverSocket,NULL,NULL);
                    // Aggiorno max se necessario
                    if (client > max_fd){
                        max_fd = client;
                    }
                    // Aggiungo nuovo FD al set
                    FD_SET(client, &attivi);
                }else if(scorri == pipeFD[0]){
                    // Leggo da pipe il fd da reinserire
                    ssize_t piperead = 0;
                    while((piperead += read(pipeFD[0],&client,sizeof(int))) != sizeof(int));
                    pthread_mutex_unlock(&mutex_pipe);
                    // Riaggiungo FD al set
                    FD_SET(client,&attivi);
                }else{
                    pthread_mutex_unlock(&mutex_pipe);
                    // Rimuovo fd dal set
                    FD_CLR(scorri,&attivi);
                    pthread_mutex_lock(&mutex_daServire);
                    // Se la vettore circolare non pieno
                    if(current == testa){
                        pthread_mutex_unlock(&mutex_daServire);
                        // Istanzion e setto risposta da inviare al cleint
                        message_t *response = calloc(1,sizeof(message_t));
                        setHeader(&response->hdr, OP_FAIL, "Server");
                        // Invio risposta
                        pthread_mutex_lock(&mutex_socket[client%MAXFDSOCK]);
                        sendHeader(scorri,&response->hdr);
                        pthread_mutex_unlock(&mutex_socket[client%MAXFDSOCK]);
                        // Chiudo FD
                        close(scorri);
                    }
                    else{
                        // Aggiungo a vettore circolare daServire e aggiorno indici
                        toServe[current] = scorri;
                        if(testa == -1)
                            testa = 0;
                        current = (current+1)%setupped->MaxConnections;
                        pthread_mutex_unlock(&mutex_daServire);
                        // Risveglia un thread che prenda il work appena aggiunto
                        pthread_cond_signal(&interrupt_signal);
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&mutex_signal);
    // Risveglio tutti i worker
    pthread_cond_broadcast(&interrupt_signal);
    // Attendo Terminazione Threads Worker
    for(int i=0;i<setupped->ThreadsInPool;i++)
        pthread_join(pool[i],NULL);
    printf("Terminati Tutti i Worker\n");
    // Chiudo serverSocket
    close(serverSocket);
    printf("Terminato Listener\n");
    return NULL;
}