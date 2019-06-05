/*
 * membox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */
/** \file connections.c  
    \author Pietro Libro 545559
    Si dichiara che il contenuto di questo file e' in ogni sua parte opera  
    originale dell'autore  
*/
#include "message.h"
#include "connections.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
/**
 * @file  connection.c
 * @brief Contiene le funzioni che implementano il protocollo
 *        tra i clients ed il server
 */
/**
* @function openConnection
* @brief Apre una connessione AF_UNIX verso il server
*
* @param path Path del socket AF_UNIX
* @param ntimes numero massimo di tentativi di retry
* @param secs tempo di attesa tra due retry consecutive
*
* @return il descrittore associato alla connessione in caso di successo
*         -1 in caso di errore
*/
int openConnection(char* path, unsigned int ntimes, unsigned int secs){
	int fdClient;
	struct sockaddr_un server;
	server.sun_family = AF_UNIX;
	strncpy(server.sun_path, path, UNIX_PATH_MAX);

	fdClient = socket(AF_UNIX, SOCK_STREAM, 0);
	int i=0;
	for(i=0;i<ntimes;i++){
		if(connect(fdClient,(struct sockaddr *) &server, sizeof(server))==0){
    		return fdClient;
    	}else
   			sleep(secs);
  	}
	perror("Impossibile aprire connessione");
	return -1;
}
// -------- server side -----
/**
 * @function readHeader
 * @brief Legge l'header del messaggio
 *
 * @param fd     descrittore della connessione
 * @param hdr    puntatore all'header del messaggio da ricevere
 *
 * @return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa)
 */
int readHeader(long connfd, message_hdr_t *hdr){
	if(connfd < 3){
		errno = ENOTCONN;
    	return -1;
	}
	if(hdr == NULL){
		errno = EINVAL;
		return -1;
	}
	ssize_t letti = 0;
	while((letti += read((int)connfd,&hdr->op,sizeof(op_t))) != sizeof(op_t)){
		if(letti==0)
			return 0;
		else if(letti == -1) {
	        errno = ENOTCONN;
			return -1;
		}
	}
	letti = 0;
	while((letti += read((int)connfd,&hdr->sender,MAX_NAME_LENGTH+1)) != (MAX_NAME_LENGTH+1)){
		if(letti == 0)
			return 0;
		if(letti == -1) {
	        errno = ENOTCONN;
	        return -1;
	    }
	}
	return 1;
}
/**
 * @function readData
 * @brief Legge il body del messaggio
 *
 * @param fd     descrittore della connessione
 * @param data   puntatore al body del messaggio
 *
 * @return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa)
 */
int readData(long fd, message_data_t *data){
	if(fd<3){
		errno = ENOTCONN;
		return -1;
	}
	if(data==NULL){
		errno = EINVAL;
		return -1;
	}
	ssize_t letti = 0;
	while((letti += read((int) fd,&(data->hdr.receiver),MAX_NAME_LENGTH+1)) != MAX_NAME_LENGTH+1){
		if(letti==0)
			return 0;
		if(letti == -1){
			errno = ENOTCONN;
			return -1;
		}
	}
	letti = 0;
	while((letti += read((int) fd,&(data->hdr.len),sizeof(unsigned int))) != sizeof(unsigned int)){
		if(letti==0)
			return 0;
		if(letti == -1){
			errno = ENOTCONN;
			return -1;
		}
	}
    size_t lunghezza = data->hdr.len;
    if(lunghezza==0)
		return 1;
	data->buf = calloc(lunghezza,sizeof(char));
	char *bufptr=data->buf;
	while(lunghezza>0){
    	ssize_t letto = read((int)fd,bufptr,lunghezza);
    	if(letto <= 0){
			errno = ENODATA;
			return -1;
		}
		lunghezza -= letto;
		bufptr += letto;
	}
	return 1;
}

/**
 * @function readMsg
 * @brief Legge l'intero messaggio
 *
 * @param fd     descrittore della connessione
 * @param data   puntatore al messaggio
 *
 * @return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa)
 */
int readMsg(long fd, message_t *msg){
	if(fd<3){
		errno = ENOTCONN;
		return -1;
	}
	if(msg==NULL){
		errno = EINVAL;
		return -1;
	}
	int result;
	if((result = readHeader(fd,&msg->hdr))<1)
		return result;
	return readData(fd,&msg->data);
}

/* da completare da parte dello studente con altri metodi di interfaccia */


// ------- client side ------
/**
 * @function sendRequest
 * @brief Invia un messaggio di richiesta al server 
 *
 * @param fd     descrittore della connessione
 * @param msg    puntatore al messaggio da inviare
 *
 * @return <=0 se c'e' stato un errore
 */
int sendRequest(long fd, message_t *msg){
	if(fd<3){
		errno = ENOTCONN;
		return -1;
	}
	if(msg==NULL){
		errno = EINVAL;
		return -1;
	}
	int result;
	if((result = sendHeader(fd,&msg->hdr))<1)
		return result;
	return sendData(fd,&msg->data);
}

/**
 * @function sendData
 * @brief Invia il body del messaggio al server
 *
 * @param fd     descrittore della connessione
 * @param msg    puntatore al messaggio da inviare
 *
 * @return <=0 se c'e' stato un errore
 */
int sendData(long fd, message_data_t *msg){
	if(fd<3){
		errno = ENOTCONN;
		return -1;
	}
	if(msg==NULL){
		errno = EINVAL;
		return -1;
	}
	size_t lunghezza = (msg->hdr).len;
	char *bufptr = msg->buf;
	ssize_t scritto = 0;
	while((scritto += write((int) fd,&((msg->hdr).receiver),MAX_NAME_LENGTH+1)) != MAX_NAME_LENGTH+1){
		if(scritto == -1){
			errno = ENOTCONN;
			return -1;
		}
	}
	scritto = 0;
  	while((scritto += write((int) fd,&((msg->hdr).len),sizeof(unsigned int))) != sizeof(unsigned int)){
  		if(scritto == -1){
			errno = ENOTCONN;
			return -1;
		}
  	}
	if(lunghezza == 0 || bufptr == NULL)
    	return 0;
	while(lunghezza > 0){
		ssize_t scritto = write((int) fd,bufptr,lunghezza);
		if(scritto == -1)
			return -1;
		lunghezza -= scritto;
		bufptr += scritto;
	}
	return 1;
}

int sendHeader(long fd, message_hdr_t *msg){
	if(fd<3){
		errno = ENOTCONN;
		return -1;
	}
	if(msg==NULL){
		errno = EINVAL;
		return -1;
	}
	ssize_t scritto = 0;
	while((scritto += write((int) fd,&(msg->op),sizeof(op_t))) != sizeof(op_t)){
		if(scritto==-1){
			errno = ENOTCONN;
			return -1;
		}
	}
	scritto = 0;
	while((scritto += write((int) fd,&(msg->sender),MAX_NAME_LENGTH+1)) != MAX_NAME_LENGTH+1){
		if(scritto == -1){
			errno = ENOTCONN;
			return -1;
		}
	}
	return 1;
}
