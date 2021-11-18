// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Core/System.h"

#include "CADKernel/Core/Database.h"
#include "CADKernel/Core/Entity.h"
#include "CADKernel/Core/KernelParameters.h"
#include "CADKernel/Core/Version.h"
#include "CADKernel/UI/Visu.h"
#include "CADKernel/Utils/Util.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#ifdef CADKERNEL_DEV
#include <stdlib.h>
#include <signal.h>
#endif 

namespace CADKernel
{

TUniquePtr<FSystem> FSystem::Instance = nullptr;

FSystem::FSystem()
	: Parameters(MakeShared<FKernelParameters>())
	, DefaultVisu()
	, Viewer(&DefaultVisu)
	, Console(&DefaultConsole)
	, ProgressManager(&DefaultProgressManager)

{
	LogLevel = Log;
	VerboseLevel = Log;
}

void FSystem::Initialize(bool bIsDll, const FString& LogFilePath, const FString& SpyFilePath)
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

void FSystem::CloseLogFiles()
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

void FSystem::Shutdown()
{
	CloseLogFiles();
	Instance.Reset();
}

void FSystem::DefineLogFile(const FString& InLogFilePath, EVerboseLevel InLevel)
{
	if(LogFile) 
	{
		LogFile->Close();
		LogFile.Reset();
	}

	LogFile = MakeShareable<FArchive>(IFileManager::Get().CreateFileWriter(*InLogFilePath, IO_WRITE));
	LogLevel = InLevel;
}

void FSystem::DefineSpyFile(const FString& InSpyFilePath)
{
	if(SpyFile) 
	{
		SpyFile->Close();
		SpyFile.Reset();
	}
	SpyFile = MakeShareable<FArchive>(IFileManager::Get().CreateFileWriter(*InSpyFilePath, IO_WRITE));
}


#if defined(CADKERNEL_DEV) || defined(CADKERNEL_STDA)
void FSystem::DefineQaDataFile(const FString& InQaDataFilePath)
{
	if (QaDataFile.IsValid())
	{
		QaDataFile->Close();
		QaDataFile.Reset();

		if (QaHeaderFile.IsValid())
		{
			QaHeaderFile->Close();
			QaHeaderFile.Reset();
		}
	}

	FString QualifPath = FPaths::GetPath(InQaDataFilePath);
	if (!FPaths::DirectoryExists(QualifPath))
	{
		IFileManager::Get().MakeDirectory(*QualifPath, true);
	}

	QaDataFile = MakeShareable<FArchive>(IFileManager::Get().CreateFileWriter(*InQaDataFilePath, IO_WRITE));

	QualifPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::GetPath(QualifPath), TEXT("QualifHeader.txt")));

	if (!IFileManager::Get().FileExists(*QualifPath))
	{
		QaHeaderFile = MakeShareable<FArchive>(IFileManager::Get().CreateFileWriter(*QualifPath, IO_WRITE));
	}
}
#endif

void FSystem::InitializeCADKernel()
{
	FSystem::Get().Initialize();
	FSystem::Get().SetVerboseLevel(EVerboseLevel::Log);
}

FString FSystem::GetToolkitVersion() const
{
	return UTF8_TO_TCHAR(TOOLKIT_VERSION_ASCII);
}

FString FSystem::GetCompilationDate() const
{
	return UTF8_TO_TCHAR(__DATE__);
}

void FSystem::PrintHeader()
{
	FMessage::Printf(Log, TEXT("_______________________________________________________________________________\n"));
	FMessage::Printf(Log, TEXT("\n"));
	FMessage::Printf(Log, TEXT("\tDatasmith CAD Kernel Toolkit release %s (%s)\n"),* GetToolkitVersion(),* GetCompilationDate());
	FMessage::Printf(Log, TEXT("\t" EPIC_COPYRIGHT "\n"));
	FMessage::Printf(Log, TEXT("\n"));
	FMessage::Printf(Log, TEXT("_______________________________________________________________________________\n"));
	FMessage::Printf(Log, TEXT("\n"));
}


}