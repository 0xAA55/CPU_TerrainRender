#include"game.h"
#include"dictcfg.h"
#include"rttimer/rttimer.h"

#include<Windows.h>
#include<tchar.h>
#include<stdio.h>
#include<stdint.h>
#include<stdlib.h>

// 这个窗口里面咱想要带进去的数据
typedef struct AppData_struct
{
	// 窗口句柄
	HWND hWnd;

	// 窗口离屏缓冲区绘图句柄
	HDC BBDC;

	// 离屏缓冲区尺寸
	uint32_t BBWidth;
	uint32_t BBHeight;

	// 离屏缓冲区像素数据指针
	uint32_t *BBPixelData;

	FILE *fp_log;

	rttimer_t Tmr;
	CPUCan_p CPUCan;
	Game_p Game;

	dict_p Config;
	int KeyConfig[GI_KeyCount];
	float Sensitivity;
}AppData_t, * AppData_p;

static AppData_p App_Create(HWND hWnd);
static void App_Delete(AppData_p App);
static void App_Update(HWND hWnd);
static void App_Render(HWND hWnd);
static LRESULT CALLBACK App_WndProc(HWND hWnd, UINT Msg, WPARAM wp, LPARAM lp);

#define APP_UPDATE_TIMER 100

// 初始化渲染器信息
static AppData_p App_Create(HWND hWnd)
{
	HDC hDCTemp = NULL;
	HBITMAP DIB = NULL;
	RECT rw, rc;
	AppData_p App = NULL;
	int MapCount;
	char StrBuf[1024];
	struct
	{
		BITMAPINFOHEADER BMIF;
		uint32_t Bitfields[4];
	}BMIF32;
	int CfgXres, CfgYres;
	dict_p d_input = NULL;
	dict_p d_render = NULL;

	// 分配内存
	App = malloc(sizeof *App);
	if (!App) return NULL;
	memset(App, 0, sizeof * App);

	App->fp_log = fopen("app.log", "a");
	if (!App->fp_log) goto FailExit;
	App->hWnd = hWnd;
	App->Config = dictcfg_load("config.cfg", App->fp_log);
	if (!App->Config) goto FailExit;
	d_input = dictcfg_section(App->Config, "[input]");
	App->KeyConfig[GI_Forward] = dictcfg_getint(d_input, "forward", 'W');
	App->KeyConfig[GI_Backward] = dictcfg_getint(d_input, "backward", 'S');
	App->KeyConfig[GI_Left] = dictcfg_getint(d_input, "left", 'A');
	App->KeyConfig[GI_Right] = dictcfg_getint(d_input, "right", 'D');
	App->KeyConfig[GI_Jump] = dictcfg_getint(d_input, "jump", VK_SPACE);
	App->KeyConfig[GI_Crouch] = dictcfg_getint(d_input, "crouch", VK_LCONTROL);
	App->KeyConfig[GI_ShootGun] = dictcfg_getint(d_input, "shootgun", VK_LBUTTON);
	App->KeyConfig[GI_ReloadGun] = dictcfg_getint(d_input, "reloadgun", 'R');
	App->KeyConfig[GI_Use] = dictcfg_getint(d_input, "use", 'E');
	App->KeyConfig[GI_Exit] = dictcfg_getint(d_input, "exit", VK_ESCAPE);

	d_render = dictcfg_section(App->Config, "[render]");
	CfgXres = dictcfg_getint(d_render, "x_res", 1024);
	CfgYres = dictcfg_getint(d_render, "y_res", 768);
	MapCount = dictcfg_getint(d_render, "map_count", 1);

	// 按照设置来配置窗口大小
	GetWindowRect(hWnd, &rw);
	GetClientRect(hWnd, &rc);
	MoveWindow(hWnd,
		rw.left,
		rw.top,
		CfgXres + ((rw.right - rw.left) - (rc.right - rc.left)),
		CfgYres + ((rw.bottom - rw.top) - (rc.bottom - rc.top)),
		FALSE);
	App->BBWidth = CfgXres;
	App->BBHeight = CfgYres;
	if ((int32_t)App->BBWidth <= 0 || (int32_t)App->BBHeight <= 0) goto FailExit;

	// 填写位图信息
	memset(&BMIF32, 0, sizeof BMIF32);
	BMIF32.BMIF.biSize = sizeof BMIF32.BMIF;
	BMIF32.BMIF.biWidth = (LONG)App->BBWidth;
	BMIF32.BMIF.biHeight = -(LONG)App->BBHeight;
	BMIF32.BMIF.biPlanes = 1;
	BMIF32.BMIF.biBitCount = 32;
	BMIF32.BMIF.biCompression = BI_BITFIELDS;
	BMIF32.BMIF.biSizeImage = (DWORD)(App->BBWidth * App->BBHeight * 4);
	BMIF32.Bitfields[0] = 0x00FF0000; // 红色部分
	BMIF32.Bitfields[1] = 0x0000FF00; // 绿色部分
	BMIF32.Bitfields[2] = 0x000000FF; // 蓝色部分
	BMIF32.Bitfields[3] = 0xFF000000; // 透明层部分，如果有的话

	// 创建内存DC用于绘图
	hDCTemp = GetDC(hWnd);
	if(!hDCTemp) goto FailExit;
	App->BBDC = CreateCompatibleDC(hDCTemp);
	if (!App->BBDC) goto FailExit;

	// 建立内存位图用作后台缓冲区
	DIB = CreateDIBSection(hDCTemp, (const BITMAPINFO*)&BMIF32, DIB_RGB_COLORS, (void**)&App->BBPixelData, NULL, 0);
	if (!DIB) goto FailExit;
	ReleaseDC(hWnd, hDCTemp); hDCTemp = NULL;

	// 选入内存位图
	SelectObject(App->BBDC, DIB);
	DeleteObject(DIB); // 解引用

	App->CPUCan = CPUCan_CreateWithRGBAFB(App->BBWidth, App->BBHeight, App->BBPixelData);
	if (!App->CPUCan) goto FailExit;

	srand(GetTickCount64());

	snprintf(StrBuf, sizeof StrBuf, "maps\\%d", (rand() % MapCount) + 1);
	App->Game = Game_Create(App->CPUCan, ".", StrBuf, App->Config);
	if (!App->Game) goto FailExit;

	rttimer_init(&App->Tmr, 0);
	SetTimer(hWnd, APP_UPDATE_TIMER, 1, NULL);

	return App;
FailExit:
	App_Delete(App);
	if (hDCTemp) ReleaseDC(hWnd, hDCTemp);
	return NULL;
}

// 释放渲染器信息
static void App_Delete(AppData_p App)
{
	if (!App) return;
	dict_delete(App->Config);
	Game_Free(App->Game);
	CPUCan_Delete(App->CPUCan);
	DeleteDC(App->BBDC);
	if (App->fp_log) fclose(App->fp_log);
	free(App);
}

//进行FPS输入
static void App_Update(HWND hWnd)
{
	double Time;
	int i;
	AppData_p App = NULL;
	RECT rc;
	POINT cursor_pos;
	POINT center_pos;
	
	App = (AppData_p)GetWindowLongPtr(hWnd, 0);
	if (!App) return;

	Time = rttimer_gettime(&App->Tmr);

	if (GetForegroundWindow() == hWnd)
	{
		GetWindowRect(hWnd, &rc);
		GetCursorPos(&cursor_pos);
		center_pos.x = (rc.right + rc.left) / 2;
		center_pos.y = (rc.bottom + rc.top) / 2;
		SetCursorPos(center_pos.x, center_pos.y);

		for (i = 0; i < GI_KeyCount; i++)
		{
			Game_KBDInput(App->Game, i, GetAsyncKeyState(App->KeyConfig[i]));
		}

		Game_FPSInput(App->Game,
			cursor_pos.x - center_pos.x,
			cursor_pos.y - center_pos.y);
	}

	Game_Update(App->Game, Time);
}

// 进行渲染
static void App_Render(HWND hWnd)
{
	AppData_p App = (AppData_p)GetWindowLongPtr(hWnd, 0);
	if (!App) return;

	Game_Render(App->Game);
}

// 消息处理过程
static LRESULT CALLBACK App_WndProc(HWND hWnd, UINT Msg, WPARAM wp, LPARAM lp)
{
	// 当前窗口实例数据
	AppData_p Inst = NULL;
	HDC hDCPaint;
	PAINTSTRUCT ps;
	switch (Msg)
	{
	case WM_CREATE: // 创建窗口
		// 创建新的实例数据并绑定到窗口上
		Inst = App_Create(hWnd);
		if (!Inst) return -1;
		SetWindowLongPtr(hWnd, 0, (LONG_PTR)Inst);
		return 0;

	case WM_TIMER:
		switch (wp)
		{
		case APP_UPDATE_TIMER:
			App_Update(hWnd);
			break;
		}
		return 0;

	case WM_PAINT: // 绘制窗口
		Inst = (AppData_p)GetWindowLongPtr(hWnd, 0);
		if (!Inst) return DefWindowProc(hWnd, Msg, wp, lp);
		hDCPaint = BeginPaint(hWnd, &ps);
		App_Render(hWnd);
		BitBlt(hDCPaint, 0, 0, Inst->BBWidth, Inst->BBHeight, Inst->BBDC, 0, 0, SRCCOPY);
		EndPaint(hWnd, &ps);
		return 0;

	case WM_DESTROY: // 销毁窗口
		Inst = (AppData_p)GetWindowLongPtr(hWnd, 0);
		SetWindowLongPtr(hWnd, 0, (LONG_PTR)0);
		App_Delete(Inst);
		PostQuitMessage(0);
		return 0;

	default: // 其它事件
		return DefWindowProc(hWnd, Msg, wp, lp);
	}
}

// 程序入口点
int APIENTRY _tWinMain(
	_In_ HINSTANCE hInst,
	_In_opt_ HINSTANCE hPrevInst,
	_In_ LPTSTR szCmd,
	_In_ int nShow)
{
	// 窗口类
	const WNDCLASSEX WCEx =
	{
		sizeof WCEx,
		0,
		App_WndProc,
		0, sizeof(void*),
		hInst,
		LoadIcon(NULL, MAKEINTRESOURCE(IDI_APPLICATION)),
		LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW)),
		(HBRUSH)(COLOR_WINDOW + 1),
		NULL,
		TEXT("CPU_RENDERER_CLASS"),
		LoadIcon(NULL, MAKEINTRESOURCE(IDI_APPLICATION))
	};
	const ATOM ClassAtom = RegisterClassEx(&WCEx);
	HWND hWnd = CreateWindowEx(0, MAKEINTATOM(ClassAtom),
		TEXT("CPU Renderer"),
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 896, 648,
		NULL, NULL, hInst, NULL);
	MSG msg;

	// 按照窗口的显示方式显示窗口
	ShowWindow(hWnd, nShow);
	UpdateWindow(hWnd);

	while (1)
	{
		if (!PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
		{
			RECT rc;
			GetClientRect(hWnd, &rc);
			InvalidateRect(hWnd, &rc, FALSE);
		}
		else
		{
			if (!GetMessage(&msg, NULL, 0, 0))
			{
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int)msg.wParam;
}

