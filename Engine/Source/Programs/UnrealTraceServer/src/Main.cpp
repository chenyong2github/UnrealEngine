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
#	include <sched.h>
#	include <signal.h>
#	include <sys/mman.h>
#	include <unistd.h>
#endif

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



////////////////////////////////////////////////////////////////////////////////
static void GetUnrealTraceHome(std::filesystem::path& Out, bool Make=false)
{
#if TS_USING(TS_PLATFORM_WINDOWS)
	wchar_t Buffer[MAX_PATH];
	SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, Buffer);
	Out = Buffer;
	Out /= "UnrealEngine/Common/UnrealTrace";
#else
	int UserId = getuid();
	const passwd* Passwd = getpwuid(UserId);
	Out = Passwd->pw_dir;
	Out /= "UnrealEngine/UnrealTrace";
#endif

	if (Make)
	{
		std::filesystem::create_directories(Out);
	}
}




// {{{1 logging ----------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
#define TS_LOG(Format, ...) \
	do { FLogging::Log(Format "\n", ##__VA_ARGS__); } while (false)

////////////////////////////////////////////////////////////////////////////////
class FLogging
{
public:
	static void			Initialize();
	static void			Shutdown();
	static void			Log(const char* Format, ...);

private:
						FLogging();
						~FLogging();
						FLogging(const FLogging&) = delete;
						FLogging(FLogging&&) = default;
	void				LogImpl(const char* String) const;
	static FLogging*	Instance;
	FILE*				File = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
FLogging* FLogging::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////
FLogging::FLogging()
{
	std::filesystem::path LogPath;

#if TS_USING(TS_PLATFORM_WINDOWS)
	GetUnrealTraceHome(LogPath, true);
	LogPath /= "Server.log";
	File = _wfopen(LogPath.c_str(), L"wb");
#else
#	if TS_USING(TS_PLATFORM_MAC)
	int UserId = getuid();
	const passwd* Passwd = getpwuid(UserId);
	LogPath = Passwd->pw_dir;
	LogPath /= "Library/Logs/Unreal Engine/UnrealTrace/Server.log";
#	else
	LogPath = "/var/log/UnrealTrace/Server.log";
#	endif

	std::filesystem::create_directories(LogPath.parent_path());
	File = fopen(LogPath.c_str(), "wb");
#endif
}

////////////////////////////////////////////////////////////////////////////////
FLogging::~FLogging()
{
	if (File != nullptr)
	{
		fclose(File);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FLogging::Initialize()
{
	if (Instance != nullptr)
	{
		return;
	}

	Instance = new FLogging();
}

////////////////////////////////////////////////////////////////////////////////
void FLogging::Shutdown()
{
	if (Instance == nullptr)
	{
		return;
	}

	delete Instance;
	Instance = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
void FLogging::LogImpl(const char* String) const
{
	if (File != nullptr)
	{
		fputs(String, File);
	}

	fputs(String, stdout);
}

////////////////////////////////////////////////////////////////////////////////
void FLogging::Log(const char* Format, ...)
{
	va_list VaList;
	va_start(VaList, Format);

	char Buffer[320];
	vsnprintf(Buffer, TS_ARRAY_COUNT(Buffer), Format, VaList);
	Buffer[TS_ARRAY_COUNT(Buffer) - 1] = '\0';

	Instance->LogImpl(Buffer);

	va_end(VaList);
}



////////////////////////////////////////////////////////////////////////////////
struct FLoggingScope
{
	FLoggingScope()		{ FLogging::Initialize(); }
	~FLoggingScope()	{ FLogging::Shutdown(); }
};



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
#if TS_USING(TS_PLATFORM_WINDOWS)
	for (;; Sleep(0))
#else
	for (;; sched_yield())
#endif
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

	TS_LOG("Opening quit event");
	FWinHandle QuitEvent = CreateEventW(nullptr, TRUE, FALSE, GQuitEventName);
	if (GetLastError() != ERROR_ALREADY_EXISTS)
	{
		TS_LOG("Not found (gle=%d)", GetLastError());
		return Result_NoQuitEvent;
	}

	TS_LOG("Open the process %d", InstanceInfo->Pid);
	DWORD OpenProcessFlags = PROCESS_TERMINATE | SYNCHRONIZE;
	FWinHandle ProcHandle = OpenProcess(OpenProcessFlags, FALSE, InstanceInfo->Pid);
	if (!ProcHandle.IsValid())
	{
		TS_LOG("Unsuccessful (gle=%d)", GetLastError());
		return CreateExitCode(Result_ProcessOpenFail);
	}

	TS_LOG("Firing quit event and waiting for process");
	SetEvent(QuitEvent);

	if (WaitForSingleObject(ProcHandle, 5000) == WAIT_TIMEOUT)
	{
		TS_LOG("Timeout. Force terminating");
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
		TS_LOG("All good. There was no active UTS process");
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
	TS_LOG("Opening exist instance's shared memory");
	FWinHandle IpcHandle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, false, GIpcName);
	if (IpcHandle.IsValid())
	{
		void* IpcPtr = MapViewOfFile(IpcHandle, FILE_MAP_ALL_ACCESS, 0, 0, GIpcSize);
		FMmapScope MmapScope(IpcPtr);

		const auto* InstanceInfo = MmapScope.As<const FInstanceInfo>();
		InstanceInfo->WaitForReady();
		if (!InstanceInfo->IsOlder())
		{
			TS_LOG("Existing instance is the same age or newer");
			return Result_Ok;
		}

		// Kill the other instance.
		int KillRet = MainKillImpl(0, nullptr, InstanceInfo);
		if (KillRet == Result_NoQuitEvent)
		{
			// If no quit event was found then we shall assume that another new
			// store instance beat us to it.
			TS_LOG("Looks like someone else has already taken care of the upgrade");
			return Result_Ok;
		}

		if (KillRet != Result_Ok)
		{
			TS_LOG("Kill attempt failed (ret=%d)", KillRet);
			return KillRet;
		}
	}
	else
	{
		TS_LOG("No existing process/shared memory found");
	}

	// Get this binary's path
	TS_LOG("Getting binary path");
	wchar_t BinPath[MAX_PATH];
	uint32 BinPathLen = GetModuleFileNameW(nullptr, BinPath, TS_ARRAY_COUNT(BinPath));
	if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		// This should never really happen...
		TS_LOG("MAX_PATH is not enough");
		return CreateExitCode(Result_UnexpectedError);
	}
	TS_LOG("Binary located at '%ls'", BinPath);

	// Calculate where to store the binaries.
	std::filesystem::path DestPath;
	GetUnrealTraceHome(DestPath);
	{
		wchar_t Buffer[64];
		_snwprintf(Buffer, TS_ARRAY_COUNT(Buffer), L"Bin/%08x/UnrealTraceServer.exe", FInstanceInfo::CurrentVersion);
		DestPath /= Buffer;
	}
	TS_LOG("Run path '%ls'", DestPath.c_str());

	// Copy the binary out to a location where it doesn't matter if the file
	// gets locked by the OS.
	if (!std::filesystem::is_regular_file(DestPath))
	{
		TS_LOG("Copying to run path");

		std::filesystem::create_directories(DestPath.parent_path());

		// Tag the destination with our PID and copy
		DWORD OurPid = GetCurrentProcessId();
		wchar_t Buffer[16];
		_snwprintf(Buffer, TS_ARRAY_COUNT(Buffer), L"_%08x", OurPid);
		std::filesystem::path TempPath = DestPath;
		TempPath += Buffer;
		if (!std::filesystem::copy_file(BinPath, TempPath))
		{
			TS_LOG("File copy failed (gle=%d)", GetLastError());
			return CreateExitCode(Result_CopyFail);
		}

		// Move the file into place. If this fails because the file exists then
		// another instance has beaten us to the punch.
		std::error_code ErrorCode;
		std::filesystem::rename(TempPath, DestPath, ErrorCode);
		if (ErrorCode)
		{
			bool bRaceLost = (ErrorCode == std::errc::file_exists);
			TS_LOG("Rename to destination failed (bRaceLost=%c)", bRaceLost);
			return bRaceLost ? Result_Ok : CreateExitCode(Result_RenameFail);
		}
	}
	else
	{
		TS_LOG("Already exists");
	}

	// Launch a new instance as a daemon and wait until we know it has started
	TS_LOG("Creating begun event");
	FWinHandle BegunEvent = CreateEventW(nullptr, TRUE, FALSE, GBegunEventName);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		TS_LOG("Did not work (gle=%d)", GetLastError());
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

	TS_LOG("Waiting on begun event");
	int Ret = Result_Ok;
	if (WaitForSingleObject(BegunEvent, 5000) == WAIT_TIMEOUT)
	{
		TS_LOG("Wait timed out (gle=%d)", GetLastError());
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

	TS_LOG("Complete (ret=%d)", Ret);
	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
static int MainDaemon(int ArgC, char** ArgV)
{
	// Create a piece of shared memory so all store instances can communicate.
	TS_LOG("Creating some shared memory");
	FWinHandle IpcHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
		PAGE_READWRITE, 0, GIpcSize, GIpcName);
	if (!IpcHandle.IsValid())
	{
		TS_LOG("Creation unsuccessful (gle=%d)", GetLastError());
		return CreateExitCode(Result_SharedMemFail);
	}

	// Create a named event so others can tell us to quit.
	TS_LOG("Creating a quit event");
	FWinHandle QuitEvent = CreateEventW(nullptr, TRUE, FALSE, GQuitEventName);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		// This really should not happen. It is expected that only one process
		// will get this far (gated by the shared-memory object creation).
		TS_LOG("It unexpectedly exists already");
		return CreateExitCode(Result_QuitExists);
	}

	// Fill out the Ipc details and publish.
	void* IpcPtr = MapViewOfFile(IpcHandle, FILE_MAP_ALL_ACCESS, 0, 0, GIpcSize);
	{
		TS_LOG("Writing shared instance info");
		FMmapScope MmapScope(IpcPtr);
		auto* InstanceInfo = MmapScope.As<FInstanceInfo>();
		InstanceInfo->Set();
	}

	// Fire up the store
	TS_LOG("Starting the store");
	FStoreService* StoreService;
	{
		std::filesystem::path StoreDir;
		GetUnrealTraceHome(StoreDir);
		StoreDir /= "Store";

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

	TS_LOG("Opening quit semaphore");
	sem_t* QuitSem = sem_open(GQuitSemName, O_RDWR, 0666, 0);
	if (QuitSem == SEM_FAILED)
	{
		// Assume someone else has closed the instance already.
		TS_LOG("Create failed (errno=%d)", errno);
		return Result_NoQuitEvent;
	}
	sem_post(QuitSem);
	sem_close(QuitSem);

	// Wait for the process to end. If it takes too long, kill it.
	TS_LOG("Waiting for pid %d", InstanceInfo->Pid);
	const uint32 SleepMs = 47;
	timespec SleepTime = { 0, SleepMs * 1000 * 1000 };
	nanosleep(&SleepTime, nullptr);
	for (uint32 i = 0; ; i += SleepMs)
	{
		if (i >= 5000) // "5000" for grep-ability
		{
			kill(InstanceInfo->Pid, SIGKILL);
			TS_LOG("Timed out. Sent SIGKILL instead (errno=%d)", errno);
			break;
		}

		if (kill(InstanceInfo->Pid, 0) == ESRCH)
		{
			TS_LOG("Process no longer exists");
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
		if (errno == ENOENT)
		{
			TS_LOG("All good. Ain't nuffin' running me ol' mucker.");
			return Result_Ok;
		}
		else
		{
			TS_LOG("Unable to open shared memory (%s, errno=%d)", GShmName, errno);
			return Result_SharedMemFail;
		}
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
	TS_LOG("Attempting to open shared memory %s", GShmName);
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
			TS_LOG("Existing instance is the same age or newer");
			return Result_Ok;
		}

		// Terminate the other instance.
		TS_LOG("Killing an older instance that is already running");
		int KillRet = MainKillImpl(0, nullptr, InstanceInfo);
		if (KillRet == Result_NoQuitEvent)
		{
			// If no quit event was found then we shall assume that another new
			// store instance beat us to it.
			TS_LOG("Looks like someone else has already taken care of the upgrade");
			return Result_Ok;
		}

		if (KillRet != Result_Ok)
		{
			TS_LOG("Kill attempt failed (ret=%d)", KillRet);
			return KillRet;
		}
	}
	else
	{
		TS_LOG("Unable to open shared memory (errno=%d)", errno);
		TS_LOG("There probably is not an existing instance running");
	}

	// Launch a new instance as a daemon and wait until we know it has started
	TS_LOG("Creating semaphore %s", GBegunSemName);
	sem_t* BegunSem = sem_open(GBegunSemName, O_RDONLY|O_CREAT|O_EXCL, 0644, 0);
	if (BegunSem == SEM_FAILED)
	{
		int Ret = (errno == EEXIST) ? Result_BegunExists : Result_BegunCreateFail;
		TS_LOG("Failed (errno=%d, ret=%d)", errno, Ret);
		return Ret;
	}
	OnScopeExit([=] () {
		sem_unlink(GBegunSemName);
		sem_close(BegunSem);
	});

	// For debugging ease and consistency we will daemonize in this process
	// instead of spawning a second one.
	pid_t DaemonPid = -1;
#if TS_USING(TS_BUILD_DEBUG)
	std::thread DaemonThread([] () { MainDaemon(0, nullptr); });
#else
	TS_LOG("Forking process");
	DaemonPid = fork();
	if (DaemonPid < 0)
	{
		TS_LOG("Failed (errno=%d)", errno);
		return Result_ForkFail;
	}
	else if (DaemonPid == 0)
	{
		return MainDaemon(0, nullptr);
	}
#endif // TS_BUILD_DEBUG

	TS_LOG("Waiting for begun semaphore from pid %d", DaemonPid);
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
			TS_LOG("Timed out");
			Ret = Result_BegunTimeout;
			break;
		}
	}

#if TS_USING(TS_BUILD_DEBUG)
	DaemonThread.join();
#endif

	TS_LOG("Forked complete (ret=%d)", Ret);
	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
static int MainDaemon(int ArgC, char** ArgV)
{
	// Create a piece of shared memory so all store instances can communicate.
	TS_LOG("Creating shared memory to store instance data (%s)", GShmName);
	int ShmHandle = shm_open(GShmName, O_RDWR|O_CREAT|O_EXCL, 0644);
	if (ShmHandle < 0)
	{
		TS_LOG("Well that was not at all successful (errno=%d)", errno);
		return Result_SharedMemFail;
	}
	OnScopeExit([=] () {
		shm_unlink(GShmName);
		close(ShmHandle);
	});

	// Create a named semaphore so others can tell us to quit.
	TS_LOG("Creating a master quit semaphore %s", GQuitSemName);
	sem_t* QuitSem = sem_open(GQuitSemName, O_RDWR|O_CREAT|O_EXCL, 0666, 0);
	if (QuitSem == SEM_FAILED)
	{
		// This really should not happen. It is expected that only one process
		// will get this far (gated by the shared-memory object creation).
		TS_LOG("Something went wrong (errno=%d)", errno);
		return Result_UnexpectedError;
	}
	OnScopeExit([=] () {
		sem_unlink(GQuitSemName);
		sem_close(QuitSem);
	});

	// Fill out the Ipc details and publish.
	TS_LOG("Truncating shared memory");
	if (ftruncate(ShmHandle, GShmSize) != 0)
	{
		TS_LOG("Oh bother (errno=%d)", errno);
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
		TS_LOG("Starting the store");
		std::filesystem::path StoreDir;
		GetUnrealTraceHome(StoreDir);
		StoreDir /= "Store";

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
	TS_LOG("Waiting on the quit semaphore");
	do
	{
		sem_wait(QuitSem);
	}
	while (errno == EINTR);
	TS_LOG("Wait over (errno=%d)", errno);

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
	FLoggingScope LoggingScope;

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
