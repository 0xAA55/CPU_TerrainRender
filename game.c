#include"game.h"
#include"dictcfg.h"
#include"logprintf.h"

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<math.h>
#include<errno.h>
#include<assert.h>

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

static int GetAlbedo(Game_p g, dict_p d_landview, FILE *fp_log, const char *path)
{
	if (!CPUCan_LoadTextureFromFile(g->CPUCan, path, Name_Game_Albedo))
	{
		log_printf(fp_log, "Could not load '%s'.\n", path);
		return 0;
	}
	g->Albedo = CPUCan_GetTexture(g->CPUCan, Name_Game_Albedo);
	if (!g->Albedo)
	{
		log_printf(fp_log, "Could not get texture '%s'.\n", Name_Game_Albedo);
		return 0;
	}
	if (!ImgBuffer_To2N(g->Albedo, RGBAGrowInterpolate, RGBAShrinkToPixel))
	{
		log_printf(fp_log, "Could not interpolate texture '%s' size to 2^N, use scissored texture.\n", Name_Game_Albedo);
	}
	return 1;
}

static vec4_t parse_vector(const char *str_vector)
{
	double x = 0, y = 0, z = 0, w = 1;
	int parsed = sscanf(str_vector, "%lf,%lf,%lf,%lf", &x, &y, &z, &w);
	return vec4((real_t)x, (real_t)y, (real_t)z, (real_t)w);
}

static int LoadCfg(Game_p g, dict_p Config)
{
	dict_p d_general = NULL;
	dict_p d_player = NULL;
	dict_p d_input = NULL;
	dict_p d_render = NULL;
	dict_p d_profile = NULL;
	FILE *fp_log = g->fp_log;

	d_general = dictcfg_section(Config, "[general]");
	/*
	if (!d_general)
	{
		log_printf(fp_log, "'[input]' section not defined in 'config.cfg' file.\n");
		goto FailExit;
	}*/
	g->Gravity = parse_vector(dictcfg_getstr( d_general, "default_gravity", "0,-9,0"));

	d_player = dictcfg_section(Config, "[player]");
	/*
	if (!d_player)
	{
		log_printf(fp_log, "'[input]' section not defined in 'config.cfg' file.\n");
		goto FailExit;
	}*/
	g->Player_Size = (float)dictcfg_getfloat(d_player, "size", 30.0);
	g->Player_Height = (float)dictcfg_getfloat(d_player, "height", 165.0) * 0.5f;
	g->Player_MinHeight = (float)dictcfg_getfloat(d_player, "min_height", 60.0) * 0.5f;
	g->Player_MaxHeight = (float)dictcfg_getfloat(d_player, "max_height", 175.0) * 0.5f;
	g->Player_CrouchHeight = (float)dictcfg_getfloat(d_player, "crouch_height", 80.0) * 0.5f;
	g->Player_StandingDamp = (float)dictcfg_getfloat(d_player, "standing_damp", 0.05);
	g->Player_Speed = (float)dictcfg_getfloat(d_player, "speed", 1000.0);
	g->Player_Accel = (float)dictcfg_getfloat(d_player, "accel", 1000.0);
	g->Player_Jump = (float)dictcfg_getfloat(d_player, "jump", 200.0);
	g->Player_Friction = (float)dictcfg_getfloat(d_player, "friction", 100.0);
	g->Player_FlyingSpeed = (float)dictcfg_getfloat(d_player, "flying_speed", 100.0);
	g->Player_SpeedDamp = (float)dictcfg_getfloat(d_player, "speed_damp", 0.9);
	g->Player_BobbingRate = (float)dictcfg_getfloat(d_player, "bobbing_rate", 0.001);
	g->Player_BobbingAmplitude = (float)dictcfg_getfloat(d_player, "bobbing_amplitude", 5) * 0.5f;

	d_input = dictcfg_section(Config, "[input]");
	/*
	if (!d_input)
	{
		log_printf(fp_log, "'[input]' section not defined in 'config.cfg' file.\n");
		goto FailExit;
	}*/
	g->Sensitivity = (float)dictcfg_getfloat(d_input, "sensitivity", 1.0);

	d_profile = dictcfg_section(Config, "[profile]");
	/*
	if (!d_profile)
	{
		log_printf(fp_log, "'[profile]' section not defined in 'config.cfg' file.\n");
		goto FailExit;
	}*/
	g->profile_task_per_line = dictcfg_getint(d_profile, "task_per_line", 1);
	g->profile_task_per_pixel = dictcfg_getint(d_profile, "task_per_pixel", !g->profile_task_per_line);
	if ((g->profile_task_per_line && g->profile_task_per_pixel) ||
		(!g->profile_task_per_line && !g->profile_task_per_pixel))
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
	g->XRes = dictcfg_getint(d_render, "x_res", g->CPUCan->Width);
	g->YRes = dictcfg_getint(d_render, "y_res", g->CPUCan->Height);
	g->Interleave = dictcfg_getint(d_render, "interleave", 0);
	g->XScaling = dictcfg_getint(d_render, "x_scale", 4);
	g->YScaling = dictcfg_getint(d_render, "y_scale", 1);
	g->Interpolate = dictcfg_getint(d_render, "interpolate", 1);
	g->Skycolor1 = ParseColorRGB(dictcfg_getstr(d_render, "skycolor1", "96,96,200"));
	g->Skycolor2 = ParseColorRGB(dictcfg_getstr(d_render, "skycolor2", "200,210,220"));

	g->LandView = ImgBuffer_Create(g->XRes / g->XScaling, g->YRes / g->YScaling, 4, 4);
	if (!g->LandView)
	{
		log_printf(fp_log, "Create landview buffer failed.\n");
		goto FailExit;
	}

	return 1;
FailExit:
	return 0;
}

static uint32_t Map_FetchAlbedo(Game_p g, int x, int y)
{
	int xm = g->Albedo->Width2N - 1;
	int ym = g->Albedo->Height2N - 1;
	return ImgBuffer_FetchU32(g->Albedo, x & xm, y & ym);
}

static uint32_t Map_GetAlbedo(Game_p g, float x, float y)
{
	x /= (float)g->AlbedoScale;
	y /= (float)g->AlbedoScale;
	if (g->Interpolate)
		return CPUCan_SampleTexture(g->Albedo, x, y);
	else
	{
		real_t fx = r_floor(x);
		real_t fy = r_floor(y);
		uint32_t ux = (uint32_t)fx;
		uint32_t uy = (uint32_t)fy;
		uint32_t uw = g->Albedo->Width2N;
		uint32_t uh = g->Albedo->Height2N;
		return ImgBuffer_FetchU32(g->Albedo, ux & (uw - 1), uy & (uh - 1));
	}
}

static void PreRenderLighting(Game_p g)
{
	ptrdiff_t i, w, h, c, xm, ys;
	w = g->Albedo->Width2N;
	h = g->Albedo->Height2N;
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

		union ColorARGB
		{
			uint8_t u8[4];
			uint32_t u32;
		}Pixel;

		float rx, gx, bx;
		vec4_t normal;

		normal = RayMap_GetNormal(g->RayMap, x, y, (float)g->AlbedoScale);

		Pixel.u32 = ImgBuffer_FetchU32(g->Albedo, x, y);
		bx = (float)Pixel.u8[0] * normal.y;
		gx = (float)Pixel.u8[1] * normal.y;
		rx = (float)Pixel.u8[2] * normal.y;
		ImgBuffer_FetchU32(g->Albedo, x, y) = ARGBSafe((int32_t)rx, (int32_t)gx, (int32_t)bx, 255);
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
		for (y = 0; y < (int)WalkAlt->Height2N; y++)
		{
			int x;
			for (x = 0; x < (int)WalkAlt->Width2N; x++)
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

static int LoadWalkMap(Game_p g, char *AssetsDirectory, char *WalkAltitudeMap, char *WalkKFile, int WalkerRadius, FILE *fp_log)
{
	g->WalkMap = RayMap_Load(AssetsDirectory, WalkAltitudeMap, WalkKFile, fp_log);
	if (g->WalkMap) return 1;

	g->WalkMap = CreateWalkMap(g->RayMap->Altitude, AssetsDirectory, WalkAltitudeMap, WalkKFile, WalkerRadius, fp_log);
	if (!g->WalkMap) return 0;

	return 1;
}

static int LoadMap(Game_p g, const char *MapDir)
{
	char StrBuf[1024];
	dict_p d_landview = NULL;
	FILE *fp_log = g->fp_log;
	char *found = NULL;
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
	if (!GetAlbedo(g, d_landview, fp_log, StrBuf)) goto FailExit;

	g->AlbedoScale = dictcfg_getfloat(d_landview, "albedo_scale", 1);
	g->AltitudeScale = dictcfg_getfloat(d_landview, "altitude_scale", 1);

	g->RayMap = RayMap_LoadFromRaw((char*)MapDir,
		dictcfg_getstr(d_landview, "altitude", "altitude.bin"),
		dictcfg_getstr(d_landview, "raw_altitude", "altitude.bmp"),
		dictcfg_getstr(d_landview, "k", "k.bin"),
		dictcfg_getint(d_landview, "raw_altitude_blur", 0),
		fp_log);
	if (!g->RayMap) goto FailExit;
	if (!LoadWalkMap(g, (char*)MapDir,
		dictcfg_getstr(d_landview, "walk_altitude", "walk_altitude.bin"),
		dictcfg_getstr(d_landview, "walk_k", "walk_k.bin"),
		(int)g->Player_Size,
		fp_log)) goto FailExit;

	PreRenderLighting(g);

	g->RenderDist = (float)dictcfg_getfloat(d_landview, "render_dist", 256);
	g->IterCount = dictcfg_getint(d_landview, "iter_count", 64);

	found = dict_search(d_landview, "gravity");
	if (found) g->Gravity = parse_vector(found);

	dict_delete(MapCfg);
	return 1;

FailExit:
	dict_delete(MapCfg);
	return 0;
}

Game_p Game_Create(CPUCan_p CPUCan, const char *WorkDir, const char *MapDir, dict_p Config)
{
	char StrBuf[1024];
	Game_p g = NULL;
	FILE *fp_log = NULL;

	if (!CPUCan || !WorkDir || !MapDir) return g;

	snprintf(StrBuf, sizeof StrBuf, "%s\\terrain.log", WorkDir);
	fp_log = fopen(StrBuf, "a");
	if (!fp_log) goto FailExit;

	g = malloc(sizeof g[0]);
	if (!g) goto FailExit;
	memset(g, 0, sizeof g[0]);

	g->CPUCan = CPUCan;
	g->fp_log = fp_log;

	if (!LoadCfg(g, Config)) goto FailExit;
	if (!LoadMap(g, MapDir)) goto FailExit;

	g->Aspect = (float)CPUCan->Width / CPUCan->Height;

	fclose(fp_log);
	return g;
FailExit:
	if (fp_log) fclose(fp_log);
	Game_Free(g);
	return NULL;
}

void Game_SetCamera(Game_p g, vec4_t CamPos, mat4_t CamRotMat, float FOV)
{
	g->CameraPos = CamPos;
	g->CamRotMat = CamRotMat;
	g->FOV = FOV;
}

float Game_GetAltitude(Game_p g, float x, float z)
{
	if (!g) return 0;
	return RayMap_GetAltitude(g->RayMap, x, z);
}

static void RenderPixel(Game_p g, float RayXScale, float RayYScale, uint32_t x, uint32_t y)
{
	vec4_t RayDir;
	vec4_t CastPoint;
	float CastDist;
	uint32_t Color = 0;

	RayDir = vec4(
		((float)x / g->LandView->Width - 0.5f) * RayXScale,
		-((float)y / g->LandView->Height - 0.5f) * RayYScale, 1, 0);
	RayDir = vec4_normalize(RayDir);
	RayDir = vec4_mul_mat4(RayDir, g->CamRotMat);
	RayDir = vec4_flushcomp(RayDir);

	if (RayMap_Raycast(g->RayMap, g->CameraPos, RayDir, g->IterCount, &CastPoint, &CastDist, g->RenderDist))
	{
		Color = ARGB_Lerp(Map_GetAlbedo(g, CastPoint.x, CastPoint.z),
			g->Skycolor2, CastDist / g->RenderDist);
	}
	else
	{
		Color = ARGB_Lerp(g->Skycolor2, g->Skycolor1, r_abs(RayDir.y));
	}

	ImgBuffer_FetchU32(g->LandView, x, y) = Color;
}

static void RenderTaskedPerPixel(Game_p g)
{
	ptrdiff_t i;
	ptrdiff_t PixelCount;
	size_t w, h;
	float RayXScale, RayYScale;
	ImgBuffer_p ColorBuf;
	float FOV_Scale = r_sin(g->FOV * 0.5f);

	ColorBuf = g->CPUCan->ColorBuf;
	w = g->LandView->Width;
	h = g->LandView->Height;
	PixelCount = (ptrdiff_t)(w * h);

	RayXScale = g->Aspect * FOV_Scale;
	RayYScale = FOV_Scale;

#pragma omp parallel for
	for (i = 0; i < PixelCount; i++)
	{
		uint32_t x, y;
		x = (uint32_t)(i % w);
		y = (uint32_t)(i / w);

		if (g->Interleave)
		{
			if (((x & 1) ^ (y & 1)) == (g->FrameCounter & 1))
				continue;
		}

		RenderPixel(g, RayXScale, RayYScale, x, y);
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
		sx = x / g->XScaling;
		sy = y / g->YScaling;
		ImgBuffer_FetchU32(ColorBuf, x, y) = ImgBuffer_FetchU32(g->LandView, sx, sy);
	}
}

static void RenderTaskedPerLine(Game_p g)
{
	ptrdiff_t y;
	size_t w, h;
	float RayXScale, RayYScale;
	ImgBuffer_p ColorBuf;
	float FOV_Scale = r_sin(g->FOV * 0.5f);

	ColorBuf = g->CPUCan->ColorBuf;
	w = g->LandView->Width;
	h = g->LandView->Height;

	RayXScale = g->Aspect * FOV_Scale;
	RayYScale = FOV_Scale;

#pragma omp parallel for
	for (y = 0; y < (int32_t)h; y++)
	{
		ptrdiff_t x;
		for (x = 0; x < (int32_t)w; x++)
		{
			if (g->Interleave)
			{
				if (((x & 1) ^ (y & 1)) == (g->FrameCounter & 1))
					continue;
			}

			RenderPixel(g, RayXScale, RayYScale, (uint32_t)x, (uint32_t)y);
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
			sx = x / g->XScaling;
			sy = y / g->YScaling;
			ImgBuffer_FetchU32(ColorBuf, x, y) = ImgBuffer_FetchU32(g->LandView, sx, sy);
		}
	}
}

void Game_KBDInput(Game_p g, Game_Input_t key, int status)
{
	if (!g) return;

	g->KBD_Status[key] = status;
}

void Game_FPSInput(Game_p g, int delta_x, int delta_y)
{
	const float HalfPi = r_pi * 0.5f;
	if (!g) return;

	g->Yaw += (float)delta_x * r_pi / 180.0f * g->Sensitivity * 0.1f;
	g->Pitch += (float)delta_y * r_pi / 180.0f * g->Sensitivity * 0.1f;
	g->Pitch = r_clamp(g->Pitch, -HalfPi, HalfPi);
}

void Game_Update(Game_p g, double Time)
{
	float TouchingGroundY;
	float Altitude;
	double DeltaTime;
	vec4_t ForwardVector;
	vec4_t RightVector;
	vec4_t AccelVector = vec4(0, 0, 0, 0);
	vec4_t GroundNormal;
	float TargetSpeed;
	float Acceleration;
	int Walking = 0;
	vec4_t CamPos;
	vec4_t Bobbing_Dir;
	float Bobbing_Phase;
	float Bobbing_X, Bobbing_Y;
	float StandUpAccel = 0;

	if (!g) return;

	g->DeltaTime = DeltaTime = Time - g->LastTime;

	Altitude = RayMap_GetAltitude(g->WalkMap, g->Player_Pos.x, g->Player_Pos.z);
	TouchingGroundY = Altitude + g->Player_MaxHeight;

	g->Player_IsCrouching = g->KBD_Status[GI_Crouch];
	g->Player_IsJumping = g->KBD_Status[GI_Jump];

	g->Player_Vel = vec4_add(g->Player_Vel, vec4_scale(g->Gravity, (real_t)DeltaTime));
	g->Player_Pos = vec4_add(g->Player_Pos, vec4_scale(g->Player_Vel, (real_t)DeltaTime));

	ForwardVector = vec4_mul_mat4(vec4(0, 0, 1, 0), mat4_rot_y(g->Yaw));
	Acceleration = g->Player_Accel;
	if (g->Player_Pos.y <= TouchingGroundY)
	{
		float MinY = Altitude + g->Player_MinHeight;
		float TargetY = Altitude + (g->Player_IsCrouching ? g->Player_CrouchHeight : g->Player_Height);

		StandUpAccel = -g->Gravity.y;
		GroundNormal = RayMap_GetNormal(g->WalkMap, (int)g->Player_Pos.x, (int)g->Player_Pos.z, 1.0f);
		RightVector = vec4_cross3(GroundNormal, ForwardVector);
		ForwardVector = vec4_cross3(RightVector, GroundNormal);

		if (g->Player_Pos.y <= MinY)
		{
			g->Player_Pos.y = MinY;
			if (g->Player_Vel.y < 0)
			{
				g->Player_Vel = vec4_add(g->Player_Vel, vec4_scale(GroundNormal, -vec4_dot(GroundNormal, g->Player_Vel)));
			}
			StandUpAccel += Acceleration;
		}
		else if (g->Player_Pos.y <= TargetY)
		{
			StandUpAccel += r_min(Acceleration, (TargetY - g->Player_Pos.y) * 10.0f);
		}
		else
		{
			StandUpAccel += r_max(g->Gravity.y, (TargetY - g->Player_Pos.y) * 10.0f);
		}

		g->Player_Vel.y += StandUpAccel * (real_t)DeltaTime;

		if (g->Player_IsJumping)
			g->Player_Vel.y = g->Player_Jump;
		else
		{
			g->Player_Vel.y *= r_pow(g->Player_StandingDamp, (real_t)DeltaTime);
		}

		g->Player_IsTouchingGround = 1;
		TargetSpeed = g->Player_Speed;
	}
	else
	{
		TargetSpeed = g->Player_FlyingSpeed;
		RightVector = vec4_mul_mat4(vec4(1, 0, 0, 0), mat4_rot_y(g->Yaw));
		g->Player_IsTouchingGround = 0;
	}

	if (g->KBD_Status[GI_Forward]) { AccelVector = vec4_normalize(vec4_add(AccelVector, ForwardVector)); Walking = 1; }
	if (g->KBD_Status[GI_Backward]) { AccelVector = vec4_normalize(vec4_sub(AccelVector, ForwardVector)); Walking = 1; }
	if (g->KBD_Status[GI_Left]) { AccelVector = vec4_normalize(vec4_sub(AccelVector, RightVector)); Walking = 1; }
	if (g->KBD_Status[GI_Right]) { AccelVector = vec4_normalize(vec4_add(AccelVector, RightVector)); Walking = 1; }

	if (g->Player_IsTouchingGround)
	{
		vec4_t Direction, BiDir, Neutralized;
		float Speed, SpeedWithFriction;
		float Friction = g->Player_Friction;

		if (vec4_length(g->Player_Vel) <= 0.001f)
		{
			if (Friction >= r_epsilon) g->Player_Vel = vec4(0, 0, 0, 0);
		}
		else
		{
			BiDir = vec4_cross3(GroundNormal, g->Player_Vel);
			Direction = vec4_normalize(vec4_cross3(BiDir, GroundNormal));
			Speed = vec4_dot(g->Player_Vel, Direction);
			SpeedWithFriction = r_max(0, Speed - Friction * (real_t)DeltaTime);

			Neutralized = vec4_sub(g->Player_Vel, vec4_scale(Direction, Speed));
			g->Player_Vel = vec4_add(Neutralized, vec4_scale(Direction, SpeedWithFriction));

			g->Player_BobbingPhase = g->Player_BobbingPhase + Speed * g->Player_BobbingRate * (real_t)DeltaTime;
		}
	}

	if (Walking)
	{
		float Accel = 0;
		float CurSpeed;
		CurSpeed = vec4_dot(g->Player_Vel, AccelVector);
		if (CurSpeed < TargetSpeed) Accel = r_min(TargetSpeed - CurSpeed, Acceleration);
		else if (g->Player_IsTouchingGround) Accel = -r_min(CurSpeed - TargetSpeed, Acceleration);

		g->Player_Vel = vec4_add(g->Player_Vel, vec4_scale(AccelVector, Accel * (real_t)DeltaTime));
	}

	g->Player_Vel = vec4_scale(g->Player_Vel, r_pow(g->Player_SpeedDamp, (real_t)DeltaTime));

	Bobbing_Phase = g->Player_BobbingPhase * r_pi * 2;
	Bobbing_X = r_sin(Bobbing_Phase);
	Bobbing_Y = r_abs(r_cos(Bobbing_Phase)) * 2;
	Bobbing_Dir = vec4(r_cos(g->Yaw), 0, -r_sin(g->Yaw), 0);
	Bobbing_Dir = vec4_scale(Bobbing_Dir, Bobbing_X);
	Bobbing_Dir.y += Bobbing_Y;
	Bobbing_Dir = vec4_scale(Bobbing_Dir, g->Player_BobbingAmplitude);
	CamPos = vec4_add(g->Player_Pos, Bobbing_Dir);

	Game_SetCamera(g,
		vec4(CamPos.x, CamPos.y, CamPos.z, 0),
		mat4_rot_euler(g->Yaw, g->Pitch, g->Roll),
		0.5f * r_pi);

	g->LastTime = Time;
}

void Game_Render(Game_p g)
{
	if (!g) return;

	if (g->profile_task_per_line)
	{
		RenderTaskedPerLine(g);
	}
	else if (g->profile_task_per_pixel)
	{
		RenderTaskedPerPixel(g);
	}

	g->FrameCounter++;
}

void Game_Free(Game_p g)
{
	if (!g) return;

	if (g->fp_log) fclose(g->fp_log);

	if (g->Albedo) CPUCan_DeleteTexture(g->CPUCan, Name_Game_Albedo);
	RayMap_Unload(g->RayMap);
	RayMap_Unload(g->WalkMap);
	free(g);
}




