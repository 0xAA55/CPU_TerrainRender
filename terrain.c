#include "terrain.h"
#include "dictcfg.h"

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<math.h>

static const char *Name_Terrain_Albedo = "Terrain_Albedo";
static const char *Name_Terrain_Altitude = "Terrain_Altitude";
static const char *Name_Terrain_K = "Terrain_K";

static int GetAlbedo(Terrain_p t, dict_p d_landview, FILE *fp_log, const char *path)
{
	if (!CPUCan_LoadTextureFromFile(t->CPUCan, path, Name_Terrain_Albedo))
	{
		log_printf(fp_log, "Could not load '%s'.\n", path);
		return 0;
	}
	t->Albedo = CPUCan_GetTexture(t->CPUCan, Name_Terrain_Albedo);
	if (!t->Albedo)
	{
		log_printf(fp_log, "Could not get texture '%s'.\n", Name_Terrain_Albedo);
		return 0;
	}
	return 1;
}

static int GetAltitude(Terrain_p t, dict_p d_landview, FILE *fp_log, char *path)
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

	if (!CPUCan_SetTexture(t->CPUCan, Name_Terrain_Altitude, Altitude)) goto FailExit;
	t->Altitude = Altitude;

	fclose(fp);
	return 1;
FailExit:
	log_printf(fp_log, "Could not load altitude file: '%s'\n", path);
	if (fp) fclose(fp);
	ImgBuffer_Destroy(Altitude);
	return 0;
}

static int GetAltitudeFromRaw(Terrain_p t, dict_p d_landview, FILE *fp_log, const char *dir, const char *raw_file, const char *file)
{
	char StrBuf[1024];
	ImgBuffer_p RawAltitude = NULL;
	ImgBuffer_p Altitude = NULL;
	FILE *fp = NULL;
	ptrdiff_t i, AllPix;

	snprintf(StrBuf, sizeof StrBuf, "%s\\%s", dir, raw_file);
	RawAltitude = ImgBuffer_CreateFromBMPFile(StrBuf);
	if (!RawAltitude)
	{
		log_printf(fp_log, "Could not load raw altitude file: '%s'\n", StrBuf);
		goto FailExit;
	}

	Altitude = ImgBuffer_Create(RawAltitude->Width, RawAltitude->Height, 4, 4);
	if (!Altitude)
	{
		log_printf(fp_log, "No enough memory for creating altitude map (%u, %u).\n",
			RawAltitude->Width, RawAltitude->Height);
		goto FailExit;
	}

	if (!CPUCan_SetTexture(t->CPUCan, Name_Terrain_Altitude, Altitude)) goto FailExit;
	t->Altitude = Altitude;

	AllPix = (ptrdiff_t)(Altitude->Width * Altitude->Height);

#pragma omp parallel for
	for (i = 0; i < AllPix; i++)
	{
		size_t
			cur_x = (size_t)i % Altitude->Width,
			cur_y = (size_t)i / Altitude->Height;
		size_t count = 0;
		float sum = 0;
		ptrdiff_t sx, sy;
		for (sy = -t->RawAltitudeBlur; sy <= t->RawAltitudeBlur; sy++)
		{
			for (sx = -t->RawAltitudeBlur; sx <= t->RawAltitudeBlur; sx++)
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

	snprintf(StrBuf, sizeof StrBuf, "%s\\%s", dir, file);
	fp = fopen(StrBuf, "wb");
	if (!fp ||
		!fwrite(&Altitude->Width, sizeof Altitude->Width, 1, fp) ||
		!fwrite(&Altitude->Height, sizeof Altitude->Height, 1, fp) ||
		!fwrite(Altitude->Buffer, Altitude->BufferSize, 1, fp))
	{
		log_printf(fp_log, "Could not write generated altitude file: '%s'\n", StrBuf);
		goto FailExit;
	}
	fclose(fp);
	ImgBuffer_Destroy(RawAltitude);
	return 1;
FailExit:
	if (fp) fclose(fp);
	ImgBuffer_Destroy(RawAltitude);
	ImgBuffer_Destroy(Altitude);
	return 0;
}

static int GetK(Terrain_p t, dict_p d_landview, FILE *fp_log, const char *Path)
{
	uint32_t w, h;
	FILE *fp = NULL;
	ImgBuffer_p K = NULL;
	size_t r;

	fp = fopen(Path, "rb");
	if (!fp) goto FailExit;

	if (!fread(&w, sizeof w, 1, fp)) goto FailExit;
	if (!fread(&h, sizeof h, 1, fp)) goto FailExit;

	K = ImgBuffer_Create(w, h, 4, 4);
	if (!K) goto FailExit;

	r = fread(K->Buffer, 1, K->BufferSize, fp);
	if (r != K->BufferSize) goto FailExit;

	if (!CPUCan_SetTexture(t->CPUCan, Name_Terrain_K, K)) goto FailExit;
	t->K = K;

	fclose(fp);
	return 1;
FailExit:
	log_printf(fp_log, "Could not load K-map file: '%s'\n", Path);
	if (fp) fclose(fp);
	ImgBuffer_Destroy(K);
	return 0;
}

static int GenerateK(Terrain_p t, dict_p d_landview, FILE *fp_log, const char *Path)
{
	uint32_t w, h;
	ImgBuffer_p Altitude = t->Altitude;
	ImgBuffer_p K1 = NULL, K2 = NULL;
	UniformBitmap_p SaveUB = NULL;
	ptrdiff_t i, AllPix;
	size_t calc_count = 0;
	FILE *fp = NULL;
	char StrBuf[1024];
	const int SearchRadius = 2;

	w = t->Altitude->Width;
	h = t->Altitude->Height;

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

	if (!CPUCan_SetTexture(t->CPUCan, Name_Terrain_K, K1)) goto FailExit;
	t->K = K1;

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
		Pixel.u8[0] = MaxK_x * 255 / w;
		Pixel.u8[1] = MaxK_y * 255 / h;
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

static int LoadCfg(Terrain_p t, dict_p Config)
{
	dict_p d_render = NULL;
	FILE *fp_log = t->fp_log;

	d_render = dictcfg_section(Config, "[render]");
	if (!d_render)
	{
		log_printf(fp_log, "'[render]' section not defined in 'config.cfg' file.\n");
		goto FailExit;
	}
	t->x_res = dictcfg_getint(d_render, "x_res", t->CPUCan->Width);
	t->y_res = dictcfg_getint(d_render, "y_res", t->CPUCan->Height);
	t->interleave = dictcfg_getint(d_render, "interleave", 0);
	t->x_scale = dictcfg_getint(d_render, "x_scale", 4);
	t->y_scale = dictcfg_getint(d_render, "y_scale", 1);

	



	t->LandView = ImgBuffer_Create(t->x_res / t->x_scale, t->y_res / t->y_scale, 4, 4);
	if (!t->LandView)
	{
		log_printf(fp_log, "Create landview buffer failed.\n");
		goto FailExit;
	}

	return 1;
FailExit:
	return 0;
}

static int LoadMap(Terrain_p t, const char *MapDir)
{
	char StrBuf[1024];
	dict_p d_landview = NULL;
	char *altitude_file = NULL;
	char *raw_altitude_file = NULL;
	FILE *fp_log = t->fp_log;
	dict_p MapCfg = NULL;

	snprintf(StrBuf, sizeof StrBuf, "%s\\meta.ini", MapDir);
	MapCfg = dictcfg_load(StrBuf, fp_log);
	if (!MapCfg) goto FailExit;

	d_landview = dictcfg_section(MapCfg, "[landview]");
	if (!d_landview)
	{
		log_printf(fp_log, "'[landview]' section not defined in map config file 'meta.ini'.\n");
		goto FailExit;
	}

	snprintf(StrBuf, sizeof StrBuf, "%s\\%s", MapDir, dictcfg_getstr(d_landview, "albedo", "albedo.bmp"));
	if (!GetAlbedo(t, d_landview, fp_log, StrBuf)) goto FailExit;

	t->AlbedoScale = dictcfg_getfloat(d_landview, "albedo_scale", 1);
	t->AltitudeScale = dictcfg_getfloat(d_landview, "altitude_scale", 1);
	t->RawAltitudeBlur = dictcfg_getint(d_landview, "raw_altitude_blur", 0);
	if (t->RawAltitudeBlur <= 0)
	{
		log_printf(fp_log, "Invalid raw_altitude_blur value '%d', should be > 0.\n", t->RawAltitudeBlur);
		goto FailExit;
	}

	snprintf(StrBuf, sizeof StrBuf, "%s\\%s", MapDir, altitude_file = dictcfg_getstr(d_landview, "altitude", "altitude.bin"));
	if (!GetAltitude(t, d_landview, fp_log, StrBuf))
	{
		log_printf(fp_log, "Could not load altitude file: '%s', trying to generate from raw.\n", StrBuf);
		raw_altitude_file = dictcfg_getstr(d_landview, "raw_altitude", "altitude.bmp");
		if (!GetAltitudeFromRaw(t, d_landview, fp_log, MapDir, raw_altitude_file, altitude_file))
		{
			goto FailExit;
		}
	}

	snprintf(StrBuf, sizeof StrBuf, "%s\\%s", MapDir, dictcfg_getstr(d_landview, "k", "k.bin"));
	if (!GetK(t, d_landview, fp_log, StrBuf))
	{
		if (!GenerateK(t, d_landview, fp_log, StrBuf))
		{
			goto FailExit;
		}
	}

	t->RenderDist = (float)dictcfg_getfloat(d_landview, "render_dist", 256);
	t->IterCount = dictcfg_getint(d_landview, "iter_count", 64);

	dict_delete(MapCfg);
	return 1;

FailExit:
	dict_delete(MapCfg);
	return 0;
}

Terrain_p Terrain_Create(CPUCan_p CPUCan, const char *WorkDir, const char *MapDir, dict_p Config)
{
	char StrBuf[1024];
	Terrain_p t = NULL;
	FILE *fp_log = NULL;

	if (!CPUCan || !WorkDir || !MapDir) return t;

	snprintf(StrBuf, sizeof StrBuf, "%s\\terrain.log", WorkDir);
	fp_log = fopen(StrBuf, "a");
	if (!fp_log) goto FailExit;

	t = malloc(sizeof t[0]);
	if (!t) goto FailExit;
	memset(t, 0, sizeof t[0]);

	t->CPUCan = CPUCan;
	t->fp_log = fp_log;

	if (!LoadCfg(t, Config)) goto FailExit;

	if (!LoadMap(t, MapDir)) goto FailExit;

	Terrain_SetCamera(t,
		vec4(0, 10, 0, 0),
		vec4(0, 0, 1, 0),
		vec4(0, 1, 0, 0), 
		0.5f);

	fclose(fp_log);
	return t;
FailExit:
	if (fp_log) fclose(fp_log);
	Terrain_Free(t);
	return NULL;
}

void Terrain_SetCamera(Terrain_p t, vec4_t CamPos, vec4_t CamDir, vec4_t CamUp, float FOV)
{
	t->CameraPos = CamPos;
	t->CameraDir = CamDir;
	t->CameraUp = CamUp;
	t->FOV = FOV;
}

static uint32_t ARGB(int32_t R, int32_t G, int32_t B, int32_t A)
{
	union Color
	{
		uint8_t u8[4];
		uint32_t u32;
	}Color = {B, G, R, A};
	return Color.u32;
}

static uint32_t ARGBSafe(int32_t R, int32_t G, int32_t B, int32_t A)
{
	if (R > 255) R = 255; else if (R < 0) R = 0;
	if (G > 255) G = 255; else if (G < 0) G = 0;
	if (B > 255) B = 255; else if (B < 0) B = 0;
	if (A > 255) A = 255; else if (A < 0) A = 0;
	return ARGB(R, G, B, A);
}

static int Map_Raycast(Terrain_p t, vec4_t RayOrig, vec4_t RayDir, vec4_p pCastPoint, float *pCastDist, float MaxDist)
{

}

void Terrain_Render(Terrain_p t)
{
	ptrdiff_t i;
	ptrdiff_t PixelCount;
	ImgBuffer_p LandView, ColorBuf;
	size_t w, h, sw, sh;

	if (!t) return;

	LandView = t->LandView;
	ColorBuf = t->CPUCan->ColorBuf;
	w = LandView->Width;
	h = LandView->Height;
	PixelCount = (ptrdiff_t)(w * h);

#pragma omp parallel for
	for (i = 0; i < PixelCount; i++)
	{
		size_t x, y;

		x = (size_t)i % w;
		y = (size_t)i / w;

		if (t->interleave)
		{
			if (((x & 1) ^ (y & 1)) == (t->FrameCounter & 1))
				continue;
		}

		ImgBuffer_FetchU32(LandView, x, y) = ARGB(x + t->FrameCounter, y + t->FrameCounter, i, 0xff);
	}

	sw = w; sh = h;
	w = ColorBuf->Width;
	h = ColorBuf->Height;
	PixelCount = (ptrdiff_t)(w * h);
#pragma omp parallel for
	for (i = 0; i < PixelCount; i++)
	{
		size_t x, y, sx, sy;
		x = (size_t)i % w;
		y = (size_t)i / w;
		sx = x / t->x_scale;
		sy = y / t->y_scale;
		ImgBuffer_FetchU32(ColorBuf, x, y) = ImgBuffer_FetchU32(LandView, sx, sy);
	}

	t->FrameCounter++;
}

void Terrain_Free(Terrain_p t)
{
	if (!t) return;

	if (t->fp_log) fclose(t->fp_log);

	if (t->Albedo) CPUCan_DeleteTexture(t->CPUCan, Name_Terrain_Albedo);
	if (t->Altitude) CPUCan_DeleteTexture(t->CPUCan, Name_Terrain_Altitude);
	if (t->K) CPUCan_DeleteTexture(t->CPUCan, Name_Terrain_K);
	free(t);
}




