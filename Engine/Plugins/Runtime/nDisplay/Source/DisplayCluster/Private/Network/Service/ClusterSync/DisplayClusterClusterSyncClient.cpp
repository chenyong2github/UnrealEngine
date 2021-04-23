// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Service/ClusterSync/DisplayClusterClusterSyncClient.h"
#include "Network/Service/ClusterSync/DisplayClusterClusterSyncStrings.h"
#include "Network/Conversion/DisplayClusterNetworkDataConversion.h"
#include "Network/Packet/DisplayClusterPacketInternal.h"

#include "Misc/ScopeLock.h"
#include "Misc/FrameRate.h"
#include "Misc/Timecode.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterClusterSyncClient::FDisplayClusterClusterSyncClient()
	: FDisplayClusterClusterSyncClient(FString("CLN_CS"))
{
}

FDisplayClusterClusterSyncClient::FDisplayClusterClusterSyncClient(const FString& InName)
	: FDisplayClusterClient(InName)
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolClusterSync
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterSyncClient::WaitForGameStart()
{
	static TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::WaitForGameStart::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(*FString::Printf(TEXT("nDisplay ClusterSyncClient::%s"), *Request->GetName()), CpuChannel);
		SendRecvPacket(Request);
	}
}

void FDisplayClusterClusterSyncClient::WaitForFrameStart()
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::WaitForFrameStart::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(*FString::Printf(TEXT("nDisplay ClusterSyncClient::%s"), *Request->GetName()), CpuChannel);
		SendRecvPacket(Request);
	}
}

void FDisplayClusterClusterSyncClient::WaitForFrameEnd()
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::WaitForFrameEnd::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(*FString::Printf(TEXT("nDisplay ClusterSyncClient::%s"), *Request->GetName()), CpuChannel);
		SendRecvPacket(Request);
	}
}

void FDisplayClusterClusterSyncClient::GetTimeData(float& InOutDeltaTime, double& InOutGameTime, TOptional<FQualifiedFrameTime>& InOutFrameTime)
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::GetTimeData::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	TSharedPtr<FDisplayClusterPacketInternal> Response;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(*FString::Printf(TEXT("nDisplay ClusterSyncClient::%s"), *Request->GetName()), CpuChannel);
		Response = SendRecvPacket(Request);
	}

	if (Response.IsValid())
	{
		// Extract sync data from response packet
		FString StrDeltaTime;
		if (!Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetTimeData::ArgDeltaTime, StrDeltaTime))
		{
			UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("Couldn't extract an argument: %s"), DisplayClusterClusterSyncStrings::GetTimeData::ArgDeltaTime);
			return;
		}

		// Convert from hex string to float
		InOutDeltaTime = DisplayClusterTypesConverter::template FromHexString<float>(StrDeltaTime);

		FString StrGameTime;
		if (!Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetTimeData::ArgGameTime, StrGameTime))
		{
			UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("Couldn't extract an argument: %s"), DisplayClusterClusterSyncStrings::GetTimeData::ArgGameTime);
			return;
	}

		// Convert from hex string to float
		InOutGameTime = DisplayClusterTypesConverter::template FromHexString<double>(StrGameTime);

		// Extract sync data from response packet
		bool bIsFrameTimeValid = false;
		if (!Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetTimeData::ArgIsFrameTimeValid, bIsFrameTimeValid))
		{
			UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("Couldn't extract an argument: %s"), DisplayClusterClusterSyncStrings::GetTimeData::ArgIsFrameTimeValid);
		}

		if (bIsFrameTimeValid)
		{
			FQualifiedFrameTime NewFrameTime;
			if (!Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetTimeData::ArgFrameTime, NewFrameTime))
			{
				UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("Couldn't extract an argument: %s"), DisplayClusterClusterSyncStrings::GetTimeData::ArgFrameTime);
			}

			InOutFrameTime = NewFrameTime;
		}
	}
}

void FDisplayClusterClusterSyncClient::GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
	static TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::GetSyncData::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);
	
	Request->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetSyncData::ArgSyncGroup, (int)SyncGroup);
	
	TSharedPtr<FDisplayClusterPacketInternal> Response;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(*FString::Printf(TEXT("nDisplay ClusterSyncClient::%s"), *Request->GetName()), CpuChannel);
		Response = SendRecvPacket(Request);
	}

	if (Response.IsValid())
	{
		// Extract data from response packet
		SyncData = Response->GetTextArgs(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory);
	}
}

void FDisplayClusterClusterSyncClient::GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents)
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::GetEventsData::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	TSharedPtr<FDisplayClusterPacketInternal> Response;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(*FString::Printf(TEXT("nDisplay ClusterSyncClient::%s"), *Request->GetName()), CpuChannel);
		Response = SendRecvPacket(Request);
	}

	if (Response.IsValid())
	{
		// Extract events data from response packet
		DisplayClusterNetworkDataConversion::JsonEventsFromInternalPacket(Response,   JsonEvents);
		DisplayClusterNetworkDataConversion::BinaryEventsFromInternalPacket(Response, BinaryEvents);
	}
}

void FDisplayClusterClusterSyncClient::GetNativeInputData(TMap<FString, FString>& NativeInputData)
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::GetNativeInputData::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	TSharedPtr<FDisplayClusterPacketInternal> Response;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(*FString::Printf(TEXT("nDisplay ClusterSyncClient::%s"), *Request->GetName()), CpuChannel);
		Response = SendRecvPacket(Request);
	}

	if (Response.IsValid())
	{
		// Extract data from response packet
		NativeInputData = Response->GetTextArgs(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory);
	}
}
