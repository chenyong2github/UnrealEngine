// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithCommands.h"

#include "DatasmithDispatcherSocket.h"
#include "DatasmithDispatcher.h"

namespace DatasmithDispatcher
{
	FDatasmithCommandManager::FDatasmithCommandManager(FDatasmithDispatcherSocket& InSocket)
		: Socket(InSocket)
		, CurrentCommandeSize(0)
	{
		Initialize();
	}

	FDatasmithCommandManager::~FDatasmithCommandManager()
	{
		Terminate();
	}

	void FDatasmithCommandManager::Initialize()
	{
		CommandMap.Reserve((int32)DatasmithCommandType::Last);

		CommandMap.Add(Ping, new FDatasmithPingCommand());
		CommandMap.Add(BackPing, new FDatasmithBackPingCommand());
		CommandMap.Add(RunTask, new FDatasmithRunTaskCommand());
		CommandMap.Add(NotifyEndTask, new FDatasmithNotifyEndTaskCommand());
		CommandMap.Add(ImportParams, new FDatasmithImportParametersCommand());
	}

	void FDatasmithCommandManager::Terminate()
	{
		for (auto ItemCommand : CommandMap)
		{
			delete ItemCommand.Value;
		}
		CommandMap.Empty();
	}

	ICommand * FDatasmithCommandManager::GetNextCommand()
	{
		uint32 DataSize;
		if (!Socket.HasPendingData(DataSize))
		{
			return nullptr;
		}

		if (!CurrentCommandeSize)
		{
			Socket >> CurrentCommandeSize;
		}

		if (!Socket.HasPendingData(DataSize))
		{
			return nullptr;
		}

		// All data is not yet arrived, wait
		if (DataSize < CurrentCommandeSize)
		{
			return nullptr;
		}

		DatasmithCommandType CommandType;
		Socket >> CommandType;

		ICommand** Command = CommandMap.Find(CommandType);
		if (Command == nullptr)
		{
			return nullptr;
		}
		 
		(*Command)->Initialize();
		(*Command)->Read(Socket);

		CurrentCommandeSize = 0;

		return (*Command);
	}

	void FDatasmithBackPingCommand::Write(FDatasmithDispatcherSocket &CurrentSocket)
	{
		CurrentSocket << GetType();
		CurrentSocket.SendData();
	}

	void FDatasmithBackPingCommand::Read(FDatasmithDispatcherSocket &CurrentSocket)
	{
	}

	void FDatasmithPingCommand::Write(FDatasmithDispatcherSocket &CurrentSocket)
	{
		CurrentSocket << GetType();
		CurrentSocket.SendData();
	}

	void FDatasmithPingCommand::Read(FDatasmithDispatcherSocket &CurrentSocket)
	{
	}

	void FDatasmithRunTaskCommand::Write(FDatasmithDispatcherSocket&  CurrentSocket)
	{
		CurrentSocket << GetType();
		CurrentSocket << JobFilePath;
		CurrentSocket << JobIndex;					
		CurrentSocket.SendData();
	}

	void FDatasmithRunTaskCommand::Read(FDatasmithDispatcherSocket& CurrentSocket)
	{
		CurrentSocket >> JobFilePath;
		CurrentSocket >> JobIndex;
	}

  	void FDatasmithNotifyEndTaskCommand::Write(FDatasmithDispatcherSocket& CurrentSocket)
	{
		CurrentSocket << GetType();

		CurrentSocket << (uint32)ExternalReferenceSet.Num();
		for (const FString& ExternalReference : ExternalReferenceSet)
		{
			CurrentSocket << ExternalReference;
		}
		CurrentSocket << ProcessResult;
		CurrentSocket << SceneGraphFileName;
		CurrentSocket << GeomFileName;
		CurrentSocket.SendData();
	}

	void FDatasmithNotifyEndTaskCommand::Read(FDatasmithDispatcherSocket& CurrentSocket)
	{
		uint32 NbReferences;

		CurrentSocket >> NbReferences;
		FString Buffer;
		for (uint32 ite = 0; ite < NbReferences; ++ite)
		{
			CurrentSocket >> Buffer;
			ExternalReferenceSet.Add(Buffer);
		}
		CurrentSocket >> ProcessResult;
		CurrentSocket >> SceneGraphFileName;
		CurrentSocket >> GeomFileName;
	}


	void FDatasmithImportParametersCommand::Write(FDatasmithDispatcherSocket&  CurrentSocket)
	{
		CurrentSocket << GetType();
		CurrentSocket << ImportParameters.ChordTolerance;
		CurrentSocket << ImportParameters.MaxEdgeLength;
		CurrentSocket << ImportParameters.MaxNormalAngle;
		CurrentSocket << ImportParameters.MetricUnit;
		CurrentSocket << ImportParameters.ScaleFactor;
		CurrentSocket << ImportParameters.StitchingTechnique;
		CurrentSocket.SendData();
	}

	void FDatasmithImportParametersCommand::Read(FDatasmithDispatcherSocket&  CurrentSocket)
	{
		CurrentSocket >> ImportParameters.ChordTolerance;
		CurrentSocket >> ImportParameters.MaxEdgeLength;
		CurrentSocket >> ImportParameters.MaxNormalAngle;
		CurrentSocket >> ImportParameters.MetricUnit;
		CurrentSocket >> ImportParameters.ScaleFactor;
		CurrentSocket >> ImportParameters.StitchingTechnique;
	}

}