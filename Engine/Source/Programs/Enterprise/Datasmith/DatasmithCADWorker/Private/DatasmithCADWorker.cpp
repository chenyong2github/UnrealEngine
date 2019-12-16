// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithCADWorker.h"
#include "DatasmithCADWorkerImpl.h"

#include "RequiredProgramMainCPPInclude.h"

IMPLEMENT_APPLICATION(DatasmithCADWorker, "DatasmithCADWorker");


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
	FString ServerPID, ServerPort, CacheDirectory;
	GetParameter(Argc, Argv, "-ServerPID", ServerPID);
	GetParameter(Argc, Argv, "-ServerPort", ServerPort);
	GetParameter(Argc, Argv, "-CacheDir", CacheDirectory);

	FString BinaryDir = FPaths::Combine(FPaths::EngineDir(), TEXT("Plugins"), TEXT("Enterprise"), TEXT("DatasmithCADImporter"), TEXT("Binaries"), TEXT("Win64"));
	FString KernelIOLibDir = FPaths::Combine(BinaryDir, TEXT("kernel_io.dll"));
	FPlatformProcess::PushDllDirectory(*BinaryDir);
	void* KernelIODllHandle = FPlatformProcess::GetDllHandle(*KernelIOLibDir);
	FPlatformProcess::PopDllDirectory(*BinaryDir);

	FDatasmithCADWorkerImpl Worker(FCString::Atoi(*ServerPID), FCString::Atoi(*ServerPort), CacheDirectory);
	Worker.Run();

	return 0;
}

int32 Filter(uint32 Code, struct _EXCEPTION_POINTERS *Ep)
{
	return EXCEPTION_EXECUTE_HANDLER;
}

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
#ifdef CAD_INTERFACE
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
#else
	return EXIT_FAILURE;
#endif // CAD_INTERFACE
}
