// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRemoteControlInterceptor.h"
#include "DisplayClusterRemoteControlInterceptorLog.h"

#include "IDisplayCluster.h"
#include "Cluster/DisplayClusterClusterEvent.h"

#include "Backends/CborStructDeserializerBackend.h"
#include "Backends/JsonStructDeserializerBackend.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "UObject/FieldPath.h"

#include "Features/IModularFeatures.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"


// Data interception source
static TAutoConsoleVariable<int32> CVarInterceptOnMasterOnly(
	TEXT("nDisplay.RemoteControlInterceptor.MasterOnly"),
	1,
	TEXT("RemoteControl commands interception location\n")
	TEXT("0 : All nodes\n")
	TEXT("1 : Master only\n")
	,
	ECVF_ReadOnly
);

// Magic numbers for now. Unfortunately there is no any ID management for binary events yet.
// This will be refactored once we have some global events registry to prevent any ID conflicts.
const int32 EventId_SetObjectProperties		= 0xaabb0701;
const int32 EventId_ResetObjectProperties	= 0xaabb0702;
const int32 EventId_InvokeCall				= 0xaabb0703;


FDisplayClusterRemoteControlInterceptor::FDisplayClusterRemoteControlInterceptor()
	: bInterceptOnMasterOnly(CVarInterceptOnMasterOnly.GetValueOnGameThread() == 1)
	, bForceApply(false)
{
	bForceApply = FParse::Param(FCommandLine::Get(), TEXT("ClusterForceApplyResponse"));
	
	// Set up cluster events handler
	EventsListener.BindRaw(this, &FDisplayClusterRemoteControlInterceptor::OnClusterEventBinaryHandler);
	// Subscribe for cluster events
	IDisplayCluster::Get().GetClusterMgr()->AddClusterEventBinaryListener(EventsListener);

	UE_LOG(LogDisplayClusterRemoteControlInterceptor, Log, TEXT("DisplayClusterRemoteControlInterceptor has been registered"));
}

FDisplayClusterRemoteControlInterceptor::~FDisplayClusterRemoteControlInterceptor()
{
	// Unsubscribe from cluster events
	IDisplayCluster::Get().GetClusterMgr()->RemoveClusterEventBinaryListener(EventsListener);
	// Unbind cluster events handler
	EventsListener.Unbind();

	UE_LOG(LogDisplayClusterRemoteControlInterceptor, Log, TEXT("DisplayClusterRemoteControlInterceptor has been unregistered"));
}

ERCIResponse FDisplayClusterRemoteControlInterceptor::SetObjectProperties(FRCIPropertiesMetadata& InProperties)
{
	// Serialize command data to binary
	TArray<uint8> Buffer;
	FMemoryWriter MemoryWriter(Buffer);
	MemoryWriter << InProperties;

	// Replicate data
	static const FString EventName = FString(TEXT("SetObjectProperties"));
	EmitReplicationEvent(EventId_SetObjectProperties, Buffer, EventName);

	if (bForceApply)
	{
		return ERCIResponse::Apply;
	}

	return ERCIResponse::Intercept;
}

ERCIResponse FDisplayClusterRemoteControlInterceptor::ResetObjectProperties(FRCIObjectMetadata& InObject)
{
	// Serialize command data to binary
	TArray<uint8> Buffer;
	FMemoryWriter MemoryWiter(Buffer);
	MemoryWiter << InObject;

	// Replicate data
	static const FString EventName = FString(TEXT("ResetObjectProperties"));
	EmitReplicationEvent(EventId_ResetObjectProperties, Buffer, EventName);

	if (bForceApply)
	{
		return ERCIResponse::Apply;
	}

	return ERCIResponse::Intercept;
}

ERCIResponse FDisplayClusterRemoteControlInterceptor::InvokeCall(FRCIFunctionMetadata& InFunction)
{
	// Serialize command data to binary
	TArray<uint8> Buffer;
	FMemoryWriter MemoryWiter(Buffer);
	MemoryWiter << InFunction;

	// Replicate data
	static const FString EventName = FString(TEXT("InvokeCall"));
	EmitReplicationEvent(EventId_InvokeCall, Buffer, EventName);

	if (bForceApply)
	{
		return ERCIResponse::Apply;
	}
	
	return ERCIResponse::Intercept;
}

void FDisplayClusterRemoteControlInterceptor::EmitReplicationEvent(int32 EventId, TArray<uint8>& Buffer, const FString& EventName)
{
	UE_LOG(LogDisplayClusterRemoteControlInterceptor, VeryVerbose, TEXT("Sending replication event %s (0x%x): %d bytes"), *EventName, EventId, Buffer.Num());

	// Cluster event instance
	FDisplayClusterClusterEventBinary Event;

	// Fill the event with data
	Event.EventId                = EventId;
	Event.bIsSystemEvent         = true;
	Event.bShouldDiscardOnRepeat = false;
	Event.EventData              = MoveTemp(Buffer);

	// Emit cluster event (or not, it depends on the bInterceptOnMasterOnly value and the role of this cluster node)
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventBinary(Event, bInterceptOnMasterOnly);
}

void FDisplayClusterRemoteControlInterceptor::OnClusterEventBinaryHandler(const FDisplayClusterClusterEventBinary& Event)
{
	// Dispatch data to a proper handler
	if (Event.bIsSystemEvent)
	{
		switch (Event.EventId)
		{
		case EventId_SetObjectProperties:
			OnReplication_SetObjectProperties(Event.EventData);
			break;

		case EventId_ResetObjectProperties:
			OnReplication_ResetObjectProperties(Event.EventData);
			break;

		case EventId_InvokeCall:
			OnReplication_InvokeCall(Event.EventData);
			break;

		default:
			// Unsupported event, skipping it
			break;
		}
	}
}

void FDisplayClusterRemoteControlInterceptor::OnReplication_SetObjectProperties(const TArray<uint8>& Buffer)
{
	UE_LOG(LogDisplayClusterRemoteControlInterceptor, VeryVerbose, TEXT("Processing replication event SetObjectProperties (0x%d): %d bytes"), EventId_SetObjectProperties, Buffer.Num());

	// Deserialize command data
	FMemoryReader MemoryReader(Buffer);
	FRCIPropertiesMetadata PropsMetadata;
	MemoryReader << PropsMetadata;

	// Initialization
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	const FName ProcessorFeatureName = IRemoteControlInterceptionFeatureProcessor::GetName();
	const int32 ProcessorsAmount = ModularFeatures.GetModularFeatureImplementationCount(IRemoteControlInterceptionFeatureProcessor::GetName());

	// Send the command to the processor(s)
	for (int32 ProcessorIdx = 0; ProcessorIdx < ProcessorsAmount; ++ProcessorIdx)
	{
		IRemoteControlInterceptionFeatureProcessor* const Processor = static_cast<IRemoteControlInterceptionFeatureProcessor*>(ModularFeatures.GetModularFeatureImplementation(ProcessorFeatureName, ProcessorIdx));
		if (Processor)
		{
			Processor->SetObjectProperties(PropsMetadata);
		}
	}
}

void FDisplayClusterRemoteControlInterceptor::OnReplication_ResetObjectProperties(const TArray<uint8>& Buffer)
{
	UE_LOG(LogDisplayClusterRemoteControlInterceptor, VeryVerbose, TEXT("Processing replication event ResetObjectProperties (0x%d): %d bytes"), EventId_ResetObjectProperties, Buffer.Num());

	// Deserialize command data
	FMemoryReader MemoryReader(Buffer);
	FRCIObjectMetadata ObjectMetadata;
	MemoryReader << ObjectMetadata;

	// Initialization
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	const FName ProcessorFeatureName = IRemoteControlInterceptionFeatureProcessor::GetName();
	const int32 ProcessorsAmount = ModularFeatures.GetModularFeatureImplementationCount(IRemoteControlInterceptionFeatureProcessor::GetName());

	// Send the command to the processor(s)
	for (int32 ProcessorIdx = 0; ProcessorIdx < ProcessorsAmount; ++ProcessorIdx)
	{
		IRemoteControlInterceptionFeatureProcessor* const Processor = static_cast<IRemoteControlInterceptionFeatureProcessor*>(ModularFeatures.GetModularFeatureImplementation(ProcessorFeatureName, ProcessorIdx));
		if (Processor)
		{
			Processor->ResetObjectProperties(ObjectMetadata);
		}
	}
}

void FDisplayClusterRemoteControlInterceptor::OnReplication_InvokeCall(const TArray<uint8>& Buffer)
{
	UE_LOG(LogDisplayClusterRemoteControlInterceptor, VeryVerbose, TEXT("Processing replication event InvokeCall (0x%d): %d bytes"), EventId_InvokeCall, Buffer.Num());

	// Deserialize command data
	FMemoryReader MemoryReader(Buffer);
	FRCIFunctionMetadata FunctionMetadata;
	MemoryReader << FunctionMetadata;

	// Initialization
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	const FName ProcessorFeatureName = IRemoteControlInterceptionFeatureProcessor::GetName();
	const int32 ProcessorsAmount = ModularFeatures.GetModularFeatureImplementationCount(IRemoteControlInterceptionFeatureProcessor::GetName());

	// Send the command to the processor(s)
	for (int32 ProcessorIdx = 0; ProcessorIdx < ProcessorsAmount; ++ProcessorIdx)
	{
		IRemoteControlInterceptionFeatureProcessor* const Processor = static_cast<IRemoteControlInterceptionFeatureProcessor*>(ModularFeatures.GetModularFeatureImplementation(ProcessorFeatureName, ProcessorIdx));
		if (Processor)
		{
			Processor->InvokeCall(FunctionMetadata);
		}
	}
}
