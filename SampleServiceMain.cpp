

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <tchar.h>
#include <fstream>
#include <wtsapi32.h>
#include <userenv.h>
#include <strsafe.h>
#include <codecvt>

#define SVCNAME TEXT("Sample Service")

SERVICE_STATUS        g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;

VOID WINAPI ServiceMain (DWORD argc, LPTSTR *argv);
VOID WINAPI ServiceCtrlHandler (DWORD);
DWORD WINAPI ServiceWorkerThread (LPVOID lpParam);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::wstring s2ws(const std::string str)
{
    typedef std::codecvt_utf8<wchar_t> convert_typeX;
    std::wstring_convert<convert_typeX, wchar_t> converterX;

    return converterX.from_bytes(str);
}

struct handle_data {
    unsigned long process_id;
    HWND window_handle;
};

BOOL is_main_window(HWND handle)
{
    return GetWindow(handle, GW_OWNER) == (HWND)0 && IsWindowVisible(handle);
}

static BOOL CALLBACK callback_EnumWindowsPrintTitle(HWND hWnd, LPARAM lparam) {
    std::ofstream log;
    log.open("C:\\Users\\Public\\Documents\\enum-windows.txt", std::ios::out | std::ios::ate | std::ios::app);

    int length = GetWindowTextLength(hWnd);
    char* buffer = new char[length + 1];
    GetWindowTextA(hWnd, (LPSTR)buffer, length + 1);
    std::string windowTitle(buffer);
    delete[] buffer;

    unsigned long process_id = 0;
    GetWindowThreadProcessId(hWnd, &process_id);

    // List visible windows with a non-empty title
    /*if (IsWindowVisible(hWnd) && length != 0) {
        std::cout << "PID:" << process_id << "|HWND:" << hWnd << "|  " << windowTitle << std::endl;
    }*/

    log << "PID:" << process_id << "|HWND:" << hWnd << "|  " << windowTitle << std::endl;
    log.close();

    return TRUE;
}

BOOL CALLBACK callback_EnumWindowStations(LPSTR lpszWindowStation, LPARAM lparam)
{
    std::ofstream log;
    log.open("C:\\Users\\Public\\Documents\\find-window-station.txt", std::ios::out | std::ios::ate | std::ios::app);

    std::string station(lpszWindowStation);
    log << "Window Station: " << station << std::endl;
    log.close();

    return TRUE;
}

BOOL CALLBACK callback_EnumDesktops(LPSTR lpszDesktop, LPARAM lparam)
{
    std::ofstream log;
    log.open("C:\\Users\\Public\\Documents\\enum-desktops.txt", std::ios::out | std::ios::ate | std::ios::app);

    std::string desktop(lpszDesktop);
    log << "Desktop: " << desktop << std::endl;
    log.close();

    return TRUE;
}

BOOL CALLBACK enum_windows_callback(HWND handle, LPARAM lParam)
{
    handle_data& data = *(handle_data*)lParam;
    unsigned long process_id = 0;
    GetWindowThreadProcessId(handle, &process_id);
    if (data.process_id != process_id || !is_main_window(handle))
        return TRUE;
    data.window_handle = handle;
    return FALSE;
}

BOOL CALLBACK callback_FindWindowByProcID(HWND handle, LPARAM lParam)
{
    std::ofstream log;
    log.open("C:\\Users\\Public\\Documents\\find-window.txt", std::ios::out | std::ios::ate | std::ios::app);

    handle_data& data = *(handle_data*)lParam;
    unsigned long process_id = 0;
    DWORD thread_id = GetWindowThreadProcessId(handle, &process_id);
    log << "PID: (" << process_id << ") TID:(" << thread_id << ")" << std::endl;

    //if (data.process_id != process_id || !is_main_window(handle))
    if (data.process_id != process_id)
    {
        log.close();
        return TRUE;
    }
        

    data.window_handle = handle;
    log.close();
    return FALSE;
}

HWND find_main_window(unsigned long process_id)
{
    handle_data data;
    data.process_id = process_id;
    data.window_handle = 0;
    //EnumWindows(callback_EnumWindowsPrintTitle, (LPARAM)&data);
    EnumDesktopWindows(NULL, callback_EnumWindowsPrintTitle, NULL);

    return data.window_handle;
}

bool StartProgram(std::string name, HANDLE userToken, LPPROCESS_INFORMATION info)
{
    STARTUPINFO lpStartupInfo;
    PROCESS_INFORMATION lpProcessInfo;

    memset(&lpStartupInfo, 0, sizeof(lpStartupInfo));
    memset(&lpProcessInfo, 0, sizeof(lpProcessInfo));

    // Processes started by the logged-on user are associated with the Winsta0\default desktop.
    // https://learn.microsoft.com/en-us/windows/win32/winstation/window-station-and-desktop-creation
    //
    lpStartupInfo.lpDesktop = (LPWSTR)L"WinSta0\\Default";
    LPVOID EnvironmentBlock = NULL; 

    //CreateProcess(s2ws(name).c_str(),NULL, NULL, NULL, NULL, NULL, NULL, NULL, &lpStartupInfo,&lpProcessInfo)
    if (CreateEnvironmentBlock(&EnvironmentBlock, userToken, FALSE) == FALSE)
    {
        //std::cerr << "Error Could not get EnvironmentBlock" << std::endl;
    }

    if (CreateProcessAsUser(userToken, s2ws(name).c_str(), NULL, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT, EnvironmentBlock, NULL, &lpStartupInfo, &lpProcessInfo) == ERROR)
    {
        return false;
    }

    *info = lpProcessInfo;

    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define SERVICE_NAME  _T("My Sample Service")

int _tmain (int argc, TCHAR *argv[])
{
    OutputDebugString(_T("My Sample Service: Main: Entry"));

    SERVICE_TABLE_ENTRY ServiceTable[] = 
    {
        {SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION) ServiceMain},
        {NULL, NULL}
    };

    if (StartServiceCtrlDispatcher (ServiceTable) == FALSE)
    {
       OutputDebugString(_T("My Sample Service: Main: StartServiceCtrlDispatcher returned error"));
       return GetLastError ();
    }

    OutputDebugString(_T("My Sample Service: Main: Exit"));
    return 0;
}


VOID WINAPI ServiceMain (DWORD argc, LPTSTR *argv)
{
    DWORD Status = E_FAIL;

    OutputDebugString(_T("My Sample Service: ServiceMain: Entry"));

    g_StatusHandle = RegisterServiceCtrlHandler (SERVICE_NAME, ServiceCtrlHandler);

    if (g_StatusHandle == NULL) 
    {
        OutputDebugString(_T("My Sample Service: ServiceMain: RegisterServiceCtrlHandler returned error"));
        goto EXIT;
    }

    // Tell the service controller we are starting
    ZeroMemory (&g_ServiceStatus, sizeof (g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE) 
    {
        OutputDebugString(_T("My Sample Service: ServiceMain: SetServiceStatus returned error"));
    }

    /* 
     * Perform tasks neccesary to start the service here
     */
    OutputDebugString(_T("My Sample Service: ServiceMain: Performing Service Start Operations"));

    // Create stop event to wait on later.
    g_ServiceStopEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
    if (g_ServiceStopEvent == NULL) 
    {
        OutputDebugString(_T("My Sample Service: ServiceMain: CreateEvent(g_ServiceStopEvent) returned error"));

        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        g_ServiceStatus.dwCheckPoint = 1;

        if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
	    {
		    OutputDebugString(_T("My Sample Service: ServiceMain: SetServiceStatus returned error"));
	    }
        goto EXIT; 
    }    

    // Tell the service controller we are started
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
    {
	    OutputDebugString(_T("My Sample Service: ServiceMain: SetServiceStatus returned error"));
    }

    // Start the thread that will perform the main task of the service
    HANDLE hThread = CreateThread (NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

    OutputDebugString(_T("My Sample Service: ServiceMain: Waiting for Worker Thread to complete"));

    // Wait until our worker thread exits effectively signaling that the service needs to stop
    WaitForSingleObject (hThread, INFINITE);
    
    OutputDebugString(_T("My Sample Service: ServiceMain: Worker Thread Stop Event signaled"));
    
    
    /* 
     * Perform any cleanup tasks
     */
    OutputDebugString(_T("My Sample Service: ServiceMain: Performing Cleanup Operations"));

    CloseHandle (g_ServiceStopEvent);

    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 3;

    if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
    {
	    OutputDebugString(_T("My Sample Service: ServiceMain: SetServiceStatus returned error"));
    }
    
    EXIT:
    OutputDebugString(_T("My Sample Service: ServiceMain: Exit"));

    return;
}


VOID WINAPI ServiceCtrlHandler (DWORD CtrlCode)
{
    OutputDebugString(_T("My Sample Service: ServiceCtrlHandler: Entry"));

    switch (CtrlCode) 
	{
     case SERVICE_CONTROL_STOP :

        OutputDebugString(_T("My Sample Service: ServiceCtrlHandler: SERVICE_CONTROL_STOP Request"));

        if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
           break;

        /* 
         * Perform tasks neccesary to stop the service here 
         */
        
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        g_ServiceStatus.dwWin32ExitCode = 0;
        g_ServiceStatus.dwCheckPoint = 4;

        if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
			OutputDebugString(_T("My Sample Service: ServiceCtrlHandler: SetServiceStatus returned error"));
		}

        // This will signal the worker thread to start shutting down
        SetEvent (g_ServiceStopEvent);

        break;

     default:
         break;
    }

    OutputDebugString(_T("My Sample Service: ServiceCtrlHandler: Exit"));
}

void DisplayLastError(LPCTSTR lpszFunction)
{
    // Retrieve the system error message for the last-error code
    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    DWORD dw = GetLastError();

    std::wofstream log;
    log.open("C:\\Users\\Public\\Documents\\error_log.txt", std::ios::out | std::ios::ate | std::ios::app);

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, NULL);

    // Display the error message and exit the process

    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
        (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
    StringCchPrintf((LPTSTR)lpDisplayBuf,
        LocalSize(lpDisplayBuf) / sizeof(TCHAR),
        TEXT("%s failed with error %d: %s"),
        lpszFunction, dw, lpMsgBuf);

    std::wstring msg((LPTSTR)lpMsgBuf);
    std::wstring display((LPTSTR)lpDisplayBuf);

    log << "Message : " << msg << std::endl;
    log << "Display : " << display << std::endl;

    //MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    log.close();
}


DWORD WINAPI ServiceWorkerThread(LPVOID lpParam)
{
    std::string prog_name = "C:\\build\\KeepAlive\\KeepAlive.exe";

    std::ofstream myfile;
    myfile.open("C:\\Users\\Public\\Documents\\service_log.txt", std::ios::out | std::ios::ate | std::ios::app);

    DWORD activeSession = 0;
    HANDLE userToken = 0;

    for (int i = 0; i < 5; i++)
    {
        activeSession = WTSGetActiveConsoleSessionId();
        if (WTSQueryUserToken(activeSession, &userToken) == ERROR)
        {
            myfile << "Error getting user token! Trying again..." << std::endl;

            // Retry After 1 second
            Sleep(1000);
        }
    }

    if (!userToken)
        myfile << "Error getting user token!" << std::endl;
    else
        myfile << "Successfully acquired user token!!" << std::endl;

    if (userToken)
    {
        EnumWindowStationsA(callback_EnumWindowStations, NULL);

        PROCESS_INFORMATION proc_info;
        StartProgram(prog_name, userToken, &proc_info);
        myfile << "Started Program PID (" << proc_info.dwProcessId << ") TID(" << proc_info.dwThreadId << ")" << std::endl;

        Sleep(3000);

        if (!GenerateConsoleCtrlEvent(CTRL_CLOSE_EVENT, proc_info.dwThreadId))
        {
            DisplayLastError(L"GenerateConsoleCtrlEvent");
        }
    }

    myfile.close();
    return ERROR_SUCCESS;
}