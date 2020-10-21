#ifndef _CPUCAN_H_
#define _CPUCAN_H_ 1

#include"imgbuffer.h"
#include"dict.h"
#include<stdint.h>

#define BUFFER_STACK_SIZE 32

typedef struct CPUCan_struct
{
	uint32_t Width;
	uint32_t Height;

	ImgBuffer_p ColorBuf;
	ImgBuffer_p DepthBuf;

	dict_p Textures;
}CPUCan_t, *CPUCan_p;


CPUCan_p CPUCan_Create(uint32_t Width, uint32_t Height);
void CPUCan_Delete(CPUCan_p c);

int CPUCan_LoadTextureFromFile(CPUCan_p c, const char *path, const char *name_of_texture);
int CPUCan_CreateTexture(CPUCan_p c, uint32_t Width, uint32_t Height, const char *name_of_texture);
ImgBuffer_p CPUCan_GetTexture(CPUCan_p c, const char *name_of_texture);
int CPUCan_SetTexture(CPUCan_p c, const char *name_of_texture, ImgBuffer_p Texture);
void CPUCan_DeleteTexture(CPUCan_p c, const char *name_of_texture);





#endif
