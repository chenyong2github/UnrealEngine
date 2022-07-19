// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDeformerInstance.h"

#include "Components/MeshComponent.h"
#include "DataInterfaces/OptimusDataInterfaceGraph.h"
#include "OptimusComputeGraph.h"
#include "OptimusDataTypeRegistry.h"
#include "OptimusDeformer.h"
#include "OptimusVariableDescription.h"
#include "RenderGraphBuilder.h"


/** Container for a pooled buffer. */
struct FOptimusPersistentStructuredBuffer
{
	TRefCountPtr<FRDGPooledBuffer> PooledBuffer;
	int32 ElementStride = 0;
	int32 ElementCount = 0;
};


void FOptimusPersistentBufferPool::GetResourceBuffers(
	FRDGBuilder& GraphBuilder,
	FName InResourceName,
	int32 InElementStride,
	TArray<int32> const& InElementCounts,
	TArray<FRDGBufferRef>& OutBuffers)
{
	OutBuffers.Reset();

	TArray<FOptimusPersistentStructuredBuffer>* ResourceBuffersPtr = ResourceBuffersMap.Find(InResourceName);
	if (ResourceBuffersPtr == nullptr)
	{
		// Create pooled buffers and store.
		TArray<FOptimusPersistentStructuredBuffer> ResourceBuffers;
		ResourceBuffers.Reserve(InElementCounts.Num());

		for (int32 Index = 0; Index < InElementCounts.Num(); Index++)
		{
			FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(InElementStride, InElementCounts[Index]);
			FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(BufferDesc, TEXT("FOptimusPersistentBuffer"), ERDGBufferFlags::None);
			OutBuffers.Add(Buffer);

			FOptimusPersistentStructuredBuffer& PersistentBuffer = ResourceBuffers.AddDefaulted_GetRef();
			PersistentBuffer.ElementStride = InElementStride;
			PersistentBuffer.ElementCount = InElementCounts[Index];
			PersistentBuffer.PooledBuffer = GraphBuilder.ConvertToExternalBuffer(Buffer);
		}

		ResourceBuffersMap.Add(InResourceName, MoveTemp(ResourceBuffers));
	}
	else
	{
		// Verify that the buffers are correct based on the incoming information. 
		// If there's a mismatch, then something has gone wrong upstream.
		// Maybe either duplicated names, missing resource clearing on recompile, or something else.
		if (!ensure(ResourceBuffersPtr->Num() == InElementCounts.Num()))
		{
			return;
		}

		for (int32 Index = 0; Index < ResourceBuffersPtr->Num(); Index++)
		{
			FOptimusPersistentStructuredBuffer& PersistentBuffer = (*ResourceBuffersPtr)[Index];
			if (!ensure(PersistentBuffer.PooledBuffer.IsValid()) ||
				!ensure(PersistentBuffer.ElementStride == InElementStride) ||
				!ensure(PersistentBuffer.ElementCount == InElementCounts[Index]))
			{
				OutBuffers.Reset();
				return;
			}	

			// Register buffer back into the graph and return it.
			FRDGBufferRef Buffer = GraphBuilder.RegisterExternalBuffer(PersistentBuffer.PooledBuffer);
			OutBuffers.Add(Buffer);
		}
	}
}

void FOptimusPersistentBufferPool::ReleaseResources()
{
	check(IsInRenderingThread());
	ResourceBuffersMap.Reset();
}


void UOptimusDeformerInstance::SetMeshComponent(UMeshComponent* InMeshComponent)
{ 
	MeshComponent = InMeshComponent;
}


void UOptimusDeformerInstance::SetupFromDeformer(UOptimusDeformer* InDeformer)
{
	// If we're doing a recompile, ditch all stored render resources.
	ReleaseResources();

	// Create the persistent buffer pool
	BufferPool = MakeShared<FOptimusPersistentBufferPool>();
	
	// (Re)Create and bind data providers.
	ComputeGraphExecInfos.Reset();
	GraphsToRunOnNextTick.Reset();

	for (const FOptimusComputeGraphInfo& ComputeGraphInfo : InDeformer->ComputeGraphs)
	{
		FOptimusDeformerInstanceExecInfo& Info = ComputeGraphExecInfos.AddDefaulted_GetRef();
		Info.GraphName = ComputeGraphInfo.GraphName;
		Info.GraphType = ComputeGraphInfo.GraphType;
		Info.ComputeGraph = ComputeGraphInfo.ComputeGraph;

		// Assume a single binding object that is our UMeshComponent.
		// We will extend that to multiple binding objects later.
		Info.ComputeGraphInstance.CreateDataProviders(Info.ComputeGraph, 0, MeshComponent.Get());

		// Schedule the setup graph to run.
		if (Info.GraphType == EOptimusNodeGraphType::Setup)
		{
			GraphsToRunOnNextTick.Add(Info.GraphName);
		}
	}
	
	// Create local storage for deformer graph variables.
	Variables = NewObject<UOptimusVariableContainer>(this);
	Variables->Descriptions.Reserve(InDeformer->GetVariables().Num());
	TSet<const UOptimusVariableDescription*> Visited;
	for (const UOptimusVariableDescription* VariableDescription : InDeformer->GetVariables())
	{
		if (!VariableDescription)
			continue;
		if (Visited.Contains(VariableDescription))
			continue;
		Visited.Add(VariableDescription);
		UOptimusVariableDescription* VariableDescriptionCopy = NewObject<UOptimusVariableDescription>();
		VariableDescriptionCopy->Guid = VariableDescription->Guid;
		VariableDescriptionCopy->VariableName = VariableDescription->VariableName;
		VariableDescriptionCopy->DataType = VariableDescription->DataType;
		VariableDescriptionCopy->ValueData = VariableDescription->ValueData;
		VariableDescriptionCopy->ResetValueDataSize();
		Variables->Descriptions.Add(VariableDescriptionCopy);
	}

	if (UMeshComponent* Ptr = MeshComponent.Get())
	{
		Ptr->MarkRenderDynamicDataDirty();
	}
}


void UOptimusDeformerInstance::SetCanBeActive(bool bInCanBeActive)
{
	bCanBeActive = bInCanBeActive;
}

void UOptimusDeformerInstance::AllocateResources()
{
	
}


void UOptimusDeformerInstance::ReleaseResources()
{
	if (BufferPool)
	{
		ENQUEUE_RENDER_COMMAND(FOptimusReleasePoolMemory)(
			[BufferPool=MoveTemp(BufferPool)](FRHICommandListImmediate& InCmdList)
			{
				BufferPool->ReleaseResources();
			});
	}
}


bool UOptimusDeformerInstance::IsActive() const
{
	if (!bCanBeActive)
	{
		return false;
	}
		
	for (const FOptimusDeformerInstanceExecInfo& Info: ComputeGraphExecInfos)
	{
		if (!Info.ComputeGraphInstance.ValidateDataProviders(Info.ComputeGraph))
		{
			return false;
		}
	}
	return !ComputeGraphExecInfos.IsEmpty();
}

void UOptimusDeformerInstance::EnqueueWork(FSceneInterface* InScene, EWorkLoad InWorkLoadType, FName InOwnerName)
{
	TSet<FName> GraphsToRun;
	{
		UE::TScopeLock<FCriticalSection> Lock(GraphsToRunOnNextTickLock);
		Swap(GraphsToRunOnNextTick, GraphsToRun);
	}
	
	for (FOptimusDeformerInstanceExecInfo& Info: ComputeGraphExecInfos)
	{
		if (Info.GraphType == EOptimusNodeGraphType::Update || GraphsToRun.Contains(Info.GraphName))
		{
			Info.ComputeGraphInstance.EnqueueWork(Info.ComputeGraph, InScene, InOwnerName);
		}
	}
}

namespace
{
	template <typename T>
	bool SetVariableValue(UOptimusVariableContainer const* InVariables, FName InVariableName, FName InTypeName, T const& InValue)
	{
		const uint8* ValueBytes = reinterpret_cast<const uint8*>(&InValue);

		FOptimusDataTypeHandle WantedType = FOptimusDataTypeRegistry::Get().FindType(InTypeName);
		for (UOptimusVariableDescription* VariableDesc : InVariables->Descriptions)
		{
			if (VariableDesc->VariableName == InVariableName && VariableDesc->DataType == WantedType)
			{
				TUniquePtr<FProperty> Property(WantedType->CreateProperty(nullptr, NAME_None));
				if (ensure(Property->GetSize() == sizeof(T)) &&
					ensure(Property->GetSize() == VariableDesc->ValueData.Num()))
				{
					FPlatformMemory::Memcpy(VariableDesc->ValueData.GetData(), ValueBytes, sizeof(T));
				}

				return true;
			}
		}

		return false;
	}
}


bool UOptimusDeformerInstance::SetBoolVariable(FName InVariableName, bool InValue)
{
	return SetVariableValue(Variables, InVariableName, FBoolProperty::StaticClass()->GetFName(), InValue);
}

bool UOptimusDeformerInstance::SetIntVariable(FName InVariableName, int32 InValue)
{
	return SetVariableValue<int32>(Variables, InVariableName, FIntProperty::StaticClass()->GetFName(), InValue);
}

bool UOptimusDeformerInstance::SetFloatVariable(FName InVariableName, float InValue)
{
	return SetVariableValue<float>(Variables, InVariableName, FFloatProperty::StaticClass()->GetFName(), InValue);
}

bool UOptimusDeformerInstance::SetVectorVariable(FName InVariableName, const FVector& InValue)
{
	return SetVariableValue<FVector>(Variables, InVariableName, "FVector", InValue);
}

bool UOptimusDeformerInstance::SetVector4Variable(FName InVariableName, const FVector4& InValue)
{
	return SetVariableValue<FVector4>(Variables, InVariableName, "FVector4", InValue);
}

const TArray<UOptimusVariableDescription*>& UOptimusDeformerInstance::GetVariables() const
{
	return Variables->Descriptions;
}


bool UOptimusDeformerInstance::EnqueueTriggerGraph(FName InTriggerGraphName)
{
	for(FOptimusDeformerInstanceExecInfo& ExecInfo: ComputeGraphExecInfos)
	{
		if (ExecInfo.GraphType == EOptimusNodeGraphType::ExternalTrigger && ExecInfo.GraphName == InTriggerGraphName)
		{
			UE::TScopeLock<FCriticalSection> Lock(GraphsToRunOnNextTickLock);
			GraphsToRunOnNextTick.Add(ExecInfo.GraphName);
			return true;
		}
	}
	
	return false;
}


void UOptimusDeformerInstance::SetConstantValueDirect(FString const& InVariableName, TArray<uint8> const& InValue)
{
	// Poke constants into the UGraphDataProvider objects.
	// This is an editor only operation when constant nodes are edited in the graph and we want to see the result without a full compile step.
	// Not sure that this is the best approach, or whether to store constants on the UOptimusDeformerInstance like variables?
	for (FOptimusDeformerInstanceExecInfo& ExecInfo : ComputeGraphExecInfos)
	{
		TArray< TObjectPtr<UComputeDataProvider> >& DataProviders = ExecInfo.ComputeGraphInstance.GetDataProviders();
		for (UComputeDataProvider* DataProvider : DataProviders)
		{
			if (UOptimusGraphDataProvider* GraphDataProvider = Cast<UOptimusGraphDataProvider>(DataProvider))
			{
				GraphDataProvider->SetConstant(InVariableName, InValue);
				break;
			}
		}
	}
}
