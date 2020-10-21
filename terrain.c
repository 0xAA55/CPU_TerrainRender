#include "terrain.h"

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdarg.h>
#include<time.h>
#include<ctype.h>
#include<math.h>

static const char *Name_Terrain_Albedo = "Terrain_Albedo";
static const char *Name_Terrain_Altitude = "Terrain_Altitude";
static const char *Name_Terrain_K = "Terrain_K";

static void ConfigDictOnRemove(void *value)
{
	dict_delete(value);
}

void log_printf(FILE *fp, const char *Format, ...)
{
	char Buffer[64];
	time_t t = time(NULL);
	struct tm *hms = localtime(&t);
	va_list ap;
	va_start(ap, Format);
	strftime(Buffer, sizeof Buffer, "[%H:%M:%S] ", hms);
	fputs(Buffer, fp);
	vfprintf(fp, Format, ap);
	va_end(ap);
}

static int issym(int chr)
{
	return
		(chr >= 'A' && chr <= 'Z') ||
		(chr >= 'a' && chr <= 'z') ||
		(chr >= '0' && chr <= '9') ||
		chr == '_' ||
		chr >= 128;
}

static char *AllocCopy(const char *Str)
{
	size_t l = strlen(Str);
	char *NewAlloc = malloc(l + 1);
	if (!NewAlloc) return NULL;
	memcpy(NewAlloc, Str, l + 1);
	return NewAlloc;
}

static dict_p LoadConfig(const char *cfg_path, FILE *fp_log)
{
	dict_p d_cfg = NULL;
	dict_p d_sub = NULL;
	FILE *fp = NULL;
	size_t LineNo = 0;
	
	fp = fopen(cfg_path, "r");
	if (!fp) goto FailExit;

	d_cfg = dict_create();
	if (!d_cfg)
	{
		log_printf(fp_log, "Create new dictionary failed.\n");
		goto FailExit;
	}

	while (!feof(fp))
	{
		char LineBuf[256];
		char *ch = &LineBuf[0];
		char *ch2, *ch3;
		LineNo++;
		if(!fgets(LineBuf, sizeof LineBuf, fp)) break;

		while (isspace(*ch)) ch++;

		switch (ch[0])
		{
		case ';':
		case '#':
		case '\0': continue;
		case '[':
			ch2 = strchr(ch + 1, ']');
			if (!ch2)
			{
				log_printf(fp_log, "Line %zu: '%s': ']' expected.\n", LineNo, ch);
				goto FailExit;
			}
			ch2[1] = '\0';
			d_sub = dict_create();
			if (!d_sub)
			{
				log_printf(fp_log, "Line %zu: Create new sub dictionary failed.\n", LineNo);
				goto FailExit;
			}
			switch (dict_insert(d_cfg, ch, d_sub))
			{
			case ds_ok: break;
			case ds_nomemory:
				log_printf(fp_log, "Line %zu: No enough memory.\n", LineNo);
				dict_delete(d_sub);
				goto FailExit;
			case ds_alreadyexists:
				log_printf(fp_log, "Line %zu: '%s' already defined.\n", LineNo, ch);
				dict_delete(d_sub);
				goto FailExit;
			default:
				log_printf(fp_log, "Line %zu: dictionary error.\n", LineNo);
				dict_delete(d_sub);
				goto FailExit;
			}
			break;
		default:
			if (!d_sub)
			{
				log_printf(fp_log, "Line %zu: '[' expected.\n", LineNo);
				goto FailExit;
			}
			ch2 = ch + 1;
			while (issym(*ch2)) ch2++;
			while (isspace(*ch2)) *ch2++ = '\0';
			if (*ch2 != '=')
			{
				log_printf(fp_log, "Line %zu: '%s': '=' expected.\n", LineNo, ch2);
				goto FailExit;
			}
			*ch2++ = '\0';
			while (isspace(*ch2)) ch2++;

			ch3 = strchr(ch2, '#'); if (ch3) *ch3 = '\0';
			ch3 = strchr(ch2, ';'); if (ch3) *ch3 = '\0';
			ch3 = strchr(ch2, '\n'); if (ch3) *ch3 = '\0';
			ch2 = AllocCopy(ch2);
			if (!ch2)
			{
				log_printf(fp_log, "Line %zu: No enough memory.\n", LineNo);
				goto FailExit;
			}
			switch (dict_insert(d_sub, ch, ch2))
			{
			case ds_ok: break;
			case ds_nomemory:
				log_printf(fp_log, "Line %zu: No enough memory.\n", LineNo);
				goto FailExit;
			case ds_alreadyexists:
				log_printf(fp_log, "Line %zu: '%s' already assigned.\n", LineNo, ch2);
				free(ch2);
				goto FailExit;
			default:
				log_printf(fp_log, "Line %zu: dictionary error.\n", LineNo);
				free(ch2);
				goto FailExit;
			}
			break;
		}
	}

	fclose(fp);
	fclose(fp_log);

	return d_cfg;
FailExit:
	dict_delete(d_cfg);
	if (fp) fclose(fp);
	if (fp_log) fclose(fp_log);
	return NULL;
}

static int GetAlbedo(Terrain_p t, dict_p d_render, FILE *fp_log, const char *path)
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

static int GetAltitude(Terrain_p t, dict_p d_render, FILE *fp_log, const char *path)
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

static int GetAltitudeFromRaw(Terrain_p t, dict_p d_render, FILE *fp_log, const char *dir, const char *raw_file, const char *file)
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

static int GetK(Terrain_p t, dict_p d_render, FILE *fp_log, const char *Path)
{
	uint32_t w, h;
	FILE *fp = NULL;
	ImgBuffer_p K = NULL;

	fp = fopen(Path, "r");
	if (!fp) goto FailExit;

	if (!fread(&w, sizeof w, 1, fp)) goto FailExit;
	if (!fread(&h, sizeof h, 1, fp)) goto FailExit;

	K = ImgBuffer_Create(w, h, 4, 4);
	if (!K) goto FailExit;

	if (!fread(K->Buffer, K->BufferSize, 1, fp)) goto FailExit;

	if (!CPUCan_SetTexture(t->CPUCan, Name_Terrain_K, K)) goto FailExit;
	t->K = K;

	fclose(fp);
	return 1;
FailExit:
	log_printf(fp_log, "Could not load altitude file: '%s'\n", Path);
	if (fp) fclose(fp);
	ImgBuffer_Destroy(K);
	return 0;
}

static int GenerateK(Terrain_p t, dict_p d_render, FILE *fp_log, const char *Path)
{
	uint32_t w, h;
	ImgBuffer_p Altitude = t->Altitude;
	ImgBuffer_p K1 = NULL, K2 = NULL;
	ptrdiff_t i, AllPix;
	size_t calc_count = 0;
	FILE *fp = NULL;
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
			cur_y = (size_t)i / h;
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
				cur_y = (size_t)i / h;
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
		!fwrite(&Altitude->Width, sizeof Altitude->Width, 1, fp) ||
		!fwrite(&Altitude->Height, sizeof Altitude->Height, 1, fp) ||
		!fwrite(Altitude->Buffer, Altitude->BufferSize, 1, fp))
	{
		log_printf(fp_log, "Could not write generated K file: '%s'\n", Path);
		goto FailExit;
	}
	fclose(fp);

	ImgBuffer_Destroy(K2);
	return 1;
FailExit:
	if (fp) fclose(fp);
	ImgBuffer_Destroy(K1);
	ImgBuffer_Destroy(K2);
	return 0;
}

Terrain_p Terrain_Create(CPUCan_p CPUCan, const char *dir)
{
	char StrBuf[1024];
	Terrain_p t = NULL;
	FILE *fp_log = NULL;
	dict_p d_render = NULL;
	char *val = NULL;
	char *val2 = NULL;

	if (!CPUCan || !dir) return t;

	fp_log = fopen("terrain.log", "a");
	if (!fp_log) goto FailExit;

	t = malloc(sizeof t[0]);
	if (!t) goto FailExit;
	memset(t, 0, sizeof t[0]);

	t->CPUCan = CPUCan;

	snprintf(StrBuf, sizeof StrBuf, "%s\\meta.ini", dir);
	t->Config = LoadConfig(StrBuf, fp_log);
	if (!t->Config) goto FailExit;

	d_render = dict_search(t->Config, "[render]");
	if (!d_render)
	{
		log_printf(fp_log, "'[render]' not defined in config file.\n");
		goto FailExit;
	}

	val = dict_search(d_render, "albedo");
	if (!val) val = "albedo.bmp";
	snprintf(StrBuf, sizeof StrBuf, "%s\\%s", dir, val);
	if (!GetAlbedo(t, d_render, fp_log, StrBuf)) goto FailExit;

	val = dict_search(d_render, "albedo_scale");
	if (!val) val = "1";
	t->AlbedoScale = atof(val);

	val = dict_search(d_render, "altitude_scale");
	if (!val) val = "1";
	t->AltitudeScale = atof(val);

	val = dict_search(d_render, "raw_altitude_blur");
	if (!val) val = "1";
	t->RawAltitudeBlur = atoi(val);
	if (t->RawAltitudeBlur <= 0)
	{
		log_printf(fp_log, "Invalid raw_altitude_blur value '%d', should be > 0.\n", t->RawAltitudeBlur);
		goto FailExit;
	}

	val = dict_search(d_render, "altitude");
	if (!val) val = "altitude.bin";
	snprintf(StrBuf, sizeof StrBuf, "%s\\%s", dir, val);
	if (!GetAltitude(t, d_render, fp_log, StrBuf))
	{
		log_printf(fp_log, "Could not load altitude file: '%s', trying to generate from raw.\n", StrBuf);
		val2 = dict_search(d_render, "raw_altitude");
		if (!val2) val2 = "altitude.bmp";
		if (!GetAltitudeFromRaw(t, d_render, fp_log, dir, val2, val))
		{
			goto FailExit;
		}
	}

	val = dict_search(d_render, "k");
	if (!val) val = "k.bin";
	snprintf(StrBuf, sizeof StrBuf, "%s\\%s", dir, val);
	if (!GetK(t, d_render, fp_log, StrBuf))
	{
		if (!GenerateK(t, d_render, fp_log, StrBuf))
		{
			goto FailExit;
		}
	}

	fclose(fp_log);
	return t;
FailExit:
	if (fp_log) fclose(fp_log);
	Terrain_Free(t);
	return NULL;
}

void Terrain_Render(Terrain_p t)
{

}

void Terrain_Free(Terrain_p t)
{
	if (!t) return;

	if (t->Albedo) CPUCan_DeleteTexture(t->CPUCan, Name_Terrain_Albedo);
	if (t->Altitude) CPUCan_DeleteTexture(t->CPUCan, Name_Terrain_Altitude);
	if (t->K) CPUCan_DeleteTexture(t->CPUCan, Name_Terrain_K);
	
	dict_delete(t->Config);
	free(t);
}




