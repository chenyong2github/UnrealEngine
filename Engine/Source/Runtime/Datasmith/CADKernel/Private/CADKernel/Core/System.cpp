// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Core/System.h"

#include "CADKernel/Core/Database.h"
#include "CADKernel/Core/Entity.h"
#include "CADKernel/Core/KernelParameters.h"
#include "CADKernel/Core/Version.h"
#include "CADKernel/UI/Visu.h"
#include "CADKernel/Utils/Util.h"

#include "HAL/FileManager.h"

#ifdef CADKERNEL_DEV
#include <stdlib.h>
#include <signal.h>
#endif 

TUniquePtr<CADKernel::FSystem> CADKernel::FSystem::Instance = nullptr;

CADKernel::FSystem::FSystem()
	: Parameters(MakeShared<FKernelParameters>())
	, DefaultVisu()
	, Viewer(&DefaultVisu)
	, Console(&DefaultConsole)
	, ProgressManager(&DefaultProgressManager)
{
	LogFile = nullptr;
	LogLevel = Log;
	SpyFile = nullptr;

	VerboseLevel = Log;
}

void CADKernel::FSystem::Initialize(bool bIsDll, const FString& LogFilePath, const FString& SpyFilePath)
{
	SetVerboseLevel(Log);

	if (LogFilePath.Len() > 0)
	{
		DefineLogFile(LogFilePath);
	}
	if (SpyFilePath.Len() > 0)
	{
		DefineSpyFile(SpyFilePath);
	}

	PrintHeader();

	fflush(stdout);

	if(bIsDll) 
	{
		SetVerboseLevel(EVerboseLevel::NoVerbose);
	} 
	else 
	{
		SetVerboseLevel(EVerboseLevel::Log);
	}
}

void CADKernel::FSystem::CloseLogFiles()
{
	if (LogFile)
	{
		LogFile->Close();
		LogFile.Reset();
	}
	if (SpyFile)
	{
		SpyFile->Close();
		SpyFile.Reset();
	}
}

void CADKernel::FSystem::Shutdown()
{
	CloseLogFiles();
	Instance.Reset();
}

void CADKernel::FSystem::DefineLogFile(const FString& InLogFilePath, EVerboseLevel InLevel)
{
	if(LogFile) 
	{
		LogFile->Close();
		LogFile.Reset();
	}

	LogFile = MakeShareable<FArchive>(IFileManager::Get().CreateFileWriter(*InLogFilePath, IO_WRITE));
	LogLevel = InLevel;
}

void CADKernel::FSystem::DefineSpyFile(const FString& InSpyFilePath)
{
	if(SpyFile) 
	{
		SpyFile->Close();
		SpyFile.Reset();
	}
	SpyFile = MakeShareable<FArchive>(IFileManager::Get().CreateFileWriter(*InSpyFilePath, IO_WRITE));
}

void CADKernel::FSystem::InitializeCADKernel()
{
	FSystem::Get().Initialize();
	FSystem::Get().SetVerboseLevel(EVerboseLevel::Log);
}

FString CADKernel::FSystem::GetToolkitVersion() const
{
	return TOOLKIT_VERSION_ASCII;
}

FString CADKernel::FSystem::GetCompilationDate() const
{
	return UTF8_TO_TCHAR(__DATE__);
}

void CADKernel::FSystem::PrintHeader()
{
	FMessage::Printf(Log, TEXT("_______________________________________________________________________________\n"));
	FMessage::Printf(Log, TEXT("\n"));
	FMessage::Printf(Log, TEXT("\tDatasmith CAD Kernel Toolkit release %s (%s)\n"),* GetToolkitVersion(),* GetCompilationDate());
	FMessage::Printf(Log, TEXT("\t" EPIC_COPYRIGHT "\n"));
	FMessage::Printf(Log, TEXT("\n"));
	FMessage::Printf(Log, TEXT("_______________________________________________________________________________\n"));
	FMessage::Printf(Log, TEXT("\n"));
}
