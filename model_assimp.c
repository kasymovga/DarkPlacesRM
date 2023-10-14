#include "quakedef.h"
#include "model_assimp.h"
const C_STRUCT aiScene *(*qaiImportFileFromMemory)(
		const char *pBuffer,
		unsigned int pLength,
		unsigned int pFlags,
		const char *pHint) = NULL;
void (*qaiReleaseImport)(
		const C_STRUCT aiScene *pScene) = NULL;
static dllhandle_t assimp_dll = NULL;
static dllfunction_t assimpfuncs[] =
{
	{"aiImportFileFromMemory", (void **) &qaiImportFileFromMemory},
	{"aiReleaseImport", (void **) &qaiReleaseImport},
	{NULL, NULL}
};

qboolean Assimp_Init(void)
{
	const char* dllnames [] =
	{
#if defined(WIN32)
		"libassimp-dprm.dll",
#elif defined(MACOSX)
		"libassimp-dprm.dylib",
#else
		"libassimp-dprm.so",
#endif
		NULL
	};

	// Already loaded?
	if (assimp_dll)
		return true;

	// Load the DLL
	return Sys_LoadLibrary(dllnames, &assimp_dll, assimpfuncs);
}

void Assimp_Shutdown(void)
{
	if (assimp_dll)
		Sys_UnloadLibrary(&assimp_dll);
}
