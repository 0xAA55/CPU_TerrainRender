#ifndef _RAYMAP_H_
#define _RAYMAP_H_ 1

#include"imgbuffer.h"
#include <stdio.h>
#include <mathutil.h>

typedef struct RayMap_struct
{
	ImgBuffer_p Altitude;
	ImgBuffer_p K;
	float MaxAltitude;
	float MinAltitude;
}RayMap_t, *RayMap_p;

RayMap_p RayMap_Load(char *AssetsDirectory, char *AltitudeFile, char *KFile, FILE *fp_log);
RayMap_p RayMap_LoadFromRaw(char *AssetsDirectory, char *AltitudeFile, char *RawAltitudeFile, char *KFile, int RawAltitudeBlur, FILE *fp_log);

// Create from altitude, but not copy the altitude data, instead of keep it
RayMap_p RayMap_CreateFromAltitude(ImgBuffer_p Altitude, char *AssetsDirectory, char *AltitudeSaveFile, char *KFile_SavePath, FILE *fp_log);
void RayMap_Unload(RayMap_p r);
float RayMap_GetAltitude(RayMap_p r, float x, float y);
float RayMap_GetK(RayMap_p r, float x, float y);
int RayMap_Raycast(RayMap_p r, vec4_t RayOrig, vec4_t RayDir, int IterCount, vec4_p Out_CastPoint, float *Out_CastDist, float MaxDist);


#endif
