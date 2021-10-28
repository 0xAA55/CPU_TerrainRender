#include"game.h"
#include"dictcfg.h"
#include"logprintf.h"

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<math.h>
#include<errno.h>

static const char *Name_Game_Albedo = "Game_Albedo";

static void RGBAGrowInterpolate(ImgBuffer_p dst, int dst_x, int dst_y, ImgBuffer_p src, float src_x, float src_y)
{
	ImgBuffer_FetchU32(dst, dst_x, dst_y) = CPUCan_SampleTexture(src, src_x, src_y);
}

static void RGBAShrinkToPixel(ImgBuffer_p dst, int dst_x, int dst_y, ImgBuffer_p src, int src_x, int src_y, uint32_t src_w, uint32_t src_h)
{
	uint32_t x, y;
	float sum_r, sum_g, sum_b, sum_a;
	int total = src_w * src_h;
	sum_r = sum_g = sum_b = sum_a = 0;
	for (y = 0; y < src_h; y++)
	{
		for (x = 0; x < src_w; x++)
		{
			union Color
			{
				uint8_t u8[4];
				uint32_t u32;
			}Pixel;
			Pixel.u32 = ImgBuffer_FetchU32(src, x + src_x, y + src_y);
			sum_b += Pixel.u8[0];
			sum_g += Pixel.u8[1];
			sum_r += Pixel.u8[2];
			sum_a += Pixel.u8[3];
		}
	}
	ImgBuffer_FetchU32(dst, dst_x, dst_y) = ARGBSafe(
		(int32_t)(sum_r / total),
		(int32_t)(sum_g / total),
		(int32_t)(sum_b / total),
		(int32_t)(sum_a / total));
}

static uint32_t ParseColorRGB(char *r_comma_g_comma_b_comma)
{
	int r, g, b;

	if (sscanf(r_comma_g_comma_b_comma, "%d,%d,%d", &r, &g, &b) != 3) return 0;
	return ARGBSafe(r, g, b, 255);
}

static int GetAlbedo(Game_p t, dict_p d_landview, FILE *fp_log, const char *path)
{
	if (!CPUCan_LoadTextureFromFile(t->CPUCan, path, Name_Game_Albedo))
	{
		log_printf(fp_log, "Could not load '%s'.\n", path);
		return 0;
	}
	t->Albedo = CPUCan_GetTexture(t->CPUCan, Name_Game_Albedo);
	if (!t->Albedo)
	{
		log_printf(fp_log, "Could not get texture '%s'.\n", Name_Game_Albedo);
		return 0;
	}
	if (!ImgBuffer_To2N(t->Albedo, RGBAGrowInterpolate, RGBAShrinkToPixel))
	{
		log_printf(fp_log, "Could not interpolate texture '%s' size to 2^N, use scissored texture.\n", Name_Game_Albedo);
	}
	return 1;
}

static int LoadCfg(Game_p t, dict_p Config)
{
	dict_p d_player = NULL;
	dict_p d_input = NULL;
	dict_p d_render = NULL;
	dict_p d_profile = NULL;
	FILE *fp_log = t->fp_log;

	d_player = dictcfg_section(Config, "[player]");
	t->Player_Height = (float)dictcfg_getfloat(d_input, "height", 170.0);
	t->Player_Size = (float)dictcfg_getfloat(d_input, "size", 30.0);

	d_input = dictcfg_section(Config, "[input]");
	/*
	if (!input)
	{
		log_printf(fp_log, "'[input]' section not defined in 'config.cfg' file.\n");
		goto FailExit;
	}*/
	t->Sensitivity = (float)dictcfg_getfloat(d_input, "sensitivity", 1.0);

	d_profile = dictcfg_section(Config, "[profile]");
	/*
	if (!d_profile)
	{
		log_printf(fp_log, "'[profile]' section not defined in 'config.cfg' file.\n");
		goto FailExit;
	}*/
	t->profile_task_per_line = dictcfg_getint(d_profile, "task_per_line", 1);
	t->profile_task_per_pixel = dictcfg_getint(d_profile, "task_per_pixel", !t->profile_task_per_line);
	if ((t->profile_task_per_line && t->profile_task_per_pixel) ||
		(!t->profile_task_per_line && !t->profile_task_per_pixel))
	{
		log_printf(fp_log, "In '[profile]' section: bad config: both 'task_per_line' and 'task_per_pixel' was set to the same boolean value (zero or non-zero).\n");
		goto FailExit;
	}

	d_render = dictcfg_section(Config, "[render]");
	/*
	if (!d_render)
	{
		log_printf(fp_log, "'[render]' section not defined in 'config.cfg' file.\n");
		goto FailExit;
	}*/
	t->x_res = dictcfg_getint(d_render, "x_res", t->CPUCan->Width);
	t->y_res = dictcfg_getint(d_render, "y_res", t->CPUCan->Height);
	t->interleave = dictcfg_getint(d_render, "interleave", 0);
	t->x_scale = dictcfg_getint(d_render, "x_scale", 4);
	t->y_scale = dictcfg_getint(d_render, "y_scale", 1);
	t->interpolate = dictcfg_getint(d_render, "interpolate", 1);
	t->skycolor1 = ParseColorRGB(dictcfg_getstr(d_render, "skycolor1", "96,96,200"));
	t->skycolor2 = ParseColorRGB(dictcfg_getstr(d_render, "skycolor1", "200,210,220"));

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

static uint32_t Map_FetchAlbedo(Game_p t, int x, int y)
{
	int xm = t->Albedo->Width2N - 1;
	int ym = t->Albedo->Height2N - 1;
	return ImgBuffer_FetchU32(t->Albedo, x & xm, y & ym);
}

static uint32_t Map_GetAlbedo(Game_p t, float x, float y)
{
	x /= (float)t->AlbedoScale;
	y /= (float)t->AlbedoScale;
	if (t->interpolate)
		return CPUCan_SampleTexture(t->Albedo, x, y);
	else
	{
		real_t fx = r_floor(x);
		real_t fy = r_floor(y);
		uint32_t ux = (uint32_t)fx;
		uint32_t uy = (uint32_t)fy;
		uint32_t uw = t->Albedo->Width2N;
		uint32_t uh = t->Albedo->Height2N;
		return ImgBuffer_FetchU32(t->Albedo, ux & (uw - 1), uy & (uh - 1));
	}
}

static void PreRenderLighting(Game_p t)
{
	ptrdiff_t i, w, h, c, xm, ys;
	w = t->Albedo->Width2N;
	h = t->Albedo->Height2N;
	c = (ptrdiff_t)w * h;
	xm = w - 1;

	ys = 0;
	i = 1;
	while (h > i) { i <<= 1; ys++; }

#pragma omp parallel for
	for (i = 0; i < c; i++)
	{
		int x = (int)(i & xm),
			y = (int)(i >> ys);
		float
			ScaledXM = (float)((x - 1) * t->AlbedoScale),
			ScaledX0 = (float)(x * t->AlbedoScale),
			ScaledXP = (float)((x + 1) * t->AlbedoScale),
			ScaledYM = (float)((y - 1) * t->AlbedoScale),
			ScaledY0 = (float)(y * t->AlbedoScale),
			ScaledYP = (float)((y + 1) * t->AlbedoScale);

		union ColorARGB
		{
			uint8_t u8[4];
			uint32_t u32;
		}Pixel;

		float x_diff, z_diff;
		float rx, gx, bx;
		vec4_t normal;

		x_diff = RayMap_GetAltitude(t->RayMap, ScaledXM, ScaledY0) - RayMap_GetAltitude(t->RayMap, ScaledXP, ScaledY0);
		z_diff = RayMap_GetAltitude(t->RayMap, ScaledX0, ScaledYM) - RayMap_GetAltitude(t->RayMap, ScaledX0, ScaledYP);
		normal = vec4(x_diff, 2.0f, z_diff, 0);
		normal = vec4_normalize(normal);
		normal = vec4_flushcomp(normal);

		Pixel.u32 = ImgBuffer_FetchU32(t->Albedo, x, y);
		bx = (float)Pixel.u8[0] * normal.y;
		gx = (float)Pixel.u8[1] * normal.y;
		rx = (float)Pixel.u8[2] * normal.y;
		ImgBuffer_FetchU32(t->Albedo, x, y) = ARGBSafe((int32_t)rx, (int32_t)gx, (int32_t)bx, 255);
	}
}

static RayMap_p CreateWalkMap(ImgBuffer_p Altitude, char *AssetsDirectory, char *WalkAltitudeMap, char *WalkKFile, int WalkerRadius, FILE *fp_log)
{
	ImgBuffer_p WalkAlt = NULL;
	RayMap_p RM = NULL;
	int y;
	size_t i, count;
	int WalkAreaSize;
	int64_t RadiusSq = (int64_t)WalkerRadius * WalkerRadius;
	uint32_t RW, RH;
	uint32_t XM, YM;
	FILE *fp = NULL;
	char buf[1024];
	struct Point
	{
		int x, y;
	} *Points;

	WalkAreaSize = WalkerRadius + WalkerRadius + 1;
	XM = Altitude->Width2N - 1;
	YM = Altitude->Height2N - 1;
	WalkAlt = ImgBuffer_Create(Altitude->Width2N, Altitude->Height2N, 4, 4);
	if (!WalkAlt)
	{
		log_printf(fp_log, "Could not create altitude buffer, maybe: '%s'\n", strerror(errno));
		return 0;
	}

	snprintf(buf, sizeof buf, "%s\\%s", AssetsDirectory, WalkAltitudeMap);
	fp = fopen(buf, "rb");
	if (fp &&
		fread(&RW, sizeof RW, 1, fp) &&
		fread(&RH, sizeof RH, 1, fp) &&
		fread(WalkAlt->Buffer, WalkAlt->BufferSize, 1, fp) &&
		RW == WalkAlt->Width &&
		RH == WalkAlt->Height)
	{
		fclose(fp);
	}
	else
	{
		log_printf(fp_log, "Unable to load walk altitude file from: '%s', trying to generate new.\n", buf);

		Points = malloc((size_t)WalkAreaSize * WalkAreaSize * sizeof Points[0]);
		if (!Points)
		{
			log_printf(fp_log, "Could not create altitude buffer search points: '%s'\n", strerror(errno));
			return 0;
		}

		count = 0;
#pragma omp parallel for
		for (y = -WalkerRadius; y <= WalkerRadius; y++)
		{
			int x;
			for (x = -WalkerRadius; x <= WalkerRadius; x++)
			{
				int64_t dist = (int64_t)x * x + (int64_t)y * y;
				if (dist <= RadiusSq)
				{
					Points[count].x = x;
					Points[count].y = y;
#pragma omp atomic
					count++;
				}
			}
		}

#pragma omp parallel for
		for (y = 0; y < WalkAlt->Height2N; y++)
		{
			int x;
			for (x = 0; x < WalkAlt->Width2N; x++)
			{
				float highest = 0;

				for (i = 0; i < count; i++)
				{
					float alt;
					int sub_x, sub_y;

					sub_x = x + Points[i].x;
					sub_y = y + Points[i].y;
					alt = ImgBuffer_FetchF32(Altitude, (uint32_t)sub_x & XM, (uint32_t)sub_y & YM);
					highest = r_max(highest, alt);
				}

				ImgBuffer_FetchF32(WalkAlt, x, y) = highest;
			}
		}
		free(Points); Points = NULL;
	}

	snprintf(buf, sizeof buf, "%s\\%s", AssetsDirectory, WalkAltitudeMap);
	fp = fopen(buf, "wb");
	if (!fp ||
		!fwrite(&WalkAlt->Width, sizeof WalkAlt->Width, 1, fp) ||
		!fwrite(&WalkAlt->Height, sizeof WalkAlt->Height, 1, fp) ||
		!fwrite(WalkAlt->Buffer, WalkAlt->BufferSize, 1, fp))
	{
		log_printf(fp_log, "Could not write walk altitude file: '%s'\n", buf);
		if (fp) fclose(fp); fp = NULL;
		goto FailExit;
	}
	fclose(fp); fp = NULL;

	RM = RayMap_CreateFromAltitude(WalkAlt, AssetsDirectory, WalkAltitudeMap, WalkKFile, fp_log);
	return RM;
FailExit:
	free(Points);
	if (RM) RayMap_Unload(RM);
	else
	{
		ImgBuffer_Destroy(WalkAlt);
	}
}

static int LoadWalkMap(Game_p t, char *AssetsDirectory, char *WalkAltitudeMap, char *WalkKFile, int WalkerRadius, FILE *fp_log)
{
	t->WalkMap = RayMap_Load(AssetsDirectory, WalkAltitudeMap, WalkKFile, fp_log);
	if (t->WalkMap) return 1;

	t->WalkMap = CreateWalkMap(t->RayMap->Altitude, AssetsDirectory, WalkAltitudeMap, WalkKFile, WalkerRadius, fp_log);
	if (!t->WalkMap) return 0;

	return 1;
FailExit:
	return 0;
}

static int LoadMap(Game_p t, const char *MapDir)
{
	char StrBuf[1024];
	dict_p d_landview = NULL;
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

	t->RayMap = RayMap_LoadFromRaw(MapDir,
		dictcfg_getstr(d_landview, "altitude", "altitude.bin"),
		dictcfg_getstr(d_landview, "raw_altitude", "altitude.bmp"),
		dictcfg_getstr(d_landview, "k", "k.bin"),
		dictcfg_getint(d_landview, "raw_altitude_blur", 0),
		fp_log);
	if (!t->RayMap) goto FailExit;
	if (!LoadWalkMap(t, MapDir,
		dictcfg_getstr(d_landview, "walk_altitude", "walk_altitude.bin"),
		dictcfg_getstr(d_landview, "walk_k", "walk_k.bin"),
		t->Player_Size,
		fp_log)) goto FailExit;

	PreRenderLighting(t);

	t->RenderDist = (float)dictcfg_getfloat(d_landview, "render_dist", 256);
	t->IterCount = dictcfg_getint(d_landview, "iter_count", 64);

	dict_delete(MapCfg);
	return 1;

FailExit:
	dict_delete(MapCfg);
	return 0;
}

Game_p Game_Create(CPUCan_p CPUCan, const char *WorkDir, const char *MapDir, dict_p Config)
{
	char StrBuf[1024];
	Game_p t = NULL;
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

	t->Aspect = (float)CPUCan->Width / CPUCan->Height;

	fclose(fp_log);
	return t;
FailExit:
	if (fp_log) fclose(fp_log);
	Game_Free(t);
	return NULL;
}

void Game_SetCamera(Game_p t, vec4_t CamPos, mat4_t CamRotMat, float FOV)
{
	t->CameraPos = CamPos;
	t->CamRotMat = CamRotMat;
	t->FOV = FOV;
}

float Game_GetAltitude(Game_p t, float x, float z)
{
	if (!t) return 0;
	return RayMap_GetAltitude(t->RayMap, x, z);
}

static void RenderPixel(Game_p t, float RayXScale, float RayYScale, uint32_t x, uint32_t y)
{
	vec4_t RayDir;
	vec4_t CastPoint;
	float CastDist;
	uint32_t Color = 0;

	RayDir = vec4(
		((float)x / t->LandView->Width - 0.5f) * RayXScale,
		-((float)y / t->LandView->Height - 0.5f) * RayYScale, 1, 0);
	RayDir = vec4_normalize(RayDir);
	RayDir = vec4_mul_mat4(RayDir, t->CamRotMat);
	RayDir = vec4_flushcomp(RayDir);

	if (RayMap_Raycast(t->RayMap, t->CameraPos, RayDir, t->IterCount, &CastPoint, &CastDist, t->RenderDist))
	{
		Color = ARGB_Lerp(Map_GetAlbedo(t, CastPoint.x, CastPoint.z),
			t->skycolor2, CastDist / t->RenderDist);
	}
	else
	{
		Color = ARGB_Lerp(t->skycolor2, t->skycolor1, r_abs(RayDir.y));
	}

	ImgBuffer_FetchU32(t->LandView, x, y) = Color;
}

static void RenderTaskedPerPixel(Game_p t)
{
	ptrdiff_t i;
	ptrdiff_t PixelCount;
	size_t w, h;
	float RayXScale, RayYScale;
	ImgBuffer_p ColorBuf;
	float FOV_Scale = r_sin(t->FOV * 0.5f);

	ColorBuf = t->CPUCan->ColorBuf;
	w = t->LandView->Width;
	h = t->LandView->Height;
	PixelCount = (ptrdiff_t)(w * h);

	RayXScale = t->Aspect * FOV_Scale;
	RayYScale = FOV_Scale;

#pragma omp parallel for
	for (i = 0; i < PixelCount; i++)
	{
		uint32_t x, y;
		x = (uint32_t)(i % w);
		y = (uint32_t)(i / w);

		if (t->interleave)
		{
			if (((x & 1) ^ (y & 1)) == (t->FrameCounter & 1))
				continue;
		}

		RenderPixel(t, RayXScale, RayYScale, x, y);
	}

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
		ImgBuffer_FetchU32(ColorBuf, x, y) = ImgBuffer_FetchU32(t->LandView, sx, sy);
	}
}

static void RenderTaskedPerLine(Game_p t)
{
	ptrdiff_t y;
	size_t w, h;
	float RayXScale, RayYScale;
	ImgBuffer_p ColorBuf;
	float FOV_Scale = r_sin(t->FOV * 0.5f);

	ColorBuf = t->CPUCan->ColorBuf;
	w = t->LandView->Width;
	h = t->LandView->Height;

	RayXScale = t->Aspect * FOV_Scale;
	RayYScale = FOV_Scale;

#pragma omp parallel for
	for (y = 0; y < (int32_t)h; y++)
	{
		ptrdiff_t x;
		for (x = 0; x < (int32_t)w; x++)
		{
			if (t->interleave)
			{
				if (((x & 1) ^ (y & 1)) == (t->FrameCounter & 1))
					continue;
			}

			RenderPixel(t, RayXScale, RayYScale, (uint32_t)x, (uint32_t)y);
		}
	}

	w = ColorBuf->Width;
	h = ColorBuf->Height;
#pragma omp parallel for
	for (y = 0; y < (int32_t)h; y++)
	{
		ptrdiff_t x;
		for (x = 0; x < (int32_t)w; x++)
		{
			ptrdiff_t sx, sy;
			sx = x / t->x_scale;
			sy = y / t->y_scale;
			ImgBuffer_FetchU32(ColorBuf, x, y) = ImgBuffer_FetchU32(t->LandView, sx, sy);
		}
	}
}

void Game_KBDInput(Game_p t, Game_Input_t key, int status)
{
	if (!t) return;

	t->KBD_Status[key] = status;
}

void Game_FPSInput(Game_p t, int delta_x, int delta_y)
{
	const float HalfPi = r_pi * 0.5f;
	if (!t) return;

	t->Yaw += (float)delta_x * r_pi / 180.0f * t->Sensitivity * 0.1f;
	t->Pitch += (float)delta_y * r_pi / 180.0f * t->Sensitivity * 0.1f;
	t->Pitch = r_clamp(t->Pitch, -HalfPi, HalfPi);
}

void Game_Update(Game_p t, double Time)
{
	if (!t) return;

	Game_SetCamera(t,
		vec4(0, t->Player_Height + RayMap_GetAltitude(t->WalkMap, 0, 0), 0, 0),
		mat4_rot_euler(t->Yaw, t->Pitch, t->Roll),
		0.5f * r_pi);
}

void Game_Render(Game_p t)
{
	if (!t) return;

	if (t->profile_task_per_line)
	{
		RenderTaskedPerLine(t);
	}
	else if (t->profile_task_per_pixel)
	{
		RenderTaskedPerPixel(t);
	}

	t->FrameCounter++;
}

void Game_Free(Game_p t)
{
	if (!t) return;

	if (t->fp_log) fclose(t->fp_log);

	if (t->Albedo) CPUCan_DeleteTexture(t->CPUCan, Name_Game_Albedo);
	RayMap_Unload(t->RayMap);
	RayMap_Unload(t->WalkMap);
	free(t);
}




