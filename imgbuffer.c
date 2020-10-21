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
	uint8_t* Buffer;
	ImgBuffer_p pBuf = NULL;

	if (!Width || !Height || !UnitSize) return 0;
	if (!RowAlign) RowAlign = 4;

	pBuf = malloc(sizeof * pBuf);
	if (!pBuf) return NULL;
	memset(pBuf, 0, sizeof * pBuf);
	pBuf->Width = Width;
	pBuf->Height = Height;
	pBuf->UnitSize = UnitSize;
	pBuf->RowAlign = RowAlign;

	pBuf->Pitch = CalcAlign(Width * UnitSize, RowAlign);
	pBuf->BufferSize = pBuf->Pitch * Height;

	pBuf->Buffer = malloc(pBuf->BufferSize);
	if (!pBuf->Buffer) goto FailExit;
	pBuf->RowPointers = malloc(sizeof pBuf->RowPointers[0] * Height);
	if (!pBuf->RowPointers) goto FailExit;

	Buffer = pBuf->Buffer;
	for (i = 0; i < Height; i++)
	{
		pBuf->RowPointers[i] = &Buffer[i * pBuf->Pitch];
	}
	memset(pBuf->Buffer, 0, pBuf->BufferSize);

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
	pBuf->UnitSize = 4;
	pBuf->RowAlign = 4;
	pBuf->Pitch = (size_t)ppUB[0]->Width * 4;
	pBuf->BufferSize = pBuf->Pitch * pBuf->Height;
	pBuf->Buffer = ppUB[0]->BitmapData;
	pBuf->RowPointers = ppUB[0]->RowPointers;

	free(ppUB[0]);
	ppUB[0] = NULL;
	return pBuf;
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
	free(pBuf->Buffer);
	free(pBuf);
}
