#include <stdio.h>
#include "csapp.h"
#include <pthread.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int clientfd);
void *thread(void *vargp);
void header_make(char *method, char *request_ip, char *user_agent_hdr, char *version, int clientfd, char *filename);

int main(int argc, char **argv) {
  printf("%s", user_agent_hdr);
  signal(SIGPIPE, SIG_IGN);
  int listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  pthread_t tid;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <server port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int)); 
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    Pthread_create(&tid, NULL, thread, connfdp);
    // Close(connfdp);  // line:netp:tiny:close 생략가능
  }
  return 0;
}

/* Routine of Peer Thread */
void *thread(void *vargp) {
	int connfd = *((int *)vargp);		// 넘겨받은 Heap 공간값을 connfd가 가리킨다.
	Pthread_detach(pthread_self());  	// 아래에서 설명할 것! (Reaping 관련)
	Free(vargp); 						// 값을 추출했으므로, 힙공간은 해제하자.
	doit(connfd);						// connfd에 대해서 Service를 제공한다!
	Close(connfd);						// 서비스 끝나면 디스크립터를 닫자!
	return NULL;						// pthread_create 함수에게 NULL을 넘김(관습)
}

void doit(int clientfd) {
  char buf[MAXLINE], cbuf[MAXLINE], sbuf[MAXLINE], method[MAXLINE], url[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host_header[MAXLINE], request_ip[MAXLINE], port[MAXLINE], filename[MAXLINE];
  rio_t rio_client, rio_server;
  int forwardfd, n;
  const char* prefix = "http://";

  /* Read client request line and headers */
  Rio_readinitb(&rio_client, clientfd);
  Rio_readlineb(&rio_client, buf, MAXLINE);
  printf("Request Headers: %s", buf);
  sscanf(buf, "%s %s %s", method, url, version);
  size_t len = strlen(url);
  if (len > 0 && url[len - 1] == '/') {
      url[len - 1] = '\0'; // 마지막 문자를 널 문자로 바꿔서 제거
  }
  // url에 "http://"가 있는지 확인
  if (strncmp(url, prefix, strlen(prefix)) == 0) {
      // "http://"를 제거
      memmove(url, url + strlen(prefix), strlen(url) - strlen(prefix) + 1);
  }
  if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0))
  {
    clienterror(clientfd, method, "501", "Not impemented",
                "Tiny does not implement this method");
    return;
  }
  // Rio_readlineb(&rio_client, buf, MAXLINE);
  int parsed_count = sscanf(url, "%[^:]:%[^/]%s", request_ip, port, uri);
  if(parsed_count < 2 || strlen(port) == 0)
  {
    strcpy(port, "80");
    if (parsed_count == 1) {
        // request_ip가 있고, uri는 없으므로 '/'가 있는 경우 처리
        char *slash = strchr(request_ip, '/');
        if (slash) {
            strcpy(uri, slash + 1); // '/0 다음부터 uri를 복사
            *slash = '\0'; // request_ip에서 '/' 제거
        } else {
            strcpy(uri, ""); // uri가 없는 경우
        }
    }
  }
  else if (parsed_count < 3 || uri[0] == '\0')
    strcpy(uri, "/");
  // read_requesthdrs(&rio_client);

  //서버요청 소켓
  forwardfd = Open_clientfd(request_ip, port);
  Rio_readinitb(&rio_server, forwardfd);        //rio_t의 읽기 버퍼와 fd를 연결
  // HTTP/1.1을 1.0으로 변경 및 서버로 전송
  sprintf(cbuf, "%s %s HTTP/1.0\r\n", method, uri);
  sprintf(cbuf, "%sHost: %s\r\n", cbuf, request_ip);
  //sprintf(buf, "%sConnection: %s\r\n", buf, "close");
  sprintf(cbuf, "%sProxy-Connection: close\r\n", cbuf);
  sprintf(cbuf, "%s%s\r\n\r\n", cbuf, user_agent_hdr);

  Rio_writen(forwardfd, cbuf, strlen(cbuf));
  // 서버 응답을 클라이언트에게 전달하는 함수 추가 필요

  while((n = Rio_readlineb(&rio_server, sbuf, MAXLINE)) > 0) {
    Rio_writen(clientfd, sbuf, n);       //fd에 써서 보내준다.(클라이언트로)
  }
  
  Close(forwardfd);
}

void header_make(char *method, char *request_ip, char *user_agent_hdr, char *version, int forwardfd, char *filename)
{
  char buf[MAXLINE];

  sprintf(buf, "%s %s %s\r\n", method, filename, "HTTP/1.0");
  sprintf(buf, "%sHost: %s\r\n", buf, request_ip);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  //sprintf(buf, "%sConnection: %s\r\n", buf, "close");
  sprintf(buf, "%sProxy-Connection: %s\r\n\r\n", buf, "close");

  Rio_writen(forwardfd, buf, strlen(buf));
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "<body bgcolor=""ffffff"">\r\n");
  sprintf(body, "%s: %s\r\n", errnum, shortmsg);
  sprintf(body, "<p>%s: %s\r\n", longmsg, cause);
  sprintf(body, "<hr><em>The Tiny Web server</em>\r\n");
  Rio_writen(fd, body, strlen(body));

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
}
