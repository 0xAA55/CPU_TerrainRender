#include"unibmp.h"

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>

#include"bunchalloc.h"

static uint32_t GetBitfieldShift(uint32_t Bitfield)
{
	uint32_t Shift = 0;
	if (!Bitfield) return 0;
	while (!(Bitfield & 1))
	{
		Shift++;
		Bitfield >>= 1;
	}
	while (Bitfield & 1)
	{
		Shift++;
		Bitfield >>= 1;
	}
	return Shift - 8;
}

static uint32_t GetBitfieldBitCount(uint32_t Bitfield)
{
	uint32_t Count = 0;
	if (!Bitfield) return 0;
	while (!(Bitfield & 1)) Bitfield >>= 1;
	while (Bitfield & 1)
	{
		Count++;
		Bitfield >>= 1;
	}
	return Count;
}

static uint32_t ARGB(uint8_t R, uint8_t G, uint8_t B, uint8_t A)
{
	union Pixel
	{
		uint8_t u8[4];
		uint32_t u32;
	}Block = {{ B, G, R, A}};
	return Block.u32;
}

static void UB_SetupRowPointers(UniformBitmap_p UB, int is_bottom_to_top)
{
	size_t i;
	if (is_bottom_to_top)
	{
		uint32_t MaxY = UB->Height - 1;
		for (i = 0; i < UB->Height; i++)
		{
			UB->RowPointers[i] = &UB->BitmapData[(MaxY - i) * UB->Width];
		}
	}
	else
	{
		for (i = 0; i < UB->Height; i++)
		{
			UB->RowPointers[i] = &UB->BitmapData[i * UB->Width];
		}
	}
}

// Create new blank UniformBitmap
UniformBitmap_p UB_CreateNew(uint32_t Width, uint32_t Height)
{
	UniformBitmap_p UB = NULL;

	// UB = calloc(1, sizeof * UB);
	UB = bunchalloc(16, sizeof UB[0],
		offsetof(UniformBitmap_t, RowPointers), (size_t)Height * sizeof UB->RowPointers[0],
		offsetof(UniformBitmap_t, BitmapData), (size_t)Width * Height * sizeof UB->BitmapData[0],
		0, 0);
	if (!UB) return NULL;

	UB->Width = Width;
	UB->Height = Height;

	// UB->BitmapData = calloc((size_t)Width * Height, sizeof UB->BitmapData[0]);
	// UB->RowPointers = malloc(Height * sizeof UB->RowPointers[0]);
	// if (!UB->BitmapData || !UB->RowPointers) goto FailExit;

	UB_SetupRowPointers(UB, 0);
	return UB;
}

// Create UniformBitmap from a `bmp` file.
// The `bmp` file shouldn't be RLE compressed. Currently not implemented.
// Indexed-color bitmap and bitmap with bitfields are supported.
// All bitmap loaded will be converted to:
// - ARGB format
// - Each channel is 8-bit, thus each pixel is 32-bit, from low to high is: blue, green, red, alpha(255)
// If the whole bitmap is loaded and all of the pixels don't contain alpha information (as all alpha value is 0),
// the alpha value will be set to 255 for every pixel.
UniformBitmap_p UB_CreateFromFile(const char *FilePath, FILE *log_fp)
{
	BitmapFileHeader_t BMFH;
	BitmapInfoHeader_t BMIF;
	UniformBitmap_p UB = NULL;
	size_t Pitch; // Bytes per line of pixels
	uint32_t Palette[256]; // If the `bmp` contains a palette, will be loaded here.
	unsigned PaletteColorCount = 0; // How many colors in the palette.
	uint32_t Bitfields[3]; // If the `bmp` contains a bitfields, will be loaded here.
	uint32_t Bitfield_A = 0; // The bitfield for the alpha channel. Normally, there are no alpha channel bitfields.
	uint8_t *ReadInLineBuffer = NULL;
	size_t i;
	uint32_t Width;
	uint32_t Height;

	// Open file for read
	FILE *fp = fopen(FilePath, "rb");
	if (!fp)
	{
		fprintf(log_fp, "Could not open %s (%s)\n", FilePath, strerror(errno));
		return NULL;
	}

	// Read the file header
	if (fread(&BMFH, sizeof BMFH, 1, fp) != 1)
	{
		fprintf(log_fp, "Read bitmap file header failed. %s (%s)\n", FilePath, strerror(errno));
		goto FailExit;
	}

	// Check if the fields of the header is good
	if (BMFH.bfType != 0x4D42 || !BMFH.bfOffbits)
	{
		fprintf(log_fp, "Load bitmap file failed: this is probably not a `bmp` file. %s\n", FilePath);
		goto FailExit;
	}

	// Read the info header
	if (fread(&BMIF, sizeof BMIF, 1, fp) != 1)
	{
		fprintf(log_fp, "Read bitmap info header failed. %s (%s)\n", FilePath, strerror(errno));
		goto FailExit;
	}

	// Check if the fields of the info header is good
	if (!BMIF.biPlanes || !BMIF.biWidth || !BMIF.biHeight)
	{
		fprintf(log_fp, "Load bitmap file failed: corrupted bitmap info header. %s\n", FilePath);
		goto FailExit;
	}

	// Check the compression method of the bitmap file
	switch (BMIF.biCompression)
	{
	case BI_Bitfields:
		// If it has bitfields, read it for processing.
		if (fread(&Bitfields, sizeof Bitfields, 1, fp) != 1)
		{
			fprintf(log_fp, "Read bitmap file bitfield failed. %s (%s)\n", FilePath, strerror(errno));
			goto FailExit;
		}
		break;
	case BI_RLE4:
	case BI_RLE8:
		fprintf(log_fp, "The bitmap file is RLE-compressed, decompression is not implemented. %s\n", FilePath);
		goto FailExit;
	case BI_RGB:
		switch (BMIF.biBitCount)
		{
		// No bitfields, but palette, read the palette.
		case 1: case 2: case 4: case 8:
			if (BMIF.biClrUsed) PaletteColorCount = BMIF.biClrUsed;
			else PaletteColorCount = (1u << BMIF.biBitCount);
			if (fread(&Palette, 4, PaletteColorCount, fp) != PaletteColorCount)
			{
				fprintf(log_fp, "Read bitmap palette failed. %s (%s)\n", FilePath, strerror(errno));
				goto FailExit;
			}
			// Check if the palette contains alpha data (normally no alpha data should be here)
			for (i = 0; i < PaletteColorCount; i++)
			{
				if (Palette[i] & 0xff000000) break;
			}
			// If no alpha data here, the alpha is set to 255.
			if (i == PaletteColorCount)
			{
				for (i = 0; i < PaletteColorCount; i++)
				{
					Palette[i] |= 0xff000000;
				}
			}
			break;
		case 16:
			// The general bitfields for the 16-bit bitmap.
			Bitfields[0] = 0x7c00;
			Bitfields[1] = 0x03e0;
			Bitfields[2] = 0x001f;
			Bitfield_A = 0x8000;
			break;
		case 24:
		case 32:
			// The general bitfields for the 24-bit or 32-bit bitmap.
			Bitfields[0] = 0xff0000;
			Bitfields[1] = 0x00ff00;
			Bitfields[2] = 0x0000ff;
			break;
		default:
			fprintf(log_fp, "Load bitmap file failed: Unknown bit count `%d`. %s\n", (int)BMIF.biBitCount, FilePath);
			goto FailExit;
		}
		break;
	default:
		fprintf(log_fp, "Load bitmap file failed: Unknown compression method `%u`. %s\n", (int)BMIF.biCompression, FilePath);
		goto FailExit;
	}

	// Seek to the bitmap data
	fseek(fp, BMFH.bfOffbits, SEEK_SET);

	Width = BMIF.biWidth;
	Height = BMIF.biHeight < 0 ? -BMIF.biHeight : BMIF.biHeight;

	// UB = malloc(sizeof UB[0]);
	UB = bunchalloc(16, sizeof UB[0],
		offsetof(UniformBitmap_t, RowPointers), (size_t)Height * sizeof UB->RowPointers[0],
		offsetof(UniformBitmap_t, BitmapData), (size_t)Width * Height * sizeof UB->BitmapData[0],
		0, 0);
	if (!UB)
	{
		perror("Allocate memory for UniformBitmap");
		goto FailExit;
	}

	UB->Width = Width;
	UB->Height = Height;

	UB->XPelsPerMeter = BMIF.biXPelsPerMeter;
	UB->YPelsPerMeter = BMIF.biYPelsPerMeter;

	// UB->BitmapData = malloc((size_t)UB->Width * UB->Height * sizeof UB->BitmapData[0]);
	// if (!UB->BitmapData)
	// {
	// 	perror("Allocate memory for bitmap");
	// 	goto FailExit;
	// }
	// UB->RowPointers = malloc(UB->Height * sizeof UB->RowPointers[0]);
	// if (!UB->RowPointers)
	// {
	// 	perror("Allocate memory for bitmap row pointers");
	// 	goto FailExit;
	// }
	// Check the row order of the bitmap file, unify it to top-bottom
	if (BMIF.biHeight < 0)
	{
		UB_SetupRowPointers(UB, 0);
	}
	else
	{
		UB_SetupRowPointers(UB, 1);
	}

	Pitch = ((size_t)(BMIF.biWidth * BMIF.biBitCount - 1) / 32 + 1) * 4;
	ReadInLineBuffer = malloc(Pitch);
	if (!ReadInLineBuffer)
	{
		perror("Allocate memory for read bitmap by rows");
		goto FailExit;
	}

	// Start read the bitmap
	switch (BMIF.biCompression)
	{
		// If no bitfields
	case BI_RGB:
	{
		// Check if it has a palette
		switch (BMIF.biBitCount)
		{
			// Every byte may contain more than 1 pixel.
		case 1: case 2: case 4:
		{
			uint32_t x, y;
			uint32_t ShiftCount = 8 - BMIF.biBitCount;
			for (y = 0; y < UB->Height; y++)
			{
				uint32_t PixelsPerBytes = 8 / BMIF.biBitCount;
				uint8_t LastByte = 0;
				uint32_t BytePosition = 0;
				uint32_t PalIndex;
				uint32_t PixelsRemainLastByte = 0;
				uint32_t *Row = UB->RowPointers[y];
				if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
				{
					perror("Could not read a complete row of the bitmap");
					goto FailExit;
				}
				for (x = 0; x < UB->Width; x++)
				{
					if (!PixelsRemainLastByte)
					{
						PixelsRemainLastByte = PixelsPerBytes;
						LastByte = ReadInLineBuffer[BytePosition++];
					}
					PalIndex = LastByte >> ShiftCount;
					LastByte <<= BMIF.biBitCount;
					PixelsRemainLastByte--;
					Row[x] = Palette[PalIndex];
				}
			}
			break;
		}
		// Every byte is a pixel, the byte value is the index of the palette
		case 8:
		{
			uint32_t x, y;
			for (y = 0; y < UB->Height; y++)
			{
				uint32_t *Row = UB->RowPointers[y];
				if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
				{
					perror("Could not read a complete row of the bitmap");
					goto FailExit;
				}
				for (x = 0; x < UB->Width; x++)
				{
					Row[x] = Palette[ReadInLineBuffer[x]];
				}
			}
			break;
		}
		// Non-palette color, every 16-bit is a pixel, the bitfields for ARGB is 1:5:5:5
		case 16:
		{
			uint32_t x, y;
			int HasAlpha = 0;
			for (y = 0; y < UB->Height; y++)
			{
				uint32_t *Row = UB->RowPointers[y];
				if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
				{
					perror("Could not read a complete row of the bitmap");
					goto FailExit;
				}
				for (x = 0; x < UB->Width; x++)
				{
					uint32_t PixelData = ((uint16_t *)ReadInLineBuffer)[x * 2];
					uint32_t R5 = (PixelData & 0x7c00) >> 10;
					uint32_t G5 = (PixelData & 0x03e0) >> 5;
					uint32_t B5 = (PixelData & 0x001f) >> 0;

					Row[x] = ARGB(
						(uint8_t)((R5 << 3) | (R5 >> 2)),
						(uint8_t)((G5 << 3) | (G5 >> 2)),
						(uint8_t)((B5 << 3) | (B5 >> 2)),
						(uint8_t)(PixelData & 0x8000 ? (HasAlpha = 1) * 255 : 0));
				}
			}
			if (!HasAlpha)
			{
				for (y = 0; y < UB->Height; y++)
				{
					uint32_t *Row = UB->RowPointers[y];
					for (x = 0; x < UB->Width; x++)
					{
						Row[x] |= 0xff000000;
					}
				}
			}
			break;
		}
		// Non-palette color, every byte is a color channel for the pixel
		case 24:
		{
			uint32_t x, y;
			for (y = 0; y < UB->Height; y++)
			{
				uint32_t *Row = UB->RowPointers[y];
				if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
				{
					perror("Could not read a complete row of the bitmap");
					goto FailExit;
				}
				for (x = 0; x < UB->Width; x++)
				{
					uint32_t ByteIndex = x * 3;
					Row[x] = ARGB(ReadInLineBuffer[ByteIndex + 2],
						ReadInLineBuffer[ByteIndex + 1],
						ReadInLineBuffer[ByteIndex + 0],
						255);
				}
			}
			break;
		}
		// Non-palette color, every byte is a color channel for the pixel, may contain alpha
		case 32:
		{
			uint32_t x, y;
			int HasAlpha = 0;
			for (y = 0; y < UB->Height; y++)
			{
				uint32_t *Row = UB->RowPointers[y];
				if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
				{
					perror("Could not read a complete row of the bitmap");
					goto FailExit;
				}
				for (x = 0; x < UB->Width; x++)
				{
					uint32_t ByteIndex = x * 4;
					Row[x] = ARGB(ReadInLineBuffer[ByteIndex + 2],
						ReadInLineBuffer[ByteIndex + 1],
						ReadInLineBuffer[ByteIndex + 0],
						ReadInLineBuffer[ByteIndex + 3]);
					if (ReadInLineBuffer[ByteIndex + 3]) HasAlpha = 1;
				}
			}
			if (!HasAlpha)
			{
				for (y = 0; y < UB->Height; y++)
				{
					uint32_t *Row = UB->RowPointers[y];
					for (x = 0; x < UB->Width; x++)
					{
						Row[x] &= 0xFF000000;
					}
				}
			}
			break;
		}
		}
		break;
	}

	case BI_Bitfields:
	{
		uint32_t RShift = GetBitfieldShift(Bitfields[0]);
		uint32_t GShift = GetBitfieldShift(Bitfields[1]);
		uint32_t BShift = GetBitfieldShift(Bitfields[2]);
		uint32_t AShift = GetBitfieldShift(Bitfield_A);
		uint32_t RBitCount = GetBitfieldBitCount(Bitfields[0]);
		uint32_t GBitCount = GetBitfieldBitCount(Bitfields[1]);
		uint32_t BBitCount = GetBitfieldBitCount(Bitfields[2]);
		uint32_t ABitCount = GetBitfieldBitCount(Bitfield_A);
		uint32_t BytesPerPixels = BMIF.biBitCount / 8;
		uint32_t x, y;

		if (!BytesPerPixels)
		{
			fprintf(log_fp, "A bitmap file with bit count less than 8 should not have bitfields. %s\n", FilePath);
			goto FailExit;
		}

		for (y = 0; y < UB->Height; y++)
		{
			uint32_t *Row = UB->RowPointers[y];
			uint8_t *PixelPointer = ReadInLineBuffer;
			if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
			{
				perror("Could not read a complete row of the bitmap");
				goto FailExit;
			}
			for (x = 0; x < UB->Width; x++)
			{
				// Read a whole pixel bits into a `uint32_t`, then try to dismantle it.
				uint32_t PixelData = *(uint32_t *)PixelPointer;
				uint32_t RV = PixelData & ((Bitfields[0]) >> RShift);
				uint32_t GV = PixelData & ((Bitfields[1]) >> GShift);
				uint32_t BV = PixelData & ((Bitfields[2]) >> BShift);
				uint32_t AV = PixelData & ((Bitfield_A) >> AShift);
				uint32_t RBC = RBitCount;
				uint32_t GBC = GBitCount;
				uint32_t BBC = BBitCount;
				uint32_t ABC = ABitCount;
				PixelPointer += BytesPerPixels;

				if (RBC) while (RBC < 8) { RV |= RV >> RBC; RBC *= 2; } else RV = 255;
				if (GBC) while (GBC < 8) { GV |= GV >> GBC; GBC *= 2; } else GV = 255;
				if (BBC) while (BBC < 8) { BV |= BV >> BBC; BBC *= 2; } else BV = 255;
				if (ABC) while (ABC < 8) { AV |= AV >> ABC; ABC *= 2; } else AV = 255;

				Row[x] = ARGB((uint8_t)RV, (uint8_t)GV, (uint8_t)BV, (uint8_t)AV);
			}
		}
		break;
	}

	default:
		fprintf(log_fp, "The bitmap file is neither bitfield encoded nor RGB encoded, could not be loaded there. %s\n", FilePath);
		goto FailExit;
	}

	fclose(fp);
	free(ReadInLineBuffer);

	// After a success read (the readed bitmap is top-to-bottom), the row pointers should also be set to top-to-bottom
	UB_SetupRowPointers(UB, 0);

	return UB;
FailExit:
	if (fp) fclose(fp);
	UB_Free(&UB);
	free(ReadInLineBuffer);
	return NULL;
}

int UB_SaveToFile_24(UniformBitmap_p UB, const char *FilePath)
{
	FILE *fp = NULL;
	size_t Pitch;
	uint8_t *Buffer = NULL;
	uint32_t x, y;
	BitmapFileHeader_t BMFH = {0};
	BitmapInfoHeader_t BMIF = {0};

	if (!UB || !FilePath) return 0;

	fp = fopen(FilePath, "wb");
	if (!fp) return 0;

	BMIF.biSize = 40;
	BMIF.biWidth = UB->Width;
	BMIF.biHeight = UB->Height;
	BMIF.biPlanes = 1;
	BMIF.biBitCount = 24;
	BMIF.biCompression = 0;
	BMIF.biSizeImage = 0;
	BMIF.biXPelsPerMeter = 0;
	BMIF.biYPelsPerMeter = 0;
	BMIF.biClrUsed = 0;
	BMIF.biClrImportant = 0;

	Pitch = ((size_t)(BMIF.biWidth * BMIF.biBitCount - 1) / 32 + 1) * 4;

	BMFH.bfType = 0x4D42;
	BMFH.bfSize = (uint32_t)(sizeof BMFH + sizeof BMIF + Pitch * UB->Height);
	BMFH.bfReserved1 = 0;
	BMFH.bfReserved2 = 0;
	BMFH.bfOffbits = sizeof BMFH + sizeof BMIF;

	if (!fwrite(&BMFH, sizeof BMFH, 1, fp)) goto FailExit;
	if (!fwrite(&BMIF, sizeof BMIF, 1, fp)) goto FailExit;

	Buffer = malloc(Pitch);
	if (!Buffer) goto FailExit;
	memset(Buffer, 0, Pitch);
	for (y = 0; y < UB->Height; y++)
	{
		union Pixel
		{
			uint8_t u8[4];
			uint32_t u32;
		} * RowPtr = (void*)UB->RowPointers[UB->Height - 1 - y]; // Store as bottom-to-top order
		uint8_t *Ptr = Buffer;
		for (x = 0; x < UB->Width; x++)
		{
			*Ptr++ = RowPtr[x].u8[0];
			*Ptr++ = RowPtr[x].u8[1];
			*Ptr++ = RowPtr[x].u8[2];
		}
		if (!fwrite(Buffer, Pitch, 1, fp)) goto FailExit;
	}

	free(Buffer);
	fclose(fp);
	return 1;
FailExit:
	free(Buffer);
	if (fp) fclose(fp);
	return 0;
}

int UB_SaveToFile_32(UniformBitmap_p UB, const char *FilePath)
{
	FILE *fp = NULL;
	size_t Pitch;
	uint32_t y;
	BitmapFileHeader_t BMFH = {0};
	BitmapInfoHeader_t BMIF = {0};

	if (!UB || !FilePath) return 0;

	fp = fopen(FilePath, "wb");
	if (!fp) return 0;

	BMIF.biSize = 40;
	BMIF.biWidth = UB->Width;
	BMIF.biHeight = UB->Height;
	BMIF.biPlanes = 1;
	BMIF.biBitCount = 32;
	BMIF.biCompression = 0;
	BMIF.biSizeImage = 0;
	BMIF.biXPelsPerMeter = 0;
	BMIF.biYPelsPerMeter = 0;
	BMIF.biClrUsed = 0;
	BMIF.biClrImportant = 0;

	Pitch = (size_t)BMIF.biWidth * 4;

	BMFH.bfType = 0x4D42;
	BMFH.bfSize = (uint32_t)(sizeof BMFH + sizeof BMIF + Pitch * UB->Height);
	BMFH.bfReserved1 = 0;
	BMFH.bfReserved2 = 0;
	BMFH.bfOffbits = sizeof BMFH + sizeof BMIF;

	if (!fwrite(&BMFH, sizeof BMFH, 1, fp)) goto FailExit;
	if (!fwrite(&BMIF, sizeof BMIF, 1, fp)) goto FailExit;

	for (y = 0; y < UB->Height; y++)
	{
		uint32_t *RowPtr = UB->RowPointers[UB->Height - 1 - y]; // Store as bottom-to-top order
		if (!fwrite(RowPtr, Pitch, 1, fp)) goto FailExit;
	}

	fclose(fp);
	return 1;
FailExit:
	if (fp) fclose(fp);
	return 0;
}

// 复制位图
UniformBitmap_p UB_CreateFromCopy(UniformBitmap_p Source)
{
	UniformBitmap_p NewUB;
	size_t BitmapSize;
	uint32_t Width, Height;

	if (!Source) return NULL;

	Width = Source->Width;
	Height = Source->Height;
	BitmapSize = (size_t)Width * Height * sizeof Source->BitmapData[0];

	NewUB = bunchalloc(16, sizeof NewUB[0],
		offsetof(UniformBitmap_t, RowPointers), (size_t)Height * sizeof NewUB->RowPointers[0],
		offsetof(UniformBitmap_t, BitmapData), BitmapSize,
		0, 0);
	if (!NewUB) return NULL;


	NewUB->Width = Source->Width;
	NewUB->Height = Source->Height;
	NewUB->XPelsPerMeter = Source->XPelsPerMeter;
	NewUB->YPelsPerMeter = Source->YPelsPerMeter;

	memcpy(NewUB->BitmapData, Source->BitmapData, BitmapSize);

	UB_SetupRowPointers(NewUB, 0);

	return NewUB;
}

// 释放位图资源
void UB_Free(UniformBitmap_p *pUB)
{
	if (pUB && *pUB)
	{
		UniformBitmap_p UB = *pUB;
		*pUB = NULL;

		bunchfree(UB);
	}
}
