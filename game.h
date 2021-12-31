#ifndef _TERRAIN_H_
#define _TERRAIN_H_ 1

#include "cpucan.h"
#include "raymap.h"

typedef enum Game_Input_enum
{
	GI_Forward,
	GI_Backward,
	GI_Left,
	GI_Right,
	GI_Jump,
	GI_Crouch,
	GI_ShootGun,
	GI_ReloadGun,
	GI_Use,
	GI_Exit,

	GI_KeyCount
}Game_Input_t, *Game_Input_p;

typedef struct Game_struct
{
	CPUCan_p CPUCan;
	FILE *fp_log;

	int profile_task_per_line;
	int profile_task_per_pixel;

	int XRes;
	int YRes;
	int Interleave;
	int XScaling;
	int YScaling;
	int Interpolate;
	uint32_t Skycolor1;
	uint32_t Skycolor2;
	vec4_t Gravity;

	ImgBuffer_p Albedo;
	double AlbedoScale;
	double AltitudeScale;

	RayMap_p RayMap;
	RayMap_p WalkMap;

	float RenderDist;
	vec4_t CameraPos;
	mat4_t CamRotMat;
	float FOV;
	float Aspect;

	ImgBuffer_p LandView;
	int IterCount;

	int KBD_Status[GI_KeyCount];
	float Yaw;
	float Pitch;
	float Roll;
	float Sensitivity;

	float Player_Size;
	float Player_Height;
	float Player_MinHeight;
	float Player_MaxHeight;
	float Player_CrouchHeight;
	float Player_StandingDamp;
	float Player_Speed;
	float Player_Accel;
	float Player_Jump;
	float Player_Friction;
	float Player_FlyingSpeed;
	float Player_SpeedDamp;
	float Player_BobbingRate;
	float Player_BobbingAmplitude;

	vec4_t Player_Pos;
	vec4_t Player_Vel;
	int Player_IsTouchingGround;
	int Player_IsCrouching;
	int Player_IsJumping;
	float Player_BobbingPhase;

	double DeltaTime;
	double LastTime;
	uint64_t FrameCounter;
}Game_t, *Game_p;

Game_p Game_Create(CPUCan_p CPUCan, const char *WorkDir, const char *MapDir, dict_p Config);
void Game_SetCamera(Game_p t, vec4_t CamPos, mat4_t CamRotMat, float FOV);
float Game_GetAltitude(Game_p t, float x, float z);
void Game_KBDInput(Game_p t, Game_Input_t key, int status);
void Game_FPSInput(Game_p t, int delta_x, int delta_y);
void Game_Update(Game_p t, double Time);
void Game_Render(Game_p t);
void Game_Free(Game_p t);




#endif
