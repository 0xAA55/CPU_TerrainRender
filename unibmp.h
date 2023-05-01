#ifndef _UNIBMP_H_
#define _UNIBMP_H_ 1

#include <stdint.h>
#include <stdio.h>

typedef struct UniformBitmap_struct
{
	// Size in pixels
	uint32_t Width;
	uint32_t Height;

	// DPI info
	uint32_t XPelsPerMeter;
	uint32_t YPelsPerMeter;

	// Bitmap in top-to-bottom row order
	uint32_t *BitmapData;

	// Row pointers of the bitmap
	uint32_t **RowPointers;
}UniformBitmap_t, *UniformBitmap_p;

// Create
UniformBitmap_p UB_CreateNew(uint32_t Width, uint32_t Height);

// Load. The alpha channel will be set to 255 if the bitmap file don't contains it.
UniformBitmap_p UB_CreateFromFile(const char *FilePath, FILE *log_fp);

// Copy.
UniformBitmap_p UB_CreateFromCopy(UniformBitmap_p Source);

int UB_SaveToFile_24(UniformBitmap_p UB, const char *FilePath);
int UB_SaveToFile_32(UniformBitmap_p UB, const char *FilePath);

void UB_Free(UniformBitmap_p *pUB);

#pragma pack(push, 1)

typedef struct BitmapFileHeader_struct
{
	uint16_t bfType;
	uint32_t bfSize;
	uint16_t bfReserved1;
	uint16_t bfReserved2;
	uint32_t bfOffbits;
}BitmapFileHeader_t, * BitmapFileHeader_p;

typedef struct BitmapInfoHeader_struct
{
	uint32_t biSize;
	int32_t biWidth;
	int32_t biHeight;
	uint16_t biPlanes;
	uint16_t biBitCount;
	uint32_t biCompression;
	uint32_t biSizeImage;
	uint32_t biXPelsPerMeter;
	uint32_t biYPelsPerMeter;
	uint32_t biClrUsed;
	uint32_t biClrImportant;
}BitmapInfoHeader_t, * BitmapInfoHeader_p;

#pragma pack(pop)

enum BitmapCompression
{
	BI_RGB = 0,
	BI_RLE8 = 1,
	BI_RLE4 = 2,
	BI_Bitfields = 3
};

#endif
