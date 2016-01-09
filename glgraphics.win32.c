
#include <windows.h>

#include <brl.mod/system.mod/system.h>
#include <pub.mod/glew.mod/GL/glew.h>
#include <pub.mod/glew.mod/GL/wglew.h>

// FIXME: Implement wglCreateContextAttribsARB

enum{
	_BACKBUFFER=	0x2,
	_ALPHABUFFER=	0x4,
	_DEPTHBUFFER=	0x8,
	_STENCILBUFFER=	0x10,
	_ACCUMBUFFER=	0x20,
	_AASAMPLES2=	0x40,
	_AASAMPLES4=	0x80,
	_AASAMPLES8=	0x100,
	_AASAMPLES16=	0x200,
	//0x400
	//0x800
};

enum{
	MODE_SHARED,
	MODE_WIDGET,
	MODE_WINDOW,
	MODE_DISPLAY
};

extern int _bbusew;

static const char *CLASS_NAME="BlitzMax GLGraphics";
static const wchar_t *CLASS_NAMEW=L"BlitzMax GLGraphics";

static const char *FAKE_CLASS_NAMEA="Fake BlitzMax GLGraphics";
static const wchar_t *FAKE_CLASS_NAMEW=L"Fake BlitzMax GLGraphics";

typedef struct BBGLContext BBGLContext;

struct BBGLContext{
	BBGLContext *succ;
	int mode,width,height,depth,hertz,flags;
	
	HDC hdc;
	HWND hwnd;
	HGLRC hglrc;
};

static BBGLContext *_contexts;
static BBGLContext *_sharedContext;
static BBGLContext *_currentContext;

static PFNWGLGETPIXELFORMATATTRIBFVARBPROC _wglGetPixelFormatAttribfvARB=NULL;
static PFNWGLGETPIXELFORMATATTRIBIVARBPROC _wglGetPixelFormatAttribivARB=NULL;
static PFNWGLCHOOSEPIXELFORMATARBPROC _wglChoosePixelFormatARB=NULL;
static PFNWGLCREATECONTEXTATTRIBSARBPROC _wglCreateContextAttribsARB=NULL;

typedef BOOL (APIENTRY * WGLSWAPINTERVALEXT) (int);

void bbGLGraphicsClose( BBGLContext *context );
void bbGLGraphicsGetSettings( BBGLContext *context,int *width,int *height,int *depth,int *hertz,int *flags );
void bbGLGraphicsSetGraphics( BBGLContext *context );

static const char *appTitle(){
	return bbTmpCString( bbAppTitle );
}

static const wchar_t *appTitleW(){
	return bbTmpWString( bbAppTitle );
}

// =================================================================================================

extern void (*bbOnDebugLog)(BBString *str);
#define MAX_DEBUG_LOG_SIZE	1024
void DebugLog(const char *format,...){
	va_list ap;
	char buf[MAX_DEBUG_LOG_SIZE];

	va_start(ap,format);
	vsprintf(buf,format,ap);
	va_end(ap);

	BBString *bbstr=bbStringFromCString(buf);
	bbOnDebugLog(bbstr);
}

// =================================================================================================

static void _initPfd( PIXELFORMATDESCRIPTOR *pfd,int flags );

static long _stdcall _fakeWinProc( HWND hwnd,UINT msg,WPARAM wp,LPARAM lp ){
	if( msg==WM_DESTROY ) PostQuitMessage( 0 );
	return DefWindowProc( hwnd,msg,wp,lp );
}

static void _checkWindowsExtension(){
	PIXELFORMATDESCRIPTOR pfd;
	int pf;
	HWND hwnd;
	HDC dc;
	HGLRC rc;

	if( _bbusew ){
		WNDCLASSEXW wc={sizeof(wc)};
		wc.style=CS_HREDRAW|CS_VREDRAW|CS_OWNDC;
		wc.lpfnWndProc=(WNDPROC)_fakeWinProc;
		wc.hInstance=GetModuleHandle(0);
		wc.lpszClassName=FAKE_CLASS_NAMEW;
		wc.hCursor=(HCURSOR)LoadCursor( 0,IDC_ARROW );
		wc.hbrBackground=0;
		if( !RegisterClassExW( &wc ) ) exit( -1 );
	}else{
		WNDCLASSEX wc={sizeof(wc)};
		wc.style=CS_HREDRAW|CS_VREDRAW|CS_OWNDC;
		wc.lpfnWndProc=(WNDPROC)_fakeWinProc;
		wc.hInstance=GetModuleHandle(0);
		wc.lpszClassName=FAKE_CLASS_NAMEA;
		wc.hCursor=(HCURSOR)LoadCursor( 0,IDC_ARROW );
		wc.hbrBackground=0;
		if( !RegisterClassEx( &wc ) ) exit( -1 );
	}

	if( _bbusew ){
		hwnd=CreateWindowExW( 0,FAKE_CLASS_NAMEW,0,WS_OVERLAPPED,0,0,1,1,0,0,GetModuleHandle( 0 ),0 );
	}else{
		hwnd=CreateWindowExA( 0,FAKE_CLASS_NAMEA,0,WS_OVERLAPPED,0,0,1,1,0,0,GetModuleHandle( 0 ),0 );
	}

	if( !hwnd ) exit( -2 );

	_initPfd( &pfd,_BACKBUFFER|_ALPHABUFFER|_DEPTHBUFFER|_STENCILBUFFER|_ACCUMBUFFER );

	dc=GetDC( hwnd );

	pf=ChoosePixelFormat( dc,&pfd );
	SetPixelFormat( dc,pf,&pfd );
	rc=wglCreateContext( dc );
	wglMakeCurrent( dc,rc );

	_wglGetPixelFormatAttribivARB=wglGetProcAddress( "wglGetPixelFormatAttribivARB" );
	_wglGetPixelFormatAttribfvARB=wglGetProcAddress( "wglGetPixelFormatAttribfvARB" );
	_wglChoosePixelFormatARB=wglGetProcAddress( "wglChoosePixelFormatARB" );

	wglDeleteContext( rc );
	ReleaseDC( hwnd,dc );

	DestroyWindow( hwnd );
	if( _bbusew ) {
		MSG msg;
		while( GetMessageW( &msg,NULL,0,0 ) ){
			TranslateMessage( &msg );
			DispatchMessageW( &msg );
		}
	}else{
		MSG msg;
		while( GetMessageW( &msg,NULL,0,0 ) ){
			TranslateMessage( &msg );
			DispatchMessageA( &msg );
		}
	}
}

static int _choosePixelFormat( HDC hdc,int flags ){
	float fattrib[]={ 0,0 };
	int pf,samples=0;
	UINT temp;

	if( flags & _AASAMPLES2 ) samples=2;
	if( flags & _AASAMPLES4 ) samples=4;
	if( flags & _AASAMPLES8 ) samples=8;
	if( flags & _AASAMPLES16) samples=16;



	{
		int iattrib[]={
			WGL_SAMPLE_BUFFERS_ARB,samples ? TRUE : FALSE,
			WGL_SAMPLES_ARB,samples ? samples : 0,
			WGL_DOUBLE_BUFFER_ARB,(flags & _BACKBUFFER) ? TRUE : FALSE,
			WGL_STENCIL_BITS_ARB,(flags & _STENCILBUFFER) ? 8 : 0,
			WGL_DEPTH_BITS_ARB,(flags & _DEPTHBUFFER) ? 24 : 0,
			WGL_RED_BITS_ARB,8,
			WGL_BLUE_BITS_ARB,8,
			WGL_GREEN_BITS_ARB,8,
			WGL_ALPHA_BITS_ARB,(flags & _ALPHABUFFER) ? 8 : 0,
			WGL_STEREO_ARB,0,
			WGL_ACCUM_BITS_ARB,(flags & _ACCUMBUFFER) ? 8 : 0,
			0,0
		};

		if( !_wglChoosePixelFormatARB( hdc,iattrib,fattrib,1,&pf,&temp ) ) {
			if( iattrib[1] && samples > 0 ){
				while( ( samples>>=2 ) ){
					iattrib[3]=samples;
					if( _wglChoosePixelFormatARB( hdc,iattrib,fattrib,1,&pf,&temp ) ) return pf;
				}

				iattrib[1]=FALSE;	// WGL_SAMPLE_BUFFERS_ARB
				iattrib[3]=0;		// WGL_SAMPLES_ARB
				if( !_wglChoosePixelFormatARB( hdc,iattrib,fattrib,1,&pf,&temp ) ) return 0;
				return pf;
			}
		}	
		return pf;
	}
}

static void _initPfd( PIXELFORMATDESCRIPTOR *pfd,int flags ){

	memset( pfd,0,sizeof(*pfd) );

	pfd->nSize=sizeof(pfd);
	pfd->nVersion=1;
	pfd->cColorBits=1;
	pfd->iPixelType=PFD_TYPE_RGBA;
	pfd->iLayerType=PFD_MAIN_PLANE;
	pfd->dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;

	pfd->dwFlags|=(flags & _BACKBUFFER) ? PFD_DOUBLEBUFFER : 0;
	pfd->cAlphaBits=(flags & _ALPHABUFFER) ? 1 : 0;
	pfd->cDepthBits=(flags & _DEPTHBUFFER) ? 1 : 0;
	pfd->cStencilBits=(flags & _STENCILBUFFER) ? 1 : 0;
	pfd->cAccumBits=(flags & _ACCUMBUFFER) ? 1 : 0;
}

static int _setSwapInterval( int n ){
	static WGLSWAPINTERVALEXT _wglSwapIntervalEXT=NULL;
	if( !_wglSwapIntervalEXT ) _wglSwapIntervalEXT=(WGLSWAPINTERVALEXT)wglGetProcAddress("wglSwapIntervalEXT");
	if( _wglSwapIntervalEXT ) _wglSwapIntervalEXT( n );
}

static long _stdcall _wndProc( HWND hwnd,UINT msg,WPARAM wp,LPARAM lp ){

	static HWND _fullScreen;

	BBGLContext *c;
	for( c=_contexts;c && c->hwnd!=hwnd;c=c->succ ){}
	if( !c ){
		return _bbusew ? DefWindowProcW( hwnd,msg,wp,lp ) : DefWindowProc( hwnd,msg,wp,lp );
	}

	bbSystemEmitOSEvent( hwnd,msg,wp,lp,&bbNullObject );

	switch( msg ){
	case WM_CLOSE:
		return 0;
	case WM_SYSCOMMAND:
		if (wp==SC_SCREENSAVE) return 1;
		if (wp==SC_MONITORPOWER) return 1;
		break;
	case WM_SYSKEYDOWN:
		if( wp!=VK_F4 ) return 0;
		break;
	case WM_SETFOCUS:
		if( c && c->mode==MODE_DISPLAY && hwnd!=_fullScreen ){
			DEVMODE dm;
			int swapInt=0;
			memset( &dm,0,sizeof(dm) );
			dm.dmSize=sizeof(dm);
			dm.dmPelsWidth=c->width;
			dm.dmPelsHeight=c->height;
			dm.dmBitsPerPel=c->depth;
			dm.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL;
			if( c->hertz ){
				dm.dmDisplayFrequency=c->hertz;
				dm.dmFields|=DM_DISPLAYFREQUENCY;
				swapInt=1;
			}
			if( ChangeDisplaySettings( &dm,CDS_FULLSCREEN )==DISP_CHANGE_SUCCESSFUL ){
				_fullScreen=hwnd;
			}else if( dm.dmFields & DM_DISPLAYFREQUENCY ){
				dm.dmDisplayFrequency=0;
				dm.dmFields&=~DM_DISPLAYFREQUENCY;
				if( ChangeDisplaySettings( &dm,CDS_FULLSCREEN )==DISP_CHANGE_SUCCESSFUL ){
					_fullScreen=hwnd;
					swapInt=0;
				}
			}

			if( !_fullScreen ) bbExThrowCString( "GLGraphicsDriver failed to set display mode" );
			
			_setSwapInterval( swapInt );
		}
		return 0;
	case WM_DESTROY:
	case WM_KILLFOCUS:
		if( hwnd==_fullScreen ){
			ChangeDisplaySettings( 0,CDS_FULLSCREEN );
			ShowWindow( hwnd,SW_MINIMIZE );
			_setSwapInterval( 0 );
			_fullScreen=0;
		}
		return 0;
	case WM_PAINT:
		ValidateRect( hwnd,0 );
		return 0;
	}
	return _bbusew ? DefWindowProcW( hwnd,msg,wp,lp ) : DefWindowProc( hwnd,msg,wp,lp );
}

static void _initWndClass(){
	static int _done;
	if( _done ) return;

	if( _bbusew ){
		WNDCLASSEXW wc={sizeof(wc)};
		wc.style=CS_HREDRAW|CS_VREDRAW|CS_OWNDC;
		wc.lpfnWndProc=(WNDPROC)_wndProc;
		wc.hInstance=GetModuleHandle(0);
		wc.lpszClassName=CLASS_NAMEW;
		wc.hCursor=(HCURSOR)LoadCursor( 0,IDC_ARROW );
		wc.hbrBackground=0;
		if( !RegisterClassExW( &wc ) ) exit( -1 );
	}else{
		WNDCLASSEX wc={sizeof(wc)};
		wc.style=CS_HREDRAW|CS_VREDRAW|CS_OWNDC;
		wc.lpfnWndProc=(WNDPROC)_wndProc;
		wc.hInstance=GetModuleHandle(0);
		wc.lpszClassName=CLASS_NAME;
		wc.hCursor=(HCURSOR)LoadCursor( 0,IDC_ARROW );
		wc.hbrBackground=0;
		if( !RegisterClassEx( &wc ) ) exit( -1 );
	}

	_done=1;
}

static void _validateSize( BBGLContext *context ){
	if( context->mode==MODE_WIDGET ){
		RECT rect;
		GetClientRect( context->hwnd,&rect );
		context->width=rect.right-rect.left;
		context->height=rect.bottom-rect.top;
	}
}

void bbGLGraphicsShareContexts(){
	BBGLContext *context;
	HDC hdc;
	HWND hwnd;
	HGLRC hglrc;
	long pf;
	PIXELFORMATDESCRIPTOR pfd;
	
	if( _sharedContext ) return;

	_checkWindowsExtension();
	
	_initWndClass();
	
	if( _bbusew ){
		hwnd=CreateWindowExW( 0,CLASS_NAMEW,0,WS_POPUP,0,0,1,1,0,0,GetModuleHandle(0),0 );
	}else{
		hwnd=CreateWindowEx( 0,CLASS_NAME,0,WS_POPUP,0,0,1,1,0,0,GetModuleHandle(0),0 );
	}
		
	_initPfd( &pfd,0 );
	
	hdc=GetDC( hwnd );
	if( !_wglChoosePixelFormatARB ){
		pf=ChoosePixelFormat( hdc,&pfd );
	}else{
		pf=_choosePixelFormat( hdc,0 );
		if( !pf ) pf=ChoosePixelFormat( hdc,&pfd );
	}
	if( !pf ){
		exit(0);
		DestroyWindow( hwnd );
		return;
	}
	SetPixelFormat( hdc,pf,&pfd );
	hglrc=wglCreateContext( hdc );
	if( !hglrc ) exit(0);
	
	_sharedContext=(BBGLContext*)malloc( sizeof(BBGLContext) );
	memset( _sharedContext,0,sizeof(BBGLContext) );

	_sharedContext->mode=MODE_SHARED;	
	_sharedContext->width=1;
	_sharedContext->height=1;
	
	_sharedContext->hdc=hdc;
	_sharedContext->hwnd=hwnd;
	_sharedContext->hglrc=hglrc;
}

int bbGLGraphicsGraphicsModes( int *modes,int count ){
	int i=0,n=0;
	while( n<count ){
		DEVMODE	mode;
		mode.dmSize=sizeof(DEVMODE);
		mode.dmDriverExtra=0;

		if( !EnumDisplaySettings(0,i++,&mode) ) break;

		if( mode.dmBitsPerPel<16 ) continue;

		*modes++=mode.dmPelsWidth;
		*modes++=mode.dmPelsHeight;
		*modes++=mode.dmBitsPerPel;
		*modes++=mode.dmDisplayFrequency;
		++n;
	}
	return n;
}

BBGLContext *bbGLGraphicsAttachGraphics( HWND hwnd,int flags ){
	BBGLContext *context;
	
	HDC hdc;
	HGLRC hglrc;
	
	long pf;
	PIXELFORMATDESCRIPTOR pfd;
	RECT rect;

	_checkWindowsExtension();
	
	_initWndClass();
	
	hdc=GetDC( hwnd );
	if( !hdc ) return 0;
	
	_initPfd( &pfd,flags );
	if( !_wglChoosePixelFormatARB ){
		pf=ChoosePixelFormat( hdc,&pfd );
	}else{
		pf=_choosePixelFormat( hdc,flags );
		if( !pf ) pf=ChoosePixelFormat( hdc,&pfd );
	}
	if( !pf ) return 0;
	SetPixelFormat( hdc,pf,&pfd );
	hglrc=wglCreateContext( hdc );
	
	if( _sharedContext ) wglShareLists( _sharedContext->hglrc,hglrc );
	
	GetClientRect( hwnd,&rect );
	
	context=(BBGLContext*)malloc( sizeof(BBGLContext) );
	memset( context,0,sizeof(*context) );
	
	context->mode=MODE_WIDGET;
	context->width=rect.right;
	context->height=rect.bottom;
	context->flags=flags;
	
	context->hdc=hdc;
	context->hwnd=hwnd;
	context->hglrc=hglrc;
	
	context->succ=_contexts;
	_contexts=context;
	
	return context;
}

BBGLContext *bbGLGraphicsCreateGraphics( int width,int height,int depth,int hertz,int flags ){
	BBGLContext *context;
	
	int mode;
	HDC hdc;
	HWND hwnd;
	HGLRC hglrc;
	
	long pf;
	PIXELFORMATDESCRIPTOR pfd;
	int hwnd_style;
	RECT rect={0,0,width,height};

	_checkWindowsExtension();
	
	_initWndClass();
	
	if( depth ){
		mode=MODE_DISPLAY;
		hwnd_style=WS_POPUP;
	}else{
		HWND desktop = GetDesktopWindow();
		RECT desktopRect;
		GetWindowRect(desktop, &desktopRect);

		rect.left=desktopRect.right/2-width/2;		
		rect.top=desktopRect.bottom/2-height/2;		
		rect.right=rect.left+width;
		rect.bottom=rect.top+height;
		
		mode=MODE_WINDOW;
		hwnd_style=WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
	}
		
	AdjustWindowRectEx( &rect,hwnd_style,0,0 );
	
	if( _bbusew ){
		hwnd=CreateWindowExW( 
			0,CLASS_NAMEW,appTitleW(),
			hwnd_style,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,0,0,GetModuleHandle(0),0 );
	}else{
		hwnd=CreateWindowEx( 
			0,CLASS_NAME,appTitle(),
			hwnd_style,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,0,0,GetModuleHandle(0),0 );
	}
		
	if( !hwnd ) return 0;

	GetClientRect( hwnd,&rect );
	width=rect.right-rect.left;
	height=rect.bottom-rect.top;

	_initPfd( &pfd,flags );

	hdc=GetDC( hwnd );	
	if( !_wglChoosePixelFormatARB ) {
		pf=ChoosePixelFormat( hdc,&pfd );
	}else{
		pf=_choosePixelFormat( hdc,flags );
		if( !pf ) pf=ChoosePixelFormat( hdc,&pfd );
	}

	if( !pf ){
		DestroyWindow( hwnd );
		return 0;
	}

	SetPixelFormat( hdc,pf,&pfd );
	hglrc=wglCreateContext( hdc );
	
	if( _sharedContext ) wglShareLists( _sharedContext->hglrc,hglrc );
	
	context=(BBGLContext*)malloc( sizeof(BBGLContext) );
	memset( context,0,sizeof(context) );
	
	context->mode=mode;
	context->width=width;
	context->height=height;
	context->depth=depth;
	context->hertz=hertz;
	context->flags=flags;
	
	context->hdc=hdc;
	context->hwnd=hwnd;
	context->hglrc=hglrc;
	
	context->succ=_contexts;
	_contexts=context;
	
	ShowWindow( hwnd,SW_SHOW );
	
	return context;
}

void bbGLGraphicsGetSettings( BBGLContext *context,int *width,int *height,int *depth,int *hertz,int *flags ){
	_validateSize( context );
	*width=context->width;
	*height=context->height;
	*depth=context->depth;
	*hertz=context->hertz;
	*flags=context->flags;
}

void bbGLGraphicsClose( BBGLContext *context ){
	BBGLContext **p,*t;
	
	for( p=&_contexts;(t=*p) && (t!=context);p=&t->succ ){}
	if( !t ) return;
	
	if( t==_currentContext ){
		bbGLGraphicsSetGraphics( 0 );
	}
	
	wglDeleteContext( context->hglrc );

	if( t->mode==MODE_DISPLAY || t->mode==MODE_WINDOW ){
		DestroyWindow( t->hwnd );
	}
	
	*p=t->succ;
}

void bbGLGraphicsSetGraphics( BBGLContext *context ){

	if( context==_currentContext ) return;
	
	_currentContext=context;
	
	if( context ){
		wglMakeCurrent( context->hdc,context->hglrc );
	}else{
		wglMakeCurrent( 0,0 );
	}
}

void bbGLGraphicsFlip( int sync ){
	if( !_currentContext ) return;
	
	_setSwapInterval( sync ? 1 : 0 );
	
	/*
	static int _sync=-1;

	sync=sync ? 1 : 0;
	if( sync!=_sync ){
		_sync=sync;
		_setSwapInterval( _sync );
	}
	*/

	SwapBuffers( _currentContext->hdc );
}
