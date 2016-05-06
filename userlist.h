#ifndef USERLIST_H
#define USERLIST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRING_SIZE 256

struct User{
    int TCPfd;
    int UDPport;
    int TCPport;
    char Username[STRING_SIZE];
    char Hostname[STRING_SIZE];
    struct User* nextUser;
};

struct User *head = (struct User *) NULL;
struct User *end = (struct User *) NULL;

struct User* initUser(int UDPport, int TCPport, char* Hostname, char* Username){
    struct User *ptr;
    ptr = (struct User *) malloc(sizeof(struct User));
    if(ptr == NULL){
    	printf("Fail to allocate!\n");
        return (struct User *) NULL;
    }
    else{
    	ptr->TCPfd = -1;
    	ptr->UDPport = UDPport;
    	ptr->TCPport = TCPport;
        strcpy(ptr->Username, Username);
        strcpy(ptr->Hostname, Hostname);
        ptr->nextUser = NULL;
        return ptr;                         
    }
}

struct User* searchName(char* Username)
{
    struct User* temp = head;
    while(temp != NULL && strcmp(Username, temp->Username)){
    	temp = temp->nextUser;
    }
    return temp;
}

int inUserList(char* Username){
    if(searchName(Username) != NULL){
        return 1;
    }
    return 0;
}

void printUser(char* Username)
{
    struct User *temp = NULL;
    temp = searchName(Username);

    if(temp == NULL){
    	printf("No such user!\n");
    	return;
    }
    printf("Host name: %s, ", temp->Hostname);
    printf("User name: %s, ", temp->Username);
    printf("UDP port: %d, ", temp->UDPport);
    printf("TCP port: %d\n", temp->TCPport);
}

void printList()
{
	struct User* ptr = head;
    int i = 1;
    while(ptr != NULL){
        printf("%d. ", i++);
        printUser(ptr->Username);           
        ptr = ptr->nextUser;
    }
}

void addUser(struct User* ptr)
{
   	if(head == NULL){
   		head = ptr;
   	}
   	else{
		end->nextUser = ptr;
   	} 	
   	end = ptr;
}


void deleteUser(char* Username)
{
    struct User *temp, *prev;
    temp = head;
    prev = head;

    temp = searchName(Username);

    if(temp == NULL){
    	printf("No such user!\n");
    	return;
    }

    if(temp == prev){
        head = head->nextUser;
        if(end == temp) 
           end = end->nextUser;
        free(temp);
    }
    else{
        while(prev->nextUser != temp){
            prev = prev->nextUser; 
        }
        prev->nextUser = temp->nextUser;
        if(end == temp)
            end = prev;
        free(temp);
    }
}


void deleteList(struct User *ptr)
{
    struct User *temp;

    if(head == NULL) return; 

    if(ptr == head) {
        head = NULL;
        end = NULL;
    }
    else{
        temp = head; 
        while(temp->nextUser != ptr)
            temp = temp->nextUser;
        end = temp;
    }

    while(ptr != NULL){
       temp = ptr->nextUser;
       free(ptr);
       ptr = temp;
    }
}

// int main(int argc, char const *argv[])
// {
// 	addUser(initUser(2,3,"ab","bc"));
// 	addUser(initUser(5,6,"cd","de"));
// 	addUser(initUser(8,9,"ef","fg"));
	
// 	deleteUser("cd","de");
// 	printlist();
// 	deleteUser("ab","bc");
// 	printlist();
// 	addUser(initUser(2,3,"ab","bc"));
// 	printlist();

	
// 	return 0;
// }

#endif
