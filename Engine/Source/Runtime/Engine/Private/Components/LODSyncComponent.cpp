// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/LODSyncComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "LODSyncInterface.h"

/* ULODSyncComponent interface
 *****************************************************************************/

ULODSyncComponent::ULODSyncComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bTickInEditor = true;

	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bCanEverTick = true;
}

void ULODSyncComponent::OnRegister()
{
	Super::OnRegister();

	InitializeSyncComponents();

	// don't reset to zero because it may pop
	CurrentLOD = FMath::Clamp(CurrentLOD, 0, CurrentNumLODs - 1);
}

void ULODSyncComponent::OnUnregister()
{
	UninitializeSyncComponents();

	Super::OnUnregister();
}

void ULODSyncComponent::InitializeSyncComponents()
{
	SubComponents.Reset();

	AActor* Owner = GetOwner();
	// for now we only support skinnedmeshcomponent
	TArray<UActorComponent*> AllComponents = Owner->GetComponentsByInterface(ULODSyncInterface::StaticClass());

	CurrentNumLODs = NumLODs;
	// we find all the components of the child and add this to prerequisite
	for (UActorComponent* Component : AllComponents)
	{
		UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(Component);
		if (PrimComponent)
		{
			FName Name = PrimComponent->GetFName();
			ILODSyncInterface* LODInterface = Cast<ILODSyncInterface>(PrimComponent);
			if (LODInterface && ComponentsToSync.Contains(Name))
			{
				PrimComponent->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
				SubComponents.Add(PrimComponent);

				if (NumLODs == -1)
				{
					// get max lod
					CurrentNumLODs = FMath::Max(CurrentNumLODs, LODInterface->GetNumSyncLODs());
				}
			}
		}
	}

	// save inverse mapping map for the case we have to reverse lookup for it
	// and smaller sets can trigger the best desired LOD, and to find what, we have to convert to generic option
	// Custom Look up is (M:N) (M <= N) 
	// Inverse look up will be to (N:M) where(M<=N)
	for (auto Iter = CustomLODMapping.CreateIterator(); Iter; ++Iter)
	{
		FLODMappingData& Data = Iter.Value();

		Data.InverseMapping.Reset();

		TMap<int32, int32> InverseMappingIndices;
		int32 MaxLOD = 0;
		for (int32 Index = 0; Index < Data.Mapping.Num();++Index)
		{
			// if same indices are used, it will use later one, (lower LOD)
			// which is what we want
			InverseMappingIndices.FindOrAdd(Data.Mapping[Index]) = Index;
			MaxLOD = FMath::Max(MaxLOD, Data.Mapping[Index]);
		}

		// it's possible we may have invalid ones between
		Data.InverseMapping.AddUninitialized(MaxLOD + 1);
		int32 LastLOD = 0;
		for (int32 Index = 0; Index <= MaxLOD; ++Index)
		{
			int32* Found = InverseMappingIndices.Find(Index);
			if (Found)
			{
				Data.InverseMapping[Index] = *Found;
				LastLOD = *Found;
			}
			else
			{
				// if there is empty slot, we want to fill between
				Data.InverseMapping[Index] = LastLOD;
			}
		}
	}
}

void ULODSyncComponent::RefreshSyncComponents()
{
	UninitializeSyncComponents();
	InitializeSyncComponents();
}

void ULODSyncComponent::UninitializeSyncComponents()
{
	for (UPrimitiveComponent* Component : SubComponents)
	{
		if (Component)
		{
			Component->PrimaryComponentTick.RemovePrerequisite(this, PrimaryComponentTick);
		}
	}

	SubComponents.Reset();
}

void ULODSyncComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// update latest LOD
	// this should tick first and it will set forced LOD
	// individual component will update it correctly
	int32 CurrentWorkingLOD = 0xff;
	if (ForcedLOD >= 0 && ForcedLOD < CurrentNumLODs)
	{
		CurrentWorkingLOD = ForcedLOD;
	}
	else
	{
		for (UPrimitiveComponent* Component : SubComponents)
		{
			if (Component)
			{
				ILODSyncInterface* LODInterface = CastChecked<ILODSyncInterface>(Component);
				const int32 DesiredSyncLOD = LODInterface->GetDesiredSyncLOD();

				if (DesiredSyncLOD >= 0)
				{
					const int32 DesiredLOD = GetSyncMappingLOD(Component->GetFName(), DesiredSyncLOD);
					UE_LOG(LogSkeletalMesh, Verbose, TEXT("LOD Sync : Current Desired LOD (source %d target %d) for (%s)"), DesiredSyncLOD, DesiredLOD, *GetNameSafe(Component));
					// we're looking for lowest LOD (highest fidelity)
					CurrentWorkingLOD = FMath::Min(CurrentWorkingLOD, DesiredLOD);
				}
			}
		}
	}

	// ensure current WorkingLOD is with in the range
	CurrentWorkingLOD = FMath::Clamp(CurrentWorkingLOD, 0, CurrentNumLODs);
	UE_LOG(LogSkeletalMesh, Verbose, TEXT("LOD Sync : Final LOD (%d)"), CurrentWorkingLOD);
	for (UPrimitiveComponent* Component : SubComponents)
	{
		if (Component)
		{
			ILODSyncInterface* LODInterface = Cast<ILODSyncInterface>(Component);
			LODInterface->SetSyncLOD(GetCustomMappingLOD(Component->GetFName(), CurrentWorkingLOD));
		}
	}
}

int32 ULODSyncComponent::GetCustomMappingLOD(const FName& ComponentName, int32 CurrentWorkingLOD) const
{
	const FLODMappingData* Found = CustomLODMapping.Find(ComponentName);
	if (Found && Found->Mapping.IsValidIndex(CurrentWorkingLOD))
	{
		return Found->Mapping[CurrentWorkingLOD];
	}

	return CurrentWorkingLOD;
}

int32 ULODSyncComponent::GetSyncMappingLOD(const FName& ComponentName, int32 CurrentSourceLOD) const
{
	const FLODMappingData* Found = CustomLODMapping.Find(ComponentName);
	if (Found && Found->InverseMapping.IsValidIndex(CurrentSourceLOD))
	{
		return Found->InverseMapping[CurrentSourceLOD];
	}

	return CurrentSourceLOD;
}

