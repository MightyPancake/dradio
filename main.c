#include <raylib.h>
#include "dradio.h"
#include <math.h>

#define discBlack 40
#define lineBlack 20

//Global variables
Track track;


int main(void){
    //General
    const int screenWidth = 1800;
    const int screenHeight = 1080;
    float dt;
    //Initialize window
    InitWindow(screenWidth, screenHeight, "Client");
    SetTargetFPS(120);
    //Load textures
    Texture2D table = LoadTexture("table.png");
    //Initialize audio device
    InitAudioDevice();
    //Start sesssion
    while (!WindowShouldClose()){
        dt = GetFrameTime();
        // Draw
        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("Congrats! You created your first window!", 190, 200, 20, LIGHTGRAY);
        EndDrawing();
    }
    CloseWindow();

    return 0;
}