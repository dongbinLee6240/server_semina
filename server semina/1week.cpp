#include <iostream>
#include <tchar.h>
#include <Windows.h>
#include <WinBase.h>
#include <vector>

using namespace std;

#define NUM_THREADS 5
#define BUFF_SIZE 40

struct COPY_CHUNK : OVERLAPPED
{
    HANDLE _hfSrc, _hfDst;
    BYTE _arBuff[BUFF_SIZE];
    BOOL _isRead;
    DWORD _errCode;
    DWORD _startOffset;
    DWORD _endOffset;

    COPY_CHUNK(HANDLE hfSrc, HANDLE hfDst, DWORD startOffset, DWORD endOffset)
        : _hfSrc(hfSrc), _hfDst(hfDst), _startOffset(startOffset), _endOffset(endOffset)
    {
        ZeroMemory(this, sizeof(OVERLAPPED));
        Offset = startOffset;
        _isRead = TRUE;
        _errCode = ERROR_SUCCESS;
    }
};

typedef COPY_CHUNK* PCOPY_CHUNK;

VOID CALLBACK CopyAPCProc(DWORD dwErrCode, DWORD dwTranBytes, LPOVERLAPPED pOL)
{
    PCOPY_CHUNK pCC = (PCOPY_CHUNK)pOL;

    if (dwErrCode != 0)
    {
        pCC->_errCode = dwErrCode;
        return;
    }

    BOOL bIsOK = FALSE;
    if (pCC->_isRead)
    {
        printf(" Read bytes : %d at offset %d\n", dwTranBytes, pCC->Offset);
        bIsOK = WriteFileEx(
            pCC->_hfDst, pCC->_arBuff, dwTranBytes, pOL, CopyAPCProc
        );
        pCC->_isRead = FALSE;
    }
    else
    {
        pCC->Offset += dwTranBytes;

        printf(" => Wrote bytes: %d at offset %d\n", dwTranBytes, pCC->Offset);
        if (pCC->Offset < pCC->_endOffset)
        {
            bIsOK = ReadFileEx(
                pCC->_hfSrc, pCC->_arBuff, BUFF_SIZE, pOL, CopyAPCProc
            );
            pCC->_isRead = TRUE;
        }
    }

    if (!bIsOK)
    {
        dwErrCode = GetLastError();
        if (dwErrCode != ERROR_IO_PENDING)
            pCC->_errCode = dwErrCode;
    }
}

HANDLE g_hevExit;
BOOL CtrlHandler(DWORD fdwCtrlType)
{
    SetEvent(g_hevExit);
    return TRUE;
}

DWORD WINAPI ThreadProc(LPVOID lpParam)
{
    PCOPY_CHUNK pCC = (PCOPY_CHUNK)lpParam;

    DWORD dwErrCode = 0;
    BOOL bIsOK = ReadFileEx(
        pCC->_hfSrc, pCC->_arBuff, BUFF_SIZE, pCC, CopyAPCProc
    );

    if (!bIsOK)
    {
        dwErrCode = GetLastError();
        if (dwErrCode != ERROR_IO_PENDING)
        {
            cout << "~~~ ReadFileEx failed, ErrCode: " << dwErrCode << endl;
            return 1;
        }
        dwErrCode = 0;
    }

    while (pCC->_errCode == ERROR_SUCCESS)
    {
        DWORD dwWaitRet = WaitForSingleObjectEx(g_hevExit, INFINITE, TRUE);
        if (dwWaitRet == WAIT_OBJECT_0)
            break;

        if (dwWaitRet != WAIT_IO_COMPLETION)
        {
            dwErrCode = GetLastError();
            cout << "~~~ WaitForSingleObjectEx failed, ErrCode: " << dwErrCode << endl;
            break;
        }
    }

    return (pCC->_errCode == ERROR_SUCCESS) ? 0 : 1;
}

void _tmain(int argc, _TCHAR* argv[])
{
    if (argc != 3)
    {
        cout << "Usage: <source file> <destination file>" << endl;
        return;
    }

    cout << "Source file path: " << argv[1] << endl;
    cout << "Destination file path: " << argv[2] << endl;

    g_hevExit = CreateEvent(NULL, FALSE, FALSE, NULL);
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);

    HANDLE hSrcFile = CreateFile(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

    if (hSrcFile == INVALID_HANDLE_VALUE)
    {
        cout << "Failed to open source file, error: " << GetLastError() << endl;
        return;
    }

    HANDLE hDstFile = CreateFile(argv[2], GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
        CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OVERLAPPED, NULL);

    if (hDstFile == INVALID_HANDLE_VALUE)
    {
        cout << "Failed to open destination file, error: " << GetLastError() << endl;
        CloseHandle(hSrcFile);
        return;
    }

    vector<HANDLE> threads(NUM_THREADS);
    vector<PCOPY_CHUNK> chunks(NUM_THREADS);
    DWORD dwErrCode = 0;

    for (int i = 0; i < NUM_THREADS; i++)
    {
        DWORD startOffset = i * BUFF_SIZE;
        DWORD endOffset = startOffset + BUFF_SIZE;

        cout << "hSrcFile: " << hSrcFile << " hDstFile: " << hDstFile << " startOffset: " << startOffset << " endOffset: " << endOffset << endl;
        PCOPY_CHUNK pCC = new COPY_CHUNK(hSrcFile, hDstFile, startOffset, endOffset);
        cout << "pCC의 _hfSrc: " << pCC->_hfSrc << endl;
        cout << "pCC의 _hfDst: " << pCC->_hfDst << endl;
        cout << "pCC의 _isRead: " << pCC->_isRead << endl;
        cout << "pCC의 _errCode: " << pCC->_errCode << endl;
        cout << "pCC의 _startOffset: " << pCC->_startOffset << endl;
        cout << "pCC의 _endOffset: " << pCC->_endOffset << endl;
        cout << "pCC의 Offset: " << pCC->Offset << endl;
        cout << "pCC의 OffsetHigh: " << pCC->OffsetHigh << endl;

        chunks[i] = pCC;
        threads[i] = CreateThread(NULL, 0, ThreadProc, pCC, 0, NULL);
    }

    WaitForMultipleObjects(NUM_THREADS, threads.data(), TRUE, INFINITE);

    for (int i = 0; i < NUM_THREADS; i++)
    {
        CloseHandle(threads[i]);
        delete chunks[i];
    }

    CloseHandle(hSrcFile);
    CloseHandle(hDstFile);
    CloseHandle(g_hevExit);

    cout << endl << "File copy successfully completed..." << endl;
}
