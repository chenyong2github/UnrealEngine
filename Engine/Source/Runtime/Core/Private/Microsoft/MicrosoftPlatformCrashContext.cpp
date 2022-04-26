// Copyright Epic Games, Inc. All Rights Reserved.

#include "Microsoft/MicrosoftPlatformCrashContext.h"
#include "HAL/ThreadManager.h"

#include "Microsoft/AllowMicrosoftPlatformTypes.h"
	#include <psapi.h>
#include "Microsoft/HideMicrosoftPlatformTypes.h"

void FMicrosoftPlatformCrashContext::CaptureAllThreadContexts()
{
	TArray<typename FThreadManager::FThreadStackBackTrace> StackTraces;
	FThreadManager::Get().GetAllThreadStackBackTraces(StackTraces);

	for (const FThreadManager::FThreadStackBackTrace& Thread : StackTraces)
	{
		AddPortableThreadCallStack(Thread.ThreadId, *Thread.ThreadName, Thread.ProgramCounters.GetData(), Thread.ProgramCounters.Num());
	}
}

void FMicrosoftPlatformCrashContext::AddPortableThreadCallStack(uint32 ThreadId, const TCHAR* ThreadName, const uint64* StackFrames, int32 NumStackFrames)
{
	FModuleHandleArray ProcModuleHandles;
	GetProcModuleHandles(ProcessHandle, ProcModuleHandles);

	FThreadStackFrames Thread;
	Thread.ThreadId = ThreadId;
	Thread.ThreadName = FString(ThreadName);
	ConvertProgramCountersToStackFrames(ProcessHandle, ProcModuleHandles, StackFrames, NumStackFrames, Thread.StackFrames);
	ThreadCallStacks.Push(Thread);
}

void FMicrosoftPlatformCrashContext::SetPortableCallStack(const uint64* StackTrace, int32 StackTraceDepth)
{
	FModuleHandleArray ProcessModuleHandles;
	GetProcModuleHandles(ProcessHandle, ProcessModuleHandles);
	ConvertProgramCountersToStackFrames(ProcessHandle, ProcessModuleHandles, StackTrace, StackTraceDepth, CallStack);
}

void FMicrosoftPlatformCrashContext::GetProcModuleHandles(const FProcHandle& ProcessHandle, FModuleHandleArray& OutHandles)
{
	// Get all the module handles for the current process. Each module handle is its base address.
	for (;;)
	{
		DWORD BufferSize = OutHandles.Num() * sizeof(HMODULE);
		DWORD RequiredBufferSize = 0;
		if (!EnumProcessModulesEx(ProcessHandle.IsValid() ? ProcessHandle.Get() : GetCurrentProcess(), (HMODULE*)OutHandles.GetData(), BufferSize, &RequiredBufferSize, LIST_MODULES_ALL))
		{
			// We do not want partial set of modules in case this fails.
			OutHandles.Empty();
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

void FMicrosoftPlatformCrashContext::ConvertProgramCountersToStackFrames(
	const FProcHandle& ProcessHandle,
	const FModuleHandleArray& SortedModuleHandles,
	const uint64* ProgramCounters,
	int32 NumPCs,
	TArray<FCrashStackFrame>& OutStackFrames)
{
	// Prepare the callstack buffer
	OutStackFrames.Reset(NumPCs);

	TCHAR Buffer[PLATFORM_MAX_FILEPATH_LENGTH_DEPRECATED];
	FString Unknown = TEXT("Unknown");

	HANDLE Process = ProcessHandle.IsValid() ? ProcessHandle.Get() : GetCurrentProcess();
	// Create the crash context
	for (int32 Idx = 0; Idx < NumPCs; ++Idx)
	{
		int32 ModuleIdx = Algo::UpperBound(SortedModuleHandles, (void*)ProgramCounters[Idx]) - 1;
		if (ModuleIdx < 0 || ModuleIdx >= SortedModuleHandles.Num())
		{
			OutStackFrames.Add(FCrashStackFrame(Unknown, 0, ProgramCounters[Idx]));
		}
		else
		{
			FStringView ModuleName = Unknown;
			if (GetModuleFileNameExW(Process, (HMODULE)SortedModuleHandles[ModuleIdx], Buffer, UE_ARRAY_COUNT(Buffer)) != 0)
			{
				ModuleName = Buffer;
				if (int32 CharIdx = 0; ModuleName.FindLastChar('\\', CharIdx))
				{
					ModuleName.RightChopInline(CharIdx +1);
				}
				if (int32 CharIdx = 0; ModuleName.FindLastChar('.', CharIdx))
				{
					ModuleName.LeftInline(CharIdx);
				}
			}

			uint64 BaseAddress = (uint64)SortedModuleHandles[ModuleIdx];
			uint64 Offset = ProgramCounters[Idx] - BaseAddress;
			OutStackFrames.Add(FCrashStackFrame(FString(ModuleName), BaseAddress, Offset));
		}
	}
}
