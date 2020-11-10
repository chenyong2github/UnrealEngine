// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/LODSyncComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "LODSyncInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogLODSync, Warning, All);

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

	UE_LOG(LogLODSync, Verbose, TEXT("Initialized Sync Component"));

	// don't reset to zero because it may pop
	CurrentLOD = FMath::Clamp(CurrentLOD, 0, CurrentNumLODs - 1);
}

void ULODSyncComponent::OnUnregister()
{
	UninitializeSyncComponents();

	UE_LOG(LogLODSync, Verbose, TEXT("Uninitialized Sync Component"));
	Super::OnUnregister();
}

const FComponentSync* ULODSyncComponent::GetComponentSync(const FName& InName) const
{
	if (InName != NAME_None)
	{
		for (const FComponentSync& Comp : ComponentsToSync)
		{
			if (Comp.Name == InName)
			{
				return &Comp;
			}
		}
	}

	return nullptr;
}

void ULODSyncComponent::InitializeSyncComponents()
{
	DriveComponents.Reset();
	SubComponents.Reset();

	AActor* Owner = GetOwner();
	// for now we only support skinnedmeshcomponent
	TArray<UActorComponent*> AllComponents = Owner->GetComponentsByInterface(ULODSyncInterface::StaticClass());

	// current num LODs start with NumLODs
	// but if nothing is set, it will be -1
	CurrentNumLODs = NumLODs;
	// if NumLODs are -1, we try to find the max number of LODs of all the components
	const bool bFindTheMaxLOD = (NumLODs == -1);
	// we find all the components of the child and add this to prerequisite
	for (UActorComponent* Component : AllComponents)
	{
		UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(Component);
		if (PrimComponent)
		{
			FName Name = PrimComponent->GetFName();
			ILODSyncInterface* LODInterface = Cast<ILODSyncInterface>(PrimComponent);
			const FComponentSync* CompSync = GetComponentSync(Name);
			if (LODInterface && CompSync && CompSync->SyncOption != ESyncOption::Disabled)
			{
				PrimComponent->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
				SubComponents.Add(PrimComponent);

				if (CompSync->SyncOption == ESyncOption::Drive)
				{
					DriveComponents.Add(PrimComponent);

					const int LODCount = LODInterface->GetNumSyncLODs();
					UE_LOG(LogLODSync, Verbose, TEXT("Adding new component (%s - LODCount : %d ) to sync."), *Name.ToString(), LODCount);
					if (bFindTheMaxLOD)
					{
						// get max lod
						CurrentNumLODs = FMath::Max(CurrentNumLODs, LODCount);
						UE_LOG(LogLODSync, Verbose, TEXT("MaxLOD now is set to (%d )."), CurrentNumLODs);
					}
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

	// after initialize we update LOD
	// so that any initialization can happen with the new LOD
	UpdateLOD();
}

void ULODSyncComponent::RefreshSyncComponents()
{
	UninitializeSyncComponents();
	InitializeSyncComponents();
}

void ULODSyncComponent::UninitializeSyncComponents()
{
	auto ResetComponentList = [&](TArray<UPrimitiveComponent*>& ComponentList)
	{
		for (UPrimitiveComponent* Component : ComponentList)
		{
			if (Component)
			{
				Component->PrimaryComponentTick.RemovePrerequisite(this, PrimaryComponentTick);
			}
		}

		ComponentList.Reset();

	};

	ResetComponentList(DriveComponents);
	ResetComponentList(SubComponents);
}

void ULODSyncComponent::UpdateLOD()
{
	// update latest LOD
	// this should tick first and it will set forced LOD
	// individual component will update it correctly
	int32 CurrentWorkingLOD = 0xff;
	// we want to ensure we have a valid set of the driving components
	if (DriveComponents.Num() > 0)
	{
		// it seems we have a situation where components becomes nullptr between registration but has the array entry
		// so we ensure this is set before we set the data. 
		bool bHaveValidSetting = false;
		if (ForcedLOD >= 0 && ForcedLOD < CurrentNumLODs)
		{
			CurrentWorkingLOD = ForcedLOD;
			bHaveValidSetting = true;
			UE_LOG(LogLODSync, Verbose, TEXT("LOD Sync : Using ForcedLOD [%d]"), ForcedLOD);
		}
		else
		{
			for (UPrimitiveComponent* Component : DriveComponents)
			{
				if (Component)
				{
					ILODSyncInterface* LODInterface = CastChecked<ILODSyncInterface>(Component);
					const int32 DesiredSyncLOD = LODInterface->GetDesiredSyncLOD();

					if (DesiredSyncLOD >= 0)
					{
						const int32 DesiredLOD = GetSyncMappingLOD(Component->GetFName(), DesiredSyncLOD);
						UE_LOG(LogLODSync, Verbose, TEXT("LOD Sync Drivers : %s - Source LOD [%d] RemappedLOD[%d]"), *GetNameSafe(Component), DesiredSyncLOD, DesiredLOD);
						// we're looking for lowest LOD (highest fidelity)
						CurrentWorkingLOD = FMath::Min(CurrentWorkingLOD, DesiredLOD);
						bHaveValidSetting = true;
					}
				}
			}
		}

		if (bHaveValidSetting)
		{
			// ensure current WorkingLOD is with in the range
			CurrentWorkingLOD = FMath::Clamp(CurrentWorkingLOD, 0, CurrentNumLODs - 1);
			UE_LOG(LogLODSync, Verbose, TEXT("LOD Sync : Current LOD (%d)"), CurrentWorkingLOD);
			for (UPrimitiveComponent* Component : SubComponents)
			{
				if (Component)
				{
					ILODSyncInterface* LODInterface = Cast<ILODSyncInterface>(Component);
					const int32 NewLOD = GetCustomMappingLOD(Component->GetFName(), CurrentWorkingLOD);
					UE_LOG(LogLODSync, Verbose, TEXT("LOD Sync Setter : %s - New LOD [%d]"), *GetNameSafe(Component), NewLOD);
					LODInterface->SetSyncLOD(NewLOD);
				}
			}
		}
	}
}

void ULODSyncComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateLOD();
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

FString ULODSyncComponent::GetLODSyncDebugText() const
{
	FString OutString;

	for (UPrimitiveComponent* Component : SubComponents)
	{
		if (Component)
		{
			ILODSyncInterface* LODInterface = CastChecked<ILODSyncInterface>(Component);
			const int32 CurrentSyncLOD = LODInterface->GetCurrentSyncLOD();
			const int32 DesiredSyncLOD = LODInterface->GetDesiredSyncLOD();

			if (DesiredSyncLOD >= 0)
			{
				OutString += FString::Printf(TEXT("%s : %d (%d)\n"), *(Component->GetFName().ToString()), CurrentSyncLOD, DesiredSyncLOD);
			}
			else
			{
				OutString += FString::Printf(TEXT("%s : %d\n"), *(Component->GetFName().ToString()), CurrentSyncLOD);
			}
		}
	}

	return OutString;
}
