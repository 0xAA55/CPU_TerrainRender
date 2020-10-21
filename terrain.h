#ifndef _TERRAIN_H_
#define _TERRAIN_H_ 1

#include "cpucan.h"
#include <mathutil.h>

typedef struct Terrain_struct
{
	CPUCan_p CPUCan;

	dict_p Config;
	ImgBuffer_p Albedo;
	double AlbedoScale;
	double AltitudeScale;

	ImgBuffer_p Altitude;
	int RawAltitudeBlur;

	ImgBuffer_p K;

	vec4_t CameraPos;
	vec4_t CameraDir;
	vec4_t CameraUp;
}Terrain_t, *Terrain_p;

Terrain_p Terrain_Create(CPUCan_p CPUCan, const char *dir);
void Terrain_Render(Terrain_p t);
void Terrain_Free(Terrain_p t);




#endif
