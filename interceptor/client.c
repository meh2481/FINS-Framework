#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>


#define xxx(a,b,c,d) 	(16777216ul*(a) + (65536ul*(b)) + (256ul*(c)) + (d))

/*

int xxx(char a,char b,char c,char d) 
{

return ((16777216ul*(a) + (65536ul*(b)) + (256ul*(c)) + (d)));

}


*/

int main()
{
int sock;
struct sockaddr_in server_addr;
int numbytes;
struct hostent *host;
char send_data[1024];




memset (send_data,89,1000 );
send_data[1000]='\0';

//host= (struct hostent *) gethostbyname((char *)"127.0.0.1");


if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
{
perror("socket");
exit(1);
}
int port =5000;
printf("MY DEST PORT BEFORE AND AFTER\n%d, %d",port, htons(port));
server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(5000);

//server_addr.sin_addr.s_addr = xxx(128,173,92,37);

server_addr.sin_addr.s_addr = xxx(127,0,0,1);

server_addr.sin_addr.s_addr = htonl(server_addr.sin_addr.s_addr);


//server_addr.sin_addr.s_addr = INADDR_LOOPBACK;
bzero(&(server_addr.sin_zero),8);

int i =0 ;
   while (1)
   {

    printf("Type Something (q or Q to quit):");
  // gets(send_data);
i = i+1; 
sleep (1);
    if ((strcmp(send_data , "q") == 0) || strcmp(send_data , "Q") == 0)
       break;

    else{
//	if (i % 100 ==0 )
      numbytes= sendto(sock, send_data, strlen(send_data), 0,
              (struct sockaddr *)&server_addr, sizeof(struct sockaddr));
	printf("\n %d",numbytes);	     	
		}
   }

}

