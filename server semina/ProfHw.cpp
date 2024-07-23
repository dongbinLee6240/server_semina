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
        printf(" => Read bytes : %d , Offset: %d\n", dwTranBytes, pCC->Offset);
        bIsOK = WriteFileEx(
            pCC->_hfDst, pCC->_arBuff, dwTranBytes, pOL, CopyAPCProc
        );
        pCC->_isRead = FALSE;
    }

    else
    {
        pCC->Offset += dwTranBytes;

        printf(" => Wrote bytes: %d , Offset: %d\n", dwTranBytes, pCC->Offset);
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

        PCOPY_CHUNK pCC = new COPY_CHUNK(hSrcFile, hDstFile, startOffset, endOffset);

        chunks[i] = pCC; //동적 해제용 chunks
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

/* 먼저 BUFFSIZE를 20으로 설정한 경우: pCC->arBuff의 크기가 20으로 설정되는 것을 확인
*  하지만 pCC->arBuff에 저장된 숫자들은 01234567890123456789 
*  즉 arBuff[0] ~ arBuff[9]까지 저장된 숫자와 arBuff[10] ~ arBuff[20]까지 저장된 숫자가 같음
*  -> 즉 BUFF_SIZE를 20으로 설정하면 50개의 숫자만 복사가 됨. 하지만 dwTranBytes는 20으로 표기됨.
*  BUFF_SIZE를 40으로 설정하면 100개의 숫자가 복사됨
*/
