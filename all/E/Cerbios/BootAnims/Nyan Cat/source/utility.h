#define BUTTONS_DPAD_UP          (1<<0)
#define BUTTONS_DPAD_DOWN        (1<<1)
#define BUTTONS_DPAD_LEFT        (1<<2)
#define BUTTONS_DPAD_RIGHT       (1<<3)
#define BUTTONS_START            (1<<4)
#define BUTTONS_BACK             (1<<5)
#define BUTTONS_LEFT_THUMB       (1<<6)
#define BUTTONS_RIGHT_THUMB      (1<<7)
#define BUTTONS_A                (1<<8)
#define BUTTONS_B                (1<<9)
#define BUTTONS_X                (1<<10)
#define BUTTONS_Y                (1<<11)
#define BUTTONS_BLACK            (1<<12)
#define BUTTONS_WHITE            (1<<13)
#define BUTTONS_LEFT_TRIGGER     (1<<14)
#define BUTTONS_RIGHT_TRIGGER    (1<<15)

extern BOOL g_ExitNow;

extern VOID XBUtil_DebugPrint( const CHAR* buf, ... );
extern VOID mappath(char * Device, char * Alias);

extern VOID bg_decompress(D3DTexture **TextureOutput);

extern VOID statusline_refresh(const CHAR* Text);

