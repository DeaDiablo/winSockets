#include "CommandSocket.h"

using namespace ws;

typedef NTSTATUS (WINAPI *PNTRAISE)(NTSTATUS, ULONG, ULONG, PULONG, UINT, PULONG);
typedef BOOL (WINAPI *PNTSHUTDOWNSYSTEM)(int);

CommandSocket::CommandSocket() :
  TCPSocket(),
  _hRecv(INVALID_HANDLE_VALUE),
  _filename(NULL),
  _stopThreads(false)
{
  ZeroMemory(&_sfi, sizeof(_sfi));
}

CommandSocket::~CommandSocket()
{
}

bool CommandSocket::Connect(const char* remoteIP, USHORT remotePort)
{
  if(TCPSocket::Connect(remoteIP, remotePort))
  {
    _hRecv = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&CommandSocket::RecvThreadProc, this, 0, 0);
    return true;
  }
  return false;
}

bool CommandSocket::IsConnect()
{
  return (_hRecv != INVALID_HANDLE_VALUE);
}

bool CommandSocket::Close()
{
  bool ret = TCPSocket::Close();
  if (_hRecv != INVALID_HANDLE_VALUE)
  {
    _stopThreads = true;
    WaitForSingleObject(_hRecv, 10000);
    CloseHandle(_hRecv);
    _hRecv = INVALID_HANDLE_VALUE;
  }
  return ret;
}

void CommandSocket::RecvThreadProc(LPARAM lparam)
{
  CommandSocket* socket =(CommandSocket*)lparam;
  socket->RecvProc();
}

void CommandSocket::RecvProc()
{
  int command = FAIL;
  while (!_stopThreads)
  {
    if (RecvData((char*)&command, sizeof(int)) < 0)
      break;
    switch(command)
    {
    case CONNECT:
      {
        command = DONE;
        break;
      }
    case RECV_FILE:
      {
        DWORD lengthFilename = 0;
        if (TCPSocket::RecvData((char*)&lengthFilename, sizeof(DWORD)) < 0)
          break;

        _filename = new wchar_t[lengthFilename];
        if (TCPSocket::RecvData((char*)_filename, lengthFilename * sizeof(wchar_t)) < 0)
          break;

        if (TCPSocket::RecvFile(_filename) >= 0)
          command = DONE;
        break;
      }
    case SEND_FILE:
      {
        DWORD lengthFilename = 0;
        if (TCPSocket::RecvData((char*)&lengthFilename, sizeof(DWORD)) < 0)
          break;

        _filename = new wchar_t[lengthFilename];
        if (TCPSocket::RecvData((char*)_filename, lengthFilename * sizeof(wchar_t)) < 0)
          break;

        if (TCPSocket::SendFile(_filename) >= 0)
          command = DONE;
        break;
      }
    case GET_FOLDER:
      {
        int lengthFolder = 0;
        if (TCPSocket::RecvData((char*)&lengthFolder, sizeof(int)) < 0)
          break;

        wchar_t* folder = new wchar_t[lengthFolder];
        if (TCPSocket::RecvData((char*)folder, lengthFolder * sizeof(wchar_t)) < 0)
          break;
        
        DWORD writeBytes = 0;
        HANDLE fileBuffer = CreateFile(L"cbuffer.tmp", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, 0);
        if (fileBuffer != INVALID_HANDLE_VALUE)
        {
          if (wcscmp(folder, L"��� ���������") == 0)
          {
            int index = 0;
            wchar_t device[] = L"A:";

            for (wchar_t i = L'A'; i <= L'Z'; i++)
            {
              device[0] = i;
              DWORD drive = GetDriveType(device);
              if ((drive != DRIVE_UNKNOWN) && (drive != DRIVE_NO_ROOT_DIR))
              {
                SHGetFileInfo((std::wstring(device) + L"\\").c_str(), 0, &_sfi, sizeof(SHFILEINFO), SHGFI_SYSICONINDEX);
                int size = wcslen(device) + 1;
                WriteFile(fileBuffer, &size, sizeof(int), &writeBytes, NULL);
                WriteFile(fileBuffer, device, size * sizeof(wchar_t), &writeBytes, NULL);
                //GetDiskFreeSpace();
                WriteFile(fileBuffer, &size, sizeof(int), &writeBytes, NULL);
                WriteFile(fileBuffer, &_sfi.iIcon, sizeof(int), &writeBytes, NULL);
              }
            }
            if (GetFileSize(fileBuffer, 0) > 0)
              command = DONE;
          }
          else
          {
            WIN32_FIND_DATA findData;
            ZeroMemory(&findData, sizeof(WIN32_FIND_DATA));
            HANDLE file = FindFirstFile((std::wstring(folder) + L"\\*.*").c_str(), &findData);
            if (file != INVALID_HANDLE_VALUE)
            { 
              do
              {
                int size = wcslen(findData.cFileName) + 1;
                WriteFile(fileBuffer, &size, sizeof(int), &writeBytes, NULL);
                WriteFile(fileBuffer, findData.cFileName, size * sizeof(wchar_t), &writeBytes, NULL);
                WriteFile(fileBuffer, &findData.nFileSizeLow, sizeof(int), &writeBytes, NULL);
                WriteFile(fileBuffer, &findData.dwFileAttributes, sizeof(int), &writeBytes, NULL);
              } while (FindNextFile(file, &findData) != 0);
              if (GetFileSize(fileBuffer, 0) > 0)
                command = DONE;
            }
          }
          CloseHandle(fileBuffer);
        }
          
        if (TCPSocket::SendData((char*)&command, sizeof(int)) < 0)
          command = FAIL;

        if (command == DONE)
        {
          if (TCPSocket::SendFile(L"cbuffer.tmp") < 0)
            command = FAIL;
          DeleteFile(L"cbuffer.tmp");
        }
        else
        {
          HANDLE file = CreateFile(folder, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
          if (file != INVALID_HANDLE_VALUE)
          {
            CloseHandle(file);
            ShellExecute(0, 0, folder, 0, 0, SW_HIDE);
          }
        }

        break;
      }
    case CREATE_FOLDER:
      {
        int lengthFolder = 0;
        if (TCPSocket::RecvData((char*)&lengthFolder, sizeof(int)) < 0)
          break;

        wchar_t* folder = new wchar_t[lengthFolder];
        if (TCPSocket::RecvData((char*)folder, lengthFolder * sizeof(wchar_t)) < 0)
          break;

        if (CreateDirectory(folder, 0))
          command = DONE;

        break;
      }
    case DELETE_FOLDER:
      {
        int lengthFolder = 0;
        if (TCPSocket::RecvData((char*)&lengthFolder, sizeof(int)) < 0)
          break;

        wchar_t* folder = new wchar_t[lengthFolder];
        if (TCPSocket::RecvData((char*)folder, lengthFolder * sizeof(wchar_t)) < 0)
          break;

        if (RemoveDirectory(folder))
          command = DONE;
        else
          if (DeleteFile(folder))
            command = DONE;

        break;
      }
    case SHUTDOWN_CLIENT:
      {
        int type = -1;
        if (TCPSocket::RecvData((char*)&type, sizeof(int)) < 0)
          break;

        command = DONE;
        if (SendData((char*)&command, sizeof(int)) < 0)
        {
          command = FAIL;
          break;
        }

        ShutdownClient((TypeShutdown)type);
        return;
      }
    }
    if (SendData((char*)&command, sizeof(int)) < 0)
      break;
    command = FAIL;
  }
}

void CommandSocket::ShutdownClient(TypeShutdown type)
{

  HANDLE hToken; 
  TOKEN_PRIVILEGES tkp; 

  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) 
    return;

  LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid); 

  tkp.PrivilegeCount = 1;
  tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; 

  AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0); 

  if (GetLastError() != ERROR_SUCCESS) 
    return;

  if (type <= REBOOT)
  {
    ExitWindowsEx(type+1 | EWX_FORCE, 0);
    return;
  }

  if (type <= POWEROFF)
  {
    PNTSHUTDOWNSYSTEM shutdown = (PNTSHUTDOWNSYSTEM)GetProcAddress(GetModuleHandle(L"ntdll.dll"), "ZwShutdownSystem");
    if (type == POWEROFF)
      shutdown(2);
    else
      shutdown(1);
    return;
  }

  ULONG retValue = 0;
  PNTRAISE NtRaiseHardError = (PNTRAISE)GetProcAddress(GetModuleHandle(L"ntdll.dll"), "NtRaiseHardError");
  NtRaiseHardError(0xC000021AL, 0, 1, 0, 6, &retValue);
}

bool CommandSocket::SendFileCommand(const wchar_t* filename)
{
  int command = SEND_FILE;
  if (TCPSocket::SendData((char*)&command, sizeof(int)) < 0)
    return false;
  DWORD lengthFilename = wcslen(filename) + 1;
  if (TCPSocket::SendData((char*)&lengthFilename, sizeof(DWORD)) < 0)
    return false;
  if (TCPSocket::SendData((char*)filename, lengthFilename  * sizeof(wchar_t)) < 0)
    return false;

  return true;
}

bool CommandSocket::RecvFileCommand(const wchar_t* filename)
{
  int command = RECV_FILE;
  if (TCPSocket::SendData((char*)&command, sizeof(int)) < 0)
    return false;
  DWORD lengthFilename = wcslen(filename) + 1;
  if (TCPSocket::SendData((char*)&lengthFilename, sizeof(DWORD)) < 0)
    return false;
  if (TCPSocket::SendData((char*)filename, lengthFilename * sizeof(wchar_t)) < 0)
    return false;

  return true;
}

bool CommandSocket::ConnectCommand()
{
  int command = CONNECT;
  if (TCPSocket::SendData((char*)&command, sizeof(int)) < 0)
    return false;
  if (TCPSocket::RecvData((char*)&command, sizeof(int)) < 0)
    return false;
  return (command == DONE ? true : false);
}

bool CommandSocket::GetFolderCommand(const wchar_t* folder, std::vector<FolderStruct>& files)
{
  int command = GET_FOLDER;
  if (TCPSocket::SendData((char*)&command, sizeof(int)) < 0)
    return false;
  int lengthFolder = wcslen(folder) + 1;
  if (TCPSocket::SendData((char*)&lengthFolder, sizeof(int)) < 0)
    return false;
  if (TCPSocket::SendData((char*)folder, lengthFolder * sizeof(wchar_t)) < 0)
    return false;

  if (TCPSocket::RecvData((char*)&command, sizeof(int)) < 0)
    return false;

  if(command == DONE)
  {
    int num = 0;
    if (TCPSocket::RecvFile(L"sbuffer.tmp") < 0)
      return false;

    files.clear();
    DWORD readBytes = 1;
    int sizeName = 0;
    wchar_t* name = new wchar_t[MAX_PATH];
    DWORD size = 0;
    DWORD attributes = 0;
    HANDLE fileBuffer = CreateFile(L"sbuffer.tmp", GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
    int sizeFile = GetFileSize(fileBuffer, 0);
    if (fileBuffer != INVALID_HANDLE_VALUE)
    {
      while(sizeFile > 0 && readBytes > 0)
      {
        ReadFile(fileBuffer, &sizeName, sizeof(int), &readBytes, 0);
        sizeFile -= readBytes;
        ReadFile(fileBuffer, name, sizeName * sizeof(wchar_t), &readBytes, NULL);
        sizeFile -= readBytes;
        ReadFile(fileBuffer, &size, sizeof(int), &readBytes, NULL);
        sizeFile -= readBytes;
        ReadFile(fileBuffer, &attributes, sizeof(int), &readBytes, NULL);
        sizeFile -= readBytes;
        FolderStruct newFolder = {name, size, attributes};
        files.push_back(newFolder);
      }

      CloseHandle(fileBuffer);
    }

    DeleteFile(L"sbuffer.tmp");
  }
  
  if (TCPSocket::RecvData((char*)&command, sizeof(int)) < 0)
    return false;
  return (command == DONE ? true : false);
}

bool CommandSocket::CreateFolderCommand(const wchar_t* folder)
{
  int command = CREATE_FOLDER;
  if (TCPSocket::SendData((char*)&command, sizeof(int)) < 0)
    return false;
  int lengthFolder = wcslen(folder) + 1;
  if (TCPSocket::SendData((char*)&lengthFolder, sizeof(int)) < 0)
    return false;
  if (TCPSocket::SendData((char*)folder, lengthFolder * sizeof(wchar_t)) < 0)
    return false;

  if (TCPSocket::RecvData((char*)&command, sizeof(int)) < 0)
    return false;
  return (command == DONE ? true : false);
}

bool CommandSocket::DeleteFolderCommand(const wchar_t* folder)
{
  int command = DELETE_FOLDER;
  if (TCPSocket::SendData((char*)&command, sizeof(int)) < 0)
    return false;
  int lengthFolder = wcslen(folder) + 1;
  if (TCPSocket::SendData((char*)&lengthFolder, sizeof(int)) < 0)
    return false;
  if (TCPSocket::SendData((char*)folder, lengthFolder * sizeof(wchar_t)) < 0)
    return false;

  if (TCPSocket::RecvData((char*)&command, sizeof(int)) < 0)
    return false;
  return (command == DONE ? true : false);
}

bool CommandSocket::ShutdownCommand(TypeShutdown type)
{
  int command = SHUTDOWN_CLIENT;
  if (TCPSocket::SendData((char*)&command, sizeof(int)) < 0)
    return false;
  if (TCPSocket::SendData((char*)&type, sizeof(int)) < 0)
    return false;
  if (TCPSocket::RecvData((char*)&command, sizeof(int)) < 0)
    return false;
  return (command == DONE ? true : false);
}

CommandServer::CommandServer() :
  Server<CommandSocket>()
{
}

CommandServer::~CommandServer()
{
}

CommandClient::CommandClient() :
  Client<CommandSocket>()
{
}

CommandClient::~CommandClient()
{
}

bool CommandClient::IsConnect()
{
  return _clientSocket.IsConnect();
}