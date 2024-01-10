#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <arpa/inet.h>

extern int errno;
int port;

void welcome()
{
    printf("              ~~~~~BINE ATI VENIT LA MERSUL TRENURILOR~~~~~\n");
    printf("\n");
    printf(" ~Pentru inceput, Folositi comanda \e[4;37m-help\e[0m pentru a vizualiza tipurile de comenzi~\n");
    printf("\n");
    printf(" Cu ce va putem ajuta astazi?\n \n");
}

int main(int argc, char *argv[])
{
    int sd;                    // descriptorul de socket
    struct sockaddr_in server; // structura folosita pentru conectare
                               // mesajul trimis
    char command[200];         // comanda introdusa de utilizator

    /* exista toate argumentele in linia de comanda? */
    if (argc != 3)
    {
        printf("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
        return -1;
    }

    /* stabilim portul */
    port = atoi(argv[2]);

    /* cream socketul */
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Eroare la socket().\n");
        return errno;
    }
    
    /* umplem structura folosita pentru realizarea conexiunii cu serverul */
    /* familia socket-ului */
    server.sin_family = AF_INET;
    /* adresa IP a serverului */
    server.sin_addr.s_addr = inet_addr(argv[1]);
    /* portul de conectare */
    server.sin_port = htons(port);

    /* ne conectam la server */
    int descriptor;
    if ((descriptor = connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr))) == -1)
    {
        perror("[client]Eroare la connect().\n");
        return errno;
    }

    int pid = fork();
    if (pid == -1)
    {
        perror("Error in fork.\n");
        return errno;
    }
    int server_opened = 1;
    if (pid == 0)
    {
        // fiu
        char message_primit[2000];
        while (1)
        {
            if(server_opened == 0)
               break;
            if (read(sd, message_primit, sizeof(message_primit)) < 0)
            {
                perror("[cl]Eroare la read() de la server.\n");
                return errno;
                break;
            }
            /* afisam mesajul primit */
            printf("\n\033[1;36m[received]:\033[0m %s\n\n", message_primit);
        }
        close(sd);
    }
    else
    {
        //tata
        welcome(); // connected succesfully
        while (1)
        {
            printf("\033[1;35m[send]:\033[0m Introduceti o comanda: ");
            fflush(stdout);
            fgets(command, sizeof(command), stdin);
            command[strcspn(command, "\n")] = '\0';
            /* trimiterea mesajului la server */
            if (write(sd, command, strlen(command) + 1) <= 0)
            {
                perror("[cl]Eroare la write() spre server.\n");
                break;
                server_opened = 0;
            }
            if (strcmp(command, "-quit") == 0)
                break;
            sleep(0.3);
        }
        kill(pid,SIGUSR1);
        close(sd);
    }
}
