/*
 * membox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */
/** \file stuffs.h  
    \author Pietro Libro 545559
    Si dichiara che il contenuto di questo file e' in ogni sua parte opera  
    originale dell'autore  
*/
#include "message.h"
#include <pthread.h>
// Pipe per comunicazione Task-Listener
int pipeFD[2];
// Mutex e Cond per Gestione sync
pthread_mutex_t mutex_signal;
pthread_mutex_t mutex_daServire;
pthread_mutex_t mutex_pipe;
pthread_mutex_t mutex_hashTable[MAX_HASH];
pthread_mutex_t mutex_socket[MAXFDSOCK];
pthread_cond_t interrupt_signal;
// Variabile per terminazione
int serverRun;
//Vettore circolare di socket connessi
int *toServe;
int current,testa;
// Firma Listener
void * serverListen(void *param);
// Struttura dati per settaggi da file di configurazione
struct setting{
    char *UnixPath;
    char *DirName;
    char *StatFileName;
    int MaxConnections;
    int ThreadsInPool;
    int MaxMsgSize;
    int MaxFileSize;
    int MaxHistMsgs;
};
// Istanziazione condivisa
struct setting *setupped;
// Struttura gestione utente in HashTable
typedef struct client_struct{
    char *nome;
    int client_fd;
    message_t **history;
    int current;
    int testa;
}utente;
// Struttura doppiamente linkata per Utente Online
typedef struct online_user{
    char name[MAX_NAME_LENGTH];
    struct online_user* prev;
    struct online_user* next;
}user;
