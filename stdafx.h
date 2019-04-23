// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <shlobj.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <stdio.h>
#include <io.h>
#include <math.h>
#include <process.h>

#include <utility>
#include <algorithm>
#include <vector>
#include <array>
#include <map>
#include <deque>
#include <queue>
#include <fstream>

// D3D12
#include <d3d12.h>
#include <dxgi1_5.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include "WinPixEventRuntime/pix3.h"
#include <DXProgrammableCapture.h>

#include "json.hpp"

#define ARRAY_SIZE(a)   (sizeof(a) / sizeof(a[0]))
