////////////////////////////////////////////////////////////////
// rune: Client DLL build

#ifdef QUICKFIND_BUILD_CLIENT

#define QUICKFIND_API_EXPORT

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "quickfind_client.h"
#include "quickfind_shared.h"

#include "quickfind_client.c"
#include "quickfind_shared.c"

#endif

////////////////////////////////////////////////////////////////
// rune: Server+CLI build

#ifdef QUICKFIND_BUILD_SERVER

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <winioctl.h>
#include <strsafe.h>
#include <shlobj.h>
#include <assert.h>
#include <intrin.h>
#include <stdio.h>
#include <stdint.h>

#pragma comment ( lib, "advapi32" )
#pragma comment ( lib, "shell32" )

#define QUICKFIND_API_STATIC

#include "quickfind_client.h"
#include "quickfind_shared.h"
#include "quickfind_server.h"
#include "quickfind_ntfs.h"
#include "quickfind_service.h"

#include "quickfind_shared.c"
#include "quickfind_client.c"
#include "quickfind_server.c"
#include "quickfind_ntfs.c"
#include "quickfind_service.c"

int main(int argc, char **argv) {
    return cli_main(argc, argv);
}

#endif
