
#include "Android/AndroidHeapProfiling.h"
#include "Containers/StringConv.h"


#if ANDROID_HEAP_PROFILING_SUPPORTED
	AHeapInfo* (*AHeapInfoCreate)(const char* _Nonnull heap_name) = nullptr;
	uint32_t(*AHeapProfileRegisterHeap)(AHeapInfo* _Nullable info) = nullptr;
	bool (*AHeapProfileReportAllocation)(uint32_t heap_id, uint64_t alloc_id, uint64_t size) = nullptr;
	void (*AHeapProfileReportFree)(uint32_t heap_id, uint64_t alloc_id) = nullptr;

	const int AppPackageNameBufferSize = 256;
	static char AppPackageNameBuffer[AppPackageNameBufferSize] = "com.epicgames.unreal";
	char* AppPackageName = AppPackageNameBuffer;

	static bool LoadSymbol(void* Module, void** FuncPtr, const char* SymbolName)
	{
		*FuncPtr = dlsym(Module, SymbolName);
		if (!*FuncPtr)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Cannot locate symbol `%s` in heapprofd_standalone_client.so"), ANSI_TO_TCHAR(SymbolName));
			return false;
		}

		return true;
	}

	static void ReadPackageName()
	{
		pid_t Pid = getpid();
		char Buf[AppPackageNameBufferSize];
		sprintf(Buf, "/proc/%u/cmdline", Pid);
		FILE* CmdLine = fopen(Buf, "r");
		if (CmdLine)
		{
			const int Result = fscanf(CmdLine, "%255s", Buf);
			if (Result == 1)
			{
				strcpy(AppPackageNameBuffer, Buf);
			}
			fclose(CmdLine);
		}
	}
#endif


bool AndroidHeapProfiling::Init()
{
#if ANDROID_HEAP_PROFILING_SUPPORTED
	const int32 OSVersion = android_get_device_api_level();
	const int32 Android10Level = __ANDROID_API_Q__;
	if (OSVersion >= Android10Level)
	{
		void* HeapprofdClient = dlopen("libheapprofd_standalone_client.so", 0);
		if (HeapprofdClient)
		{
			bool InitSuccessful = LoadSymbol(HeapprofdClient, (void**)(&AHeapInfoCreate), "AHeapInfo_create");
			InitSuccessful &= LoadSymbol(HeapprofdClient, (void**)(&AHeapProfileRegisterHeap), "AHeapProfile_registerHeap");
			InitSuccessful &= LoadSymbol(HeapprofdClient, (void**)(&AHeapProfileReportAllocation), "AHeapProfile_reportAllocation");
			InitSuccessful &= LoadSymbol(HeapprofdClient, (void**)(&AHeapProfileReportFree), "AHeapProfile_reportFree");

			if (!InitSuccessful)
			{
				dlclose(HeapprofdClient);
				AHeapInfoCreate = nullptr;
				AHeapProfileRegisterHeap = nullptr;
				AHeapProfileReportAllocation = nullptr;
				AHeapProfileReportFree = nullptr;
			}
			else
			{
				ReadPackageName();
			}

			return InitSuccessful;
		}
		else
		{
			const char* Error = dlerror();
			char ErrorBuf[DEFAULT_STRING_CONVERSION_SIZE];
			FCStringAnsi::Strncpy(ErrorBuf, Error, DEFAULT_STRING_CONVERSION_SIZE);
			FPlatformMisc::LocalPrint(ANSI_TO_TCHAR(ErrorBuf));
		}
	}
#endif
	return false;
}