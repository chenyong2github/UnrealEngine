// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Templates/TypeHash.h"

enum class ESwitchboardTaskType : uint8
{
	Start,
	Kill,
	KillAll,
	Restart,
	ReceiveFileFromClient,
	SendFileToClient,
	Disconnect,
	KeepAlive,
	GetSyncStatus,
	GetFlipMode,
};

struct FSwitchboardTask
{
	ESwitchboardTaskType Type;
	FString Name;
	FGuid TaskID;
	FIPv4Endpoint Recipient;

	FSwitchboardTask(ESwitchboardTaskType InType, FString InName, FGuid InTaskID, FIPv4Endpoint InRecipient)
		: Type(InType)
		, Name(InName)
		, TaskID(InTaskID)
		, Recipient(InRecipient)
	{}

	/** Calculates a hash that should be the same for equivalent Tasks, even if their TaskID is different */
	virtual uint32 GetEquivalenceHash() const
	{
		return HashCombine(GetTypeHash(Type), GetTypeHash(Recipient));
	}
	
	virtual ~FSwitchboardTask()
	{}
};

struct FSwitchboardGetSyncStatusTask : public FSwitchboardTask
{
	FSwitchboardGetSyncStatusTask(const FGuid& InTaskId, const FIPv4Endpoint& InEndpoint, const FGuid& InProgramID)
		: FSwitchboardTask{ ESwitchboardTaskType::GetSyncStatus, TEXT("get sync status"), InTaskId, InEndpoint }
		, ProgramID(InProgramID)
	{}

	/** ID of the program that we wish to get the FlipMode of */
	FGuid ProgramID;

	//~ Begin FSwitchboardTask interface
	virtual uint32 GetEquivalenceHash() const override
	{
		return HashCombine(FSwitchboardTask::GetEquivalenceHash(), GetTypeHash(ProgramID));
	}
	//~ End FSwitchboardTask interface
};

struct FSwitchboardStartTask : public FSwitchboardTask
{
	FSwitchboardStartTask(
		const FGuid& InTaskId, 
		const FIPv4Endpoint& InEndpoint, 
		const FString& InCommand, 
		const FString& InArgs, 
		const FString& InName, 
		const FString& InCaller, 
		bool bInUpdateClientsWithStdout
	)
		: FSwitchboardTask{ ESwitchboardTaskType::Start, TEXT("start"), InTaskId, InEndpoint }
		, Command(InCommand)
		, Arguments(InArgs)
		, Name(InName)
		, Caller(InCaller)
		, bUpdateClientsWithStdout(bInUpdateClientsWithStdout)
	{
	}

	FString Command;
	FString Arguments;
	FString Name;
	FString Caller;
	bool bUpdateClientsWithStdout;

	//~ Begin FSwitchboardTask interface
	virtual uint32 GetEquivalenceHash() const override
	{
		uint32 Hash = HashCombine(FSwitchboardTask::GetEquivalenceHash(), GetTypeHash(Command));
		Hash = HashCombine(Hash, GetTypeHash(Caller));
		return HashCombine(Hash, GetTypeHash(Arguments));
	}
	//~ End FSwitchboardTask interface
};

struct FSwitchboardKillTask : public FSwitchboardTask
{
	FSwitchboardKillTask(const FGuid& InTaskId, const FIPv4Endpoint& InEndpoint, const FGuid& InProgramID)
		: FSwitchboardTask{ ESwitchboardTaskType::Kill, TEXT("kill"), InTaskId, InEndpoint}
		, ProgramID(InProgramID)
	{}

	FGuid ProgramID; // unique ID of process to kill

	//~ Begin FSwitchboardTask interface
	virtual uint32 GetEquivalenceHash() const override
	{
		return HashCombine(FSwitchboardTask::GetEquivalenceHash(), GetTypeHash(ProgramID));
	}
	//~ End FSwitchboardTask interface
};

struct FSwitchboardKillAllTask : public FSwitchboardTask
{
	FSwitchboardKillAllTask(const FGuid& InTaskID, const FIPv4Endpoint& InEndpoint)
		: FSwitchboardTask{ ESwitchboardTaskType::KillAll, TEXT("killall"), InTaskID, InEndpoint }
	{}
};

struct FSwitchboardReceiveFileFromClientTask : public FSwitchboardTask
{
	FSwitchboardReceiveFileFromClientTask(const FGuid& InTaskID, const FIPv4Endpoint& InEndpoint, const FString& InDestination, const FString& InContent)
		: FSwitchboardTask{ ESwitchboardTaskType::ReceiveFileFromClient, TEXT("receive file from client"), InTaskID, InEndpoint }
		, Destination(InDestination)
		, FileContent(InContent)
	{}

	FString Destination;
	FString FileContent;

	//~ Begin FSwitchboardTask interface
	virtual uint32 GetEquivalenceHash() const override
	{
		uint32 Hash = HashCombine(FSwitchboardTask::GetEquivalenceHash(), GetTypeHash(Destination));
		return HashCombine(Hash, GetTypeHash(FileContent));
	}
	//~ End FSwitchboardTask interface
};

struct FSwitchboardSendFileToClientTask : public FSwitchboardTask
{
	FSwitchboardSendFileToClientTask(const FGuid& InTaskID, const FIPv4Endpoint& InEndpoint, const FString& InSource)
		: FSwitchboardTask{ ESwitchboardTaskType::SendFileToClient, TEXT("send file to client"), InTaskID, InEndpoint }
		, Source(InSource)
	{}

	FString Source;

	//~ Begin FSwitchboardTask interface
	virtual uint32 GetEquivalenceHash() const override
	{
		return HashCombine(FSwitchboardTask::GetEquivalenceHash(), GetTypeHash(Source));
	}
	//~ End FSwitchboardTask interface
};

struct FSwitchboardDisconnectTask : public FSwitchboardTask
{
	FSwitchboardDisconnectTask(const FGuid& InTaskId, const FIPv4Endpoint& InEndpoint)
		: FSwitchboardTask{ESwitchboardTaskType::Disconnect, TEXT("disconnect"), InTaskId, InEndpoint}
	{}
};

struct FSwitchboardKeepAliveTask : public FSwitchboardTask
{
	FSwitchboardKeepAliveTask(const FGuid& InTaskId, const FIPv4Endpoint& InEndpoint)
		: FSwitchboardTask{ESwitchboardTaskType::KeepAlive, TEXT("keep alive"), InTaskId, InEndpoint}
	{}
};
