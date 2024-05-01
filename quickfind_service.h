////////////////////////////////////////////////////////////////
// rune: References

// https://learn.microsoft.com/en-us/windows/win32/services/svc-cpp

////////////////////////////////////////////////////////////////
// rune: Globals

#define SERVICE_NAME "QuickFind"

static SERVICE_STATUS          g_service_status;
static SERVICE_STATUS_HANDLE   g_status_handle;
static server                  g_server;

////////////////////////////////////////////////////////////////
// rune: Service

static void svc_report_error(char *text);
static void svc_report_last_error(char *function_name);
static void svc_report_status(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint);
static bool svc_install(void);
static bool svc_uninstall(void);

static void WINAPI svc_ctrl_handler(DWORD dwCtrl);
static void WINAPI svc_main(DWORD dwArgc, LPSTR *lpszArgv);

////////////////////////////////////////////////////////////////
// rune: CLI

static int cli_main(int argc, char **argv);
