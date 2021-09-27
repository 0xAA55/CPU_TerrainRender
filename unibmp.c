#include"unibmp.h"

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#pragma pack(push, 1)

// 位图文件头
typedef struct BitmapFileHeader_struct
{
	uint16_t bfType;
	uint32_t bfSize;
	uint16_t bfReserved1;
	uint16_t bfReserved2;
	uint32_t bfOffbits;
}BitmapFileHeader_t, *BitmapFileHeader_p;

// 位图信息头
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
}BitmapInfoHeader_t, *BitmapInfoHeader_p;

#pragma pack(pop)

enum BitmapCompression
{
	BI_RGB = 0,
	BI_RLE8 = 1,
	BI_RLE4 = 2,
	BI_Bitfields = 3
};

// 取得位域位置
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

// 取得位域位数
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

// 生成颜色值
static uint32_t ARGB(uint8_t R, uint8_t G, uint8_t B, uint8_t A)
{
	union Pixel
	{
		uint8_t u8[4];
		uint32_t u32;
	}Block = { B, G, R, A};
	return Block.u32;
}

// 从文件创建位图
// 位图不可以是RLE压缩，但位图可以是带位域的位图、带调色板的索引颜色位图。
// 被读入后的图像数据会被强制转换为：ARGB 格式，每通道 8 bit 位深，每个像素4字节，分别是：蓝，绿，红，Alpha
// 如果整个图像的Alpha通道皆为0（或者整个图像不包含Alpha通道）则读出来的位图的Alpha通道会被设置为最大值（即 255）
UniformBitmap_p UB_CreateFromFile(const char *FilePath)
{
	BitmapFileHeader_t BMFH;
	BitmapInfoHeader_t BMIF;
	UniformBitmap_p UB = NULL;
	size_t Pitch; // 原位图文件每行像素的总字节数（包含对齐）
	uint32_t Palette[256]; // 调色板，如果有，要读入
	unsigned PaletteColorCount = 0;
	uint32_t Bitfields[3]; // 位域，如果有，要读入
	uint32_t Bitfield_A = 0; // 透明通道的位域，通常没有透明通道。
	uint8_t *ReadInLineBuffer = NULL;
	size_t i;

	// 准备读取文件
	FILE *fp = fopen(FilePath, "rb");
	if (!fp)
	{
		fprintf(stderr, "打不开文件：%s （%s）\n",
			FilePath, strerror(errno));
		return NULL;
	}

	// 读取位图文件头
	if (fread(&BMFH, sizeof BMFH, 1, fp) != 1)
	{
		fprintf(stderr, "读取位图文件 %s 的位图文件头失败：%s\n",
			FilePath, strerror(errno));
		goto FailExit;
	}

	// 判断头部字段是否合理
	if (BMFH.bfType != 0x4D42 || !BMFH.bfOffbits)
	{
		fprintf(stderr, "这大概不是一个BMP位图文件。");
		goto FailExit;
	}

	// 读取位图信息头
	if (fread(&BMIF, sizeof BMIF, 1, fp) != 1)
	{
		fprintf(stderr, "读取位图文件 %s 的位图信息头失败：%s\n",
			FilePath, strerror(errno));
		goto FailExit;
	}

	if (!BMIF.biPlanes || !BMIF.biWidth || !BMIF.biHeight)
	{
		fprintf(stderr, "位图文件 %s 的位图信息头的内容不合理。\n", FilePath);
		goto FailExit;
	}

	// 判断位图的压缩方式
	switch (BMIF.biCompression)
	{
	case BI_Bitfields:
		// 有位域，读取位域信息
		if (fread(&Bitfields, sizeof Bitfields, 1, fp) != 1)
		{
			fprintf(stderr, "读取位图文件 %s 的位域信息失败：%s\n",
				FilePath, strerror(errno));
			goto FailExit;
		}
		break;
	case BI_RLE4:
	case BI_RLE8:
		fprintf(stderr, "位图文件 %s 使用了RLE压缩。暂不支持RLE压缩的位图，懒得弄。\n", FilePath);
		goto FailExit;
	case BI_RGB:
		// 没有位域，但是有调色板，读取调色板信息
		switch (BMIF.biBitCount)
		{
		case 1: case 2: case 4: case 8:
			if (BMIF.biClrUsed) PaletteColorCount = BMIF.biClrUsed;
			else PaletteColorCount = (1u << BMIF.biBitCount);
			if (fread(&Palette, 4, PaletteColorCount, fp) != PaletteColorCount)
			{
				fprintf(stderr, "读取位图文件 %s 的调色板信息失败：%s\n",
					FilePath, strerror(errno));
				goto FailExit;
			}
			// 检测调色板的Alpha通道是否包含数据
			for (i = 0; i < PaletteColorCount; i++)
			{
				if (Palette[i] & 0xff000000) break;
			}
			// Alpha通道不包含数据时，将Alpha通道设置为255
			if (i == PaletteColorCount)
			{
				for (i = 0; i < PaletteColorCount; i++)
				{
					Palette[i] |= 0xff000000;
				}
			}
			break;
		case 16:
			Bitfields[0] = 0x7c00;
			Bitfields[1] = 0x03e0;
			Bitfields[2] = 0x001f;
			Bitfield_A = 0x8000;
			break;
		case 24:
		case 32:
			Bitfields[0] = 0xff0000;
			Bitfields[1] = 0x00ff00;
			Bitfields[2] = 0x0000ff;
			break;
		default:
			fprintf(stderr, "位图文件 %s 的位图信息头的内容不合理：颜色位数是 %d。\n", FilePath, (int)BMIF.biBitCount);
			goto FailExit;
		}
		break;
	default:
		fprintf(stderr, "位图文件 %s 的位图信息头的内容不合理。\n", FilePath);
		goto FailExit;
	}

	// 设置文件指针指向位图数据
	fseek(fp, BMFH.bfOffbits, SEEK_SET);

	// 分配内存，存储解析的图像的信息
	UB = malloc(sizeof UB[0]);
	if (!UB)
	{
		perror("不应该呀");
		goto FailExit;
	}

	UB->Width = BMIF.biWidth;
	UB->Height = BMIF.biHeight < 0 ? -BMIF.biHeight : BMIF.biHeight;

	// 保留DPI信息
	UB->XPelsPerMeter = BMIF.biXPelsPerMeter;
	UB->YPelsPerMeter = BMIF.biYPelsPerMeter;

	// 位图数据Buffer
	UB->BitmapData = malloc((size_t)UB->Width * UB->Height * sizeof UB->BitmapData[0]);
	if (!UB->BitmapData)
	{
		perror("分配内存用于存储位图数据");
		goto FailExit;
	}
	UB->RowPointers = malloc(UB->Height * sizeof UB->RowPointers[0]);
	if (!UB->RowPointers)
	{
		perror("分配内存用于存储位图数据行指针");
		goto FailExit;
	}
	// 位图的存储既可以是正常行序也可以是颠倒行序
	if (BMIF.biHeight < 0)
	{
		for (i = 0; i < UB->Height; i++)
		{
			UB->RowPointers[i] = &UB->BitmapData[i * UB->Width];
		}
	}
	else
	{
		for (i = 0; i < UB->Height; i++)
		{
			UB->RowPointers[i] = &UB->BitmapData[(UB->Height - i - 1) * UB->Width];
		}
	}

	Pitch = ((size_t)(BMIF.biWidth * BMIF.biBitCount - 1) / 32 + 1) * 4;
	ReadInLineBuffer = malloc(Pitch);
	if (!ReadInLineBuffer)
	{
		perror("分配内存用于按行读取源位图行");
		goto FailExit;
	}

	// 开始读取位图
	switch (BMIF.biCompression)
	{
		// 原始位图不包含位域信息
	case BI_RGB:
	{
		// 根据位数判断是否为调色板颜色
		switch (BMIF.biBitCount)
		{
			// 每个字节可能包含多个像素
		case 1: case 2: case 4:
		{
			uint32_t x, y;
			uint32_t ShiftCount = 8 - BMIF.biBitCount;
			for (y = 0; y < UB->Height; y++)
			{
				uint32_t PixelsPerBytes = 8 / BMIF.biBitCount;
				uint8_t LastByte;
				uint32_t BytePosition = 0;
				uint32_t PalIndex;
				uint32_t PixelsRemainLastByte = 0;
				uint32_t *Row = UB->RowPointers[y];
				if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
				{
					perror("未读取完整位图行");
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
		// 一个字节一个像素，字节值即为像素索引
		case 8:
		{
			uint32_t x, y;
			for (y = 0; y < UB->Height; y++)
			{
				uint32_t *Row = UB->RowPointers[y];
				if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
				{
					perror("未读取完整位图行");
					goto FailExit;
				}
				for (x = 0; x < UB->Width; x++)
				{
					Row[x] = Palette[ReadInLineBuffer[x]];
				}
			}
			break;
		}
		// 非索引颜色，每16个bit按照从高到低 1:5:5:5 存储 ARGB 四个通道
		case 16:
		{
			uint32_t x, y;
			int HasAlpha = 0;
			for (y = 0; y < UB->Height; y++)
			{
				uint32_t *Row = UB->RowPointers[y];
				if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
				{
					perror("未读取完整位图行");
					goto FailExit;
				}
				for (x = 0; x < UB->Width; x++)
				{
					uint32_t PixelData = *(uint16_t *)ReadInLineBuffer[x * 2];
					uint32_t R5 = (PixelData & 0x7c00) >> 10;
					uint32_t G5 = (PixelData & 0x03e0) >> 5;
					uint32_t B5 = (PixelData & 0x001f) >> 0;

					Row[x] = ARGB(
						(R5 << 3) | (R5 >> 2),
						(G5 << 3) | (G5 >> 2),
						(B5 << 3) | (B5 >> 2),
						PixelData & 0x8000 ? (HasAlpha = 1) * 255 : 0);
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
		// 非索引颜色，每通道1字节
		case 24:
		{
			uint32_t x, y;
			for (y = 0; y < UB->Height; y++)
			{
				uint32_t *Row = UB->RowPointers[y];
				if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
				{
					perror("未读取完整位图行");
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
		// 非索引颜色，每通道1字节，可能包含Alpha通道
		case 32:
		{
			uint32_t x, y;
			int HasAlpha = 0;
			for (y = 0; y < UB->Height; y++)
			{
				uint32_t *Row = UB->RowPointers[y];
				if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
				{
					perror("未读取完整位图行");
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
			fprintf(stderr, "包含位域信息的位图的每像素位数应当超过8。\n");
			goto FailExit;
		}

		for (y = 0; y < UB->Height; y++)
		{
			uint32_t *Row = UB->RowPointers[y];
			uint8_t *PixelPointer = ReadInLineBuffer;
			if (fread(ReadInLineBuffer, 1, Pitch, fp) != Pitch)
			{
				perror("未读取完整位图行");
				goto FailExit;
			}
			for (x = 0; x < UB->Width; x++)
			{
				// 将一个像素的全部数据读入，再拆出其红绿蓝各个通道的颜色值
				uint32_t PixelData = *(uint32_t *)PixelPointer;
				uint32_t RV = (Bitfields[0]) >> RShift;
				uint32_t GV = (Bitfields[1]) >> GShift;
				uint32_t BV = (Bitfields[2]) >> BShift;
				uint32_t AV = (Bitfield_A) >> AShift;
				uint32_t RBC = RBitCount;
				uint32_t GBC = GBitCount;
				uint32_t BBC = BBitCount;
				uint32_t ABC = ABitCount;
				PixelPointer += BytesPerPixels;

				if (RBC) while (RBC < 8) { RV |= RV >> RBC; RBC *= 2; } else RV = 255;
				if (GBC) while (GBC < 8) { GV |= GV >> GBC; GBC *= 2; } else GV = 255;
				if (BBC) while (BBC < 8) { BV |= BV >> BBC; BBC *= 2; } else BV = 255;
				if (ABC) while (ABC < 8) { AV |= AV >> ABC; ABC *= 2; } else AV = 255;

				Row[x] = ARGB(RBC, GBC, BBC, ABC);
			}
		}
		break;
	}

	default:
		fprintf(stderr, "既非位域格式又非RGB格式，停止解析。\n");
		goto FailExit;
	}

	fclose(fp);
	free(ReadInLineBuffer);

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
	BitmapFileHeader_t BMFH;
	BitmapInfoHeader_t BMIF;

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
		} * RowPtr = (void*)UB->RowPointers[UB->Height - 1 - y];
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
	BitmapFileHeader_t BMFH;
	BitmapInfoHeader_t BMIF;

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
		uint32_t *RowPtr = UB->RowPointers[UB->Height - 1 - y];
		if (!fwrite(RowPtr, Pitch, 1, fp)) goto FailExit;
	}

	fclose(fp);
	return 1;
FailExit:
	if (fp) fclose(fp);
	return 0;
}

// 释放位图资源
void UB_Free(UniformBitmap_p *pUB)
{
	if (pUB && *pUB)
	{
		UniformBitmap_p UB = *pUB;
		*pUB = NULL;

		free(UB->BitmapData);
		free(UB->RowPointers);
		free(UB);
	}
}
