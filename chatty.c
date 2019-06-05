/*
 * membox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */
/** \file chatty.c  
    \author Pietro Libro 545559
    Si dichiara che il contenuto di questo file e' in ogni sua parte opera  
    originale dell'autore  
*/
#define _POSIX_C_SOURCE 200809L
#include "stats.h"
#include "stuffs.h"
#include "connections.h"
#include "icl_hash.h"
#include "message.h"
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
// Statistiche del server
struct statistics chattyStats = { 0,0,0,0,0,0,0 };
// Mutex statistica e online
pthread_mutex_t mutex_online = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_statistica = PTHREAD_MUTEX_INITIALIZER;
// Metodi e Funzioni aggiuntive
int setup(char *filename);
int fillin(char* param, char *value, size_t size);
void * gestisci_segnali(void *param);
void * task(void *param);
int isOnline(char* name);
int prelevaFD();
void freeUser(void *u1);
char* getConnectedString();
void aggiungiUser(char *s);
void rimuoviUser(char *s);
void pulisciConnected();
// Metodi in line di un task
static inline void switchOp(message_t *msg, int client_fd);
static inline void registrazione(char* sender, int client);
static inline void connessione(int client, message_t* msg);
static inline void inviaMsg(message_t *msg, int client,int toAll);
static inline void inviaMsgToAll(message_t *msg, int client);
static inline void inviaFile(message_t *msg, int client);
static inline void ottieniFile(message_t *msg, int client);
static inline void ottieniHistory(message_t *msg, int client);
static inline void userList(int client, char* sender);
static inline void deRegistrazione(char* sender, int client);
static inline int disconnetti(int client);
//HashTable
icl_hash_t *users;
// Utenti Connessi
user *connectedUser;
static void usage(const char *progname) {
    fprintf(stderr, "Il server va lanciato con il seguente comando:\n");
    fprintf(stderr, "  %s -f conffile\n", progname);
}
// Funzione main
int main(int argc, char *argv[]) {
    serverRun = 1;
    // Verifiche preliminari di avvio e parsing parametri di configurazione
    setupped = calloc(1,sizeof(struct setting));
    if (argc < 3 || strcmp(argv[1],"-f")!=0 || setup(argv[2])!=0){
    	/* Se i campi in argv non sono corretti o il file 
    		di configurazione non e' corretto termino*/
        free(setupped);
        usage("chatty");
        return 1;
    }
    // Mi assicuro che il socket non esista
    unlink(setupped->UnixPath);
    // Cambio la current working dir
    chdir(setupped->DirName);
    // Inizializzo hashtable utenti registrati
    users = icl_hash_create(512, NULL, NULL);
    // Vettore Circolare Richieste
    current = 0;
    testa = -1;
    toServe = calloc((size_t) setupped->MaxConnections, sizeof(int));
    // Inizializzo mutex hashTable
    int i=0;
    for (i = 0; i<MAX_HASH;i++)
        pthread_mutex_init(&mutex_hashTable[i], NULL);
    // Inizializzo lock su fd utenti
    for (i = 0; i<MAXFDSOCK;i++)
        pthread_mutex_init(&mutex_socket[i], NULL);
    // Inizializzo mutex e cond ausiliarie
    pthread_mutex_init(&mutex_signal, NULL);
    pthread_mutex_init(&mutex_daServire, NULL);
    pthread_mutex_init(&mutex_pipe, NULL);
    pthread_cond_init(&interrupt_signal, NULL);
    // Inizializzo PIPE
    if(pipe(pipeFD) == -1) {
        perror("Pipe error");
        exit(EXIT_FAILURE);
    }
    // Ignoro SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    // Preparo maschera signal
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_BLOCK,&mask,NULL);
   
    // Avvio Pool di Thread Worker
    pthread_t pool [setupped->ThreadsInPool];
    for(i=0;i<setupped->ThreadsInPool;i++){
        pthread_create(&pool[i], NULL, task,NULL);
    }
    // Utenti online
    connectedUser = NULL;
    // Avvio Thread Signal Handler
    pthread_t signal_handler;
    pthread_create(&signal_handler, NULL, gestisci_segnali,(void*) &mask);
    // Avvio Thread Listener
    pthread_t listener;
    pthread_create(&listener, NULL, serverListen,(void*) &pool);

    // Attendo terminazione Signal Handler Thread
    pthread_join(signal_handler,NULL);
    // Attendo terminazione Listener Thread
    pthread_join(listener,NULL);
    free(toServe);
    // Free connessi
    pulisciConnected();
    // Free profonda hashtable
    icl_hash_destroy(users, free, freeUser);
    // Free configurazioni
    unlink(setupped->UnixPath);
    free(setupped->UnixPath);
    free(setupped->DirName);
    free(setupped->StatFileName);
    free(setupped);
    printf("Terminato Main\n");
    return 0;
}
// Funzione per le configurazioni preliminari (Ritorna 1 se ERRORE, 0 altrimenti)
int setup(char *filename){
	// Apro il fiel passato come parametro
    FILE *file  = fopen(filename, "r");
    if (file == NULL) {
        perror("Error: ");
        return 1;
    }
    int totale = 0;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    // Leggo linea per linea
    while ((read = getline(&line, &len, file)) != -1) {
        if(read != 1 && line[0]!='#'){
        	// Tokenizzo ogni linea fino al primo spazio
            char *token = strtok(line, " ");
            size_t tokenSize = strlen(token);
            // Rimuove eventuali TAB alla fine
            if(token[tokenSize-1] == 9)
                tokenSize--;
            // Copio in param il nome del parametro da settare
            char *param = calloc(tokenSize, sizeof(char));
            memcpy(param,token,tokenSize);
            // Ottengo il valore per il parametro
            token = strtok(NULL,"= ");
            size_t valueSize = strlen(token);
            // Rimuvoo eventuali TAB/Spazi/NewLine alla fine
            if(token[valueSize-1] == 9 || token[valueSize-1] == 32  || token[valueSize-1] == 10)
                valueSize--;
            // Metodo per inserimento parametro valore in struttura dati di configurazione
            if(fillin(param,token,valueSize+1)==1){
                totale++;
            }
            free(param);
        }
    }
    // Libero memoria e concludo funzione
    free(line);
    fclose(file);
    // Se tutti i parametri di configurazione sono stati inseriti termino con successo
    return totale==8?0:1;
}
/* Funzione ausiliaria di setup (Ritorna 1 se il parametro appartiene 
	alle info necessarie, 0 altrimenti)*/
int fillin(char* param, char* value, size_t size){
    char *endptr;
    value[size-1] = '\0';
    if(strncmp(param,"DirName",7)==0){
        setupped->DirName = calloc( size, sizeof(char));
        memcpy(setupped->DirName,value,size);
    }
    else if(strncmp(param,"UnixPath",8)==0){
        setupped->UnixPath = calloc((size_t) size, sizeof(char));
        memcpy(setupped->UnixPath,value,size);
    }
    else if(strncmp(param,"MaxMsgSize",10)==0){
        setupped->MaxMsgSize = (int)strtol(value,&endptr,10);
    }
    else if(strncmp(param,"MaxFileSize",11)==0){
        setupped->MaxFileSize = (int)strtol(value,&endptr,10);
    }
    else if(strncmp(param,"MaxHistMsgs",11)==0){
        setupped->MaxHistMsgs = (int)strtol(value,&endptr,10);
    }
    else if(strncmp(param,"StatFileName",12)==0){
        setupped->StatFileName = calloc((size_t) size, sizeof(char));
        memcpy(setupped->StatFileName,value,size);
    }
    else if(strncmp(param,"ThreadsInPool",13)==0){
        setupped->ThreadsInPool = (int)strtol(value,&endptr,10);
    }    
    else if(strncmp(param,"MaxConnections",14)==0){
        setupped->MaxConnections = (int)strtol(value,&endptr,10);
    }
    else
        return 0;
    return 1;
}
// Thread Signal Handler
void * gestisci_segnali(void *param){
	// Maschera Signal
    sigset_t *set = (sigset_t *)param;
    // Variabile di appoggio
    int received_signal;
    while(1){
    	// Se il server deve terminare esco dal ciclo
        pthread_mutex_lock(&mutex_signal);
        if(!serverRun)break;
        pthread_mutex_unlock(&mutex_signal);
        // Sigwait in attesa di un segnale
        if(sigwait(set, &received_signal)==0){
        	// Stampa Statistica
            if(received_signal==SIGUSR1){
            	FILE * statistica;
            	// Apro/Creo il file in cui scrivere in append(a fine file)
		        if((statistica = fopen(setupped->StatFileName, "a+")) == NULL ) {
		            perror("Scrittura Statistica:");
		            fclose(statistica);
		        }else{
			        printStats(statistica);
			        fclose(statistica);
		    	}
            }// Terminazione del server
            else if (received_signal==SIGINT || received_signal==SIGQUIT || received_signal==SIGTERM){
            	// Setto terminazione
                pthread_mutex_lock(&mutex_signal);
                serverRun = 0;
                pthread_mutex_unlock(&mutex_signal);
            }
        }
    }
    pthread_mutex_unlock(&mutex_signal);
    printf("Terminato Signal Handler\n");
    return NULL;
}
// Thread Worker
void * task(void *param){
    while(1){ // Parte thread worker
        int client;
        pthread_mutex_lock(&mutex_signal);
        pthread_mutex_lock(&mutex_daServire);
        while(serverRun && (testa==-1 || current==testa)){
            pthread_mutex_unlock(&mutex_daServire);
            // WAIT se non server sta funzionando e se vettore circ vuoto
            pthread_cond_wait(&interrupt_signal, &mutex_signal);
            pthread_mutex_lock(&mutex_daServire);
        }
        pthread_mutex_unlock(&mutex_daServire);
        // Mi assicuro di non aver ricevuto lo shutdown
        if(!serverRun){
        	// Terminazione worker
            pthread_mutex_unlock(&mutex_signal);
            return NULL;
        }
        pthread_mutex_unlock(&mutex_signal);
        // Prelevo un descrittore per servirlo
        if((client = prelevaFD()) != -1){
        	// Leggo la richiesta
            message_t *rcvMsg = calloc(1, sizeof(message_t));
            int letto = readMsg(client, rcvMsg);
            // Ho letto niente devo disconnetter l'utente
            if (letto <= 0) {
                // Free msg ricevuto e disconnessione utente
                if(rcvMsg->data.buf != NULL)
                    free(rcvMsg->data.buf);
                free(rcvMsg);
                disconnetti(client);
                // Chiusura FD
                close(client);
            }else {
            	// Conservo il CODOP
				int opwas = rcvMsg->hdr.op;
				// Se il messaggio va in history ci pensa il metodo specifico
                switchOp(rcvMsg, client);
                // Non ho bisogno di history a prescindere per queste op
                if (opwas!= 2 && opwas!= 3 && opwas != 4) {
                	// Libero memoria cancellando il msg ricevuto
                    if(rcvMsg->data.buf != NULL)
                        free(rcvMsg->data.buf);
                    free(rcvMsg);
                }
                // Comunico a listener che bisogna riascoltare da FD
                ssize_t scritto = 0;
                pthread_mutex_lock(&mutex_pipe);
                while((scritto += write(pipeFD[1], &client, sizeof(int))) != sizeof(int));
                pthread_mutex_unlock(&mutex_pipe);
            }
        }
    }
    //Terminazione
    return NULL;
}
// Gestione richiesta
static inline void switchOp(message_t *msg, int client_fd){
    op_t op = msg->hdr.op;
    switch(op){
        case REGISTER_OP:
            registrazione(msg->hdr.sender,client_fd);
            break;
        case CONNECT_OP:
            connessione(client_fd,msg);
            break;
        case POSTTXT_OP:
            inviaMsg(msg,client_fd,0);
            break;
        case POSTTXTALL_OP:
            inviaMsgToAll(msg,client_fd);
            break;
        case POSTFILE_OP:
            inviaFile(msg,client_fd);
            break;
        case GETFILE_OP:
            ottieniFile(msg,client_fd);
            break;
        case GETPREVMSGS_OP:
            ottieniHistory(msg,client_fd);
            break;
        case USRLIST_OP:
            userList(client_fd,msg->hdr.sender);
            break;
        case UNREGISTER_OP:
            deRegistrazione(msg->hdr.sender, client_fd);
            break;
        case DISCONNECT_OP:
            disconnetti(client_fd);
            break;
        default:
            fprintf(stderr,"Operazione %d non implementata\n",op);
    }
}

static inline void registrazione(char *sender, int client){
	// Instanzio risposta da inviare
    message_t *response = calloc(1,sizeof(message_t));
    int registrato = 0;
    // Creo utente da inserire in hashTable
    utente *newuser = calloc(1, sizeof(utente));
    newuser->nome = calloc(MAX_NAME_LENGTH+1, sizeof(char));
    newuser->client_fd = client;
    newuser->history = calloc((size_t) setupped->MaxHistMsgs, sizeof(message_t*));
    memcpy(newuser->nome, sender,MAX_NAME_LENGTH+1);
    newuser->current = 0;
    newuser->testa = -1;
    // Chiave per hashtable
    size_t username_len = strlen(sender);
    char* username = calloc(username_len+1, sizeof(char));
    memcpy(username,sender,username_len);
    username[username_len]='\0';
    // Indice generato dalla HashTable
    int indice = (((*users->hash_function)(username)) % (users->nbuckets))/4;
    // Acquisisco LOCK per mutua esclusione sull'utente
    pthread_mutex_lock(&mutex_hashTable[indice]);
    // Controllo validita' username da registrare
    if(icl_hash_find(users,username) == NULL){
        // Inserimento in hashTable
        icl_hash_insert(users,username,(void*)newuser);
        registrato = 1;
    }
    pthread_mutex_unlock(&mutex_hashTable[indice]);
    // Utente gia presente
    if(registrato==0){
    	// Setto risposta e la invio subito
        setHeader(&response->hdr, OP_NICK_ALREADY, "Server");
        pthread_mutex_lock(&mutex_socket[client%MAXFDSOCK]);
        sendHeader(client,&response->hdr);
        pthread_mutex_unlock(&mutex_socket[client%MAXFDSOCK]);
        // Aggiorno Statistica
        pthread_mutex_lock(&mutex_statistica);
        chattyStats.nerrors++;
        pthread_mutex_unlock(&mutex_statistica);
        // Libero Memoria
        free(newuser->history);
        free(newuser->nome);
        free(newuser);
        free(username);
    }else{// utente appena registrato lo aggiunge il nuovo utente alla lista online e aggiorna stats
    	// Aggiorno Statistica
        pthread_mutex_lock(&mutex_statistica);
        chattyStats.nusers++;
        pthread_mutex_unlock(&mutex_statistica);
        // Aggiungo a lista online
        aggiungiUser(sender);
        // Invio utenti connessi e responso
        userList(client,username);
    }
    free(response);
}

static inline void connessione(int client, message_t *msg){
	// Alloco risposta mittente
    message_t *response = calloc(1,sizeof(message_t));
    // Controllo se mittente online
    int isOn = isOnline(msg->hdr.sender);
    // Costruisco chiave ricerca HT
    size_t username_len = strlen(msg->hdr.sender);
    char* username = calloc(username_len+1, sizeof(char));
    memcpy(username,msg->hdr.sender,username_len);
    username[username_len]='\0';
    // Genero Indice HashTable
    int indice = (((*users->hash_function)(username)) % (users->nbuckets))/4;
    // Recupero eventualmente l'utente registarato
    pthread_mutex_lock(&mutex_hashTable[indice]);
    utente *usr = icl_hash_find(users,username);
    pthread_mutex_unlock(&mutex_hashTable[indice]);
    if(usr == NULL){// Se non registrato
    	// Setto reponso
        setHeader(&response->hdr, OP_NICK_UNKNOWN, msg->hdr.sender);
        // Invio Responso
        pthread_mutex_lock(&mutex_socket[client%MAXFDSOCK]);
        sendHeader(client,&response->hdr);
        pthread_mutex_unlock(&mutex_socket[client%MAXFDSOCK]);
        // Aggiorno statistica
        pthread_mutex_lock(&mutex_statistica);
        chattyStats.nerrors++;
        pthread_mutex_unlock(&mutex_statistica);
    }else{// Se registrato 
        if(isOn != 1){// Se non ancora online
        	// Aggiungo lista online
            aggiungiUser(usr->nome);
            // Setto in hash table il nuovo fd
            pthread_mutex_lock(&mutex_hashTable[indice]);
            usr->client_fd = client;
            pthread_mutex_unlock(&mutex_hashTable[indice]);
        }
        // Invio lista utenti connessi
        userList(client,username);
    }
    // Libero Memoria
    free(username);
    free(response);
}

static inline void inviaMsg(message_t *msg, int client, int toAll){
	// Descrittore destinatario
    int fd_receiver = 0;
    // Messaggio di risposta a mittente
    message_t *response = calloc(1,sizeof(message_t));
    // Recupero chiave HashTable destinatario
    size_t receiver_len = strlen(msg->data.hdr.receiver);
    char* receiver_name = calloc(receiver_len+1, sizeof(char));
    memcpy(receiver_name,msg->data.hdr.receiver,receiver_len);
    receiver_name[receiver_len]='\0';
    // Recupero indice HashTable destinatario
    int indice_receiver = (((*users->hash_function)(receiver_name)) % (users->nbuckets))/4;
    // Check lunghezza msg
    if(msg->data.hdr.len > setupped->MaxMsgSize){
    	// Aggiorno statistica
        pthread_mutex_lock(&mutex_statistica);
        chattyStats.nerrors++;
        pthread_mutex_unlock(&mutex_statistica);
        // Setto HDR risposta mittente
        setHeader(&response->hdr, OP_MSG_TOOLONG, "Server");
        // Invio responso a mittente
        pthread_mutex_lock(&(mutex_socket[client%MAXFDSOCK]));
        sendHeader(client,&response->hdr);
        pthread_mutex_unlock(&(mutex_socket[client%MAXFDSOCK]));
        // Libero memoria
        free(msg->data.buf);
        free(msg);
    }else{// Lunghezza messaggio ok
        // Cerco il FD destinatario.
        pthread_mutex_lock(&mutex_hashTable[indice_receiver]);
        utente *receiver = ((utente*)icl_hash_find(users,receiver_name));
        if(receiver!=NULL)
            fd_receiver = receiver->client_fd;
        else// Il destinatario non esiste
            fd_receiver = -2;
        pthread_mutex_unlock(&mutex_hashTable[indice_receiver]);
        // Controllo che destinatario e mittente siano diversi
        if((strlen(msg->data.hdr.receiver) == strlen(msg->hdr.sender)) &&
           memcmp(msg->data.hdr.receiver,msg->hdr.sender,strlen(msg->hdr.sender)) == 0)
            fd_receiver = -2;
        if(fd_receiver >0){//Destinatario online
        	 // Se non sto inviando a tutti(TXTALL) rispondo a mittente
            if(toAll==0) {
                setHeader(&response->hdr, OP_OK, "Server");
                pthread_mutex_lock(&(mutex_socket[client % MAXFDSOCK]));
                sendHeader(client, &response->hdr);
                pthread_mutex_unlock(&(mutex_socket[client % MAXFDSOCK]));
            }
            // Imposto op del messaggio da recapitare
            msg->hdr.op = TXT_MESSAGE;
            // Invio effettivo del msg al destinatario online
            pthread_mutex_lock(&(mutex_socket[fd_receiver%MAXFDSOCK]));
            sendRequest(fd_receiver, msg);
            pthread_mutex_unlock(&(mutex_socket[fd_receiver%MAXFDSOCK]));
            // Aggiorno statistica
            pthread_mutex_lock(&mutex_statistica);
            chattyStats.ndelivered++;
            pthread_mutex_unlock(&mutex_statistica);
            // Libero memoria
            free(msg->data.buf);
            free(msg);
        }else if(fd_receiver == -1){//Destinatario OFFLINE
	        // Aggiungo alla HST destinatario
          	pthread_mutex_lock(&mutex_hashTable[indice_receiver]);
            if(receiver->current < setupped->MaxHistMsgs){
                // Salvo in history
                receiver->history[receiver->current] = msg;
                receiver->current = ((receiver->current+1)%setupped->MaxHistMsgs);
                if(receiver->testa == -1)
                    receiver->testa = ((receiver->testa+1)%setupped->MaxHistMsgs);
                pthread_mutex_unlock(&mutex_hashTable[indice_receiver]);
                // Se non sto inviando a tutti(TXTALL) rispondo a mittente
                if(toAll==0) {
                    setHeader(&response->hdr, OP_OK, "Server");
                    pthread_mutex_lock(&(mutex_socket[client % MAXFDSOCK]));
                    sendHeader(client, &response->hdr);
                    pthread_mutex_unlock(&(mutex_socket[client % MAXFDSOCK]));
                }
                // Aggiorno statistica
		        pthread_mutex_lock(&mutex_statistica);
		        chattyStats.nnotdelivered++;
		        pthread_mutex_unlock(&mutex_statistica);
            }
            else{ // History Piena
                pthread_mutex_unlock(&mutex_hashTable[indice_receiver]);
                 // Se non sto inviando a tutti(TXTALL) rispondo a mittente
                if(toAll==0){
                    setHeader(&response->hdr, OP_FAIL, "Server");
                    pthread_mutex_lock(&(mutex_socket[client % MAXFDSOCK]));
                    sendHeader(client, &response->hdr);
                    pthread_mutex_unlock(&(mutex_socket[client % MAXFDSOCK]));
                }
                // Aggiorno statistica
                pthread_mutex_lock(&mutex_statistica);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&mutex_statistica);
                // Libero Memoria
                free(msg->data.buf);
                free(msg);
            }
        }else{//Destinatario non registrato
        	 // Se non sto inviando a tutti(TXTALL) rispondo a mittente
            if(toAll==0){
                setHeader(&response->hdr, OP_NICK_UNKNOWN, "Server");
                pthread_mutex_lock(&(mutex_socket[client % MAXFDSOCK]));
                sendHeader(client, &response->hdr);
                pthread_mutex_unlock(&(mutex_socket[client % MAXFDSOCK]));
            }
            // Aggiorno statistica
            pthread_mutex_lock(&mutex_statistica);
            chattyStats.nerrors++;
            pthread_mutex_unlock(&mutex_statistica);
            // Libero memoria
            free(msg->data.buf);
            free(msg);
        }
    }
    // Libero memoria
    free(response);
    free(receiver_name);
}

static inline void inviaMsgToAll(message_t* msg, int client){
    int done = 0;
    message_t *response = calloc(1,sizeof(message_t));
    // Ricavo Key del mittente per hashtable
    size_t username_len = strlen(msg->hdr.sender);
    char* username = calloc(username_len+1, sizeof(char));
    memcpy(username,msg->hdr.sender,username_len);
    username[username_len]='\0';
    // Controllo prima di ciclare se messaggio va bene
    if(msg->data.hdr.len > setupped->MaxMsgSize){
    	// Aggiorno statistica
        pthread_mutex_lock(&mutex_statistica);
        chattyStats.nerrors++;
        pthread_mutex_unlock(&mutex_statistica);
        // Setto risposta al mittente
        setHeader(&response->hdr, OP_MSG_TOOLONG, "Server");
        // Invio effettivo risposta
        pthread_mutex_lock(&mutex_socket[client%MAXFDSOCK]);
        sendHeader(client,&response->hdr);
        pthread_mutex_unlock(&mutex_socket[client%MAXFDSOCK]);	
    }else{// Length ok
    	// Recupero ogni utente hash table
        icl_entry_t *bucket, *curr;
        for (int i=0; i<users->nbuckets; i++) {
            bucket = users->buckets[i];
            for (curr=bucket; curr!=NULL; curr=curr->next) {
            	// Utente potenziale destinatario
                utente *receiver = (utente*)curr->data;
                // Controllo che destiantario e mittente siano diveri
                if(strcmp(receiver->nome,username)!=0){
                	// Alloco un nuovo msg per ogni utente
                    message_t *broadcast = calloc(1,sizeof(message_t));
                    // Setto il msg che in caso verra conservato
                    memcpy(broadcast->hdr.sender,msg->hdr.sender,MAX_NAME_LENGTH+1);
                    broadcast->hdr.op = POSTTXT_OP;
                    broadcast->data.hdr.len = msg->data.hdr.len;
                    memcpy(broadcast->data.hdr.receiver,receiver->nome,MAX_NAME_LENGTH+1);
                    broadcast->data.buf = calloc(msg->data.hdr.len,sizeof(char));
                    memcpy(broadcast->data.buf,msg->data.buf,msg->data.hdr.len);
                    // Delego a inviaMsg la gestione dell'invio e la pulizia memoria del msg
                    inviaMsg(broadcast,client,1);
                    // Almeno un destinatario
                    done++;
                }
            }
        }
	    if(done==0){//Nessun utente oltre sender
	    	// Aggiorno statistica
	        pthread_mutex_lock(&mutex_statistica);
	        chattyStats.nerrors++;
	        pthread_mutex_unlock(&mutex_statistica);
	        // Setto risposta a mittente
	        setHeader(&response->hdr, OP_FAIL, "Server");
	    }else
	        setHeader(&response->hdr, OP_OK, "Server");
	    // Invio effettivo responso
	    pthread_mutex_lock(&mutex_socket[client%MAXFDSOCK]);
	    sendHeader(client,&response->hdr);
	    pthread_mutex_unlock(&mutex_socket[client%MAXFDSOCK]);
	}
    // Libero memoria
    free(msg->data.buf);
    free(msg);
    free(response);
    free(username);
}

static inline void inviaFile(message_t *msg, int client){
	// Instanzio risposta
    message_t *response = calloc(1,sizeof(message_t));
    // Recupero chiave destinatario
    size_t receiver_len = strlen(msg->data.hdr.receiver);
    char* receiver = calloc(receiver_len+1, sizeof(char));
    memcpy(receiver,msg->data.hdr.receiver,receiver_len);
    receiver[receiver_len]='\0';
    // Indice destinatario HashTable
    int indice_receiver = (((*users->hash_function)(receiver)) % (users->nbuckets))/4;
    // Controllo se receiver esiste
    pthread_mutex_lock(&mutex_hashTable[indice_receiver]);
    utente *usr = icl_hash_find(users,receiver);
    if(usr == NULL){// Destinatario non esistente
        pthread_mutex_unlock(&mutex_hashTable[indice_receiver]);
        // Setto responso
        setHeader(&response->hdr, OP_NICK_UNKNOWN, "Server");
        // Invio responso a mittente
        pthread_mutex_lock(&(mutex_socket[client%MAXFDSOCK]));
        sendHeader(client,&response->hdr);
        pthread_mutex_unlock(&(mutex_socket[client%MAXFDSOCK]));
        // Aggiorno statistica
        pthread_mutex_lock(&mutex_statistica);
        chattyStats.nerrors++;
        pthread_mutex_unlock(&mutex_statistica);
        // Libero memoria
        free(msg->data.buf);
        free(msg);
        free(receiver);
        free(response);
        return;
    }
    pthread_mutex_unlock(&mutex_hashTable[indice_receiver]);
    // Mi prepraro a ricevere il messaggio contenente la mappatura del file
    message_data_t *file = calloc(1,sizeof(message_data_t));
    if( readData(client, file) <= 0){
    	// Libero memoria ed esco
        perror("Problema: ");
        free(msg->data.buf);
        free(msg);
        free(file);
        free(receiver);
        free(response);
        return;
    }
    if(file->hdr.len>0){
        // Check length 1024 = 1KByte
        printf("File size %d on %d\n",(file->hdr.len)/1024,setupped->MaxFileSize );
        if((file->hdr.len)/1024 >= setupped->MaxFileSize){ 
        	// File troppo lungo
        	// Setto responso
            setHeader(&response->hdr, OP_MSG_TOOLONG, "Server");
            // Invio responso
            pthread_mutex_lock(&(mutex_socket[client%MAXFDSOCK]));
            sendHeader(client,&response->hdr);
            pthread_mutex_unlock(&(mutex_socket[client%MAXFDSOCK]));
          	// Aggiorno statistica
            pthread_mutex_lock(&mutex_statistica);
            chattyStats.nerrors++;
            pthread_mutex_unlock(&mutex_statistica);
            // Libero memoria
            free(msg->data.buf);
            free(msg);
        }
        else{// Lunghezza ok
        	// Aggiorno statistica
            pthread_mutex_lock(&mutex_statistica);
            chattyStats.nfilenotdelivered++;
            pthread_mutex_unlock(&mutex_statistica);
            // Var per lunghezza stringa directory
            size_t len = strlen(setupped->DirName);
            size_t filename_size = strlen(msg->data.buf);
            // +1 per terminatore +1 per slash
            char* directory = calloc(len+filename_size+1+1,sizeof(char));
            memcpy(directory,setupped->DirName,len);
            directory[len] = '\0';
            char *path;
            // Cerco prima occorrenza sotto stringa /
            if((path=strstr(msg->data.buf,"/"))==NULL){
            	// / non presente lo aggiungo e concateno il nomefile
                strncat(directory,"/",1);
                strncat(directory,msg->data.buf,filename_size);
            }else// Slash trovato concateno la sottostringa
                strncat(directory,path,strlen(path)+1);
            // Apro/Creo il file
            int file_fd;
            if((file_fd = open(directory, O_RDWR|O_CREAT, 0777)) == -1){
                perror("Aprendo il file");
                free(file);
                free(directory);
                free(msg->data.buf);
                free(msg);
                free(receiver);
                free(response);
                return;
            }
            // Setto size file
            ftruncate(file_fd, file->hdr.len);
            // Creo mappa file
            char *dest = mmap(NULL,file->hdr.len,PROT_READ | PROT_WRITE, MAP_SHARED, file_fd, 0);
            // Copio la mappa che sta nel buffer del msg nella mappa appena creata
            memcpy(dest,file->buf,file->hdr.len);
            // Cancello messaggio ricevuto
            free(file->buf);
            // Recupero FD destinatario
            pthread_mutex_lock(&mutex_hashTable[indice_receiver]);
            int receiver_fd = usr->client_fd;
            pthread_mutex_unlock(&mutex_hashTable[indice_receiver]);

            if(receiver_fd != -1){//ONLINE
            	// Setto risposta mittente
                setHeader(&response->hdr, OP_OK, "Server");
                // Invio risposta mittente
                pthread_mutex_lock(&(mutex_socket[client%MAXFDSOCK]));
                sendHeader(client,&response->hdr);
                pthread_mutex_unlock(&(mutex_socket[client%MAXFDSOCK]));
                // Preparo il messaggio per destinatario
                msg->hdr.op = FILE_MESSAGE;
                // Invio messaggio destinatario
                pthread_mutex_lock(&(mutex_socket[receiver_fd%MAXFDSOCK]));
                sendRequest(receiver_fd, msg);
                pthread_mutex_unlock(&(mutex_socket[receiver_fd%MAXFDSOCK]));
                // NON Aggiorno statistica
                // Libero memoria
                free(msg->data.buf);
                free(msg);
            }else{ // NON ONLINE
                // Aggiungo alla history
                pthread_mutex_lock(&mutex_hashTable[indice_receiver]);
                if(usr->current < setupped->MaxHistMsgs){
                    // Salvo in history
                    usr->history[usr->current] = msg;
                    usr->current = ((usr->current+1) % setupped->MaxHistMsgs);
                    if(usr->testa == -1)
                        usr->testa = ((usr->testa+1) % setupped->MaxHistMsgs);
                    pthread_mutex_unlock(&mutex_hashTable[indice_receiver]);
                    // Setto ed invio rispsota al mittente
                    setHeader(&response->hdr, OP_OK, "Server");
                    pthread_mutex_lock(&(mutex_socket[client%MAXFDSOCK]));
                    sendHeader(client,&response->hdr);
                    pthread_mutex_unlock(&(mutex_socket[client%MAXFDSOCK]));
                    // NON Aggiorno statistica
                }
                else{ // History Piena
                    pthread_mutex_unlock(&mutex_hashTable[indice_receiver]);
                    // Setto risposta mittente
                    setHeader(&response->hdr, OP_FAIL, "Server");
                    // Invio risposta mittente
                    pthread_mutex_lock(&(mutex_socket[client%MAXFDSOCK]));
                    sendHeader(client,&response->hdr);
                    pthread_mutex_unlock(&(mutex_socket[client%MAXFDSOCK]));
                    // Aggiorno statistica
                    pthread_mutex_lock(&mutex_statistica);
                    chattyStats.nerrors++;
                    pthread_mutex_unlock(&mutex_statistica);
                    // Libero memoria
                    free(msg->data.buf);
                    free(msg);
                }
            }
            // Chiudo il file
            close(file_fd);
            // Libero memoria
            free(directory);
        }
    }else{ // Length <= 0
    	// Aggiorno statistica
        pthread_mutex_lock(&mutex_statistica);
        chattyStats.nerrors++;
        pthread_mutex_unlock(&mutex_statistica);
        // Setto responso mittente
        setHeader(&response->hdr, OP_FAIL, "Server");
        // Invio responso mittente
        pthread_mutex_lock(&(mutex_socket[client%MAXFDSOCK]));
        sendHeader(client,&response->hdr);
        pthread_mutex_unlock(&(mutex_socket[client%MAXFDSOCK]));
        // Libero memoria
        free(msg->data.buf);
        free(msg);
    }
    free(receiver);
    free(response);
    free(file);
}

static inline void ottieniFile(message_t *msg, int client){
    FILE* file_copy;
    FILE* file;
    // Instanzio responso mittente
    message_t *response = calloc(1,sizeof(message_t));
    // Prepraro per leggere dal file (COME IN inviaFile)
    size_t len = strlen(setupped->DirName);
    size_t filename_size = strlen(msg->data.buf);
    char* fileIN = calloc(len+filename_size+1+1,sizeof(char));
    memcpy(fileIN,setupped->DirName,len);
    fileIN[len] = '\0';
    char *path;
    if((path=strstr(msg->data.buf,"/"))==NULL){
        strncat(fileIN,"/",1);
        strncat(fileIN,msg->data.buf,filename_size);
    }else
        strncat(fileIN,path,strlen(path)+1);
    struct stat st;
    /* Recupero informazioni sul file da inviare
    	Mi assicuro che esista */
    if(stat(fileIN, &st) != -1){
        // Apro file da leggere
        if((file = fopen(fileIN, "r")) == NULL ) {
            perror("Apertura file lettura");
            free(fileIN);
            free(response);
            return;
        }
        // Apro/Creo/Sovrascrivo il file in cui scrivere
        if((file_copy = fopen(msg->data.buf, "w+")) == NULL ) {
            perror("File scrittura");
            free(fileIN);
            free(response);
            fclose(file);
            return;
        }
        // Buffer di supporto
        char* buffer = calloc(256,sizeof(char));
        //Copia effettiva
        while(1) {
            fgets(buffer, 256, file);
            if(feof(file))
                break;
            fputs(buffer, file_copy);
        }
        /* chiude i file */
        fclose(file_copy);
        fclose(file);
        // Setta il messaggio contenente la risposta
        setData(&response->data,msg->data.hdr.receiver,msg->data.buf,msg->data.hdr.len);
        setHeader(&response->hdr, OP_OK, "Server");
        // Invia risposta
        pthread_mutex_lock(&mutex_socket[client%MAXFDSOCK]);
        sendRequest(client,response);
        pthread_mutex_unlock(&mutex_socket[client%MAXFDSOCK]);
        // Aggiorno Statistica
        pthread_mutex_lock(&mutex_statistica);
        chattyStats.nfiledelivered++;
        pthread_mutex_unlock(&mutex_statistica);
        // Libero memoria
        free(buffer);
    }else{// In caso il file non fosse piu' presente
        perror("File non piu presente nel server");
        // Setto risposta
        setHeader(&response->hdr, OP_NO_SUCH_FILE, "Server");
        setData(&response->data, msg->hdr.sender, NULL, 0);
        // Invia risposta
        pthread_mutex_lock(&mutex_socket[client%MAXFDSOCK]);
        sendRequest(client,response);
        pthread_mutex_unlock(&mutex_socket[client%MAXFDSOCK]);
        // Aggiorno statistica
        pthread_mutex_lock(&mutex_statistica);
        chattyStats.nerrors++;
        pthread_mutex_unlock(&mutex_statistica);
        // Libero memoria
        free(msg->data.buf);
        free(msg);
    }
    // Libero memoria
    free(fileIN);
    free(response);
}

static inline void ottieniHistory(message_t *msg, int client){
	// Recupero chiave mittente
    size_t username_len = strlen(msg->hdr.sender);
    char* username = calloc(username_len+1, sizeof(char));
    memcpy(username,msg->hdr.sender,username_len);
    username[username_len]='\0';
    // Recuper indice HashTable
    int indice = (((*users->hash_function)(username)) % (users->nbuckets))/4;
    pthread_mutex_lock(&mutex_hashTable[indice]);
    utente *usr = icl_hash_find(users,username);
    pthread_mutex_unlock(&mutex_hashTable[indice]);
    // Instanzio risposta mittente
    message_t *response = calloc(1,sizeof(message_t));
    // Setto OP_OK e numero di messaggi da leggere
    size_t n_msg =  (size_t)( usr->testa == -1 ? 0 : usr->current - usr->testa);
    setHeader(&response->hdr, OP_OK, "Server");
    setData(&(response->data), msg->hdr.sender,(char*) &n_msg, sizeof(size_t*));
    // Invio effettivo
    pthread_mutex_lock(&(mutex_socket[client%MAXFDSOCK]));
    sendRequest(client, response);
    pthread_mutex_unlock(&(mutex_socket[client%MAXFDSOCK]));
    // Libeor memoria
    free(response);
    free(username);
    // Invio dei messaggi
    pthread_mutex_lock(&mutex_hashTable[indice]);
    if(n_msg>0){
        message_t *hst;
        // Per ogni messaggio in history
        for(int i=0;i<n_msg; i++,usr->testa++){
            hst = usr->history[usr->testa];
            // Aggiorno statistica
            pthread_mutex_lock(&mutex_statistica);
            chattyStats.ndelivered++;
            pthread_mutex_unlock(&mutex_statistica);
            // Se messaggio testuale
            if(hst->hdr.op==21 || hst->hdr.op==2){
                    pthread_mutex_lock(&(mutex_socket[client%MAXFDSOCK]));
                    sendRequest(client, hst);
                    pthread_mutex_unlock(&(mutex_socket[client%MAXFDSOCK]));
            }
            else if(hst->hdr.op==22 || hst->hdr.op==4){
            	// E' un file
                hst->hdr.op=FILE_MESSAGE;
                pthread_mutex_lock(&(mutex_socket[client%MAXFDSOCK]));
                sendRequest(client, hst);
                pthread_mutex_unlock(&(mutex_socket[client%MAXFDSOCK]));
            }
            // Libero memoria
            free(hst->data.buf);
            free(hst);
        }
        // Resetto history messaggi utente
        usr->testa = -1;
        usr->current = 0;
    }
    pthread_mutex_unlock(&mutex_hashTable[indice]);
}

static inline void userList(int client, char* sender){
	// Risposta da inviare
    message_t *response = calloc(1,sizeof(message_t));
    char *str = getConnectedString();
    // Devo accedere alla statistica per settare DATAHDR risposta
    pthread_mutex_lock(&mutex_statistica);
    setData(&response->data, sender, str, (unsigned int) (chattyStats.nonline * (MAX_NAME_LENGTH + 1)));
    pthread_mutex_unlock(&mutex_statistica);
    setHeader(&response->hdr, OP_OK, "Server");
    // Invio effettivo lista utenti connessi
    pthread_mutex_lock(&mutex_socket[client%MAXFDSOCK]);
    sendRequest(client,response);
    pthread_mutex_unlock(&mutex_socket[client%MAXFDSOCK]);
    // Libero Memoria
    free(str);
    free(response);
}

static inline void deRegistrazione(char* user, int client){
	// Alloco risposta mittente
    message_t *response = calloc(1,sizeof(message_t));
    // Ottengo username da cercare
    size_t username_len = strlen(user);
    char* username = calloc(username_len+1, sizeof(char));
    memcpy(username,user,username_len);
    username[username_len]='\0';
    // Indice per lock hashtable
    int indice = hash_pjw(username) % MAX_HASH;
    //Acquisisco lock HashTable
    pthread_mutex_lock(&mutex_hashTable[indice]);
    if((icl_hash_find(users,username))== NULL){// Se non e' registrato
    	pthread_mutex_unlock(&mutex_hashTable[indice]);
    	// Risposta da inviare
        setHeader(&response->hdr, OP_NICK_UNKNOWN, "Server");
        // Aggiorno statistica
        pthread_mutex_lock(&mutex_statistica);
        chattyStats.nerrors++;
        pthread_mutex_unlock(&mutex_statistica);
    }else{// Utente registrato
    	pthread_mutex_unlock(&mutex_hashTable[indice]);
    	// Controllo se online
    	if(isOnline(username)){// ONLINE
	        // Rimuovo da hashTabke e da lista utenti online
	        pthread_mutex_lock(&mutex_hashTable[indice]);
	        icl_hash_delete(users,username,free, freeUser);
	        pthread_mutex_unlock(&mutex_hashTable[indice]);
	        rimuoviUser(username);
	    	// Risposta da inviare
	        setHeader(&response->hdr, OP_OK, "Server");
	        // Aggiorno statistica
	        pthread_mutex_lock(&mutex_statistica);
	        chattyStats.nusers--;
	        pthread_mutex_unlock(&mutex_statistica);
    	}else{	// OFFLINE
    		// Risposta da inviare
    		setHeader(&response->hdr, OP_FAIL, "Server");
    		// Aggiorno statistica
            pthread_mutex_lock(&mutex_statistica);
            chattyStats.nerrors++;
            pthread_mutex_unlock(&mutex_statistica);
    	}        
	}
	// Invio effettivo risposta
    pthread_mutex_lock(&mutex_socket[client%MAXFDSOCK]);
    sendHeader(client,&response->hdr);
    pthread_mutex_unlock(&mutex_socket[client%MAXFDSOCK]);
    // Libero memoria
    free(response);
    free(username);
}

static inline int disconnetti(int client){
	// Cerco in hashTable l'utente con questo FD
    icl_entry_t *bucket, *curr;
    utente *current;
    int i;
    for(i=0; i<users->nbuckets; i++) {
        bucket = users->buckets[i];
        pthread_mutex_lock(&mutex_hashTable[i/4]);
        for(curr=bucket; curr!=NULL; curr=curr->next ) {
            current = (utente*) curr->data;
            if(client == current->client_fd){// TROVATO
            	// Setto utente offlien
                current->client_fd = -1;
                // Rimuovo da lista utenti online
                rimuoviUser(current->nome);
                pthread_mutex_unlock(&mutex_hashTable[i/4]);
                // FINITO
                return 0;
            }
        }
        pthread_mutex_unlock(&mutex_hashTable[i/4]);
    }
    return -1;
}
/* Se trovato ritorna 1(TRUE) altrimenti 0*/
int isOnline(char *name) {
	// Controllo nella lista utente online
    user *ptr = connectedUser;
    pthread_mutex_lock(&mutex_online);
    while(ptr != NULL){
        if(strncmp(ptr->name, name, MAX_NAME_LENGTH) == 0) {
            pthread_mutex_unlock(&mutex_online);
            // Trovato esito positivo ritorno 1
            return 1;
        }
        ptr = ptr->next;
    }
    pthread_mutex_unlock(&mutex_online);
    return 0;
}
/* Preleva dal vettore circolare (-1) se nessuno da servira*/
int prelevaFD(){
	// FD non valido
    int client = -1;
    pthread_mutex_lock(&mutex_daServire);
    if(testa!=-1) {
    	// Trovato un "daServire"
        client = toServe[testa];
        testa = (testa + 1) % (setupped->MaxConnections);
        // Lista vuota dopo prelievo
        if (testa == current) {
            testa = -1;
            current = 0;
        }
    }
    pthread_mutex_unlock(&mutex_daServire);
    // Ritorna eventuale FD da servire
    return client;
}
// Free necessarie per elemento di HashTable
void freeUser(void *u1){
	// Libera memoria struttura dati utente di HT
	utente *u = (utente*) u1;
	// Per ogni messaggio in History
    for(int i=u->testa;i<(u->current) && u->testa>-1;i++){
        if(u->history[i] != NULL){
        	// Libera msg in hst
            message_t* toFree = (u->history)[i];
            if(toFree->data.buf != NULL)
                free(toFree->data.buf);
            free(toFree);
        }
    }
    // Libera memoria
    free(u->history);
    free(u->nome);
    free(u);
}
// Aggiunge utente con alla lista 
void aggiungiUser(char *s){
    // Aggiorna la statistica
    pthread_mutex_lock(&mutex_statistica);
    ++chattyStats.nonline;
    pthread_mutex_unlock(&mutex_statistica);
    // Crea il nuovo user online
    pthread_mutex_lock(&mutex_online);
    user* nuovo = calloc(1,sizeof(user));
    memcpy(nuovo->name,s,MAX_NAME_LENGTH);
    nuovo->next = NULL;
    nuovo->prev = NULL;
    // Se nessun utente online
    if(connectedUser == NULL)
        connectedUser = nuovo;
    else {
    	// Aggiunge in testa
    	connectedUser->prev = nuovo;
    	nuovo->next = connectedUser;
    	connectedUser = nuovo;
    }
    pthread_mutex_unlock(&mutex_online);
}
// Cancella tutta la lista di utenti online
void pulisciConnected(){
    // Rimuove tutti i connected user
    user* ptr = connectedUser;
    pthread_mutex_lock(&mutex_online);
    // Lista vuota
    if(connectedUser==NULL){
        pthread_mutex_unlock(&mutex_online);
        return;
    }
    // Piu di un elemento
    while(ptr->next != NULL ){
        user *tmp = ptr;
        ptr = ptr->next;
        free(tmp);
    }
    free(ptr);
    pthread_mutex_unlock(&mutex_online);
}
// Rimuove utente dalla lista
void rimuoviUser(char *s) {
    pthread_mutex_lock(&mutex_online);
    // Nessun utente online
    if(connectedUser == NULL){
        pthread_mutex_unlock(&mutex_online);
        return;
    }
    // Aggiorna la statistica
    pthread_mutex_lock(&mutex_statistica);
    chattyStats.nonline--;
    pthread_mutex_unlock(&mutex_statistica);
    // Rimuove dalla lista
    user *ptr = connectedUser;
    if(strncmp(ptr->name, s, MAX_NAME_LENGTH) == 0){
    	//Trovato in testa, sposto testa
        connectedUser = ptr->next;
        if(connectedUser!=NULL)
            connectedUser->prev = NULL;
        // Libero memoria
        free(ptr);
    }else{
    	// Scorro la rimanente(oltre la testa) lista 
        ptr = ptr->next;
        while(ptr != NULL){
            if(strncmp(ptr->name, s, MAX_NAME_LENGTH) == 0) {
                ptr->prev->next = ptr->next;
                if(ptr->next != NULL)
                    ptr->next->prev = ptr->prev;
                free(ptr);
                pthread_mutex_unlock(&mutex_online);
                return;
            }
            ptr = ptr->next;
        }
    }
    pthread_mutex_unlock(&mutex_online);
}
// Ritorna stringa di utenti online
char* getConnectedString(){
	// Creo stringa di utenti online
    int max = setupped->MaxConnections;
    int i = 0 , onesize = MAX_NAME_LENGTH+1;
    user *ptr = connectedUser;
    char *str = calloc((size_t) onesize*max,sizeof(char));
    pthread_mutex_lock(&mutex_online);
    // Per ogni utente nella lista copio il nome nella stringa
    while(ptr != NULL){
        memcpy(&str[i*onesize],ptr->name,MAX_NAME_LENGTH);
        i++;
        str[i*MAX_NAME_LENGTH] = '\0';
        ptr = ptr->next;
    }
    pthread_mutex_unlock(&mutex_online);
    // Restituisco stringa
    return str;
}
//gcc -std=c99 -g -L . -I . -Wall chatty.c connections.c listener.c icl_hash.c -o chatty -lpthread
//valgrind --leak-check=full ./chatty -f DATA/chatty.conf1