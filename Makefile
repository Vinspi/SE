
CFLAGS=-Wall -std=c99 -w
CC = gcc


bor-util: bor-util.h
	$(CC) -o bor-util.o bor-util.c

simul: bor-util.o simul.c
	$(CC) -o simul simul.c bor-util.o

all : simul
