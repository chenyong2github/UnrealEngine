// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/LightWeightInstanceManager.h"
#include "GameFramework/LightWeightInstanceSubsystem.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

ALightWeightInstanceManager::ALightWeightInstanceManager(const FObjectInitializer& ObjectInitializer)
{
	bReplicates = true;
	AcceptedClass = AActor::StaticClass();
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FLightWeightInstanceSubsystem::Get().LWInstanceManagers.Add(this);
	}
}

ALightWeightInstanceManager::~ALightWeightInstanceManager()
{
	FLightWeightInstanceSubsystem::Get().LWInstanceManagers.Remove(this);
}

void ALightWeightInstanceManager::SetRepresentedClass(UClass* ActorClass)
{
	// we should not be changing this once it has been set
	ensure(RepresentedClass == nullptr);

	RepresentedClass = ActorClass;
}

void ALightWeightInstanceManager::Tick(float DeltaSeconds)
{
	// do nothing
}

AActor* ALightWeightInstanceManager::GetActorFromHandle(const FActorInstanceHandle& Handle)
{
	// make sure the handle doesn't have an actor already
	if (ensure(!Handle.Actor.IsValid()))
	{
		// check if we already have an actor for this handle
		AActor* const* FoundActor = nullptr;
		if ((FoundActor = Actors.Find(Handle.GetInstanceIndex())) == nullptr)
		{
			// spawn a new actor
			FActorSpawnParameters SpawnParams;
			SetSpawnParameters(SpawnParams);

			AActor* Actor = GetLevel()->GetWorld()->SpawnActor<AActor>(RepresentedClass, InstanceTransforms[Handle.GetInstanceIndex()], SpawnParams);
			check(Actor);

			Handle.Actor = Actor;
			Actors.Add(Handle.GetInstanceIndex(), Actor);

			PostActorSpawn(Handle);
		}
		else
		{
			Handle.Actor = *FoundActor;
		}
	}

	ensure(Handle.Actor.IsValid());
	return Handle.Actor.Get();
}

int32 ALightWeightInstanceManager::FindIndexForActor(const AActor* InActor) const
{
	for (const TPair<int32, AActor*>& ActorPair : Actors)
	{
		if (ActorPair.Value == InActor)
		{
			return ActorPair.Key;
		}
	}

	return INDEX_NONE;
}

int32 ALightWeightInstanceManager::ConvertCollisionIndexToLightWeightIndex(int32 InIndex) const
{
	return InIndex;
}

void ALightWeightInstanceManager::SetSpawnParameters(FActorSpawnParameters& SpawnParams)
{
	SpawnParams.OverrideLevel = GetLevel();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags = RF_Transactional;
}

void ALightWeightInstanceManager::PostActorSpawn(const FActorInstanceHandle& Handle)
{
	// do nothing
}

bool ALightWeightInstanceManager::IsIndexValid(int32 Index) const
{
	if (Index < ValidIndices.Num())
	{
		return ValidIndices[Index];
	}

	return false;
}

bool ALightWeightInstanceManager::FindActorForHandle(const FActorInstanceHandle& Handle) const
{
	ensure(!Handle.Actor.IsValid());

	AActor* const* FoundActor = Actors.Find(Handle.GetInstanceIndex());
	Handle.Actor = FoundActor ? *FoundActor : nullptr;
	return Handle.Actor != nullptr;
}

FVector ALightWeightInstanceManager::GetLocation(const FActorInstanceHandle& Handle) const
{
	if (FindActorForHandle(Handle))
	{
		return Handle.Actor->GetActorLocation();
	}

	if (ensure(IsIndexValid(Handle.GetInstanceIndex())))
	{
		return InstanceTransforms[Handle.GetInstanceIndex()].GetTranslation();
	}

	return FVector();
}

FString ALightWeightInstanceManager::GetName(const FActorInstanceHandle& Handle) const
{
	if (FindActorForHandle(Handle))
	{
		return Handle.Actor->GetName();
	}

	return FString::Printf(TEXT("%s_%u"), *BaseInstanceName, Handle.GetInstanceIndex());
}

bool ALightWeightInstanceManager::DoesRepresentClass(const UClass* OtherClass) const
{
	return OtherClass ? OtherClass->IsChildOf(RepresentedClass) : false;
}

bool ALightWeightInstanceManager::DoesAcceptClass(const UClass* OtherClass) const
{
	return OtherClass ? OtherClass->IsChildOf(AcceptedClass) : false;
}

UClass* ALightWeightInstanceManager::GetRepresentedClass() const
{
	return RepresentedClass;
}

UClass* ALightWeightInstanceManager::GetAcceptedClass() const
{
	return AcceptedClass;
}

int32 ALightWeightInstanceManager::AddNewInstance(FLWIData* InitData)
{
	if (!InitData)
	{
		return INDEX_NONE;
	}

	// use one of the free indices if any are available; otherwise grow the size of the array
	const int32 DataIdx = FreeIndices.Num() > 0 ? FreeIndices.Pop(false) : ValidIndices.Num();
	
	// Update the rest of our per instance data
	AddNewInstanceAt(InitData, DataIdx);

	return DataIdx;
}

void ALightWeightInstanceManager::AddNewInstanceAt(FLWIData* InitData, int32 Index)
{
	// allocate space on the end of the array if we need to
	if (Index >= ValidIndices.Num())
	{
		InstanceTransforms.AddUninitialized();
		ValidIndices.AddUninitialized();
	}
	ensure(Index < ValidIndices.Num());
	ensure(InstanceTransforms.Num() == ValidIndices.Num());

	// update our data
	InstanceTransforms[Index] = InitData->Transform;
	ValidIndices[Index] = true;
}

void ALightWeightInstanceManager::RemoveInstance(const int32 Index)
{
	if (ensure(IsIndexValid(Index)))
	{
		// mark the index as no longer in use
		FreeIndices.Add(Index);
		ValidIndices[Index] = false;

		// destroy the associated actor if one existed
		if (AActor** FoundActor = Actors.Find(Index))
		{
			AActor* ActorToDestroy = *FoundActor;
			ActorToDestroy->Destroy();
		}
	}
}

void ALightWeightInstanceManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ALightWeightInstanceManager, RepresentedClass);
	DOREPLIFETIME(ALightWeightInstanceManager, InstanceTransforms);
	DOREPLIFETIME(ALightWeightInstanceManager, FreeIndices);
	DOREPLIFETIME(ALightWeightInstanceManager, ValidIndices);
}

void ALightWeightInstanceManager::OnRep_Transforms()
{
	// do nothing
}
#if WITH_EDITOR
void ALightWeightInstanceManager::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (Name == GET_MEMBER_NAME_CHECKED(ALightWeightInstanceManager, RepresentedClass))
	{
		SetRepresentedClass(RepresentedClass);
	}
}
#endif