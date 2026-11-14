#define _CRT_SECURE_NO_WARNINGS
#include "framework.h"
#include "Resource.h"
//#include "MetaString.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "ws2_32.lib")

#include <shlwapi.h>
#pragma comment (lib,"Shlwapi.lib")

#include "Utils.h"
#include "oicq\tea.h"
#include <string>
#include <map>
#include <fstream>
#include <iostream>
#include <codecvt>
#include <sstream>
#include <cstdio>
#include <cstdarg>
#include "MD5.h"
#include <thread>
#include <mutex>
#define TIMEOUT 5000

struct ButtonBitmapMapping {
	HWND buttonHandle;
	int bitmapIndex;
};

std::vector<HBITMAP> hBitmapButtons;
std::vector<ButtonBitmapMapping> buttonMappings;
extern BOOL OnInitDialog(HWND hDlg, const std::vector<int>& bitmapResources, const std::vector<HWND>& buttonHandles);
extern void OnPaintButton(HWND hWnd);

extern HINSTANCE hInst;
extern TCHAR GamePath[MAX_PATH];
extern TCHAR GameAppFile[MAX_PATH];
extern UINT LoginUin;
extern char LoginPwd[128];
UINT IP = 0;
USHORT Port = 0;
BYTE Key[16] = { 0x52, 0x7E, 0x60, 0x40, 0x63, 0x59, 0x59, 0x77, 0x41, 0x62, 0x32, 0x27, 0x3F, 0x78, 0x76, 0x73 };
std::string key = "R~`@cYYwAb2'?xvs";
char NewIP[64] = "";

int isinvite = 0;
int startdomin = 0;
int passmd5 = 1;
int Bit = 64;
const wchar_t* baseTitle = L"Launcher";
LPCTSTR resourceTypedb = _T("db");
LPCTSTR resourceTyperar = _T("rar");
std::wstring initialTitle;
extern BYTE GameKey[16];
HDC hMemDC2;
HANDLE hLoaderBG2;

constexpr int WM_MY_ASYNCSOCKET = (WM_APP + 1);
SOCKET hSocket = INVALID_SOCKET;
LONG_PTR BufferPtr = NULL;
LONG_PTR BufferSize = 0;
LONG_PTR BufferOffset = 0;
std::wstring version;

char* domainip = NULL;
//char *downip[32] = { NULL };

static std::once_flag gLoginConsoleOnce;
static std::mutex gLoginLogMutex;

static bool ShouldHashLoginPassword()
{
	char value[16] = { 0 };
	GetPrivateProfileStringA("ServerInfo1", "passmd5", "1", value, sizeof(value), ".\\ASpeed.ini");
	passmd5 = atoi(value) != 0 ? 1 : 0;
	return passmd5 != 0;
}

static void InitLoginConsole()
{
	std::call_once(gLoginConsoleOnce, []()
		{
			if (!AttachConsole(ATTACH_PARENT_PROCESS))
			{
				AllocConsole();
			}

			SetConsoleTitleW(L"Launcher Login Debug Log");
			SetConsoleOutputCP(CP_UTF8);
			SetConsoleCP(CP_UTF8);

			FILE* fp = nullptr;
			freopen_s(&fp, "CONOUT$", "w", stdout);
			freopen_s(&fp, "CONOUT$", "w", stderr);
			freopen_s(&fp, "CONIN$", "r", stdin);
			std::ios::sync_with_stdio(true);

			printf("[Login] console initialized\r\n");
			fflush(stdout);
		});
}

static void LoginLog(const char* format, ...)
{
	InitLoginConsole();

	char message[2048] = { 0 };
	va_list args;
	va_start(args, format);
	vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
	va_end(args);

	SYSTEMTIME now;
	GetLocalTime(&now);

	char line[2300] = { 0 };
	snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] %s\r\n",
		now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, message);

	{
		std::lock_guard<std::mutex> lock(gLoginLogMutex);
		printf("%s", line);
		fflush(stdout);
	}

	OutputDebugStringA(line);
}

static void LoginDumpPlaintext(const char* label, USHORT msgId, const BYTE* data, DWORD length)
{
	LoginLog("%s msgId=%u len=%lu", label, msgId, length);
	if (!data || length == 0)
	{
		return;
	}

	const DWORD bytesPerLine = 16;
	for (DWORD offset = 0; offset < length; offset += bytesPerLine)
	{
		char hex[bytesPerLine * 3 + 1] = { 0 };
		char ascii[bytesPerLine + 1] = { 0 };
		DWORD count = length - offset;
		if (count > bytesPerLine)
		{
			count = bytesPerLine;
		}

		char* hexCursor = hex;
		size_t hexLeft = sizeof(hex);
		for (DWORD i = 0; i < count; ++i)
		{
			BYTE ch = data[offset + i];
			int written = snprintf(hexCursor, hexLeft, "%02X%s", ch, (i + 1 < count) ? " " : "");
			if (written > 0 && (size_t)written < hexLeft)
			{
				hexCursor += written;
				hexLeft -= written;
			}
			ascii[i] = (ch >= 0x20 && ch <= 0x7E) ? (char)ch : '.';
		}

		LoginLog("%s +%04lX  %-47s  |%s|", label, offset, hex, ascii);
	}
}

static const char* SocketEventName(WORD event)
{
	switch (event)
	{
	case FD_READ:
		return "FD_READ";
	case FD_WRITE:
		return "FD_WRITE";
	case FD_CLOSE:
		return "FD_CLOSE";
	default:
		return "UNKNOWN";
	}
}

int GetRadioStatus(HWND hDlg) {
	if (IsDlgButtonChecked(hDlg, IDC_RADIO1) == BST_CHECKED) {
		return 1;
	}
	else if (IsDlgButtonChecked(hDlg, IDC_RADIO2) == BST_CHECKED) {
		return 2;
	}
}


std::wstring charToWstringUsingConverter(const char* str) {
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	return converter.from_bytes(str);
}

void lookdns(char* domain)
{
	char** pptr = NULL;
	HOSTENT* pHostEntry = gethostbyname(domain);
	if (pHostEntry != NULL)
	{
		int i = 0;
		char* szIpBuff = NULL;
		for (i = 0, pptr = pHostEntry->h_addr_list; *pptr != NULL; ++pptr, i++)
		{
			szIpBuff = new char[32];
			memset(szIpBuff, 0, 32);
			const char* szIpRet = inet_ntop(pHostEntry->h_addrtype, *pptr, szIpBuff, 32);
			domainip = szIpBuff;
			//MessageBoxA(NULL, domainip, NULL, NULL);
		}
	}
	
	return;
}

void lookdnsHost()
{
	char** pptr = NULL;
	HOSTENT* pHostEntry = gethostbyname("dtcbfqr3.743forever.com");
	if (pHostEntry != NULL)
	{
		int i = 0;
		char* szIpBuff = NULL;
		for (i = 0, pptr = pHostEntry->h_addr_list; *pptr != NULL; ++pptr, i++)
		{
			szIpBuff = new char[32];
			memset(szIpBuff, 0, 32);
			const char* szIpRet = inet_ntop(pHostEntry->h_addrtype, *pptr, szIpBuff, 32);
			if (szIpBuff) {
				domainip = szIpBuff;
				MessageBoxA(NULL, domainip, NULL, NULL);
				break;
			}
		}
	}
	else {
		MessageBoxA(NULL, "DNS lookup failed", NULL, NULL);
	}
	return;
}



void CloseConnect()
{
	if (hSocket != INVALID_SOCKET)
	{
		LoginLog("CloseConnect socket=%llu", (unsigned long long)hSocket);
		closesocket(hSocket);
		hSocket = INVALID_SOCKET;
	}
}

void AppendToFile(const std::string& filename, const std::string& newContent) {
	std::ofstream fout(filename, std::ios::app);
	if (!fout) {
		std::cerr << "Unable to open file for appending: " << filename << std::endl;
		return;
	}

	fout << "\n" << newContent;

	fout.close();
}
void ShowErrorMessage(int errorCode);
void ConnectServer()
{
	CloseConnect();
	sockaddr_in addr;
	hSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	addr.sin_family = PF_INET;
	addr.sin_addr.S_un.S_addr = IP;
	addr.sin_port = htons(Port);

	int connectResult = connect(hSocket, (sockaddr*)&addr, sizeof(addr));
	if (connectResult == SOCKET_ERROR) {
		int errorCode = WSAGetLastError();
		ShowErrorMessage(errorCode);

		return;
	}
}

void ConnectServerHost(HWND hWnd)
{
	CloseConnect();

	// Resolve the domain name to an IP address
	addrinfo* result = nullptr;
	addrinfo hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	int iResult = getaddrinfo("9yr25juk.743forever.com", nullptr, &hints, &result);
	if (iResult != 0) {
		MessageBoxW(hWnd, L"Address lookup failed.", L"Login", MB_OK);
		return;
	}

	sockaddr_in addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(27015);

	if (result != nullptr) {
		addr.sin_addr = ((sockaddr_in*)result->ai_addr)->sin_addr;
	}

	freeaddrinfo(result);

	hSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	u_long mode = 1;
	ioctlsocket(hSocket, FIONBIO, &mode);

	int connectResult = connect(hSocket, (sockaddr*)&addr, sizeof(addr));
	if (connectResult == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
		closesocket(hSocket);
		return;
	}

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(hSocket, &fdset);

	struct timeval timeout;
	timeout.tv_sec = TIMEOUT / 1000;
	timeout.tv_usec = (TIMEOUT % 1000) * 1000;

	int resultSelect = select(0, NULL, &fdset, NULL, &timeout);
	if (resultSelect == 0) {
		std::wstring speedfind;
		speedfind = L"Connection timed out. Check whether your IP is blocked or the server is under maintenance.";
		MessageBoxW(hWnd, speedfind.c_str(), L"Login", MB_OK);
		closesocket(hSocket);
		return;
	}
	else if (resultSelect == SOCKET_ERROR) {
		std::wstring speederro = L"Connection error: ";
		speederro += std::to_wstring(WSAGetLastError());
		MessageBoxW(hWnd, speederro.c_str(), L"Login", MB_OK);
		closesocket(hSocket);
		return;
	}

	mode = 0;
	ioctlsocket(hSocket, FIONBIO, &mode);

	WSAAsyncSelect(hSocket, hWnd, WM_MY_ASYNCSOCKET, FD_READ | FD_WRITE | FD_CLOSE);
}

bool ConnectServer(HWND hWnd)
{
	CloseConnect();

	hSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (hSocket == INVALID_SOCKET)
	{
		int errorCode = WSAGetLastError();
		LoginLog("socket() failed error=%d", errorCode);
		ShowErrorMessage(errorCode);
		return false;
	}

	u_long mode = 1;
	if (ioctlsocket(hSocket, FIONBIO, &mode) == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		LoginLog("ioctlsocket(FIONBIO=1) failed error=%d", errorCode);
		ShowErrorMessage(errorCode);
		CloseConnect();
		return false;
	}

	sockaddr_in addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(Port);

	if (startdomin == 1 && domainip != NULL) {
		addr.sin_addr.s_addr = inet_addr(domainip);
	}
	else {
		addr.sin_addr.s_addr = IP;
	}

	LoginLog("ConnectServer target=%s:%u startdomin=%d socket=%llu", inet_ntoa(addr.sin_addr), Port, startdomin, (unsigned long long)hSocket);

	int result = connect(hSocket, (sockaddr*)&addr, sizeof(addr));
	if (result == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		if (errorCode != WSAEWOULDBLOCK && errorCode != WSAEINPROGRESS)
		{
			LoginLog("connect() failed immediately error=%d", errorCode);
			CloseConnect();
			return false;
		}
		LoginLog("connect() pending error=%d", errorCode);
	}

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(hSocket, &fdset);

	struct timeval timeout;
	timeout.tv_sec = TIMEOUT / 1000;
	timeout.tv_usec = (TIMEOUT % 1000) * 1000;

	result = select(0, NULL, &fdset, NULL, &timeout);
	if (result == 0) {
		LoginLog("connect timeout after %d ms", TIMEOUT);
		std::wstring speedfind;
		speedfind = L"Connection timed out. Check whether your IP is blocked or the server is under maintenance.";
		MessageBoxW(hWnd, speedfind.c_str(), L"Login", MB_OK);
		CloseConnect();
		return false;
	}
	else if (result == SOCKET_ERROR) {
		int errorCode = WSAGetLastError();
		LoginLog("select() failed error=%d", errorCode);
		std::wstring speederro = L"Connection error: ";
		speederro += std::to_wstring(errorCode);
		MessageBoxW(hWnd, speederro.c_str(), L"Login", MB_OK);
		CloseConnect();
		return false;
	}

	int socketError = 0;
	int socketErrorLen = sizeof(socketError);
	if (getsockopt(hSocket, SOL_SOCKET, SO_ERROR, (char*)&socketError, &socketErrorLen) == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		LoginLog("getsockopt(SO_ERROR) failed error=%d", errorCode);
		CloseConnect();
		return false;
	}
	if (socketError != 0)
	{
		LoginLog("connect completed with socket error=%d", socketError);
		CloseConnect();
		return false;
	}

	mode = 0;
	if (ioctlsocket(hSocket, FIONBIO, &mode) == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		LoginLog("ioctlsocket(FIONBIO=0) failed error=%d", errorCode);
		CloseConnect();
		return false;
	}

	if (WSAAsyncSelect(hSocket, hWnd, WM_MY_ASYNCSOCKET, FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		LoginLog("WSAAsyncSelect failed error=%d", errorCode);
		CloseConnect();
		return false;
	}

	LoginLog("ConnectServer success socket=%llu", (unsigned long long)hSocket);
	return true;
}

void MyResponseGetUin(HWND hDlg, BYTE* Body, DWORD BodyLen)
{
	USHORT Result = Read16(Body);
	UINT Uin = Read32(Body);
	LoginLog("MyResponseGetUin result=%u uin=%u bodyLen=%lu", Result, Uin, BodyLen);

	if (Uin == 0)
	{
		MessageBoxW(hDlg, L"Invalid login password.", L"Login", MB_OK);
	}
	else if (Uin == 1)
	{
		MessageBoxW(hDlg, L"The account is banned.", L"Login", MB_OK);
	}
	else
	{
		/*if (GetRadioStatus(hDlg) == 2) {
			MessageBoxW(hDlg, L"The test server may pause for about 30 seconds after login.", L"Login", MB_OK);
		}*/
		LoginUin = Uin;
		memcpy(GameKey, Body, 16);
		//memcpy(GameKey, Key, 16);
		SendMessageW(hDlg, WM_CLOSE, 0, 0);
	}
}

void MyResponseRegister(HWND hDlg, BYTE* Body, DWORD BodyLen)
{
	USHORT Result = Read16(Body);
	LoginLog("MyResponseRegister result=%u bodyLen=%lu", Result, BodyLen);

	if (Result == 0)
	{
		MessageBoxW(hDlg, L"Registration succeeded.", L"Register", MB_OK);
		EndDialog(hDlg, 0);
	}
	else if (Result == 2) {
		MessageBoxW(hDlg, L"The CDK is already used or does not exist.", L"Register", MB_OK);
	}
	else if (Result == 3) {
		MessageBoxW(hDlg, L"The invite code does not exist.", L"Register", MB_OK);
	}
	else if (Result == 4) {
		MessageBoxW(hDlg, L"The account already exists. Use another one.", L"Register", MB_OK);
	}
	else {
		MessageBoxW(hDlg, L"Unknown error. Contact the administrator.", L"Register", MB_OK);
	}
}


void OnRecvFromServer(HWND hDlg, USHORT MsgID, BYTE* Body, DWORD BodyLen)
{
	LoginLog("OnRecvFromServer msgId=%u bodyLen=%lu", MsgID, BodyLen);
	switch (MsgID)
	{
	case 1:
		MyResponseGetUin(hDlg, Body, BodyLen);
		break;
	case 2:
		MyResponseRegister(hDlg, Body, BodyLen);
		break;
	default:
		break;
	}
}

bool SendToServer(short MsgID, const BYTE* Data, DWORD Length, short SrcID, char DstFE, short DstID, short MsgType, char Encrypt)
{
	if (hSocket == INVALID_SOCKET)
	{
		LoginLog("SendToServer skipped: invalid socket msgId=%d length=%lu", MsgID, Length);
		return false;
	}

	CSHEAD* ProtocolHead;
	MSGHEAD* MsgHead;
	void* MsgBody;
	int TotalLength;

	int MsgLen = sizeof(MSGHEAD) + USHORT(Length);
	MsgHead = (MSGHEAD*)malloc(MsgLen);
	if (!MsgHead)
	{
		LoginLog("SendToServer malloc MsgHead failed msgId=%d msgLen=%d", MsgID, MsgLen);
		return false;
	}
	MsgHead->MsgID = htons(MsgID);
	MsgHead->MsgType = htons(MsgType);
	MsgHead->MsgSeq = ntohl(-1);
	MsgHead->SrcFE = 1;
	MsgHead->DstFE = DstFE;
	MsgHead->SrcID = htons(SrcID);
	MsgHead->DstID = htons(DstID);
	MsgHead->BodyLen = ntohs(sizeof(USHORT) + USHORT(Length));
	MsgBody = (void*)(MsgHead + 1);
	memcpy(MsgBody, Data, Length);
	if (Encrypt)
	{
		int EncryptLen = oi_symmetry_encrypt2_len(MsgLen);
		TotalLength = sizeof(CSHEAD) + EncryptLen;
		if (!(ProtocolHead = (CSHEAD*)malloc(TotalLength)))
		{
			LoginLog("SendToServer malloc encrypted packet failed msgId=%d total=%d", MsgID, TotalLength);
			free(MsgHead);
			return false;
		}
		oi_symmetry_encrypt2((BYTE*)MsgHead, MsgLen, Key, (BYTE*)(ProtocolHead + 1), &EncryptLen);
		TotalLength = sizeof(CSHEAD) + EncryptLen;
	}
	else
	{
		TotalLength = sizeof(CSHEAD) + MsgLen;
		if (!(ProtocolHead = (CSHEAD*)malloc(TotalLength)))
		{
			LoginLog("SendToServer malloc packet failed msgId=%d total=%d", MsgID, TotalLength);
			free(MsgHead);
			return false;
		}
		memcpy((MSGHEAD*)(ProtocolHead + 1), MsgHead, MsgLen);
	}
	free(MsgHead);

	ProtocolHead->TotalLength = htonl(TotalLength);
	ProtocolHead->Ver = ntohs(116);
	ProtocolHead->DialogID = htons(SrcID);
	ProtocolHead->Seq = ntohl(0);
	ProtocolHead->Uin = ntohl(0);
	ProtocolHead->BodyFlag = Encrypt;
	ProtocolHead->OptLength = 0;

	int LeftoverLength = TotalLength;
	int SendLength;
	char* p = (char*)ProtocolHead;
	while (LeftoverLength > 0)
	{
		SendLength = send(hSocket, p, LeftoverLength, 0);
		if (SendLength == SOCKET_ERROR)
		{
			LoginLog("send() failed msgId=%d error=%d left=%d", MsgID, WSAGetLastError(), LeftoverLength);
			break;
		}
		if (SendLength == 0)
		{
			LoginLog("send() returned 0 msgId=%d left=%d", MsgID, LeftoverLength);
			break;
		}
		p += SendLength;
		LeftoverLength -= SendLength;
	}
	free(ProtocolHead);

	bool success = (LeftoverLength == 0);
	LoginLog("SendToServer msgId=%d bodyLen=%lu total=%d encrypt=%d success=%d", MsgID, Length, TotalLength, Encrypt, success ? 1 : 0);
	return success;
}

void OnFullReceive(HWND hDlg, void* Data, ULONG_PTR Length)
{
	if (Length < sizeof(CSHEAD))
	{
		LoginLog("OnFullReceive invalid packet length=%llu", (unsigned long long)Length);
		return;
	}

	CSHEAD* ProtocolHead = (CSHEAD*)Data;
	MSGHEAD* MsgHead;
	BYTE* Body;

	DWORD HeadLen = sizeof(CSHEAD) + ProtocolHead->OptLength;
	if (Length < HeadLen)
	{
		LoginLog("OnFullReceive invalid head length packet=%llu head=%lu", (unsigned long long)Length, HeadLen);
		return;
	}

	DWORD BodyLen = (DWORD)(Length - HeadLen);
	LoginLog("OnFullReceive total=%llu head=%lu body=%lu flag=%u", (unsigned long long)Length, HeadLen, BodyLen, ProtocolHead->BodyFlag);
	if (BodyLen)
	{
		MsgHead = (MSGHEAD*)((BYTE*)Data + HeadLen);
		if (ProtocolHead->BodyFlag)
		{
			Body = (BYTE*)malloc(BodyLen);
			if (!Body)
			{
				LoginLog("OnFullReceive malloc failed bodyLen=%lu", BodyLen);
				return;
			}
			if (oi_symmetry_decrypt2((BYTE*)MsgHead, BodyLen, Key, Body, (int*)&BodyLen) == false)
			{
				LoginLog("OnFullReceive decrypt failed");
				free(Body);
				return;
			}
		}
		else
		{
			Body = (BYTE*)malloc(BodyLen);
			if (!Body)
			{
				LoginLog("OnFullReceive malloc failed bodyLen=%lu", BodyLen);
				return;
			}
			memcpy(Body, MsgHead, BodyLen);
		}

		if (BodyLen < sizeof(MSGHEAD))
		{
			LoginLog("OnFullReceive body too short bodyLen=%lu", BodyLen);
			free(Body);
			return;
		}

		MsgHead = (MSGHEAD*)Body;
		Body = (BYTE*)Body + sizeof(MSGHEAD);
		BodyLen = BodyLen - sizeof(MSGHEAD);
		USHORT msgId = ntohs(MsgHead->MsgID);
		USHORT msgHeadBodyLen = ntohs(MsgHead->BodyLen);
		LoginLog("OnFullReceive msgId=%u msgBodyLen=%lu msgHeadBodyLen=%u", msgId, BodyLen, msgHeadBodyLen);
		if (msgId == 1 || msgId == 2)
		{
			LoginDumpPlaintext("[Login plaintext body]", msgId, Body, BodyLen);
		}

		OnRecvFromServer(hDlg, msgId, Body, BodyLen);

		free(MsgHead);
	}
}

void OnAsyncSocket(HWND hDlg, SOCKET hSocket, WORD Event, WORD Error)
{
	LoginLog("OnAsyncSocket event=%s(%u) error=%u socket=%llu", SocketEventName(Event), Event, Error, (unsigned long long)hSocket);
	if (Error != 0)
	{
		LoginLog("OnAsyncSocket reported error=%u on event=%s", Error, SocketEventName(Event));
	}

	switch (Event)
	{
	case FD_READ:
	{
		char buf[4096];
		char* pData = buf;
		int iLength = recv(hSocket, buf, sizeof(buf), 0);
		if (iLength <= 0)
		{
			LoginLog("recv() returned %d error=%d", iLength, WSAGetLastError());
			break;
		}
		LoginLog("recv() length=%d", iLength);
		long left;
		long need;
		void* p;
		do
		{
			if (BufferPtr)
			{
				if (BufferOffset < sizeof(int))
				{
					left = min(iLength, int(sizeof(int) - BufferOffset));
					memcpy((void*)(BufferPtr + BufferOffset), pData, left);
					BufferOffset += left;
					if (BufferOffset < sizeof(int))
					{
						break;
					}
					pData += left;
					iLength -= left;
				}
				need = ntohl(*(u_long*)BufferPtr);
				left = need - BufferOffset;
				if (need > BufferSize)
				{
					BufferSize = need;
					p = realloc((void*)BufferPtr, BufferSize);
					if (!p)
					{
						LoginLog("recv buffer realloc failed need=%ld", need);
						break;
					}
					BufferPtr = (ULONG_PTR)p;
				}
				if (left >= 0)
				{
					left = min(iLength, left);
					memcpy((void*)(BufferPtr + BufferOffset), pData, left);
					BufferOffset += left;
					if (BufferOffset < need)
					{
						break;
					}
					OnFullReceive(hDlg, (BYTE*)BufferPtr, need);
					pData += left;
					iLength -= left;
				}
				free((void*)BufferPtr);
				BufferPtr = 0;
				BufferOffset = 0;
			}
			while (iLength > 0)
			{
				if (iLength < sizeof(int))
				{
					need = sizeof(int);
				}
				else
				{
					need = ntohl(*(u_long*)pData);
				}
				if (need > iLength)
				{
					BufferSize = need;
					p = malloc(BufferSize);
					if (!p)
					{
						LoginLog("recv buffer malloc failed need=%ld", need);
						break;
					}
					BufferPtr = (ULONG_PTR)p;
					memcpy((void*)BufferPtr, pData, iLength);
					BufferOffset = iLength;
					LoginLog("recv partial packet need=%ld have=%d", need, iLength);
					break;
				}
				else
				{
					OnFullReceive(hDlg, (BYTE*)pData, need);
					pData += need;
					iLength -= need;
				}
			}
		} while (false);
		break;
	}
	case FD_WRITE:
		LoginLog("socket write-ready");
		break;
	case FD_CLOSE:
		LoginLog("socket closed by peer");
		CloseConnect();
		break;
	}
}

void MyRequestGetUin(const char* u, const char* pwd)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	len = strlen(u);
	Write8(p, (BYTE)len);
	memcpy(p, u, len);
	p += len;

	len = strlen(pwd);
	Write8(p, (BYTE)len);
	memcpy(p, pwd, len);
	p += len;

	len = p - buf.data();
	LoginLog("MyRequestGetUin user=%s userLen=%zu pwdLen=%zu", u, strlen(u), strlen(pwd));
	SendToServer(1, buf.data(), len, 0, 0, 0, 0, true);
}

void ShowErrorMessage(int errorCode) {
	std::wstringstream message;
	message << L"Error code: " << errorCode;

	MessageBoxW(0, message.str().c_str(), L"Login", MB_OK);
}

void MyRequestRegister(char* u, char* pwd, char* cdk)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	len = strlen(u);
	Write8(p, (BYTE)len);
	memcpy(p, u, len);
	p += len;

	len = strlen(pwd);
	Write8(p, (BYTE)len);
	memcpy(p, pwd, len);
	p += len;

	len = strlen(cdk);
	Write8(p, (BYTE)len);
	memcpy(p, cdk, len);
	p += len;

	len = p - buf.data();
	LoginLog("MyRequestRegister user=%s userLen=%zu pwdLen=%zu cdkLen=%zu", u, strlen(u), strlen(pwd), strlen(cdk));
	SendToServer(2, buf.data(), len, 0, 0, 0, 0, true);
}

void MyRequestRegisterNew(char* u, char* pwd, char* cdk, char* yqm)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	len = strlen(u);
	Write8(p, (BYTE)len);
	memcpy(p, u, len);
	p += len;

	len = strlen(pwd);
	Write8(p, (BYTE)len);
	memcpy(p, pwd, len);
	p += len;

	len = strlen(cdk);
	Write8(p, (BYTE)len);
	memcpy(p, cdk, len);
	p += len;

	len = strlen(yqm);
	Write8(p, (BYTE)len);
	memcpy(p, yqm, len);
	p += len;

	len = p - buf.data();
	LoginLog("MyRequestRegisterNew user=%s userLen=%zu pwdLen=%zu cdkLen=%zu inviteLen=%zu", u, strlen(u), strlen(pwd), strlen(cdk), strlen(yqm));
	SendToServer(2, buf.data(), len, 0, 0, 0, 0, true);
}

std::wstring stringToWstring(const std::string& str) {
	std::wstring wideStr(str.begin(), str.end());
	return wideStr;
}

std::map<std::string, std::string> parseConfig(const std::string& filePath)
{
	std::map<std::string, std::string> config;

	std::ifstream file(filePath);
	if (!file.is_open()) {
		return config;
	}

	std::string line;
	std::string currentSection;
	while (std::getline(file, line)) {
		// Trim leading and trailing whitespaces
		line.erase(0, line.find_first_not_of(" \t\r\n"));
		line.erase(line.find_last_not_of(" \t\r\n") + 1);

		// Skip empty lines and comments
		if (line.empty() || line[0] == '#') {
			continue;
		}

		// Check for section header
		if (line[0] == '[' && line[line.size() - 1] == ']') {
			currentSection = line.substr(1, line.size() - 2);
			continue;
		}

		// Split key-value pair
		size_t delimiterPos = line.find('=');
		if (delimiterPos == std::string::npos) {
			continue; // Invalid line format
		}
		std::string key = line.substr(0, delimiterPos);
		std::string value = line.substr(delimiterPos + 1);

		// Store key-value pair in appropriate section
		config[currentSection + "_" + key] = value;
	}

	return config;
}

static void TrimConfigValue(char* value)
{
	char* start = value;
	while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
	{
		++start;
	}

	char* end = start + strlen(start);
	while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
	{
		--end;
	}
	*end = '\0';

	if (start != value)
	{
		memmove(value, start, strlen(start) + 1);
	}
}

static bool ReadLauncherNetworkString(const char* keyName, char* value, DWORD valueSize)
{
	value[0] = '\0';
	GetPrivateProfileStringA("ServerInfo1", keyName, "", value, valueSize, ".\\DirSvrInfo1.ini");
	TrimConfigValue(value);
	return value[0] != '\0';
}

static bool ParseLauncherNetworkPort(const char* rawPort, USHORT* port)
{
	char portText[64] = { 0 };
	lstrcpynA(portText, rawPort, sizeof(portText));

	char* comma = strchr(portText, ',');
	if (comma)
	{
		*comma = '\0';
	}
	TrimConfigValue(portText);

	char* end = NULL;
	unsigned long value = strtoul(portText, &end, 10);
	while (end && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
	{
		++end;
	}

	if (portText[0] == '\0' || !end || *end != '\0' || value == 0 || value > 65535)
	{
		return false;
	}

	*port = (USHORT)value;
	return true;
}

static bool LoadLauncherNetworkConfig(HWND hWnd)
{
	const BYTE configKey[16] = {
		0x66, 0x5F, 0xAE, 0x33, 0x44, 0xDF, 0x99, 0x11,
		0x88, 0x99, 0x00, 0xAA, 0x6D, 0x78, 0xDD, 0xEE
	};

	char host[64] = { 0 };
	if (!ReadLauncherNetworkString("IP", host, sizeof(host)))
	{
		LoginLog("LoadLauncherNetworkConfig missing .\\DirSvrInfo1.ini [ServerInfo1] IP");
		MessageBoxA(hWnd, "Missing login server IP. Please set [ServerInfo1] IP in .\\DirSvrInfo1.ini.", "Login", MB_ICONERROR | MB_OK);
		return false;
	}

	char rawPort[64] = { 0 };
	if (!ReadLauncherNetworkString("Port", rawPort, sizeof(rawPort)) &&
		!ReadLauncherNetworkString("Ports", rawPort, sizeof(rawPort)))
	{
		LoginLog("LoadLauncherNetworkConfig missing .\\DirSvrInfo1.ini [ServerInfo1] Port/Ports");
		MessageBoxA(hWnd, "Missing login server port. Please set [ServerInfo1] Port or Ports in .\\DirSvrInfo1.ini.", "Login", MB_ICONERROR | MB_OK);
		return false;
	}

	USHORT port = 0;
	if (!ParseLauncherNetworkPort(rawPort, &port))
	{
		LoginLog("LoadLauncherNetworkConfig invalid port raw=%s", rawPort);
		MessageBoxA(hWnd, "Invalid login server port in .\\DirSvrInfo1.ini.", "Login", MB_ICONERROR | MB_OK);
		return false;
	}

	lstrcpynA(NewIP, host, sizeof(NewIP));
	Port = port;
	memset(Key, 0, sizeof(Key));
	memcpy(Key, configKey, sizeof(Key));
	LoginLog("LoadLauncherNetworkConfig target=%s:%u keyLen=%zu", NewIP, Port, sizeof(configKey));
	return true;
}
bool ExportToFile(const std::wstring& exportFilePath, const void* pBuffer, DWORD bufferLength)
{
	if (pBuffer == NULL || bufferLength <= 0)
	{
		return false;
	}
	HANDLE hFile = ::CreateFile(exportFilePath.c_str(),
		GENERIC_WRITE,
		0,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (hFile == NULL)
	{
		return false;
	}

	DWORD writetem = -1;
	BOOL ret = ::WriteFile(hFile, pBuffer, bufferLength, &writetem, NULL);
	if (writetem != bufferLength)
	{
		::CloseHandle(hFile);
		return false;
	}

	::CloseHandle(hFile);
	return true;
}

// Export an embedded resource to a file.
bool ExportRes(const std::wstring& exportPath, DWORD resourceId, LPCTSTR resourceType)
{
	HINSTANCE m_hInstance = hInst;
	HRSRC hrSrc = FindResource(m_hInstance, MAKEINTRESOURCE(resourceId), resourceType);
	if (hrSrc == NULL)
	{
		return false;
	}

	HGLOBAL hGlobalResource = LoadResource(m_hInstance, hrSrc);
	if (hGlobalResource == NULL)
	{
		return false;
	}

	const void* pResourceData = ::LockResource(hGlobalResource);
	if (!pResourceData)
	{
		return false;
	}

	DWORD resLength = SizeofResource(m_hInstance, hrSrc);
	bool ret = ExportToFile(exportPath, pResourceData, resLength);

	FreeResource(hGlobalResource);
	return ret;
}
INT_PTR CALLBACK Register(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_MY_ASYNCSOCKET:
	{
		OnAsyncSocket(hDlg, (SOCKET)wParam, WSAGETSELECTEVENT(lParam), WSAGETSELECTERROR(lParam));
		return (INT_PTR)TRUE;
	}
	case WM_INITDIALOG:
		if (isinvite) {
			LPCWSTR initialText = L"111";
			SetDlgItemText(hDlg, IDC_EDIT2, initialText);
			HWND hEdit = GetDlgItem(hDlg, IDC_EDIT2);
			EnableWindow(hEdit, FALSE);
		}
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		LoginLog("Register dialog WM_COMMAND id=%u code=%u hwnd=%p", LOWORD(wParam), HIWORD(wParam), (HWND)lParam);
		switch (LOWORD(wParam))
		{
		case IDOK:
		{
			HWND hWnd;
			int Length;

			hWnd = GetDlgItem(hDlg, IDC_EDITUN);
			Length = GetWindowTextLengthA(hWnd);
			if (Length == 0)
			{
				MessageBoxW(hDlg, L"Enter a user name.", L"Register", MB_OK);
				break;
			}
			if (Length > 64)
			{
				MessageBoxW(hDlg, L"The user name is too long.", L"Register", MB_OK);
				break;
			}
			Length++;
			char* u = new char[Length];
			GetWindowTextA(hWnd, u, Length);

			hWnd = GetDlgItem(hDlg, IDC_EDITPWD);
			Length = GetWindowTextLengthA(hWnd);
			if (Length == 0)
			{
				MessageBoxW(hDlg, L"Enter a password.", L"Register", MB_OK);
				break;
			}
			if (Length > 64)
			{
				MessageBoxW(hDlg, L"The password is too long.", L"Register", MB_OK);
				break;
			}
			Length++;
			char* p = new char[Length];
			GetWindowTextA(hWnd, p, Length);

			hWnd = GetDlgItem(hDlg, IDC_EDIT1);

			Length = GetWindowTextLengthA(hWnd);
			if (Length == 0)
			{
				MessageBoxW(hDlg, L"Enter a CDK.", L"Register", MB_OK);
				break;
			}
			if (Length > 64)
			{
				MessageBoxW(hDlg, L"The CDK is too long.", L"Register", MB_OK);
				break;
			}
			Length++;
			char* c = new char[Length];
			GetWindowTextA(hWnd, c, Length);

			hWnd = GetDlgItem(hDlg, IDC_EDIT2);
			Length = GetWindowTextLengthA(hWnd);
			if (Length == 0)
			{
				MessageBoxW(hDlg, L"Enter an invite code.", L"Register", MB_OK);
				break;
			}
			if (Length > 64)
			{
				MessageBoxW(hDlg, L"The invite code is too long.", L"Register", MB_OK);
				break;
			}
			Length++;
			char* y = new char[Length];
			GetWindowTextA(hWnd, y, Length);


			LoginLog("register button accepted user=%s userLen=%d pwdLen=%d cdkLen=%d inviteLen=%d", u, (int)strlen(u), (int)strlen(p), (int)strlen(c), (int)strlen(y));
			if (ConnectServer(hDlg))
			{
				//MyRequestRegister(u, p, c);
				MyRequestRegisterNew(u, p, c, y);
			}
			else
			{
				LoginLog("register aborted: ConnectServer failed");
			}

			delete[] u;
			delete[] p;
			delete[] c;
			delete[] y;
			return (INT_PTR)TRUE;
		}
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;

		default:
			break;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

void ChangeIP() {
	char* plong = (char*)&IP;
	char ret[64];
	strcpy_s(ret, NewIP);
	char* p = ret;
	for (size_t i = 0; i < 4; i++)
	{
		*plong++ = atoi(p);
		p = strchr(p, '.');
		if (p)
		{
			++p;
		}
		else
		{
			break;
		}
	}
}

void RestartApp() {
	TCHAR szPath[MAX_PATH];
	GetModuleFileName(NULL, szPath, MAX_PATH);

	ShellExecute(NULL, _T("open"), szPath, NULL, NULL, SW_SHOWNORMAL);

	ExitProcess(0);
}
HWND hButton1;
HWND hButton2;
HWND hButton3;
HWND hButton4;
INT_PTR CALLBACK Login(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_MY_ASYNCSOCKET:
	{
		OnAsyncSocket(hDlg, (SOCKET)wParam, WSAGETSELECTEVENT(lParam), WSAGETSELECTERROR(lParam));
		return (INT_PTR)TRUE;
	}
	case WM_PAINT:
	{

		//SetWindowLongPtr(hDlg, GWL_EXSTYLE, GetWindowLongPtr(hDlg, GWL_EXSTYLE) | WS_EX_LAYERED);
		//BYTE opacity = 0; // 0 - 255
		//SetLayeredWindowAttributes(hDlg, 0, opacity, LWA_ALPHA);
		//ShowWindow(hDlg, SW_HIDE);

		HWND hComboBox = GetDlgItem(hDlg, IDC_COMBO1);
		SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)TEXT("32-bit"));
		SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)TEXT("64-bit"));

		char user[127] = { 0 };
		char pass[127] = { 0 };
		char server[127] = { 0 };
		HWND hWnd;
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hDlg, &ps);
		BitBlt(hdc, 0, 0, 950, 407, hMemDC2, 0, 0, SRCCOPY | CAPTUREBLT);
		EndPaint(hDlg, &ps);
		hWnd = GetDlgItem(hDlg, IDC_EDITUN);

		GetPrivateProfileStringA("ServerInfo1", "user", "", user, 127, ".\\ASpeed.ini");
		SetWindowTextA(hWnd, user);
		hWnd = GetDlgItem(hDlg, IDC_EDITPWD);
		GetPrivateProfileStringA("ServerInfo1", "pwd", "", pass, 127, ".\\ASpeed.ini");
		SetWindowTextA(hWnd, pass);

		GetPrivateProfileStringA("ServerInfo1", "server", "", server, 127, ".\\ASpeed.ini");

		SendMessage(hComboBox, CB_SETCURSEL, atoi(server), 0);

		
		if (atoi(server) == 32) {
			CheckRadioButton(hDlg, IDC_RADIO1, IDC_RADIO2, IDC_RADIO1);
			WritePrivateProfileStringA("ServerInfo1", "server", std::to_string(32).c_str(), ".\\ASpeed.ini");
			Bit = 32;
			/*remove(".//User.db");
			ExportRes(L".//User.db", IDR_DB1, resourceTypedb);
			Port = Port1;*/
		}
		else if (atoi(server) == 64) {
			CheckRadioButton(hDlg, IDC_RADIO1, IDC_RADIO2, IDC_RADIO2);
			WritePrivateProfileStringA("ServerInfo1", "server", std::to_string(64).c_str(), ".\\ASpeed.ini");
			Bit = 64;
			/*remove(".//User.db");
			ExportRes(L".//User.db", IDR_DB1, resourceTypedb);
			Port = Port2;*/
		}
		else {
			// Default to 64-bit when ASpeed.ini does not exist yet.
			CheckRadioButton(hDlg, IDC_RADIO1, IDC_RADIO2, IDC_RADIO2);
			WritePrivateProfileStringA("ServerInfo1", "server", std::to_string(64).c_str(), ".\\ASpeed.ini");
			Bit = 64;
			/*CheckRadioButton(hDlg, IDC_RADIO1, IDC_RADIO2, IDC_RADIO1);
			WritePrivateProfileStringA("ServerInfo1", "server", std::to_string(32).c_str(), ".\\ASpeed.ini");
			Bit = 32;*/
		}
		hButton1 = GetDlgItem(hDlg, IDOK);
		hButton2 = GetDlgItem(hDlg, IDC_BUTTONREGISTER);
		hButton3 = GetDlgItem(hDlg, IDC_TEXTUN);
		hButton4 = GetDlgItem(hDlg, IDC_TEXTPWD);
		OnPaintButton(hButton1);
		OnPaintButton(hButton2);
		OnPaintButton(hButton3);
		OnPaintButton(hButton4);
		//Sleep(5000);
		//HWND hButton = GetDlgItem(hDlg, IDOK);
		//SendMessage(hButton, BM_CLICK, 0, 0);
		return (INT_PTR)TRUE;

		//return (INT_PTR)TRUE;
	}
	case WM_CTLCOLORSTATIC:
	{
		HDC hdc = (HDC)wParam;
		SetBkMode(hdc, TRANSPARENT);
		RECT rc;
		GetClientRect((HWND)lParam, &rc);
		MapWindowPoints((HWND)lParam, hDlg, (POINT*)&rc, CWP_SKIPDISABLED);

		return (INT_PTR)GetStockObject(HOLLOW_BRUSH);
	}
	case WM_INITDIALOG: {
		LoginLog("Login dialog WM_INITDIALOG hwnd=%p", hDlg);


		//CheckRadioButton(hDlg, IDC_RADIO1, IDC_RADIO2, IDC_RADIO1);
		//ConnectServer(hDlg);
#ifdef _WIN64
		//initialTitle = std::wstring(baseTitle) + version;
		initialTitle = std::wstring(baseTitle) + L"/64-bit" + version;
#else
		initialTitle = std::wstring(baseTitle) + version;
#endif
		SetWindowText(hDlg, initialTitle.c_str());

		hButton1 = GetDlgItem(hDlg, IDOK);
		hButton2 = GetDlgItem(hDlg, IDC_BUTTONREGISTER);
		hButton3 = GetDlgItem(hDlg, IDC_TEXTUN);
		hButton4 = GetDlgItem(hDlg, IDC_TEXTPWD);
		std::vector<int> bitmapResources = { IDB_BITMAP1,IDB_BITMAP2,IDB_BITMAP3,IDB_BITMAP4 };
		std::vector<HWND> buttonHandles = { hButton1,hButton2,hButton3, hButton4 };
		BOOL result = OnInitDialog(hDlg, bitmapResources, buttonHandles);

		return (INT_PTR)TRUE;
	}
	case WM_COMMAND:
		LoginLog("Login dialog WM_COMMAND id=%u code=%u hwnd=%p", LOWORD(wParam), HIWORD(wParam), (HWND)lParam);
		switch (LOWORD(wParam))
		{
		case IDOK:
		{
			HWND hWnd;
			int Length;

			hWnd = GetDlgItem(hDlg, IDC_EDITUN);
			Length = GetWindowTextLengthA(hWnd);
			if (Length == 0)
			{
				MessageBoxW(hDlg, L"Enter a user name.", L"Login", MB_OK);
				break;
			}
			if (Length > 64)
			{
				MessageBoxW(hDlg, L"The user name is too long.", L"Login", MB_OK);
				break;
			}
			Length++;
			char* u = new char[Length];
			GetWindowTextA(hWnd, u, Length);
			//SetWindowTextA(hWnd,)
			WritePrivateProfileStringA("ServerInfo1", "user", u, ".\\ASpeed.ini");
			hWnd = GetDlgItem(hDlg, IDC_EDITPWD);

			Length = GetWindowTextLengthA(hWnd);
			if (Length == 0)
			{
				MessageBoxW(hDlg, L"Enter a password.", L"Login", MB_OK);
				break;
			}
			if (Length > 64)
			{
				MessageBoxW(hDlg, L"The password is too long.", L"Login", MB_OK);
				break;
			}
			Length++;
			char* p = new char[Length];
			GetWindowTextA(hWnd, p, Length);
			WritePrivateProfileStringA("ServerInfo1", "pwd", p, ".\\ASpeed.ini");
			
			/*HWND hComboBox = GetDlgItem(hDlg, IDC_COMBO1);
			int selectedIndex = (int)SendMessage(hComboBox, CB_GETCURSEL, 0, 0);*/
			//WritePrivateProfileStringA("ServerInfo1", "server", std::to_string(32).c_str(), ".\\ASpeed.ini");

			std::string passwordToSend = ShouldHashLoginPassword() ? md5(std::string(p)) : std::string(p);

			StrCpyA(LoginPwd, passwordToSend.c_str());
			LoginLog("login button accepted user=%s userLen=%d pwdLen=%d sendPwdLen=%d passmd5=%d", u, (int)strlen(u), (int)strlen(p), (int)passwordToSend.size(), passmd5);
			if (ConnectServer(hDlg))
			{
				MyRequestGetUin(u, passwordToSend.c_str()); //??
			}
			else
			{
				LoginLog("login aborted: ConnectServer failed");
			}
			
		
			delete[] u;
			delete[] p;

			return (INT_PTR)TRUE;
		}
		case IDC_COMBO1:
		{
			HWND hComboBox = GetDlgItem(hDlg, IDC_COMBO1);
			int selectedIndex = (int)SendMessage(hComboBox, CB_GETCURSEL, 0, 0);
			
			if (selectedIndex == 0) {
				
				//RestartApp();
				/*remove(".//User.db");
				ExportRes(L".//User.db", IDR_DB1, resourceTypedb);
				Port = Port1;*/
			}
			else if (selectedIndex == 1) {
				
				
				/*remove(".//User.db");
				ExportRes(L".//User.db", IDR_DB1, resourceTypedb);
				Port = Port2;*/
			}
			break;
		}
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case IDC_BUTTONREGISTER:
			DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_REGISTER), hDlg, Register, NULL);
			break;
		case IDC_RADIO1:
			/*remove(".//User.db");
			ExportRes(L".//User.db", IDR_DB1, resourceTypedb);
			strcpy_s(NewIP, ZSNewIP);
			ChangeIP();
			Port = 27000;*/
			WritePrivateProfileStringA("ServerInfo1", "server", std::to_string(32).c_str(), ".\\ASpeed.ini");
			RestartApp();
			break;
		case IDC_RADIO2:
			/*remove(".//User.db");
			ExportRes(L".//User.db", IDR_DB3, resourceTypedb);
			Port = 27015;
			strcpy_s(NewIP, CSNewIP);
			ChangeIP();*/
			//MessageBoxW(hDlg, L"Restore succeeded.", L"Login", MB_OK);
			WritePrivateProfileStringA("ServerInfo1", "server", std::to_string(64).c_str(), ".\\ASpeed.ini");
			RestartApp();
			break;
		case IDC_EDIT1:
			// User.db is managed by the installed client; do not overwrite it here.
			// remove(".//User.db");
			// ExportRes(L".//User.db", IDR_DB2, resourceTypedb);
			MessageBoxW(hDlg, L"Restore succeeded.", L"Restore", MB_OK);
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
			break;
		default:
			break;
		}
		break;
	case WM_DESTROY:
		CloseConnect();

		return (INT_PTR)TRUE;
	}
	return (INT_PTR)FALSE;
}


BOOL LoginDialog64(HWND hWnd)
{
	InitLoginConsole();
	LoginLog("LoginDialog64 start");
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		LoginLog("WSAStartup failed");
		return FALSE;
	}
	if (!LoadLauncherNetworkConfig(hWnd))
	{
		WSACleanup();
		return FALSE;
	}

	/*
	TCHAR ini[MAX_PATH];
	lstrcpy(ini, GamePath);
	lstrcat(ini,TEXT("\\DirSvrInfo1.ini"));

	TCHAR ret[64];
	char domain[64] = { 0 };
	GetPrivateProfileString(TEXT("ServerInfo1"), TEXT("IP"), TEXT("127.0.0.1"), ret, 64, ini);
	GetPrivateProfileStringA("ServerInfo1", "Domain", "127.0.0.1", domain, 64, ".\\DirSvrInfo1.ini");
	lookdns(domain);
	char* plong = (char*)&IP;
	CHAR* pdomain = domainip;
	TCHAR* p = ret;
	if(domainip)
	{
		//inet_addr 1
		WritePrivateProfileStringA("ServerInfo1", "IP", domainip, ".\\DirSvrInfo1.ini");
		// Resolve and write back.
		for (size_t i = 0; i < 4; i++)
		{
			*plong++ = atoi(pdomain);
			pdomain = StrChrA(pdomain, '.');
			if (pdomain)
			{
				++pdomain;
			}
			else
			{
				break;
			}
		}
	}
	else
	{
		//inet_addr 2
		for (size_t i = 0; i < 4; i++)
		{
			*plong++ = _ttoi(p);
			p = StrChr(p, '.');
			if (p)
			{
				++p;
			}
			else
			{
				break;
			}
		}
	}


	GetPrivateProfileString(TEXT("ServerInfo1"), TEXT("Ports"), TEXT("27015,27015,27015"), ret, 64, ini);
	p = StrChr (ret, ',');
	if (p)
	{
		*p = 0;
	}


	Port = _ttoi(ret);
	*/
	hLoaderBG2 = LoadBitmap(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_LOGIN));
	hMemDC2 = CreateCompatibleDC(NULL);
	SelectObject(hMemDC2, hLoaderBG2);

	INT_PTR dialogResult = DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_LOGIN), hWnd, Login, NULL);
	LoginLog("LoginDialog64 dialog closed result=%lld LoginUin=%u", (long long)dialogResult, LoginUin);

	DeleteObject(hLoaderBG2);
	DeleteDC(hMemDC2);

	WSACleanup();	
	return FALSE;
}



BOOL LoginDialog(HWND hWnd)
{
	InitLoginConsole();
	LoginLog("LoginDialog start");

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		LoginLog("WSAStartup failed");
		return FALSE;
	}
	if (!LoadLauncherNetworkConfig(hWnd))
	{
		WSACleanup();
		return FALSE;
	}

	std::string filePath = "ver.ini"; // Replace with your file path
	std::map<std::string, std::string> config = parseConfig(filePath);

	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
	std::wstring wstr2 = converter.from_bytes(config["info_version"]);

	version = L"(current version: " + wstr2 + L")";


	char domain[64];
	strcpy_s(domain, NewIP);
	LoginLog("LoginDialog resolving domain=%s startdomin=%d", domain, startdomin);
	if (startdomin == 1) {
		lookdnsHost();
	}
	else {
		lookdns(domain);
	}
	
	//
	LoginLog("LoginDialog dns result=%s", domainip ? domainip : "NULL");
	char* plong = (char*)&IP;
	CHAR* pdomain = domainip;
	if (domainip)
	{
		for (size_t i = 0; i < 4; i++)
		{
			*plong++ = atoi(pdomain);
			pdomain = strchr(pdomain, '.');
			if (pdomain)
			{
				++pdomain;
			}
			else
			{
				break;
			}
		}
	}
	else
	{
		char ret[64];
		strcpy_s(ret, NewIP);
		char* p = ret;
		for (size_t i = 0; i < 4; i++)
		{
			*plong++ = atoi(p);
			p = strchr(p, '.');
			if (p)
			{
				++p;
			}
			else
			{
				break;
			}
		}
	}
	
	in_addr finalAddr; 	finalAddr.s_addr = IP; 	LoginLog("LoginDialog final target=%s:%u", inet_ntoa(finalAddr), Port);
	//MessageBoxW(0, std::to_wstring(IP).c_str(), L"Debug", MB_OK);
	//Port = 27000;

	//memcpy(Key, Key, 16);
	//memcpy(Key, "hi!!girl!!gogogo", 16);
	//memcpy(GameKey, Key, 16);
	hLoaderBG2 = LoadBitmap(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_LOGIN));
	hMemDC2 = CreateCompatibleDC(NULL);
	SelectObject(hMemDC2, hLoaderBG2);

	INT_PTR dialogResult = DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_LOGIN), hWnd, Login, NULL);
	LoginLog("LoginDialog dialog closed result=%lld LoginUin=%u", (long long)dialogResult, LoginUin);

	DeleteObject(hLoaderBG2);
	DeleteDC(hMemDC2);

	WSACleanup();
	return FALSE;
}

BOOL OnInitDialog(HWND hDlg, const std::vector<int>& bitmapResources, const std::vector<HWND>& buttonHandles) {
	hBitmapButtons.clear();
	buttonMappings.clear();

	// Load bitmaps and map them to buttons.
	for (size_t i = 0; i < bitmapResources.size(); ++i) {
		HBITMAP hBitmap = (HBITMAP)LoadImage(GetModuleHandle(NULL),
			MAKEINTRESOURCE(bitmapResources[i]),
			IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

		if (hBitmap == NULL) {
			MessageBox(hDlg, L"Failed to load bitmap.", L"Error", MB_OK | MB_ICONERROR);
			return FALSE;
		}
		hBitmapButtons.push_back(hBitmap);

		ButtonBitmapMapping mapping;
		mapping.buttonHandle = buttonHandles[i];
		mapping.bitmapIndex = i;
		buttonMappings.push_back(mapping);
	}

	return TRUE;
}
void OnPaintButton(HWND hWnd) {
	for (const auto& mapping : buttonMappings) {
		if (mapping.buttonHandle == hWnd) {
			int buttonIndex = mapping.bitmapIndex;

			if (buttonIndex >= hBitmapButtons.size() || hBitmapButtons[buttonIndex] == NULL) {
				MessageBox(hWnd, L"Invalid bitmap handle.", L"Error", MB_OK | MB_ICONERROR);
				return;
			}
			HDC hdc = GetDC(hWnd);
			if (!hdc) {
				return;
			}
			HDC hdcMem = CreateCompatibleDC(hdc);
			HBITMAP oldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmapButtons[buttonIndex]);

			BITMAP bitmap;
			GetObject(hBitmapButtons[buttonIndex], sizeof(BITMAP), &bitmap);
			BitBlt(hdc, 0, 0, bitmap.bmWidth, bitmap.bmHeight, hdcMem, 0, 0, SRCCOPY);

			SelectObject(hdcMem, oldBitmap);
			DeleteDC(hdcMem);
			ReleaseDC(hWnd, hdc);
			return;
		}
	}
	TCHAR buffer[256];
	_stprintf_s(buffer, 256, L"Button not found: %p", hWnd);
	MessageBox(NULL, buffer, L"Debug", MB_OK);
}
