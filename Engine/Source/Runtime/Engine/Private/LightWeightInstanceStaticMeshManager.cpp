// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/LightWeightInstanceStaticMeshManager.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

ALightWeightInstanceStaticMeshManager::ALightWeightInstanceStaticMeshManager(const FObjectInitializer& ObjectInitializer)
{
	InstancedStaticMeshComponent = ObjectInitializer.CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(this, TEXT("InstancedStaticMeshComponent0"));

	if (StaticMesh.IsValid())
	{
		OnStaticMeshSet();
	}
	SetInstancedStaticMeshParams();

	RootComponent = InstancedStaticMeshComponent;
	AddInstanceComponent(InstancedStaticMeshComponent);
	if (GetWorld())
	{
		InstancedStaticMeshComponent->RegisterComponent();
	}
}

void ALightWeightInstanceStaticMeshManager::SetRepresentedClass(UClass* ActorClass)
{
	Super::SetRepresentedClass(ActorClass);

	AActor* ActorCDO = Cast<AActor>(RepresentedClass->GetDefaultObject());
	if (ActorCDO)
	{
		BaseInstanceName = ActorCDO->GetName();
		SetStaticMeshFromActor(ActorCDO);
	}
	else
	{
		BaseInstanceName.Reset();
		ClearStaticMesh();
	}

	if (InstancedStaticMeshComponent)
	{
		InstancedStaticMeshComponent->OnPostLoadPerInstanceData();
	}
}

int32 ALightWeightInstanceStaticMeshManager::ConvertCollisionIndexToLightWeightIndex(int32 InIndex) const
{
	return RenderingIndicesToDataIndices[InIndex];
}

void ALightWeightInstanceStaticMeshManager::AddNewInstanceAt(FLWIData* InitData, int32 Index)
{
	Super::AddNewInstanceAt(InitData, Index);

	// The rendering indices are tightly packed so we know it's going on the end of the array
	const int32 RenderingIdx = RenderingIndicesToDataIndices.Add(Index);

	// Now that we know the rendering index we can fill in the other side of the map
	if (Index >= DataIndicesToRenderingIndices.Num())
	{
		ensure(Index == DataIndicesToRenderingIndices.Add(RenderingIdx));
	}
	else
	{
		DataIndicesToRenderingIndices[Index] = RenderingIdx;
	}

	// Update the HISMC
	if (InstancedStaticMeshComponent)
	{
		ensure(RenderingIdx == InstancedStaticMeshComponent->AddInstanceWorldSpace(InitData->Transform));
	}
}

void ALightWeightInstanceStaticMeshManager::RemoveInstance(const int32 Index)
{
	RemoveInstanceFromRendering(Index);
	Super::RemoveInstance(Index);
}

void ALightWeightInstanceStaticMeshManager::RemoveInstanceFromRendering(int32 DataIndex)
{
	if (IsIndexValid(DataIndex))
	{
		const int32 RenderingIndex = DataIndicesToRenderingIndices[DataIndex];
		DataIndicesToRenderingIndices[DataIndex] = INDEX_NONE;

		if (RenderingIndex != INDEX_NONE)
		{
			if (InstancedStaticMeshComponent)
			{
				InstancedStaticMeshComponent->RemoveInstance(RenderingIndex);
			}

			// update the map to match what is now in the instanced static mesh component
			RenderingIndicesToDataIndices.RemoveAtSwap(RenderingIndex);

			// fix up the other side of the map to match the change we just made
			// if we removed the last element than nothing was moved so we're done
			if (RenderingIndex < RenderingIndicesToDataIndices.Num())
			{
				// find the data index that corresponds with the changed rendering index
				const int32 ShiftedDataIndex = RenderingIndicesToDataIndices[RenderingIndex];

				DataIndicesToRenderingIndices[ShiftedDataIndex] = RenderingIndex;
			}
		}
	}
}

void ALightWeightInstanceStaticMeshManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ALightWeightInstanceStaticMeshManager, StaticMesh);
	DOREPLIFETIME(ALightWeightInstanceStaticMeshManager, RenderingIndicesToDataIndices);
	DOREPLIFETIME(ALightWeightInstanceStaticMeshManager, DataIndicesToRenderingIndices);
}

void ALightWeightInstanceStaticMeshManager::OnRep_StaticMesh()
{
	OnStaticMeshSet();
}

void ALightWeightInstanceStaticMeshManager::OnRep_Transforms()
{
	Super::OnRep_Transforms();

	if (InstancedStaticMeshComponent)
	{
		InstancedStaticMeshComponent->AddInstanceWorldSpace(InstanceTransforms.Last());
	}
}

void ALightWeightInstanceStaticMeshManager::PostActorSpawn(const FActorInstanceHandle& Handle)
{
	Super::PostActorSpawn(Handle);

	// remove the rendered instance from the HISMC
	RemoveInstanceFromRendering(Handle.GetInstanceIndex());
}

void ALightWeightInstanceStaticMeshManager::SetInstancedStaticMeshParams()
{
	FName CollisionProfileName(TEXT("LightWeightInstancedStaticMeshPhysics"));
	InstancedStaticMeshComponent->SetCollisionProfileName(CollisionProfileName);

	InstancedStaticMeshComponent->CanCharacterStepUpOn = ECB_Owner;
	InstancedStaticMeshComponent->CastShadow = true;
	InstancedStaticMeshComponent->bCastDynamicShadow = true;
	InstancedStaticMeshComponent->bCastStaticShadow = true;
	InstancedStaticMeshComponent->PrimaryComponentTick.bCanEverTick = false;
	// Allows updating in game, while optimizing rendering for the case that it is not modified
	InstancedStaticMeshComponent->Mobility = EComponentMobility::Movable;
	// Allows per-instance selection in the editor
	InstancedStaticMeshComponent->bHasPerInstanceHitProxies = true;
}

void ALightWeightInstanceStaticMeshManager::SetStaticMeshFromActor(AActor* InActor)
{
	ensureMsgf(false, TEXT("ALightWeightInstanceManager::SetStaticMeshFromActor was called on %s. Projects should override this function in subclasses."), *GetNameSafe(InActor));
	ClearStaticMesh();
}

void ALightWeightInstanceStaticMeshManager::OnStaticMeshSet()
{
	if (InstancedStaticMeshComponent)
	{
		EComponentMobility::Type Mobility = InstancedStaticMeshComponent->Mobility;
		if (Mobility == EComponentMobility::Static)
		{
			InstancedStaticMeshComponent->SetMobility(EComponentMobility::Stationary);
			InstancedStaticMeshComponent->SetStaticMesh(StaticMesh.Get());
			InstancedStaticMeshComponent->SetMobility(Mobility);
		}
		else
		{
			InstancedStaticMeshComponent->SetStaticMesh(StaticMesh.Get());
		}

		UStaticMesh* Mesh = StaticMesh.Get();
		for (int32 Idx = 0; Idx < Mesh->GetStaticMaterials().Num(); ++Idx)
		{
			InstancedStaticMeshComponent->SetMaterial(Idx, Mesh->GetMaterial(Idx));
		}
	}
}

void ALightWeightInstanceStaticMeshManager::ClearStaticMesh()
{
	StaticMesh = nullptr;
	OnStaticMeshSet();
}