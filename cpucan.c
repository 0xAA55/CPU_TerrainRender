#include"cpucan.h"
#include"crc3264.h"

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<math.h>

static int CompFunc(const void *key1, const void *key2)
{
	return strcmp(key1, key2);
}

static void TexOnRemove(void *value)
{
	ImgBuffer_Destroy(value);
}

CPUCan_p CPUCan_Create(uint32_t Width, uint32_t Height)
{
	CPUCan_p c = NULL;

	if (!Width || !Height) return c;
	
	c = malloc(sizeof * c);
	if (!c) return c;
	memset(c, 0, sizeof *c);

	c->Width = Width;
	c->Height = Height;

	c->Textures = dict_create();
	if (!c->Textures) goto FailExit;

	dict_set_compare_func(c->Textures, CompFunc);
	dict_set_on_delete_value(c->Textures, TexOnRemove);

	c->ColorBuf = ImgBuffer_Create(Width, Height, 4, 4);
	c->DepthBuf = ImgBuffer_Create(Width, Height, 4, 4);
	if (!c->ColorBuf || !c->DepthBuf) goto FailExit;

	if (!CPUCan_SetTexture(c, "ColorBuf", c->ColorBuf)) goto FailExit;
	if (!CPUCan_SetTexture(c, "DepthBuf", c->DepthBuf)) goto FailExit;

	return c;
FailExit:
	CPUCan_Delete(c);
	return NULL;
}

CPUCan_p CPUCan_CreateWithRGBAFB(uint32_t Width, uint32_t Height, void *Address_RGBA_FB)
{
	CPUCan_p c = NULL;

	if (!Width || !Height) return c;

	c = malloc(sizeof * c);
	if (!c) return c;
	memset(c, 0, sizeof * c);

	c->Width = Width;
	c->Height = Height;

	c->Textures = dict_create();
	if (!c->Textures) goto FailExit;

	dict_set_compare_func(c->Textures, CompFunc);
	dict_set_on_delete_value(c->Textures, TexOnRemove);

	c->ColorBuf = ImgBuffer_CreateFromBuffer(Address_RGBA_FB, Width, Height, 4, 4, 0);
	c->DepthBuf = ImgBuffer_Create(Width, Height, 4, 4);
	if (!c->ColorBuf || !c->DepthBuf) goto FailExit;

	if (!CPUCan_SetTexture(c, "ColorBuf", c->ColorBuf)) goto FailExit;
	if (!CPUCan_SetTexture(c, "DepthBuf", c->DepthBuf)) goto FailExit;

	return c;
FailExit:
	CPUCan_Delete(c);
	return NULL;
}

ImgBuffer_p CPUCan_GetTexture(CPUCan_p c, const char *name_of_texture)
{
	return dict_search(c->Textures, name_of_texture);
}

int CPUCan_SetTexture(CPUCan_p c, const char *name_of_texture, ImgBuffer_p Texture)
{
	return dict_assign(c->Textures, name_of_texture, Texture) == ds_ok;
}

void CPUCan_DeleteTexture(CPUCan_p c, const char *name_of_texture)
{
	dict_remove(c->Textures, name_of_texture);
}

void CPUCan_Delete(CPUCan_p c)
{
	if (!c) return;

	dict_delete(c->Textures);
	free(c);
}

int LoadBMPTextureFromFile(CPUCan_p c, const char* path, const char* name_of_texture)
{
	UniformBitmap_p UB = NULL;
	ImgBuffer_p NewImg = NULL;

	if (!c || !path || !name_of_texture) return 0;

	if (dict_search(c->Textures, name_of_texture)) goto FailExit;

	UB = UB_CreateFromFile(path);
	if (!UB) goto FailExit;

	NewImg = ImgBuffer_ConvertFromUniformBitmap(&UB);
	if (!NewImg) goto FailExit;

	if (dict_insert(c->Textures, name_of_texture, NewImg) != ds_ok) goto FailExit;
	return 1;
FailExit:
	ImgBuffer_Destroy(NewImg);
	UB_Free(&UB);
	return 0;
}

int CPUCan_LoadTextureFromFile(CPUCan_p c, const char* path, const char* name_of_texture)
{
	return LoadBMPTextureFromFile(c, path, name_of_texture);
}

int CPUCan_CreateTexture(CPUCan_p c, uint32_t Width, uint32_t Height, const char *name_of_texture)
{
	ImgBuffer_p NewImg = NULL;

	if (!c || !Width || !Height || !name_of_texture) return 0;

	if (dict_search(c->Textures, name_of_texture)) goto FailExit;

	NewImg = ImgBuffer_Create(Width, Height, 4, 4);
	if (!NewImg) goto FailExit;

	if (dict_insert(c->Textures, name_of_texture, NewImg) != ds_ok) goto FailExit;
	return 1;
FailExit:
	ImgBuffer_Destroy(NewImg);
	return 0;
}

uint32_t CPUCan_SampleTexture(ImgBuffer_p i, float x, float y)
{
	float fx = floorf(x);
	float fy = floorf(y);
	uint32_t ux = (uint32_t)fx;
	uint32_t uy = (uint32_t)fy;
	float sx = x - fx;
	float sy = y - fy;
	uint32_t c00 = ImgBuffer_FetchU32(i, (ux + 0) & (i->Width2N - 1), (uy + 0) & (i->Height2N - 1));
	uint32_t c01 = ImgBuffer_FetchU32(i, (ux + 0) & (i->Width2N - 1), (uy + 1) & (i->Height2N - 1));
	uint32_t c10 = ImgBuffer_FetchU32(i, (ux + 1) & (i->Width2N - 1), (uy + 0) & (i->Height2N - 1));
	uint32_t c11 = ImgBuffer_FetchU32(i, (ux + 1) & (i->Width2N - 1), (uy + 1) & (i->Height2N - 1));

	return ARGB_Lerp(
		ARGB_Lerp(c00, c10, sx),
		ARGB_Lerp(c01, c11, sx),
		sy);
}
