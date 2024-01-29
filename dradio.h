#include <raylib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define alloc_str(STR) ({char* tmp__str = (char*)malloc(strlen(STR)+1); strcpy(tmp__str, STR); tmp__str;})

#define tracks_cap 4
#define of_megabytes(A) (A*1048576)
#define MAX_AUDIO_SIZE 12
#define MAX_COVER_SIZE 3
#define MAX_AUDIO_SZ of_megabytes(MAX_AUDIO_SIZE)
#define MAX_COVER_SZ of_megabytes(MAX_COVER_SIZE)
#define MAX_NAME_SZ 128

#define MAX_CLIENT_SZ 16

#define MAX_TRACKS_SZ 64

#define MAX_PACKET_DATA_SZ (1048576/32)
// #define MAX_PACKET_DATA_SZ 2

//Server will require confirmation of receiving data after N amount of bytes sent
#define CONFIRM_EVERY (MAX_PACKET_DATA_SZ*4)

typedef struct dradio_track_data{
	char*						audio_data;
	long						audio_sz;
	char*						cover_data;
	long						cover_sz;
	char* 					name;
	float						audio_duration;
}dradio_track_data;

typedef enum dradio_request_kind{
	dradio_no_request,
	dradio_skip_request,
	dradio_pause_request,
	dradio_play_request
}dradio_request_kind;

typedef struct dradio_request{
	dradio_request_kind		kind;
	long									sz;
}dradio_request;

const char* request_str(dradio_request r){
	switch(r.kind){
		case dradio_skip_request: return "skip"; break;
		case dradio_pause_request: return "pause"; break;
		case dradio_play_request: return "play"; break;
		case dradio_no_request: return "no"; break;
		default: return "unknown"; break;
	}
}

typedef enum dradio_response_kind{
	dradio_no_response,
	dradio_audio_response,
	dradio_cover_response,
	dradio_track_response,
	dradio_seek_response,
	dradio_skip_response,
	dradio_play_response,
	dradio_pause_response
}dradio_response_kind;

typedef struct dradio_response{
	dradio_response_kind		kind;
	long										sz;
}dradio_response;

const char* response_str(dradio_response r){
	switch(r.kind){
		case dradio_audio_response: return "audio"; break;
		case dradio_cover_response: return "cover"; break;
		case dradio_track_response: return "track"; break;
		case dradio_seek_response: return "seek"; break;
		case dradio_skip_response: return "skip"; break;
		case dradio_play_response: return "play"; break;
		case dradio_pause_response: return "pause"; break;
		case dradio_no_response: return "no"; break;
		default: return "unknown"; break;
	}
}

typedef struct Track{
	char					name[MAX_NAME_SZ];
	Music					music;
	Texture2D			cover;
}Track;

typedef struct dradio_session{
	dradio_track_data	tracks_data[MAX_TRACKS_SZ];
	int								tracks_len;
	int								current_track;
	bool							paused;
	float							track_pos;
	int								client_sockets[MAX_CLIENT_SZ];
	int								clients_len;
}dradio_session;

typedef struct dradio_client{
	//Tracks
	int					tracks_len;
	Track*			tracks;
	//Current song
	int					current_track;
	float				track_pos;
	bool				paused;
}dradio_client;

char* file_to_bytes(const char* filename, long *sz){
  char *buf;
  long len;
  FILE *f = fopen(filename, "rb");
  if (f){
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek (f, 0, SEEK_SET);
    buf = malloc(len);
    if (buf){
      int n = fread(buf, 1, len, f);
    }
    fclose (f);
  }else{
		return NULL;
	}
  *sz=len;
  return buf;
}

void bytes_to_file(char* filename, char* bytes, long sz){
	FILE *file = fopen(filename, "wb");
	if (file==NULL){
		printf("Couldn't create a file!\n");
		exit(1);
	}
	fwrite(bytes, sizeof(char), (size_t)sz, file);
	fclose(file);
}

dradio_track_data loadTrackData(const char* dir, const char* name){
	dradio_track_data ret;
	int no_ext_len = strlen(dir) + 1 + strlen(name); //dir/name
	char* fullpath = (char*)malloc(no_ext_len+1+4); //.png/.mp3 + '\0'
	strcpy(fullpath, dir);
	strcat(fullpath, "/");
	strcat(fullpath, name);
	//Set name
	ret.name = (char*)malloc(strlen(name)+1);
	strcpy(ret.name, name);
	//Load audio file
	fullpath[no_ext_len] = '\0';
	strcat(fullpath, ".mp3");
	ret.audio_data = file_to_bytes(fullpath, &(ret.audio_sz));
	if (ret.audio_data==NULL) printf("Couldn't load audio file '%s'.\n", fullpath);
	else if (ret.audio_sz>MAX_AUDIO_SZ) printf("Audio size %ld is over the limit of %d.\n", ret.audio_sz, MAX_AUDIO_SZ);
	else {
		//Get audio duration
		Music music = LoadMusicStream(fullpath);
		ret.audio_duration=GetMusicTimeLength(music);
		UnloadMusicStream(music);
	}
	//Load cover file
	fullpath[no_ext_len] = '\0';
	strcat(fullpath, ".png");
	ret.cover_data = file_to_bytes(fullpath, &(ret.cover_sz));
	if (ret.cover_data==NULL) printf("Couldn't load cover file '%s'.\n", fullpath);
	else if (ret.cover_sz>MAX_COVER_SZ) printf("Cover size %ld is over the limit of %d.\n", ret.cover_sz, MAX_COVER_SZ);
	return ret;
}

void new_session(dradio_session *ses, const char* dir){
	printf("Creating session from directory '%s'...\n", dir);
	InitAudioDevice();
	//Wait until audio device is initiated
	while (!IsAudioDeviceReady()) ;
	ses->tracks_len=0;
	ses->tracks_len = 0;
	ses->current_track = 0;
	ses->paused = true;
	ses->track_pos = 0.0;
	ses->clients_len = 0;
	FilePathList filepaths = LoadDirectoryFilesEx(dir, ".mp3", false);
	for (int i = 0; i<filepaths.count; i++){
		char* filename = filepaths.paths[i];
		printf("Loading track '%s'\n", GetFileNameWithoutExt(filename));
		ses->tracks_data[ses->tracks_len] = loadTrackData(dir, GetFileNameWithoutExt(filename));
		ses->tracks_len++;
	}
}

