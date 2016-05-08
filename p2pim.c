#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <time.h>
#include <netdb.h>
#include <errno.h>
#include <termios.h>
#include <ctype.h>
#include "userlist.h"

#define BUFFER_SIZE 512
#define MAX_FD      50

#define DISCOVERY   0x0001
#define REPLY       0x0002
#define CLOSING     0x0003
#define ESTABLISH   0x0004
#define ACCEPT      0x0005
#define UNAVAILABLE 0x0006
#define USERLIST    0x0007
#define LISTREPLY   0x0008
#define DATA        0x0009
#define DISCONTINUE 0x000A

#define MESSAGEIN   0x0001
#define NUMBERIN    0x0002
#define REQUESTIN   0x0004

int UDPfd, TCPfd, TCPs[MAX_FD-3], msgLen[MAX_FD-3], zeroCount[MAX_FD-3];
char* const short_options = "n:u:t:i:m:p:";  
char hostname[256], *username;
char messages[MAX_FD-3][BUFFER_SIZE];
int uport = 50550, tport = 50551, uitimeout = 5, umtimeout = 60;
int nonCanState = 0;
struct sockaddr_in BroadcastAddress, UserAddress;
struct termios SavedTermAttributes;
struct hostent* Client;
struct pollfd fds[MAX_FD];

void error(const char *message){
    perror(message);
    exit(1);
}

int firstAvailableFD(){
    int i = 0;
    while(i != 2 && fds[i].fd > 0){
        i++;
        if(i >= MAX_FD){
            printf("No available fd\n");
            return -1;
        }
    }
    return i;
}

void DisplayMessage(char *data, int length){
    int Offset = 0;
    int Index;
    
    while(Offset < length){
        printf("%04X ", Offset);
        for(Index = 0; Index < 16; Index++){
            if((Offset + Index) < length){
                printf("%02X ",data[Offset + Index]);
            }
            else{
                printf("   ");
            }
        }
        for(Index = 0; Index < 16; Index++){
            if((Offset + Index) < length){
                if((' ' <= data[Offset + Index])&&(data[Offset + Index] <= '~')){
                    printf("%c",data[Offset + Index]);
                }
                else{
                    printf(".");
                }
            }
            else{
                printf(" ");
            }
        }
        printf("\n");
        Offset += 16;
    }
    printf("\n");
}

struct option long_options[] = {
    { "username",     1,   NULL,    'n'     },  
    { "uport",        1,   NULL,    'u'     },  
    { "tport",        1,   NULL,    't'     },
    { "uitimeout",    1,   NULL,    'i'     },
    { "umtimeout",    1,   NULL,    'm'     },
    { "pport",        1,   NULL,    'p'     },
    {      0,         0,      0,     0      },  
};

static void usage(void){
    printf(
        "Usage: p2pim [options]\n"
        "  -n,   --username       Username\n"
        "  -u,   --uport          UDP port\n"
        "  -t,   --tport          TCP port\n"
        "  -i,   --uitimeout      UDP initial timeout\n"
        "  -m,   --umtimeout      UDP maximum timeout\n"
        "  -p,   --pport          Host/port to send unicast UDP discovery message\n"
    );
}

int header(char* message, uint16_t type, int uport, int tport, char* username){
    int length = 0;
    uint16_t conversion = 0;
    uint32_t conversionL = 0;
    char* temp = message;
    conversion = htons(type);
    message[0] = 'P';
    message[1] = '2';
    message[2] = 'P';
    message[3] = 'I';                      
    temp += 4;
    memcpy(temp, &conversion, 2);                
    temp += 2;
    length = temp - message;
    switch(type){
        case DISCOVERY:
        case REPLY:
        case CLOSING:
            conversion = htons(uport);
            memcpy(temp, &conversion, 2); 
            temp += 2;
            conversion = htons(tport);
            memcpy(temp, &conversion, 2); 
            temp += 2;
            memcpy(temp, hostname, strlen(hostname));
            temp += strlen(hostname);
            *temp = 0;
            temp += 1;
            memcpy(temp, username, strlen(username));
            temp += strlen(username);
            *temp = 0;
            length = temp - message;
            break;
        case ESTABLISH:
            memcpy(temp, username, strlen(username));
            temp += strlen(username);
            *temp = 0;
            length = temp - message;
            break;
        case ACCEPT:
        case UNAVAILABLE:
        case USERLIST:
        case DATA:
        case DISCONTINUE:
            break;
        case LISTREPLY:
            conversionL = htonl(getUserNum());
            memcpy(temp, &conversionL, 4);
            temp += 4;
            struct User* ptr = head;
            if(ptr == NULL){
                printf("No users!\n");
                length = -1;
                break;
            }
            int count = -1;
            while(ptr != NULL){
                count++;
                conversionL = htonl(count);
                memcpy(temp, &conversionL, 4);
                temp += 4;
                conversion = htons(ptr->UDPport);
                memcpy(temp, &conversion, 2);
                temp += 2;
                memcpy(temp, ptr->Hostname, strlen(ptr->Hostname));
                temp += strlen(ptr->Hostname);
                *temp = 0;
                temp += 1;
                conversion = htons(ptr->TCPport);
                memcpy(temp, &conversion, 2);
                temp += 2;
                memcpy(temp, ptr->Username, strlen(ptr->Username));
                temp += strlen(ptr->Username);
                *temp = 0;
                temp += 1;
                ptr = ptr->nextUser;
            }
            length = temp - message;
            break;
        default:
            length = -1;
            break;
    }
    return length;
}

void ResetCanonicalMode(int fd, struct termios *savedattributes){
    tcsetattr(fd, TCSANOW, savedattributes);
}

void SetNonCanonicalMode(int fd, struct termios *savedattributes){
    struct termios TermAttributes;
    
    // Make sure stdin is a terminal. 
    if(!isatty(fd)){
        fprintf (stderr, "Not a terminal.\n");
        exit(0);
    }
    
    // Save the terminal attributes so we can restore them later. 
    tcgetattr(fd, savedattributes);
    
    // Set the funny terminal modes. 
    tcgetattr (fd, &TermAttributes);
    TermAttributes.c_lflag &= ~(ICANON | ECHO); // Clear ICANON and ECHO. 
    TermAttributes.c_cc[VMIN] = 1;
    TermAttributes.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSAFLUSH, &TermAttributes);
}

// Need to be modified
void SignalHandler(int param){
    char buffer[BUFFER_SIZE];
    bzero(buffer, sizeof(buffer));
    int length = header(buffer, CLOSING, uport, tport, username);

    int Result = sendto(UDPfd, buffer, length, 0, (struct sockaddr *)&BroadcastAddress, sizeof(BroadcastAddress));
    if(0 > Result){
        error("ERROR send to client");
    }
    DisplayMessage(buffer, Result);
    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    close(UDPfd);
    close(STDIN_FILENO);
    struct User* temp = head;
    while(temp != NULL){
        close(temp->TCPfd);
        temp = temp->nextUser;
    }
    if(head != NULL)
        deleteList(head);
    exit(0);
}

void TCPmsgProcess(int j, int fd){
    int type, length, Result;
    char sendBuffer[BUFFER_SIZE];
    type = ntohs(*(uint16_t *)(messages[j]+4));
    switch(type){
        case ESTABLISH:
        case LISTREPLY:
        case DATA:
            zeroCount[j] = 1;
            break;
        case ACCEPT:
        case UNAVAILABLE:
            break;
        case USERLIST:
            length = header(sendBuffer, LISTREPLY, uport, tport, username);
            DisplayMessage(sendBuffer, length);
            Result = write(fd, sendBuffer, length);
            if(0 > Result){
	        error("ERROR writing to socket");
            }
            bzero(messages[j], BUFFER_SIZE);
            zeroCount[j] = 0;
            msgLen[j] = 0;
            break;
        case DISCONTINUE:
            close(fd);
            bzero(messages[j], BUFFER_SIZE);
            zeroCount[j] = 0;
            msgLen[j] = 0;
            break;
        default:
            break;
    }
}


void processCommand(char c){
    if(isprint(c)){
        printf("RX: '%c' 0x%02X\n", c, c);   
    }
    else{
        printf("RX: ' ' 0x%02X\n", c);
    }    
    switch(c){
        case 'H': case 'h':
            printf("Help:\n");
            printf("C/c: Connect to a user in list\n");
            printf("D/d: Delete a user in list\n");
            printf("S/s: Speak to a user in list\n");
            break;
        case 'C': case 'c': // Connect to a user
            printList();
            if(getUserNum() != 0){
                int num;
                printf("Which one to connect? Input a number:\n");
                ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                scanf("%d", &num);
                struct User* temp = searchNameByNum(num);
                SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                if(temp->TCPfd > 0){
                    printf("Already connected, press 's' to speak\n");
                    break;
                }
                temp->TCPfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                Client = gethostbyname(temp->Hostname);
                if(NULL == Client){
                    fprintf(stderr,"ERROR, no such host\n");
                    exit(0);
                }
                bzero((char *) &UserAddress, sizeof(UserAddress));
                UserAddress.sin_family = AF_INET;
                bcopy((char *)Client->h_addr, (char *)&UserAddress.sin_addr.s_addr, Client->h_length);
                UserAddress.sin_port = htons(temp->TCPport);
                if(0 > connect(temp->TCPfd, (struct sockaddr *)&UserAddress, sizeof(UserAddress))){
                    error("ERROR connecting");
                }
                
                int first = firstAvailableFD();
                fds[first].fd = temp->TCPfd;
                fds[first].events = POLLIN | POLLPRI;
                char sendBuffer[BUFFER_SIZE];
                int length = header(sendBuffer, ESTABLISH, temp->UDPport, temp->TCPport, temp->Username);
                DisplayMessage(sendBuffer, length);
                int Result = write(temp->TCPfd, sendBuffer, length);
                if(0 > Result){
                    error("ERROR writing to socket");
                }
            }
            break;
        case 'D': case 'd': // Delete a user
            printList();
            if(getUserNum() != 0){
                printf("Which one to delete? Input a number:\n");
            }
            break;
        case 'S': case 's': // Speak to a user
            printList();
            if(getUserNum() != 0){
                int num;
                printf("Which one to connect? Input a number:\n");
                //ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                //scanf("%d", &num);
                //struct User* temp = searchNameByNum(num);

                //if(temp->TCPfd <= 0){
                  //  printf("Connection not established!\n");
                  //  break;
                //}
                //printf("Input the message:\n");
                //char sendBuffer[BUFFER_SIZE];
                //int length = header(sendBuffer, DATA, temp->UDPport, temp->TCPport, temp->Username);
                //fgets(sendBuffer + length, BUFFER_SIZE-1-length, stdin);
                //length += strlen(sendBuffer+length);
                //int Result = write(temp->TCPfd, sendBuffer, length);
                //SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
            }
        default:
            break;
    }
};

int main(int argc, char *argv[])
{
    int Result, rv;
    char recvBuffer[BUFFER_SIZE], sendBuffer[BUFFER_SIZE], tcpBuffer[1];
    struct sockaddr_in ServerAddress, ClientAddress;
    socklen_t ClientLength = sizeof(ClientAddress);
    char RXChar;
    int broadcast = 1, c, length, UserCount = 0, on = 1;
    int timeout = 0, restartDiscover = 1;
    time_t start = time(NULL), diff;
    int nfds = 3;
    // char stdinBuffer[BUFFER_SIZE];
    // struct hostent* Server;

    if(-1 == gethostname(hostname, 255)){
        error("No host name.\n");
        exit(1);
    }
    username = getenv("USER");
    memset(msgLen, 0, MAX_FD-3);
    memset(msgLen, 0, MAX_FD-3);
    tcpBuffer[0] = -1;

    while((c = getopt_long (argc, argv, short_options, long_options, NULL)) != -1){  
        switch (c)  
        {  
            case 'n':
                bzero(username, sizeof(username));
                username = strdup(optarg);
                printf("Username is %s.\n", optarg);  
                break;  
            case 'u':
                uport = atoi(optarg);
                printf("UDP port is %d.\n", uport);  
                break;  
            case 't':
                tport = atoi(optarg);
                printf("TCP port is %d.\n", tport);  
                break;
            case 'i':
                uitimeout = atoi(optarg);
                printf("UDP initial timeout %d.\n", uitimeout);  
                break;  
            case 'm':
                umtimeout = atoi(optarg);
                printf("UDP maximum timeout %d.\n", umtimeout);  
                break;
            case 'p':
                printf("New hosts/ports set.\n");
                break;
            case '?':
                printf("Use default.\n");
                break;
            default:
                usage();
                exit(0);
        }  
    }

    if(-1 == gethostname(hostname, 255)){
        error("No host name");
        exit(1);
    }

    signal(SIGTERM, SignalHandler);
    signal(SIGINT, SignalHandler);




    UDPfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(0 > UDPfd){
        error("ERROR opening UDP socket");
    }
    if((setsockopt(UDPfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast)) == -1){
        perror("setsockopt - SOL_SOCKET ");
        exit(1);
    }

    bzero((char *) &ServerAddress, sizeof(ServerAddress));
    ServerAddress.sin_family = AF_INET;
    ServerAddress.sin_addr.s_addr = INADDR_ANY;
    ServerAddress.sin_port = htons(uport);

    bzero((char *) &BroadcastAddress, sizeof(BroadcastAddress));
    BroadcastAddress.sin_family = AF_INET;
    BroadcastAddress.sin_addr.s_addr = INADDR_BROADCAST;
    BroadcastAddress.sin_port = htons(uport);

    if(0 > bind(UDPfd, (struct sockaddr *)&ServerAddress, sizeof(ServerAddress))){
        error("ERROR on binding");
    }

    TCPfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(0 > TCPfd){
        error("ERROR opening TCP socket");
        exit(1);
    }

    Result = setsockopt(TCPfd, SOL_SOCKET,  SO_REUSEADDR, (char *)&on, sizeof(on));
    if(0 > Result){
        error("Error setting socket reusable");
        exit(1);
    }

    Result = ioctl(TCPfd, FIONBIO, (char *)&on);
    if(0 > Result){
        error("ioctl failed");
        exit(1);
    }

    bzero((char *) &ServerAddress, sizeof(ServerAddress));
    ServerAddress.sin_family = AF_INET;
    ServerAddress.sin_addr.s_addr = INADDR_ANY;
    ServerAddress.sin_port = htons(tport);

    Result = bind(TCPfd, (struct sockaddr *)&ServerAddress, sizeof(ServerAddress));
    if (Result < 0)
    {
        perror("bind() failed");
        close(TCPfd);
        exit(1);
    }

    Result = listen(TCPfd, 32);
    if(0 > Result){
        error("listen failed");
        exit(1);
    }


    fds[0].fd = UDPfd;
    fds[0].events = POLLIN | POLLPRI;

    fds[1].fd = TCPfd;
    fds[1].events = POLLIN | POLLPRI;

    fds[2].fd = STDIN_FILENO;
    fds[2].events = POLLIN | POLLPRI;

    timeout = uitimeout;
    SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);

    while(1){       
        diff = time(NULL) - start;

        if((diff >= timeout && UserCount <= 0) || restartDiscover){
            start = time(NULL);
            bzero(sendBuffer, sizeof(sendBuffer));
            length = header(sendBuffer, DISCOVERY, uport, tport, username);
            if(!restartDiscover){
                timeout = timeout * 2 < umtimeout ? timeout * 2 : umtimeout;
            }
            printf("Sending discovery, timeout = %ds\n", timeout);
            restartDiscover = 0;
            Result = sendto(UDPfd, sendBuffer, length, 0, (struct sockaddr *)&BroadcastAddress, sizeof(BroadcastAddress));
            if(0 > Result){
                error("ERROR send to client");
                break;
            }
            continue;
        }

        rv = poll(fds, MAX_FD, timeout * 1000);

        if (rv == -1){
            perror("Polling"); 
        } 
        else if (rv == 0){
            // printf("Time out! Continue to send.\n");
            continue;
        } 
        else{
            for(int i = 0; i < MAX_FD; i++){
                if(fds[i].revents & POLLIN && i == 0){
                    bzero(recvBuffer, sizeof(recvBuffer));
                    Result = recvfrom(UDPfd, recvBuffer, BUFFER_SIZE, 0, (struct sockaddr *)&ClientAddress, &ClientLength);
                    if(0 > Result){
                        error("ERROR receive from client");
                        break;
                    }
                    int type2 = ntohs(*(uint16_t *)(recvBuffer+4));
                    int uport2 = ntohs(*(uint16_t *)(recvBuffer+6));
                    int tport2 = ntohs(*(uint16_t *)(recvBuffer+8));
                    char* hostname2 = recvBuffer + 10;
                    char* username2 = recvBuffer + 10 + strlen(hostname2) + 1;
                    // DisplayMessage(recvBuffer, Result);

                    switch(type2){
                        case DISCOVERY:
                            if(!strcmp(hostname, hostname2) && !strcmp(username, username2)){
                                printf("Receive self discovery\n");
                            }
                            else{
                                start = time(NULL);
                                bzero(sendBuffer, sizeof(sendBuffer));
                                length = header(sendBuffer, REPLY, uport, tport, username);
                                // DisplayMessage(sendBuffer, length); 
                                Result = sendto(UDPfd, sendBuffer, length, 0, (struct sockaddr *)&ClientAddress, ClientLength);
                                if(0 > Result){
                                    error("ERROR send to client");
                                    break;
                                }

                                if(!inUserList(username2))
                                    addUser(initUser(uport2, tport2, hostname2, username2));
                                printList();
                                UserCount++;
                            }
                            bzero(recvBuffer, sizeof(recvBuffer));
                            break;
                        case REPLY:
                            DisplayMessage(recvBuffer, Result);
                            if(!inUserList(username2))
                                addUser(initUser(uport2, tport2, hostname2, username2));
                            printList();
                            UserCount++;
                            bzero(recvBuffer, sizeof(recvBuffer));
                            break;
                        case CLOSING:
                            DisplayMessage(recvBuffer, Result);
                            bzero(recvBuffer, sizeof(recvBuffer));
                            start = time(NULL);
                            restartDiscover = 1;
                            timeout = uitimeout;
                            deleteUser(username2);
                            UserCount--;
                            continue;
                            break;
                        default:
                            break;
                    }
                }
                else if(fds[i].revents & POLLIN && i == 1){ 
                    int tempfd = accept(TCPfd, NULL, NULL);
                    if (tempfd < 0){
                        if (errno != EWOULDBLOCK){
                            error("accept() failed");
                        }
                        break;
                    }

                    printf("New incoming connection: %d\n", tempfd);
                    int first = firstAvailableFD();
                    fds[first].fd = tempfd;
                    fds[first].events = POLLIN | POLLPRI;
                }
                else if(fds[i].revents & POLLIN && i == 2){ // STDIN file descriptor
                    read(STDIN_FILENO, &RXChar, 1);
                    if(0x04 == RXChar){
                        break;
                    }
                    else{
                        processCommand(RXChar);
                    }
                }
                else if(fds[i].revents & POLLIN){
                    int j = i-3;
                    int type2 = 0;
                    read(fds[i].fd, messages[j] + msgLen[j], 1);
                    msgLen[j]++;
                    // DisplayMessage(messages[j], msgLen[j]);
                    if(messages[j][msgLen[j]-1] == '\0')
                        zeroCount[j]--;
                    if(msgLen[j] == 6){
                        TCPmsgProcess(j, fds[i].fd);
                        DisplayMessage(messages[j], msgLen[j]);
                    }
                    else if(msgLen[j] > 6){
                        type2 = ntohs(*(uint16_t *)(messages[j]+4));
                        switch(type2){
                            case ESTABLISH:
                                // DisplayMessage(messages[j], msgLen[j]);
                                if(zeroCount[j] == 0){
                                    printf("Like to connect with %s?\n", messages[j]+6);
                                    char ch;
                                    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                                    do{
                                        ch = getchar();
                                    }while(ch != 'y' && ch!= 'n' && ch != 'Y' && ch != 'N');
                                    SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                                    if(ch == 'y' || ch == 'Y'){
                                        struct User* temp = searchName(messages[j]+6);
                                        temp->TCPfd = fds[i].fd;
                                        printf("Socket %d for %s stored.\n", temp->TCPfd, messages[j]+6);
                                        bzero(sendBuffer, sizeof(sendBuffer));
                                        length = header(sendBuffer, ACCEPT, 0, 0, NULL);
                                        Result = write(fds[i].fd, sendBuffer, length);
                                        if(0 > Result){
                                            error("ERROR writing to socket");
                                        }
                                        DisplayMessage(sendBuffer, length);
                                        bzero(messages[j], BUFFER_SIZE);
                                        zeroCount[j] = 0;
                                        msgLen[j] = 0;
                                    }
                                    else{
                                        printf("Won't connect\n");
                                        bzero(sendBuffer, sizeof(sendBuffer));
                                        length = header(sendBuffer, UNAVAILABLE, 0, 0, NULL);
                                        Result = write(fds[i].fd, sendBuffer, length);
                                        if(0 > Result){
                                            error("ERROR writing to socket");
                                        }
                                        DisplayMessage(sendBuffer, length);
                                        bzero(messages[j], BUFFER_SIZE);
                                        zeroCount[j] = 0;
                                        msgLen[j] = 0;
                                    }
                                }
                                break;
                            case LISTREPLY:
                                if(msgLen[j] == 10){
                                    int numEntries = ntohl(*(uint32_t *)(messages[i]+6));
                                    zeroCount[j] += numEntries;
                                }
                                break;
                            case DATA:
                                if(zeroCount[j] == 0){
                                    DisplayMessage(messages[j], msgLen[j]);
                                    bzero(messages[j], BUFFER_SIZE);
                                    zeroCount[j] = 0;
                                    msgLen[j] = 0;
                                }
                                break;
                            case DISCONTINUE:
                                if(zeroCount[j] == 0){
                                    DisplayMessage(messages[j], msgLen[j]);
                                    close(fds[i].fd);
                                    bzero(messages[j], BUFFER_SIZE);
                                    zeroCount[j] = 0;
                                    msgLen[j] = 0;
                                }
                                break;
                            default:
                                break;
                        }
                    }
                }
            }
        }
    }
    return 0;  
}  
