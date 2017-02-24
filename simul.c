
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bor-util.h"



/**********************************************************
** Codage d'une instruction (32 bits)
***********************************************************/

typedef struct {
	unsigned OP: 10;  /* code operation (10 bits)  */
	unsigned i:   3;  /* nu 1er registre (3 bits)  */
	unsigned j:   3;  /* nu 2eme registre (3 bits) */
	short    ARG;     /* argument (16 bits)        */
} INST;


/**********************************************************
** definition de la memoire simulee
***********************************************************/
int CC = 0;
typedef int WORD;  /* un mot est un entier 32 bits  */
WORD mem[128];     /* memoire                       */

char tampon = '\0';
char charToAdd = '\0';

int nbInstructions = 0;

int current_time = 0;

/**********************************************************
** Codes associes aux syscall
***********************************************************/

#define SYSC_EXIT (0)
#define SYSC_PUTI (1)
#define SYSC_NEW_THREAD (2)
#define SYSC_SLEEP (3)
#define SYSC_GETCHAR (4)

/**********************************************************
** Codes associes aux instructions
***********************************************************/

#define INST_ADD	(0)
#define INST_SUB	(1)
#define INST_CMP	(2)
#define INST_IFGT (3)
#define INST_NOP	(4)
#define INST_JUMP	(5)
#define INST_HALT	(6)
#define INST_SYSC (7)
#define INST_LOAD (8)
#define INST_STORE (9)



/**********************************************************
** Placer une instruction en memoire
***********************************************************/

void make_inst(int adr, unsigned code, unsigned i, unsigned j, short arg) {
	union { WORD word; INST fields; } inst;
	inst.fields.OP  = code;
	inst.fields.i   = i;
	inst.fields.j   = j;
	inst.fields.ARG = arg;
	mem[adr] = inst.word;
}


/**********************************************************
** Codes associes aux interruptions
***********************************************************/

#define INT_NONE (0)
#define INT_INIT	(1)
#define INT_SEGV	(2)
#define INT_INST	(3)
#define INT_TRACE	(4)
#define INT_CLOCK (5)
#define INT_EXIT (6)
#define INT_PUTI (7)
#define INT_NEW_THREAD (8)
#define INT_SLEEP (9)
#define INT_GETCHAR (10)



/**********************************************************
** Le Mot d'Etat du Processeur (PSW)
***********************************************************/

typedef struct PSW {    /* Processor Status Word */
	WORD PC;        /* Program Counter */
	WORD SB;        /* Segment Base */
	WORD SS;        /* Segment Size */
	WORD IN;        /* Interrupt number */
	WORD DR[8];     /* Data Registers */
	WORD AC;        /* Accumulateur */
	INST RI;        /* Registre instruction */
} PSW;


#define MAX_PROCESS  (20)   /* nb maximum de processus  */
#define EMPTY         (0)   /* processus non-pret       */
#define READY         (1)   /* processus pret           */
#define SLEEPING (2) /* processus endormi */
#define GETCHAR (3)


struct {
	PSW  cpu;               /* mot d’etat du processeur */
	int  state;    					/* etat du processus        */
	int dateReveil;
} process[MAX_PROCESS];   /* table des processus      */

int current_process = 0;   /* nu du processus courant  */

/**********************************************************
** Simulation de la CPU (mode utilisateur)
***********************************************************/

/* instruction d'addition */
PSW cpu_ADD(PSW m) {
	m.AC = m.DR[m.RI.i] += (m.DR[m.RI.j] + m.RI.ARG);
	m.PC += 1;
	return m;
}

/* instruction de soustraction */
PSW cpu_SUB(PSW m) {
	m.AC = m.DR[m.RI.i] -= (m.DR[m.RI.j] + m.RI.ARG);
	m.PC += 1;
	return m;
}

/* instruction de comparaison */
PSW cpu_CMP(PSW m) {
	m.AC = (m.DR[m.RI.i] - (m.DR[m.RI.j] + m.RI.ARG));
	m.PC += 1;
	return m;
}

/* instruction de branchement */
PSW cpu_IFGT(PSW m){
	if(m.AC > 0) m.PC = m.RI.ARG;
	else m.PC++;
	return m;
}

/* non operation */
PSW cpu_NOP(PSW m){
	m.PC++;
	return m;
}

/* instruction de saut */
PSW cpu_JUMP(PSW m){
	m.PC = m.RI.ARG;
	return m;
}

/* instruction HALT */
PSW cpu_HALT(PSW m){
	exit(1);
}

PSW cpu_SYSC(PSW m){
	switch (m.RI.ARG) {
		case SYSC_EXIT:
			m.IN = INT_EXIT;
			break;
		case SYSC_PUTI:
			m.IN = INT_PUTI;
			break;
		case SYSC_NEW_THREAD:
			m.IN = INT_NEW_THREAD;
			break;
		case SYSC_SLEEP:
			m.IN = INT_SLEEP;
			break;
		case SYSC_GETCHAR:
			m.IN = INT_GETCHAR;
			break;
	}
	m.PC++;
	return m;
}

PSW cpu_LOAD(PSW m){


	m.AC = m.DR[m.RI.j] + m.RI.ARG;

	if(m.AC < 0 || m.AC >= m.SS){
		printf("%s\n", "Erreur d'adressage");
		exit(1);
	}
	m.AC = mem[m.SB+m.AC];

	m.DR[m.RI.i] = m.AC;
	m.PC++;
	return m;
}

PSW cpu_STORE(PSW m){

	m.AC = m.DR[m.RI.j] + m.RI.ARG;

	if(m.AC < 0 || m.AC >= m.SS){
		printf("%s\n", "Erreur d'adressage");
		exit(1);
	}

	mem[m.SB+m.AC] = m.DR[m.RI.i];
	m.AC = m.DR[m.RI.i];
	m.PC++;
	return m;
}

/* Simulation de la CPU */
PSW cpu(PSW m) {

	//printf("CC = %d\n", CC);
	union { WORD word; INST in; } inst;

	/*** interruption apres chaque instruction ***/
	if(CC == 2){
		m.IN = INT_CLOCK;
	}
	CC = (CC+1)%3;

	/*** lecture et decodage de l'instruction ***/
	if (m.PC < 0 || m.PC >= m.SS) {
		m.IN = INT_SEGV;
		return (m);
	}
	inst.word = mem[m.PC + m.SB];
	m.RI = inst.in;
	/*** execution de l'instruction ***/
	switch (m.RI.OP) {
	case INST_ADD:
		m = cpu_ADD(m);
		break;
	case INST_SUB:
		m = cpu_SUB(m);
		break;
	case INST_CMP:
		m = cpu_CMP(m);
		break;
	case INST_IFGT:
		m = cpu_IFGT(m);
		break;
	case INST_NOP:
		m = cpu_NOP(m);
		break;
	case INST_JUMP:
		m = cpu_JUMP(m);
		break;
	case INST_HALT:
		m = cpu_HALT(m);
		break;
	case INST_SYSC:
		m = cpu_SYSC(m);
		break;
	case INST_LOAD:
		m = cpu_LOAD(m);
		break;
	case INST_STORE:
		m = cpu_STORE(m);
		break;

	default:
		/*** interruption instruction inconnue ***/
		m.IN = INT_INST;
		return (m);
	}

	return m;
}


/**********************************************************
** Fonctions de test a appeler dans "systeme_init()"
***********************************************************/

void test_increment_and_display(){
		make_inst(0,INST_SUB,1,1,0);
		make_inst(1,INST_SUB,3,3,-1);
		make_inst(2,INST_SUB,2,2,-10);
		make_inst(3,INST_SYSC,1,1,SYSC_NEW_THREAD);
		make_inst(4,INST_IFGT,0,0,9);
		make_inst(5,INST_ADD,1,3,0);
		make_inst(6,INST_STORE,1,0,1);
		make_inst(7,INST_SYSC,3,1,SYSC_SLEEP);
		make_inst(8,INST_JUMP,0,0,5);

		make_inst(9,INST_LOAD,1,0,1);
		make_inst(10,INST_SYSC,1,2,SYSC_PUTI);
		make_inst(11,INST_SYSC,3,0,SYSC_SLEEP);
		make_inst(12,INST_JUMP,0,0,9);

		nbInstructions = 13;
}


void test_read_and_print_char_from_buffer(char charToRead){
		charToAdd = charToRead;

		make_inst(0,INST_SUB,1,1,0);
		make_inst(1,INST_SUB,2,2,-1);
		make_inst(2,INST_SYSC,1,1,SYSC_GETCHAR);
		make_inst(3,INST_SYSC,1,1,SYSC_PUTI);
		make_inst(4,INST_NOP,2,1,0);
		make_inst(5,INST_JUMP,1,1,2);

		nbInstructions = 6;
}

void test_fibonacci_to_ten(){
		make_inst(0,INST_SUB,1,1,0);
		make_inst(1,INST_SUB,2,2,-1);
		make_inst(2,INST_SUB,3,3,-10);
		make_inst(3,INST_SUB,4,4,0);
		make_inst(4,INST_SUB,5,5,-1);
		make_inst(5,INST_STORE,2,0,1);
		make_inst(6,INST_ADD,2,1,0);
		make_inst(7,INST_SYSC,4,4,SYSC_PUTI);
		make_inst(8,INST_SYSC,2,0,SYSC_PUTI);
		make_inst(9,INST_LOAD,1,0,1);
		make_inst(10,INST_ADD,4,5,0);
		make_inst(11,INST_CMP,4,3,0);
		make_inst(12,INST_IFGT,4,4,14);
		make_inst(13,INST_JUMP,0,0,5);
		make_inst(14,INST_HALT,0,0,0);

		nbInstructions = 15;
}


/**********************************************************
** Demarrage du systeme
***********************************************************/

PSW systeme_init(void) {
	PSW cpu;


	printf("Booting.\n");

	//test_increment_and_display();

	test_read_and_print_char_from_buffer('a');

	//test_fibonacci_to_ten();


	/*** valeur initiale du PSW ***/
	memset (&cpu, 0, sizeof(cpu));
	cpu.PC = 0;
	cpu.SB = 0;
	cpu.SS = nbInstructions;

	process[0].cpu = cpu;
	process[0].state = READY;

	return cpu;
}

void affiche_DR(PSW m){
		printf("\n------------------\n");
		for(int i=0;i<8;i++){
			printf("DR[%d] : %d\n", i, m.DR[i]);
		}
		printf("------------------\n\n");
}

void afficher_1er_RI(PSW m){
	printf("R[%d] = %d\n",m.RI.i, m.DR[m.RI.i]);
}


PSW switchProcess(PSW m){

	process[current_process].cpu = m;
	do {
		current_process = (current_process + 1) % MAX_PROCESS;
		if(process[current_process].state == SLEEPING && process[current_process].dateReveil <= time(NULL))
			process[current_process].state = READY;
		else if(process[current_process].state == GETCHAR && tampon != '\0'){
			process[current_process].state = READY;
			process[current_process].cpu.DR[process[current_process].cpu.RI.i] = tampon;
			tampon = '\0';
		}
	} while (process[current_process].state != READY);

	CC = 0;
	process[current_process].cpu.IN = INT_NONE;
	return process[current_process].cpu;
}

int dupProcessus(PSW m){

	PSW newProcess;

	memset(&newProcess, 0, sizeof(newProcess));

	newProcess.PC = m.PC;
	newProcess.SB = m.SB;
	newProcess.SS = m.SS;
	newProcess.IN = m.IN;

	for(int i=0;i<8;i++)
			newProcess.DR[i] = m.DR[i];

	newProcess.AC = 0;
	newProcess.DR[newProcess.RI.i] = 0;

	for(int i=0;i<MAX_PROCESS;i++){
		if(process[i].state == EMPTY){
				process[i].cpu = newProcess;
				process[i].state = READY;
                m.AC = i;
                m.DR[m.RI.i] = i;
				return 0;
		}
	}
	return -1;
}

void endortProcessus(PSW m){

	process[current_process].state = SLEEPING;
	process[current_process].dateReveil = time(NULL) + m.DR[m.RI.i];

}

void getcharre(PSW m){
	process[current_process].state = GETCHAR;
}

/**********************************************************
** Simulation du systeme (mode systeme)
***********************************************************/

PSW systeme(PSW m) {
	//printf("%s\n", "coucou");
	switch (m.IN) {
		case INT_INIT:
			printf("%s\n", "INT_INIT\n");
			return (systeme_init());
		case INT_SEGV:
		  printf("%s\n", "INT_SEGV\n");
			process[current_process].state = EMPTY;
			break;
		case INT_TRACE:
		  printf("%s\n", "INT_TRACE\n");
			printf("%s %d\n", "PC :",m.PC);
			affiche_DR(m);
			break;
		case INT_INST:
			printf("%s\n", "INT_INST\n");
			process[current_process].state = EMPTY;
			break;
		case INT_CLOCK:
			printf("%s\n", "INT_CLOCK\n");
			break;
		case INT_EXIT:
			printf("%s\n", "INT_EXIT\n");
			process[current_process].state = EMPTY;
			break;
		case INT_PUTI:
			afficher_1er_RI(m);
			break;
		case INT_NEW_THREAD:
			if(dupProcessus(m)<0)
				printf("%s\n", "Erreur, plus de place");
			break;
		case INT_SLEEP:
			endortProcessus(m);
			break;
		case INT_GETCHAR:
			getcharre(m);
			break;
	}
	m = switchProcess(m);
	return m;
}

void handler(int signal){
	  tampon = charToAdd;
		if(tampon != '\0')
			printf("\nAlarm detectée (Ecriture de '%c' dans tampon)\n\n", tampon);
		else
			printf("\nAlarm detectée\n\n");
		alarm(3);
}

/**********************************************************
** fonction principale
** (ce code ne doit pas etre modifie !)
***********************************************************/

int main(void) {
	PSW mep;

	bor_signal(SIGALRM,handler,0);
	alarm(3);

	mep.IN = INT_INIT; /* interruption INIT */
	while (1) {
		mep = systeme(mep);
		mep = cpu(mep);
	}

	return (EXIT_SUCCESS);
}
