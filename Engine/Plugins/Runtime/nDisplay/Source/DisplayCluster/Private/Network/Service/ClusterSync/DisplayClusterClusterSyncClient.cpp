// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterClusterSyncClient.h"
#include "DisplayClusterClusterSyncMsg.h"

#include "Misc/ScopeLock.h"
#include "Misc/FrameRate.h"
#include "Misc/Timecode.h"

#include "DisplayClusterLog.h"


FDisplayClusterClusterSyncClient::FDisplayClusterClusterSyncClient() :
	FDisplayClusterClient(FString("CLN_CS"))
{
}

FDisplayClusterClusterSyncClient::FDisplayClusterClusterSyncClient(const FString& InName) :
	FDisplayClusterClient(InName)
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterClusterSyncProtocol
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterSyncClient::WaitForGameStart()
{
	static TSharedPtr<FDisplayClusterMessage> Request(new FDisplayClusterMessage(FDisplayClusterClusterSyncMsg::WaitForGameStart::name, FDisplayClusterClusterSyncMsg::TypeRequest, FDisplayClusterClusterSyncMsg::ProtocolName));
	TSharedPtr<FDisplayClusterMessage> Response = SendRecvMsg(Request);
}

void FDisplayClusterClusterSyncClient::WaitForFrameStart()
{
	static const TSharedPtr<FDisplayClusterMessage> Request(new FDisplayClusterMessage(FDisplayClusterClusterSyncMsg::WaitForFrameStart::name, FDisplayClusterClusterSyncMsg::TypeRequest, FDisplayClusterClusterSyncMsg::ProtocolName));
	TSharedPtr<FDisplayClusterMessage> response = SendRecvMsg(Request);
}

void FDisplayClusterClusterSyncClient::WaitForFrameEnd()
{
	static const TSharedPtr<FDisplayClusterMessage> Request(new FDisplayClusterMessage(FDisplayClusterClusterSyncMsg::WaitForFrameEnd::name, FDisplayClusterClusterSyncMsg::TypeRequest, FDisplayClusterClusterSyncMsg::ProtocolName));
	TSharedPtr<FDisplayClusterMessage> response = SendRecvMsg(Request);
}

void FDisplayClusterClusterSyncClient::WaitForTickEnd()
{
	static const TSharedPtr<FDisplayClusterMessage> Request(new FDisplayClusterMessage(FDisplayClusterClusterSyncMsg::WaitForTickEnd::name, FDisplayClusterClusterSyncMsg::TypeRequest, FDisplayClusterClusterSyncMsg::ProtocolName));
	TSharedPtr<FDisplayClusterMessage> Response = SendRecvMsg(Request);
}

void FDisplayClusterClusterSyncClient::GetDeltaTime(float& DeltaSeconds)
{
	static const TSharedPtr<FDisplayClusterMessage> Request(new FDisplayClusterMessage(FDisplayClusterClusterSyncMsg::GetDeltaTime::name, FDisplayClusterClusterSyncMsg::TypeRequest, FDisplayClusterClusterSyncMsg::ProtocolName));
	TSharedPtr<FDisplayClusterMessage> Response = SendRecvMsg(Request);

	if (!Response.IsValid())
	{
		return;
	}

	// Extract sync data from response message
	if (Response->GetArg(FDisplayClusterClusterSyncMsg::GetDeltaTime::argDeltaSeconds, DeltaSeconds) == false)
	{
		UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("Couldn't extract an argument: %s"), FDisplayClusterClusterSyncMsg::GetDeltaTime::argDeltaSeconds);
	}
}

void FDisplayClusterClusterSyncClient::GetTimecode(FTimecode& Timecode, FFrameRate& FrameRate)
{
	static const TSharedPtr<FDisplayClusterMessage> Request(new FDisplayClusterMessage(FDisplayClusterClusterSyncMsg::GetTimecode::name, FDisplayClusterClusterSyncMsg::TypeRequest, FDisplayClusterClusterSyncMsg::ProtocolName));
	TSharedPtr<FDisplayClusterMessage> Response = SendRecvMsg(Request);

	if (!Response.IsValid())
	{
		return;
	}

	// Extract sync data from response message
	if (Response->GetArg(FDisplayClusterClusterSyncMsg::GetTimecode::argTimecode, Timecode) == false)
	{
		UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("Couldn't extract an argument: %s"), FDisplayClusterClusterSyncMsg::GetTimecode::argTimecode);
	}
	if (Response->GetArg(FDisplayClusterClusterSyncMsg::GetTimecode::argFrameRate, FrameRate) == false)
	{
		UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("Couldn't extract an argument: %s"), FDisplayClusterClusterSyncMsg::GetTimecode::argTimecode);
	}
}

void FDisplayClusterClusterSyncClient::GetSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
	static TSharedPtr<FDisplayClusterMessage> Request(new FDisplayClusterMessage(FDisplayClusterClusterSyncMsg::GetSyncData::name, FDisplayClusterClusterSyncMsg::TypeRequest, FDisplayClusterClusterSyncMsg::ProtocolName));
	
	Request->SetArg(FDisplayClusterClusterSyncMsg::GetSyncData::argSyncGroup, (int)SyncGroup);
	
	TSharedPtr<FDisplayClusterMessage> Response = SendRecvMsg(Request);

	if (!Response.IsValid())
	{
		return;
	}

	// Extract data from response message
	SyncData = Response->GetArgs();
}

void FDisplayClusterClusterSyncClient::GetInputData(FDisplayClusterMessage::DataType& InputData)
{
	static const TSharedPtr<FDisplayClusterMessage> Request(new FDisplayClusterMessage(FDisplayClusterClusterSyncMsg::GetInputData::name, FDisplayClusterClusterSyncMsg::TypeRequest, FDisplayClusterClusterSyncMsg::ProtocolName));
	TSharedPtr<FDisplayClusterMessage> Response = SendRecvMsg(Request);

	if (!Response.IsValid())
	{
		return;
	}

	// Extract data from response message
	InputData = Response->GetArgs();
}

void FDisplayClusterClusterSyncClient::GetEventsData(FDisplayClusterMessage::DataType& EventsData)
{
	static const TSharedPtr<FDisplayClusterMessage> Request(new FDisplayClusterMessage(FDisplayClusterClusterSyncMsg::GetEventsData::name, FDisplayClusterClusterSyncMsg::TypeRequest, FDisplayClusterClusterSyncMsg::ProtocolName));
	TSharedPtr<FDisplayClusterMessage> Response = SendRecvMsg(Request);

	if (!Response.IsValid())
	{
		return;
	}

	// Extract data from response message
	EventsData = Response->GetArgs();
}

void FDisplayClusterClusterSyncClient::GetNativeInputData(FDisplayClusterMessage::DataType& NativeInputData)
{
	static const TSharedPtr<FDisplayClusterMessage> Request(new FDisplayClusterMessage(FDisplayClusterClusterSyncMsg::GetNativeInputData::name, FDisplayClusterClusterSyncMsg::TypeRequest, FDisplayClusterClusterSyncMsg::ProtocolName));
	TSharedPtr<FDisplayClusterMessage> Response = SendRecvMsg(Request);

	if (!Response.IsValid())
	{
		return;
	}

	// Extract data from response message
	NativeInputData = Response->GetArgs();
}
