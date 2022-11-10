/*============================================================
Archivo:                opr.c
Autores:                Katherine Arleth Rac Pocon (201029)
                        Rodrigo Alejandro Cruz Fagiani (20073)
Fecha de Creación:      02-noviembre-2022
Fecha de Entrega:       27-noviembre-2022
Título:                 Proyecto 1 - Digital 3

Descripción:
Operador para estación de control de sistema de regado con 
sensores y bomba de regado.

Compnentes:
- Botón Regar Ahora
- Botón Status
- Switch Ahorro de Agua
- Switch Habilitar Regado Automático
- Sensor de Humedad y Temperatura AHT10 (analógico)
=======================================================*/

//====================================================================
// LIBRERÍAS
//====================================================================
#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

//====================================================================
// CONSTANTES
//====================================================================
#define DEFAULT_BRD_ADDR    "192.168.1.255"
//#define DEFAULT_BRD_ADDR    "10.0.0.255"
//#define DEFAULT_BRD_ADDR    "192.168.199.255"
#define DEFAULT_PORT        2000
#define MSG_SIZE            60   

//====================================================================
// ESTRUCTURAS
//====================================================================
struct sockaddr_in anybody, from;

//====================================================================
// VARIABLES DEL PROGRAMA
//====================================================================
int boolval = 1;        
int brd_switch;
int master_switch;
int port_num;
int socketfd;
int time_scs = 0;           //Variables para contar el tiempo
int time_mns = 0;
int time_hrs = 0;
int utr_count = 0;          //Contador de UTRS
char buffer_bld[MSG_SIZE];
char buffer_rcv[MSG_SIZE];
char buffer_snd[MSG_SIZE];
char IP_brd[20];            //Almacenar dirección de broadcast
char time_now[25];          //Guardar el tiempo actual
char new_instruction[20];
sem_t sem_buffer;
unsigned int length;

//====================================================================
// PROTOTIPOS DE FUNCIONES
//====================================================================
void error(const char *msg);        //Impresión de errores
void getCurrentTime(void);          //Obtener la hora actual
void getBrdAddr (void);             //Obtener la dirección de broadcast
void rcvThread (void);
void sndThread (void);
void schThread (void);

//====================================================================
// FUNCIÓN PRINCIPAL
//====================================================================
int main(int argc, char *argv[]) {

    //====================================================================
    // INICIALIZACIÓN
    //====================================================================
    //Definición del puerto para el socket
    puts("INITIALIZING HISTORIAN");
    if(argc != 2) {
        port_num = DEFAULT_PORT;
    } 
    else {
        port_num = atoi(argv[1]);
    }
    printf("> Port in use: %d\n", port_num);
    //Obtenemos la hora al iniciar el programa
    getCurrentTime();
    fflush(stdout);
    printf("> Current time: %d:%d:%d\n", time_hrs, time_mns, time_scs);
    //Obtener la dirección de Broadcast
    getBrdAddr();
    fflush(stdout);
    printf("> Broadcast IP Address: %s\n", IP_brd);

    //====================================================================
    // CREACIÓN E INICIALIZACIÓN DEL SOCKET
    //====================================================================
    puts("\nSOCKET INITIALIZATION");
    //Inicialización del socket
    socketfd = socket(AF_INET, SOCK_DGRAM, 0);          //Obtener el file descriptor
    if (socketfd < 0) {
        error("Opening socket");                        //Si no se puediera recuperar el file descriptor...
    }
    //Conectando el socket a una dirección IP
    anybody.sin_family = AF_INET;                        //Define la familia de dispositivos como "dominio de internet"
    anybody.sin_port = htons(port_num);                  //Se define el puerto en el que se llevará la comunicación
    anybody.sin_addr.s_addr = htonl(INADDR_ANY);         //Conectar a cualquier IP local
    puts("> Characteristics definition finished");
    //Conexión al socket
    length = sizeof(struct sockaddr_in);
    if(bind(socketfd, (struct sockaddr *)&anybody, length) < 0) {
        error("Error binding socket.");
    }
    puts("> Connection to socket finished");
    //Configuración del socket: (socket, nivel, protocolo, valor booleano retornado, tamaño de la variable booleana )
    if(setsockopt(socketfd, SOL_SOCKET, SO_BROADCAST, &boolval, sizeof(boolval)) < 0) {
        error("Error setting socket options\n");
    }
    puts("> Socket setup finished\n");

    puts("\nPROGRAM STARTED, LISTENING TO UTR(S)");

    //====================================================================
    // CONEXIÓN CON HILOS EXTRA
    //====================================================================
    master_switch = 1;
    sem_init(&sem_buffer, 0, 1);
    pthread_t thread_rcv, thread_snd, thread_sch;
    pthread_create(&thread_rcv, NULL, (void *)&rcvThread, NULL);
    pthread_create(&thread_snd, NULL, (void *)&sndThread, NULL);
    pthread_create(&thread_sch, NULL, (void *)&schThread, NULL);
    pthread_join(thread_snd, NULL);

    //====================================================================
    // FINALIZACIÓN DEL PROGRAMA
    //====================================================================
    close(socketfd);
    return 0;
}

//====================================================================
// LOOPS PARA HILOS
//====================================================================
//Hilo para recibir mensajes
void rcvThread (void) {
    do {
        memset(buffer_rcv, 0, MSG_SIZE);
        if((recvfrom(socketfd, buffer_rcv, MSG_SIZE, 0, (struct sockaddr *)&from, &length)) < 0) { 
	 		error("recvfrom");
        }

        //Se se recibe una solicitud de asignación
        if (strcmp(buffer_rcv, "NEW UTR") == 0) {
            printf("\n> RECV: %s", buffer_rcv);
            fflush(stdout);
            utr_count++;
            memset(buffer_bld, 0, MSG_SIZE);
            sprintf(buffer_bld, "ASSIGNED UTR #%d", utr_count);
            if((sendto(socketfd, buffer_bld, MSG_SIZE, 0, (struct sockaddr *)&from, length)) < 0) { 
                error("recvfrom");
            }
            printf("\n>>> SENT: %s", buffer_bld);
            fflush(stdout);
        }

        //Se se recibe una actualización de estado
        if (strncmp(buffer_rcv, "UTR #", 5) == 0) {
            printf("\n> RECV: %s", buffer_rcv);
            fflush(stdout);
        }
    } while (master_switch == 1);
    pthread_exit(0);
}

//Hilo para enviar mensajes
void sndThread (void) {
    do {
        memset(buffer_snd, 0, MSG_SIZE);                    //Limpiar el buffer de construcción de cadenas
        printf("%s", ">>> WAITING FOR CMD: ");
        fgets(buffer_snd,MSG_SIZE-1,stdin);                     //Esperar que el usuario ingrese un mensaje
        anybody.sin_addr.s_addr = inet_addr(IP_brd);        //Enviar mensaje broadcast
        if((sendto(socketfd, buffer_snd, MSG_SIZE, 0, (struct sockaddr *)&anybody, length)) < 0) { 
	 		error("recvfrom");
        }
        //Comando de apagar el sistema
        if (strcmp(buffer_snd, "END\n") == 0) {
            puts("> SYST: SHUTTING DOWN");
            master_switch = 0;
        } else {
            printf("> CMND: %s", buffer_snd);                   //Imprimir mensaje enviado
        }
    } while (master_switch == 1);
    pthread_exit(0);
}

void schThread (void) {
    do {
        anybody.sin_addr.s_addr = inet_addr(IP_brd);        //Enviar mensaje broadcast
        if((sendto(socketfd, "HELLO?", MSG_SIZE, 0, (struct sockaddr *)&anybody, length)) < 0) { 
            error("recvfrom");
        }
        usleep(2000000);
    } while (master_switch == 1);
}

//====================================================================
// FUNCIONES AUXILIARES
//====================================================================
void error(const char *msg) {
    perror(msg);
    exit(0);
}

void getCurrentTime(void) {
    //Creación de Variables a utilizar
    time_t rawtime;                                     //Se crea una variable tipo time_t
    struct tm* timeinfo;                                //Se define que timeinfo será una estructura de tipo tm
    //Guardando hora local en variables
    time (&rawtime);                                    //Se le coloca una estructura de tipo tm a rawtime
    timeinfo = localtime (&rawtime);                    //Introduce los valores de tiempo local a la estructura
    sprintf (time_now, "%s", asctime (timeinfo));       //Se crea una cadena con la hora
    //Separación por tokens para guardar solo la hora
    int i;                                              //Variable contadora
    char *token = strtok(time_now, " :");               //Se crea la variable token que ira guardando los fragmentos
    while(token != NULL) {                              //Mientras si haya un valor en token...
        if (i == 3) {time_hrs = atoi(token);}           //Guardando la hora
        else if (i == 4) {time_mns = atoi(token);}      //Guardando los minutos
        else if (i == 5) {time_scs = atoi(token);}      //Guardando los segundos
        token = strtok(NULL, " :");                     //Separar el siguiente token
        i++;                                            //Aumentar el contador
    }
}
//Función para obtener la IP de broadcast
void getBrdAddr (void) {
    int n;                                                                          //Variable para guardar el fd del socket nuevo
    struct ifreq ifr;                                   
    char array[] = "wlan0";                                                         //Se declara tipo de conexión inalámbrica
    char *IP_addr;
    int IP_pcs_1, IP_pcs_2, IP_pcs_3;

    n = socket(AF_INET, SOCK_DGRAM, 0);                                             //Se crea el nuevo socket
    ifr.ifr_addr.sa_family = AF_INET;

    strncpy(ifr.ifr_name, array, IFNAMSIZ - 1);                                     //Recorrer el resultado de "ifconfig"
    ioctl(n, SIOCGIFADDR, &ifr);
    close(n);                                                                       //Se cierra el nuevo socket creado
    IP_addr = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr )->sin_addr);          //Guardamos la dirección IP en un puntero
    int i = 0;
    char *token = strtok(IP_addr, " .");                                            //Se crea la variable token que ira guardando los fragmentos de la IP
    while(token != NULL) {                                                          //Mientras si haya un valor en token...
        if (i == 0) {IP_pcs_1 = atoi(token);}                                       //Primer fragmento guardado
        else if (i == 1) {IP_pcs_2 = atoi(token);}                                  //Segundo fragmento guardado
        else if (i == 2) {IP_pcs_3 = atoi(token);}                                  //Tercero fragmento guardado
        token = strtok(NULL, " .");                                                 //Separar el siguiente token
        i++;                                                                        //Aumentar el contador
    }
    if (IP_pcs_1 == 0) {
        strcpy(IP_brd, DEFAULT_BRD_ADDR);                                           //Si la configuracin falla, se establece una dirección predeterminada
    } else {
        fflush(stdout);
        sprintf(IP_brd, "%d.%d.%d.255", IP_pcs_1, IP_pcs_2, IP_pcs_3);              //Nueva dirección IP de broadcast creada
    }
}