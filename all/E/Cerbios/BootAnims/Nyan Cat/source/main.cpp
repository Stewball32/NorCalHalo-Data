#include <xtl.h>
#include <xgraphics.h>
#include <dsound.h>

#include <tchar.h>
#include <stdio.h>

#include "externals.h"
#include "utility.h"
#include "animation.h"
#include "hvl_replay.h"
#include "nyansong.h"
#include "dsstdfx.h"
#include "dsstdfx_bin.h"

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------

float bob, weave;

unsigned short colors[256] = { 0 };

ULONG g_Frame = 0;
#define FINALFRAME 1150
BOOL g_ExitNow = FALSE;
BOOL g_SoundEnabled = FALSE;
BOOL g_PlayOnce = TRUE;

struct hvl_tune *ht = NULL;

LPDIRECTSOUND8 g_pDSound;
LPDIRECTSOUNDBUFFER8 g_pDSBuffer;
WAVEFORMATEX g_wfx;
DSBUFFERDESC g_dsbd;
HANDLE hFillAudioBufferEvent;

#define AUDIOFRAMESIZE ((44100*2*2)/50)
#define AUDIOFRAMES 10
#define AUDIOBUFFERSIZE (AUDIOFRAMESIZE*AUDIOFRAMES)
DSBPOSITIONNOTIFY g_dspn[AUDIOFRAMES];

int nextaudiobuf = 0;

ULONG g_ButtonsLast = 0;
ULONG g_ButtonsHeld = 0;
ULONG g_ButtonsPressed = 0;
ULONG g_ButtonsReleased = 0;
HANDLE g_Controller[4];

LPDIRECT3D8             g_pD3D                = NULL; // Used to create the D3DDevice
LPDIRECT3DDEVICE8       g_pd3dDevice          = NULL; // Our rendering device

D3DMATERIAL8 g_mtrl;

D3DXMATRIX g_matView;
D3DXMATRIX g_matProj;
D3DXMATRIX g_matWorld;

//-----------------------------------------------------------------------------
// Name: XBUtil_DebugPrint()
// Desc: For printing to the debugger with formatting.
//-----------------------------------------------------------------------------
VOID XBUtil_DebugPrint( const CHAR* buf, ... )
{
    CHAR strBuffer[1024];

    va_list arglist;
    va_start( arglist, buf );
    _vsnprintf( strBuffer, sizeof(strBuffer), buf, arglist );
    va_end( arglist );

    strBuffer[sizeof(strBuffer)-1] = '\0';
    OutputDebugStringA( strBuffer );
}

VOID XBUtil_Reboot(VOID)
{
	HalReturnToFirmware(2);
}

typedef struct {
    DWORD dwWidth;
    DWORD dwHeight;
    BOOL  fProgressive;
    BOOL  fWideScreen;
	DWORD dwFreq;
} DISPLAY_MODE;

// Display modes in order of our preference
DISPLAY_MODE g_aDisplayModes[] =
{
//    Width  Height Progressive Widescreen

// HDTV Progressive Modes
    {  1280,    720,    TRUE,   TRUE,  60 },         // 1280x720 progressive 16x9

// EDTV Progressive Modes
    {   720,    480,    TRUE,   TRUE,  60 },         // 720x480 progressive 16x9
    {   640,    480,    TRUE,   TRUE,  60 },         // 640x480 progressive 16x9
    {   720,    480,    TRUE,   FALSE, 60 },         // 720x480 progressive 4x3
    {   640,    480,    TRUE,   FALSE, 60 },         // 640x480 progressive 4x3

// HDTV Interlaced Modes
//    {  1920,   1080,    FALSE,  TRUE,  60 },         // 1920x1080 interlaced 16x9

// SDTV PAL-50 Interlaced Modes
    {   720,    480,    FALSE,  TRUE,  50 },         // 720x480 interlaced 16x9 50Hz
    {   640,    480,    FALSE,  TRUE,  50 },         // 640x480 interlaced 16x9 50Hz
    {   720,    480,    FALSE,  FALSE, 50 },         // 720x480 interlaced 4x3  50Hz
    {   640,    480,    FALSE,  FALSE, 50 },         // 640x480 interlaced 4x3  50Hz

// SDTV NTSC / PAL-60 Interlaced Modes
    {   720,    480,    FALSE,  TRUE,  60 },         // 720x480 interlaced 16x9
    {   640,    480,    FALSE,  TRUE,  60 },         // 640x480 interlaced 16x9
    {   720,    480,    FALSE,  FALSE, 60 },         // 720x480 interlaced 4x3
    {   640,    480,    FALSE,  FALSE, 60 },         // 640x480 interlaced 4x3
};
#define NUM_MODES ( sizeof( g_aDisplayModes ) / sizeof( g_aDisplayModes[0] ) )

///////////////////////////////////////////////////////////////////////////////
BOOL SupportsMode( DISPLAY_MODE mode, DWORD dwVideoStandard, DWORD dwVideoFlags )
{
    if( mode.dwFreq == 60 && !(dwVideoFlags & XC_VIDEO_FLAGS_PAL_60Hz) && (dwVideoStandard == XC_VIDEO_STANDARD_PAL_I))
        return FALSE;
    
    if( mode.dwFreq == 50 && (dwVideoStandard != XC_VIDEO_STANDARD_PAL_I))
        return FALSE;

    // Need to check for widescreen on 480 modes only - 
    // 720p and 1080i are by definition widescreen.
    if( mode.dwHeight == 480 && mode.fWideScreen && !(dwVideoFlags & XC_VIDEO_FLAGS_WIDESCREEN ) )
        return FALSE;

    // Explicit check for 480p
    if( mode.dwHeight == 480 && mode.fProgressive && !(dwVideoFlags & XC_VIDEO_FLAGS_HDTV_480p) )
        return FALSE;

    // Explicit check for 720p (only 720 mode)
    if( mode.dwHeight == 720 && !(dwVideoFlags & XC_VIDEO_FLAGS_HDTV_720p) )
        return FALSE;

    // Explicit check for 1080i (only 1080 mode)
    if( mode.dwHeight == 1080 && !(dwVideoFlags & XC_VIDEO_FLAGS_HDTV_1080i) )
        return FALSE;

    return TRUE;
}


//-----------------------------------------------------------------------------
// Name: SetupMatrices()
// Desc: Creates the world, view, and projection transform matrices.
//-----------------------------------------------------------------------------
VOID SetupMatrices(BOOL bWidescreen)
{
	float scale = 0.6f;

    // Set up our view matrix.
    const D3DXVECTOR3 vEyePos( 0.0f, 0.0f, 10.0f );
    const D3DXVECTOR3 vLookAt( 0.0f, 0.0f, 0.0f );
    const D3DXVECTOR3 vUp    ( 0.0f, 1.0f, 0.0f );
    D3DXMatrixLookAtLH( &g_matView, &vEyePos, &vLookAt, &vUp );

    // For the projection matrix.
	D3DXMatrixOrthoLH( &g_matProj, bWidescreen ? (106.0f*scale) : (80.0f*scale), (48.0f*scale), 1.0f, 100.0f );

	D3DXMatrixIdentity( &g_matWorld );
}

//-----------------------------------------------------------------------------
// Name: InitD3D()
// Desc: Initializes Direct3D
//-----------------------------------------------------------------------------
HRESULT InitD3D()
{
	DWORD m_dwVideoFlags, m_dwVideoStandard;
	DWORD m_dwCurrentMode = 0;

    // Create the D3D object.
    if( NULL == ( g_pD3D = Direct3DCreate8( D3D_SDK_VERSION ) ) )
        return E_FAIL;

	// Find the best supported video mode.
    m_dwVideoStandard = XGetVideoStandard();
    m_dwVideoFlags = XGetVideoFlags();
    for(m_dwCurrentMode = 0; m_dwCurrentMode < NUM_MODES-1; m_dwCurrentMode++)
    {
		if(SupportsMode( g_aDisplayModes[ m_dwCurrentMode ], m_dwVideoStandard, m_dwVideoFlags )) break;
    } 

    // Set up the structure used to create the D3DDevice.
    D3DPRESENT_PARAMETERS d3dpp; 
    ZeroMemory( &d3dpp, sizeof(d3dpp) );
	d3dpp.BackBufferWidth        = g_aDisplayModes[ m_dwCurrentMode ].dwWidth;
    d3dpp.BackBufferHeight       = g_aDisplayModes[ m_dwCurrentMode ].dwHeight;
	d3dpp.Flags                  = g_aDisplayModes[ m_dwCurrentMode ].fProgressive ? D3DPRESENTFLAG_PROGRESSIVE : D3DPRESENTFLAG_INTERLACED;
    d3dpp.Flags                 |= g_aDisplayModes[ m_dwCurrentMode ].fWideScreen ? D3DPRESENTFLAG_WIDESCREEN : 0;
    d3dpp.FullScreen_RefreshRateInHz = g_aDisplayModes[ m_dwCurrentMode ].dwFreq;
    d3dpp.BackBufferFormat       = D3DFMT_A8R8G8B8;
    d3dpp.BackBufferCount        = 1;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
	d3dpp.MultiSampleType        = D3DMULTISAMPLE_4_SAMPLES_SUPERSAMPLE_GAUSSIAN;

	// Create the Direct3D device.
    if( FAILED( g_pD3D->CreateDevice( 0, D3DDEVTYPE_HAL, NULL,
                                      D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                      &d3dpp, &g_pd3dDevice ) ) )
        return E_FAIL;

	SetupMatrices(g_aDisplayModes[ m_dwCurrentMode ].fWideScreen);

	return S_OK;
}

//-----------------------------------------------------------------------------
// Name: Render()
// Desc: Draws the scene
//-----------------------------------------------------------------------------
VOID Render()
{
	int x, y;
	float vx, vy;
	unsigned int /*fg,*/ bg, r, g, b, fade;

	// Clear the backbuffer and the zbuffer
    g_pd3dDevice->Clear( 0, NULL, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, 
                         D3DCOLOR_XRGB(0,0,0), 1.0f, 0 );

	DirectSoundDoWork();

	g_pd3dDevice->SetTransform( D3DTS_WORLD, &g_matWorld );
    g_pd3dDevice->SetTransform( D3DTS_VIEW, &g_matView );
    g_pd3dDevice->SetTransform( D3DTS_PROJECTION, &g_matProj );

	g_pd3dDevice->SetRenderState( D3DRS_LIGHTING, FALSE );
	g_pd3dDevice->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
	g_pd3dDevice->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    g_pd3dDevice->SetRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    g_pd3dDevice->SetRenderState( D3DRS_ZENABLE, FALSE );

    g_pd3dDevice->SetRenderState( D3DRS_ALPHAREF,        0x08 );
    g_pd3dDevice->SetRenderState( D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL );

	g_pd3dDevice->SetRenderState( D3DRS_AMBIENT, 0xffffffff );
    g_pd3dDevice->SetRenderState( D3DRS_LIGHTING, TRUE );

	g_pd3dDevice->SetTextureStageState( 0, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	g_pd3dDevice->SetTextureStageState( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
	g_pd3dDevice->SetVertexShader( D3DFVF_XYZ );

	if(g_Frame < 50) {
		fade = 0;
	} else if(g_Frame < 80) {
		fade = g_Frame - 50;
	} else if(g_Frame > (FINALFRAME - 30)) {
		fade = FINALFRAME - g_Frame;
	} else {
		fade = 30;
	}

	for(y = 0; y < FRAME_HEIGHT; y++ ) {
		vy = (FRAME_HEIGHT * 0.5f) - y + bob;
		for(x = 0; x < FRAME_WIDTH; x++ ) {
			vx = (FRAME_WIDTH * 0.5f) - x + weave;

			bg = colors[frames[((g_Frame/4)%12)][(y*FRAME_WIDTH)+x]];
			if(bg != 0x000) {
				r = (bg & 0xF00) >> 8;
				g = (bg & 0x0F0) >> 4;
				b = (bg & 0x00F) >> 0;

				g_mtrl.Ambient.r = (r * fade) / (15.0f * 30.0f);
				g_mtrl.Ambient.g = (g * fade) / (15.0f * 30.0f);
				g_mtrl.Ambient.b = (b * fade) / (15.0f * 30.0f);
				g_pd3dDevice->SetMaterial( &g_mtrl );

				g_pd3dDevice->Begin( D3DPT_QUADLIST );
				g_pd3dDevice->SetVertexData4f( D3DVSDE_VERTEX, vx-1.0f, vy-1.0f, 0.0f, 1.0f );
				g_pd3dDevice->SetVertexData4f( D3DVSDE_VERTEX, vx+0.0f, vy-1.0f, 0.0f, 1.0f );
				g_pd3dDevice->SetVertexData4f( D3DVSDE_VERTEX, vx+0.0f, vy+0.0f, 0.0f, 1.0f );
				g_pd3dDevice->SetVertexData4f( D3DVSDE_VERTEX, vx-1.0f, vy+0.0f, 0.0f, 1.0f );
				g_pd3dDevice->End();
			}
		}
	}

	// Present the backbuffer contents to the display
    g_pd3dDevice->Present( NULL, NULL, NULL, NULL );
}

VOID XInputGetStateAll()
{
	XINPUT_STATE ControllerState[4];
	int i, j, c = -1;
	ULONG ButtonsCurrent = 0;

	for (i = 0; i < 4; i++)
	{
		if (g_Controller[i])
		{
			XInputGetState(g_Controller[i], &ControllerState[i]);
			ButtonsCurrent |= (ControllerState[i].Gamepad.wButtons) & 0xff;

#if 0
			if (ControllerState[i].Gamepad.sThumbLY < -28672)
				ButtonsCurrent |= XINPUT_GAMEPAD_DPAD_UP;
			if (ControllerState[i].Gamepad.sThumbLY > 28671)
				ButtonsCurrent |= XINPUT_GAMEPAD_DPAD_DOWN;
			if (ControllerState[i].Gamepad.sThumbLX < -28672)
				ButtonsCurrent |= XINPUT_GAMEPAD_DPAD_LEFT;
			if (ControllerState[i].Gamepad.sThumbLX > 28671)
				ButtonsCurrent |= XINPUT_GAMEPAD_DPAD_RIGHT;
#endif

			for (j = 0; j < 8; j++)
			{
				if (ControllerState[i].Gamepad.bAnalogButtons[j] > 0x7f)
					ButtonsCurrent |= (1 << (j + 8));
			}
		}
	}

	g_ButtonsHeld = ButtonsCurrent & g_ButtonsLast;
	g_ButtonsReleased = g_ButtonsLast & ~ButtonsCurrent;
	g_ButtonsPressed = ButtonsCurrent & ~g_ButtonsLast;
	g_ButtonsLast = ButtonsCurrent;
}

VOID ParseInput()
{
	XInputGetStateAll();

	g_Frame++;

	bob = (sin(g_Frame / 30.0f) * 0.5f);
	weave = (cos(g_Frame / 30.0f) * 0.5f);

	if((g_Frame < (FINALFRAME - 60)) && ((g_ButtonsLast & BUTTONS_BACK) || (!ht))) {
		g_Frame = (FINALFRAME - 60);
	}

	if(g_Frame >= FINALFRAME) {
		g_ExitNow = TRUE;
	}
}

VOID HivelyCallback()
{
	LPVOID audiobuffer;
	DWORD audiobytes;

	if( FAILED( g_pDSBuffer->Lock( (nextaudiobuf * AUDIOFRAMESIZE), AUDIOFRAMESIZE, &audiobuffer, &audiobytes, NULL, NULL, 0 ) ) ) {
		g_pDSBuffer->Stop();
		return;
	}

	if(ht && ht->ht_SongEndReached && g_PlayOnce) {
		ht = NULL;
	}

	if(!ht) {
		ZeroMemory( audiobuffer, AUDIOFRAMESIZE );
	} else {
		hvl_DecodeFrame( ht, ((char*)audiobuffer), ((char*)audiobuffer)+2, 4 );
	}
	nextaudiobuf = (nextaudiobuf+1)%AUDIOFRAMES;

	g_pDSBuffer->Unlock( audiobuffer, audiobytes, NULL, NULL );
}

//-----------------------------------------------------------------------------
// Name: main()
// Desc: Entry point to the program.
//-----------------------------------------------------------------------------
VOID __cdecl main()
{
	// Initialize Direct3D
    if( FAILED( InitD3D() ) )
        return;

	Render();

	colors[',']  = 0x005; /* Blue background */
	colors['.']  = 0xfff; /* White stars */
	colors['\''] = 0x000; /* Black border */
	colors['@']  = 0xffd; /* Tan poptart */
	colors['$']  = 0xd8a; /* Pink poptart */
	colors['-']  = 0xd08; /* Red poptart */
	colors['>']  = 0xf00; /* Red rainbow */
	colors['&']  = 0xfa0; /* Orange rainbow */
	colors['+']  = 0xff0; /* Yellow Rainbow */
	colors['#']  = 0x8f0; /* Green rainbow */
	colors['=']  = 0x08f; /* Light blue rainbow */
	colors[';']  = 0x00d; /* Dark blue rainbow */
	colors['*']  = 0x555; /* Gray cat face */
	colors['%']  = 0xd8a; /* Pink cheeks */

	XDEVICE_PREALLOC_TYPE deviceTypes[] =
	{
		{XDEVICE_TYPE_GAMEPAD, 4},
	};

	XInitDevices( sizeof(deviceTypes) / sizeof(XDEVICE_PREALLOC_TYPE), deviceTypes );

	Sleep(2000);

	g_Controller[0] = XInputOpen( XDEVICE_TYPE_GAMEPAD, XDEVICE_PORT0, XDEVICE_NO_SLOT, 0);
	g_Controller[1] = XInputOpen( XDEVICE_TYPE_GAMEPAD, XDEVICE_PORT1, XDEVICE_NO_SLOT, 0);
	g_Controller[2] = XInputOpen( XDEVICE_TYPE_GAMEPAD, XDEVICE_PORT2, XDEVICE_NO_SLOT, 0);
	g_Controller[3] = XInputOpen( XDEVICE_TYPE_GAMEPAD, XDEVICE_PORT3, XDEVICE_NO_SLOT, 0);

	if( FAILED( DirectSoundCreate( NULL, &g_pDSound, NULL ) ) ) {
		g_SoundEnabled = FALSE;
	} else {
		g_SoundEnabled = TRUE;

		DSEFFECTIMAGELOC EffectLoc;
		EffectLoc.dwI3DL2ReverbIndex = GraphI3DL2_I3DL2Reverb;
		EffectLoc.dwCrosstalkIndex   = GraphXTalk_XTalk;
		g_pDSound->DownloadEffectsImage( dsstdfx_bin, sizeof(dsstdfx_bin), &EffectLoc, NULL );

		XAudioCreatePcmFormat( 2, 44100, 16, &g_wfx );

		ZeroMemory( &g_dsbd, sizeof(DSBUFFERDESC) );
		g_dsbd.dwSize = sizeof(DSBUFFERDESC);
		g_dsbd.dwBufferBytes = AUDIOBUFFERSIZE;
		g_dsbd.lpwfxFormat = &g_wfx;
		
		if( FAILED( g_pDSound->CreateSoundBuffer( &g_dsbd, &g_pDSBuffer, NULL ) ) ) {
			g_SoundEnabled = FALSE;
		} else {
			hFillAudioBufferEvent = CreateEvent( NULL, FALSE, FALSE, NULL );

			hvl_InitReplayer();
			ht = hvl_LoadTune( nyansong, sizeof(nyansong), 44100, 0 );
			if(!ht)
				g_SoundEnabled = FALSE;
			else {
				int i;
				for( i = 0; i < AUDIOFRAMES; i++) {
					g_dspn[i].dwOffset = i * AUDIOFRAMESIZE;
					g_dspn[i].hEventNotify = hFillAudioBufferEvent;
				}
				for( i = 0; i < AUDIOFRAMES/4; i++) {
					HivelyCallback();
				}
				g_pDSBuffer->SetNotificationPositions( AUDIOFRAMES, g_dspn );
			}
		}
	}

	// Enter render loop
    while(!g_ExitNow)
    {
		while(g_SoundEnabled && (WaitForSingleObject( hFillAudioBufferEvent, 0 ) == WAIT_OBJECT_0)) {
			HivelyCallback();
		}
		if(g_SoundEnabled) {
			if(g_Frame == 51)
				g_pDSBuffer->Play(0, 0, DSBPLAY_LOOPING);
			if((g_Frame > 50) && (g_Frame < 75))
				g_pDSBuffer->SetVolume( DSBVOLUME_MIN + (((DSBVOLUME_MAX - DSBVOLUME_MIN) / 26) * (g_Frame-50)) );
			if(g_Frame > (FINALFRAME - 25))
				g_pDSBuffer->SetVolume( DSBVOLUME_MAX - (((DSBVOLUME_MAX - DSBVOLUME_MIN) / 26) * (FINALFRAME - g_Frame)) );
		}

		ParseInput();
		Render();
    }

	g_pd3dDevice->PersistDisplay();

	HalReturnToFirmware(2);
}
