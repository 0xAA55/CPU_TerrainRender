#ifndef _IMAGE_BUFFER_H_
#define _IMAGE_BUFFER_H_ 1

#include<stdint.h>
#include<stddef.h>
#include"unibmp.h"

typedef struct ImgBuffer_struct
{
	uint32_t Width;
	uint32_t Height;

	uint32_t Width2N;
	uint32_t Height2N;

	size_t UnitSize;
	size_t RowAlign;

	size_t Pitch;
	size_t BufferSize;

	void* Buffer;
	void** RowPointers;

	int OwnBuffer;
}ImgBuffer_t, * ImgBuffer_p;

typedef void(*fn_Interpolate)(ImgBuffer_p dst, int dst_x, int dst_y, ImgBuffer_p src, float src_x, float src_y);
typedef void(*fn_ShrinkToPixel)(ImgBuffer_p dst, int dst_x, int dst_y, ImgBuffer_p src, int src_x, int src_y, uint32_t src_w, uint32_t src_h);

ImgBuffer_p ImgBuffer_Create(uint32_t Width, uint32_t Height, size_t UnitSize, size_t RowAlign);
ImgBuffer_p ImgBuffer_CreateFromBuffer(void *Buffer, uint32_t Width, uint32_t Height, size_t UnitSize, size_t RowAlign, int OwnBuffer);
ImgBuffer_p ImgBuffer_CreateFromBMPFile(const char *path);
ImgBuffer_p ImgBuffer_ConvertFromUniformBitmap(UniformBitmap_p* ppUB);
UniformBitmap_p ImgBuffer_ConvertToUniformBitmap(ImgBuffer_p *ppIB);
int ImgBuffer_Grow2N(ImgBuffer_p pBuf, fn_Interpolate pfn_Interpolate);
int ImgBuffer_Shrink2N(ImgBuffer_p pBuf, fn_ShrinkToPixel pfn_ShrinkToPixel);
int ImgBuffer_To2N(ImgBuffer_p pBuf, fn_Interpolate pfn_Interpolate, fn_ShrinkToPixel pfn_ShrinkToPixel);
void ImgBuffer_Blt(ImgBuffer_p pDst, int x, int y, int w, int h, ImgBuffer_p pSrc, int src_x, int src_y);
void ImgBuffer_Destroy(ImgBuffer_p pBuf);

#define ImgBuffer_Fetch(pBuf,x,y,type) (((type*)((pBuf)->RowPointers[y]))[x])

#define ImgBuffer_FetchI8(pBuf,x,y)  ImgBuffer_Fetch(pBuf, x, y, int8_t)
#define ImgBuffer_FetchU8(pBuf,x,y)  ImgBuffer_Fetch(pBuf, x, y, uint8_t)
#define ImgBuffer_FetchI16(pBuf,x,y) ImgBuffer_Fetch(pBuf, x, y, int16_t)
#define ImgBuffer_FetchU16(pBuf,x,y) ImgBuffer_Fetch(pBuf, x, y, uint16_t)
#define ImgBuffer_FetchI32(pBuf,x,y) ImgBuffer_Fetch(pBuf, x, y, int32_t)
#define ImgBuffer_FetchU32(pBuf,x,y) ImgBuffer_Fetch(pBuf, x, y, uint32_t)
#define ImgBuffer_FetchI64(pBuf,x,y) ImgBuffer_Fetch(pBuf, x, y, int64_t)
#define ImgBuffer_FetchU64(pBuf,x,y) ImgBuffer_Fetch(pBuf, x, y, uint64_t)
#define ImgBuffer_FetchF32(pBuf,x,y) ImgBuffer_Fetch(pBuf, x, y, float)
#define ImgBuffer_FetchF64(pBuf,x,y) ImgBuffer_Fetch(pBuf, x, y, double)

uint32_t ARGB(int32_t R, int32_t G, int32_t B, int32_t A);
uint32_t ARGBSafe(int32_t R, int32_t G, int32_t B, int32_t A);
uint32_t ARGB_Lerp(uint32_t c1, uint32_t c2, float s);

#endif 
