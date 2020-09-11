// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IPv4/Ipv4Endpoint.h"

enum class ESwitchboardTaskType : uint8
{
	Start,
	Kill,
	KillAll,
	VcsInit,
	VcsReportRevision,
	VcsSync,
	Restart,
	ReceiveFileFromClient,
	SendFileToClient,
	Disconnect,
	KeepAlive,
};

struct FSwitchboardTask
{
	ESwitchboardTaskType Type;
	FString Name;
	FGuid TaskID;
	FIPv4Endpoint Recipient;
};

struct FSwitchboardStartTask : public FSwitchboardTask
{
	FSwitchboardStartTask(const FGuid& InTaskId, const FIPv4Endpoint& InEndpoint, const FString& InCommand, const FString& InArgs)
		: FSwitchboardTask{ ESwitchboardTaskType::Start, TEXT("start"), InTaskId, InEndpoint }
		, Command(InCommand)
		, Arguments(InArgs)
	{
	}

	FString Command;
	FString Arguments;
};

struct FSwitchboardKillTask : public FSwitchboardTask
{
	FSwitchboardKillTask(const FGuid& InTaskId, const FIPv4Endpoint& InEndpoint, const FGuid& InProgramID)
		: FSwitchboardTask{ ESwitchboardTaskType::Kill, TEXT("kill"), InTaskId, InEndpoint}
		, ProgramID(InProgramID)
	{}

	FGuid ProgramID; // unique ID of process to kill
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
};

struct FSwitchboardSendFileToClientTask : public FSwitchboardTask
{
	FSwitchboardSendFileToClientTask(const FGuid& InTaskID, const FIPv4Endpoint& InEndpoint, const FString& InSource)
		: FSwitchboardTask{ ESwitchboardTaskType::SendFileToClient, TEXT("send file to client"), InTaskID, InEndpoint }
		, Source(InSource)
	{}

	FString Source;
};

struct FSwitchboardVcsInitTask : public FSwitchboardTask
{
	FSwitchboardVcsInitTask(const FGuid& InTaskId, const FIPv4Endpoint& InEndpoint, const FString& InVcsProviderName, const TMap<FString, FString>& InVcsSettings)
		: FSwitchboardTask{ ESwitchboardTaskType::VcsInit, TEXT("vcs init"), InTaskId, InEndpoint }
		, ProviderName(InVcsProviderName)
		, VcsSettings(InVcsSettings)
	{}

	FString ProviderName;
	TMap<FString, FString> VcsSettings;
};

struct FSwitchboardVcsReportRevisionTask : public FSwitchboardTask
{
	FSwitchboardVcsReportRevisionTask(const FGuid& InTaskId, const FIPv4Endpoint& InEndpoint, const FString& InPath)
		: FSwitchboardTask{ ESwitchboardTaskType::VcsReportRevision, TEXT("vcs report revision"), InTaskId, InEndpoint }
		, Path(InPath)
	{}

	FString Path;
};

struct FSwitchboardVcsSyncTask : public FSwitchboardTask
{
	FSwitchboardVcsSyncTask(const FGuid& InTaskId, const FIPv4Endpoint& InEndpoint, const FString& InRevision, const FString& InPath)
		: FSwitchboardTask{ ESwitchboardTaskType::VcsSync, TEXT("vcs sync"), InTaskId, InEndpoint }
		, Revision(InRevision)
		, Path(InPath)
	{}

	FString Revision;
	FString Path;
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
