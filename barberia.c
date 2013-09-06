#define CLIENTS_TOTALS	50
#define BARBERS	3
#define CLIENTS_BARBERIA 	20
#define CLIENTS_SOFA 		4
#define CLIENTS_BARBER	3
#define COORD		3

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Declaració semàfors */
int s_capacitat_maxima;
int s_sofa;
int s_barber;
int sem_coord;
int sem_mutex1;
int sem_mutex2;
int s_client_acabat;
int s_cedir_cadira_b;
int s_pagat;
int s_rebut;
int s_fi_barberia[CLIENTS_TOTALS]; //Acaba el problema de la barberia

/* Variables d'àmbit global */
int *count; // Torn del client
int cua; // Conjunt de missatges (cua) per a enviar el torn als barbers
int clients_despatxats; // Clients amb els cabells ja tallats
pid_t pid_barberos[BARBERS]; 
pid_t pid_cajero;


/* Estructura que defineix el format d'un missatge */
typedef struct {
	long id;
	int numero;
} missatge;


/* Zona de memoria compartida per a compartir el torn del client */
int crea_memoria(key_t key) { 
	int mem;
	//Creació de la zona de memòria compartida (el tercer paràmetre són permisos de lectura, escriptura, etc.)
	mem = shmget(key, sizeof(int), IPC_CREAT|S_IRUSR|S_IWUSR);
	if (mem == -1) {
		perror("shmget");
		exit(EXIT_FAILURE);
	}
	
	return mem;
}

/* Mapeig de la memòria compartida */
int *map_mem(int mem) { 
	int *dir;

	dir = (int *) shmat(mem, NULL, 0); 
    	if (dir == (void *)-1) {
        	perror("shmat");
        	exit(EXIT_FAILURE);
    	} 
	return dir;
}

/* Eliminació de la memòria compartida */
void allibera_memoria(int mem, int *dir) { 
	if (shmdt(dir) == -1) { // Desfà el mapeig de la funció anterior (memòria compartida)
		perror("shmdt");
        	exit(EXIT_FAILURE);
    	}
	if (shmctl(mem, IPC_RMID, NULL) == -1) { //Allibera la memòria compartida
        	perror("shmctl");
        	exit(EXIT_FAILURE);
    	}
}

/* Funció per crear cues de missatges */
int crea_cua(key_t clave) {
	int cua;
	cua = msgget(clave, IPC_CREAT|S_IRUSR|S_IWUSR);
	if (cua == -1) {
		printf("Error en la cua %d\n", cua);
		exit (EXIT_FAILURE);
	}
	return cua;
}

/* Funció per desfer una cua generada per la funció anterior */
void desfer_cua (int cua) {
	msgctl(cua, IPC_RMID, 0);
}

/* Crea un semàfor */
int crea_semafor(key_t key) {
	int sem;

	sem = semget(key, 1, IPC_CREAT|S_IRUSR|S_IWUSR);
	if (sem == -1) {
		perror("semget");
		exit(EXIT_FAILURE);
	}
	return sem;
}

/* Inicialitza un semàfor */
void init_sem(int sem, int valor_ini) {
	if (semctl(sem, 0, SETVAL, valor_ini) == -1) {
		perror("semctl_set");
		exit(EXIT_FAILURE);
	}
}

/* Operació atòmica wait per a un semàfor */
void wait_sem(int sem) {
	struct sembuf sbuf;

	sbuf.sem_num = 0;
	sbuf.sem_op = -1;
	sbuf.sem_flg = 0;
	if (semop(sem, &sbuf, 1) == -1) {
		perror("semop_wait");
		exit(EXIT_FAILURE);
	}
}

/* Operació signal per a un semàfor */
void signal_sem(int sem) {
	struct sembuf sbuf;

	sbuf.sem_num = 0;
	sbuf.sem_op = 1;
	sbuf.sem_flg = 0;
	if (semop(sem, &sbuf, 1) == -1) {
		perror("semop_signal");
		exit(EXIT_FAILURE);
	}
}

/* Funció per a eliminar un semàfor */
void elimina_semafor(int sem) {
	if (semctl(sem, 0, IPC_RMID, 0) == -1) {
		perror("semctl_rm");
		exit(EXIT_FAILURE);
	}
}


/* Funció que allibera l'espai dels procesos client que ja hagin sortit de la barberia */
void alliberar_client () {
	while (wait3(NULL, WNOHANG, NULL)>0) {
		clients_despatxats++;
	}
}

/* Procés per a cada client */
void client() {
	int rmissatge;
	missatge torn_client; 
	wait_sem (s_capacitat_maxima);
	puts("Entra a la tenda un client");
	wait_sem(sem_mutex1);
	torn_client.id = getpid();
	torn_client.numero = *count;
	(*count)++;
	wait_sem (s_sofa);
	signal_sem (sem_mutex1);
	printf("El client %d: S'ha assegut al sofà\n", torn_client.numero);
	wait_sem (s_barber);
	printf("El client %d: s'ha aixecat del sofà\n", torn_client.numero);
	signal_sem (s_sofa);
	printf("El client %d: s'està tallant els cabells\n", torn_client.numero);
	wait_sem (sem_mutex2);
	
	// Metemos en la cua el número del client
	if (msgsnd(cua, &torn_client, sizeof(missatge)-sizeof(long), IPC_NOWAIT) == -1) {
		printf("Error: el missatge no s'ha enviat correctament: %d\n", getpid(), cua);
		exit(EXIT_FAILURE);
	}
	
	signal_sem (s_client_acabat);
	signal_sem (sem_mutex2);
	wait_sem (s_fi_barberia[torn_client.numero]);
	printf("El client %d: s'ha acabat de tallar els cabells\n", torn_client.numero);
	signal_sem (s_cedir_cadira_b);
	printf("El client %d: ha pagat\n", torn_client.numero);
	signal_sem(s_pagat);
	wait_sem(s_rebut);
	printf("El client %d: deixa la barberia\n", torn_client.numero);
	signal_sem(s_capacitat_maxima);
}

/* Procés dels barbers */
void barbero() {
	int rmissatge;
	missatge client_seguent;
	while (1) {
		wait_sem(s_client_acabat);
		wait_sem(sem_mutex2);

		// Es fa passar a un client de la cua
		if (msgrcv(cua, &client_seguent, sizeof(missatge)-sizeof(long), 0, 0)==-1) {
			printf("Error: no s'ha enviat el missatge %d\n", getpid(), cua);
		}

		signal_sem(sem_mutex2);
		wait_sem(sem_coord);
		printf("El client %d s'està tallant els cabells\n", client_seguent.numero);
		sleep(random()%5); // Tallar els cabells (x temps depenent dels cabells del client = random)
		signal_sem(sem_coord);
		signal_sem(s_fi_barberia[client_seguent.numero]);
		wait_sem(s_cedir_cadira_b);
		signal_sem (s_barber);		
	}
}

/* Procés caixer */
void cajero() {
	while (1) {
		wait_sem(s_pagat);
		wait_sem(sem_coord);
 		printf("S'ha rebut el pagament\n");
		signal_sem(sem_coord);
		signal_sem(s_rebut);
 	}
}

int main(int argc, char **argv) {
	int i;
	int mem;

	key_t key[62]; // Generam una clau per a cada recurs UNIX(semàfor, memòria compartida i cues)
	for (i=0; i<62; i++) {
		key[i] = ftok("/bin/ls", i);
		if (key[i] == -1) {
			perror("ftok");
			exit(EXIT_FAILURE);
		}
	}
	
	/* S'assigna alliberar_client als fills de client */
	signal(SIGCHLD, alliberar_client);
	
	/* Es creen tots els semàfors */
	s_capacitat_maxima = crea_semafor(key[0]);
	s_sofa = crea_semafor(key[1]);
	s_barber = crea_semafor(key[2]);	
	sem_coord = crea_semafor(key[3]);
	sem_mutex1 = crea_semafor(key[4]);
	sem_mutex2 = crea_semafor(key[5]);
	s_client_acabat = crea_semafor(key[6]);
	s_cedir_cadira_b = crea_semafor(key[7]);
	s_pagat = crea_semafor(key[8]);
	s_rebut = crea_semafor(key[9]);

	for (i=0; i<CLIENTS_TOTALS; i++) {
		s_fi_barberia[i] = crea_semafor(key[10+i]);
	}

	/* S'inicialitza cada semàfor */
	init_sem(s_capacitat_maxima, CLIENTS_BARBERIA); 	/* Capacitat barbería */
	init_sem(s_sofa, CLIENTS_SOFA); 			/* Capacitat sofà */
	init_sem(s_barber, CLIENTS_BARBER);	/* Seients de la barberia */
	init_sem(sem_coord, COORD); 
	init_sem(sem_mutex1, 1); 	
	init_sem(sem_mutex2, 1); 		
	init_sem(s_client_acabat, 0);	
	init_sem(s_cedir_cadira_b, 0); 
	init_sem(s_pagat, 0);
	init_sem(s_rebut, 0);
	
	for (i=0; i<CLIENTS_TOTALS; i++) {
		init_sem(s_fi_barberia[i], 0);
	}

	/* Es crea la cua de missatges */	
	cua = crea_cua(key[60]);

	/* Es crea la zona de memoria compartida per a concórrer en el torn del client */
	mem = crea_memoria(key[61]);
	count = map_mem(mem);
	*count = 0;

	/* Fem els fork adients per a cada barber */
	for (i=0; i<BARBERS; i++) {
		if ((pid_barberos[i] = fork()) == 0) {
			barbero();
		} 
	}

	/* Invocam el procés del caixer */
	if ((pid_cajero = fork()) == 0) {
		cajero();
	}

	/* Fem un fork del procés client per a cada client */
	for (i=0; i<CLIENTS_TOTALS; i++) {
		if (fork() == 0) {
			client();
			exit(EXIT_SUCCESS);
		}
	}
	
	//Fins que s'acabin els clients...
	while (clients_despatxats < CLIENTS_TOTALS) {
		pause();//...esperar
	}

	for (i=0; i<BARBERS; i++) {
		printf("Fi del procés barber: %d\n", pid_barberos[i]);
		kill(pid_barberos[i], SIGTERM);
	}
	printf("Fi del procés caixer: %d\n", pid_cajero);

	/* S'esborren els semàfors */
	printf("A continuació s'alliberen els semafors...\n");
	elimina_semafor(s_capacitat_maxima);
	elimina_semafor(s_sofa);
	elimina_semafor(s_barber);
	elimina_semafor(sem_coord);
	elimina_semafor(sem_mutex1); 	
	elimina_semafor(sem_mutex2); 		
	elimina_semafor(s_client_acabat);	
	elimina_semafor(s_cedir_cadira_b); 
	elimina_semafor(s_pagat);
	elimina_semafor(s_rebut);
	for (i=0; i<CLIENTS_TOTALS; i++) {
		elimina_semafor(s_fi_barberia[i]);
	}

	/* Es desfà la cua de missatges */
	printf("Es desfà la cua de missatges\n");
	desfer_cua(cua);

	/* Alliberam les zones de memoria compartida */
	printf("A continuació s'allibera la memòria compartida\n");
	allibera_memoria(mem, count);

	return EXIT_SUCCESS;
}
