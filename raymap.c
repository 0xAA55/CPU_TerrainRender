#include"raymap.h"
#include"logprintf.h"

#include<string.h>
#include<errno.h>

static int LoadAltitude(RayMap_p r, FILE *fp_log, char *path)
{
	uint32_t w, h;
	FILE *fp = NULL;
	ImgBuffer_p Altitude = NULL;

	fp = fopen(path, "r");
	if (!fp) goto FailExit;

	if (!fread(&w, sizeof w, 1, fp)) goto FailExit;
	if (!fread(&h, sizeof h, 1, fp)) goto FailExit;

	Altitude = ImgBuffer_Create(w, h, 4, 4);
	if (!Altitude) goto FailExit;

	if (!fread(Altitude->Buffer, Altitude->BufferSize, 1, fp)) goto FailExit;

	r->Altitude = Altitude;

	fclose(fp);
	return 1;
FailExit:
	log_printf(fp_log, "Could not load altitude file: '%s'\n", path);
	if (fp) fclose(fp);
	ImgBuffer_Destroy(Altitude);
	return 0;
}

static int LoadAltitudeFromRaw(RayMap_p r, FILE *fp_log, const char *RawAltitudeFile, const char *AltitudeFile, const int RawAltitudeBlur)
{
	ImgBuffer_p RawAltitude = NULL;
	ImgBuffer_p Altitude = NULL;
	FILE *fp = NULL;
	ptrdiff_t i, AllPix;

	RawAltitude = ImgBuffer_CreateFromBMPFile(RawAltitudeFile);
	if (!RawAltitude)
	{
		log_printf(fp_log, "Could not load raw altitude file: '%s'\n", RawAltitudeFile);
		goto FailExit;
	}

	Altitude = ImgBuffer_Create(RawAltitude->Width, RawAltitude->Height, 4, 4);
	if (!Altitude)
	{
		log_printf(fp_log, "No enough memory for creating altitude map (%u, %u).\n",
			RawAltitude->Width, RawAltitude->Height);
		goto FailExit;
	}

	r->Altitude = Altitude;

	AllPix = (ptrdiff_t)Altitude->Width * Altitude->Height;

#pragma omp parallel for
	for (i = 0; i < AllPix; i++)
	{
		size_t
			cur_x = (size_t)i % Altitude->Width,
			cur_y = (size_t)i / Altitude->Height;
		size_t count = 0;
		float sum = 0;
		ptrdiff_t sx, sy;
		for (sy = -RawAltitudeBlur; sy <= RawAltitudeBlur; sy++)
		{
			for (sx = -RawAltitudeBlur; sx <= RawAltitudeBlur; sx++)
			{
				ptrdiff_t
					x = sx + (ptrdiff_t)cur_x,
					y = sy + (ptrdiff_t)cur_y;
				union
				{
					uint8_t channel[4];
					uint32_t u32;
				}color;
				while (x < 0) x += Altitude->Width;
				while (y < 0) y += Altitude->Height;
				x %= Altitude->Width;
				y %= Altitude->Height;
				color.u32 = ImgBuffer_FetchU32(RawAltitude, x, y);
				sum +=
					(float)color.channel[0] / 255.0f +
					(float)color.channel[1] / 255.0f +
					(float)color.channel[2] / 255.0f;
				count++;
			}
		}
		ImgBuffer_FetchF32(Altitude, cur_x, cur_y) = sum;
	}

	fp = fopen(AltitudeFile, "wb");
	if (!fp ||
		!fwrite(&Altitude->Width, sizeof Altitude->Width, 1, fp) ||
		!fwrite(&Altitude->Height, sizeof Altitude->Height, 1, fp) ||
		!fwrite(Altitude->Buffer, Altitude->BufferSize, 1, fp))
	{
		log_printf(fp_log, "Could not write generated altitude file: '%s'\n", AltitudeFile);
		goto FailExit;
	}
	fclose(fp);
	ImgBuffer_Destroy(RawAltitude);
	return 1;
FailExit:
	if (fp) fclose(fp);
	ImgBuffer_Destroy(RawAltitude);
	ImgBuffer_Destroy(Altitude);
	r->Altitude = NULL;
	return 0;
}

static int LoadK(RayMap_p r, FILE *fp_log, const char *Path)
{
	uint32_t w, h;
	FILE *fp = NULL;
	ImgBuffer_p K = NULL;
	size_t rd;

	fp = fopen(Path, "rb");
	if (!fp) goto FailExit;

	if (!fread(&w, sizeof w, 1, fp)) goto FailExit;
	if (!fread(&h, sizeof h, 1, fp)) goto FailExit;

	if (w != r->Altitude->Width2N || h != r->Altitude->Height2N) return 0;

	K = ImgBuffer_Create(w, h, 4, 4);
	if (!K) goto FailExit;

	rd = fread(K->Buffer, 1, K->BufferSize, fp);
	if (rd != K->BufferSize) goto FailExit;

	r->K = K;

	fclose(fp);
	return 1;
FailExit:
	log_printf(fp_log, "Could not load K-map file: '%s'\n", Path);
	if (fp) fclose(fp);
	ImgBuffer_Destroy(K);
	return 0;
}

static int GenerateK(RayMap_p r, FILE *fp_log, const char *Path)
{
	uint32_t w, h;
	ImgBuffer_p Altitude = r->Altitude;
	ImgBuffer_p K1 = NULL, K2 = NULL;
	UniformBitmap_p SaveUB = NULL;
	ptrdiff_t i, AllPix;
	size_t calc_count = 0;
	FILE *fp = NULL;
	char StrBuf[1024];
	const int SearchRadius = 16;

	w = r->Altitude->Width;
	h = r->Altitude->Height;

	if (w >= 0x10000 || h >= 0x10000) return 0;

	K1 = ImgBuffer_Create(w, h, 4, 4);
	K2 = ImgBuffer_Create(w, h, 4, 4);
	if (!K1 || !K2) goto FailExit;

	AllPix = (ptrdiff_t)(w * h);

#pragma omp parallel for
	for (i = 0; i < AllPix; i++)
	{
		size_t
			cur_x = (size_t)i % w,
			cur_y = (size_t)i / w;
		ptrdiff_t sx, sy;
		float MaxK = 0;
		uint32_t MaxK_x = (uint32_t)cur_x;
		uint32_t MaxK_y = (uint32_t)cur_y;
		float CurAlt = ImgBuffer_FetchF32(Altitude, cur_x, cur_y);
		for (sy = -SearchRadius; sy <= SearchRadius; sy++)
		{
			for (sx = -SearchRadius; sx <= SearchRadius; sx++)
			{
				ptrdiff_t x, y;
				float Dist;
				float AltDiff;
				float K_val;
				if (!sx && !sy) continue;
				x = sx + (ptrdiff_t)cur_x;
				y = sy + (ptrdiff_t)cur_y;
				while (x < 0) x += w;
				while (y < 0) y += h;
				x %= Altitude->Width;
				y %= Altitude->Height;
				AltDiff = ImgBuffer_FetchF32(Altitude, x, y) - CurAlt;
				Dist = sqrtf((float)sx * sx + (float)sy * sy);
				K_val = AltDiff / Dist;
				if (K_val > MaxK)
				{
					MaxK = K_val;
					MaxK_x = (uint32_t)x;
					MaxK_y = (uint32_t)y;
				}
			}
		}
		ImgBuffer_FetchU32(K2, cur_x, cur_y) = MaxK_x | (MaxK_y << 16);
		ImgBuffer_FetchF32(K1, cur_x, cur_y) = MaxK;
	}

	do
	{
		calc_count = 0;

#pragma omp parallel for
		for (i = 0; i < AllPix; i++)
		{
			size_t
				cur_x = (size_t)i % w,
				cur_y = (size_t)i / w;
			ptrdiff_t sx, sy;
			float MaxK = ImgBuffer_FetchF32(K1, cur_x, cur_y);
			uint32_t MaxK_xy = ImgBuffer_FetchU32(K2, cur_x, cur_y);
			uint32_t MaxK_x = MaxK_xy & 0xffff;
			uint32_t MaxK_y = MaxK_xy >> 16;
			float CurAlt = ImgBuffer_FetchF32(Altitude, cur_x, cur_y);
			for (sy = -SearchRadius; sy <= SearchRadius; sy++)
			{
				for (sx = -SearchRadius; sx <= SearchRadius; sx++)
				{
					ptrdiff_t x, y;
					float Dist;
					float AltDiff;
					float dx, dy;
					if (!sx && !sy) continue;
					x = sx + (ptrdiff_t)MaxK_x;
					y = sy + (ptrdiff_t)MaxK_y;
					while (x < 0) x += w;
					while (y < 0) y += h;
					x %= Altitude->Width;
					y %= Altitude->Height;
					AltDiff = ImgBuffer_FetchF32(Altitude, x, y) - CurAlt;
					dx = (float)cur_x - x;
					dy = (float)cur_y - y;
					Dist = sqrtf(dx * dx + dy * dy);
					if (Dist > 0.5)
					{
						float K_val = AltDiff / Dist;
						if (K_val > MaxK)
						{
							MaxK = K_val;
							MaxK_x = (uint32_t)x;
							MaxK_y = (uint32_t)y;
#pragma omp atomic
							calc_count++;
						}
					}
				}
			}
			ImgBuffer_FetchU32(K2, cur_x, cur_y) = MaxK_x | (MaxK_y << 16);
			ImgBuffer_FetchF32(K1, cur_x, cur_y) = MaxK;
		}

	} while (calc_count);

	r->K = K1;

	fp = fopen(Path, "wb");
	if (!fp ||
		!fwrite(&K1->Width, sizeof K1->Width, 1, fp) ||
		!fwrite(&K1->Height, sizeof K1->Height, 1, fp) ||
		!fwrite(K1->Buffer, K1->BufferSize, 1, fp))
	{
		log_printf(fp_log, "Could not write generated K file: '%s'\n", Path);
		goto FailExit;
	}
	fclose(fp);

#pragma omp parallel for
	for (i = 0; i < AllPix; i++)
	{
		size_t
			cur_x = (size_t)i % w,
			cur_y = (size_t)i / w;
		union Pixel
		{
			uint8_t u8[4];
			uint32_t u32;
		}Pixel;
		uint32_t MaxK_xy = ImgBuffer_FetchU32(K2, cur_x, cur_y);
		uint32_t MaxK_x = MaxK_xy & 0xffff;
		uint32_t MaxK_y = MaxK_xy >> 16;

		Pixel.u32 = 0;
		Pixel.u8[0] = (MaxK_x - (int32_t)cur_x) + 127;
		Pixel.u8[1] = (MaxK_y - (int32_t)cur_y) + 127;
		Pixel.u8[2] = (uint8_t)(ImgBuffer_FetchF32(K1, MaxK_x, MaxK_y) * 255);
		ImgBuffer_FetchU32(K2, cur_x, cur_y) = Pixel.u32;
	}

	SaveUB = ImgBuffer_ConvertToUniformBitmap(&K2);
	snprintf(StrBuf, sizeof StrBuf, "%s.bmp", Path);
	UB_SaveToFile_32(SaveUB, StrBuf);
	UB_Free(&SaveUB);
	return 1;
FailExit:
	if (fp) fclose(fp);
	ImgBuffer_Destroy(K1);
	ImgBuffer_Destroy(K2);
	UB_Free(&SaveUB);
	return 0;
}

static void GetMinMax(RayMap_p r)
{
	ptrdiff_t i, c;
	int w, h;
	float
		min_a = real_max,
		max_a = -real_max;
	w = r->Altitude->Width;
	h = r->Altitude->Height;
	c = (ptrdiff_t)w * h;

#if _OPENMP >= 201107

#pragma omp parallel for reduction(min:min_a)
	for (i = 0; i < c; i++)
	{
		int x = i % w,
			y = i / w;

		min_a = r_min(min_a, ImgBuffer_FetchF32(r->Altitude, x, y));
	}

#pragma omp parallel for reduction(max:max_a)
	for (i = 0; i < c; i++)
	{
		int x = i % w,
			y = i / w;

		max_a = r_max(max_a, ImgBuffer_FetchF32(r->Altitude, x, y));
	}

#else
	c /= 4;
	w /= 2;
#pragma omp parallel for
	for (i = 0; i < c; i++)
	{
		int x = (int)(i % w) * 2,
			y = (int)(i / w) * 2;
		float
			alt00 = ImgBuffer_FetchF32(r->Altitude, x + 0, y + 0),
			alt01 = ImgBuffer_FetchF32(r->Altitude, x + 0, y + 1),
			alt10 = ImgBuffer_FetchF32(r->Altitude, x + 1, y + 0),
			alt11 = ImgBuffer_FetchF32(r->Altitude, x + 1, y + 1);
		float max_alt = r_max(
			r_max(alt00, alt01),
			r_max(alt10, alt11));
		float min_alt = r_min(
			r_min(alt00, alt01),
			r_min(alt10, alt11));

#pragma omp critical
		{
			max_a = r_max(max_a, max_alt);
			min_a = r_min(min_a, min_alt);
		}
	}
#endif
	r->MaxAltitude = max_a;
	r->MinAltitude = min_a;
}

static void Altitude_GrowInterpolate(ImgBuffer_p dst, int dst_x, int dst_y, ImgBuffer_p src, float src_x, float src_y)
{
	real_t fx = r_floor(src_x);
	real_t fy = r_floor(src_y);
	real_t lx = fx - src_x;
	real_t ly = fy - src_y;
	int ix = (int)fx;
	int iy = (int)fy;
	ImgBuffer_FetchF32(dst, dst_x, dst_y) = r_lerp(
		r_lerp(ImgBuffer_FetchF32(src, ix + 0, iy + 0), ImgBuffer_FetchF32(src, ix + 1, iy + 0), lx),
		r_lerp(ImgBuffer_FetchF32(src, ix + 0, iy + 1), ImgBuffer_FetchF32(src, ix + 1, iy + 1), lx),
		ly);
}

static void Altitude_ShrinkToPixel(ImgBuffer_p dst, int dst_x, int dst_y, ImgBuffer_p src, int src_x, int src_y, uint32_t src_w, uint32_t src_h)
{
	uint32_t x, y;
	float val = 0;
	int total = src_w * src_h;
	for (y = 0; y < src_h; y++)
	{
		for (x = 0; x < src_w; x++)
		{
			val += ImgBuffer_FetchF32(src, x + src_x, y + src_y);
		}
	}
	ImgBuffer_FetchF32(dst, dst_x, dst_y) = val / total;
}

RayMap_p RayMap_LoadFromRaw(char *AssetsDirectory, char *AltitudeFile, char *RawAltitudeFile, char *KFile, int RawAltitudeBlur, FILE *fp_log)
{
	char Path[1024];
	RayMap_p r = NULL;
	int AltitudeLoaded = 0;

	if (!fp_log) fp_log = stderr;
	if (!AssetsDirectory) AssetsDirectory = ".";
	if (!AltitudeFile && !RawAltitudeFile)
	{
		log_printf(fp_log, "Invalid function call: no altitude file or raw altitude file is provided.\n");
		goto FailExit;
	}

	if (RawAltitudeBlur < 0)
	{
		log_printf(fp_log, "Invalid raw_altitude_blur value '%d', should be >= 0.\n", RawAltitudeBlur);
		goto FailExit;
	}

	r = malloc(sizeof * r);
	if (!r)
	{
		log_printf(fp_log, "Unable to create RayMap instance: %s\n", strerror(errno));
		goto FailExit;
	}
	memset(r, 0, sizeof * r);

	while (!AltitudeLoaded)
	{
		if (AltitudeFile)
		{
			snprintf(Path, sizeof Path, "%s\\%s", AssetsDirectory, AltitudeFile);
			AltitudeFile = Path;
			if (LoadAltitude(r, fp_log, AltitudeFile))
			{
				AltitudeLoaded = 1;
				continue;
			}
		}
		if (RawAltitudeFile)
		{
			char RawPath[1024];
			if (!AltitudeFile)
			{
				snprintf(Path, sizeof Path, "%s\\%s_generated.bin", AssetsDirectory, RawAltitudeFile);
				AltitudeFile = Path;
			}
			snprintf(RawPath, sizeof Path, "%s\\%s", AssetsDirectory, RawAltitudeFile);
			RawAltitudeFile = RawPath;
			log_printf(fp_log, "Generating altitude file from raw file '%s' => '%s'.\n", RawAltitudeFile, AltitudeFile);
			if (LoadAltitudeFromRaw(r, fp_log, RawAltitudeFile, AltitudeFile, RawAltitudeBlur))
			{
				AltitudeLoaded = 1;
				continue;
			}
		}
		log_printf(fp_log, "Unable to load altitude file.\n");
		goto FailExit;
	}

	if (!ImgBuffer_Grow2N(r->Altitude, Altitude_GrowInterpolate))
	{
		log_printf(fp_log, "Expanding altitude map ('%s') size to 2^N failed, trying to reduce the size to 2^N.\n", AltitudeFile);
		if (!ImgBuffer_Shrink2N(r->Altitude, Altitude_ShrinkToPixel))
		{
			log_printf(fp_log, "Shrinking altitude map ('%s') size to 2^N failed, use scissored altitude map.\n", AltitudeFile);
		}
	}

	snprintf(Path, sizeof Path, "%s\\%s", AssetsDirectory, KFile);
	KFile = Path;
	if (!LoadK(r, fp_log, KFile))
	{
		log_printf(fp_log, "Generating K file '%s'.\n", KFile);
		if (!GenerateK(r, fp_log, KFile)) goto FailExit;
	}

	GetMinMax(r);
	return r;
FailExit:
	RayMap_Unload(r);
	return NULL;
}

RayMap_p RayMap_Load(char *AssetsDirectory, char *AltitudeFile, char *KFile, FILE *fp_log)
{
	char Path[1024];
	RayMap_p r = NULL;

	if (!fp_log) fp_log = stderr;
	if (!AssetsDirectory) AssetsDirectory = ".";
	if (!AltitudeFile)
	{
		log_printf(fp_log, "Invalid function call: no altitude file is provided.\n");
		goto FailExit;
	}

	r = malloc(sizeof * r);
	if (!r)
	{
		log_printf(fp_log, "Unable to create RayMap instance: %s\n", strerror(errno));
		goto FailExit;
	}
	memset(r, 0, sizeof * r);

	if (!LoadAltitude(r, fp_log, AltitudeFile))
	{
		log_printf(fp_log, "Unable to load altitude file.\n");
		goto FailExit;
	}

	if (!ImgBuffer_Grow2N(r->Altitude, Altitude_GrowInterpolate))
	{
		log_printf(fp_log, "Expanding altitude map ('%s') size to 2^N failed, trying to reduce the size to 2^N.\n", AltitudeFile);
		if (!ImgBuffer_Shrink2N(r->Altitude, Altitude_ShrinkToPixel))
		{
			log_printf(fp_log, "Shrinking altitude map ('%s') size to 2^N failed, use scissored altitude map.\n", AltitudeFile);
		}
	}

	snprintf(Path, sizeof Path, "%s\\%s", AssetsDirectory, KFile);
	KFile = Path;
	if (!LoadK(r, fp_log, KFile))
	{
		log_printf(fp_log, "Generating K file '%s'.\n", KFile);
		if (!GenerateK(r, fp_log, KFile)) goto FailExit;
	}

	GetMinMax(r);
	return r;
FailExit:
	RayMap_Unload(r);
	return NULL;
}

RayMap_p RayMap_CreateFromAltitude(ImgBuffer_p Altitude, char *AssetsDirectory, char *AltitudeSaveFile, char *KFile_SavePath, FILE *fp_log)
{
	char Path[1024];
	RayMap_p r = NULL;

	if (!fp_log) fp_log = stderr;
	if (!AssetsDirectory) AssetsDirectory = ".";
	if (!Altitude)
	{
		log_printf(fp_log, "Invalid function call: no altitude is provided.\n");
		goto FailExit;
	}

	r = malloc(sizeof * r);
	if (!r)
	{
		log_printf(fp_log, "Unable to create RayMap instance: %s\n", strerror(errno));
		goto FailExit;
	}
	memset(r, 0, sizeof * r);

	r->Altitude = Altitude;

	if (!ImgBuffer_Grow2N(r->Altitude, Altitude_GrowInterpolate))
	{
		log_printf(fp_log, "Expanding altitude map ('%s') size to 2^N failed, trying to reduce the size to 2^N.\n", AltitudeSaveFile);
		if (!ImgBuffer_Shrink2N(r->Altitude, Altitude_ShrinkToPixel))
		{
			log_printf(fp_log, "Shrinking altitude map ('%s') size to 2^N failed, use scissored altitude map.\n", AltitudeSaveFile);
		}
		else
		{
			FILE *fp = fopen(AltitudeSaveFile, "wb");
			if (!fp ||
				!fwrite(&Altitude->Width, sizeof Altitude->Width, 1, fp) ||
				!fwrite(&Altitude->Height, sizeof Altitude->Height, 1, fp) ||
				!fwrite(Altitude->Buffer, Altitude->BufferSize, 1, fp))
			{
				log_printf(fp_log, "Could not write altitude file: '%s'\n", AltitudeSaveFile);
				if (fp) fclose(fp); fp = NULL;
				goto FailExit;
			}
		}
	}

	snprintf(Path, sizeof Path, "%s\\%s", AssetsDirectory, KFile_SavePath);
	KFile_SavePath = Path;
	if (!LoadK(r, fp_log, KFile_SavePath))
	{
		log_printf(fp_log, "Generating K file '%s'.\n", KFile_SavePath);
		if (!GenerateK(r, fp_log, KFile_SavePath)) goto FailExit;
	}

	GetMinMax(r);
	return r;
FailExit:
	RayMap_Unload(r);
	return NULL;
}

void RayMap_Unload(RayMap_p r)
{
	if (!r) return;
	ImgBuffer_Destroy(r->Altitude);
	ImgBuffer_Destroy(r->K);
	free(r);
}

float RayMap_GetAltitude(RayMap_p r, float x, float y)
{
	int x1, y1, x2, y2;
	real_t fx, fy;
	real_t sx, sy;
	int w, h;

	fx = r_floor(x); sx = r_clamp(x - fx, 0, 1);
	fy = r_floor(y); sy = r_clamp(y - fy, 0, 1);

	w = r->Altitude->Width2N;
	h = r->Altitude->Height2N;

	x1 = (int)fx & (w - 1);
	y1 = (int)fy & (h - 1);
	x2 = (x1 + 1) & (w - 1);
	y2 = (y1 + 1) & (h - 1);

	return r_lerp(
		r_lerp(ImgBuffer_FetchF32(r->Altitude, x1, y1), ImgBuffer_FetchF32(r->Altitude, x2, y1), sx),
		r_lerp(ImgBuffer_FetchF32(r->Altitude, x1, y2), ImgBuffer_FetchF32(r->Altitude, x2, y2), sx),
		sy);
}

float RayMap_GetK(RayMap_p r, float x, float y)
{
	int x1, y1, x2, y2;
	real_t fx, fy;
	real_t sx, sy;
	int w, h;

	fx = r_floor(x); sx = r_clamp(x - fx, 0, 1);
	fy = r_floor(y); sy = r_clamp(y - fy, 0, 1);

	w = r->K->Width2N;
	h = r->K->Height2N;

	x1 = (int)fx & (w - 1);
	y1 = (int)fy & (h - 1);
	x2 = (x1 + 1) & (w - 1);
	y2 = (y1 + 1) & (h - 1);

	return r_lerp(
		r_lerp(ImgBuffer_FetchF32(r->K, x1, y1), ImgBuffer_FetchF32(r->K, x2, y1), sx),
		r_lerp(ImgBuffer_FetchF32(r->K, x1, y2), ImgBuffer_FetchF32(r->K, x2, y2), sx),
		sy);
}

int RayMap_Raycast(RayMap_p r, vec4_t RayOrig, vec4_t RayDir, int IterCount, vec4_p Out_CastPoint, float *Out_CastDist, float MaxDist)
{
	real_t Ray_HorZ, CastDist, RayK;
	int MaxIterCount;
	real_t MinDistThreshold;
	vec4_t CurOrig = RayOrig;
	int i;

	MaxIterCount = IterCount;
	MinDistThreshold = 0.1f;

	Ray_HorZ = RayDir.x * RayDir.x + RayDir.z * RayDir.z;
	if (Ray_HorZ <= r_epsilon)
	{
		CurOrig.y = RayMap_GetAltitude(r, CurOrig.x, CurOrig.z);
		CastDist = RayOrig.y - CurOrig.y;
		if (Out_CastPoint) *Out_CastPoint = CurOrig;
		if (Out_CastDist) *Out_CastDist = CastDist;
		return 1;
	}

	Ray_HorZ = r_sqr(Ray_HorZ);
	RayK = -RayDir.y / Ray_HorZ;
	CastDist = 0;

	for (i = 0; i < MaxIterCount; i++)
	{
		real_t CurAlt = RayMap_GetAltitude(r, CurOrig.x, CurOrig.z);
		real_t CurK = RayMap_GetK(r, CurOrig.x, CurOrig.z);
		real_t K = CurK + RayK;
		real_t AdvancingDistance;
		if (RayK < 0 && -RayK > CurK)
		{
			return 0;
		}
		if (K > r_epsilon) AdvancingDistance = (CurOrig.y - CurAlt) / Ray_HorZ / K;
		else return 0;
		AdvancingDistance *= 0.5;
		CurOrig = vec4_add(CurOrig, vec4_scale(RayDir, AdvancingDistance));
		CastDist += AdvancingDistance;
		if (AdvancingDistance <= MinDistThreshold && CurOrig.y <= CurAlt + MinDistThreshold) break;
		if (CastDist > MaxDist)
		{
			return 0;
		}
	}

	if (Out_CastPoint) *Out_CastPoint = CurOrig;
	if (Out_CastDist) *Out_CastDist = CastDist;
	return 1;
}
