// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FoliageActor.h"
#include "InstancedFoliageActor.h"
#include "FoliageType_Actor.h"
#include "FoliageHelper.h"
#include "Engine/Engine.h"

//
//
// FFoliageActor
void FFoliageActor::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	for (AActor*& Actor : ActorInstances)
	{
		if (Actor != nullptr)
		{
			Collector.AddReferencedObject(Actor, InThis);
		}
	}
}

void FFoliageActor::Serialize(FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	if (Ar.CustomVer(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageActorSupportNoWeakPtr)
	{
		Ar << ActorInstances_Deprecated;
	}
	else
#endif
	{
		Ar << ActorInstances;
	}

		
	Ar << ActorClass;
}

void FFoliageActor::DestroyActors(bool bOnLoad)
{
	TArray<AActor*> CopyActorInstances(ActorInstances);
	ActorInstances.Empty();
	for (AActor* Actor : CopyActorInstances)
	{
		if (Actor != nullptr)
		{
            if (bOnLoad)
			{
				Actor->ConditionalPostLoad();
			}
			Actor->GetWorld()->DestroyActor(Actor);
		}
	}
}

#if WITH_EDITOR
bool FFoliageActor::IsInitialized() const
{
	return ActorClass != nullptr;
}

void FFoliageActor::Initialize(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType)
{
	check(!IsInitialized());
	const UFoliageType_Actor* FoliageType_Actor = Cast<UFoliageType_Actor>(FoliageType);
	ActorClass = FoliageType_Actor->ActorClass ? FoliageType_Actor->ActorClass.Get() : AActor::StaticClass();
	bShouldAttachToBaseComponent = FoliageType_Actor->bShouldAttachToBaseComponent;
}

void FFoliageActor::Uninitialize()
{
	check(IsInitialized());
	DestroyActors(false);
	ActorClass = nullptr;
}

AActor* FFoliageActor::Spawn(AInstancedFoliageActor* IFA, const FFoliageInstance& Instance)
{
	if (ActorClass == nullptr)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags = RF_Transactional;
	SpawnParameters.bHideFromSceneOutliner = true;
	SpawnParameters.OverrideLevel = IFA->GetLevel();
	AActor* NewActor = IFA->GetWorld()->SpawnActor(ActorClass, nullptr, nullptr, SpawnParameters);
	if (NewActor)
	{
		NewActor->SetActorTransform(Instance.GetInstanceWorldTransform());
		FFoliageHelper::SetIsOwnedByFoliage(NewActor);
	}
	return NewActor;
}

TArray<AActor*> FFoliageActor::GetActorsFromSelectedIndices(const TSet<int32>& SelectedIndices) const
{
	TArray<AActor*> Selection;
	Selection.Reserve(SelectedIndices.Num());
	for (int32 i : SelectedIndices)
	{
		check(i < ActorInstances.Num());
		if (ActorInstances[i] != nullptr)
		{
			Selection.Add(ActorInstances[i]);
		}
	}

	return Selection;
}

int32 FFoliageActor::GetInstanceCount() const
{
	return ActorInstances.Num();
}

void FFoliageActor::PreAddInstances(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType, int32 Count)
{
	if (!IsInitialized())
	{
		Initialize(IFA, FoliageType);
		check(IsInitialized());
	}
}

void FFoliageActor::AddInstance(AInstancedFoliageActor* IFA, const FFoliageInstance& NewInstance)
{
	ActorInstances.Add(Spawn(IFA, NewInstance));
}

void FFoliageActor::AddExistingInstance(AInstancedFoliageActor* IFA, const FFoliageInstance& ExistingInstance, UObject* InstanceImplementation)
{
	AActor* Actor = Cast<AActor>(InstanceImplementation);
	check(Actor);
	check(Actor->GetClass() == ActorClass);
	Actor->SetActorTransform(ExistingInstance.GetInstanceWorldTransform());
	FFoliageHelper::SetIsOwnedByFoliage(Actor);
	check(IFA->GetLevel() == Actor->GetLevel());
	ActorInstances.Add(Actor);
}

void FFoliageActor::RemoveInstance(int32 InstanceIndex)
{
	AActor* Actor = ActorInstances[InstanceIndex];
	ActorInstances.RemoveAtSwap(InstanceIndex);
	Actor->GetWorld()->DestroyActor(Actor, true);
	bActorsDestroyed = true;
}

void FFoliageActor::MoveInstance(int32 InstanceIndex, UObject*& OutInstanceImplementation)
{
	AActor* Actor = ActorInstances[InstanceIndex];
	OutInstanceImplementation = Actor;
	ActorInstances.RemoveAtSwap(InstanceIndex);
}

void FFoliageActor::BeginUpdate()
{
	bActorsDestroyed = false;
}

void FFoliageActor::EndUpdate()
{
	if (bActorsDestroyed)
	{
		// This is to null out refs to components that have been created through ConstructionScript (same as it is done in edactDeleteSelected).
		// Because components that return true for IsCreatedByConstructionScript forward their Modify calls to their owning Actor so they are not part of the transaction.
		// Undoing the DestroyActor will re-run the construction script and those components will be recreated.
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}
	bActorsDestroyed = false;
}

void FFoliageActor::SetInstanceWorldTransform(int32 InstanceIndex, const FTransform& Transform, bool bTeleport)
{
	if (AActor* Actor = ActorInstances[InstanceIndex])
	{
		Actor->SetActorTransform(Transform);
	}
}

FTransform FFoliageActor::GetInstanceWorldTransform(int32 InstanceIndex) const
{
	if (AActor* Actor = ActorInstances[InstanceIndex])
	{
		return Actor->GetTransform();
	}

	return FTransform::Identity;
}

bool FFoliageActor::IsOwnedComponent(const UPrimitiveComponent* Component) const
{
	const AActor* Owner = Component->GetOwner();

	return ActorInstances.Contains(Owner);
}

int32 FFoliageActor::FindIndex(const AActor* InActor) const
{
	return ActorInstances.IndexOfByKey(InActor);
}

int32 FFoliageActor::FindIndex(const UPrimitiveComponent* Component) const
{
	return FindIndex(Component->GetOwner());
}

void FFoliageActor::Refresh(AInstancedFoliageActor* IFA, const TArray<FFoliageInstance>& Instances, bool Async, bool Force)
{
	for (int32 i = 0; i < Instances.Num(); ++i)
	{
		if (ActorInstances[i] == nullptr || ActorInstances[i]->IsPendingKill())
		{
			ActorInstances[i] = Spawn(IFA, Instances[i]);
		}
	}
}

void FFoliageActor::OnHiddenEditorViewMaskChanged(uint64 InHiddenEditorViews)
{
	for (AActor* Actor : ActorInstances)
	{
		if (Actor != nullptr)
		{
			if (Actor->HiddenEditorViews != InHiddenEditorViews)
			{
				Actor->HiddenEditorViews = InHiddenEditorViews;
				Actor->MarkComponentsRenderStateDirty();
			}
		}
	}
}

void FFoliageActor::UpdateActorTransforms(const TArray<FFoliageInstance>& Instances)
{
	for (int32 i = 0; i < Instances.Num(); ++i)
	{
		SetInstanceWorldTransform(i, Instances[i].GetInstanceWorldTransform(), true);
	}
}

void FFoliageActor::PostEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices)
{
	UpdateActorTransforms(Instances);
}

void FFoliageActor::PreMoveInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesMoved)
{
	for (int32 Index : InInstancesMoved)
	{
		if (AActor* Actor = ActorInstances[Index])
		{
			Actor->Modify();
		}
	}
}

void FFoliageActor::PostMoveInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesMoved, bool bFinished)
{
	// Copy because moving actors might remove them from ActorInstances
	TArray<AActor*> MovedActors;
	MovedActors.Reserve(InInstancesMoved.Num());
	for (int32 Index : InInstancesMoved)
	{
		if (AActor * Actor = ActorInstances[Index])
		{
			MovedActors.Add(Actor);
			Actor->PostEditMove(bFinished);
		}
	}

	if (GIsEditor && GEngine && MovedActors.Num() && bFinished)
	{
		GEngine->BroadcastActorsMoved(MovedActors);
	}
}

void FFoliageActor::NotifyFoliageTypeChanged(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices, bool bSourceChanged)
{
	if (UFoliageType_Actor* FoliageTypeActor = Cast<UFoliageType_Actor>(FoliageType))
	{
		if (bShouldAttachToBaseComponent != FoliageTypeActor->bShouldAttachToBaseComponent)
		{
			bShouldAttachToBaseComponent = FoliageTypeActor->bShouldAttachToBaseComponent;
			if (!bShouldAttachToBaseComponent)
			{
				IFA->RemoveBaseComponentOnFoliageTypeInstances(FoliageType);
			}
		}
	}

	if (bSourceChanged)
	{
		Reapply(IFA, FoliageType, Instances);
		ApplySelection(true, SelectedIndices);
	}
}

void FFoliageActor::Reapply(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances, bool bPostLoad)
{
	IFA->Modify();
	DestroyActors(bPostLoad);
	
	if (IsInitialized())
	{
		Uninitialize();
	}
	Initialize(IFA, FoliageType);

	for (int32 i = 0; i < Instances.Num(); ++i)
	{
		ActorInstances.Add(Spawn(IFA, Instances[i]));
	}
}

void FFoliageActor::SelectAllInstances(bool bSelect)
{
	AInstancedFoliageActor::SelectionChanged.Broadcast(bSelect, ActorInstances);
}

void FFoliageActor::SelectInstance(bool bSelect, int32 Index)
{
	TArray<AActor*> SingleInstance;
	SingleInstance.Add(ActorInstances[Index]);
	AInstancedFoliageActor::SelectionChanged.Broadcast(bSelect, SingleInstance);
}

void FFoliageActor::SelectInstances(bool bSelect, const TSet<int32>& SelectedIndices)
{
	AInstancedFoliageActor::SelectionChanged.Broadcast(bSelect, GetActorsFromSelectedIndices(SelectedIndices));
}

void FFoliageActor::ApplySelection(bool bApply, const TSet<int32>& SelectedIndices)
{
	if (bApply && SelectedIndices.Num() > 0)
	{
		AInstancedFoliageActor::SelectionChanged.Broadcast(true, GetActorsFromSelectedIndices(SelectedIndices));
	}
}

void FFoliageActor::ClearSelection(const TSet<int32>& SelectedIndices)
{
	AInstancedFoliageActor::SelectionChanged.Broadcast(false, GetActorsFromSelectedIndices(SelectedIndices));
}

bool FFoliageActor::UpdateInstanceFromActor(AInstancedFoliageActor* IFA, AActor* InActor, FFoliageInfo& FoliageInfo)
{
	int32 Index = FindIndex(InActor);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	IFA->Modify();
	const bool bChecked = false; // In the case of the PostEditUndo its possible that the instancehash is empty.
	FoliageInfo.InstanceHash->RemoveInstance(FoliageInfo.Instances[Index].Location, Index, bChecked);
	
	FTransform ActorTransform = InActor->GetTransform();
	FoliageInfo.Instances[Index].Location = ActorTransform.GetLocation();
	FoliageInfo.Instances[Index].Rotation = FRotator(ActorTransform.GetRotation());
	FoliageInfo.Instances[Index].PreAlignRotation = FoliageInfo.Instances[Index].Rotation;
	FoliageInfo.Instances[Index].DrawScale3D = InActor->GetActorScale3D();
	FoliageInfo.InstanceHash->InsertInstance(FoliageInfo.Instances[Index].Location, Index);
	
	return true;
}

void FFoliageActor::GetInvalidInstances(TArray<int32>& InvalidInstances)
{
	for (int32 Index = 0; Index < ActorInstances.Num(); ++Index)
	{
		if (ActorInstances[Index] == nullptr)
		{
			InvalidInstances.Add(Index);
		}
	}
}

#endif // WITH_EDITOR
