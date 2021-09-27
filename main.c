#include"cpucan.h"
#include"terrain.h"

#include<Windows.h>
#include<tchar.h>
#include<stdio.h>
#include<stdint.h>
#include<stdlib.h>

// ���������������Ҫ����ȥ������
typedef struct AppData_struct
{
	// ���ھ��
	HWND hWnd;

	// ����������������ͼ���
	HDC BBDC;

	// �����������ߴ�
	uint32_t BBWidth;
	uint32_t BBHeight;

	// ������������������ָ��
	uint32_t *BBPixelData;

	CPUCan_p CPUCan;
	Terrain_p Terrain;
}AppData_t, * AppData_p;

static AppData_p CreateAppData(HWND hWnd);
static void DeleteAppData(AppData_p RD);
static void DoRender(HWND hWnd);
static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wp, LPARAM lp);

// ��ʼ����Ⱦ����Ϣ
static AppData_p CreateAppData(HWND hWnd)
{
	HDC hDCTemp = NULL;
	HBITMAP DIB = NULL;
	RECT rc;
	AppData_p RD = NULL;
	char StrBuf[1024];
	struct
	{
		BITMAPINFOHEADER BMIF;
		uint32_t Bitfields[4];
	}BMIF32;

	// �����ڴ�
	RD = malloc(sizeof *RD);
	if (!RD) return NULL;
	memset(RD, 0, sizeof * RD);

	RD->hWnd = hWnd;

	// ��ȡ���ڴ�С
	GetClientRect(hWnd, &rc);
	RD->BBWidth = rc.right - rc.left;
	RD->BBHeight = rc.bottom - rc.top;
	if ((int32_t)RD->BBWidth <= 0 || (int32_t)RD->BBHeight <= 0) goto FailExit;

	// ��дλͼ��Ϣ
	memset(&BMIF32, 0, sizeof BMIF32);
	BMIF32.BMIF.biSize = sizeof BMIF32.BMIF;
	BMIF32.BMIF.biWidth = (LONG)RD->BBWidth;
	BMIF32.BMIF.biHeight = -(LONG)RD->BBHeight;
	BMIF32.BMIF.biPlanes = 1;
	BMIF32.BMIF.biBitCount = 32;
	BMIF32.BMIF.biCompression = BI_BITFIELDS;
	BMIF32.BMIF.biSizeImage = (DWORD)(RD->BBWidth * RD->BBHeight * 4);
	BMIF32.Bitfields[0] = 0x00FF0000; // ��ɫ����
	BMIF32.Bitfields[1] = 0x0000FF00; // ��ɫ����
	BMIF32.Bitfields[2] = 0x000000FF; // ��ɫ����
	BMIF32.Bitfields[3] = 0xFF000000; // ͸���㲿�֣�����еĻ�

	// �����ڴ�DC���ڻ�ͼ
	hDCTemp = GetDC(hWnd);
	if(!hDCTemp) goto FailExit;
	RD->BBDC = CreateCompatibleDC(hDCTemp);
	if (!RD->BBDC) goto FailExit;

	// �����ڴ�λͼ������̨������
	DIB = CreateDIBSection(hDCTemp, (BITMAPINFO*)&BMIF32, DIB_RGB_COLORS, &RD->BBPixelData, NULL, 0);
	if (!DIB) goto FailExit;
	ReleaseDC(hWnd, hDCTemp); hDCTemp = NULL;

	// ѡ���ڴ�λͼ
	SelectObject(RD->BBDC, DIB);
	DeleteObject(DIB); // ������

	RD->CPUCan = CPUCan_CreateWithRGBAFB(RD->BBWidth, RD->BBHeight, RD->BBPixelData);
	if (!RD->CPUCan) goto FailExit;

	snprintf(StrBuf, sizeof StrBuf, "maps\\%d", (rand() % 2) + 1);
	RD->Terrain = Terrain_Create(RD->CPUCan, StrBuf);
	if (!RD->Terrain) goto FailExit;

	return RD;
FailExit:
	DeleteAppData(RD);
	if (hDCTemp) ReleaseDC(hWnd, hDCTemp);
	return NULL;
}

// �ͷ���Ⱦ����Ϣ
static void DeleteAppData(AppData_p RD)
{
	if (!RD) return;
	Terrain_Free(RD->Terrain);
	CPUCan_Delete(RD->CPUCan);
	DeleteDC(RD->BBDC);
	free(RD);
}

// ������Ⱦ
static void DoRender(HWND hWnd)
{
	AppData_p RD = (AppData_p)GetWindowLongPtr(hWnd, 0);
	if (!RD) return;

	Terrain_Render(RD->Terrain);
}

// ��Ϣ�������
static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wp, LPARAM lp)
{
	// ��ǰ����ʵ������
	AppData_p Inst = NULL;
	HDC hDCPaint;
	PAINTSTRUCT ps;
	switch (Msg)
	{
	case WM_CREATE: // ��������
		// �����µ�ʵ�����ݲ��󶨵�������
		Inst = CreateAppData(hWnd);
		if (!Inst) return -1;
		SetWindowLongPtr(hWnd, 0, (LONG_PTR)Inst);
		return 0;

	case WM_PAINT: // ���ƴ���
		Inst = (AppData_p)GetWindowLongPtr(hWnd, 0);
		if (!Inst) return DefWindowProc(hWnd, Msg, wp, lp);
		hDCPaint = BeginPaint(hWnd, &ps);
		DoRender(hWnd);
		BitBlt(hDCPaint, 0, 0, Inst->BBWidth, Inst->BBHeight, Inst->BBDC, 0, 0, SRCCOPY);
		EndPaint(hWnd, &ps);
		return 0;

	case WM_DESTROY: // ���ٴ���
		DeleteAppData((AppData_p)GetWindowLongPtr(hWnd, 0));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR)0);
		PostQuitMessage(0);
		return 0;

	default: // �����¼�
		return DefWindowProc(hWnd, Msg, wp, lp);
	}
}

// ������ڵ�
int APIENTRY _tWinMain(
	_In_ HINSTANCE hInst,
	_In_opt_ HINSTANCE hPrevInst,
	_In_ LPTSTR szCmd,
	_In_ int nShow)
{
	// ������
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

	// ���մ��ڵ���ʾ��ʽ��ʾ����
	ShowWindow(hWnd, nShow);
	UpdateWindow(hWnd);

	// �������¼���Ϣѭ��
	for (;;)
	{
		// û����Ϣ��ʱ��ˢ�´���ʹ���ͼ
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

