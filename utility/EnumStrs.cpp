#include "EnumStrs.h"
using namespace std;

char* D3DTRANSFORMSTATETYPE_Str(int enumNum){
	switch(enumNum){
	case 2:return "D3DTS_VIEW";
	case 3:return "D3DTS_PROJECTION";
	case 16:return "D3DTS_TEXTURE0";
	case 17:return "D3DTS_TEXTURE1";
	case 18:return "D3DTS_TEXTURE2";
	case 19:return "D3DTS_TEXTURE3";
	case 20:return "D3DTS_TEXTURE4";
	case 21:return "D3DTS_TEXTURE5";
	case 22:return "D3DTS_TEXTURE6";
	case 23:return "D3DTS_TEXTURE7";
	case 0x7fffffff:return "D3DTS_FORCE_DWORD";
	case 256:return "D3DTS_WORLD";
	case 257:return "D3DTS_WORLD1";
	case 258:return "D3DTS_WORLD2";
	case 259:return "D3DTS_WORLD3";
	}
}

/*
D3DTS_VIEW          = 2,
D3DTS_PROJECTION    = 3,
D3DTS_TEXTURE0      = 16,
D3DTS_TEXTURE1      = 17,
D3DTS_TEXTURE2      = 18,
D3DTS_TEXTURE3      = 19,
D3DTS_TEXTURE4      = 20,
D3DTS_TEXTURE5      = 21,
D3DTS_TEXTURE6      = 22,
D3DTS_TEXTURE7      = 23,
D3DTS_FORCE_DWORD     = 0x7fffffff
#define D3DTS_WORLDMATRIX(index) (D3DTRANSFORMSTATETYPE)(index + 256)
#define D3DTS_WORLD  D3DTS_WORLDMATRIX(0)
#define D3DTS_WORLD1 D3DTS_WORLDMATRIX(1)
#define D3DTS_WORLD2 D3DTS_WORLDMATRIX(2)
#define D3DTS_WORLD3 D3DTS_WORLDMATRIX(3)
*/