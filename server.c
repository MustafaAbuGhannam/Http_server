#include<stdio.h>
#include<stdlib.h>
#include"threadpool.h"
#include<string.h>
#include <unistd.h>        	          // for read/write/close
#include <sys/types.h>     	          /* standard system types        */
#include <netinet/in.h>    	          /* Internet address structures */
#include <sys/socket.h>   	          /* socket interface functions  */
#include <netdb.h>         	          /* host to IP resolution            */
#include<dirent.h>
#include<sys/stat.h>
#include<errno.h>
#include <fcntl.h>


#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

#define Massage_404 "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H4>404 Not Found</H4>\nFile not found.\n</BODY></HTML>"

#define Massage_403 "<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H4>403 Forbidden</H4>\nAccess denied.\n</BODY></HTML>"

#define Massage_302 "<HTML><HEAD><TITLE>302 Found</TITLE></HEAD>\n<BODY><H4>302 Found</H4>\nDirectories must end with a slash.\n</BODY></HTML>"

#define Massage_400 "<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H4>400 Bad request</H4>\nBad Request.\n</BODY></HTML>"

#define Massage_500 "<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H4>500 Internal Server Error</H4>\nSome server side error.\n</BODY></HTML>\n"

#define Massage_501 "<HTML><HEAD><TITLE>501 Not supported</TITLE></HEAD>\n<BODY><H4>501 Not supported</H4>\nMethod is not supported.\n</BODY></HTML>"

#define Massage_Protocol "HTTP/1.1 %d %s\r\n"

#define Massage_Server "Server: webserver/1.0\r\n"

#define Massage_Connection "Connection: close\r\n"

#define Massage_Content_Type "Content-Type: %s\r\n"

#define Massage_Content_length "Content-Length: %d\r\n"

#define Massage_Last_Modified "Last-Modified: %s\r\n"

#define Table_Massage_first_Lines "<HTML>\n<HEAD><TITLE>Index of %s</TITLE></HEAD>\n<BODY>\n<H4>Index of %s</H4>\n<table CELLSPACING=8>\n<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\n"

#define Table_Row_Massage "<tr>\n<td><A HREF=\"%s\">%s</A></td>\n<td>%s</td>\n<td>%s</td>\n</tr>\n\n"

#define Table_end_Massage "</table>\n\n<HR>\n\n<ADDRESS>webserver/1.0</ADDRESS>\n\n</BODY></HTML>"

void sendErororMassage(int error, int fd, char* path);

int requestDealer(void* arg);

void DealWithPath(int fd, char* path);

void wirteRespondeFile(int sockfd, int fd, char* path);

char *get_mime_type(char *name);

void sendDirContents(int sockfd, char* path);

int cheackPath(char* path);

char* pathChanger(char* path);



/*

    the main function of this program is going to initilize all the neccesary structs and data to start the server , like the thread pool , socket , and a data structer that 

    will be used to store the fds of the sockets for each client that trys to connect to the server


*/


int main(int argc, char** argv){

    if(argc != 4){
        fprintf(stderr, "Usage: server <port> <pool-size>");
        exit(EXIT_FAILURE);
    } 

    int port;
    int pool_size;
    int max_number_of_request;
    int sockFd;
    char* tol;
    int sucsses = -1;
    const int backLog = 5;
    int i;
    int* acceptFds;

    threadpool* pool;

    port = strtoll(argv[1], &tol, 10);

    if(strcmp(tol, "") != 0 || port < 1024){
        fprintf(stderr, "Usage: server <port> <pool-size>");
        exit(EXIT_FAILURE);
    }

    pool_size = strtoll(argv[2], &tol, 10);

    if(strcmp(tol, "") || pool_size <= 0){
        fprintf(stderr, "Usage: server <port> <pool-size>");
        exit(EXIT_FAILURE);
    }

    max_number_of_request = strtoll(argv[3], &tol, 10);

    if(strcmp(tol, "")|| max_number_of_request < 1){
        fprintf(stderr, "Usage: server <port> <pool-size>");
        exit(EXIT_FAILURE);
    }

    pool = create_threadpool(pool_size);
    if(pool == NULL){
        fprintf(stderr, "create thread_pool failed\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = PF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    sockFd = socket(AF_INET, SOCK_STREAM, 0);

    if(sockFd == -1){
        perror("socket failed");
        destroy_threadpool(pool);
        exit(EXIT_FAILURE);
    }

    sucsses = bind(sockFd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    if(sucsses != 0){
        destroy_threadpool(pool);
        perror("bind failed");
        close(sockFd);
        exit(EXIT_FAILURE);
    }

    sucsses = listen(sockFd, backLog);

    if(sucsses != 0){
        perror("listen failed");
        destroy_threadpool(pool);
        exit(EXIT_FAILURE);
    }


    acceptFds = (int*)malloc(sizeof(int) * max_number_of_request);
    if(acceptFds == NULL){
        perror("malloc");
        destroy_threadpool(pool);
        exit(EXIT_FAILURE);
    }
    i = 0;

    while( i < max_number_of_request){
        
        acceptFds[i] = accept(sockFd, NULL, NULL);

        if(acceptFds[i] == -1){
            perror("accept failed");
            free(acceptFds);
            close(sockFd);
            destroy_threadpool(pool);
            exit(EXIT_FAILURE);
        }
        dispatch(pool, requestDealer, (acceptFds + i));
        i++;
    }

    destroy_threadpool(pool);
    close(sockFd);
    free(acceptFds);
    return 0;
}


/*

    the requestDealer function will parse the first 4000 bytes that have been resved from the client  and  ( actually the server dont care about all the headers that the client sends the server will parse the request until the first /r/n token)

    the fisrt line of the requset must contain the type, path (starting with /), and the version of the http ( the server deals just with HTTP/1.1)

    if the first line of the requset dose not containe one of these tokens the server will send back to the client a bad requset answer, if the the method is not equal to GET or the version of the protcol is not 1.1 
    
    the server will send back a not spported massage;



*/

int requestDealer(void* arg){
    char readBuffer [4001] = {0};
    char* firstLine;
    int firstLineLen = 0;
    int fd = *((int*) arg);
    int readd = 0;
    char* Ocuurnce;
    int numOfTokens = 0;
    char*token = NULL;
    char* type = NULL;
    char* path = NULL;
    char* protocol = NULL;
    char** tokens;
    int i;
    int j;

    readd = read(fd, readBuffer, sizeof(char) * (4000));
    if(readd == -1){
        perror("read failed");
        return -1;
    }

    readBuffer[readd] = '\0';

   Ocuurnce = strstr(readBuffer,"\r\n");

   if(Ocuurnce == NULL){
       sendErororMassage(400, fd, path);
       close(fd);
       return -1;
   }

   firstLineLen = (int)strlen(readBuffer) - (int)strlen(Ocuurnce);

    firstLine = (char*)malloc(sizeof(char) * (firstLineLen + 1));

    if(firstLine == NULL){
        perror("malloc failed");
        sendErororMassage(500, fd, path);
        close(fd);
        return -1;
    }

    for(i = 0; i< firstLineLen; i++){

        firstLine[i] = readBuffer[i];
    }
    firstLine[i] = '\0';

    token = strtok(firstLine, " ");
    if(token == NULL){
        sendErororMassage(400, fd, path);
        close(fd);
        free(firstLine);
        return -1;
    }
    i = 0;
    tokens = (char**)malloc(sizeof(char*) * 3);
    if(tokens == NULL){
        perror("malloc failed");
        sendErororMassage(500, fd, path);
        close(fd);
        free(firstLine);
        return -1;
    }
    for(i = 0; i < 3; i++){
        tokens[i] = NULL;
    }
    i = 0;
    while(token != NULL){
        int len = strlen(token);
        tokens[i] = (char*)malloc(sizeof(char) * (len + 1));
        if(tokens[i] == NULL){
            perror("malloc failed");
            sendErororMassage(500, fd, path);
            close(fd);
            free(firstLine);
            if(i == 1){
                free(tokens[0]);
            }
            if(i == 2){
                free(tokens[0]);
                free(tokens[1]);
            }
            free(tokens);
            return -1;
        }
        for(j = 0 ; j < len;j++){
            tokens[i][j] = '\0';
        }
        tokens[i][j] = '\0';
        strcpy(tokens[i], token);
        token =strtok(NULL, " ");
        numOfTokens++;
        i++;
    }


    type = tokens[0];
    if(tokens[1] != NULL){
        path = tokens[1];
    }
    if(tokens[2] != NULL){
        protocol = tokens[2];
    }

    if(numOfTokens != 3){
        sendErororMassage(400, fd, path);
        close(fd);
        if(type != NULL){
        free(type);
        }
        if( protocol != NULL){
        free(protocol);
        }
        if(path != NULL){
        free(path);
        }
        free(tokens);
        free(firstLine);
        return -1;
    }

    if(strcmp(type, "GET") != 0){
        sendErororMassage(501, fd, path);
        close(fd);
        free(type);
        free(protocol);
        free(path);
        free(tokens);
        free(firstLine);
        return -1;
    }

    if(strcmp(protocol, "HTTP/1.1") != 0 && strcmp(protocol, "HTTP/1.0") != 0){
        sendErororMassage(400, fd ,path);
        close(fd);
        free(type);
        free(protocol);
        free(path);
        free(tokens);
        free(firstLine);
        return -1;
    }





    printf("%s\n\n", readBuffer);

    path = pathChanger(path);
    if(path != NULL){
        DealWithPath(fd, path);
    }
    else{
        sendErororMassage(400, fd, path);
    }
    free(firstLine);
    free(type);
    free(protocol);
    free(path);
    free(tokens);
    close(fd);
    return 0;
}


/*

    this function is the error massage handeler: if the server found an error in the request , or if the server had some internal error this function be the one handels sending a good answer for the client

*/

void sendErororMassage(int error, int fd, char* path){

    char errorResponde [1100] = {0};
    char errorBody [600] = {0};
    char massageP [100] = {0};
    time_t now;
    char timebuf[128];
    now = time(NULL);
    int conLen;
    int rc;


    if(error == 500){
        conLen = strlen(Massage_500);
        sprintf(errorBody,"%s", Massage_500);
        sprintf(massageP,Massage_Protocol, error, "Internal Server Error");
    }
    else if(error == 400){
        conLen = strlen(Massage_400);
        sprintf(errorBody,"%s", Massage_400);
        sprintf(massageP,Massage_Protocol, error, "Bad Request");
    }
    else if(error == 302){
        conLen =strlen(Massage_302);
        sprintf(errorBody,"%s", Massage_302);
        sprintf(massageP,Massage_Protocol, error, "Found");
    }
    else if(error == 404){
        conLen = strlen(Massage_404);
        sprintf(errorBody,"%s", Massage_404);
        sprintf(massageP,Massage_Protocol, error, "Not Found");
    }
    else if(error == 403){
        conLen = strlen(Massage_403);
        sprintf(errorBody,"%s", Massage_403);
        sprintf(massageP,Massage_Protocol, error, "Forbidden");
    }
    else{
        conLen = strlen(Massage_501);
        sprintf(errorBody,"%s", Massage_501);
        sprintf(massageP,Massage_Protocol, error, "Not supported");
    }

    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    char type[30] = {0};
    sprintf(type, Massage_Content_Type, "text/html");


    if(error == 302){
        sprintf(errorResponde, "%s%sDate: %s\r\n%sLocation: %s/\r\nContent-Length: %d\r\n%s\r\n\r\n%s", massageP, Massage_Server, timebuf, type, path, conLen, Massage_Connection, errorBody);
    }

    else{
        sprintf(errorResponde, "%s%sDate: %s\r\n%sContent-Length: %d\r\n%s\r\n\r\n%s", massageP, Massage_Server, timebuf, type , conLen, Massage_Connection, errorBody);
    }

    puts(errorResponde);

    rc = write(fd, errorResponde, strlen(errorResponde) + 1);
    if(rc == -1){
        perror("write failed");
        return;
    }
    return;

}  

/*

    this function will get the path that was receved from the client and will cheack if ther are permisons for the client to accses the file in the path , or if the file exists at all

*/


void DealWithPath(int fd, char* path){

    struct stat fs;
    int len = strlen(path);
    DIR* dir;
    struct dirent* dirent;
    int indexFlag = -1;
    int succses = stat(path + 1, &fs);
    int file;
    int chResult = 0;
    chResult = cheackPath(path);
    if(succses == -1){
        sendErororMassage(404, fd, path);
        return;
        
    }
    if(chResult == -2){
        sendErororMassage(403, fd, path);
        return;
    }
    else if(chResult == -1){
        sendErororMassage(500, fd, path);
        return;
    }


    if(S_ISDIR(fs.st_mode) && path[len - 1] != '/'){
        sendErororMassage(302, fd, path);
        return;
    }

    if(S_ISDIR(fs.st_mode)){
        dir = opendir(path + 1);
        if(dir == NULL){
            perror("opendir failed");
            sendErororMassage(500, fd, path);
            return;
        }

    while((dirent = readdir(dir)) != NULL){
        if(strcmp(dirent->d_name, "index.html") == 0){
            indexFlag = 1;
            break;
        }
    }

    if(indexFlag == 1){
        char filee [4000] = {0};
        sprintf(filee, "%sindex.html", path);
        chResult = cheackPath(filee);
        if(chResult == -2){
            closedir(dir);
            sendErororMassage(403, fd, path);
            return;
        }
        succses = stat(filee + 1, &fs);
        if(succses == -1){
            sendErororMassage(500, fd, path);
            perror("stat failed");
            closedir(dir);
            return;
        }
        if(!(fs.st_mode & S_IROTH)){
            sendErororMassage(403, fd, path);
            closedir(dir);
            return;
        }
        file = open(filee + 1, O_RDONLY, 0666);
        if(file  < 0){
            perror("fopen failed");
            sendErororMassage(500, fd, path);
            closedir(dir);
            return;
        }
        wirteRespondeFile(fd, file, filee + 1);
        close(file);
        closedir(dir);
        return;
    }

    else{
        succses = stat(path + 1, &fs);
        if(succses == -1){
            perror("stat");
            sendErororMassage(500, fd, path);
            closedir(dir);
            return;
        }
        if(!(fs.st_mode & S_IROTH)){
            sendErororMassage(403, fd, path);
            closedir(dir);
            return;
        }
        sendDirContents(fd, path + 1);
        closedir(dir);
        return;
    }


    }

    else{

        if(!S_ISREG(fs.st_mode)){
            sendErororMassage(403, fd, path);
            return;
        }
        if((fs.st_mode & S_IROTH)){
            file = open(path + 1, O_RDONLY, 0666);
            if( file < 0){
                perror("fopen failed");
                sendErororMassage(500, fd, path);
                return;
            }
            wirteRespondeFile(fd, file, path + 1);
            close(file);

        }
        else{
            sendErororMassage(403, fd, path);
            return;
        }

    }


}


/*

    this function will hndel sending the respond massage back to the clinet after cheaking evrey thing by the other functions 


*/

void wirteRespondeFile(int sockfd, int fd, char* path){

    char respondeHeders[1000] = {0};
    struct stat fs;
    int len;
    char* type = NULL;
    time_t now;
    char timebuf[128] = {0};
    char mtimebuf[128] = {0};
    char massageP [100] = {0};
    char contentT [100] = {0};
    char contentL [100] = {0};
    char lastModified [228] = {0};
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    type = get_mime_type(path);


    if(stat(path, &fs) == -1){
        perror("stat failed");
        sendErororMassage(500, sockfd, path);
        return;
    }
    
    strftime(mtimebuf, sizeof(timebuf), RFC1123FMT, gmtime((const time_t*)&fs.st_mtim));
    len = (int) fs.st_size;

    sprintf(massageP, Massage_Protocol, 200, "OK");
    sprintf(contentT, Massage_Content_Type, type);
    sprintf(contentL, Massage_Content_length, len);
    sprintf(lastModified, Massage_Last_Modified, mtimebuf);
    if(type != NULL){
        sprintf(respondeHeders,"%s%s%s%s\r\n%s%s%s%s\r\n", massageP, Massage_Server, "Date: ", timebuf, contentT, contentL, lastModified, Massage_Connection);
    }
    else{
        sprintf(respondeHeders,"%s%s%s%s\r\n%s%s%s\r\n", massageP, Massage_Server, "Date: ", timebuf, contentL, lastModified, Massage_Connection);
  
    }
    puts("\n");
    puts(respondeHeders);
    write(sockfd, respondeHeders, strlen(respondeHeders));
        
    
    int nread;
    unsigned char buffer[1000000] = {0};
    int counter = 0;

  

    while(1){
        memset(buffer, 0 , 1000000);
        nread = read(fd , buffer, 1000000);
        counter += nread;
        if(nread > 0){
        write(sockfd, buffer, nread);
        }
        else{
            
            if(counter != len){
                perror("read");
            }

            break;

        }
    }
    return;
    
}


/*

    a given function that return the type of the requested file

*/

char *get_mime_type(char *name) {
char *ext = strrchr(name, '.');
if (!ext) return NULL;
if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
if (strcmp(ext, ".gif") == 0) return "image/gif";
if (strcmp(ext, ".png") == 0) return "image/png";
if (strcmp(ext, ".css") == 0) return "text/css";
if (strcmp(ext, ".au") == 0) return "audio/basic";
if (strcmp(ext, ".wav") == 0) return "audio/wav";
if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg"; 
if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
return NULL;
}

/*

    this function will be the hndeler of creating and sending a table that contains  all the files that are inside of some dir 

*/


void sendDirContents(int sockfd, char* path){
    char respondeHeders[1000] = {0};
    int pathLen = strlen(path);
    DIR* dir;
    struct dirent* dirent;
    struct stat fs;
    int len = 0;
    char* type = NULL;
    time_t now;
    char timebuf[128] = {0};
    char mtimebuf[128] = {0};
    char massageP [100] = {0};
    char contentT [100] = {0};
    char contentL [100] = {0};
    char lastModified [228] = {0};
    char entityLen [9] = {0};
    char entityModDate [128] = {0};
    char entityName [500] = {0};
    int rowLen = strlen(Table_Row_Massage) + 500 + 128 + 9;
    char *tabelRow  = (char*) malloc(sizeof(char) * rowLen);
    char *table = NULL;
    if(tabelRow == NULL){
        perror("malloc");
        sendErororMassage(500, sockfd, path);
        return;
    }
    now = time(NULL);

    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    type = "text/html";

    if(stat(path, &fs) == -1){
        perror("fstat failed");
        sendErororMassage(500, sockfd, path);
        return;
    }

    strftime(mtimebuf, sizeof(timebuf), RFC1123FMT, gmtime((const time_t*)&fs.st_mtim));
    sprintf(lastModified, Massage_Last_Modified, mtimebuf);

    dir = opendir(path);

    if(dir == NULL){
        perror("opendir");
        free(tabelRow);
        sendErororMassage(500, sockfd, path);
        return;
    }

    while((dirent = readdir(dir)) != NULL){
        memset(entityName, 0, sizeof(entityName));
        memset(entityModDate, 0, sizeof(entityModDate));
        memset(entityLen, 0, sizeof(entityLen));
        memset(tabelRow, 0, rowLen);
        strcpy(entityName, dirent->d_name);
        if(strcmp("..", dirent->d_name) != 0 && strcmp(".", dirent->d_name) != 0){
            int dLen = strlen(dirent->d_name);
            char* newPath = (char*)malloc(sizeof(char) * (pathLen + dLen + 1));
            sprintf(newPath, "%s%s", path, dirent->d_name);
            stat(newPath, &fs);
            free(newPath);
        }
        else{
            stat(dirent->d_name, &fs);
        }
        
        strftime(entityModDate, sizeof(timebuf), RFC1123FMT, gmtime((const time_t*)&fs.st_mtim));
        if(dirent->d_type == DT_REG){
            sprintf(entityLen,"%d", (int)fs.st_size);
        }
        else{
            strcpy(entityLen,"");
        }
        sprintf(tabelRow, Table_Row_Massage, entityName, entityName, entityModDate, entityLen);

        if(table == NULL){
            int ll = strlen(tabelRow);
            len += ll;
            table = (char *)malloc(sizeof(char) * (ll + 1));
            if(table == NULL){
                perror("malloc");
                sendErororMassage(500, sockfd, path);
                return;
            }
            memset(table, 0, ll);
            strcat(table, tabelRow);
        }
        else{
            int ll = strlen(tabelRow);
            int i = strlen(table);
            int lll = i;
            len += ll;
            table = (char *)realloc(table, sizeof(char) * (ll + i + 1));
            if(table == NULL){
                perror("realloc");
                sendErororMassage(500, sockfd, path);
                return;
            }
            for(; i <= (lll + ll); i++){
                table[i] = '\0';
            }
            strcat(table, tabelRow);

        }

    }

    int firstLL = strlen(path - 1) * 2 + strlen(Table_Massage_first_Lines);
    char* firstLine = (char*) malloc(sizeof(char) * (firstLL + 1));
    memset(firstLine, 0, firstLL);
    sprintf(firstLine, Table_Massage_first_Lines, path - 1, path - 1);
    sprintf(contentL, Massage_Content_length,(int)(strlen(table) + strlen(firstLine) + strlen(Table_end_Massage)));
    sprintf(massageP, Massage_Protocol, 200, "OK");
    sprintf(contentT, Massage_Content_Type, type);
    sprintf(respondeHeders,"%s%s%s%s\r\n%s%s%s%s\r\n", massageP, Massage_Server, "Date: ", timebuf, contentT, contentL, lastModified, Massage_Connection);
    puts("\n");
    puts(respondeHeders);
    write(sockfd, respondeHeders, strlen(respondeHeders));
   
    write(sockfd, firstLine, strlen(firstLine));
    write(sockfd, table, strlen(table));
    write(sockfd, Table_end_Massage, strlen(Table_end_Massage) + 1);


    closedir(dir);
    free(table);
    free(tabelRow);
    free (firstLine);
    return;
}


/*

    this function will cheack if there are perrmison for the client to acces the given files in the path

*/

int cheackPath(char* path){
    int len = strlen(path);
    char* token = NULL;
    char* newPath = (char*)malloc(sizeof(char) * (len + 1));
    struct stat fs;
    int result = 0;
    if(newPath == NULL){
        perror("malloc");
        return -1;
    }
    char* pathCopy = (char*)malloc(sizeof(char) * (len + 1));
    if(pathCopy == NULL){
        perror("malloc");
        free(newPath);
        return - 1;
    }
    memset(pathCopy, 0, len);
    memset(newPath, 0, len);
    sprintf(pathCopy, "%s", path  + 1);


    token = strtok(pathCopy, "/");
    while(token != NULL){
        strcat(newPath, token);
        strcat(newPath, "/");
        token = strtok(NULL, "/");
        if(token == NULL){
            break;
        }
        result = stat(newPath, &fs);
        if(result == -1){
            perror("stat");
            free(newPath);
            free(pathCopy);
            return -3;
        }
        if(!(fs.st_mode & S_IXOTH)){
            free(newPath);
            free(pathCopy);
            return -2;
        }
    }

    free(newPath);
    free(pathCopy);
    return 1;

}

/*

    this function cheaks if the path start with / and other things to make sure that the path is legal


*/


char* pathChanger(char* path){
    char * newPath = NULL;
    int len = strlen(path);
    char* token = NULL;
    if(path[0] == '/' && len == 1){
        newPath = (char*)malloc(sizeof(char) * (len + 3));
        memset(newPath, 0, len + 2);
        newPath[0] = '/';
        newPath[1] = '.';
        strcat(newPath, path);
        free(path);
        return newPath;
    }
    if(path[0] == '/' && path[1] == '/'){
        free(path);
        return NULL;
    }
    if(path[0] == '.'){
        free(path);
        return NULL;
    }
    if(path[0] != '/'){
        free(path);
        return NULL;
    }
    token = strstr(path, "//");
    if(token != NULL){
        free(path);
        return NULL;
    }

    return path;
}
