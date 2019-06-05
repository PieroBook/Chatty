/*
 * membox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */
/**
 * @file config.h
 * @brief File contenente alcune define con valori massimi utilizzabili
 */
/** \file config.h  
    \author Pietro Libro 545559
    Si dichiara che il contenuto di questo file e' in ogni sua parte opera  
    originale dell'autore  
*/
#if !defined(CONFIG_H_)
#define CONFIG_H_

#define MAX_NAME_LENGTH                  32

/* aggiungere altre define qui */
// Numero Massimo di lock su fd utenti
#define MAXFDSOCK 128
// Dimensione Hash Table
#define HASH_DIM 512  
// Numero Massimo lock hash            
#define MAX_HASH 128


// to avoid warnings like "ISO C forbids an empty translation unit"
typedef int make_iso_compilers_happy;

#endif /* CONFIG_H_ */
