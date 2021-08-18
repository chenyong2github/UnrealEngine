// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pch.h"
#include "StoreService.h"
#include "Utils.h"
#include "Version.h"

#if TS_USING(TS_BUILD_DEBUG)
#	include <thread>
#endif

#if TS_USING(TS_PLATFORM_LINUX) || TS_USING(TS_PLATFORM_MAC)
#	include <pwd.h>
#	include <semaphore.h>
#	include <signal.h>
#	include <sys/mman.h>
#	include <unistd.h>
#endif

#include <immintrin.h>

// {{{1 misc -------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
class FMmapScope
{
public:
								FMmapScope(void* InPtr, uint32 InLength=0);
								~FMmapScope();
	template <typename T> T*	As() const;

private:
	void*						Ptr;
	int32						Length;

private:
								FMmapScope() = delete;
								FMmapScope(const FMmapScope&) = delete;
								FMmapScope(const FMmapScope&&) = delete;
	FMmapScope&					operator = (const FMmapScope&) = delete;
	FMmapScope&					operator = (const FMmapScope&&) = delete;
};

////////////////////////////////////////////////////////////////////////////////
FMmapScope::FMmapScope(void* InPtr, uint32 InLength)
: Ptr(InPtr)
, Length(InLength)
{
}

////////////////////////////////////////////////////////////////////////////////
FMmapScope::~FMmapScope()
{
#if TS_USING(TS_PLATFORM_WINDOWS)
	UnmapViewOfFile(Ptr);
#elif TS_USING(TS_PLATFORM_LINUX) || TS_USING(TS_PLATFORM_MAC)
	munmap(Ptr, Length);
#endif
}

////////////////////////////////////////////////////////////////////////////////
template <typename T>
T* FMmapScope::As() const
{
	return (T*)Ptr;
}



////////////////////////////////////////////////////////////////////////////////
template <typename T>
struct TOnScopeExit
{
		TOnScopeExit()	= default;
		~TOnScopeExit()	{ (*Lambda)(); }
	T*	Lambda;
};

#define OnScopeExit(x) \
	auto TS_CONCAT(OnScopeExitFunc, __LINE__) = x; \
	TOnScopeExit<decltype(TS_CONCAT(OnScopeExitFunc, __LINE__))> TS_CONCAT(OnScopeExitInstance, __LINE__); \
	do { TS_CONCAT(OnScopeExitInstance, __LINE__).Lambda = &TS_CONCAT(OnScopeExitFunc, __LINE__); } while (0)




// {{{1 store ------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
static FStoreService* StartStore(const char* StoreDir)
{
	FStoreService::FDesc Desc;
	Desc.StoreDir = StoreDir;
	Desc.StorePort = 1989;
	Desc.RecorderPort = 1980;
	return FStoreService::Create(Desc);
}



// {{{1 instance-info ----------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
struct FInstanceInfo
{
public:
	static const uint32	CurrentVersion =
#if TS_USING(TS_BUILD_DEBUG)
		0x8000'0000 |
#endif
		((TS_VERSION_PROTOCOL & 0xffff) << 16) | (TS_VERSION_MINOR & 0xffff);

	void				Set();
	void				WaitForReady() const;
	bool				IsOlder() const;
	std::atomic<uint32> Published;
	uint32				Version;
	uint32				Pid;
};

////////////////////////////////////////////////////////////////////////////////
void FInstanceInfo::Set()
{
	Version = 1;
#if TS_USING(TS_PLATFORM_WINDOWS)
	Pid = GetCurrentProcessId();
#elif TS_USING(TS_PLATFORM_LINUX) || TS_USING(TS_PLATFORM_MAC)
	Pid = getpid();
#endif
	Published.fetch_add(1, std::memory_order_release);
}

////////////////////////////////////////////////////////////////////////////////
void FInstanceInfo::WaitForReady() const
{
	// Spin until this instance info is published (by another process)
	for (;; _mm_pause())
	{
		if (Published.load(std::memory_order_acquire))
		{
			break;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
bool FInstanceInfo::IsOlder() const
{
	// Decide which is older; this compiled code or the instance we have a
	// pointer to.
	bool bIsOlder = false;
	bIsOlder |= (Version < FInstanceInfo::CurrentVersion);
	return bIsOlder;
}



// {{{1 windows ----------------------------------------------------------------

#if TS_USING(TS_PLATFORM_WINDOWS)
////////////////////////////////////////////////////////////////////////////////
class FWinHandle
{
public:
	FWinHandle(HANDLE InHandle)
	: Handle(InHandle)
	{
		if (Handle == INVALID_HANDLE_VALUE)
		{
			Handle = nullptr;
		}
	}

				~FWinHandle()				{ if (Handle) CloseHandle(Handle); }
				operator HANDLE () const	{ return Handle; }
	bool		IsValid() const				{ return Handle != nullptr; }

private:
	HANDLE		Handle;
	FWinHandle&	operator = (const FWinHandle&) = delete;
				FWinHandle(const FWinHandle&) = delete;
				FWinHandle(const FWinHandle&&) = delete;
};

////////////////////////////////////////////////////////////////////////////////
static const wchar_t*	GIpcName					= L"Local\\UnrealTraceInstance";
static const int32		GIpcSize					= 4 << 10;
static const wchar_t*	GQuitEventName				= L"Local\\UnrealTraceEvent";
static const wchar_t*	GBegunEventName				= L"Local\\UnrealTraceEventBegun";
static int				MainDaemon(int, char**);

////////////////////////////////////////////////////////////////////////////////
static int MainFork(int ArgC, char** ArgV)
{
	// Check for an existing instance that is already running.
	FWinHandle IpcHandle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, false, GIpcName);
	if (IpcHandle.IsValid())
	{
		void* IpcPtr = MapViewOfFile(IpcHandle, FILE_MAP_ALL_ACCESS, 0, 0, GIpcSize);
		FMmapScope MmapScope(IpcPtr);

		const auto* InstanceInfo = MmapScope.As<const FInstanceInfo>();
		InstanceInfo->WaitForReady();
		if (!InstanceInfo->IsOlder())
		{
			return 0;
		}

		// Signal to the existing instance to shutdown or forcefully do it if it
		// does not respond in time.
		FWinHandle QuitEvent = CreateEventW(nullptr, TRUE, FALSE, GQuitEventName);
		if (GetLastError() != ERROR_ALREADY_EXISTS)
		{
			// Assume someone else has closed the instance already.
			return 0;
		}

		DWORD OpenProcessFlags = PROCESS_TERMINATE | SYNCHRONIZE;
		FWinHandle ProcHandle = OpenProcess(OpenProcessFlags, FALSE, InstanceInfo->Pid);
		if (!ProcHandle.IsValid())
		{
			return 5;
		}

		SetEvent(QuitEvent);

		if (WaitForSingleObject(ProcHandle, 5000) == WAIT_TIMEOUT)
		{
			TerminateProcess(ProcHandle, 10);
		}
	}

	// Get this binary's path
	wchar_t BinPath[MAX_PATH];
	uint32 BinPathLen = GetModuleFileNameW(nullptr, BinPath, TS_ARRAY_COUNT(BinPath));
	if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		// This should never really happen...
		return 1;
	}

	// Calculate where to store the binaries.
	std::filesystem::path DestPath;
	{
		wchar_t Buffer[MAX_PATH];
		SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, Buffer);
		DestPath = Buffer;
	}
	DestPath /= "UnrealEngine/Common/UnrealTrace";
	{
		wchar_t Buffer[64];
		_snwprintf(Buffer, TS_ARRAY_COUNT(Buffer), L"Bin/%08x/UnrealTraceServer.exe", FInstanceInfo::CurrentVersion);
		DestPath /= Buffer;
	}

	// Copy the binary out to a location where it doesn't matter if the file
	// gets locked by the OS.
	if (!std::filesystem::is_regular_file(DestPath))
	{
		std::filesystem::create_directories(DestPath.parent_path());

		// Tag the destination with our PID and copy
		DWORD OurPid = GetCurrentProcessId();
		wchar_t Buffer[16];
		_snwprintf(Buffer, TS_ARRAY_COUNT(Buffer), L"_%08x", OurPid);
		std::filesystem::path TempPath = DestPath;
		TempPath += Buffer;
		if (!std::filesystem::copy_file(BinPath, TempPath))
		{
			return 3;
		}

		// Move the file into place. If this fails because the file exists then
		// another instance has beaten us to the punch.
		std::error_code ErrorCode;
		std::filesystem::rename(TempPath, DestPath, ErrorCode);
		if (ErrorCode)
		{
			bool RaceLost = (ErrorCode == std::errc::file_exists);
			return RaceLost ? 0 : 4;
		}
	}

	// Launch a new instance as a daemon and wait until we know it has started
	FWinHandle BegunEvent = CreateEventW(nullptr, TRUE, FALSE, GBegunEventName);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		return 7;
	}

	// For debugging ease and consistency we will daemonize in this process
	// instead of spawning a second one.
#if TS_USING(TS_BUILD_DEBUG)
	std::thread DaemonThread([] () { MainDaemon(0, nullptr); });
#else
	uint32 Flags = CREATE_BREAKAWAY_FROM_JOB;
	STARTUPINFOW StartupInfo = { sizeof(STARTUPINFOW) };
	PROCESS_INFORMATION ProcessInfo = {};
	BOOL bOk = CreateProcessW(DestPath.c_str(), LPWSTR(L"UnrealTraceServer.exe daemon"), nullptr,
		nullptr, FALSE, Flags, nullptr, nullptr, &StartupInfo, &ProcessInfo);

	if (bOk == FALSE)
	{
		return 2;
	}
#endif // TS_BUILD_DEBUG

	int Ret = 0;
	if (WaitForSingleObject(BegunEvent, 5000) == WAIT_TIMEOUT)
	{
		Ret = 8;
	}

#if TS_USING(TS_BUILD_DEBUG)
	DaemonThread.join();
#else
	CloseHandle(ProcessInfo.hProcess);
	CloseHandle(ProcessInfo.hThread);
#endif

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
static int MainDaemon(int ArgC, char** ArgV)
{
	// Create a piece of shared memory so all store instances can communicate.
	FWinHandle IpcHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
		PAGE_READWRITE, 0, GIpcSize, GIpcName);
	if (!IpcHandle.IsValid())
	{
		return 1;
	}

	// Create a named event so others can tell us to quit.
	FWinHandle QuitEvent = CreateEventW(nullptr, TRUE, FALSE, GQuitEventName);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		// This really should not happen. It is expected that only one process
		// will get this far (gated by the shared-memory object creation).
		return 2;
	}

	// Fill out the Ipc details and publish.
	void* IpcPtr = MapViewOfFile(IpcHandle, FILE_MAP_ALL_ACCESS, 0, 0, GIpcSize);
	{
		FMmapScope MmapScope(IpcPtr);
		auto* InstanceInfo = MmapScope.As<FInstanceInfo>();
		InstanceInfo->Set();
	}

	// Fire up the store
	FStoreService* StoreService;
	{
		std::filesystem::path StoreDir;
		wchar_t Buffer[MAX_PATH];
		SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, Buffer);
		StoreDir = Buffer;
		StoreDir /= "UnrealEngine/Common/UnrealTrace/Store";

		std::u8string StoreDirUtf8 = StoreDir.u8string();
		StoreService = StartStore((const char*)(StoreDirUtf8.c_str()));
	}

	// Let every one know we've started.
	{
		FWinHandle BegunEvent = CreateEventW(nullptr, TRUE, FALSE, GBegunEventName);
		if (BegunEvent.IsValid())
		{
			SetEvent(BegunEvent);
		}
	}

	// Wait to be told to resign.
	WaitForSingleObject(QuitEvent, INFINITE);

	// Clean up. We are done here.
	delete StoreService;
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
int WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	// Get command line arguments and convert to UTF8
	const wchar_t* CommandLine = GetCommandLineW();
	int ArgC;
	wchar_t** WideArgV = CommandLineToArgvW(CommandLine, &ArgC);

	TInlineBuffer<320> ArgBuffer;
	char** ArgV = (char**)(ArgBuffer->Append(ArgC * sizeof(char*)));
	for (int i = 0; i < ArgC; ++i)
	{
		const wchar_t* WideArg = WideArgV[i];
		int32 ArgSize = WideCharToMultiByte(CP_UTF8, 0, WideArg, -1, nullptr, 0, nullptr, nullptr);

		char* Arg = (char*)(ArgBuffer->Append(ArgSize));
		WideCharToMultiByte(CP_UTF8, 0, WideArg, -1, Arg, ArgSize, nullptr, nullptr);

		ArgV[i] = Arg;
	}

	// The proper entry point
	extern int main(int, char**);
	int Ret = main(ArgC, ArgV);

	// Clean up
	LocalFree(ArgV);
	return Ret;
}

#endif // TS_PLATFORM_WINDOWS



// {{{1 linux/mac --------------------------------------------------------------

#if TS_USING(TS_PLATFORM_LINUX) || TS_USING(TS_PLATFORM_MAC)

////////////////////////////////////////////////////////////////////////////////
static const char*		GShmName					= "/UnrealTraceShm";
static const int32		GShmSize					= 4 << 10;
static const char*		GBegunSemName				= "/UnrealTraceSemBegun";
static const char*		GQuitSemName				= "/UnrealTraceSemQuit";
static int				MainDaemon(int, char**);

////////////////////////////////////////////////////////////////////////////////
static int MainFork(int ArgC, char** ArgV)
{
	// Check for an existing instance that is already running.
	int ShmHandle = shm_open(GShmName, O_RDONLY, S_IROTH|S_IWOTH);
	if (ShmHandle >= 0)
	{
		void* ShmPtr = mmap(nullptr, GShmSize, PROT_READ, MAP_SHARED, ShmHandle, 0);
		FMmapScope MmapScope(ShmPtr, GShmSize);
		close(ShmHandle);

		const auto* InstanceInfo = MmapScope.As<const FInstanceInfo>();
		InstanceInfo->WaitForReady();
		if (!InstanceInfo->IsOlder())
		{
			return 0;
		}

		// Signal to the existing instance to shutdown.
		sem_t* QuitSem = sem_open(GQuitSemName, O_RDWR, 0666, 0);
		if (QuitSem == SEM_FAILED)
		{
			// Assume someone else has closed the instance already.
			return 0;
		}
		sem_post(QuitSem);
		sem_close(QuitSem);

		// Wait for the process to end. If it takes too long, kill it.
		for (uint32 i = 0; ; i += 1000)
		{
			if (kill(InstanceInfo->Pid, 0) < 0)
			{
				break;
			}

			if (i >= 5000) // "5000" for grap-ability
			{
				kill(InstanceInfo->Pid, SIGTERM);
				break;
			}
		}
	}

	// Launch a new instance as a daemon and wait until we know it has started
	sem_t* BegunSem = sem_open(GBegunSemName, O_RDONLY|O_CREAT|O_EXCL, 0644, 0);
	if (BegunSem == SEM_FAILED)
	{
		return (errno == EEXIST) ? 7 : 11;
	}
	OnScopeExit([=] () {
		sem_unlink(GBegunSemName);
		sem_close(BegunSem);
	});

	// For debugging ease and consistency we will daemonize in this process
	// instead of spawning a second one.
#if TS_USING(TS_BUILD_DEBUG)
	std::thread DaemonThread([] () { MainDaemon(0, nullptr); });
#else
	pid_t DaemonPid = fork();
	if (DaemonPid < 0)
	{
		return 3;
	}
	else if (DaemonPid == 0)
	{
		return MainDaemon(0, nullptr);
	}
#endif // TS_BUILD_DEBUG

	int Ret = 0;
	timespec Timeout;
	clock_gettime(CLOCK_REALTIME, &Timeout);
	Timeout.tv_sec += (5000 / 1000); // expressed in milliseconds for grep-ability
	if (sem_timedwait(BegunSem, &Timeout) < 0)
	{
		Ret = 8;
	}

#if TS_USING(TS_BUILD_DEBUG)
	DaemonThread.join();
#endif

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
static int MainDaemon(int ArgC, char** ArgV)
{
	// Create a piece of shared memory so all store instances can communicate.
	int ShmHandle = shm_open(GShmName, O_RDWR|O_CREAT|O_EXCL, S_IROTH|S_IWOTH);
	if (ShmHandle < 0)
	{
		return 1;
	}
	OnScopeExit([=] () {
		shm_unlink(GShmName);
		close(ShmHandle);
	});

	// Create a named semaphore so others can tell us to quit.
	sem_t* QuitSem = sem_open(GQuitSemName, O_RDWR|O_CREAT|O_EXCL, 0666, 0);
	if (QuitSem == SEM_FAILED)
	{
		// This really should not happen. It is expected that only one process
		// will get this far (gated by the shared-memory object creation).
		return 2;
	}
	OnScopeExit([=] () {
		sem_unlink(GQuitSemName);
		sem_close(QuitSem);
	});

	// Fill out the Ipc details and publish.
	if (ftruncate(ShmHandle, GShmSize) != 0)
	{
		return 3;
	}

	int32 ProtFlags = PROT_READ|PROT_WRITE;
	void* ShmPtr = mmap(nullptr, GShmSize, ProtFlags, MAP_SHARED, ShmHandle, 0);
	{
		FMmapScope MmapScope(ShmPtr, GShmSize);
		auto* InstanceInfo = MmapScope.As<FInstanceInfo>();
		InstanceInfo->Set();
	}

	// Fire up the store
	FStoreService* StoreService;
	{
		int UserId = getuid();
		const passwd* Passwd = getpwuid(UserId);

		std::filesystem::path StoreDir = Passwd->pw_dir;
		StoreDir /= "UnrealEngine/UnrealTrace/Store";

		StoreService = StartStore(StoreDir.c_str());
	}

	// Let every one know we've started.
	sem_t* BegunSem = sem_open(GBegunSemName, O_RDWR, 0644, 0);
	if (BegunSem != SEM_FAILED)
	{
		sem_post(BegunSem);
		sem_close(BegunSem);
	}

	// Wait to be told to resign.
	sem_wait(QuitSem);

	// Clean up. We are done here.
	delete StoreService;
	return 0;
}

#endif // TS_PLATFORM_LINUX/MAC



// {{{1 main -------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
int MainTest(int ArgC, char** ArgV)
{
	extern void TestCbor();
	TestCbor();
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
int main(int ArgC, char** ArgV)
{
	struct {
		const char*	Verb;
		int			(*Entry)(int, char**);
	} Dispatches[] = {
		"fork",		MainFork,
		"daemon",	MainDaemon,
		"test",		MainTest,
		//"kill",		MainKill,
	};

	if (ArgC > 1)
	{
		for (const auto& Dispatch : Dispatches)
		{
			if (strcmp(ArgV[1], Dispatch.Verb) == 0)
			{
				return (Dispatch.Entry)(ArgC - 1, ArgV + 1);
			}
		}
	}

	return MainFork(ArgC, ArgV);
}

/* vim: set noexpandtab foldlevel=1 : */
