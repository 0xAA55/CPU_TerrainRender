#ifndef _TERRAIN_H_
#define _TERRAIN_H_ 1

#include "cpucan.h"
#include <stdio.h>
#include <mathutil.h>

typedef struct Terrain_struct
{
	CPUCan_p CPUCan;
	FILE *fp_log;

	int x_res;
	int y_res;
	int interleave;
	int x_scale;
	int y_scale;

	ImgBuffer_p Albedo;
	double AlbedoScale;
	double AltitudeScale;

	ImgBuffer_p Altitude;
	int RawAltitudeBlur;

	ImgBuffer_p K;

	float RenderDist;
	vec4_t CameraPos;
	vec4_t CameraDir;
	vec4_t CameraUp;
	float FOV;
	float Aspect;

	ImgBuffer_p LandView;
	int IterCount;
}Terrain_t, *Terrain_p;

Terrain_p Terrain_Create(CPUCan_p CPUCan, const char *WorkDir, const char *MapDir, dict_p Config);
void Terrain_SetCamera(Terrain_p t, vec4_t CamPos, vec4_t CamDir, vec4_t CamUp, float FOV);
void Terrain_Render(Terrain_p t);
void Terrain_Free(Terrain_p t);




#endif
