#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

//dradio
#include "dradio.h"

#define PORT 1100
#define ADDRESS "127.0.0.1"

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

//Session
dradio_session ses;

typedef struct served_client{
  dradio_session*    ses;
  int                socket;
}served_client;

typedef struct connection_accepter{
  int                server_socket;    
  dradio_session*    ses;
}connection_accepter;

void send_file(int dest, char* bytes, long total, dradio_response_kind rk){
  // printf("Sending file...\n");
  char confirmation = '!';
  int confirms = 0;
  dradio_response response = (dradio_response){
    .kind=rk,
    .sz=total
  };
  int n = send(dest, &response, sizeof(response), 0);
  // printf("n=%d\n", n);
  long bytes_sent = 0;
  long packet_sz = MAX_PACKET_DATA_SZ;
  while(bytes_sent<total){
    if (total-bytes_sent<MAX_PACKET_DATA_SZ) packet_sz = total-bytes_sent;
    int n = send(dest, &(bytes[bytes_sent]), packet_sz, 0);
    bytes_sent+=n;
    // printf("Sending file (%ld/%ld bytes)\n", bytes_sent, total);
    if (bytes_sent/CONFIRM_EVERY>confirms){
      // printf("Waiting for packet confirmation...\n");
      while(recv(dest, &confirmation, 1, 0)<=0 && confirmation!='o');
      confirms++;
      if (n>0 && confirmation == 'o')printf("Confirmed!\n");
      else printf("Failed to confirm!\n");
    }
    if (bytes_sent<0) return;
  }
  printf("File sent!\n");
}


void send_track(int dest, int id){
  dradio_track_data td = ses.tracks_data[id];
  printf("Sending track '%s'\n", td.name);
  send_file(dest, td.audio_data, td.audio_sz, dradio_audio_response);
  send_file(dest, td.cover_data, td.cover_sz, dradio_cover_response);
  send_file(dest, td.name, (long)strlen(td.name)+1, dradio_track_response);
}

void send_pos(int dest, float pos){
  dradio_response response = (dradio_response){
    .kind=dradio_seek_response,
    .sz=(long)sizeof(pos)
  };
  int n = send(dest, &response, sizeof(response), 0);
  send(dest, &pos, sizeof(pos), 0);
}

void send_cmd(int dest, dradio_response_kind cmd){
  char ok = 'y';
  dradio_response response = (dradio_response){
    .kind=cmd,
    .sz=(long)sizeof(ok)
  };
  int n = send(dest, &response, sizeof(response), 0);
  send(dest, &ok, sizeof(ok), 0);
}

void* serve(void *arg){
  int n;
  served_client client = *(served_client*)arg;
  printf("Client connected!\n");
  send_track(client.socket, client.ses->current_track);
  send_track(client.socket, (client.ses->current_track+1)%client.ses->tracks_len);
  sleep(2.0);
  send_pos(client.socket, client.ses->track_pos);
  if (!client.ses->paused) send_cmd(client.socket, dradio_play_response);
  //Handle data transfer from client
  dradio_request req = (dradio_request){
    .kind=dradio_no_request,
    .sz=0
  };
  char confirm = 'o'; //ok
  int confirms_sent = 0;
  n = 0;
  long rcvd = 0;
  int expected_sz = MAX_PACKET_DATA_SZ;
  char* rcvd_bytes = (char*)malloc(of_megabytes(12));
  if (rcvd_bytes==NULL){printf("Couldn't allocate receive buffer.\n"); return NULL;}
  for (long i=0;i<of_megabytes(12); i++) rcvd_bytes[i]= '\0';
  char buf[MAX_PACKET_DATA_SZ];
  int sock = client.socket;
  while(1){
    if (req.kind == dradio_no_request){
        n=recv(sock, &req, sizeof(req), MSG_DONTWAIT);
        if (n<=0) req.kind=dradio_no_request;
        if (req.kind!=dradio_no_request) printf("Receiving %s...\n", request_str(req));
    }else{
      expected_sz = req.sz-rcvd<MAX_PACKET_DATA_SZ ? req.sz-rcvd : MAX_PACKET_DATA_SZ;
      //FIX: Write straight to rcvd_bytes
      n=recv(sock, buf, expected_sz, MSG_DONTWAIT);
      if (n>0){
        for (int i=0; i<n; i++) rcvd_bytes[rcvd+(long)i] = buf[i];
        rcvd+=(long)n;
        // if (rcvd/CONFIRM_EVERY>confirms_sent){
        //   // send(sock, &confirm, 1, MSG_DONTWAIT);
        //   // confirms_sent++;
        // }
        if (rcvd==req.sz){
          switch(req.kind){
            case dradio_skip_request:
              client.ses->track_pos=0.0;
              client.ses->current_track=(client.ses->current_track+1)%client.ses->tracks_len;
              int next_id = (client.ses->current_track+1)%client.ses->tracks_len;
              for (int i = 0; i<client.ses->clients_len; i++){
                int sck = client.ses->client_sockets[i];
                send_cmd(sck, dradio_skip_response);
                if (!client.ses->paused) send_cmd(sck, dradio_play_response);
                send_track(sck, next_id);
              }
              break;
            case dradio_play_request:
              client.ses->paused=false;
              for (int i = 0; i<client.ses->clients_len; i++){
                int sck = client.ses->client_sockets[i];
                send_cmd(sck, dradio_play_response);
              }
              break;
            case dradio_pause_request:
              client.ses->paused=false;
              for (int i = 0; i<client.ses->clients_len; i++){
                int sck = client.ses->client_sockets[i];
                send_cmd(sck, dradio_pause_response);
              }
              break;
          }
          //Reset
          req.kind=dradio_no_request;
          rcvd=0;
          confirms_sent=0;
        }
      }
    }
  }
  printf("User disconnected.\n");
  pthread_exit(NULL);
}

void* accept_connections(void *arg){
  connection_accepter accptr = *(connection_accepter*)arg;
  pthread_t thread_id;
  int client_sock;
  socklen_t addr_size;
  struct sockaddr_storage serverStorage;
  while(1){
    //Check for new clients
    addr_size = sizeof serverStorage;
    client_sock=accept(accptr.server_socket, (struct sockaddr *) &serverStorage, &addr_size);
    served_client c_client = (served_client){
      .socket=client_sock,
      .ses=accptr.ses
    };
    if (client_sock>0){
      accptr.ses->client_sockets[accptr.ses->clients_len++] = client_sock;
      if(pthread_create(&thread_id, NULL, serve, &c_client) != 0 )
         printf("Failed to create thread\n");
      pthread_detach(thread_id);
    }
  }
  pthread_exit(NULL);
}

int main(int argc, const char* argv[]){
  SetTraceLogLevel(LOG_WARNING);
  int serverSocket, newSocket;
  struct sockaddr_in serverAddr;
  if (argc<2)
    printf("Usage: server <dir>\n");
  else
    new_session(&ses, argv[1]);
  serverSocket = socket(PF_INET, SOCK_STREAM, 0);
  //Set serverSocket to be non-blocking
  // int flags = fcntl(serverSocket, F_GETFL, 0);
  // fcntl(serverSocket, F_SETFL, flags | O_NONBLOCK);

  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(PORT);
  serverAddr.sin_addr.s_addr = inet_addr(ADDRESS);
  memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);
  if (bind(serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr))){
    printf("Error while binding.\n");
    return 1;
  }
  if(listen(serverSocket,50)==0)
    printf("Serving on %s:%d\n", ADDRESS, PORT);
  else
    printf("Error\n");
  
  //Check for new connections
  pthread_t thread_id;
  connection_accepter accptr = (connection_accepter){
    .server_socket=serverSocket,
    .ses=&ses
  };

  if(pthread_create(&thread_id, NULL, accept_connections, &accptr) != 0 )
     printf("Failed to create thread\n");
  pthread_detach(thread_id);
  
  ses.track_pos = 0.0;
  ses.paused=false;
  //Naive, but probably works just fine
  while(1){
    WaitTime(1.0);
    if (!ses.paused) ses.track_pos += 1.0;
    if (ses.track_pos>=ses.tracks_data[ses.current_track].audio_duration){
      ses.track_pos=0.0;
      ses.current_track=(ses.current_track+1)%ses.tracks_len;
      int next_id = (ses.current_track+1)%ses.tracks_len;
      for (int i = 0; i<ses.clients_len; i++){
        int sck = ses.client_sockets[i];
        send_cmd(sck, dradio_skip_response);
        if (!ses.paused) send_cmd(sck, dradio_play_response);
        send_track(sck, next_id);
      }
    }
  }
  return 0;
}
