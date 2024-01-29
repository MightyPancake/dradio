#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
//math
#include <math.h>
//Port
#define DEFAULT_PORT 1100
//dradio
#include "dradio.h"
#define discBlack 40
#define lineBlack 20

int volume = 10; //0-20
bool paused = false;

//Global variables
dradio_track_data track_data;
int tracks_loaded = 0; //This is 0, 1 or 2
Track track;
Track next_track;
//UI related
const int screenWidth = 1800;
const int screenHeight = 1080;
const int fontSize = (int)((float)screenHeight*0.04);
float dt;
Texture2D table;
//Disc drawing
Vector2 center;
float angle = 0.0;
float rotSpeed = 80.0; // Deg./sec.
const float radius = (screenHeight<screenWidth ? screenHeight : screenWidth)/2.0*0.85;
const float lineWidth = radius/100.0;
//Colors
Color discColor = (Color){discBlack, discBlack, discBlack, 255};
Color discLineColor = (Color){lineBlack, lineBlack, lineBlack, 255};

int sock;

void connect_to_server(const char* address, int port){
  printf("Connecting to: %s:%d...\n", address, port);
  //Start connection out
  struct sockaddr_in serverAddr = (struct sockaddr_in){
    .sin_family = AF_INET,
    .sin_port = htons(port),
    .sin_addr.s_addr = inet_addr(address)
  };
  memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);

  sock = socket(PF_INET, SOCK_STREAM, 0);

  serverAddr.sin_family = AF_INET;

  if (connect(sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr))){
    switch(errno){
      case ECONNREFUSED:
        printf("Connection refused!\n"); break;
      case ENETUNREACH:
        printf("Connection unreachable!\n"); break;
      default:
        printf("Uknown error.\n"); break;
    }
    exit(1);
  }
  printf("Connected.\n");
}

char* time_str(char* str, float pos, float len){
  if ((int)pos%60<10){
    if ((int)len%60<10){
      sprintf(str, "%d:0%d - %d:0%d", (int)pos/60, (int)pos%60, (int)len/60, (int)len%60);
    }else{
      sprintf(str, "%d:0%d - %d:%d", (int)pos/60, (int)pos%60, (int)len/60, (int)len%60);
    }
  }else{
    if ((int)len%60<10){
      sprintf(str, "%d:%d - %d:0%d", (int)pos/60, (int)pos%60, (int)len/60, (int)len%60);
    }else{
      sprintf(str, "%d:%d - %d:%d", (int)pos/60, (int)pos%60, (int)len/60, (int)len%60);
    }
  }
  return str;
}

void drawUI(){
  DrawTextureEx(table, (Vector2){0,0}, 0.0, 1.0, WHITE);
  if (tracks_loaded==0) return;
  //Draw cover
  int coverSize = (int)radius*0.8;
  DrawTexturePro(track.cover, (Rectangle){0,0, track.cover.width, track.cover.height}, (Rectangle){screenWidth/2, screenHeight/2, coverSize, coverSize}, (Vector2){coverSize/2, coverSize/2}, -angle, WHITE);
  //Vinyl
  DrawRing(center, 0.0, radius*0.07, 0.0, 360.0, 0, LIGHTGRAY);
  DrawText("Now playing:", 0, 0, fontSize, WHITE);
  DrawText(track.name, 0, fontSize+fontSize/2, fontSize, WHITE);
  //Main disc part
  DrawRing(center, radius*0.35, radius, 0.0, 360.0, 0, discColor);
  //Middle
  // DrawRing(center, radius*0.07, radius*0.37, 0.0, 360.0, 0, GOLD);
  DrawRing(center, radius*0.07, radius*0.12, 0.0, 360.0, 0, (Color){0,0,0,50});
  DrawRing(center, radius*0.35, radius*0.42, 0.0, 360.0, 0, BLACK);
  DrawRing(center, radius*0.98, radius, 0.0, 360.0, 0, BLACK);
  //Additional lines
  for (float i = 0.0; i<27.0; i++)
      DrawRing(center, radius*0.43+i*radius*0.02, radius*0.43+i*radius*0.02+lineWidth, 0, 360.0, 0, discLineColor);
  float max_i = 50.0;
  int max_shadow = 200;
  int shadow_incr = max_shadow/max_i;
  float spread = 70.0;
  for (float i = 0.0; i<max_i; i++){
      DrawRing(center, radius*0.43, radius, angle-i/max_i*spread, angle+i/max_i*spread, 0, (Color){0,0,0,200-(int)i*shadow_incr});
      DrawRing(center, radius*0.43, radius, 180.0+angle-i/max_i*spread, 180.0+angle+i/max_i*spread, 0, (Color){0,0,0,200-(int)i*shadow_incr});
  }
  //Position bar
  float len = GetMusicTimeLength(track.music);
  float pos = GetMusicTimePlayed(track.music);
  int bar_height = radius*0.05;
  DrawRectangle(0, screenHeight-bar_height, screenWidth*(int)pos/len, bar_height, (Color){255,255,255,80});
  //Position
  char posstr[100];
  time_str(posstr, pos, len);
  DrawText(posstr, 0, screenHeight-bar_height-fontSize, fontSize, WHITE);
  //Draw volume
  int vol_width = radius*0.11;
  int vol_height = screenHeight*0.02;
  for (int i = 0; i<volume; i++){
    DrawRectangle(screenWidth-vol_width, screenHeight*0.7-i*3*vol_height/2, vol_width, vol_height, (Color){255,255,255,120});
  }
}

void send_cmd(int dest, dradio_request_kind cmd){
  char ok = 'y';
  dradio_request request = (dradio_request){
    .kind=cmd,
    .sz=(long)sizeof(ok)
  };
  int n = send(dest, &request, sizeof(request), 0);
  send(dest, &ok, sizeof(ok), 0);
}

int main(int argc, const char** argv){
  SetTraceLogLevel(LOG_ERROR);
  if (argc==0){
    printf("Please provide a valid server adress.\n");
    return 1;
  }
  //Initialize window
  InitWindow(screenWidth, screenHeight, "dradio client");
  center = (Vector2){GetScreenWidth()/2.0f, GetScreenHeight()/2.0f};
  SetTargetFPS(120);
  //Load textures
  table = LoadTexture("table.png");
  //Initialize audio device
  InitAudioDevice();
  //Connect to server
  connect_to_server(argv[1], DEFAULT_PORT);
  //Main loop
  char confirm = 'o'; //ok
  int confirms_sent = 0;
  int n = 0;
  long rcvd = 0;
  int expected_sz = MAX_PACKET_DATA_SZ;
  char* rcvd_bytes = (char*)malloc(of_megabytes(12));
  if (rcvd_bytes==NULL){printf("Couldn't allocate receive buffer.\n"); return 1;}
  for (long i=0;i<of_megabytes(12); i++) rcvd_bytes[i]= '\0';
  char buf[MAX_PACKET_DATA_SZ];
  dradio_response resp = (dradio_response){.kind=dradio_no_response, .sz=0};
  while(!WindowShouldClose()){
    if (resp.kind == dradio_no_response){
        n=recv(sock, &resp, sizeof(resp), MSG_DONTWAIT);
        if (n<=0) resp.kind=dradio_no_response;
        if (resp.kind!=dradio_no_response) printf("Receiving %s...\n", response_str(resp));
    }else{
      expected_sz = resp.sz-rcvd<MAX_PACKET_DATA_SZ ? resp.sz-rcvd : MAX_PACKET_DATA_SZ;
      //FIX: Write straight to rcvd_bytes
      n=recv(sock, buf, expected_sz, MSG_DONTWAIT);
      if (n>0){
        for (int i=0; i<n; i++) rcvd_bytes[rcvd+(long)i] = buf[i];
        rcvd+=(long)n;
        // printf("Receiving file (%ld/%ld bytes)\n", rcvd, resp.sz);
        // printf("%ld/%ld\n", rcvd, resp.sz);
        if (rcvd/CONFIRM_EVERY>confirms_sent){
          send(sock, &confirm, 1, 0);
          // printf("Sending confirmation!\n");
          confirms_sent++;
        }
        if (rcvd==resp.sz){
          switch(resp.kind){
            case dradio_audio_response:
              printf("Audio received!\n");
              //Read audio
              bytes_to_file("audio.mp3", rcvd_bytes, rcvd);
              if (tracks_loaded==0)
                track.music=LoadMusicStream("audio.mp3");
              else
                next_track.music=LoadMusicStream("audio.mp3");
              remove("audio.mp3");
              break;
            case dradio_cover_response:
              printf("Cover received!\n");
              //Read cover
              bytes_to_file("cover.png", rcvd_bytes, rcvd);
              if (tracks_loaded==0)
                track.cover=LoadTexture("cover.png");
              else
                next_track.cover=LoadTexture("cover.png");
              remove("cover.png");
              break;
            case dradio_track_response:
              //Read track name
              if (tracks_loaded==0)
                strcpy(track.name, rcvd_bytes);
              else
                strcpy(next_track.name, rcvd_bytes);
              printf("Track '%s' received!\n", rcvd_bytes);
              tracks_loaded++;
              break;
            case dradio_seek_response:
              if (tracks_loaded>0){
                float pos = ((float*)(rcvd_bytes))[0];
                printf("Seeked current track to %f!\n", pos);
                SeekMusicStream(track.music, pos);
              }
              break;
            case dradio_skip_response:
              printf("Skipped!\n");
              tracks_loaded--;
              if (tracks_loaded==0) break;
              UnloadMusicStream(track.music);
              UnloadTexture(track.cover);
              track = next_track;
              next_track = (Track){};
              break;
            case dradio_pause_response:
              printf("Pausing!\n");
              PauseMusicStream(track.music);
              paused=true;
              break;
            case dradio_play_response:
              printf("Playing!\n");
              PlayMusicStream(track.music);
              paused=false;
              break;
          }
          //Reset
          resp.kind=dradio_no_response;
          rcvd=0;
          confirms_sent=0;
        }
      }
    }
    //Update
    dt = GetFrameTime();
    if (!paused) angle = fmod(360.0+angle-rotSpeed*dt, 360.0);
    if (tracks_loaded>0) UpdateMusicStream(track.music);
    //Handle key presses
    int key_pressed;
    while(key_pressed=GetKeyPressed()){
      switch(key_pressed){
        case KEY_SPACE:
          if (tracks_loaded==0) break;
          if (paused)
            send_cmd(sock, dradio_play_request);
          else
            send_cmd(sock, dradio_pause_request);
          break;
        case KEY_RIGHT:
          if (tracks_loaded==0) break;
            send_cmd(sock, dradio_skip_request);
          break;
        case KEY_UP:
          volume++;
          if (volume>20) volume=20;
          SetMasterVolume((float)volume/20.0);
          break;
        case KEY_DOWN:
          volume--;
          if (volume<0) volume=0;
          SetMasterVolume((float)volume/20.0);
          break;
      }
    }
    //Draw
    BeginDrawing();
    ClearBackground(RAYWHITE);
    drawUI();
    EndDrawing();
  }
  //Free stuff
  CloseWindow();
  close(sock);
  return 0;
}

