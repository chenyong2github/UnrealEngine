// Copyright Epic Games, Inc. All Rights Reserved.

#include "Windows/WindowsPlatformCrashContext.h"
#include "HAL/PlatformMallocCrash.h"
#include "HAL/ExceptionHandling.h"
#include "Misc/EngineVersion.h"
#include "Misc/EngineBuildSettings.h"
#include "HAL/ExceptionHandling.h"
#include "HAL/ThreadHeartBeat.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformOutputDevices.h"
#include "Internationalization/Internationalization.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FeedbackContext.h"
#include "Misc/MessageDialog.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/OutputDeviceFile.h"
#include "Windows/WindowsPlatformStackWalk.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Templates/UniquePtr.h"
#include "Misc/OutputDeviceArchiveWrapper.h"
#include "HAL/ThreadManager.h"
#include "BuildSettings.h"
#include <strsafe.h>
#include <dbghelp.h>
#include <Shlwapi.h>
#include <psapi.h>
#include <tlhelp32.h>

#ifndef UE_LOG_CRASH_CALLSTACK
	#define UE_LOG_CRASH_CALLSTACK 1
#endif

#if WITH_EDITOR
	#define USE_CRASH_REPORTER_MONITOR 1
#else 
	#define USE_CRASH_REPORTER_MONITOR 0
#endif

#define CR_CLIENT_MAX_PATH_LEN 265
#define CR_CLIENT_MAX_ARGS_LEN 256

#pragma comment( lib, "version.lib" )
#pragma comment( lib, "Shlwapi.lib" )

LONG WINAPI UnhandledException(EXCEPTION_POINTERS *ExceptionInfo);
LONG WINAPI UnhandledStaticInitException(LPEXCEPTION_POINTERS ExceptionInfo);

/** Platform specific constants. */
enum EConstants
{
	UE4_MINIDUMP_CRASHCONTEXT = LastReservedStream + 1,
};


/**
 * Code for an assert exception
 */
const uint32 AssertExceptionCode = 0x4000;
const uint32 GPUCrashExceptionCode = 0x8000;

namespace {
	/**
	 * Write a Windows minidump to disk
	 * @param The Crash context with its data already serialized into its buffer
	 * @param Path Full path of file to write (normally a .dmp file)
	 * @param ExceptionInfo Pointer to structure containing the exception information
	 * @return Success or failure
	 */

	 // #CrashReport: 2014-10-08 Move to FWindowsPlatformCrashContext
	bool WriteMinidump(HANDLE Process, DWORD ThreadId, FWindowsPlatformCrashContext& InContext, const TCHAR* Path, LPEXCEPTION_POINTERS ExceptionInfo)
	{
		// Are we calling this in process or from an external process?
		const BOOL bIsClientPointers = Process != GetCurrentProcess();

		// Try to create file for minidump.
		HANDLE FileHandle = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		if (FileHandle == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		// Initialise structure required by MiniDumpWriteDumps
		MINIDUMP_EXCEPTION_INFORMATION DumpExceptionInfo = {};

		DumpExceptionInfo.ThreadId = ThreadId;
		DumpExceptionInfo.ExceptionPointers = ExceptionInfo;
		DumpExceptionInfo.ClientPointers = bIsClientPointers;

		// CrashContext.runtime-xml is now a part of the minidump file.
		MINIDUMP_USER_STREAM CrashContextStream = { 0 };
		CrashContextStream.Type = UE4_MINIDUMP_CRASHCONTEXT;
		CrashContextStream.BufferSize = InContext.GetBuffer().GetAllocatedSize();
		CrashContextStream.Buffer = (void*)*InContext.GetBuffer();

		MINIDUMP_USER_STREAM_INFORMATION CrashContextStreamInformation = { 0 };
		CrashContextStreamInformation.UserStreamCount = 1;
		CrashContextStreamInformation.UserStreamArray = &CrashContextStream;

		MINIDUMP_TYPE MinidumpType = MiniDumpNormal;//(MINIDUMP_TYPE)(MiniDumpWithPrivateReadWriteMemory|MiniDumpWithDataSegs|MiniDumpWithHandleData|MiniDumpWithFullMemoryInfo|MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules);

		// For ensures by default we use minidump to avoid severe hitches when writing 3GB+ files.
		// However the crash dump mode will remain the same.
		bool bShouldBeFullCrashDump = InContext.IsFullCrashDump();
		if (bShouldBeFullCrashDump)
		{
			MinidumpType = (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
		}

		const BOOL Result = MiniDumpWriteDump(Process, GetProcessId(Process), FileHandle, MinidumpType, &DumpExceptionInfo, &CrashContextStreamInformation, NULL);
		CloseHandle(FileHandle);

		return Result == TRUE;
	}
}

/**
 * Stores information about an assert that can be unpacked in the exception handler.
 */
struct FAssertInfo
{
	const TCHAR* ErrorMessage;
	int32 NumStackFramesToIgnore;

	FAssertInfo(const TCHAR* InErrorMessage, int32 InNumStackFramesToIgnore)
		: ErrorMessage(InErrorMessage)
		, NumStackFramesToIgnore(InNumStackFramesToIgnore)
	{
	}
};

void FWindowsPlatformCrashContext::GetProcModuleHandles(const FProcHandle& ProcessHandle, FModuleHandleArray& OutHandles)
{
	// Get all the module handles for the current process. Each module handle is its base address.
	for (;;)
	{
		DWORD BufferSize = OutHandles.Num() * sizeof(HMODULE);
		DWORD RequiredBufferSize = 0;
		if (!EnumProcessModulesEx(ProcessHandle.IsValid() ? ProcessHandle.Get() : GetCurrentProcess(), (HMODULE*)OutHandles.GetData(), BufferSize, &RequiredBufferSize, LIST_MODULES_ALL))
		{
			return;
		}
		if (RequiredBufferSize <= BufferSize)
		{
			break;
		}
		OutHandles.SetNum(RequiredBufferSize / sizeof(HMODULE));
	}
	// Sort the handles by address. This allows us to do a binary search for the module containing an address.
	Algo::Sort(OutHandles);
}

void FWindowsPlatformCrashContext::ConvertProgramCountersToStackFrames(
	const FProcHandle& ProcessHandle,
	const FModuleHandleArray& SortedModuleHandles,
	const uint64* ProgramCounters,
	int32 NumPCs,
	TArray<FCrashStackFrame>& OutStackFrames)
{
	// Prepare the callstack buffer
	OutStackFrames.Reset(NumPCs);

	// Create the crash context
	for (int32 Idx = 0; Idx < NumPCs; ++Idx)
	{
		int32 ModuleIdx = Algo::UpperBound(SortedModuleHandles, (void*)ProgramCounters[Idx]) - 1;
		if (ModuleIdx < 0 || ModuleIdx >= SortedModuleHandles.Num())
		{
			OutStackFrames.Add(FCrashStackFrame(TEXT("Unknown"), 0, ProgramCounters[Idx]));
		}
		else
		{
			TCHAR ModuleName[MAX_PATH];
			if (GetModuleFileNameExW(ProcessHandle.IsValid() ? ProcessHandle.Get() : GetCurrentProcess(), (HMODULE)SortedModuleHandles[ModuleIdx], ModuleName, MAX_PATH) != 0)
			{
				TCHAR* ModuleNameEnd = FCString::Strrchr(ModuleName, '\\');
				if (ModuleNameEnd != nullptr)
				{
					FMemory::Memmove(ModuleName, ModuleNameEnd + 1, (FCString::Strlen(ModuleNameEnd + 1) + 1) * sizeof(TCHAR));
				}

				TCHAR* ModuleNameExt = FCString::Strrchr(ModuleName, '.');
				if (ModuleNameExt != nullptr)
				{
					*ModuleNameExt = 0;
				}
			}
			else
			{
				const DWORD Err = GetLastError();
				FCString::Strcpy(ModuleName, TEXT("Unknown"));
			}

			uint64 BaseAddress = (uint64)SortedModuleHandles[ModuleIdx];
			uint64 Offset = ProgramCounters[Idx] - BaseAddress;
			OutStackFrames.Add(FCrashStackFrame(ModuleName, BaseAddress, Offset));
		}
	}
}

void FWindowsPlatformCrashContext::SetPortableCallStack(const uint64* StackTrace, int32 StackTraceDepth)
{
	FModuleHandleArray ProcessModuleHandles;
	GetProcModuleHandles(ProcessHandle, ProcessModuleHandles);
	ConvertProgramCountersToStackFrames(ProcessHandle, ProcessModuleHandles, StackTrace, StackTraceDepth, CallStack);
}

void FWindowsPlatformCrashContext::AddPlatformSpecificProperties() const
{
	AddCrashProperty(TEXT("PlatformIsRunningWindows"), 1);
	AddCrashProperty(TEXT("IsRunningOnBattery"), FPlatformMisc::IsRunningOnBattery());
}

bool FWindowsPlatformCrashContext::GetPlatformAllThreadContextsString(FString& OutStr) const
{
	for (const FThreadStackFrames& Thread : ThreadCallStacks)
	{
		AddThreadContextString(
			CrashedThreadId, 
			Thread.ThreadId, 
			Thread.ThreadName, 
			Thread.StackFrames,
			OutStr
		);
	}
	return !OutStr.IsEmpty();
}

void FWindowsPlatformCrashContext::AddThreadContextString(
	uint32 CrashedThreadId,
	uint32 ThreadId,
	const FString& ThreadName,
	const TArray<FCrashStackFrame>& StackFrames,
	FString& OutStr)
{
	OutStr += TEXT("<Thread>");
	{
		OutStr += TEXT("<CallStack>");

		int32 MaxModuleNameLen = 0;
		for (const FCrashStackFrame& StFrame : StackFrames)
		{
			MaxModuleNameLen = FMath::Max(MaxModuleNameLen, StFrame.ModuleName.Len());
		}

		FString CallstackStr;
		for (const FCrashStackFrame& StFrame : StackFrames)
		{
			CallstackStr += FString::Printf(TEXT("%-*s 0x%016x + %-8x"), MaxModuleNameLen + 1, *StFrame.ModuleName, StFrame.BaseAddress, StFrame.Offset);
			CallstackStr += LINE_TERMINATOR;
		}
		AppendEscapedXMLString(OutStr, *CallstackStr);
		OutStr += TEXT("</CallStack>");
		OutStr += LINE_TERMINATOR;
	}
	OutStr += FString::Printf(TEXT("<IsCrashed>%s</IsCrashed>"), ThreadId == CrashedThreadId ? TEXT("true") : TEXT("false"));
	OutStr += LINE_TERMINATOR;
	// TODO: do we need thread register states?
	OutStr += TEXT("<Registers></Registers>");
	OutStr += LINE_TERMINATOR;
	OutStr += FString::Printf(TEXT("<ThreadID>%d</ThreadID>"), ThreadId);
	OutStr += LINE_TERMINATOR;
	OutStr += FString::Printf(TEXT("<ThreadName>%s</ThreadName>"), *ThreadName);
	OutStr += LINE_TERMINATOR;
	OutStr += TEXT("</Thread>");
	OutStr += LINE_TERMINATOR;
}

void FWindowsPlatformCrashContext::AddPortableThreadCallStack(uint32 ThreadId, const TCHAR* ThreadName, const uint64* StackFrames, int32 NumStackFrames)
{
	FModuleHandleArray ProcModuleHandles;
	GetProcModuleHandles(ProcessHandle, ProcModuleHandles);

	FThreadStackFrames Thread;
	Thread.ThreadId = ThreadId;
	Thread.ThreadName = FString(ThreadName);
	ConvertProgramCountersToStackFrames(ProcessHandle, ProcModuleHandles, StackFrames, NumStackFrames, Thread.StackFrames);
	ThreadCallStacks.Push(Thread);
}

void FWindowsPlatformCrashContext::CopyPlatformSpecificFiles(const TCHAR* OutputDirectory, void* Context)
{
	FGenericCrashContext::CopyPlatformSpecificFiles(OutputDirectory, Context);

	// Save minidump
	LPEXCEPTION_POINTERS ExceptionInfo = (LPEXCEPTION_POINTERS)Context;
	if (ExceptionInfo != nullptr)
	{
		const FString MinidumpFileName = FPaths::Combine(OutputDirectory, FGenericCrashContext::UE4MinidumpName);
		WriteMinidump(ProcessHandle.Get(), CrashedThreadId, *this, *MinidumpFileName, ExceptionInfo);
	}

	// If present, include the crash video
	const FString CrashVideoPath = FPaths::ProjectLogDir() / TEXT("CrashVideo.avi");
	if (IFileManager::Get().FileExists(*CrashVideoPath))
	{
		FString CrashVideoFilename = FPaths::GetCleanFilename(CrashVideoPath);
		const FString CrashVideoDstAbsolute = FPaths::Combine(OutputDirectory, *CrashVideoFilename);
		static_cast<void>(IFileManager::Get().Copy(*CrashVideoDstAbsolute, *CrashVideoPath));	// best effort, so don't care about result: couldn't copy -> tough, no video
	}
}

void FWindowsPlatformCrashContext::CaptureAllThreadContexts()
{
	TArray<typename FThreadManager::FThreadStackBackTrace> StackTraces;
	FThreadManager::Get().GetAllThreadStackBackTraces(StackTraces);

	for (const FThreadManager::FThreadStackBackTrace& Thread : StackTraces)
	{
		AddPortableThreadCallStack(Thread.ThreadId, *Thread.ThreadName, Thread.ProgramCounters.GetData(), Thread.ProgramCounters.Num());
	}	
}


namespace
{

static int32 ReportCrashCallCount = 0;

static FORCEINLINE bool CreatePipeWrite(void*& ReadPipe, void*& WritePipe)
{
	SECURITY_ATTRIBUTES Attr = { sizeof(SECURITY_ATTRIBUTES), NULL, true };

	if (!::CreatePipe(&ReadPipe, &WritePipe, &Attr, 0))
	{
		return false;
	}

	if (!::SetHandleInformation(WritePipe, HANDLE_FLAG_INHERIT, 0))
	{
		return false;
	}

	return true;
}

/**
 * Finds the crash reporter binary path. Returns true if the file exists.
 */
bool CreateCrashReportClientPath(TCHAR* OutClientPath, int32 MaxLength)
{
	auto CreateCrashReportClientPathImpl = [&OutClientPath, MaxLength](const TCHAR* CrashReportClientExeName) -> bool
	{
		const TCHAR* EngineDir = FPlatformMisc::EngineDir();
		const TCHAR* BinariesDir = FPlatformProcess::GetBinariesSubdirectory();

		// Find the path to crash reporter binary. Avoid creating FStrings.
		*OutClientPath = 0;
		FCString::Strncat(OutClientPath, EngineDir, MaxLength);
		FCString::Strncat(OutClientPath, TEXT("Binaries/"), MaxLength);
		FCString::Strncat(OutClientPath, BinariesDir, MaxLength);
		FCString::Strncat(OutClientPath, TEXT("/"), MaxLength);
		FCString::Strncat(OutClientPath, CrashReportClientExeName, MaxLength);

		const DWORD Results = GetFileAttributesW(OutClientPath);
		return Results != INVALID_FILE_ATTRIBUTES;
	};

#if WITH_EDITOR
	const TCHAR CrashReportClientShippingName[] = TEXT("CrashReportClientEditor.exe");
	const TCHAR CrashReportClientDevelopmentName[] = TEXT("CrashReportClientEditor-Win64-Development.exe");
#else
	const TCHAR CrashReportClientShippingName[] = TEXT("CrashReportClient.exe");
	const TCHAR CrashReportClientDevelopmentName[] = TEXT("CrashReportClient-Win64-Development.exe");
#endif

	if (CreateCrashReportClientPathImpl(CrashReportClientShippingName))
	{
		return true;
	}

#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING)
	if (CreateCrashReportClientPathImpl(CrashReportClientDevelopmentName))
	{
		return true;
	}
#endif

	return false;
}

/**
 * Launches crash reporter client and creates the pipes for communication.
 */
FProcHandle LaunchCrashReportClient(void** OutWritePipe, void** OutReadPipe)
{
	TCHAR CrashReporterClientPath[CR_CLIENT_MAX_PATH_LEN] = { 0 };
	TCHAR CrashReporterClientArgs[CR_CLIENT_MAX_ARGS_LEN] = { 0 };

	void *PipeChildInRead, *PipeChildInWrite, *PipeChildOutRead, *PipeChildOutWrite;

	if (!CreatePipeWrite(PipeChildInRead, PipeChildInWrite) || !FPlatformProcess::CreatePipe(PipeChildOutRead, PipeChildOutWrite))
	{
		return FProcHandle();
	}

	// Pass the endpoints to the creator of the client ...
	*OutWritePipe = PipeChildInWrite;
	*OutReadPipe = PipeChildOutRead;
	
	// ... and the other ends to the child
	FCString::Sprintf(CrashReporterClientArgs, TEXT(" -READ=%0u -WRITE=%0u"), PipeChildInRead, PipeChildOutWrite);

	{
		TCHAR PidStr[256] = { 0 };
		FCString::Sprintf(PidStr, TEXT(" -MONITOR=%u"), FPlatformProcess::GetCurrentProcessId());
		FCString::Strncat(CrashReporterClientArgs, PidStr, CR_CLIENT_MAX_ARGS_LEN);
	}

#if WITH_EDITOR // Disaster recovery is only enabled for the Editor. Start the server even if in -game, -server, commandlet, the client-side will not connect (its too soon here to query this executable config).
	{
		// Disaster recovery service command line.
		FString DisasterRecoveryServiceCommandLine;
		DisasterRecoveryServiceCommandLine += FString::Printf(TEXT(" -ConcertServer=\"%s\""), *RecoveryService::GetRecoveryServerName()); // Must be in-sync with disaster recovery client module.

		FCString::Strncat(CrashReporterClientArgs, *DisasterRecoveryServiceCommandLine, CR_CLIENT_MAX_ARGS_LEN);
	}
#endif

	// Launch the crash reporter if the client exists
	if (CreateCrashReportClientPath(CrashReporterClientPath, CR_CLIENT_MAX_PATH_LEN))
	{
		return FPlatformProcess::CreateProc(
			CrashReporterClientPath,
			CrashReporterClientArgs,
			true, false, false,
			nullptr, 0,
			nullptr,
			nullptr,
			nullptr
		);
	}
	return FProcHandle();
}

/**
 * Enum indicating whether to run the crash reporter UI
 */
enum class EErrorReportUI
{
	/** Ask the user for a description */
	ShowDialog,

	/** Silently uploaded the report */
	ReportInUnattendedMode	
};

/**
 * Write required information about the crash to the shared context, and then signal the crash reporter client 
 * running in monitor mode about the crash.
 */
int32 ReportCrashForMonitor(
	LPEXCEPTION_POINTERS ExceptionInfo,
	ECrashContextType Type,
	const TCHAR* ErrorMessage,
	int NumStackFramesToIgnore,
	HANDLE CrashingThreadHandle,
	DWORD CrashingThreadId,
	FProcHandle& CrashMonitorHandle,
	FSharedCrashContext* SharedContext,
	void* WritePipe,
	void* ReadPipe,
	EErrorReportUI ReportUI)
{
	FGenericCrashContext::CopySharedCrashContext(*SharedContext);

	// Set the platform specific crash context, so that we can stack walk and minidump from
	// the crash reporter client.
	SharedContext->PlatformCrashContext = (void*)ExceptionInfo;

	// Setup up the shared memory area so that the crash report
	SharedContext->CrashType = Type;
	SharedContext->CrashingThreadId = CrashingThreadId;
	SharedContext->NumStackFramesToIgnore = NumStackFramesToIgnore;

	// Determine UI settings for the crash report. Suppress the user input dialog if we're running in unattended mode
	// Usage data controls if we want analytics in the crash report client
	// Finally we cannot call some of these functions if we crash during static init, so check if they are initialized.
	bool bNoDialog = ReportUI == EErrorReportUI::ReportInUnattendedMode || IsRunningDedicatedServer();
	bool bSendUnattendedBugReports = true;
	bool bSendUsageData = true;
	bool bCanSendCrashReport = true;
	// Some projects set this value in non editor builds to automatically send error reports unattended, but display
	// a plain message box in the crash report client. See CRC app code for details.
	bool bImplicitSend = false;

	if (FCommandLine::IsInitialized())
	{
		bNoDialog |= FApp::IsUnattended();
	}

	if (GConfig)
	{
		GConfig->GetBool(TEXT("/Script/UnrealEd.CrashReportsPrivacySettings"), TEXT("bSendUnattendedBugReports"), bSendUnattendedBugReports, GEditorSettingsIni);
		GConfig->GetBool(TEXT("/Script/UnrealEd.AnalyticsPrivacySettings"), TEXT("bSendUsageData"), bSendUsageData, GEditorSettingsIni);
		
#if !UE_EDITOR
		if (ReportUI != EErrorReportUI::ReportInUnattendedMode)
		{
			// Only check if we are in a non-editor build
			GConfig->GetBool(TEXT("CrashReportClient"), TEXT("bImplicitSend"), bImplicitSend, GEngineIni);
		}
#endif
	}

#if !UE_EDITOR
	if (BuildSettings::IsLicenseeVersion())
	{
		// do not send unattended reports in licensees' builds except for the editor, where it is governed by the above setting
		bSendUnattendedBugReports = false;
		bSendUsageData = false;
	}
#endif

	if (bNoDialog && !bSendUnattendedBugReports)
	{
		// If we shouldn't display a dialog (like for ensures) and the user
		// does not allow unattended bug reports we cannot send the report.
		bCanSendCrashReport = false;
	}

	if (!bCanSendCrashReport)
	{
		return EXCEPTION_CONTINUE_EXECUTION;
	}

	SharedContext->UserSettings.bNoDialog = bNoDialog;
	SharedContext->UserSettings.bSendUnattendedBugReports = bSendUnattendedBugReports;
	SharedContext->UserSettings.bSendUsageData = bSendUsageData;
	SharedContext->UserSettings.bImplicitSend = bImplicitSend;

	SharedContext->SessionContext.bIsExitRequested = IsEngineExitRequested();
	FCString::Strcpy(SharedContext->ErrorMessage, CR_MAX_ERROR_MESSAGE_CHARS-1, ErrorMessage);

	if (GLog)
	{
		GLog->PanicFlushThreadedLogs();
	}

	// Setup all the thread ids and names using snapshot dbghelp. Since it's not possible to 
	// query thread names from an external process.
	uint32 ThreadIdx = 0;
	const bool bThreadManagerAvailable = FThreadManager::IsInitialized();
	DWORD CurrentProcessId = GetCurrentProcessId();
	DWORD CurrentThreadId = GetCurrentThreadId();
	HANDLE ThreadSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	TArray<HANDLE, TInlineAllocator<CR_MAX_THREADS>> ThreadHandles;
	if (ThreadSnapshot != INVALID_HANDLE_VALUE)
	{
		THREADENTRY32 ThreadEntry;
		ThreadEntry.dwSize = sizeof(THREADENTRY32);
		if (Thread32First(ThreadSnapshot, &ThreadEntry))
		{
			do
			{
				if (ThreadEntry.th32OwnerProcessID == CurrentProcessId)
				{
					// Stop the thread (except current!). We will resume once the crash reporter is done.
					if (ThreadEntry.th32ThreadID != CurrentThreadId)
					{
						HANDLE ThreadHandle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, ThreadEntry.th32ThreadID);
						SuspendThread(ThreadHandle);
						ThreadHandles.Push(ThreadHandle);
					}

					SharedContext->ThreadIds[ThreadIdx] = ThreadEntry.th32ThreadID;
					const TCHAR* ThreadName = nullptr;
					if (ThreadEntry.th32ThreadID == GGameThreadId)
					{
						ThreadName = TEXT("GameThread");
					}
					else if (bThreadManagerAvailable)
					{
						const FString& TmThreadName = FThreadManager::Get().GetThreadName(ThreadEntry.th32ThreadID);
						ThreadName = TmThreadName.IsEmpty() ? TEXT("Unknown") : *TmThreadName;
					}
					else
					{
						ThreadName = TEXT("Unavailable");
					}
					FCString::Strcpy(
						&SharedContext->ThreadNames[ThreadIdx*CR_MAX_THREAD_NAME_CHARS],
						CR_MAX_THREAD_NAME_CHARS - 1,
						ThreadName
					);
					ThreadIdx++;
				}
			} while (Thread32Next(ThreadSnapshot, &ThreadEntry) && (ThreadIdx < CR_MAX_THREADS));
		}
	}
	SharedContext->NumThreads = ThreadIdx;
	CloseHandle(ThreadSnapshot);

	FString CrashDirectoryAbsolute;
	if (FGenericCrashContext::CreateCrashReportDirectory(SharedContext->SessionContext.CrashGUIDRoot, ReportCrashCallCount, CrashDirectoryAbsolute))
	{
		FCString::Strcpy(SharedContext->CrashFilesDirectory, *CrashDirectoryAbsolute);
		// Copy the log file to output
		FGenericCrashContext::DumpLog(CrashDirectoryAbsolute);
	}

	// Allow the monitor process to take window focus
	if (const DWORD MonitorProcessId = ::GetProcessId(CrashMonitorHandle.Get()))
	{
		::AllowSetForegroundWindow(MonitorProcessId);
	}

	// Write the shared context to the pipe
	int32 OutDataWritten = 0;
	FPlatformProcess::WritePipe(WritePipe, (UINT8*)SharedContext, sizeof(FSharedCrashContext), &OutDataWritten);
	check(OutDataWritten == sizeof(FSharedCrashContext));

	// Wait for a response, saying it's ok to continue
	bool bCanContinueExecution = false;
	int32 ExitCode = 0;
	// Would like to use TInlineAllocator here to avoid heap allocation on crashes, but it doesn't work since ReadPipeToArray 
	// cannot take array with non-default allocator
	TArray<uint8> ResponseBuffer;
	ResponseBuffer.AddZeroed(16);
	while (!FPlatformProcess::GetProcReturnCode(CrashMonitorHandle, &ExitCode) && !bCanContinueExecution)
	{
		if (FPlatformProcess::ReadPipeToArray(ReadPipe, ResponseBuffer))
		{
			if (ResponseBuffer[0] == 0xd && ResponseBuffer[1] == 0xe &&
				ResponseBuffer[2] == 0xa && ResponseBuffer[3] == 0xd)
			{
				bCanContinueExecution = true;
			}
		}
	}

	for (HANDLE ThreadHandle : ThreadHandles)
	{
		ResumeThread(ThreadHandle);
		CloseHandle(ThreadHandle);
	}

	return EXCEPTION_CONTINUE_EXECUTION;
}

/** 
 * Create a crash report, add the user log and video, and save them a unique the crash folder
 * Launch CrashReportClient.exe to read the report and upload to our CR pipeline
 */
int32 ReportCrashUsingCrashReportClient(FWindowsPlatformCrashContext& InContext, EXCEPTION_POINTERS* ExceptionInfo, EErrorReportUI ReportUI)
{
	// Prevent CrashReportClient from spawning another CrashReportClient.
	const TCHAR* ExecutableName = FPlatformProcess::ExecutableName();
	bool bCanRunCrashReportClient = FCString::Stristr( ExecutableName, TEXT( "CrashReportClient" ) ) == nullptr;

	// Suppress the user input dialog if we're running in unattended mode
	bool bNoDialog = FApp::IsUnattended() || ReportUI == EErrorReportUI::ReportInUnattendedMode || IsRunningDedicatedServer();

	bool bImplicitSend = false;
#if !UE_EDITOR
	if (GConfig && ReportUI != EErrorReportUI::ReportInUnattendedMode)
	{
		// Only check if we are in a non-editor build
		GConfig->GetBool(TEXT("CrashReportClient"), TEXT("bImplicitSend"), bImplicitSend, GEngineIni);
	}
#endif

	bool bSendUnattendedBugReports = true;
	if (GConfig)
	{
		GConfig->GetBool(TEXT("/Script/UnrealEd.CrashReportsPrivacySettings"), TEXT("bSendUnattendedBugReports"), bSendUnattendedBugReports, GEditorSettingsIni);
	}

	// Controls if we want analytics in the crash report client
	bool bSendUsageData = true;
	if (GConfig)
	{
		GConfig->GetBool(TEXT("/Script/UnrealEd.AnalyticsPrivacySettings"), TEXT("bSendUsageData"), bSendUsageData, GEditorSettingsIni);
	}

#if !UE_EDITOR
	if (BuildSettings::IsLicenseeVersion())
	{
		// do not send unattended reports in licensees' builds except for the editor, where it is governed by the above setting
		bSendUnattendedBugReports = false;
		bSendUsageData = false;
	}
#endif

	if (bNoDialog && !bSendUnattendedBugReports)
	{
		bCanRunCrashReportClient = false;
	}

	if( bCanRunCrashReportClient )
	{
		TCHAR CrashReporterClientPath[CR_CLIENT_MAX_PATH_LEN] = { 0 };
		bool bCrashReporterRan = false;

		// Generate Crash GUID
		TCHAR CrashGUID[FGenericCrashContext::CrashGUIDLength];
		InContext.GetUniqueCrashName(CrashGUID, FGenericCrashContext::CrashGUIDLength);
		const FString AppName = InContext.GetCrashGameName();

		FString CrashFolder = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("Crashes"), CrashGUID);
		FString CrashFolderAbsolute = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*CrashFolder);
		if (IFileManager::Get().MakeDirectory(*CrashFolderAbsolute, true))
		{
			// Save crash context
			const FString CrashContextXMLPath = FPaths::Combine(*CrashFolderAbsolute, FPlatformCrashContext::CrashContextRuntimeXMLNameW);
			InContext.SerializeAsXML(*CrashContextXMLPath);

			// Copy platform specific files (e.g. minidump) to output directory
			InContext.CopyPlatformSpecificFiles(*CrashFolderAbsolute, (void*) ExceptionInfo);

			// Copy the log file to output
			GLog->PanicFlushThreadedLogs();
			FGenericCrashContext::DumpLog(CrashFolderAbsolute);

			// Build machines do not upload these automatically since it is not okay to have lingering processes after the build completes.
			if (GIsBuildMachine)
			{
				return EXCEPTION_CONTINUE_EXECUTION;
			}

			// Run Crash Report Client
			FString CrashReportClientArguments = FString::Printf(TEXT("\"%s\""), *CrashFolderAbsolute);

			// If the editor setting has been disabled to not send analytics extend this to the CRC
			if (!bSendUsageData)
			{
				CrashReportClientArguments += TEXT(" -NoAnalytics ");
			}

			// Pass nullrhi to CRC when the engine is in this mode to stop the CRC attempting to initialize RHI when the capability isn't available
			bool bNullRHI = !FApp::CanEverRender();

			if (bImplicitSend)
			{
				CrashReportClientArguments += TEXT(" -Unattended -ImplicitSend");
			}
			else if (bNoDialog || bNullRHI)
			{
				CrashReportClientArguments += TEXT(" -Unattended");
			}

			if (bNullRHI)
			{
				CrashReportClientArguments += TEXT(" -nullrhi");
			}

			CrashReportClientArguments += FString(TEXT(" -AppName=")) + AppName;
			CrashReportClientArguments += FString(TEXT(" -CrashGUID=")) + CrashGUID;

			const FString DownstreamStorage = FWindowsPlatformStackWalk::GetDownstreamStorage();
			if (!DownstreamStorage.IsEmpty())
			{
				CrashReportClientArguments += FString(TEXT(" -DebugSymbols=")) + DownstreamStorage;
			}

			// CrashReportClient.exe should run without dragging in binaries from an inherited dll directory
			// So, get the current dll directory for restore and clear before creating process
			TCHAR* CurrentDllDirectory = nullptr;
			DWORD BufferSize = (GetDllDirectory(0, nullptr) + 1) * sizeof(TCHAR);
			
			if (BufferSize > 0)
			{
				CurrentDllDirectory = (TCHAR*) FMemory::Malloc(BufferSize);
				if (CurrentDllDirectory)
				{
					FMemory::Memset(CurrentDllDirectory, 0, BufferSize);
					GetDllDirectory(BufferSize, CurrentDllDirectory);
					SetDllDirectory(nullptr);
				}
			}

			FString AbsCrashReportClientLog;
			if (FParse::Value(FCommandLine::Get(), TEXT("AbsCrashReportClientLog="), AbsCrashReportClientLog))
			{
				CrashReportClientArguments += FString::Format(TEXT(" -abslog=\"{0}\""), { AbsCrashReportClientLog });
			}

			if (CreateCrashReportClientPath(CrashReporterClientPath, CR_CLIENT_MAX_PATH_LEN))
			{
				bCrashReporterRan = FPlatformProcess::CreateProc(CrashReporterClientPath, *CrashReportClientArguments, true, false, false, NULL, 0, NULL, NULL).IsValid();
			}

			// Restore the dll directory
			if (CurrentDllDirectory)
			{
				SetDllDirectory(CurrentDllDirectory);
				FMemory::Free(CurrentDllDirectory);
			}
		}

		if (!bCrashReporterRan && !bNoDialog)
		{
			UE_LOG(LogWindows, Log, TEXT("Could not start crash report client using %s"), CrashReporterClientPath);
			FPlatformMemory::DumpStats(*GWarn);
			FText MessageTitle(FText::Format(
				NSLOCTEXT("MessageDialog", "AppHasCrashed", "The {0} {1} has crashed and will close"),
				FText::FromString(AppName),
				FText::FromString(FPlatformMisc::GetEngineMode())
				));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(GErrorHist), &MessageTitle);
		}
	}

	// Let the system take back over (return value only used by ReportEnsure)
	return EXCEPTION_CONTINUE_EXECUTION;
}

} // end anonymous namespace


#include "Windows/HideWindowsPlatformTypes.h"

// Original code below

#include "Windows/AllowWindowsPlatformTypes.h"
	#include <ErrorRep.h>
	#include <DbgHelp.h>
#include "Windows/HideWindowsPlatformTypes.h"

#pragma comment(lib, "Faultrep.lib")

/** 
 * Creates an info string describing the given exception record.
 * See MSDN docs on EXCEPTION_RECORD.
 */
#include "Windows/AllowWindowsPlatformTypes.h"
void CreateExceptionInfoString(EXCEPTION_RECORD* ExceptionRecord)
{
	// #CrashReport: 2014-08-18 Fix FString usage?
	FString ErrorString = TEXT("Unhandled Exception: ");

#define HANDLE_CASE(x) case x: ErrorString += TEXT(#x); break;

	switch (ExceptionRecord->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		ErrorString += TEXT("EXCEPTION_ACCESS_VIOLATION ");
		if (ExceptionRecord->ExceptionInformation[0] == 0)
		{
			ErrorString += TEXT("reading address ");
		}
		else if (ExceptionRecord->ExceptionInformation[0] == 1)
		{
			ErrorString += TEXT("writing address ");
		}
		ErrorString += FString::Printf(TEXT("0x%08x"), (uint32)ExceptionRecord->ExceptionInformation[1]);
		break;
	HANDLE_CASE(EXCEPTION_ARRAY_BOUNDS_EXCEEDED)
	HANDLE_CASE(EXCEPTION_DATATYPE_MISALIGNMENT)
	HANDLE_CASE(EXCEPTION_FLT_DENORMAL_OPERAND)
	HANDLE_CASE(EXCEPTION_FLT_DIVIDE_BY_ZERO)
	HANDLE_CASE(EXCEPTION_FLT_INVALID_OPERATION)
	HANDLE_CASE(EXCEPTION_ILLEGAL_INSTRUCTION)
	HANDLE_CASE(EXCEPTION_INT_DIVIDE_BY_ZERO)
	HANDLE_CASE(EXCEPTION_PRIV_INSTRUCTION)
	HANDLE_CASE(EXCEPTION_STACK_OVERFLOW)
	default:
		ErrorString += FString::Printf(TEXT("0x%08x"), (uint32)ExceptionRecord->ExceptionCode);
	}

	FCString::Strncpy(GErrorExceptionDescription, *ErrorString, UE_ARRAY_COUNT(GErrorExceptionDescription));

#undef HANDLE_CASE
}




/** 
 * Crash reporting thread. 
 * We process all the crashes on a separate thread in case the original thread's stack is corrupted (stack overflow etc).
 * We're using low level API functions here because at the time we initialize the thread, nothing in the engine exists yet.
 **/
class FCrashReportingThread
{
private:
	/** Thread Id of reporter thread*/
	DWORD ThreadId;
	/** Thread handle to reporter thread */
	HANDLE Thread;
	/** Stops this thread */
	FThreadSafeCounter StopTaskCounter;
	/** Signals that the game has crashed */
	HANDLE CrashEvent;
	/** Event that signals the crash reporting thread has finished processing the crash */
	HANDLE CrashHandledEvent;

	/** Exception information */
	LPEXCEPTION_POINTERS ExceptionInfo;
	/** ThreadId of the crashed thread */
	DWORD CrashingThreadId;
	/** Handle to crashed thread.*/
	HANDLE CrashingThreadHandle;
	/** Handle used to remove vectored exception handler. */
	HANDLE VectoredExceptionHandle;

	/** Process handle to crash reporter client */
	FProcHandle CrashClientHandle;
	/** Pipe for writing to the monitor process. */
	void* CrashMonitorWritePipe;
	/** Pipe for reading from the monitor process. */
	void* CrashMonitorReadPipe;
	/** Memory allocated for crash context. */
	FSharedCrashContext SharedContext;
	

	/** Thread main proc */
	static DWORD STDCALL CrashReportingThreadProc(LPVOID pThis)
	{
		FCrashReportingThread* This = (FCrashReportingThread*)pThis;
		return This->Run();
	}

	/** Main loop that waits for a crash to trigger the report generation */
	FORCENOINLINE uint32 Run()
	{
		// Removed vectored exception handler, we are now guaranteed to
		// be able to catch unhandled exception in the main try/catch block.
#if _WIN32_WINNT >= 0x0500
		if (!FPlatformMisc::IsDebuggerPresent())
		{
			RemoveVectoredExceptionHandler(VectoredExceptionHandle);
		}
#endif
		while (StopTaskCounter.GetValue() == 0)
		{
			if (WaitForSingleObject(CrashEvent, 500) == WAIT_OBJECT_0)
			{
				ResetEvent(CrashHandledEvent);
				HandleCrashInternal();
				ResetEvent(CrashEvent);
				// Let the thread that crashed know we're done.				
				SetEvent(CrashHandledEvent);
				break;
			}
		}
		return 0;
	}

	/** Called by the destructor to terminate the thread */
	void Stop()
	{
		StopTaskCounter.Increment();
	}

public:
		
	FCrashReportingThread()
		: ThreadId(0)
		, Thread(nullptr)
		, CrashEvent(nullptr)
		, CrashHandledEvent(nullptr)
		, ExceptionInfo(nullptr)
		, CrashingThreadId(0)
		, CrashingThreadHandle(nullptr)
		, VectoredExceptionHandle(INVALID_HANDLE_VALUE)
		, CrashMonitorWritePipe(nullptr)
		, CrashMonitorReadPipe(nullptr)
	{
		// Synchronization objects
		CrashEvent = CreateEvent(nullptr, true, 0, nullptr);
		CrashHandledEvent = CreateEvent(nullptr, true, 0, nullptr);

		// Add an exception handler to catch issues during static initialization. This
		// is removed once the crash reporter thread is started.
#if _WIN32_WINNT >= 0x0500
		if (!FPlatformMisc::IsDebuggerPresent())
		{
			VectoredExceptionHandle = AddVectoredExceptionHandler(1, UnhandledStaticInitException);
		}
#endif

#if USE_CRASH_REPORTER_MONITOR
		CrashClientHandle = LaunchCrashReportClient(&CrashMonitorWritePipe, &CrashMonitorReadPipe);
		FMemory::Memzero(SharedContext);
#endif

		// Create a background thread that will process the crash and generate crash reports
		Thread = CreateThread(NULL, 0, CrashReportingThreadProc, this, 0, &ThreadId);
		if (Thread)
		{
			SetThreadPriority(Thread, THREAD_PRIORITY_BELOW_NORMAL);
		}

		FGenericCrashContext::SetIsOutOfProcessCrashReporter(CrashClientHandle.IsValid());
	}

	FORCENOINLINE ~FCrashReportingThread()
	{
		if (Thread)
		{
			// Stop the crash reporting thread
			Stop();
			// 1s should be enough for the thread to exit, otherwise don't bother with cleanup
			if (WaitForSingleObject(Thread, 1000) == WAIT_OBJECT_0)
			{
				CloseHandle(Thread);
			}
			Thread = nullptr;
		}

		CloseHandle(CrashEvent);
		CrashEvent = nullptr;

		CloseHandle(CrashHandledEvent);
		CrashHandledEvent = nullptr;

	}

	/** Ensures are passed trough this. */
	FORCEINLINE int32 OnEnsure(LPEXCEPTION_POINTERS InExceptionInfo, int NumStackFramesToIgnore, const TCHAR* ErrorMessage, EErrorReportUI ReportUI)
	{
		if (CrashClientHandle.IsValid() && FPlatformProcess::IsProcRunning(CrashClientHandle))
		{
			return ReportCrashForMonitor(
				InExceptionInfo, 
				ECrashContextType::Ensure, 
				ErrorMessage, 
				NumStackFramesToIgnore, 
				GetCurrentThread(), 
				GetCurrentThreadId(), 
				CrashClientHandle, 
				&SharedContext, 
				CrashMonitorWritePipe, 
				CrashMonitorReadPipe,
				ReportUI
			);
		}
		else
		{
			FWindowsPlatformCrashContext CrashContext(ECrashContextType::Ensure, ErrorMessage);
			CrashContext.SetCrashedProcess(FProcHandle(::GetCurrentProcess()));
			void* ContextWrapper = FWindowsPlatformStackWalk::MakeThreadContextWrapper(InExceptionInfo->ContextRecord, GetCurrentThread());
			CrashContext.CapturePortableCallStack(NumStackFramesToIgnore, ContextWrapper);

			return ReportCrashUsingCrashReportClient(CrashContext, InExceptionInfo, ReportUI);
		}
	}

	/** The thread that crashed calls this function which will trigger the CR thread to report the crash */
	FORCEINLINE void OnCrashed(LPEXCEPTION_POINTERS InExceptionInfo)
	{
		ExceptionInfo = InExceptionInfo;
		CrashingThreadId = GetCurrentThreadId();
		CrashingThreadHandle = GetCurrentThread();
		SetEvent(CrashEvent);
	}

	/** The thread that crashed calls this function to wait for the report to be generated */
	FORCEINLINE bool WaitUntilCrashIsHandled()
	{
		// Wait 60s, it's more than enough to generate crash report. We don't want to stall forever otherwise.
		return WaitForSingleObject(CrashHandledEvent, 60000) == WAIT_OBJECT_0;
	}

	/** Crashes during static init should be reported directly to crash monitor. */
	FORCEINLINE int32 OnCrashDuringStaticInit(LPEXCEPTION_POINTERS InExceptionInfo)
	{
		if (CrashClientHandle.IsValid() && FPlatformProcess::IsProcRunning(CrashClientHandle))
		{
			const ECrashContextType Type = ECrashContextType::Crash;
			const int NumStackFramesToIgnore = 1;
			const TCHAR* ErrorMessage = TEXT("Crash during static initialization");

			if (!FPlatformCrashContext::IsInitalized())
			{
				FPlatformCrashContext::Initialize();
			}

			return ReportCrashForMonitor(
				InExceptionInfo,
				Type,
				ErrorMessage,
				NumStackFramesToIgnore,
				CrashingThreadHandle,
				CrashingThreadId,
				CrashClientHandle,
				&SharedContext,
				CrashMonitorWritePipe,
				CrashMonitorReadPipe,
				EErrorReportUI::ShowDialog
			);
		}

		return EXCEPTION_CONTINUE_EXECUTION;
	}

private:

	/** Handles the crash */
	FORCENOINLINE void HandleCrashInternal()
	{
		// Stop the heartbeat thread so that it doesn't interfere with crashreporting
		FThreadHeartBeat::Get().Stop();

		GLog->PanicFlushThreadedLogs();

		// Then try run time crash processing and broadcast information about a crash.
		FCoreDelegates::OnHandleSystemError.Broadcast();

		if (GLog)
		{
			GLog->PanicFlushThreadedLogs();
		}
		
		// Get the default settings for the crash context
		ECrashContextType Type = ECrashContextType::Crash;
		const TCHAR* ErrorMessage = TEXT("Unhandled exception");
		int NumStackFramesToIgnore = 2;

		void* ContextWrapper = nullptr;

		// If it was an assert or GPU crash, allow overriding the info from the exception parameters
		if (ExceptionInfo->ExceptionRecord->ExceptionCode == AssertExceptionCode && ExceptionInfo->ExceptionRecord->NumberParameters == 1)
		{
			const FAssertInfo& Info = *(const FAssertInfo*)ExceptionInfo->ExceptionRecord->ExceptionInformation[0];
			Type = ECrashContextType::Assert;
			ErrorMessage = Info.ErrorMessage;
			NumStackFramesToIgnore += Info.NumStackFramesToIgnore;
		}
		else if (ExceptionInfo->ExceptionRecord->ExceptionCode == GPUCrashExceptionCode && ExceptionInfo->ExceptionRecord->NumberParameters == 1)
		{
			const FAssertInfo& Info = *(const FAssertInfo*)ExceptionInfo->ExceptionRecord->ExceptionInformation[0];
			Type = ECrashContextType::GPUCrash;
			ErrorMessage = Info.ErrorMessage;
			NumStackFramesToIgnore += Info.NumStackFramesToIgnore;
		}
		// Generic exception description is stored in GErrorExceptionDescription
		else if (ExceptionInfo->ExceptionRecord->ExceptionCode != 1)
		{
			// When a generic exception is thrown, it is important to get all the stack frames
			NumStackFramesToIgnore = 0;
			CreateExceptionInfoString(ExceptionInfo->ExceptionRecord);
			ErrorMessage = GErrorExceptionDescription;
		}

#if USE_CRASH_REPORTER_MONITOR
		if (CrashClientHandle.IsValid() && FPlatformProcess::IsProcRunning(CrashClientHandle))
		{
			// If possible use the crash monitor helper class to report the error. This will do most of the analysis
			// in the crash reporter client process.
			ReportCrashForMonitor(
				ExceptionInfo,
				Type,
				ErrorMessage,
				NumStackFramesToIgnore,
				CrashingThreadHandle,
				CrashingThreadId,
				CrashClientHandle,
				&SharedContext,
				CrashMonitorWritePipe,
				CrashMonitorReadPipe,
				EErrorReportUI::ShowDialog
			);
		}
		else
#endif
		{
			// Not super safe due to dynamic memory allocations, but at least enables new functionality.
			// Introduces a new runtime crash context. Will replace all Windows related crash reporting.
			FWindowsPlatformCrashContext CrashContext(Type, ErrorMessage);

			// Thread context wrapper for stack operations
			ContextWrapper = FWindowsPlatformStackWalk::MakeThreadContextWrapper(ExceptionInfo->ContextRecord, CrashingThreadHandle);
			CrashContext.SetCrashedProcess(FProcHandle(::GetCurrentProcess()));
			CrashContext.CapturePortableCallStack(NumStackFramesToIgnore, ContextWrapper);
			CrashContext.SetCrashedThreadId(CrashingThreadId);
			CrashContext.CaptureAllThreadContexts();

			// Also mark the same number of frames to be ignored if we symbolicate from the minidump
			CrashContext.SetNumMinidumpFramesToIgnore(NumStackFramesToIgnore);

			// First launch the crash reporter client.
#if WINVER > 0x502	// Windows Error Reporting is not supported on Windows XP
			if (GUseCrashReportClient)
			{
				ReportCrashUsingCrashReportClient(CrashContext, ExceptionInfo, EErrorReportUI::ShowDialog);
			}
			else
#endif		// WINVER
			{
				CrashContext.SerializeContentToBuffer();
				WriteMinidump(GetCurrentProcess(), GetCurrentThreadId(), CrashContext, MiniDumpFilenameW, ExceptionInfo);
			}
		}

		const bool bGenerateRuntimeCallstack =
#if UE_LOG_CRASH_CALLSTACK
			true;
#else
			FParse::Param(FCommandLine::Get(), TEXT("ForceLogCallstacks")) || FEngineBuildSettings::IsInternalBuild() || FEngineBuildSettings::IsPerforceBuild() || FEngineBuildSettings::IsSourceDistribution();
#endif // UE_LOG_CRASH_CALLSTACK
		if (bGenerateRuntimeCallstack)
		{
			const SIZE_T StackTraceSize = 65535;
			ANSICHAR* StackTrace = (ANSICHAR*)GMalloc->Malloc(StackTraceSize);
			StackTrace[0] = 0;
			// Walk the stack and dump it to the allocated memory. This process usually allocates a lot of memory.
			if (!ContextWrapper)
			{
				ContextWrapper = FWindowsPlatformStackWalk::MakeThreadContextWrapper(ExceptionInfo->ContextRecord, CrashingThreadHandle);
			}
			
			FPlatformStackWalk::StackWalkAndDump(StackTrace, StackTraceSize, 0, ContextWrapper);
			
			if (ExceptionInfo->ExceptionRecord->ExceptionCode != 1 && ExceptionInfo->ExceptionRecord->ExceptionCode != AssertExceptionCode)
			{
				CreateExceptionInfoString(ExceptionInfo->ExceptionRecord);
				FCString::Strncat(GErrorHist, GErrorExceptionDescription, UE_ARRAY_COUNT(GErrorHist));
				FCString::Strncat(GErrorHist, TEXT("\r\n\r\n"), UE_ARRAY_COUNT(GErrorHist));
			}

			FCString::Strncat(GErrorHist, ANSI_TO_TCHAR(StackTrace), UE_ARRAY_COUNT(GErrorHist));

			GMalloc->Free(StackTrace);
		}

		// Make sure any thread context wrapper is released
		if (ContextWrapper)
		{
			FWindowsPlatformStackWalk::ReleaseThreadContextWrapper(ContextWrapper);
		}

#if !UE_BUILD_SHIPPING
		FPlatformStackWalk::UploadLocalSymbols();
#endif
	}

};

#include "Windows/HideWindowsPlatformTypes.h"

#ifndef NOINITCRASHREPORTER
#define NOINITCRASHREPORTER 0
#endif

#if !NOINITCRASHREPORTER
TOptional<FCrashReportingThread> GCrashReportingThread(InPlace);
#endif


LONG WINAPI UnhandledStaticInitException(LPEXCEPTION_POINTERS ExceptionInfo)
{
#if !NOINITCRASHREPORTER
	// Top bit in exception code is fatal exceptions. Report those but not other types.
	if ((ExceptionInfo->ExceptionRecord->ExceptionCode & 0x80000000L) != 0)
	{
		// If we get an exception during static init we hope that the crash reporting thread
		// object has been created, otherwise we cannot handle the exception. This will hopefully 
		// work even if there is a stack overflow. See 
		// https://peteronprogramming.wordpress.com/2016/08/10/crashes-you-cant-handle-easily-2-stack-overflows-on-windows/
		// @note: Even if the object has been created, the actual thread has not been started yet,
		// (that happens after static init) so we must bypass that and report directly from this thread. 
		// 
		if (GCrashReportingThread.IsSet())
		{
			return GCrashReportingThread->OnCrashDuringStaticInit(ExceptionInfo);
		}
	}
#endif
	
	return EXCEPTION_CONTINUE_SEARCH;
}

/**
 * Fallback for catching exceptions which aren't caught elsewhere. This allows catching exceptions on threads created outside the engine.
 * Note that Windows does not call this handler if a debugger is attached, separately to our internal logic around crash handling.
 */
LONG WINAPI UnhandledException(EXCEPTION_POINTERS *ExceptionInfo)
{
	ReportCrash(ExceptionInfo);
	GIsCriticalError = true;
	FPlatformMisc::RequestExit(true);
	return EXCEPTION_CONTINUE_SEARCH;
}

// #CrashReport: 2015-05-28 This should be named EngineCrashHandler
int32 ReportCrash( LPEXCEPTION_POINTERS ExceptionInfo )
{
#if !NOINITCRASHREPORTER
	// Only create a minidump the first time this function is called.
	// (Can be called the first time from the RenderThread, then a second time from the MainThread.)
	if (GCrashReportingThread)
	{
		if (FPlatformAtomics::InterlockedIncrement(&ReportCrashCallCount) == 1)
		{
			GCrashReportingThread->OnCrashed(ExceptionInfo);
		}

		// Wait 60s for the crash reporting thread to process the message
		GCrashReportingThread->WaitUntilCrashIsHandled();
	}
#endif

	return EXCEPTION_EXECUTE_HANDLER;
}

static FCriticalSection EnsureLock;
static bool bReentranceGuard = false;

#if WINVER > 0x502	// Windows Error Reporting is not supported on Windows XP
/**
 * A wrapper for ReportCrashUsingCrashReportClient that creates a new ensure crash context
 */
int32 ReportEnsureUsingCrashReportClient(EXCEPTION_POINTERS* ExceptionInfo, int NumStackFramesToIgnore, const TCHAR* ErrorMessage, EErrorReportUI ReportUI)
{
#if !NOINITCRASHREPORTER
	return GCrashReportingThread->OnEnsure(ExceptionInfo, NumStackFramesToIgnore, ErrorMessage, ReportUI);
#else 
	return EXCEPTION_EXECUTE_HANDLER;
#endif
}
#endif

FORCENOINLINE void ReportEnsureInner(const TCHAR* ErrorMessage, int NumStackFramesToIgnore)
{
	// Skip this frame and the ::RaiseException call itself
	NumStackFramesToIgnore += 2;

	/** This is the last place to gather memory stats before exception. */
	FGenericCrashContext::SetMemoryStats(FPlatformMemory::GetStats());

#if WINVER > 0x502	// Windows Error Reporting is not supported on Windows XP
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
	__try
#endif
	{
		::RaiseException(1, 0, 0, nullptr);
	}
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
	__except (ReportEnsureUsingCrashReportClient( GetExceptionInformation(), NumStackFramesToIgnore, ErrorMessage, IsInteractiveEnsureMode() ? EErrorReportUI::ShowDialog : EErrorReportUI::ReportInUnattendedMode))
		CA_SUPPRESS(6322)
	{
	}
#endif
#endif	// WINVER
}

FORCENOINLINE void ReportAssert(const TCHAR* ErrorMessage, int NumStackFramesToIgnore)
{
	/** This is the last place to gather memory stats before exception. */
	FGenericCrashContext::SetMemoryStats(FPlatformMemory::GetStats());

	FAssertInfo Info(ErrorMessage, NumStackFramesToIgnore + 2); // +2 for this function and RaiseException()

	ULONG_PTR Arguments[] = { (ULONG_PTR)&Info };
	::RaiseException(AssertExceptionCode, 0, UE_ARRAY_COUNT(Arguments), Arguments);
}

FORCENOINLINE void ReportGPUCrash(const TCHAR* ErrorMessage, int NumStackFramesToIgnore)
{
	/** This is the last place to gather memory stats before exception. */
	FGenericCrashContext::SetMemoryStats(FPlatformMemory::GetStats());

	FAssertInfo Info(ErrorMessage, NumStackFramesToIgnore + 2); // +2 for this function and RaiseException()

	ULONG_PTR Arguments[] = { (ULONG_PTR)&Info };
	::RaiseException(GPUCrashExceptionCode, 0, UE_ARRAY_COUNT(Arguments), Arguments);
}

void ReportHang(const TCHAR* ErrorMessage, const uint64* StackFrames, int32 NumStackFrames, uint32 HungThreadId)
{
	if (ReportCrashCallCount > 0 || FDebug::HasAsserted())
	{
		// Don't report ensures after we've crashed/asserted, they simply may be a result of the crash as
		// the engine is already in a bad state.
		return;
	}

	FWindowsPlatformCrashContext CrashContext(ECrashContextType::Hang, ErrorMessage);
	CrashContext.SetCrashedProcess(FProcHandle(::GetCurrentProcess()));
	CrashContext.SetCrashedThreadId(HungThreadId);
	CrashContext.SetPortableCallStack(StackFrames, NumStackFrames);
	CrashContext.CaptureAllThreadContexts();

	EErrorReportUI ReportUI = IsInteractiveEnsureMode() ? EErrorReportUI::ShowDialog : EErrorReportUI::ReportInUnattendedMode;
	ReportCrashUsingCrashReportClient(CrashContext, nullptr, ReportUI);
}

// #CrashReport: 2015-05-28 This should be named EngineEnsureHandler
/**
 * Report an ensure to the crash reporting system
 */
FORCENOINLINE void ReportEnsure(const TCHAR* ErrorMessage, int NumStackFramesToIgnore)
{
	if (ReportCrashCallCount > 0 || FDebug::HasAsserted())
	{
		// Don't report ensures after we've crashed/asserted, they simply may be a result of the crash as
		// the engine is already in a bad state.
		return;
	}

	// Simple re-entrance guard.
	EnsureLock.Lock();

	if (bReentranceGuard)
	{
		EnsureLock.Unlock();
		return;
	}

	// Stop checking heartbeat for this thread (and stop the gamethread hitch detector if we're the game thread).
	// Ensure can take a lot of time (when stackwalking), so we don't want hitches/hangs firing.
	// These are no-ops on threads that didn't already have a heartbeat etc.
	FSlowHeartBeatScope SuspendHeartBeat(true);
	FDisableHitchDetectorScope SuspendGameThreadHitch;

	bReentranceGuard = true;

	ReportEnsureInner(ErrorMessage, NumStackFramesToIgnore + 1);

	bReentranceGuard = false;
	EnsureLock.Unlock();
}

#if !IS_PROGRAM && 0
namespace {
	/** Utility class to test crashes during static initialization. */
	struct StaticInitCrasher
	{
		StaticInitCrasher()
		{
			// Test stack overflow
			//StackOverlowMe(0);

			// Test GPF
			//int* i = nullptr;
			//*i = 3;

			// Check assert (shouldn't work during static init)
			//check(false);
		}

		void StackOverlowMe(uint32 s) {
			uint32 buffer[1024];
			memset(&buffer, 0xdeadbeef, 1024);
			(void*)buffer;
			if (s == 0xffffffff)
				return;
			StackOverlowMe(s + 1);
		}
	};

	static StaticInitCrasher GCrasher;
}
#endif



