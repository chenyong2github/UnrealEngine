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

FOptimusDeformerInstanceExecInfo::FOptimusDeformerInstanceExecInfo()
{
	GraphType = EOptimusNodeGraphType::Update;
}


void UOptimusDeformerInstanceSettings::RefreshComponentBindings(
	UOptimusDeformer* InDeformer,
	UMeshComponent* InMeshComponent
	)
{
	
	// Try to retain existing component binding as much as possible if called after a compilation or component rebuild
	TMap<FName, UActorComponent*> ExistingBindings;

	for (FOptimusDeformerInstanceComponentBinding& Binding: Bindings)
	{
		// It's likely that a component we referred to has been nixed, via AActor::RerunConstructionScripts, so
		// we need to re-fetch the object from memory.
		Binding.ActorComponent.ResetWeakPtr();
		ExistingBindings.Add(Binding.ProviderName, Binding.ActorComponent.Get());
	}
	
	AActor* Actor = InMeshComponent->GetOwner();
	TSet<UActorComponent*> ComponentsUsed;

	const TArray<UOptimusComponentSourceBinding*>& ComponentBindings = InDeformer->GetComponentBindings();
	Bindings.Reset(ComponentBindings.Num());
	for (const UOptimusComponentSourceBinding* Binding: ComponentBindings)
	{
		FName BindingName = Binding->BindingName;
		UActorComponent* BoundComponent = nullptr;

		// Primary binding always binds to the mesh component we're applied to.
		if (Binding->IsPrimaryBinding())
		{
			BoundComponent = InMeshComponent;
		}
		else
		{
			// Try an existing binding first and see if they still match by class. We ignore tags for this match
			// because we want to respect the will of the user, unless absolutely not possible (i.e. class mismatch).
			if (ExistingBindings.Contains(BindingName))
			{
				if (UActorComponent* Component = ExistingBindings[BindingName])
				{
					if (Component->IsA(Binding->GetComponentSource()->GetComponentClass()))
					{
						BoundComponent = Component;
					}
				}
			}
			
			// If not, try to find a component owned by this actor that matches the tag and class.
			if (!BoundComponent && !Binding->ComponentTags.IsEmpty())
			{
				TSet<UActorComponent*> TaggedComponents;
				for (FName Tag: Binding->ComponentTags)
				{
					TArray<UActorComponent*> Components = Actor->GetComponentsByTag(Binding->GetComponentSource()->GetComponentClass(), Tag);

					for (UActorComponent* Component: Components)
					{
						if (!ComponentsUsed.Contains(Component))
						{
							TaggedComponents.Add(Component);
						}
					}
				}
				TArray<UActorComponent*> RankedTaggedComponents = TaggedComponents.Array();

				// Rank the components by the number of tags they match.
				RankedTaggedComponents.Sort([Tags=TSet<FName>(Binding->ComponentTags)](const UActorComponent& InCompA, const UActorComponent& InCompB)
				{
					TSet<FName> TagsA(InCompA.ComponentTags);
					TSet<FName> TagsB(InCompB.ComponentTags);
					
					return Tags.Intersect(TagsA).Num() < Tags.Intersect(TagsB).Num();
				});

				if (!RankedTaggedComponents.IsEmpty())
				{
					BoundComponent = RankedTaggedComponents[0];
				}
			}

			// Otherwise just use class matching on components owned by the actor.
			if (!BoundComponent)
			{
				TArray<UActorComponent*> Components;
				Actor->GetComponents(Binding->GetComponentSource()->GetComponentClass(), Components);

				for (UActorComponent* Component: Components)
				{
					if (!ComponentsUsed.Contains(Component))
					{
						BoundComponent = Component;
						break;
					}
				}
			}
		}

		Bindings.Add({BindingName, BoundComponent});
		ComponentsUsed.Add(BoundComponent);
	}
}

TArray<UActorComponent*> UOptimusDeformerInstanceSettings::GetBoundComponents() const
{
	TArray<UActorComponent*> Result;
	for (const FOptimusDeformerInstanceComponentBinding& Binding: Bindings)
	{
		UActorComponent* ActorComponent = Binding.ActorComponent.Get();
		if (ActorComponent)
		{
			Result.Add(ActorComponent);
		}
	}
	return Result;
}


AActor* UOptimusDeformerInstanceSettings::GetActor() const
{
	// We should be owned by an actor at some point.
	return GetTypedOuter<AActor>();
}

UOptimusComponentSourceBinding* UOptimusDeformerInstanceSettings::GetComponentBindingByName(FName InBindingName) const
{
	if (const UOptimusDeformer* DeformerResolved = Deformer.Get())
	{
		for (UOptimusComponentSourceBinding* Binding: DeformerResolved->GetComponentBindings())
		{
			if (Binding->BindingName == InBindingName)
			{
				return Binding;
			}
		}
	}
	return nullptr;
}

#if WITH_EDITOR
void UOptimusDeformerInstanceSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FOptimusDeformerInstanceComponentBinding, ActorComponent))
	{
		// TODO: Update dependencies.
	}
}
#endif

void UOptimusDeformerInstanceSettings::InitializeSettings(
	UOptimusDeformer* InDeformer,
	UMeshComponent* InMeshComponent
	)
{
	Deformer = InDeformer;
	RefreshComponentBindings(InDeformer, InMeshComponent);
}


void UOptimusDeformerInstance::SetMeshComponent(UMeshComponent* InMeshComponent)
{ 
	MeshComponent = InMeshComponent;
}

void UOptimusDeformerInstance::SetInstanceSettings(UOptimusDeformerInstanceSettings* InInstanceSettings)
{
	InstanceSettings = InInstanceSettings; 
}


void UOptimusDeformerInstance::SetupFromDeformer(
	UOptimusDeformer* InDeformer,
	const bool bInRefreshBindings
	)
{
	// If we're doing a recompile, ditch all stored render resources.
	ReleaseResources();

	// Update the component bindings before creating data providers. The bindings are in the same order
	// as the component bindings in the deformer.
	UOptimusDeformerInstanceSettings* InstanceSettingsPtr = InstanceSettings.Get(); 
	if (bInRefreshBindings && InstanceSettingsPtr)
	{
		InstanceSettingsPtr->RefreshComponentBindings(InDeformer, MeshComponent.Get());
	}

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

		if (InstanceSettingsPtr)
		{
			// Fall back on everything being the given component.
			for (int32 Index = 0; Index < InstanceSettingsPtr->Bindings.Num(); Index++)
			{
				Info.ComputeGraphInstance.CreateDataProviders(Info.ComputeGraph, Index, InstanceSettingsPtr->Bindings[Index].ActorComponent.Get());
			}
		}
		else
		{
			// Fall back on everything being the given component.
			for (int32 Index = 0; Index < InDeformer->GetComponentBindings().Num(); Index++)
			{
				Info.ComputeGraphInstance.CreateDataProviders(Info.ComputeGraph, Index, MeshComponent.Get());
			}
		}

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
