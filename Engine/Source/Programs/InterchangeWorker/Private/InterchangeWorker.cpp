// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeWorker.h"
#include "InterchangeWorkerImpl.h"

#include "RequiredProgramMainCPPInclude.h"

IMPLEMENT_APPLICATION(InterchangeWorker, "InterchangeWorker");
DEFINE_LOG_CATEGORY(LogInterchangeWorker);

#define EXIT_MISSING_CORETECH_MODULE 2

bool GIsConsoleExecutable = true;

void GetParameter(int32 Argc, TCHAR* Argv[], const FString& InParam, FString& OutValue)
{
	OutValue.Empty();
	for (int32 Index = 1; Index < Argc; Index++)
	{
		if (! FCString::Strcmp(*InParam, Argv[Index]))
		{
			if (Index + 1 < Argc)
			{
				OutValue = Argv[Index + 1];
			}
		}
	}
}

bool HasParameter(int32 Argc, TCHAR* Argv[], const FString& InParam)
{
	for (int32 Index = 1; Index < Argc; Index++)
	{
		if (!FCString::Strcmp(*InParam, Argv[Index]))
		{
			return true;
		}
	}
	return false;
}

int32 Main(int32 Argc, TCHAR * Argv[])
{
	UE_SET_LOG_VERBOSITY(LogInterchangeWorker, Verbose);

	FString ResultFolder, ServerPID, ServerPort, InterchangeDispatcherVersion;
	GetParameter(Argc, Argv, "-ServerPID", ServerPID);
	GetParameter(Argc, Argv, "-ServerPort", ServerPort);
	GetParameter(Argc, Argv, "-InterchangeDispatcherVersion", InterchangeDispatcherVersion);
	GetParameter(Argc, Argv, "-ResultFolder", ResultFolder);

	int32 Major = 0;
	int32 Minor = 0;
	int32 Patch = 0;
	if (!InterchangeDispatcher::DispatcherCommandVersion::FromString(InterchangeDispatcherVersion, Major, Minor, Patch))
	{
		UE_LOG(LogInterchangeWorker, Error, TEXT("Incompatible interchange dispatcher version string command argument."));
		return EXIT_FAILURE;
	}
 	if (!InterchangeDispatcher::DispatcherCommandVersion::IsAPICompatible(Major, Minor, Patch))
 	{
 		UE_LOG(LogInterchangeWorker, Error, TEXT("Incompatible interchange dispatcher version. Please recompile InterchangeWorker target."));
 		return EXIT_FAILURE;
 	}

	FInterchangeWorkerImpl Worker(FCString::Atoi(*ServerPID), FCString::Atoi(*ServerPort), ResultFolder);
	Worker.Run();

	return EXIT_SUCCESS;
}

int32 Filter(uint32 Code, struct _EXCEPTION_POINTERS *Ep)
{
	return EXCEPTION_EXECUTE_HANDLER;
}

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	GEngineLoop.PreInit(ArgC, ArgV);

	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	_set_abort_behavior(0, _WRITE_ABORT_MSG);
	__try
	{
		return Main(ArgC, ArgV);
	}
	__except (Filter(GetExceptionCode(), GetExceptionInformation()))
	{
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
