#include <windows.h>

#ifdef WORDSCOMPLETE_EXPORTS
#define WORDSCOMPLETE_API __declspec(dllexport) WINAPI
#else
#define WORDSCOMPLETE_API __declspec(dllimport) WINAPI
#endif


