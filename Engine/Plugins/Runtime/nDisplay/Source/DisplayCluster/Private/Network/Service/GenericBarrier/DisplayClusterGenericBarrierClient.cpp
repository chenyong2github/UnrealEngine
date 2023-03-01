// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Service/GenericBarrier/DisplayClusterGenericBarrierClient.h"
#include "Network/Service/GenericBarrier/DisplayClusterGenericBarrierStrings.h"
#include "Network/Packet/DisplayClusterPacketInternal.h"
#include "Network/Listener/DisplayClusterHelloMessageStrings.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "Cluster/IPDisplayClusterClusterManager.h"

#include "DisplayClusterEnums.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"
#include "Misc/DisplayClusterTypesConverter.h"

#include "DisplayClusterConfigurationTypes.h"


FDisplayClusterGenericBarrierClient::FDisplayClusterGenericBarrierClient()
	: FDisplayClusterGenericBarrierClient(FString("CLN_GB"))
{
}

FDisplayClusterGenericBarrierClient::FDisplayClusterGenericBarrierClient(const FString& InName)
	: FDisplayClusterClient(InName)
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterClient
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterGenericBarrierClient::Connect(const FString& Address, const uint16 Port, const uint32 ConnectRetriesAmount, const uint32 ConnectRetryDelay)
{
	// First, allow base class to perform connection
	if (!FDisplayClusterClient::Connect(Address, Port, ConnectRetriesAmount, ConnectRetryDelay))
	{
		return false;
	}

	// Prepare 'hello' message
	TSharedPtr<FDisplayClusterPacketInternal> HelloMsg = MakeShared<FDisplayClusterPacketInternal>(
		DisplayClusterHelloMessageStrings::Hello::Name,
		DisplayClusterGenericBarrierStrings::TypeRequest,
		DisplayClusterGenericBarrierStrings::ProtocolName
	);

	// Fill in the message with data
	const FString NodeId = GDisplayCluster->GetPrivateClusterMgr()->GetNodeId();
	HelloMsg->SetTextArg(DisplayClusterHelloMessageStrings::ArgumentsDefaultCategory, DisplayClusterHelloMessageStrings::Hello::ArgNodeId, NodeId);

	// Send message (no response awaiting)
	return SendPacket(HelloMsg);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterGenericBarriersClient
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterGenericBarrierClient::Connect()
{
	// Generic barriers are available in runtime only
	if (!GDisplayCluster || (GDisplayCluster->GetOperationMode() != EDisplayClusterOperationMode::Cluster))
	{
		return false;
	}

	// Now we need to get server address/port data from the config
	if (const IPDisplayClusterConfigManager* const ConfigMgr = GDisplayCluster->GetPrivateConfigMgr())
	{
		if (const UDisplayClusterConfigurationData* const Config = ConfigMgr->GetConfig())
		{
			if (const UDisplayClusterConfigurationClusterNode* const PrimaryNode = Config->Cluster->GetNode(Config->Cluster->PrimaryNode.Id))
			{
				// Now try to establish connection
				return Connect(PrimaryNode->Host, Config->Cluster->PrimaryNode.Ports.ClusterSync, 3, 1000);
			}
		}
	}

	return false;
}

void FDisplayClusterGenericBarrierClient::Disconnect()
{
	// Forward IDisplayClusterGenericBarriersClient::Disconnect() to FDisplayClusterClient::Disconnect() explicitly
	FDisplayClusterClient::Disconnect();
}

bool FDisplayClusterGenericBarrierClient::IsConnected() const
{
	// Forward IDisplayClusterGenericBarriersClient::IsConnected() to FDisplayClusterClient::IsConnected() explicitly
	return FDisplayClusterClient::IsConnected();
}

FString FDisplayClusterGenericBarrierClient::GetName() const
{
	// Forward IDisplayClusterGenericBarriersClient::GetName() to FDisplayClusterClient::GetName() explicitly
	return FDisplayClusterClient::GetName();
}

bool FDisplayClusterGenericBarrierClient::CreateBarrier(const FString& BarrierId, const TArray<FString>& UniqueThreadMarkers, uint32 Timeout)
{
	EBarrierControlResult CtrlResult = EBarrierControlResult::UnknownError;
	const EDisplayClusterCommResult Result = CreateBarrier(BarrierId, UniqueThreadMarkers, Timeout, CtrlResult);
	return (CtrlResult == EBarrierControlResult::CreatedSuccessfully || CtrlResult == EBarrierControlResult::AlreadyExists) && (Result == EDisplayClusterCommResult::Ok);
}

bool FDisplayClusterGenericBarrierClient::WaitUntilBarrierIsCreated(const FString& BarrierId)
{
	EBarrierControlResult CtrlResult = EBarrierControlResult::UnknownError;
	const EDisplayClusterCommResult Result = WaitUntilBarrierIsCreated(BarrierId, CtrlResult);
	return (CtrlResult == EBarrierControlResult::AlreadyExists) && (Result == EDisplayClusterCommResult::Ok);
}

bool FDisplayClusterGenericBarrierClient::IsBarrierAvailable(const FString& BarrierId)
{
	EBarrierControlResult CtrlResult = EBarrierControlResult::UnknownError;
	const EDisplayClusterCommResult Result = IsBarrierAvailable(BarrierId, CtrlResult);
	return (CtrlResult == EBarrierControlResult::AlreadyExists) && (Result == EDisplayClusterCommResult::Ok);
}

bool FDisplayClusterGenericBarrierClient::ReleaseBarrier(const FString& BarrierId)
{
	EBarrierControlResult CtrlResult = EBarrierControlResult::UnknownError;
	const EDisplayClusterCommResult Result = ReleaseBarrier(BarrierId, CtrlResult);
	return (CtrlResult == EBarrierControlResult::ReleasedSuccessfully) && (Result == EDisplayClusterCommResult::Ok);
}

bool FDisplayClusterGenericBarrierClient::Synchronize(const FString& BarrierId, const FString& UniqueThreadMarker)
{
	EBarrierControlResult CtrlResult = EBarrierControlResult::UnknownError;
	const EDisplayClusterCommResult Result = SyncOnBarrier(BarrierId, UniqueThreadMarker, CtrlResult);
	return (CtrlResult == EBarrierControlResult::SynchronizedSuccessfully) && (Result == EDisplayClusterCommResult::Ok);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolGenericBarrier
//////////////////////////////////////////////////////////////////////////////////////////////
EDisplayClusterCommResult FDisplayClusterGenericBarrierClient::CreateBarrier(const FString& BarrierId, const TArray<FString>& UniqueThreadMarkers, uint32 Timeout, EBarrierControlResult& Result)
{
	const TSharedPtr<FDisplayClusterPacketInternal> Request = MakeShared<FDisplayClusterPacketInternal>(
		DisplayClusterGenericBarrierStrings::CreateBarrier::Name,
		DisplayClusterGenericBarrierStrings::TypeRequest,
		DisplayClusterGenericBarrierStrings::ProtocolName);

	// Request arguments
	Request->SetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::ArgBarrierId, BarrierId);
	Request->SetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::CreateBarrier::ArgTimeout, Timeout);

	// Stringify markers array and put as a single parameter
	const FString ThreadMarkersPacked = DisplayClusterHelpers::str::template ArrayToStr(UniqueThreadMarkers, DisplayClusterStrings::common::ArrayValSeparator, false);
	Request->SetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::CreateBarrier::ArgThreadMarkers, ThreadMarkersPacked);

	TSharedPtr<FDisplayClusterPacketInternal> Response;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CLN_GB::CreateBarrier);
		Response = SendRecvPacket(Request);
	}

	if (!Response)
	{
		UE_LOG(LogDisplayClusterNetwork, Warning, TEXT("Network error on '%s'"), *Request->GetName());
		return EDisplayClusterCommResult::NetworkError;
	}

	// Extract response data
	FString CtrlResult;
	Response->GetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::ArgResult, CtrlResult);
	Result = static_cast<EBarrierControlResult>(DisplayClusterTypesConverter::template FromString<uint8>(CtrlResult));

	return Response->GetCommResult();
}

EDisplayClusterCommResult FDisplayClusterGenericBarrierClient::WaitUntilBarrierIsCreated(const FString& BarrierId, EBarrierControlResult& Result)
{
	const TSharedPtr<FDisplayClusterPacketInternal> Request = MakeShared<FDisplayClusterPacketInternal>(
		DisplayClusterGenericBarrierStrings::WaitUntilBarrierIsCreated::Name,
		DisplayClusterGenericBarrierStrings::TypeRequest,
		DisplayClusterGenericBarrierStrings::ProtocolName);

	// Request arguments
	Request->SetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::ArgBarrierId, BarrierId);

	TSharedPtr<FDisplayClusterPacketInternal> Response;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CLN_GB::WaitUntilBarrierIsCreated);
		Response = SendRecvPacket(Request);
	}

	if (!Response)
	{
		UE_LOG(LogDisplayClusterNetwork, Warning, TEXT("Network error on '%s'"), *Request->GetName());
		return EDisplayClusterCommResult::NetworkError;
	}

	// Extract response data
	FString CtrlResult;
	Response->GetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::ArgResult, CtrlResult);
	Result = static_cast<EBarrierControlResult>(DisplayClusterTypesConverter::template FromString<uint8>(CtrlResult));

	return Response->GetCommResult();
}

EDisplayClusterCommResult FDisplayClusterGenericBarrierClient::IsBarrierAvailable(const FString& BarrierId, EBarrierControlResult& Result)
{
	const TSharedPtr<FDisplayClusterPacketInternal> Request = MakeShared<FDisplayClusterPacketInternal>(
		DisplayClusterGenericBarrierStrings::IsBarrierAvailable::Name,
		DisplayClusterGenericBarrierStrings::TypeRequest,
		DisplayClusterGenericBarrierStrings::ProtocolName);

	// Request arguments
	Request->SetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::ArgBarrierId, BarrierId);

	TSharedPtr<FDisplayClusterPacketInternal> Response;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CLN_GB::IsBarrierAvailable);
		Response = SendRecvPacket(Request);
	}

	if (!Response)
	{
		UE_LOG(LogDisplayClusterNetwork, Warning, TEXT("Network error on '%s'"), *Request->GetName());
		return EDisplayClusterCommResult::NetworkError;
	}

	// Extract response data
	FString CtrlResult;
	Response->GetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::ArgResult, CtrlResult);
	Result = static_cast<EBarrierControlResult>(DisplayClusterTypesConverter::template FromString<uint8>(CtrlResult));

	return Response->GetCommResult();
}

EDisplayClusterCommResult FDisplayClusterGenericBarrierClient::ReleaseBarrier(const FString& BarrierId, EBarrierControlResult& Result)
{
	const TSharedPtr<FDisplayClusterPacketInternal> Request = MakeShared<FDisplayClusterPacketInternal>(
		DisplayClusterGenericBarrierStrings::ReleaseBarrier::Name,
		DisplayClusterGenericBarrierStrings::TypeRequest,
		DisplayClusterGenericBarrierStrings::ProtocolName);

	// Request arguments
	Request->SetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::ArgBarrierId, BarrierId);

	TSharedPtr<FDisplayClusterPacketInternal> Response;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CLN_GB::ReleaseBarrier);
		Response = SendRecvPacket(Request);
	}

	if (!Response)
	{
		UE_LOG(LogDisplayClusterNetwork, Warning, TEXT("Network error on '%s'"), *Request->GetName());
		return EDisplayClusterCommResult::NetworkError;
	}

	// Extract response data
	FString CtrlResult;
	Response->GetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::ArgResult, CtrlResult);
	Result = static_cast<EBarrierControlResult>(DisplayClusterTypesConverter::template FromString<uint8>(CtrlResult));

	return Response->GetCommResult();
}

EDisplayClusterCommResult FDisplayClusterGenericBarrierClient::SyncOnBarrier(const FString& BarrierId, const FString& UniqueThreadMarker, EBarrierControlResult& Result)
{
	const TSharedPtr<FDisplayClusterPacketInternal> Request = MakeShared<FDisplayClusterPacketInternal>(
		DisplayClusterGenericBarrierStrings::SyncOnBarrier::Name,
		DisplayClusterGenericBarrierStrings::TypeRequest,
		DisplayClusterGenericBarrierStrings::ProtocolName);

	// Request arguments
	Request->SetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::ArgBarrierId, BarrierId);
	Request->SetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::SyncOnBarrier::ArgThreadMarker, UniqueThreadMarker);

	TSharedPtr<FDisplayClusterPacketInternal> Response;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CLN_GB::SyncOnBarrier);
		Response = SendRecvPacket(Request);
	}

	if (!Response)
	{
		UE_LOG(LogDisplayClusterNetwork, Warning, TEXT("Network error on '%s'"), *Request->GetName());
		return EDisplayClusterCommResult::NetworkError;
	}

	// Extract response data
	FString CtrlResult;
	Response->GetTextArg(DisplayClusterGenericBarrierStrings::ArgumentsDefaultCategory, DisplayClusterGenericBarrierStrings::ArgResult, CtrlResult);
	Result = static_cast<EBarrierControlResult>(DisplayClusterTypesConverter::template FromString<uint8>(CtrlResult));

	return Response->GetCommResult();
}
