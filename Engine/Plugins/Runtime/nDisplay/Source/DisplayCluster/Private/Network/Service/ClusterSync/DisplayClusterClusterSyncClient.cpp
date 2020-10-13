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
void FDisplayClusterClusterSyncClient::WaitForGameStart(double* ThreadWaitTime, double* BarrierWaitTime)
{
	static TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::WaitForGameStart::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	TSharedPtr<FDisplayClusterPacketInternal> Response = SendRecvPacket(Request);

	if (Response.IsValid())
	{
		if (ThreadWaitTime)
		{
			if (!Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, FString(DisplayClusterClusterSyncStrings::WaitForGameStart::ArgThreadTime), *ThreadWaitTime))
			{
				UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Argument %s not available"), DisplayClusterClusterSyncStrings::WaitForGameStart::ArgThreadTime);
			}
		}

		if (BarrierWaitTime)
		{
			if (!Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, FString(DisplayClusterClusterSyncStrings::WaitForGameStart::ArgBarrierTime), *BarrierWaitTime))
			{
				UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Argument %s not available"), DisplayClusterClusterSyncStrings::WaitForGameStart::ArgBarrierTime);
			}
		}
	}
}

void FDisplayClusterClusterSyncClient::WaitForFrameStart(double* ThreadWaitTime, double* BarrierWaitTime)
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::WaitForFrameStart::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	TSharedPtr<FDisplayClusterPacketInternal> Response = SendRecvPacket(Request);

	if (Response.IsValid())
	{
		if (ThreadWaitTime)
		{
			if (!Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, FString(DisplayClusterClusterSyncStrings::WaitForFrameStart::ArgThreadTime), *ThreadWaitTime))
			{
				UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Argument %s not available"), DisplayClusterClusterSyncStrings::WaitForFrameStart::ArgThreadTime);
			}
		}

		if (BarrierWaitTime)
		{
			if (!Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, FString(DisplayClusterClusterSyncStrings::WaitForFrameStart::ArgBarrierTime), *BarrierWaitTime))
			{
				UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Argument %s not available"), DisplayClusterClusterSyncStrings::WaitForFrameStart::ArgBarrierTime);
			}
		}
	}
}

void FDisplayClusterClusterSyncClient::WaitForFrameEnd(double* ThreadWaitTime, double* BarrierWaitTime)
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::WaitForFrameEnd::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	TSharedPtr<FDisplayClusterPacketInternal> Response = SendRecvPacket(Request);

	if (Response.IsValid())
	{
		if (ThreadWaitTime)
		{
			if (!Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, FString(DisplayClusterClusterSyncStrings::WaitForFrameEnd::ArgThreadTime), *ThreadWaitTime))
			{
				UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Argument %s not available"), DisplayClusterClusterSyncStrings::WaitForFrameEnd::ArgThreadTime);
			}
		}

		if (BarrierWaitTime)
		{
			if (!Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, FString(DisplayClusterClusterSyncStrings::WaitForFrameEnd::ArgBarrierTime), *BarrierWaitTime))
			{
				UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Argument %s not available"), DisplayClusterClusterSyncStrings::WaitForFrameEnd::ArgBarrierTime);
			}
		}
	}
}

void FDisplayClusterClusterSyncClient::GetDeltaTime(float& DeltaSeconds)
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::GetDeltaTime::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	TSharedPtr<FDisplayClusterPacketInternal> Response = SendRecvPacket(Request);

	if (Response.IsValid())
	{
		// Extract sync data from response packet
		FString StrDeltaSeconds;
		if (Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetDeltaTime::ArgDeltaSeconds, StrDeltaSeconds) == false)
		{
			UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("Couldn't extract an argument: %s"), DisplayClusterClusterSyncStrings::GetDeltaTime::ArgDeltaSeconds);
			return;
		}

		// Convert from hex string to float
		DeltaSeconds = DisplayClusterTypesConverter::template FromHexString<float>(StrDeltaSeconds);
	}
}

void FDisplayClusterClusterSyncClient::GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime)
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::GetFrameTime::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	TSharedPtr<FDisplayClusterPacketInternal> Response = SendRecvPacket(Request);

	if (Response.IsValid())
	{
		FrameTime.Reset();

		// Extract sync data from response packet
		bool bIsValid = false;
		if (Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetFrameTime::ArgIsValid, bIsValid) == false)
		{
			UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("Couldn't extract an argument: %s"), DisplayClusterClusterSyncStrings::GetFrameTime::ArgIsValid);
		}

		if (bIsValid)
		{
			FQualifiedFrameTime NewFrameTime;
			if (Response->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetFrameTime::ArgFrameTime, NewFrameTime) == false)
			{
				UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("Couldn't extract an argument: %s"), DisplayClusterClusterSyncStrings::GetFrameTime::ArgFrameTime);
			}

			FrameTime = NewFrameTime;
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
	
	TSharedPtr<FDisplayClusterPacketInternal> Response = SendRecvPacket(Request);

	if (Response.IsValid())
	{
		// Extract data from response packet
		SyncData = Response->GetTextArgs(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory);
	}
}

void FDisplayClusterClusterSyncClient::GetInputData(TMap<FString, FString>& InputData)
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::GetInputData::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	TSharedPtr<FDisplayClusterPacketInternal> Response = SendRecvPacket(Request);

	if (Response.IsValid())
	{
		// Extract data from response packet
		InputData = Response->GetTextArgs(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory);
	}
}

void FDisplayClusterClusterSyncClient::GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary>>& BinaryEvents)
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterClusterSyncStrings::GetEventsData::Name,
			DisplayClusterClusterSyncStrings::TypeRequest,
			DisplayClusterClusterSyncStrings::ProtocolName)
	);

	TSharedPtr<FDisplayClusterPacketInternal> Response = SendRecvPacket(Request);

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

	TSharedPtr<FDisplayClusterPacketInternal> Response = SendRecvPacket(Request);

	if (Response.IsValid())
	{
		// Extract data from response packet
		NativeInputData = Response->GetTextArgs(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory);
	}
}
