// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapARPinComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "MagicLeapARPinFunctionLibrary.h"
#include "MagicLeapARPinModule.h"
#include "IMagicLeapPlugin.h"
#include "MagicLeapCFUID.h"
#include "Lumin/CAPIShims/LuminAPI.h"

UMagicLeapARPinComponent::UMagicLeapARPinComponent()
: UserIndex(0)
, AutoPinType(EMagicLeapAutoPinType::OnlyOnDataRestoration)
, bShouldPinActor(false)
, PinDataClass(UMagicLeapARPinSaveGame::StaticClass())
, PinnedSceneComponent(nullptr)
, PinData(nullptr)
, OldComponentWorldTransform(FTransform::Identity)
, OldCFUIDTransform(FTransform::Identity)
, NewComponentWorldTransform(FTransform::Identity)
, NewCFUIDTransform(FTransform::Identity)
, bPinned(false)
, bDataRestored(false)
, bAttemptedPinningAfterTrackerCreation(false)
, bPinFoundInEnvironmentPrevFrame(false)
{
	// Make sure this component ticks
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bAutoActivate = true;
}

void UMagicLeapARPinComponent::BeginPlay()
{
	Super::BeginPlay();

	UMagicLeapARPinFunctionLibrary::CreateTracker();

	if (ObjectUID.Len() == 0)
	{
		if (GetOwner() != nullptr)
		{
			ObjectUID = GetOwner()->GetName();
			UE_LOG(LogMagicLeapARPin, Warning, TEXT("ObjectUID is empty. Using Owner actor's name [%s] instead. A non-empty unique ID is required to make the object persistent."), *ObjectUID);
		}
	}

	if (ObjectUID.Len() != 0)
	{
		PinData = Cast<UMagicLeapARPinSaveGame>(UGameplayStatics::LoadGameFromSlot(ObjectUID, UserIndex));
		if (PinData)
		{
			PinnedCFUID = PinData->PinnedID;
			OldComponentWorldTransform = PinData->ComponentWorldTransform;
			OldCFUIDTransform = PinData->PinTransform;
			bDataRestored = true;
		}
	}
	else
	{
		UE_LOG(LogMagicLeapARPin, Warning, TEXT("ObjectUID is empty. A non-empty unique ID is required to make the object persistent."));
	}
}

void UMagicLeapARPinComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!(IMagicLeapPlugin::Get().IsMagicLeapHMDValid() && UMagicLeapARPinFunctionLibrary::IsTrackerValid()))
	{
		return;
	}

	if (!bAttemptedPinningAfterTrackerCreation)
	{
		bAttemptedPinningAfterTrackerCreation = true;
		if ((AutoPinType == EMagicLeapAutoPinType::Always) || (bDataRestored && AutoPinType == EMagicLeapAutoPinType::OnlyOnDataRestoration))
		{
			if (bShouldPinActor)
			{
				PinActor(GetOwner());
			}
			else
			{
				PinSceneComponent(this);
			}
		}
	}

	bool bPinFoundInEnvironment = false;

	if (PinnedSceneComponent != nullptr)
	{
		if (!bPinned)
		{
			if (bDataRestored)
			{
				FVector DummyPosition = FVector::ZeroVector;
				FRotator DummyOrientation = FRotator::ZeroRotator;
				// use MLSnapshotGetTransform's return value of PoseNotFound to determine of the given PinnedCFUID is available in the current environment.
				bPinned = UMagicLeapARPinFunctionLibrary::GetARPinPositionAndOrientation(PinnedCFUID, DummyPosition, DummyOrientation, bPinFoundInEnvironment);
			}
			else
			{
				EMagicLeapPassableWorldError Error = UMagicLeapARPinFunctionLibrary::GetClosestARPin(PinnedSceneComponent->GetComponentLocation(), PinnedCFUID);
				if (Error == EMagicLeapPassableWorldError::None)
				{
					FVector PinWorldPosition = FVector::ZeroVector;
					FRotator PinWorldOrientation = FRotator::ZeroRotator;
					bool bCFUIDTransform = UMagicLeapARPinFunctionLibrary::GetARPinPositionAndOrientation(PinnedCFUID, PinWorldPosition, PinWorldOrientation, bPinFoundInEnvironment);
					if (bCFUIDTransform)
					{
						bPinned = true;
						OldComponentWorldTransform = PinnedSceneComponent->GetComponentToWorld();
						OldCFUIDTransform = FTransform(PinWorldOrientation, PinWorldPosition);

						if (ObjectUID.Len() != 0)
						{
							if (!PinData)
							{
								PinData = Cast<UMagicLeapARPinSaveGame>(UGameplayStatics::CreateSaveGameObject(PinDataClass));
							}
							check(PinData);
							PinData->PinnedID = PinnedCFUID;
							PinData->ComponentWorldTransform = OldComponentWorldTransform;
							PinData->PinTransform = OldCFUIDTransform;
							bool bDataSaved = UGameplayStatics::SaveGameToSlot(PinData, ObjectUID, UserIndex);
							UE_CLOG(!bDataSaved, LogMagicLeapARPin, Error, TEXT("Error saving data persistent data for MagicLeapARPin %s."), *ObjectUID);
						}
					}
				}
			}
		}

		if (bPinned)
		{
			FVector PinWorldPosition = FVector::ZeroVector;
			FRotator PinWorldOrientation = FRotator::ZeroRotator;
			bool bCFUIDTransform = UMagicLeapARPinFunctionLibrary::GetARPinPositionAndOrientation(PinnedCFUID, PinWorldPosition, PinWorldOrientation, bPinFoundInEnvironment);
			if (bCFUIDTransform)
			{
				NewCFUIDTransform = FTransform(PinWorldOrientation.Quaternion(), PinWorldPosition);

				const FMatrix NewComponentWorldTransformMatrix = OldComponentWorldTransform.ToMatrixNoScale() * (OldCFUIDTransform.ToMatrixNoScale().Inverse() * NewCFUIDTransform.ToMatrixNoScale());
				NewComponentWorldTransform = FTransform(NewComponentWorldTransformMatrix);

				PinnedSceneComponent->SetWorldLocationAndRotation(NewComponentWorldTransform.GetLocation(), NewComponentWorldTransform.Rotator());
			}
		}
	}
	else
	{
		bPinned = false;
	}

	if (bPinFoundInEnvironmentPrevFrame != bPinFoundInEnvironment)
	{
		if (bPinFoundInEnvironment)
		{
			OnPersistentEntityPinned.Broadcast(bDataRestored);
		}
		else
		{
			OnPersistentEntityPinLost.Broadcast();
		}
	}

	bPinFoundInEnvironmentPrevFrame = bPinFoundInEnvironment;
}

void UMagicLeapARPinComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ObjectUID.Len() != 0 && bPinned)
	{
		if (!PinData)
		{
			PinData = Cast<UMagicLeapARPinSaveGame>(UGameplayStatics::CreateSaveGameObject(PinDataClass));
		}
		check(PinData);
		PinData->PinnedID = PinnedCFUID;
		PinData->ComponentWorldTransform = NewComponentWorldTransform;
		PinData->PinTransform = NewCFUIDTransform;
		bool bDataSaved = UGameplayStatics::SaveGameToSlot(PinData, ObjectUID, UserIndex);
		UE_CLOG(!bDataSaved, LogMagicLeapARPin, Error, TEXT("Error saving data persistent data for MagicLeapARPin %s."), *ObjectUID);
	}

	// Reset flags on EndPlay so consequetive VRPreview launches are respected.
	bAttemptedPinningAfterTrackerCreation = false;
	bPinFoundInEnvironmentPrevFrame = false;

	Super::EndPlay(EndPlayReason);
}

bool UMagicLeapARPinComponent::PinSceneComponent(USceneComponent* ComponentToPin)
{
	if (ComponentToPin == nullptr)
	{
		UE_LOG(LogMagicLeapARPin, Warning, TEXT("nullptr passed to UMagicLeapARPinComponent::PinSceneComponent(). Use UMagicLeapARPinComponent::UnPin() if you no longer wish for this component to be persistent or want to move the component around."));
		return false;
	}

	bPinned = false; // Will resolve in Tick
	PinnedSceneComponent = ComponentToPin;
	return true;
}

bool UMagicLeapARPinComponent::PinActor(AActor* ActorToPin)
{
	if (ActorToPin != nullptr)
	{
		return PinSceneComponent(ActorToPin->GetRootComponent());
	}
	return false;
}

void UMagicLeapARPinComponent::UnPin()
{
	PinnedSceneComponent = nullptr;
	bPinned = false;

	if (ObjectUID.Len() != 0)
	{
		UGameplayStatics::DeleteGameInSlot(ObjectUID, UserIndex);
		PinData = nullptr;
		bDataRestored = false;
	}
}

bool UMagicLeapARPinComponent::IsPinned() const
{
	return bPinned;
}

bool UMagicLeapARPinComponent::PinRestoredOrSynced() const
{
	return bDataRestored;
}

bool UMagicLeapARPinComponent::GetPinnedPinID(FGuid& PinID)
{
	if (bPinned)
	{
		PinID = PinnedCFUID;
	}
	return bPinned;
}

UMagicLeapARPinSaveGame* UMagicLeapARPinComponent::GetPinData(TSubclassOf<UMagicLeapARPinSaveGame> InPinDataClass)
{
	checkf(InPinDataClass == PinDataClass, TEXT("Attempted to get user data object of different type than what was defined in MagicLeapARPinComponent!"));

	if (!PinData)
	{
		PinData = Cast<UMagicLeapARPinSaveGame>(UGameplayStatics::CreateSaveGameObject(PinDataClass));
	}
	
	return PinData;
}
