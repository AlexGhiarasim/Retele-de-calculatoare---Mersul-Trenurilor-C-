#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <time.h>
#include <ctype.h>

#define PORT 2908 /* portul folosit */
#define MAX_CLIENTS 100
extern int errno; /* codul de eroare returnat de anumite apeluri */

typedef struct thData
{
  int idThread; // id-ul thread-ului tinut in evidenta de acest program
  int cl;       // descriptorul intors de accept
} thData;

int server_opened = 1;
thData allClients[MAX_CLIENTS];
int nrClients = 0;
static void *treat(void *); // functia executata de fiecare thread ce realizeaza comunicarea cu clientii
static void *command_delay_queue(void *arg); // pentru apelurile -delay:
void *send_program(void *arg); // pentru a trimite programul la clienti automat la x secunde
void raspunde(void *);

char all_commands[][100] = {"-help", "-ruta: ", "-mersuri", "-plecari: ", "-sosiri: ", "-delay: ", "-quit"};
struct statie
{
  char nume_statie[50]; // ex: Pascani
  char ora[6];          // ex: 03:14
};

struct info_about_trains
{
  char numar[10];
  struct statie plecare;
  struct statie sosire;
  char intarziere[10];
} trains[10];
int number = 0;

struct Queue
{
  char message_to_send[2000];
  char IdTren[6];
  char min[4];
};

int nrCommands = 0;
struct Queue queue[100];

void remove_command_from_queue()
{
  for (int i = 0; i < nrCommands - 1; i++)
  {
    queue[i] = queue[i + 1];
  }
  nrCommands--;
}
void add_command_to_queue(const char message_to_send[], const char IdTren[], const char min[])
{
  strcpy(queue[nrCommands].IdTren, IdTren);
  strcpy(queue[nrCommands].message_to_send, message_to_send);
  strcpy(queue[nrCommands].min, min);
  nrCommands++;
}

void remove_client(int descriptor)
{
  for (int i = 0; i < nrClients; i++)
  {
    if (allClients[i].cl == descriptor)
    {
      for (int j = i; j < nrClients - 1; j++)
        allClients[j] = allClients[j + 1];
      nrClients--;
      break;
    }
  }
}

void parseXMLfile(const char *filename)
{
  xmlDocPtr doc;
  xmlNodePtr trenNode;

  doc = xmlReadFile(filename, NULL, 0);
  if (doc == NULL)
  {
    printf("Eroare la parsarea fisierului");
    exit(errno);
  }

  trenNode = xmlDocGetRootElement(doc); // punct de plecare la parsare
  for (trenNode = trenNode->children; trenNode != NULL; trenNode = trenNode->next)
  {
    if (trenNode->type == XML_ELEMENT_NODE)
    {
      xmlNodePtr numarNode = NULL;
      xmlNodePtr plecareNode = NULL;
      xmlNodePtr sosireNode = NULL;
      xmlNodePtr intarziereNode = NULL;

      for (xmlNodePtr child = trenNode->children; child != NULL; child = child->next) // se parcurge fiecare copil al nodului specific unui tren
      {
        if (child->type == XML_ELEMENT_NODE)
        {
          if (xmlStrcmp(child->name, (const xmlChar *)"numar") == 0)
          {
            numarNode = child;
          }
          else if (xmlStrcmp(child->name, (const xmlChar *)"plecare") == 0)
          {
            plecareNode = child;
          }
          else if (xmlStrcmp(child->name, (const xmlChar *)"sosire") == 0)
          {
            sosireNode = child;
          }
          else if (xmlStrcmp(child->name, (const xmlChar *)"intarziere") == 0)
          {
            intarziereNode = child;
          }
        }
      }
      xmlNodePtr plecareStatieNode = xmlFirstElementChild(plecareNode);
      xmlNodePtr plecareOraNode = xmlLastElementChild(plecareNode);
      xmlNodePtr sosireStatieNode = xmlFirstElementChild(sosireNode);
      xmlNodePtr sosireOraNode = xmlLastElementChild(sosireNode);

      if (numarNode && plecareNode && sosireNode && intarziereNode)
      {
        // printf("Numar tren: %s\n", xmlNodeGetContent(numarNode));
        //  printf("Plecare: %s la ora %s\n", xmlNodeGetContent(plecareStatieNode), xmlNodeGetContent(plecareOraNode));
        //  printf("Sosire: %s la ora %s\n", xmlNodeGetContent(sosireStatieNode), xmlNodeGetContent(sosireOraNode));
        //  printf("Intarziere: %s\n\n", xmlNodeGetContent(intarziereNode));
        number = number + 1;
        strcpy(trains[number - 1].numar, (const char *)xmlNodeGetContent(numarNode));
        strcpy(trains[number - 1].plecare.nume_statie, (const char *)xmlNodeGetContent(plecareStatieNode));
        strcpy(trains[number - 1].plecare.ora, (const char *)xmlNodeGetContent(plecareOraNode));
        strcpy(trains[number - 1].sosire.nume_statie, (const char *)xmlNodeGetContent(sosireStatieNode));
        strcpy(trains[number - 1].sosire.ora, (const char *)xmlNodeGetContent(sosireOraNode));
        strcpy(trains[number - 1].intarziere, (const char *)xmlNodeGetContent(intarziereNode));
      }
    }
  }

  xmlFreeDoc(doc);
}
void updateDelay(const char *numarTren, const char *newDelay)
{
  xmlDocPtr doc;
  xmlNodePtr radacina, trenNode;

  doc = xmlReadFile("infos_trains.xml", NULL, 0);
  if (doc == NULL)
  {
    printf("Eroare la parsarea fisierului");
    exit(errno);
  }

  radacina = xmlDocGetRootElement(doc);

  for (trenNode = radacina->children; trenNode; trenNode = trenNode->next)
  {
    if (trenNode->type == XML_ELEMENT_NODE && xmlStrcmp(trenNode->name, (const xmlChar *)"tren") == 0)
    {
      xmlNodePtr numarNode = trenNode->children;
      while (numarNode)
      {
        if (numarNode->type == XML_ELEMENT_NODE && xmlStrcmp(numarNode->name, (const xmlChar *)"numar") == 0)
        {
          xmlChar *numarValue = xmlNodeGetContent(numarNode);
          if (numarValue != NULL && xmlStrcmp(numarValue, (const xmlChar *)numarTren) == 0)
          {
            xmlNodePtr intarziereNode = trenNode->children;
            while (intarziereNode)
            {
              if (intarziereNode->type == XML_ELEMENT_NODE && xmlStrcmp(intarziereNode->name, (const xmlChar *)"intarziere") == 0)
              {
                xmlNodeSetContent(intarziereNode, (const xmlChar *)newDelay);
                xmlSaveFile("infos_trains.xml", doc);
                break;
              }
              intarziereNode = intarziereNode->next;
            }
          }
          xmlFree(numarValue);
        }
        numarNode = numarNode->next;
      }
    }
  }

  xmlFreeDoc(doc);
}
void print_infos_about_train(int index, char message_to_send[])
{
  sprintf(message_to_send + strlen(message_to_send),
          "\nTrenul \033[1;34m%s\033[0m: plecare %s, ora \e[4;37m%s\e[0m, sosire %s, ora \e[4;37m%s\e[0m",
          trains[index].numar,
          trains[index].plecare.nume_statie,
          trains[index].plecare.ora,
          trains[index].sosire.nume_statie,
          trains[index].sosire.ora);

  time_t timp = time(NULL);
  struct tm *current_time = localtime(&timp);
  int h_sosire, min_sosire, h_plecare, min_plecare;
  sscanf(trains[index].sosire.ora, "%d:%d", &h_sosire, &min_sosire);
  sscanf(trains[index].plecare.ora, "%d:%d", &h_plecare, &min_plecare);

  h_sosire = (h_sosire + atoi(trains[index].intarziere) / 60) % 24;
  min_sosire = (min_sosire + atoi(trains[index].intarziere) % 60);
  h_sosire += min_sosire / 60;
  min_sosire = min_sosire % 60;

  h_plecare = (h_plecare + atoi(trains[index].intarziere) / 60) % 24;
  min_plecare = (min_plecare + atoi(trains[index].intarziere) % 60);
  h_plecare += min_plecare / 60;
  min_plecare = min_plecare % 60;

  if (current_time->tm_hour > h_plecare || (current_time->tm_hour == h_plecare && current_time->tm_min > min_plecare)) // daca trenul a plecat
  {
    if (atoi(trains[index].intarziere) == 0)
    {
      if (current_time->tm_hour < h_sosire || (current_time->tm_hour == h_sosire && current_time->tm_min < min_sosire))
      {
        strcat(message_to_send, "\n[la timp la sosirea in ");
        strcat(message_to_send, trains[index].sosire.nume_statie);
        strcat(message_to_send, "]");
      }
      strcat(message_to_send, "  [URMATOAREA PLECARE MAINE]\n");
    }
    else if (atoi(trains[index].intarziere) > 0)
    {
      strcat(message_to_send, "\n[cu \033[1;31m+");
      strcat(message_to_send, trains[index].intarziere);
      strcat(message_to_send, " \033[0mminute intarziere la sosirea in ");
      strcat(message_to_send, trains[index].sosire.nume_statie);
      strcat(message_to_send, "]   [URMATOAREA PLECARE MAINE]\n");
    }
    else
    {
      strcat(message_to_send, "\n[cu \033[1;32m");
      strcat(message_to_send, trains[index].intarziere);
      strcat(message_to_send, " \033[0mminute mai devreme la sosirea in ");
      strcat(message_to_send, trains[index].sosire.nume_statie);
      strcat(message_to_send, "]   [URMATOAREA PLECARE MAINE]\n");
    }
  }
  else if ((current_time->tm_hour < h_plecare || (current_time->tm_hour == h_plecare && current_time->tm_min < min_plecare)) && current_time->tm_hour + 2 >= h_plecare)
  {
    if (atoi(trains[index].intarziere) > 0)
    {
      strcat(message_to_send, "\n[cu \033[1;31m+");
      strcat(message_to_send, trains[index].intarziere);
      strcat(message_to_send, " \033[0mminute intarziere de la ora plecarii din ");
    }
    else
    {
      strcat(message_to_send, " \n[pleaca la timp din ");
    }
    strcat(message_to_send, trains[index].plecare.nume_statie);
    strcat(message_to_send, "]   [URMATOAREA PLECARE ASTAZI]\n");
  }
  else
    strcat(message_to_send, " \n[URMATOAREA PLECARE ASTAZI]\n");
}
void sendDelayToAll(const char *message, thData *allClients, int nrClients)
{
  for (int i = 0; i < nrClients; i++)
  {
    if (write(allClients[i].cl, message, strlen(message) + 1) <= 0)
    {
      perror("Eroare la write() catre clienti.");
    }
  }
}
void resetDelayToDefault()
{
  int timp_last_modified[6];
  xmlDocPtr doc = xmlReadFile("date_modified.xml", NULL, 0);
  if (doc == NULL)
  {
    fprintf(stderr, "Eroare la citirea fișierului XML\n");
    return;
  }

  xmlNodePtr root = xmlDocGetRootElement(doc);

  // Obținerea conținutului text din nodul rădăcină (data și ora)
  const char *date_str = (const char *)xmlNodeGetContent(root);
  time_t timp = time(NULL);
  struct tm *current_time = localtime(&timp);

  sscanf(date_str, "%d-%d-%d %d:%d",
         &timp_last_modified[0], &timp_last_modified[1], &timp_last_modified[2],
         &timp_last_modified[3], &timp_last_modified[4]);

  // Anul în structura `tm` este offset de la 1900
  timp_last_modified[0] -= 1900;
  // Luna în structura `tm` începe de la 0
  timp_last_modified[1]--;
  for (int i = 0; i < number; i++)
  {
    int h_sosire, min_sosire, h_plecare, min_plecare;
    sscanf(trains[i].sosire.ora, "%d:%d", &h_sosire, &min_sosire);
    sscanf(trains[i].plecare.ora, "%d:%d", &h_plecare, &min_plecare);

    h_sosire = (h_sosire + atoi(trains[i].intarziere) / 60) % 24;
    min_sosire = (min_sosire + atoi(trains[i].intarziere) % 60);
    h_sosire += min_sosire / 60;
    min_sosire = min_sosire % 60;
    if (timp_last_modified[0] == current_time->tm_year && timp_last_modified[1] == current_time->tm_mon)
    {
      if (current_time->tm_mday == timp_last_modified[2] && h_sosire > h_plecare)
      {
        if (h_sosire < current_time->tm_hour && h_sosire > h_plecare)
        {
          updateDelay(trains[i].numar, "0");
          strcpy(trains[i].intarziere, "0");
        }
        else if (h_sosire == current_time->tm_hour && h_sosire > h_plecare)
        {
          if (min_sosire < current_time->tm_min)
          {
            updateDelay(trains[i].numar, "0");
            strcpy(trains[i].intarziere, "0");
          }
        }
        if (((h_plecare > current_time->tm_hour + 2) || (atoi(trains[i].intarziere) < 0 && h_plecare > current_time->tm_hour)) && h_sosire > h_plecare)
        {
          updateDelay(trains[i].numar, "0");
          strcpy(trains[i].intarziere, "0");
        }
        else if (h_plecare == current_time->tm_hour + 2 && h_sosire > h_plecare)
        {
          if (min_plecare > current_time->tm_min)
          {
            updateDelay(trains[i].numar, "0");
            strcpy(trains[i].intarziere, "0");
          }
        }
      }
      else if (current_time->tm_mday - timp_last_modified[2] <= 1)
      {
        if (((current_time->tm_hour < h_plecare - 2) && (current_time->tm_hour > h_sosire || (current_time->tm_hour == h_sosire && current_time->tm_min > min_sosire))) && h_plecare > h_sosire)
        {
          updateDelay(trains[i].numar, "0");
          strcpy(trains[i].intarziere, "0");
        }
      }
    }
  }
  // Eliberarea resurselor
  xmlFreeDoc(doc);
}
void write_lastTimeModified() // se obtine data ultimei comenzi procesate de server
{
  xmlDocPtr doc = xmlNewDoc((const xmlChar *)"1.0");
  xmlNodePtr root_node = xmlNewNode(NULL, (const xmlChar *)"date_modified.xml");
  xmlDocSetRootElement(doc, root_node);

  time_t my_time = time(NULL);
  struct tm *info = localtime(&my_time);

  char buffer[80];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", info);

  xmlNodePtr date_node = xmlNewText((const xmlChar *)buffer);
  xmlAddChild(root_node, date_node);

  xmlSaveFile("date_modified.xml", doc);
  xmlFreeDoc(doc);
}

void help_command(char a[100], struct thData tdL) // -help
{
  char message_to_send[2000] = "                 \033[1;35m-- COMENZI: --\033[0m\n";
  strcat(message_to_send, "  -help\n");
  strcat(message_to_send, "  -ruta:                           <<statieA>> <<statieB>> \n");
  strcat(message_to_send, "  -mersuri \n");
  strcat(message_to_send, "  -plecari:                        <<statie>> <<h>>\n");
  strcat(message_to_send, "  -sosiri:                         <<statie>> <<h>>\n");
  strcat(message_to_send, "  -delay:                          <<IdTren>> <<min>>\n");
  strcat(message_to_send, "  -quit");
  if (write(tdL.cl, message_to_send, strlen(message_to_send) + 1) <= 0)
  {
    printf("[Thread %d] ", tdL.idThread);
    perror("[Thread] Eroare la write() catre client.\n");
  }
  else
    printf("[Thread %d] Mesajul a fost transmis cu succes.\n", tdL.idThread);
}
void ruta_command(char a[100], struct thData tdL) // ruta: Pascani Iasi
{
  int ok = 1;
  char message_to_send[2000] = "";

  char *p = strtok(a, ":\0");
  p = strtok(NULL, " \0");
  char statie1[100], statie2[100];
  if (ok == 1)
  {
    if (p == NULL)
    {
      ok = 0;
      strcat(message_to_send, " Numar de argumente prea mic.\n Utilizeaza -ruta: <<statieA>> <<statieB>>");
    }
    else
    {
      strcpy(statie1, p);
      p = strtok(NULL, " \0");
    }
    if (p == NULL)
    {
      ok = 0;
      strcat(message_to_send, " Numar de argumente prea mic.\n Utilizeaza -ruta: <<statieA>> <<statieB>>");
    }
    else
    {
      strcpy(statie2, p);
      p = strtok(NULL, " \0");
    }
  }
  if (ok == 1 && p != NULL)
  {
    strcat(message_to_send, " Numar de argumente prea mare.\n Utilizeaza -ruta: <<statieA>> <<statieB>>");
  }
  if (ok == 1)
  {
    strcat(message_to_send, "                 \033[1;35m-- RUTA CAUTATA --\033[0m");
    int exists_train = 0;
    for (int i = 0; i < number; i++)
    {
      if (strcmp(statie1, trains[i].plecare.nume_statie) == 0 && strcmp(statie2, trains[i].sosire.nume_statie) == 0)
      {
        print_infos_about_train(i, message_to_send);
        exists_train = 1;
      }
    }
    if (exists_train == 0)
      strcat(message_to_send, " Nu exista trenuri pe ruta specificata. \n");
  }
  if (write(tdL.cl, message_to_send, strlen(message_to_send) + 1) <= 0)
  {
    printf("[Thread %d] ", tdL.idThread);
    perror("[Thread] Eroare la write() catre client.\n");
  }
  else
    printf("[Thread %d] Mesajul a fost transmis cu succes.\n", tdL.idThread);
}
void mersuri_command(struct thData tdL) // -mersuri
{
  char message_to_send[2000] = "                 \033[1;35m-- PROGRAM TRENURI --\033[0m";
  for (int i = 0; i < number; i++)
  {
    print_infos_about_train(i, message_to_send);
  }
  if (write(tdL.cl, message_to_send, strlen(message_to_send) + 1) <= 0)
  {
    printf("[Thread %d] ", tdL.idThread);
    perror("[Thread] Eroare la write() catre client.\n");
  }
  else
    printf("[Thread %d] Mesajul a fost transmis cu succes.\n", tdL.idThread);
}
void plecari_command(char a[100], struct thData tdL) // -plecari: Iasi 5
{
  int ok = 1;
  char message_to_send[2000] = "                 \033[1;35m-- PLECARI --\033[0m";
  char *p = strtok(a, ":\0");
  p = strtok(NULL, " \0");
  char numeStatie[50] = "", h[3] = "";
  if (p == NULL)
  {
    ok = 0;
    strcat(message_to_send, " \nNumar de argumente prea mic.\n Utilizeaza -plecari: <<statie>> <<h>>");
  }
  else
  {
    strcat(numeStatie, p);
    p = strtok(NULL, " \0");
  }
  if (ok == 1 && p == NULL)
  {
    ok = 0;
    strcat(message_to_send, " \nNumar de argumente prea mic.\n Utilizeaza -plecari: <<statie>> <<h>>");
  }
  else if (ok == 1 && p != NULL)
  {
    strcat(h, p);
    p = strtok(NULL, " \0");
  }
  if (ok == 1 && p != NULL)
  {
    strcat(message_to_send, " \nNumar de argumente prea mare.\n -plecari: <<statie>> <<h>>\n");
    ok = 0;
  }
  if (ok == 1)
  {
    if (((!isdigit(h[0]) || !isdigit(h[1])) && ((strlen(h) == 2))) || (!isdigit(h[0]) && strlen(h) != 1))
    {
      ok = 0;
      strcat(message_to_send, " \nArgumentul <<h>> trebuie sa fie de tip strict natural (1 <= h <= 24)\n");
    }
    else
    {
      time_t timp = time(NULL);
      struct tm *current_time = localtime(&timp);
      for (int i = 0; i < number; i++)
      {
        if (strcmp(trains[i].plecare.nume_statie, numeStatie) == 0)
        {
          char ora[3], min[3];
          ora[0] = trains[i].plecare.ora[0];
          ora[1] = trains[i].plecare.ora[1];
          ora[2] = '\0';
          min[0] = trains[i].plecare.ora[3];
          min[1] = trains[i].plecare.ora[4];
          min[2] = '\0';
          int ora_cu_intarziere = (atoi(ora) + (atoi(min) + atoi(trains[i].intarziere)) / 60) % 24;
          int min_cu_intarziere = (atoi(min) + atoi(trains[i].intarziere)) % 60;

          if ((current_time->tm_hour + atoi(h) > ora_cu_intarziere && (current_time->tm_hour < ora_cu_intarziere || (current_time->tm_hour == ora_cu_intarziere && current_time->tm_min < min_cu_intarziere))) || (current_time->tm_hour + atoi(h) == ora_cu_intarziere && current_time->tm_min < min_cu_intarziere))
          {
            print_infos_about_train(i, message_to_send);
          }
          if (current_time->tm_hour + atoi(h) >= 24)
          {
            int nextd = (current_time->tm_hour + atoi(h)) % 24;
            if ((nextd > ora_cu_intarziere) || (nextd + atoi(h) == ora_cu_intarziere && current_time->tm_min < min_cu_intarziere))
            {
              print_infos_about_train(i, message_to_send);
            }
          }
        }
      }
    }
  }

  if (write(tdL.cl, message_to_send, strlen(message_to_send) + 1) <= 0)
  {
    printf("[Thread %d] ", tdL.idThread);
    perror("[Thread] Eroare la write() catre client.\n");
  }
  else
    printf("[Thread %d] Mesajul a fost transmis cu succes.\n", tdL.idThread);
}
void sosiri_command(char a[100], struct thData tdL) // -sosiri: Iasi 5
{
  int ok = 1;
  char message_to_send[2000] = "                 \033[1;35m-- SOSIRI --\033[0m";
  char *p = strtok(a, ":");
  p = strtok(NULL, " \0");
  char numeStatie[50] = "", h[3] = "";
  if (p == NULL)
  {
    ok = 0;
    strcat(message_to_send, " \nNumar de argumente prea mic.\n Utilizeaza -sosiri: <<statie>> <<h>>");
  }
  else
  {
    strcat(numeStatie, p);
    p = strtok(NULL, " \0");
  }
  if (ok == 1 && p == NULL)
  {
    ok = 0;
    strcat(message_to_send, " \nNumar de argumente prea mic.\n Utilizeaza -sosiri: <<statie>> <<h>>");
  }
  else if (ok == 1 && p != NULL)
  {
    strcat(h, p);
    p = strtok(NULL, " \0");
  }
  if (ok == 1 && p != NULL)
  {
    strcat(message_to_send, " \nNumar de argumente prea mare.\n Utilizeaza -plecari: <<sosiri>> <<h>>\n");
    ok = 0;
  }
  if (ok == 1)
  {
    if (((!isdigit(h[0]) || !isdigit(h[1])) && ((strlen(h) == 2))) || (!isdigit(h[0]) && strlen(h) != 1))
    {
      ok = 0;
      strcat(message_to_send, " Argumentul <<h>> trebuie sa fie de tip strict natural (1 <= h <= 24)\n");
    }
    else
    {
      time_t timp = time(NULL);
      struct tm *current_time = localtime(&timp);
      for (int i = 0; i < number; i++)
      {
        if (strcmp(trains[i].sosire.nume_statie, numeStatie) == 0)
        {
          char ora[3], min[3];
          ora[0] = trains[i].sosire.ora[0];
          ora[1] = trains[i].sosire.ora[1];
          ora[2] = '\0';
          min[0] = trains[i].sosire.ora[3];
          min[1] = trains[i].sosire.ora[4];
          min[2] = '\0';
          int ora_cu_intarziere = (atoi(ora) + (atoi(min) + atoi(trains[i].intarziere)) / 60) % 24;
          int min_cu_intarziere = (atoi(min) + atoi(trains[i].intarziere)) % 60;

          if ((current_time->tm_hour + atoi(h) > ora_cu_intarziere || (current_time->tm_hour > ora_cu_intarziere || (current_time->tm_hour == ora_cu_intarziere && current_time->tm_min > min_cu_intarziere))) || (current_time->tm_hour + atoi(h) == ora_cu_intarziere && current_time->tm_min > min_cu_intarziere))
          {
            print_infos_about_train(i, message_to_send);
          }
          if (current_time->tm_hour + atoi(h) >= 24)
          {
            int nextd = (current_time->tm_hour + atoi(h)) % 24;
            if ((nextd < ora_cu_intarziere) || (nextd + atoi(h) == ora_cu_intarziere && current_time->tm_min > min_cu_intarziere))
            {
              print_infos_about_train(i, message_to_send);
            }
          }
        }
      }
    }
  }

  if (write(tdL.cl, message_to_send, strlen(message_to_send) + 1) <= 0)
  {
    printf("[Thread %d] ", tdL.idThread);
    perror("[Thread] Eroare la write() catre client.\n");
  }
  else
    printf("[Thread %d] Mesajul a fost transmis cu succes.\n", tdL.idThread);
}
void delay_command(char a[100], struct thData tdL) // -delay: IR2020 10
{
  int ok = 1, sent = 0;
  char message_to_send[2000] = "                 \033[1;35m-- DELAY --\033[0m";

  char *p = strtok(a, ":");
  p = strtok(NULL, " ");
  char IDTren[6] = "", min[4] = "";
  if (ok == 1)
  {
    if (p == NULL)
    {
      ok = 0;
      strcat(message_to_send, " \nNumar de argumente prea mic.\n Utilizeaza -delay: <<idTren>> <<min>>");
    }
    else
    {
      strcat(IDTren, p);
      p = strtok(NULL, " \0");
    }
    if (p == NULL)
    {
      ok = 0;
      strcat(message_to_send, " \nNumar de argumente prea mic.\n Utilizeaza -delay: <<idTren>> <<min>>");
    }
    else
    {
      strcat(min, p);
      p = strtok(NULL, " \0");
    }
  }
  if (ok == 1 && p != NULL)
  {
    strcat(message_to_send, " \nNumar de argumente prea mare.\n Utilizeaza -delay: <<idTren>> <<min>>");
    ok = 0;
  }
  int nr = atoi(min);
  if (nr == 0 && ok == 1 && strcmp("0", min) != 0)
  {
    ok = 0;
    strcat(message_to_send, " \nArgumentul <<min>> trebuie sa fie de tip intreg");
  }
  time_t timp = time(NULL);
  struct tm *current_time = localtime(&timp);
  int modified = 1;
  if (ok == 1)
  {
    for (int i = 0; i < number; i++)
    {
      if (strcmp(IDTren, trains[i].numar) == 0)
      {
        modified = 0;
        int h_sosire, min_sosire, h_plecare, min_plecare;
        sscanf(trains[i].sosire.ora, "%d:%d", &h_sosire, &min_sosire);
        sscanf(trains[i].plecare.ora, "%d:%d", &h_plecare, &min_plecare);
        int ok1 = 0, ok2 = 0, ok3 = 0;

        if (h_plecare > h_sosire) // trenul ajunge maine
        {
          if (current_time->tm_hour < h_sosire || (current_time->tm_hour == h_sosire && current_time->tm_min < min_sosire))
            ok1 = 1;
        }
        else // trenul ajunge astazi
        {
          if (current_time->tm_hour + 2 >= h_plecare || (current_time->tm_hour + 2 == h_plecare && current_time->tm_min > h_plecare))
            if (atoi(min) >= 0 && (current_time->tm_hour < h_sosire || (current_time->tm_hour == h_sosire && current_time->tm_min < min_sosire)))
              ok2 = 1;
          if (current_time->tm_hour > h_plecare || (current_time->tm_hour == h_plecare && current_time->tm_min > h_plecare))
            if (atoi(min) < 0 && (current_time->tm_hour < h_sosire || (current_time->tm_hour == h_sosire && current_time->tm_min < min_sosire)))
              ok3 = 1;
        }

        if (ok1 || ok2 || ok3)
        {
          sent = 1;
          strcpy(trains[i].intarziere, min);
          if (nr > 0)
          {
            sprintf(message_to_send + strlen(message_to_send),
                    "\n(UPDATE) Trenul \033[1;34m%s\033[0m: din directia %s, ora \e[4;37m%s\e[0m, spre %s, ora \e[4;37m%s\e[0m, are o intarziere de \033[1;31m+%s\033[0m minute!!",
                    trains[i].numar,
                    trains[i].plecare.nume_statie,
                    trains[i].plecare.ora,
                    trains[i].sosire.nume_statie,
                    trains[i].sosire.ora,
                    trains[i].intarziere);
            if (current_time->tm_hour < h_plecare || (current_time->tm_hour == h_plecare && current_time->tm_min < min_plecare))
              strcat(message_to_send, " (de la ora plecarii)");
            add_command_to_queue(message_to_send, trains[i].numar, min);
            break;
          }
          else if (nr < 0 && current_time->tm_hour >= h_plecare)
          {
            sprintf(message_to_send + strlen(message_to_send),
                    "\n(UPDATE) Trenul \033[1;34m%s\033[0m: din directia %s, ora \e[4;37m%s\e[0m, spre %s, ora \e[4;37m%s\e[0m, soseste mai devreme cu \033[1;32m%s\033[0m minute!!",
                    trains[i].numar,
                    trains[i].plecare.nume_statie,
                    trains[i].plecare.ora,
                    trains[i].sosire.nume_statie,
                    trains[i].sosire.ora,
                    trains[i].intarziere);
            updateDelay(IDTren, min);
            add_command_to_queue(message_to_send, trains[i].numar, min);
            break;
          }
          else if (nr == 0)
          {
            sprintf(message_to_send + strlen(message_to_send),
                    "\n(UPDATE) Trenul \033[1;34m%s\033[0m: din directia %s, ora \e[4;37m%s\e[0m, spre %s, ora \e[4;37m%s\e[0m",
                    trains[i].numar,
                    trains[i].plecare.nume_statie,
                    trains[i].plecare.ora,
                    trains[i].sosire.nume_statie,
                    trains[i].sosire.ora);
            if (current_time->tm_hour < h_plecare || (current_time->tm_hour == h_plecare && current_time->tm_min < min_plecare))
              strcat(message_to_send, ", pleaca la timp!!");
            else
              strcat(message_to_send, ", soseste la timp!!");
            add_command_to_queue(message_to_send, trains[i].numar, min);
            break;
          }
          ok = 0;
        }
        else
        {
          strcat(message_to_send, " \n  Comunicarea intarzierilor se poate face incepand cu 2 ore inainte de plecarea trenului.\n");
          strcat(message_to_send, "  Comunicarea sosirii mai devreme a unui tren se poate face doar in timpul mersului trenului respectiv.\n");
        }
      }
    }
  }
  if (modified == 1 && ok == 1)
    strcat(message_to_send, " \nTrenul nu a fost gasit.\n");
  if (sent == 0)
  {
    if (write(tdL.cl, message_to_send, strlen(message_to_send) + 1) <= 0)
    {
      printf("[Thread %d] ", tdL.idThread);
      perror("[Thread] Eroare la write() catre client.\n");
    }
    else
      printf("[Thread %d] Mesajul a fost transmis cu succes.\n", tdL.idThread);
  }
}
void quit_command(char a[100], struct thData tdL) // -quit
{
  char message_to_send[2000] = "";
  strcat(message_to_send, "Client deconectat cu succes!\n");
  if (write(tdL.cl, message_to_send, strlen(message_to_send) + 1) <= 0)
  {
    printf("[Thread %d] ", tdL.idThread);
    perror("[Thread] Eroare la write() catre client.\n");
  }
  else
    printf("[Thread %d] Deconectat cu succes.\n", tdL.idThread);
}
void recognise_command(struct thData tdL, char a[100])
{
  resetDelayToDefault();
  write_lastTimeModified();
  if (strcmp(a, all_commands[0]) == 0)
  {
    help_command(a, tdL);
  }
  else if (strstr(a, all_commands[1]) != NULL)
  {
    ruta_command(a, tdL);
  }
  else if (strcmp(a, all_commands[2]) == 0)
  {
    mersuri_command(tdL);
  }
  else if (strstr(a, all_commands[3]) != NULL)
  {
    plecari_command(a, tdL);
  }
  else if (strstr(a, all_commands[4]) != NULL)
  {
    sosiri_command(a, tdL);
  }
  else if (strstr(a, all_commands[5]) != NULL)
  {
    delay_command(a, tdL);
  }
  else if (strstr(a, all_commands[6]) != NULL)
  {
    quit_command(a, tdL);
  }
  else
  {
    char message_to_send[200] = "";
    strcat(message_to_send, "Comanda necunoscuta\n");
    strcat(message_to_send, "Foloseste comanda -help pentru a vizualiza comenzile existente si argumentele necesare");

    if (write(tdL.cl, message_to_send, strlen(message_to_send) + 1) <= 0)
    {
      printf("[Thread %d] ", tdL.idThread);
      perror("[Thread] Eroare la write() catre client.\n");
    }
    else
      printf("[Thread %d] Mesajul a fost transmis cu succes.\n", tdL.idThread);
  }
}

int main()
{
  struct sockaddr_in server; // structura folosita de server
  struct sockaddr_in from;
  int sd; // descriptorul de socket
  pthread_t th[100];
  int i = 0;

  const char *path_XML_file = "infos_trains.xml";
  resetDelayToDefault();
  parseXMLfile(path_XML_file); // parsare date din fisierul XML, se executa o data, apoi actualizarile de fiecare data cand este nevoie
  /* crearea unui socket */
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("[server]Eroare la socket().\n");
    return errno;
  }
  /* utilizarea optiunii SO_REUSEADDR */
  int on = 1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  /* pregatirea structurilor de date */
  bzero(&server, sizeof(server));
  bzero(&from, sizeof(from));

  /* umplem structura folosita de server */
  /* stabilirea familiei de socket-uri */
  server.sin_family = AF_INET;
  /* acceptam orice adresa */
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  /* utilizam un port utilizator */
  server.sin_port = htons(PORT);

  /* atasam socketul */
  if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
  {
    perror("[server]Eroare la bind().\n");
    return errno;
  }

  /* punem serverul sa asculte daca vin clienti sa se conecteze */
  if (listen(sd, 2) == -1)
  {
    perror("[server]Eroare la listen().\n");
    return errno;
  }
  pthread_t thread1;
  pthread_create(&thread1, NULL, &command_delay_queue, NULL);
  //pthread_t thread2;
  //pthread_create(&thread2, NULL, &send_program, NULL);
  while (1)
  {
    int client;
    thData *td; // parametru functia executata de thread
    int length = sizeof(from);

    printf("[server]Asteptam la portul %d...\n", PORT);
    fflush(stdout);
    // client= malloc(sizeof(int));
    /* acceptam un client (stare blocanta pina la realizarea conexiunii) */
    if ((client = accept(sd, (struct sockaddr *)&from, (socklen_t *)&length)) < 0)
    {
      perror("[server]Eroare la accept().\n");
      continue;
    }
    /* s-a realizat conexiunea, se astepta mesajul */
    // int idThread; //id-ul threadului
    // int cl; //descriptorul intors de accept
    td = (struct thData *)malloc(sizeof(struct thData));
    td->idThread = i++;
    td->cl = client;

    if (nrClients < MAX_CLIENTS)
    {
      allClients[nrClients++] = *td;
    }
    pthread_create(&th[i], NULL, &treat, td);
  } // while
  server_opened = 0;
  return 0;
}
void *treat(void *arg)
{
  struct thData tdL;
  tdL = *((struct thData *)arg);
  printf("[Thread %d] Asteptam mesajul...\n", tdL.idThread);
  fflush(stdout);
  pthread_detach(pthread_self());
  raspunde((struct thData *)arg);
  /* am terminat cu acest client, inchidem conexiunea */
  close(tdL.cl);
  free(arg);
  return NULL;
}
void *command_delay_queue(void *arg)
{
  pthread_detach(pthread_self());
  while (1)
  {
    if (nrCommands > 0)
    {
      updateDelay(queue[0].IdTren, queue[0].min);
      sendDelayToAll(queue[0].message_to_send, allClients, nrClients);
      remove_command_from_queue();
    }
    else
    {
      sleep(0.5);
    }
  }
  return NULL;
}
void *send_program(void *arg)
{
  pthread_detach(pthread_self());
  while (1)
  {
    sleep(8);
    char message_to_send[2000];
    for (int i = 0; i < number; i++)
    {
      print_infos_about_train(i, message_to_send);
    }
    for (int i = 0; i < nrClients; i++)
    {
      if (write(allClients[i].cl, message_to_send, strlen(message_to_send) + 1) <= 0)
      {
        perror("Eroare la write() catre clienti.");
        exit(errno);
      }
    }
    strcpy(message_to_send,"");
  }
  return NULL;
}

void raspunde(void *arg)
{
  char msg_from_client[100];
  struct thData tdL;
  tdL = *((struct thData *)arg);

  while (1)
  {
    // Citim mesajul de la client
    if (read(tdL.cl, msg_from_client, sizeof(msg_from_client)) <= 0)
    {
      printf("[Thread %d]\n", tdL.idThread);
      perror("Eroare la read() de la client.\n");
      break;
    }
    recognise_command(tdL, msg_from_client);
    if (strcmp(msg_from_client, "-quit") == 0)
      break;
    if (server_opened == 0)
      break;
  }
  remove_client(tdL.cl);
  close(tdL.cl);
}