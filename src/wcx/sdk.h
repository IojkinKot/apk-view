#pragma once
#include <windows.h>
#include <cstddef>

// ABI subset of Total Commander WCX SDK 2.21 SE:
// https://ghisler.github.io/WCX-SDK/header_for_c.htm
namespace wcx {
inline constexpr int E_END_ARCHIVE=10, E_NO_MEMORY=11, E_BAD_DATA=12, E_BAD_ARCHIVE=13,
    E_UNKNOWN_FORMAT=14, E_EOPEN=15, E_ECREATE=16, E_EREAD=18, E_EWRITE=19,
    E_SMALL_BUF=20, E_EABORTED=21, E_NOT_SUPPORTED=24;
inline constexpr int PK_OM_LIST=0, PK_OM_EXTRACT=1;
inline constexpr int PK_SKIP=0, PK_TEST=1, PK_EXTRACT=2;
inline constexpr int PK_CAPS_BY_CONTENT=64;
#pragma pack(push,8)
template<class Char> struct OpenArchiveData {
    Char* ArcName;
    int OpenMode, OpenResult;
    Char* CmtBuf;
    int CmtBufSize,CmtSize,CmtState;
};
using tOpenArchiveData=OpenArchiveData<char>;
using tOpenArchiveDataW=OpenArchiveData<WCHAR>;
struct tHeaderData {
    char ArcName[260], FileName[260];
    int Flags, PackSize, UnpSize, HostOS, FileCRC, FileTime, UnpVer, Method, FileAttr;
    char* CmtBuf;
    int CmtBufSize,CmtSize,CmtState;
};
template<class Char> struct HeaderDataEx {
    Char ArcName[1024], FileName[1024];
    int Flags;
    unsigned int PackSize,PackSizeHigh,UnpSize,UnpSizeHigh;
    int HostOS,FileCRC,FileTime,UnpVer,Method,FileAttr;
    char* CmtBuf;
    int CmtBufSize,CmtSize,CmtState;
    char Reserved[1024];
};
using tHeaderDataEx=HeaderDataEx<char>;
using tHeaderDataExW=HeaderDataEx<WCHAR>;
#pragma pack(pop)
using tChangeVolProc=int (WINAPI*)(char*,int);
using tChangeVolProcW=int (WINAPI*)(WCHAR*,int);
using tProcessDataProc=int (WINAPI*)(char*,int);
using tProcessDataProcW=int (WINAPI*)(WCHAR*,int);

static_assert(sizeof(tOpenArchiveDataW)==(sizeof(void*)==8?40:28));
static_assert(offsetof(tHeaderDataExW,UnpSize)==4108);
static_assert(offsetof(tHeaderDataExW,CmtBuf)==(sizeof(void*)==8?4144:4140));
static_assert(sizeof(tHeaderDataExW)==(sizeof(void*)==8?5192:5180));
}
