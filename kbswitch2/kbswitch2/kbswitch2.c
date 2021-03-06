#include "../common.inl"

#include "resource.h"

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static const char MUTEX_NAME[]="kbswitch2_mutex";

static const char WND_CLASS_NAME[]="kbswitch2_wnd";

enum {
	NOTIFY_MSG=WM_APP+1,
	SHOWTIP_MSG,
};
enum {NOTIFY_ID=1};

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static UINT g_controlMsg;
static HWND g_hWnd;

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct Options
{
	wchar_t *pLayoutToSet;
	BOOL showWindow;
	BOOL nextLayout;
	BOOL prevLayout;
};
typedef struct Options Options;

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct Layout
{
	struct Layout *pNextLayout;

	HKL hkl;
	wchar_t *pDisplayName;
};
typedef struct Layout Layout;

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// If this function is optimized, VC++ strips it out and replaces references to
// it with references to memset. Except that memset isn't available, because I'm
// not linking with the C runtime. Which is why I'm writing this function in the
// first place.
//
// THIS ENTIRE SITUATION IS STUPID. ALL OF IT.

#pragma optimize("",off)
void ZeroFill(void *p,size_t n)
{
	size_t i;

	for(i=0;i<n;++i)
		((char *)p)[i]=0;
}
#pragma optimize("",on)

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static Layout *FindActiveLayout(Layout *pFirstLayout)
{
	Layout *pLayout;
	HKL hkl=GetKeyboardLayout(0);

	for(pLayout=pFirstLayout;pLayout;pLayout=pLayout->pNextLayout)
	{
		if(pLayout->hkl==hkl)
			return pLayout;
	}

	return NULL;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static Layout *FindLayoutByName(Layout *pFirstLayout,const wchar_t *pName)
{
	if(pName)
	{
		Layout *pLayout;

		for(pLayout=pFirstLayout;pLayout;pLayout=pLayout->pNextLayout)
		{
			if(lstrcmpiW(pLayout->pDisplayName,pName)==0)
				return pLayout;
		}
	}

	return NULL;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void DeleteLayoutList(Layout *pFirstLayout)
{
	Layout *pLayout=pFirstLayout;

	while(pLayout)
	{
		Layout *pNextLayout=pLayout->pNextLayout;

		LocalFree(pLayout->pDisplayName);
		LocalFree(pLayout);

		pLayout=pNextLayout;
	}
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static Layout *CreateLayoutsList(void)
{
	BOOL good=FALSE;
	int i;
	Layout *pFirstLayout=NULL,**ppNextLayout=&pFirstLayout;
	int numHKLs;
	HKL *pHKLs=NULL;

	numHKLs=GetKeyboardLayoutList(0,NULL);
	if(numHKLs==0)
		goto bye;

	pHKLs=LocalAlloc(0,numHKLs*sizeof *pHKLs);
	if(!pHKLs)
		goto bye;

	if(GetKeyboardLayoutList(numHKLs,pHKLs)!=numHKLs)
		goto bye;

	for(i=0;i<numHKLs;++i)
	{
		Layout entry,*pEntry;

		entry.hkl=pHKLs[i];

		entry.pDisplayName=GetLayoutDisplayName(entry.hkl);
		if(!entry.pDisplayName)
			continue;

		pEntry=LocalAlloc(0,sizeof *pEntry);
		if(!pEntry)
			goto bye;

		*pEntry=entry;

		*ppNextLayout=pEntry;
		ppNextLayout=&pEntry->pNextLayout;
		*ppNextLayout=NULL;
	}

	good=TRUE;

bye:
	if(!good)
	{
		DeleteLayoutList(pFirstLayout);
		pFirstLayout=NULL;
	}

	return pFirstLayout;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void NotifyCtl(DWORD action,const wchar_t *pTip,UINT extraFlags)
{
	NOTIFYICONDATAW nid;

	ZeroFill(&nid,sizeof nid);

	nid.cbSize=sizeof nid;
	nid.hWnd=g_hWnd;
	nid.uID=NOTIFY_ID;
	nid.uFlags=NIF_ICON|NIF_MESSAGE|extraFlags;
	nid.uCallbackMessage=NOTIFY_MSG;
	nid.hIcon=LoadIcon(GetModuleHandle(NULL),MAKEINTRESOURCE(IDI_ICON1));
	nid.uTimeout=500;

	if(pTip)
	{
		enum {
			MAX_TIP_SIZE=sizeof nid.szTip/sizeof nid.szTip[0],
			MAX_INFO_SIZE=sizeof nid.szInfo/sizeof nid.szInfo[0],
			MAX_INFO_TITLE_SIZE=sizeof nid.szInfoTitle/sizeof nid.szInfoTitle[0],
		};

		nid.uFlags|=NIF_TIP;

		lstrcpynW(nid.szTip,pTip,MAX_TIP_SIZE);
		nid.szTip[MAX_TIP_SIZE-1]=0;

		lstrcpynW(nid.szInfo,pTip,MAX_INFO_SIZE);
		nid.szInfo[MAX_INFO_SIZE-1]=0;

		lstrcpynW(nid.szInfoTitle,L"kbswitch2",MAX_INFO_TITLE_SIZE);
		nid.szInfoTitle[MAX_INFO_TITLE_SIZE-1]=0;
	}

	Shell_NotifyIconW(action,&nid);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void UpdateToolTip(BOOL info)
{
	Layout *pFirstLayout=CreateLayoutsList();
	Layout *pActiveLayout=FindActiveLayout(pFirstLayout);
	wchar_t *pDisplayName;
	UINT extraFlags;

	pActiveLayout=FindActiveLayout(pFirstLayout);

	if(!pActiveLayout)
		pDisplayName=L"?";
	else
		pDisplayName=pActiveLayout->pDisplayName;

	extraFlags=0;
	if(info)
		extraFlags=NIF_INFO;

	NotifyCtl(NIM_MODIFY,pDisplayName,extraFlags);

	DeleteLayoutList(pFirstLayout);
	pFirstLayout=NULL;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void SetLayoutByHandle(HKL hkl,BOOL showTip)
{
	PostMessage(HWND_BROADCAST,WM_INPUTLANGCHANGEREQUEST,0,(LPARAM)hkl);

	if(showTip)
		PostMessage(g_hWnd,SHOWTIP_MSG,0,0);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static HKL FindLayoutHandleForDisplayName(const wchar_t *pName)
{
	HKL hkl;
	Layout *pFirstLayout=CreateLayoutsList();
	Layout *pLayout=FindLayoutByName(pFirstLayout,pName);

	if(pLayout)
		hkl=pLayout->hkl;
	else
		hkl=GetKeyboardLayout(0);

	DeleteLayoutList(pFirstLayout);
	pFirstLayout=NULL;

	return hkl;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void ApplyOptions(const Options *options)
{
	if(options->pLayoutToSet)
	{
		HKL hkl=FindLayoutHandleForDisplayName(options->pLayoutToSet);
		PostMessage(HWND_BROADCAST,g_controlMsg,CONTROL_SET_LAYOUT,(LPARAM)hkl);
	}
	else if(options->nextLayout)
	{
		PostMessage(HWND_BROADCAST,g_controlMsg,CONTROL_NEXT_LAYOUT,0);
	}
	else if(options->prevLayout)
	{
		PostMessage(HWND_BROADCAST,g_controlMsg,CONTROL_PREV_LAYOUT,0);
	}
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void DoPopupMenu(void)
{
	int idLayoutsBegin,idLayoutsEnd,idExit,cmd;
	POINT mpt;
	HMENU hMenu=CreatePopupMenu();
	Layout *pFirstLayout=CreateLayoutsList();

	SetForegroundWindow(g_hWnd);

	// Fill menu
	{
		int id=1;

		idLayoutsBegin=id;
		if(pFirstLayout)
		{
			Layout *pActiveLayout=FindActiveLayout(pFirstLayout),*pLayout;

			for(pLayout=pFirstLayout;pLayout;pLayout=pLayout->pNextLayout)
			{
				UINT flags=0;

				if(pLayout==pActiveLayout)
					flags|=MF_CHECKED;

				AppendMenuW(hMenu,flags,id++,pLayout->pDisplayName);
			}

			AppendMenuW(hMenu,MF_SEPARATOR,0,0);
		}
		idLayoutsEnd=id;

		idExit=id++;
		AppendMenuA(hMenu,0,idExit,"E&xit");
	}

	// Popup menu
	GetCursorPos(&mpt);
	cmd=TrackPopupMenuEx(hMenu,TPM_RETURNCMD,mpt.x,mpt.y,g_hWnd,0);

	// Do what?
	if(cmd==idExit)
	{
		// Bye...
		PostQuitMessage(0);
	}
	else if(cmd>=idLayoutsBegin&&cmd<idLayoutsEnd)
	{
		// Set new layout
		int idx=cmd-idLayoutsBegin;
		Layout *pLayout=pFirstLayout;

		while(idx-->0)
			pLayout=pLayout->pNextLayout;

		SetLayoutByHandle(pLayout->hkl,FALSE);
	}

	DeleteLayoutList(pFirstLayout);
	pFirstLayout=NULL;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static const char RIGHT_CLICK_MESSAGE[]="Right click here to get the kbswitch menu.";
static const size_t RIGHT_CLICK_MESSAGE_LEN=sizeof RIGHT_CLICK_MESSAGE-1;

static LRESULT CALLBACK WndProc(HWND hWnd,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
	switch(uMsg)
	{
	default:
		if(uMsg==g_controlMsg)
		{
			if(wParam==CONTROL_NEXT_LAYOUT||wParam==CONTROL_PREV_LAYOUT)
			{
				Layout *pFirstLayout,*pActiveLayout;

				pFirstLayout=CreateLayoutsList();
				pActiveLayout=FindActiveLayout(pFirstLayout);

				if(pActiveLayout)
				{
					Layout *pNewLayout;

					LOG("Active layout: \"%S\"\n",pActiveLayout->pDisplayName);
					LOG("wParam=%d\n",(int)wParam);

					if(wParam==CONTROL_NEXT_LAYOUT)
					{
						pNewLayout=pActiveLayout->pNextLayout;
						if(!pNewLayout)
							pNewLayout=pFirstLayout;
					}
					else
					{
						Layout *pNextLayout;
						if(pActiveLayout==pFirstLayout)
							pNextLayout=NULL;
						else
							pNextLayout=pActiveLayout;

						for(pNewLayout=pFirstLayout;pNewLayout;pNewLayout=pNewLayout->pNextLayout)
						{
							if(pNewLayout->pNextLayout==pNextLayout)
								break;
						}
					}

					if(pNewLayout)
					{
						LOG("New layout: \"%S\"\n",pNewLayout->pDisplayName);
						SetLayoutByHandle(pNewLayout->hkl,TRUE);
					}
				}

				DeleteLayoutList(pFirstLayout);
				pFirstLayout=NULL;
			}
			else if(wParam==CONTROL_SET_LAYOUT)
			{
				SetLayoutByHandle((HKL)lParam,TRUE);
			}
		}
		break;

	case WM_PAINT:
		{
			RECT client;
			PAINTSTRUCT ps;
			HDC dc=BeginPaint(hWnd,&ps);

			SaveDC(dc);

			GetClientRect(hWnd,&client);
			FillRect(dc,&client,GetStockObject(WHITE_BRUSH));

			SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
			SetTextColor(dc,RGB(0,0,0));
			SetTextAlign(dc,TA_CENTER|VTA_CENTER);
			TextOutA(dc,client.right/2,client.bottom/2,RIGHT_CLICK_MESSAGE,RIGHT_CLICK_MESSAGE_LEN);

			RestoreDC(dc,-1);

			EndPaint(hWnd,&ps);
		}
		return 0;

	case WM_INPUTLANGCHANGEREQUEST:
		UpdateToolTip(FALSE);
		break;

	case WM_RBUTTONUP:
		// avoid injecting the DLL into explorer...
		DoPopupMenu();
		return 0;

	case SHOWTIP_MSG:
		UpdateToolTip(TRUE);
		return 0;

	case NOTIFY_MSG:
		{
			switch(lParam)
			{
			case WM_RBUTTONDOWN:
				DoPopupMenu();
				return 0;
			}
		}
		break;

	case WM_CLOSE:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd,uMsg,wParam,lParam);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static BOOL CreateWnd(BOOL show)
{
	WNDCLASSEXA w;

	w.cbClsExtra=0;
	w.cbSize=sizeof w;
	w.cbWndExtra=0;
	w.hbrBackground=0;//GetStockObject(NULL_BRUSH);
	w.hCursor=LoadCursor(0,IDC_ARROW);
	w.hIcon=LoadIcon(GetModuleHandle(NULL),MAKEINTRESOURCE(IDI_ICON1));
	w.hIconSm=LoadIcon(GetModuleHandle(NULL),MAKEINTRESOURCE(IDI_ICON1));
	w.hInstance=GetModuleHandle(0);
	w.lpfnWndProc=&WndProc;
	w.lpszClassName=WND_CLASS_NAME;
	w.lpszMenuName=0;
	w.style=CS_HREDRAW|CS_VREDRAW;

	if(!RegisterClassExA(&w))
		return 0;

	g_hWnd=CreateWindowA(WND_CLASS_NAME,"kbswitch2",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,0,0,GetModuleHandle(0),0);
	if(!g_hWnd)
		return FALSE;

	if(show)
	{
		UpdateWindow(g_hWnd);
		ShowWindow(g_hWnd,SW_SHOW);
	}

	return TRUE;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static int Main(const Options *options)
{
	if(!CreateWnd(options->showWindow))
		return 1;

	NotifyCtl(NIM_ADD,L"",0);

	UpdateToolTip(FALSE);

	if(options->pLayoutToSet)
	{
		HKL hkl=FindLayoutHandleForDisplayName(options->pLayoutToSet);
		SetLayoutByHandle(hkl,FALSE);
	}

	for(;;)
	{
		int r;
		MSG msg;

		r=GetMessage(&msg,NULL,0,0);
		if(r==0||r==-1)
			break;

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	NotifyCtl(NIM_DELETE,NULL,0);

	return 0;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static const STARTUPINFOW SI_ZERO={0,};

static BOOL RunHelper(const wchar_t *pEXEName)
{
	wchar_t *pFileName;
	BOOL good;
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;

	{
		DWORD moduleFileNameSize=32768;
		wchar_t *pModuleFileName=LocalAlloc(0,moduleFileNameSize*sizeof *pModuleFileName);
		wchar_t *p;
		wchar_t *pLastSep=pModuleFileName;
		DWORD fileNameSize;

		GetModuleFileNameW(NULL,pModuleFileName,moduleFileNameSize);
		pModuleFileName[moduleFileNameSize-1]=0;

		for(p=pModuleFileName;*p!=0;p=CharNextW(p))
		{
			if(*p==L'\\'||*p==L'/')
				pLastSep=p;
		}

		*pLastSep=0;

		fileNameSize=lstrlenW(pModuleFileName)+1+lstrlenW(pEXEName)+1;
		pFileName=LocalAlloc(0,fileNameSize*sizeof pFileName);

		lstrcpyW(pFileName,pModuleFileName);
		lstrcatW(pFileName,L"\\");
		lstrcatW(pFileName,pEXEName);

		LocalFree(pModuleFileName);
		pModuleFileName=NULL;
	}

	si=SI_ZERO;
	si.cb=sizeof si;

	good=CreateProcessW(pFileName,NULL,NULL,NULL,TRUE,0,NULL,NULL,&si,&pi);
	if(!good)
	{
		DWORD err=GetLastError();
		wchar_t msg[500];

		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM,NULL,err,0,msg,sizeof msg/sizeof msg[0],NULL);

		MessageBoxW(NULL,msg,pFileName,MB_OK|MB_ICONERROR);
	}

	LocalFree(pFileName);
	pFileName=NULL;

	return good;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static BOOL RunHelpers(void)
{
	BOOL isWOW64;

	if(IsWow64Process(GetCurrentProcess(),&isWOW64)&&isWOW64)
	{
		if(!RunHelper(L"kbswitch2_helper_x64" W(BUILD_SUFFIX) L".exe"))
			return FALSE;
	}

	if(!RunHelper(L"kbswitch2_helper_x86" W(BUILD_SUFFIX) L".exe"))
		return FALSE;

	return TRUE;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void ProcessCommandLine(Options *options)
{
	int numArgs,i;
	LPWSTR *ppArgs=CommandLineToArgvW(GetCommandLineW(),&numArgs);
	if(!ppArgs)
		return;//???

	for(i=1;i<numArgs;++i)
	{
		if(lstrcmpiW(ppArgs[i],L"/showwindow")==0)
			options->showWindow=TRUE;
		else if(lstrcmpiW(ppArgs[i],L"/nextlayout")==0)
			options->nextLayout=TRUE;
		else if(lstrcmpiW(ppArgs[i],L"/prevlayout")==0)
			options->prevLayout=TRUE;
		else
		{
			// Well... whatever it is, assume it's a keyboard layout...
			LocalFree(options->pLayoutToSet);

			options->pLayoutToSet=LocalAlloc(0,(lstrlenW(ppArgs[i])+1)*sizeof(wchar_t));
			lstrcpyW(options->pLayoutToSet,ppArgs[i]);
		}
	}

	LocalFree(ppArgs);
	ppArgs=NULL;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

void Entry(void)
{
	HANDLE hMutex;
	int result=1;
	Options options;

	g_controlMsg=RegisterWindowMessageA(CONTROL_MESSAGE_NAME);

	ZeroFill(&options,sizeof options);
	ProcessCommandLine(&options);

#ifdef _DEBUG
	options.showWindow=TRUE;
#endif

	hMutex=OpenMutexA(MUTEX_ALL_ACCESS,FALSE,MUTEX_NAME);
	if(hMutex)
	{
		// Already running.
		ApplyOptions(&options);
	}
	else
	{
		if(options.nextLayout||options.prevLayout)
		{
			// Don't do anything.
		}
		else
		{
			hMutex=CreateMutexA(0,FALSE,MUTEX_NAME);

			if(!RunHelpers())
				result=1;
			else
			{
				result=Main(&options);

				ReleaseMutex(hMutex);
				hMutex=NULL;
			}

			PostMessage(HWND_BROADCAST,g_controlMsg,CONTROL_QUIT,0);
		}
	}

	LocalFree(options.pLayoutToSet);
	options.pLayoutToSet=NULL;

	ExitProcess(result);
}
