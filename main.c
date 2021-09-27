#include"terrain.h"
#include"dictcfg.h"

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
	CPUCan_p CPUCan;
	dict_p Config;
	Terrain_p Terrain;
}AppData_t, * AppData_p;

static AppData_p CreateAppData(HWND hWnd);
static void DeleteAppData(AppData_p RD);
static void DoRender(HWND hWnd);
static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wp, LPARAM lp);

// 初始化渲染器信息
static AppData_p CreateAppData(HWND hWnd)
{
	HDC hDCTemp = NULL;
	HBITMAP DIB = NULL;
	RECT rw, rc;
	AppData_p RD = NULL;
	char StrBuf[1024];
	struct
	{
		BITMAPINFOHEADER BMIF;
		uint32_t Bitfields[4];
	}BMIF32;
	int CfgXres, CfgYres;
	dict_p d_render = NULL;

	// 分配内存
	RD = malloc(sizeof *RD);
	if (!RD) return NULL;
	memset(RD, 0, sizeof * RD);

	RD->fp_log = fopen("app.log", "a");
	if (!RD->fp_log) goto FailExit;
	RD->hWnd = hWnd;
	RD->Config = dictcfg_load("config.cfg", RD->fp_log);
	if (!RD->Config) goto FailExit;
	d_render = dictcfg_section(RD->Config, "[render]");
	CfgXres = dictcfg_getint(d_render, "x_res", 1024);
	CfgYres = dictcfg_getint(d_render, "y_res", 768);

	// 按照设置来配置窗口大小
	GetWindowRect(hWnd, &rw);
	GetClientRect(hWnd, &rc);
	MoveWindow(hWnd,
		rw.left,
		rw.top,
		CfgXres + ((rw.right - rw.left) - (rc.right - rc.left)),
		CfgYres + ((rw.bottom - rw.top) - (rc.bottom - rc.top)),
		FALSE);
	RD->BBWidth = CfgXres;
	RD->BBHeight = CfgYres;
	if ((int32_t)RD->BBWidth <= 0 || (int32_t)RD->BBHeight <= 0) goto FailExit;

	// 填写位图信息
	memset(&BMIF32, 0, sizeof BMIF32);
	BMIF32.BMIF.biSize = sizeof BMIF32.BMIF;
	BMIF32.BMIF.biWidth = (LONG)RD->BBWidth;
	BMIF32.BMIF.biHeight = -(LONG)RD->BBHeight;
	BMIF32.BMIF.biPlanes = 1;
	BMIF32.BMIF.biBitCount = 32;
	BMIF32.BMIF.biCompression = BI_BITFIELDS;
	BMIF32.BMIF.biSizeImage = (DWORD)(RD->BBWidth * RD->BBHeight * 4);
	BMIF32.Bitfields[0] = 0x00FF0000; // 红色部分
	BMIF32.Bitfields[1] = 0x0000FF00; // 绿色部分
	BMIF32.Bitfields[2] = 0x000000FF; // 蓝色部分
	BMIF32.Bitfields[3] = 0xFF000000; // 透明层部分，如果有的话

	// 创建内存DC用于绘图
	hDCTemp = GetDC(hWnd);
	if(!hDCTemp) goto FailExit;
	RD->BBDC = CreateCompatibleDC(hDCTemp);
	if (!RD->BBDC) goto FailExit;

	// 建立内存位图用作后台缓冲区
	DIB = CreateDIBSection(hDCTemp, (BITMAPINFO*)&BMIF32, DIB_RGB_COLORS, &RD->BBPixelData, NULL, 0);
	if (!DIB) goto FailExit;
	ReleaseDC(hWnd, hDCTemp); hDCTemp = NULL;

	// 选入内存位图
	SelectObject(RD->BBDC, DIB);
	DeleteObject(DIB); // 解引用

	RD->CPUCan = CPUCan_CreateWithRGBAFB(RD->BBWidth, RD->BBHeight, RD->BBPixelData);
	if (!RD->CPUCan) goto FailExit;

	snprintf(StrBuf, sizeof StrBuf, "maps\\%d", (rand() % 2) + 1);
	RD->Terrain = Terrain_Create(RD->CPUCan, ".", StrBuf, RD->Config);
	if (!RD->Terrain) goto FailExit;

	return RD;
FailExit:
	DeleteAppData(RD);
	if (hDCTemp) ReleaseDC(hWnd, hDCTemp);
	return NULL;
}

// 释放渲染器信息
static void DeleteAppData(AppData_p RD)
{
	if (!RD) return;
	dict_delete(RD->Config);
	Terrain_Free(RD->Terrain);
	CPUCan_Delete(RD->CPUCan);
	DeleteDC(RD->BBDC);
	if (RD->fp_log) fclose(RD->fp_log);
	free(RD);
}

// 进行渲染
static void DoRender(HWND hWnd)
{
	AppData_p RD = (AppData_p)GetWindowLongPtr(hWnd, 0);
	if (!RD) return;

	Terrain_Render(RD->Terrain);
}

// 消息处理过程
static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wp, LPARAM lp)
{
	// 当前窗口实例数据
	AppData_p Inst = NULL;
	HDC hDCPaint;
	PAINTSTRUCT ps;
	switch (Msg)
	{
	case WM_CREATE: // 创建窗口
		// 创建新的实例数据并绑定到窗口上
		Inst = CreateAppData(hWnd);
		if (!Inst) return -1;
		SetWindowLongPtr(hWnd, 0, (LONG_PTR)Inst);
		return 0;

	case WM_PAINT: // 绘制窗口
		Inst = (AppData_p)GetWindowLongPtr(hWnd, 0);
		if (!Inst) return DefWindowProc(hWnd, Msg, wp, lp);
		hDCPaint = BeginPaint(hWnd, &ps);
		DoRender(hWnd);
		BitBlt(hDCPaint, 0, 0, Inst->BBWidth, Inst->BBHeight, Inst->BBDC, 0, 0, SRCCOPY);
		EndPaint(hWnd, &ps);
		return 0;

	case WM_DESTROY: // 销毁窗口
		DeleteAppData((AppData_p)GetWindowLongPtr(hWnd, 0));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR)0);
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
		WndProc,
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

	// 处理窗口事件消息循环
	for (;;)
	{
		// 没有消息的时候，刷新窗体使其绘图
		if (!PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
		{
			RECT rc;
			GetClientRect(hWnd, &rc);
			InvalidateRect(hWnd, &rc, FALSE);
		}
		else
		{
			if (!GetMessage(&msg, NULL, 0, 0)) break;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int)msg.wParam;
}

