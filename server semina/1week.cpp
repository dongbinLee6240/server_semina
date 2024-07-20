#include <iostream>
#include <tchar.h>
#include <Windows.h>
#include <WinBase.h>
#include <vector>

using namespace std;
#define NUM_THREADS 5
#define BUFF_SIZE 65536

struct COPY_CHUNK : OVERLAPPED
{
    HANDLE _hfSrc, _hfDst; // �ҽ� �� Ÿ�� ���� �ڵ�
    BYTE _arBuff[BUFF_SIZE]; // ���� ���縦 ���� ����
    BOOL _isRead;  // true: �б� �Ϸ�, false: ���� �Ϸ�
    DWORD _errCode;  // �����ڵ� ���� �ʵ�
    DWORD _startOffset;
    DWORD _endOffset;

    COPY_CHUNK(HANDLE hfSrc, HANDLE hfDst, DWORD startOffset, DWORD endOffset)
        :_hfSrc(hfSrc), _hfDst(hfDst), _startOffset(startOffset), _endOffset(endOffset)
    {
        memset(this, 0, sizeof(COPY_CHUNK));
        _isRead = TRUE; // �б� �Ϸ�
    }
};

typedef COPY_CHUNK* PCOPY_CHUNK;

#define HasOverlappedIoCompleted(lpOverlapped)\
(((DWORD)(lpOverlapped)->Internal) != STATUS_PENDING)

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
        printf(" => Read bytes : %d \n", pCC->Offset);
        bIsOK = WriteFileEx(
            pCC->_hfDst, pCC->_arBuff, dwTranBytes, pOL, CopyAPCProc
        );
        pCC->_isRead = FALSE;
    }
    else
    {
        pCC->Offset += dwTranBytes;

        printf(" => Wrote bytes: %d\n", pCC->Offset);
        bIsOK = ReadFileEx(
            pCC->_hfSrc, pCC->_arBuff, BUFF_SIZE, pOL, CopyAPCProc
        );

        pCC->_isRead = TRUE;
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
    //�б�
    BOOL bIsOK = ReadFileEx(
        pCC->_hfSrc, pCC->_arBuff, BUFF_SIZE, pCC, CopyAPCProc
    );

    if (!bIsOK) //bIsOK�� false�� ��� -> ReadFileEx ����
    {
        dwErrCode = GetLastError();
        if (dwErrCode != ERROR_IO_PENDING) //�б� ����
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

        if (dwWaitRet != WAIT_IO_COMPLETION) //��� ����
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

    g_hevExit = CreateEvent(NULL, FALSE, FALSE, NULL);
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);

    HANDLE hSrcFile = CreateFile(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);

    if (hSrcFile == INVALID_HANDLE_VALUE)
    {
        cout << "Failed to open source file, error: " << GetLastError() << endl;
        return;
    }

    HANDLE hDstFile = CreateFile(argv[2], GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);

    if (hDstFile == INVALID_HANDLE_VALUE)
    {
        cout << "Failed to open destination file, error: " << GetLastError() << endl;
        CloseHandle(hSrcFile);
        return;
    }
    vector<HANDLE> threads(NUM_THREADS);
    vector<PCOPY_CHUNK> chunks(NUM_THREADS);
    DWORD chunkSize = 20; //����
    DWORD dwErrCode = 0;
    for (int i = 0; i < NUM_THREADS; i++)
    {
        DWORD startOffset = i * chunkSize; //0 20 40 60 80
        DWORD endOffset = startOffset + chunkSize;  //20 40 60 80 100

        PCOPY_CHUNK pCC = new COPY_CHUNK(hSrcFile, hDstFile, startOffset, endOffset);
        chunks[i] = pCC;

        threads[i] = CreateThread(NULL, 0, ThreadProc, pCC, 0, NULL); //�񵿱� ������̹Ƿ� �� ���Ͽ� ���� ������ ���� ����
    }

    WaitForMultipleObjects(NUM_THREADS, threads.data(), TRUE, INFINITE);

    for (int i = 0; i < NUM_THREADS; i++) //close handle
    {
        CloseHandle(threads[i]);
        delete chunks[i];
    }
    CloseHandle(hSrcFile);
    CloseHandle(hDstFile);
    CloseHandle(g_hevExit);

    cout << endl << "File copy successfully completed..." << endl;
}
