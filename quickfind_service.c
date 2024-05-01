
// Report an error to the windows event log
static void svc_report_error(char *text) {
    HANDLE event_source = RegisterEventSourceA(null, SERVICE_NAME);
    if (event_source != null) {
        char *strings[] = { SERVICE_NAME, text };

        ReportEventA(event_source,           // event log handle
                     EVENTLOG_ERROR_TYPE,   // event type
                     0,                     // event category
                     0x1,                   // event identifier
                     null,                  // no security identifier
                     countof(strings),      // size of lpszStrings array
                     0,                     // no binary data
                     strings,               // array of strings
                     null);                 // no binary data

        DeregisterEventSource(event_source);
    }
}

// Report and error with GetLastError() to the windows event log
static void svc_report_last_error(char *function_name) {
    char buffer[256];
    if (StringCbPrintfA(buffer, sizeof(buffer), "%s failed with %d", function_name, GetLastError()) == S_OK) {
        svc_report_error(buffer);
    }
}

// Reports status to the service control manager.
static void svc_report_status(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint) {
    g_service_status.dwCurrentState = dwCurrentState;
    g_service_status.dwWin32ExitCode = dwWin32ExitCode;
    g_service_status.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING) {
        g_service_status.dwControlsAccepted = 0;
    } else {
        g_service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    }

    if ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED)) {
        g_service_status.dwCheckPoint = 0;
    }

    SetServiceStatus(g_status_handle, &g_service_status);
}

static bool svc_install(void) {
    char unquoted_path[MAX_PATH];

    if (!GetModuleFileNameA(null, unquoted_path, sizeof(unquoted_path))) {
        printf("Cannot install service (%d)\n", GetLastError());
        return false;
    }

    char quoated_path[MAX_PATH];
    StringCbPrintfA(quoated_path, sizeof(quoated_path), "\"%s\"", unquoted_path);

    SC_HANDLE service_manager = OpenSCManagerA(null,                    // local computer
                                               null,                    // ServicesActive database
                                               SC_MANAGER_ALL_ACCESS);  // full access rights

    if (null == service_manager) {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return false;
    }

    SC_HANDLE service = CreateServiceA(service_manager,     // SCM database
                                       SERVICE_NAME,              // name of service
                                       SERVICE_NAME,              // service name to display
                                       SERVICE_ALL_ACCESS,        // desired access
                                       SERVICE_WIN32_OWN_PROCESS, // service type
                                       SERVICE_AUTO_START,        // start type
                                       SERVICE_ERROR_NORMAL,      // error control type
                                       quoated_path,               // path to service's binary
                                       null,                      // no load ordering group
                                       null,                      // no tag identifier
                                       null,                      // no dependencies
                                       null,                      // LocalSystem account
                                       null);                     // no password

    if (service == null) {
        printf("CreateService failed (%d)\n", GetLastError());
        CloseServiceHandle(service_manager);
        return false;
    } else {
        printf(ANSI_FG_GREEN "Service installed successfully\n" ANSI_RESET);
        CloseServiceHandle(service);
        CloseServiceHandle(service_manager);
        return false;
    }
}

static bool svc_uninstall(void) {
    bool retval = true;

    SC_HANDLE existing_service = null;
    SC_HANDLE service_manager = OpenSCManagerA(null,                    // local computer
                                               null,                    // ServicesActive database
                                               SC_MANAGER_ALL_ACCESS);  // full access rights

    if (service_manager == null) {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        retval = false;
        goto exit;
    }

    existing_service = OpenServiceA(service_manager, SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (existing_service) {
        printf("Found existing %s\n", SERVICE_NAME);

        SERVICE_STATUS existing_service_status = { 0 };
        if (!ControlService(existing_service, SERVICE_CONTROL_STOP, &existing_service_status)) {
            if (GetLastError() != ERROR_SERVICE_NOT_ACTIVE) {
                printf("ControlService failed (%d)\n", GetLastError());
                retval = false;
                goto exit;
            }
        }

        printf("Stopped existing service\n");

        if (!DeleteService(existing_service)) {
            printf("DeleteService failed (%d)\n", GetLastError());
            retval = false;
            goto exit;
        }

        printf("Uninstalled existing service\n");
    } else {
        if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST) {
            printf("OpenServiceA failed (%d)\n", GetLastError());
            retval = false;
            goto exit;
        }
    }

exit:
    CloseServiceHandle(existing_service);
    CloseServiceHandle(service_manager);
    return true;
}

static void WINAPI svc_ctrl_handler(DWORD dwCtrl) {
    switch (dwCtrl) {
        case SERVICE_CONTROL_STOP:
            svc_report_status(SERVICE_STOP_PENDING, NO_ERROR, 0);
            server_begin_shutdown(&g_server);
            svc_report_status(g_service_status.dwCurrentState, NO_ERROR, 0);
            return;
    }
}

static void WINAPI svc_main(DWORD dwArgc, LPSTR *lpszArgv) {
    g_status_handle = RegisterServiceCtrlHandlerA(SERVICE_NAME, svc_ctrl_handler);

    if (!g_status_handle) {
        svc_report_last_error("RegisterServiceCtrlHandler");
        return;
    }

    // These SERVICE_STATUS members remain as set here
    g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_service_status.dwServiceSpecificExitCode = 0;

    // Report initial status to the SCM
    svc_report_status(SERVICE_START_PENDING, NO_ERROR, 3000);
    svc_report_status(SERVICE_RUNNING, NO_ERROR, 0);

    // Run named pipe server until an error is encountered or SvcCtrlHandler calls service_begin_shutdown.
    server_create(&g_server);
    server_run(&g_server);
    server_destroy(&g_server);

    svc_report_status(SERVICE_STOPPED, NO_ERROR, 0);
}

////////////////////////////////////////////////////////////////
// rune: CLI

int cli_main(int argc, char **argv) {
    // TODO(rune): More user friendly CLI

    // rune: Run server rom command line
    if (argc == 2 && _strcmpi(argv[1], "server") == 0) {
        server *server = heap_alloc(sizeof(*server), false); // TODO(rune): Report error
        server_create(server);
        server_run(server);
        server_destroy(server);
        return 0;
    }

    // rune: Install service
    if (argc == 2 && _strcmpi(argv[1], "install") == 0) {
        if (svc_uninstall()) {
            svc_install();
        }

        return 0;
    }

    // rune: Uninstall service
    if (argc == 2 && _strcmpi(argv[1], "uninstall") == 0) {
        svc_uninstall();
        return 0;
    }

    // rune: Benchmarks
    if (argc == 2 && _strcmpi(argv[1], "bench") == 0) {
        char *strings[] = {
            "fK",
            "fka",
            "sKLa",
            "fka.",
            "kdNet",
            "textal",
            "abcdefghjiasdjkalsddhj",
        };

        for (int i = 0; i < countof(strings); i++) {

            quickfind_params params = { 0 };
            params.return_count = 100;
            params.stop_count   = UINT64_MAX;
            params.skip_count   = 0;
            params.text         = strings[i];
            params.text_length  = (u32)strlen(strings[i]);

            buffer result_buffer = {
                .data = heap_alloc(MEGABYTES(1), false),
                .capacity = MEGABYTES(1),
            };

            LARGE_INTEGER frequency;
            LARGE_INTEGER performance_count_start;
            LARGE_INTEGER performance_count_end;

            QueryPerformanceFrequency(&frequency);

            f64 sum = 0;
            u64 result_count;

            int iteration_count = 100;
            for (int i = 0; i < iteration_count; i++) {
                buffer_reset(&result_buffer);

                QueryPerformanceCounter(&performance_count_start);

                quickfind_results results = { 0 };
                quickfind_open(&params, &results, 0, 0);
                result_count = quickfind_get_found_count(&results);
                quickfind_close(&results);

                QueryPerformanceCounter(&performance_count_end);

                LONGLONG performance_diff = performance_count_end.QuadPart - performance_count_start.QuadPart;
                f64 duration              = ((f64)performance_diff * 1000.0) / ((f64)frequency.QuadPart);

                sum += duration;
            }

            heap_free(result_buffer.data);

            f64 time = (f64)sum / (f64)iteration_count;

            printf("Average: %f ms (len = %i, count = %llu) (\"%s\")\n", time, (u32)strlen(strings[i]), result_count, strings[i]);
        }

        return 0;
    }

    // rune: If there's not arguments we assume the service control manager started the exe.
    if (argc == 1) {
        SERVICE_TABLE_ENTRYA dispatch_table[] =
        {
            { SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONA)svc_main },
            { null, null }
        };

        if (!StartServiceCtrlDispatcherA(dispatch_table)) {
            svc_report_last_error("StartServiceCtrlDispatcher");
        }
    }

    return 0;
}
