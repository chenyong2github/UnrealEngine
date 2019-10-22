// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADOptions.h"
#include "HAL/Runnable.h"
#include "Misc/DateTime.h"


class FCoreTechFileParser;

/**
 * Serializable interface.
 * Should allow writing/Reading from Socket/file/any stream
 */

namespace DatasmithDispatcher
{

	enum EProcessState
	{
		Unknown = 0x00,
		UnTreated = 0x01,
		Running = 0x02,
		ProcessOk = 0x04,
		ProcessFailed = 0x08,
		FileNotFound = 0x10
	};

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

	/**
	 * Enum with all command types
	 */
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
		//Initialize command
		virtual void Initialize() = 0;

		//Get command type
		virtual DatasmithCommandType GetType() = 0;

		//Write to stream
		virtual void Write(FDatasmithDispatcherSocket&) = 0;

		//Read from stream
		virtual void Read(FDatasmithDispatcherSocket&) = 0;
	};

	/**
	 * Process commands received by client listener (here GPCTClientListener)
	 */
	class FDatasmithCommandManager
	{
	public:
		//Initialize command manager (add default commands)
		FDatasmithCommandManager(FDatasmithDispatcherSocket& CurrentSocket);

		//Destructor
		~FDatasmithCommandManager();

		void Initialize();
		void Terminate();

		ICommand *GetNextCommand();

	private:
		//Command map (map command type with command implementation -> for Reading)
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
		//Initialize command
		FDatasmithPingCommand() {}

		virtual void Initialize() override
		{
		}

		// Inherited via ICommand
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
		//Initialize command
		FDatasmithBackPingCommand() {}

		virtual void Initialize() override
		{
		}

		// Inherited via ICommand
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
		//Constructors
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

		// Inherited via ICommand
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
		virtual ~FDatasmithNotifyEndTaskCommand()
		{
		}

		void SetExternalReferences(const TArray<FString>& ExternalRef)
		{
			ExternalReferenceSet = ExternalRef;
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

		virtual void Initialize() override
		{
			ExternalReferenceSet.Empty();
			ProcessResult = UnTreated;
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
		virtual ~FDatasmithImportParametersCommand()
		{
		}

		virtual DatasmithCommandType GetType() override
		{
			return ImportParams;
		}
		
		virtual void Initialize() override
		{
			ImportParameters.init();
		}

		virtual void Set(const CAD::FImportParameters& InImportParameters)
		{
			ImportParameters = InImportParameters;
		}

		virtual void Get(CAD::FImportParameters& OutImportParameters)
		{
			OutImportParameters = ImportParameters;
		}

		virtual void Write(FDatasmithDispatcherSocket&) override;
		virtual void Read(FDatasmithDispatcherSocket&) override;

	private:
		CAD::FImportParameters ImportParameters;
	};

} // NS DatasmithDispatcher
