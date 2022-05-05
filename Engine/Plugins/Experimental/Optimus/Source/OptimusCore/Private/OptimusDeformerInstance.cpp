// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDeformerInstance.h"

#include "Components/MeshComponent.h"
#include "DataInterfaces/OptimusDataInterfaceGraph.h"
#include "OptimusComputeGraph.h"
#include "OptimusDataTypeRegistry.h"
#include "OptimusDeformer.h"
#include "OptimusVariableDescription.h"


void FOptimusPersistentStructuredBuffer::InitRHI()
{
	if (ElementCount > 0)
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("PersistentStructuredBuffer"));
		Buffer = RHICreateStructuredBuffer(
			ElementStride, ElementStride * ElementCount,
			BUF_ShaderResource | BUF_UnorderedAccess,
			CreateInfo);

		BufferUAV = RHICreateUnorderedAccessView(Buffer, false, false);
	}
}


void FOptimusPersistentStructuredBuffer::ReleaseRHI()
{
	BufferUAV.SafeRelease();
	Buffer.SafeRelease();
}


const TArray<FOptimusPersistentStructuredBufferPtr>& FOptimusPersistentBufferPool::GetResourceBuffers(
	FName InResourceName, 
	int32 InElementStride, 
	TArray<int32> InInvocationElementCount
	)
{
	check(IsInRenderingThread());

	TArray<FOptimusPersistentStructuredBufferPtr>* ResourceBuffersPtr = ResourceBuffersMap.Find(InResourceName);
	if (ResourceBuffersPtr == nullptr)
	{
		TArray<FOptimusPersistentStructuredBufferPtr> ResourceBuffers;

		for (int32 Index = 0; Index < InInvocationElementCount.Num(); Index++)
		{
			FOptimusPersistentStructuredBufferPtr BufferPtr = MakeShared<FOptimusPersistentStructuredBuffer>(InInvocationElementCount[Index], InElementStride);
			BufferPtr->InitRHI();
			ResourceBuffers.Add(BufferPtr);
		}

		return ResourceBuffersMap.Add(InResourceName, MoveTemp(ResourceBuffers));
	}
	else
	{
		static TArray<FOptimusPersistentStructuredBufferPtr> EmptyArray;
		
		// Verify that the buffers are correct based on the incoming information. If there's a
		// mismatch, then something has gone wrong upstream (either duplicated names, missing
		// resource clearing on recompile, or something else).
		if (!ensure(ResourceBuffersPtr->Num() == InInvocationElementCount.Num()))
		{
			return EmptyArray;
		}

		for (int32 Index = 0; Index < ResourceBuffersPtr->Num(); Index++)
		{
			FOptimusPersistentStructuredBufferPtr BufferPtr = (*ResourceBuffersPtr)[Index];
			if (!ensure(BufferPtr.IsValid()) ||
				!ensure(BufferPtr->GetElementStride() == InElementStride) ||
				!ensure(BufferPtr->GetElementCount() == InInvocationElementCount[Index]))
			{
				return EmptyArray;
			}	
		}

		return *ResourceBuffersPtr;
	}
}


void FOptimusPersistentBufferPool::ReleaseResources()
{
	check(IsInRenderingThread());

	for (TTuple<FName, TArray<FOptimusPersistentStructuredBufferPtr>>& ResourceBuffersInfo: ResourceBuffersMap)
	{
		for (const FOptimusPersistentStructuredBufferPtr& BufferPtr: ResourceBuffersInfo.Value)
		{
			BufferPtr->ReleaseRHI();
		}
	}

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
		TArray<UObject*, TInlineAllocator<1>> BindingObjects;
		BindingObjects.Add(MeshComponent.Get());

		FOptimusDeformerInstanceExecInfo Info;
		Info.GraphName = ComputeGraphInfo.GraphName;
		Info.GraphType = ComputeGraphInfo.GraphType;
		// Schedule the setup and update graphs to run. The flag for the update graph is never
		// cleared.
		if (Info.GraphType == EOptimusNodeGraphType::Setup)
		{
			GraphsToRunOnNextTick.Add(Info.GraphName);
		}
		Info.ComputeGraph = ComputeGraphInfo.ComputeGraph;
		Info.ComputeGraphInstance.CreateDataProviders(Info.ComputeGraph, BindingObjects);
		ComputeGraphExecInfos.Add(Info);
	}
	
	// Create local storage for deformer graph variables.
	Variables = NewObject<UOptimusVariableContainer>(this);
	Variables->Descriptions.Reserve(InDeformer->GetVariables().Num());
	for (const UOptimusVariableDescription* VariableDescription : InDeformer->GetVariables())
	{
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
