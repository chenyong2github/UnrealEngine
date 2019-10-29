// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADOptions.h"
#include "HAL/Runnable.h"
#include "Misc/DateTime.h"
#include "CoreTechFileParser.h"


class FCoreTechFileParser;

namespace DatasmithDispatcher
{
struct FTask
{
	FTask(const FString& InFile)
	{
		FileName = InFile;
		State = UnTreated;
	}

	FTask()
	{
	}

	FString FileName;
	int32 Index = -1;
	EProcessState State = Unknown;
};

class FDatasmithDispatcherSocket;
class FDatasmithDispatcher;

enum DatasmithCommandType
{
	Ping,
	BackPing,
	RunTask,
	NotifyEndTask,
	ImportParams,
	Last
};

/**
 * Command interface
 * Serializable for Socket
 */
class ICommand
{
public:
	virtual ~ICommand() = default;

	virtual void Initialize()
	{
	}

	virtual DatasmithCommandType GetType() = 0;

	virtual void Write(FDatasmithDispatcherSocket&) = 0;
	virtual void Read(FDatasmithDispatcherSocket&) = 0;
};
	
/**
 * Process commands received by client listener (here GPCTClientListener)
 */
class FDatasmithCommandManager
{
public:
	FDatasmithCommandManager(FDatasmithDispatcherSocket& CurrentSocket);

	~FDatasmithCommandManager();

	void Initialize();
	void Terminate();

	ICommand *GetNextCommand();

private:
	// Command map (map command type with command implementation -> for Reading)
	TMap<DatasmithCommandType, ICommand*> CommandMap;

	FDatasmithDispatcherSocket& Socket;
	uint32 CurrentCommandeSize;
};

/**
 * "Template" command. Use this command if you want to implement new ones -> this is one of the simplest possible commands
 */
class FDatasmithPingCommand : public ICommand
{
public:
	virtual DatasmithCommandType GetType() override
	{
		return Ping;
	}
	virtual void Write(FDatasmithDispatcherSocket&) override;
	virtual void Read(FDatasmithDispatcherSocket&) override;

private:

};

class FDatasmithBackPingCommand : public ICommand
{
public:
	virtual DatasmithCommandType GetType() override
	{
		return BackPing;
	}
	virtual void Write(FDatasmithDispatcherSocket&) override;
	virtual void Read(FDatasmithDispatcherSocket&) override;

private:

};

class FDatasmithRunTaskCommand : public ICommand
{
public:
	FDatasmithRunTaskCommand()
	{
	}

	FDatasmithRunTaskCommand(const FString& InOutputFile, const int32 InJobIndex) 
		: JobFilePath(InOutputFile)
		, JobIndex(InJobIndex)
	{
	}

	virtual ~FDatasmithRunTaskCommand()
	{
	}

	const FString& GetFileToProcess() const
	{
		return JobFilePath;
	}

	virtual void Initialize() override
	{
		JobFilePath.Empty();
		JobIndex = 0;
	}

	virtual DatasmithCommandType GetType() override
	{
		return RunTask;
	}

	virtual void Write(FDatasmithDispatcherSocket&) override;
	virtual void Read(FDatasmithDispatcherSocket&) override;

private:
	FString JobFilePath;
	int32 JobIndex;
};

class FDatasmithNotifyEndTaskCommand : public ICommand
{
public:
	void SetExternalReferences(const TSet<FString>& ExternalRefSet)
	{
		ExternalReferenceSet.Reserve(ExternalRefSet.Num());
		for (const FString& ExternalFile : ExternalRefSet)
		{
			ExternalReferenceSet.Add(ExternalFile);
		}
	}
	
	const TArray<FString>& GetExternalReferences()
	{
		return ExternalReferenceSet;
	}

	void SetProcessResult(EProcessState InProcessResult)
	{
		ProcessResult = InProcessResult;
	}

	EProcessState GetProcessResult()
	{
		return ProcessResult;
	}

	void SetSceneGraphFile(const FString& InSceneGraphFileName)
	{
		SceneGraphFileName = InSceneGraphFileName;
	}

	const FString& GetSceneGraphFile()
	{
		return SceneGraphFileName;
	}

	void SetGeomFile(const FString& InGeomFile)
	{
		GeomFileName = InGeomFile;
	}

	const FString& GetGeomFile()
	{
		return GeomFileName;
	}

	virtual void Initialize()
	{
		ExternalReferenceSet.Empty();
		ProcessResult = EProcessState::UnTreated;
	}

	virtual DatasmithCommandType GetType() override
	{
		return NotifyEndTask;
	}

	virtual void Write(FDatasmithDispatcherSocket&) override;
	virtual void Read(FDatasmithDispatcherSocket&) override;

private:
	TArray<FString> ExternalReferenceSet;
	EProcessState ProcessResult;
	FString SceneGraphFileName;
	FString GeomFileName;
};

class FDatasmithImportParametersCommand : public ICommand
{
public:
	virtual DatasmithCommandType GetType() override
	{
		return ImportParams;
	}
		
	virtual void Set(const CADLibrary::FImportParameters& InImportParameters)
	{
		ImportParameters = InImportParameters;
	}

	virtual void Get(CADLibrary::FImportParameters& OutImportParameters)
	{
		OutImportParameters = ImportParameters;
	}

	virtual void Write(FDatasmithDispatcherSocket&) override;
	virtual void Read(FDatasmithDispatcherSocket&) override;

private:
	CADLibrary::FImportParameters ImportParameters;
};

} // NS DatasmithDispatcher
