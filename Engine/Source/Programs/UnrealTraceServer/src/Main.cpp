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
	Desc.RecorderPort = 1981;
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
	Version = CurrentVersion;
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



// {{{1 return codes -----------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
enum : int
{
	Result_Ok				= 0,
	Result_BegunCreateFail,
	Result_BegunExists,
	Result_BegunTimeout,
	Result_CopyFail,
	Result_ForkFail,
	Result_LaunchFail,
	Result_NoQuitEvent,
	Result_ProcessOpenFail,
	Result_QuitExists,
	Result_RenameFail,
	Result_SharedMemFail,
	Result_SharedMemTruncFail,
	Result_UnexpectedError,
};



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
void					AddToSystemTray(FStoreService&);
void					RemoveFromSystemTray();

////////////////////////////////////////////////////////////////////////////////
static int CreateExitCode(uint32 Id)
{
	return (GetLastError() & 0xfff) | (Id << 12);
}

////////////////////////////////////////////////////////////////////////////////
static int MainKillImpl(int ArgC, char** ArgV, const FInstanceInfo* InstanceInfo)
{
	// Signal to the existing instance to shutdown or forcefully do it if it
	// does not respond in time.

	FWinHandle QuitEvent = CreateEventW(nullptr, TRUE, FALSE, GQuitEventName);
	if (GetLastError() != ERROR_ALREADY_EXISTS)
	{
		return Result_NoQuitEvent;
	}

	DWORD OpenProcessFlags = PROCESS_TERMINATE | SYNCHRONIZE;
	FWinHandle ProcHandle = OpenProcess(OpenProcessFlags, FALSE, InstanceInfo->Pid);
	if (!ProcHandle.IsValid())
	{
		return CreateExitCode(Result_ProcessOpenFail);
	}

	SetEvent(QuitEvent);

	if (WaitForSingleObject(ProcHandle, 5000) == WAIT_TIMEOUT)
	{
		TerminateProcess(ProcHandle, 10);
	}

	return Result_Ok;
}

////////////////////////////////////////////////////////////////////////////////
static int MainKill(int ArgC, char** ArgV)
{
	// Find if an existing instance is already running.
	FWinHandle IpcHandle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, false, GIpcName);
	if (!IpcHandle.IsValid())
	{
		return Result_Ok;
	}

	// There is an instance running so we can get its info block
	void* IpcPtr = MapViewOfFile(IpcHandle, FILE_MAP_ALL_ACCESS, 0, 0, GIpcSize);
	FMmapScope MmapScope(IpcPtr);
	const auto* InstanceInfo = MmapScope.As<const FInstanceInfo>();
	InstanceInfo->WaitForReady();

	return MainKillImpl(ArgC, ArgV, InstanceInfo);
}

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
			return Result_Ok;
		}

		// Kill the other instance.
		int KillRet = MainKillImpl(0, nullptr, InstanceInfo);
		if (KillRet == Result_NoQuitEvent)
		{
			// If no quit event was found then we shall assume that another new
			// store instance beat us to it.
			return Result_Ok;
		}

		if (KillRet != Result_Ok)
		{
			return KillRet;
		}
	}

	// Get this binary's path
	wchar_t BinPath[MAX_PATH];
	uint32 BinPathLen = GetModuleFileNameW(nullptr, BinPath, TS_ARRAY_COUNT(BinPath));
	if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		// This should never really happen...
		return CreateExitCode(Result_UnexpectedError);
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
			return CreateExitCode(Result_CopyFail);
		}

		// Move the file into place. If this fails because the file exists then
		// another instance has beaten us to the punch.
		std::error_code ErrorCode;
		std::filesystem::rename(TempPath, DestPath, ErrorCode);
		if (ErrorCode)
		{
			bool RaceLost = (ErrorCode == std::errc::file_exists);
			return RaceLost ? Result_Ok : CreateExitCode(Result_RenameFail);
		}
	}

	// Launch a new instance as a daemon and wait until we know it has started
	FWinHandle BegunEvent = CreateEventW(nullptr, TRUE, FALSE, GBegunEventName);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		return CreateExitCode(Result_BegunExists);
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
		return CreateExitCode(Result_LaunchFail);
	}
#endif // TS_BUILD_DEBUG

	int Ret = Result_Ok;
	if (WaitForSingleObject(BegunEvent, 5000) == WAIT_TIMEOUT)
	{
		Ret = CreateExitCode(Result_BegunTimeout);
	}

#if TS_USING(TS_BUILD_DEBUG)
	static bool volatile bShouldExit = false;
	while (true)
	{
		Sleep(500);

		if (bShouldExit)
		{
			FWinHandle QuitEvent = CreateEventW(nullptr, TRUE, FALSE, GQuitEventName);
			SetEvent(QuitEvent);
			break;
		}
	}

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
		return CreateExitCode(Result_SharedMemFail);
	}

	// Create a named event so others can tell us to quit.
	FWinHandle QuitEvent = CreateEventW(nullptr, TRUE, FALSE, GQuitEventName);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		// This really should not happen. It is expected that only one process
		// will get this far (gated by the shared-memory object creation).
		return CreateExitCode(Result_QuitExists);
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

	// To clearly indicate to users that we are around we'll add an icon to the
	// system tray.
	AddToSystemTray(*StoreService);

	// Wait to be told to resign.
	WaitForSingleObject(QuitEvent, INFINITE);

	// Clean up. We are done here.
	RemoveFromSystemTray();
	delete StoreService;
	return Result_Ok;
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
static int MainKillImpl(int ArgC, char** ArgV, const FInstanceInfo* InstanceInfo)
{
	// Signal to the existing instance to shutdown or forcefully do it if it
	// does not respond in time.

	sem_t* QuitSem = sem_open(GQuitSemName, O_RDWR, 0666, 0);
	if (QuitSem == SEM_FAILED)
	{
		// Assume someone else has closed the instance already.
		return Result_NoQuitEvent;
	}
	sem_post(QuitSem);
	sem_close(QuitSem);

	// Wait for the process to end. If it takes too long, kill it.
	const uint32 SleepMs = 47;
	timespec SleepTime = { 0, SleepMs * 1000 * 1000 };
	nanosleep(&SleepTime, nullptr);
	for (uint32 i = 0; ; i += SleepMs)
	{
		if (i >= 5000) // "5000" for grep-ability
		{
			kill(InstanceInfo->Pid, SIGKILL);
			break;
		}

		if (kill(InstanceInfo->Pid, 0) == ESRCH)
		{
			break;
		}

		nanosleep(&SleepTime, nullptr);
	}

	return Result_Ok;
}

////////////////////////////////////////////////////////////////////////////////
static int MainKill(int ArgC, char** ArgV)
{
	// Check for an existing instance that is already running.
	int ShmHandle = shm_open(GShmName, O_RDONLY, 0444);
	if (ShmHandle < 0)
	{
		return (errno == ENOENT) ? Result_Ok : Result_SharedMemFail;
	}

	void* ShmPtr = mmap(nullptr, GShmSize, PROT_READ, MAP_SHARED, ShmHandle, 0);
	FMmapScope MmapScope(ShmPtr, GShmSize);
	const auto* InstanceInfo = MmapScope.As<const FInstanceInfo>();
	InstanceInfo->WaitForReady();
	close(ShmHandle);

	return MainKillImpl(ArgC, ArgV, InstanceInfo);
}

////////////////////////////////////////////////////////////////////////////////
static int MainFork(int ArgC, char** ArgV)
{
	// Check for an existing instance that is already running.
	int ShmHandle = shm_open(GShmName, O_RDONLY, 0444);
	if (ShmHandle >= 0)
	{
		void* ShmPtr = mmap(nullptr, GShmSize, PROT_READ, MAP_SHARED, ShmHandle, 0);
		FMmapScope MmapScope(ShmPtr, GShmSize);
		close(ShmHandle);

		const auto* InstanceInfo = MmapScope.As<const FInstanceInfo>();
		InstanceInfo->WaitForReady();
		if (!InstanceInfo->IsOlder())
		{
			return Result_Ok;
		}

		// Terminate the other instance.
		int KillRet = MainKillImpl(0, nullptr, InstanceInfo);
		if (KillRet == Result_NoQuitEvent)
		{
			// If no quit event was found then we shall assume that another new
			// store instance beat us to it.
			return Result_Ok;
		}

		if (KillRet != Result_Ok)
		{
			return KillRet;
		}
	}

	// Launch a new instance as a daemon and wait until we know it has started
	sem_t* BegunSem = sem_open(GBegunSemName, O_RDONLY|O_CREAT|O_EXCL, 0644, 0);
	if (BegunSem == SEM_FAILED)
	{
		return (errno == EEXIST) ? Result_BegunExists : Result_BegunCreateFail;
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
		return Result_ForkFail;
	}
	else if (DaemonPid == 0)
	{
		return MainDaemon(0, nullptr);
	}
#endif // TS_BUILD_DEBUG

	int Ret = Result_Ok;
	int TimeoutMs = 5000;
	while (sem_trywait(BegunSem) < 0)
	{
		const uint32 SleepMs = 239;
		timespec SleepTime = { 0, SleepMs * 1000 * 1000 };
		nanosleep(&SleepTime, nullptr);

		TimeoutMs -= SleepMs;
		if (TimeoutMs <= 0)
		{
			Ret = Result_BegunTimeout;
			break;
		}
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
	int ShmHandle = shm_open(GShmName, O_RDWR|O_CREAT|O_EXCL, 0644);
	if (ShmHandle < 0)
	{
		return Result_SharedMemFail;
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
		return Result_UnexpectedError;
	}
	OnScopeExit([=] () {
		sem_unlink(GQuitSemName);
		sem_close(QuitSem);
	});

	// Fill out the Ipc details and publish.
	if (ftruncate(ShmHandle, GShmSize) != 0)
	{
		return Result_SharedMemTruncFail;
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
	do
	{
		sem_wait(QuitSem);
	}
	while (errno == EINTR);

	// Clean up. We are done here.
	delete StoreService;
	return Result_Ok;
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
		"kill",		MainKill,
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
