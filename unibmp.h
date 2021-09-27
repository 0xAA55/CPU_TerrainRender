#ifndef _UNIBMP_H_
#define _UNIBMP_H_ 1

#include <stdint.h>

// 读入到内存的位图文件数据
// 所有的位图格式在被读进来后，都会被转换为 ARGB 格式（每通道 8 bit 位深）
// UniformBitmap意为“格式统一了的位图”
typedef struct UniformBitmap_struct
{
	// 位图信息
	uint32_t Width;
	uint32_t Height;

	// 位图DPI信息
	uint32_t XPelsPerMeter;
	uint32_t YPelsPerMeter;

	// 位图数据
	uint32_t *BitmapData;

	// 位图数据的行指针
	uint32_t **RowPointers;
}UniformBitmap_t, *UniformBitmap_p;

// 从文件创建位图
// 位图不可以是RLE压缩，但位图可以是带位域的位图、带调色板的索引颜色位图。
// 被读入后的图像数据会被强制转换为：ARGB 格式，每通道 8 bit 位深，每个像素4字节，分别是：蓝，绿，红，Alpha
// 如果整个图像的Alpha通道皆为0（或者整个图像不包含Alpha通道）则读出来的位图的Alpha通道会被设置为最大值（即 255）
UniformBitmap_p UB_CreateFromFile(const char *FilePath);

int UB_SaveToFile_24(UniformBitmap_p UB, const char *FilePath);
int UB_SaveToFile_32(UniformBitmap_p UB, const char *FilePath);

// 释放位图资源
void UB_Free(UniformBitmap_p *pUB);

#endif
