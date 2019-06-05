# @file: script.sh
# @author: Pietro Libro 545559
# @email: libropiero@gmail.com

#!bin/bash
# Funzione di help
function help { 
	echo "---------------------------HELP---------------------------"
	echo "COMMAND  : $0 path intVal"
	echo "path     : percorso che contiene il file di configurazione"
	echo "intVal   : valore intero positivo"
	echo "----------------------------------------------------------"
	exit
}
# Chiamo funzione di help se richiesto
for i; do
	if [ "$i" = "-help" ]; then
		help
	fi
done
# Meno di due argomenti
if [ $# -lt 2 ]; then
	help
fi
# -f True se file esiste
if [ ! -f $1 ]; then
	echo "$1 non esiste o non e' un file"
	exit
fi
# Controllo che sia un valore intero >=0
if [[ ! $2 =~ ^[0-9]+$ ]] || [ $2 -lt 0 ]; then
	echo " $2 non e' un valore intero positivo"
	exit
fi
# Prelevo la directory da file di configurazione
DIRNAME=$(grep -v '^#' $1 |grep DirName |cut -d= -f2 | tr -d [[:space:]])
# -d True se esiste ed e' una directory
if [ ! -d $DIRNAME ]; then
	echo "La directory $DIRNAME non esiste"
	exit
fi
# Controllo stringa vuota
if [ -z $DIRNAME ]; then
	echo "La directory e' ROOT"
	exit
fi
# Se il valore intero e' 0 stampo file presenti
if [ $2 -eq 0 ]; then
	echo "File Presenti echo in directory : $DIRNAME"
	for i in "$DIRNAME"/*
	do
		if [ -f $i ]; then
			echo $(basename $i)
		fi
	done
else
	# Crea il tar se nella directory sono presenti file di size>0
	echo "Directory : $DIRNAME"
	find $DIRNAME -mmin $((-$2)) ! -path $DIRNAME -exec tar -cvf chatty.tar {} + |xargs rm -vfd
	echo "Creato tar"
fi
