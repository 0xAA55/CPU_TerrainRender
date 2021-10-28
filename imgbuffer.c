#include"imgbuffer.h"

#include<stdlib.h>
#include<string.h>

static size_t CalcAlign(size_t Size, size_t Align)
{
	return ((Size - 1) / Align + 1) * Align;
}

ImgBuffer_p ImgBuffer_Create(uint32_t Width, uint32_t Height, size_t UnitSize, size_t RowAlign)
{
	size_t i;
	uint8_t* Buffer = NULL;

	if (!Width || !Height || !UnitSize) return 0;
	if (!RowAlign) RowAlign = 4;

	i = CalcAlign(Width * UnitSize, RowAlign) * Height;
	Buffer = malloc(i);
	if (!Buffer) goto FailExit;

	return ImgBuffer_CreateFromBuffer(Buffer, Width, Height, UnitSize, RowAlign, 1);
FailExit:
	free(Buffer);
	return 0;
}

ImgBuffer_p ImgBuffer_CreateFromBuffer(void *Buffer, uint32_t Width, uint32_t Height, size_t UnitSize, size_t RowAlign, int OwnBuffer)
{
	size_t i;
	ImgBuffer_p pBuf = NULL;

	if (!Buffer || !Width || !Height || !UnitSize) return 0;
	if (!RowAlign) RowAlign = 4;

	pBuf = malloc(sizeof * pBuf);
	if (!pBuf) return NULL;
	memset(pBuf, 0, sizeof * pBuf);

	pBuf->Width = Width;
	pBuf->Height = Height;

	pBuf->Width2N = 1; while (pBuf->Width2N <= Width) pBuf->Width2N <<= 1; pBuf->Width2N >>= 1;
	pBuf->Height2N = 1; while (pBuf->Height2N <= Height) pBuf->Height2N <<= 1; pBuf->Height2N >>= 1;

	pBuf->UnitSize = UnitSize;
	pBuf->RowAlign = RowAlign;

	pBuf->Pitch = CalcAlign(Width * UnitSize, RowAlign);
	pBuf->BufferSize = pBuf->Pitch * Height;

	pBuf->Buffer = Buffer;
	pBuf->RowPointers = malloc(sizeof pBuf->RowPointers[0] * Height);
	if (!pBuf->RowPointers) goto FailExit;

	Buffer = pBuf->Buffer;
	for (i = 0; i < Height; i++)
	{
		pBuf->RowPointers[i] = &((uint8_t*)Buffer)[i * pBuf->Pitch];
	}
	memset(pBuf->Buffer, 0, pBuf->BufferSize);
	pBuf->OwnBuffer = OwnBuffer;

	return pBuf;
FailExit:
	ImgBuffer_Destroy(pBuf);
	return 0;
}

ImgBuffer_p ImgBuffer_CreateFromBMPFile(const char *path)
{
	UniformBitmap_p UB = UB_CreateFromFile(path);
	return ImgBuffer_ConvertFromUniformBitmap(&UB);
}

ImgBuffer_p ImgBuffer_ConvertFromUniformBitmap(UniformBitmap_p *ppUB)
{
	ImgBuffer_p pBuf = NULL;

	if (!ppUB || !ppUB[0]) return NULL;

	pBuf = malloc(sizeof * pBuf);
	if (!pBuf) return NULL;
	memset(pBuf, 0, sizeof * pBuf);
	pBuf->Width = ppUB[0]->Width;
	pBuf->Height = ppUB[0]->Height;
	pBuf->Width2N = 1; while (pBuf->Width2N <= pBuf->Width) pBuf->Width2N <<= 1; pBuf->Width2N >>= 1;
	pBuf->Height2N = 1; while (pBuf->Height2N <= pBuf->Height) pBuf->Height2N <<= 1; pBuf->Height2N >>= 1;
	pBuf->UnitSize = 4;
	pBuf->RowAlign = 4;
	pBuf->Pitch = (size_t)ppUB[0]->Width * 4;
	pBuf->BufferSize = pBuf->Pitch * pBuf->Height;
	pBuf->Buffer = ppUB[0]->BitmapData;
	pBuf->RowPointers = (void **)ppUB[0]->RowPointers;
	pBuf->OwnBuffer = 1;

	free(ppUB[0]); ppUB[0] = NULL;
	return pBuf;
}

UniformBitmap_p ImgBuffer_ConvertToUniformBitmap(ImgBuffer_p *ppIB)
{
	UniformBitmap_p UB = NULL;

	if (!ppIB || !ppIB[0]) return NULL;

	UB = malloc(sizeof * UB);
	if (!UB) return NULL;
	memset(UB, 0, sizeof * UB);
	UB->Width = ppIB[0]->Width;
	UB->Height = ppIB[0]->Height;
	UB->BitmapData = ppIB[0]->Buffer;
	UB->RowPointers = (uint32_t**)ppIB[0]->RowPointers;

	free(ppIB[0]); ppIB[0] = NULL;
	return UB;
}

int ImgBuffer_Grow2N(ImgBuffer_p pBuf, fn_Interpolate pfn_Interpolate)
{
	ptrdiff_t i, c;
	ImgBuffer_p pTemp = NULL;
	uint32_t SW, SH;
	ptrdiff_t W2N, H2N;
	ptrdiff_t XM;
	int YShift = 0;
	void *TmpPtr;

	if (!pBuf || !pfn_Interpolate) return 0;
	if (!pBuf->OwnBuffer) return 0;

	SW = pBuf->Width;
	SH = pBuf->Height;
	W2N = H2N = 1;
	while (W2N < SW) W2N <<= 1;
	while (H2N < SH) { H2N <<= 1; YShift++; }

	if (pBuf->Width == W2N && pBuf->Height == H2N) return 1;

	pTemp = ImgBuffer_Create((uint32_t)W2N, (uint32_t)H2N, pBuf->UnitSize, pBuf->RowAlign);
	if (!pTemp) return 0;

	XM = W2N - 1;
	c = W2N * H2N;

#pragma omp parallel for
	for (i = 0; i < c; i++)
	{
		int dst_x = (int)(i & XM),
			dst_y = (int)(i >> YShift);
		float
			src_x = (float)dst_x * SW / W2N,
			src_y = (float)dst_y * SH / H2N;
		pfn_Interpolate(pTemp, dst_x, dst_y, pBuf, src_x, src_y);
	}

	TmpPtr = pBuf->Buffer;
	pBuf->Buffer = pTemp->Buffer;
	pTemp->Buffer = TmpPtr;

	TmpPtr = pBuf->RowPointers;
	pBuf->RowPointers = pTemp->RowPointers;
	pTemp->RowPointers = TmpPtr;

	ImgBuffer_Destroy(pTemp);

	return 1;
}

int ImgBuffer_Shrink2N(ImgBuffer_p pBuf, fn_ShrinkToPixel pfn_ShrinkToPixel)
{
	ptrdiff_t i, c;
	ImgBuffer_p pTemp = NULL;
	uint32_t SW, SH;
	ptrdiff_t W2N, H2N;
	ptrdiff_t XM;
	int YShift = 0;
	void *TmpPtr;

	if (!pBuf || !pfn_ShrinkToPixel) return 0;
	if (!pBuf->OwnBuffer) return 0;

	SW = pBuf->Width;
	SH = pBuf->Height;
	W2N = H2N = 1;
	while (W2N < SW) W2N <<= 1;
	while (H2N < SH) { H2N <<= 1; YShift++; }

	if (pBuf->Width == W2N && pBuf->Height == H2N) return 1;

	pTemp = ImgBuffer_Create((uint32_t)W2N, (uint32_t)H2N, pBuf->UnitSize, pBuf->RowAlign);
	if (!pTemp) return 0;

	XM = W2N - 1;
	c = W2N * H2N;

#pragma omp parallel for
	for (i = 0; i < c; i++)
	{
		int dst_x = (int)(i & XM),
			dst_y = (int)(i >> YShift);
		int
			src_x = (int)((float)dst_x * SW / W2N),
			src_y = (int)((float)dst_y * SH / H2N);
		uint32_t
			src_r = (uint32_t)((float)(dst_x + 1) * SW / W2N),
			src_b = (uint32_t)((float)(dst_y + 1) * SH / H2N);
		uint32_t src_w, src_h;
		if (src_r >= pBuf->Width) src_r = pBuf->Width;
		if (src_b >= pBuf->Height) src_b = pBuf->Height;
		src_w = src_r - src_x;
		src_h = src_b - src_y;
		pfn_ShrinkToPixel(pTemp, dst_x, dst_y, pBuf, src_x, src_y, src_w, src_h);
	}

	TmpPtr = pBuf->Buffer;
	pBuf->Buffer = pTemp->Buffer;
	pTemp->Buffer = TmpPtr;

	TmpPtr = pBuf->RowPointers;
	pBuf->RowPointers = pTemp->RowPointers;
	pTemp->RowPointers = TmpPtr;

	ImgBuffer_Destroy(pTemp);

	return 1;
}

int ImgBuffer_To2N(ImgBuffer_p pBuf, fn_Interpolate pfn_Interpolate, fn_ShrinkToPixel pfn_ShrinkToPixel)
{
	if (!ImgBuffer_Grow2N(pBuf, pfn_Interpolate))
	{
		if (!ImgBuffer_Shrink2N(pBuf, pfn_ShrinkToPixel))
		{
			return 0;
		}
	}
	return 1;
}

void ImgBuffer_Blt(ImgBuffer_p pDst, int x, int y, int w, int h, ImgBuffer_p pSrc, int src_x, int src_y)
{
	int i;
	size_t CopyCount;
	size_t SrcOffset, DstOffset;
	size_t UnitSize;

	if (!pDst || !pSrc || w <= 0 || h <= 0 || src_x < 0 || src_y < 0) return;

	UnitSize = pSrc->UnitSize;
	if (UnitSize != pDst->UnitSize) return;

	if (x < 0)
	{
		src_x -= x;
		w += x;
		x = 0;
	}
	else 
	{
		if ((uint32_t)(x + w) > pDst->Width) w = pDst->Width - x;
	}
	if ((uint32_t)(src_x + w) > pSrc->Width) w = pSrc->Width - src_x;
	if (w <= 0) return;

	if (y < 0)
	{
		src_y -= y;
		h += y;
		y = 0;
	}
	else
	{
		if ((uint32_t)(y + h) > pDst->Height) h = pDst->Height - y;
	}
	if ((uint32_t)(src_y + h) > pSrc->Height) h = pSrc->Height - src_y;
	if (h <= 0) return;

	SrcOffset = src_x * UnitSize;
	DstOffset = x * UnitSize;
	CopyCount = w * UnitSize;

#pragma omp parallel for
	for (i = 0; i < h; i++)
	{
		uint8_t *SrcPtr = pSrc->RowPointers[src_y + i];
		uint8_t *DstPtr = pDst->RowPointers[y + i];
		memcpy(DstPtr + DstOffset, SrcPtr + SrcOffset, CopyCount);
	}
}

void ImgBuffer_Destroy(ImgBuffer_p pBuf)
{
	if (!pBuf) return;

	free(pBuf->RowPointers);
	if (pBuf->OwnBuffer) free(pBuf->Buffer);
	free(pBuf);
}


uint32_t ARGB(int32_t R, int32_t G, int32_t B, int32_t A)
{
	union Color
	{
		uint8_t u8[4];
		uint32_t u32;
	}Color = {B, G, R, A};
	return Color.u32;
}

uint32_t ARGBSafe(int32_t R, int32_t G, int32_t B, int32_t A)
{
	if (R > 255) R = 255; else if (R < 0) R = 0;
	if (G > 255) G = 255; else if (G < 0) G = 0;
	if (B > 255) B = 255; else if (B < 0) B = 0;
	if (A > 255) A = 255; else if (A < 0) A = 0;
	return ARGB(R, G, B, A);
}

uint32_t ARGB_Lerp(uint32_t c1, uint32_t c2, float s)
{
	int is;
	union Color
	{
		uint8_t u8[4];
		uint32_t u32;
	}Color1, Color2, ColorRet;
	if (s < 0) s = 0; else if (s > 1) s = 1;
	is = (int)(255 * s);
	Color1.u32 = c1;
	Color2.u32 = c2;
	ColorRet.u8[0] = (uint8_t)((int)Color1.u8[0] + ((int)Color2.u8[0] - Color1.u8[0]) * is / 255);
	ColorRet.u8[1] = (uint8_t)((int)Color1.u8[1] + ((int)Color2.u8[1] - Color1.u8[1]) * is / 255);
	ColorRet.u8[2] = (uint8_t)((int)Color1.u8[2] + ((int)Color2.u8[2] - Color1.u8[2]) * is / 255);
	ColorRet.u8[3] = (uint8_t)((int)Color1.u8[3] + ((int)Color2.u8[3] - Color1.u8[3]) * is / 255);
	return ColorRet.u32;
}
