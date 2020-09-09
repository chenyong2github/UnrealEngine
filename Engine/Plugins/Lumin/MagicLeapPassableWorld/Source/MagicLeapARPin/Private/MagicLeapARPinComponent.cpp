// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapARPinComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "MagicLeapARPinFunctionLibrary.h"
#include "IMagicLeapARPinFeature.h"
#include "Components/SphereComponent.h"

constexpr float kDefaultRadius = 250.0f;
constexpr int kMaxResultsForComponent = 1;

UMagicLeapARPinComponent::UMagicLeapARPinComponent()
: UserIndex(0)
, AutoPinType(EMagicLeapAutoPinType::OnlyOnDataRestoration)
, bShouldPinActor(false)
, PinDataClass(UMagicLeapARPinSaveGame::StaticClass())
, SearchPinTypes( { EMagicLeapARPinType::SingleUserSingleSession, EMagicLeapARPinType::SingleUserMultiSession, EMagicLeapARPinType::MultiUserMultiSession }) 
, PinnedSceneComponent(nullptr)
, PinData(nullptr)
, OldComponentWorldTransform(FTransform::Identity)
, OldCFUIDTransform(FTransform::Identity)
, NewComponentWorldTransform(FTransform::Identity)
, NewCFUIDTransform(FTransform::Identity)
, bHasValidPin(false)
, bDataRestored(false)
, bAttemptedPinningAfterDataRestoration(false)
, bPinFoundInEnvironmentPrevFrame(false)
{
	// Make sure this component ticks
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bAutoActivate = true;

	SearchVolume = CreateDefaultSubobject<USphereComponent>(TEXT("SearchVolume"));
	if (!IsTemplate())
	{
		SearchVolume->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	}
	SearchVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SearchVolume->SetCanEverAffectNavigation(false);
	SearchVolume->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
	SearchVolume->SetCollisionObjectType(ECollisionChannel::ECC_WorldDynamic);
	SearchVolume->SetGenerateOverlapEvents(false);
	SearchVolume->SetSphereRadius(kDefaultRadius, false);

	SaveGameDelegate.BindUObject(this, &UMagicLeapARPinComponent::OnSaveGameToSlot);
	LoadGameDelegate.BindUObject(this, &UMagicLeapARPinComponent::OnLoadGameFromSlot);
}

void UMagicLeapARPinComponent::BeginPlay()
{
	Super::BeginPlay();

	UMagicLeapARPinFunctionLibrary::CreateTracker();

	AttemptPinDataRestorationAsync();
}

void UMagicLeapARPinComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl == nullptr || !ARPinImpl->IsTrackerValid())
	{
		return;
	}

	// Select component to be pinned for auto pinning.
	if (!bAttemptedPinningAfterDataRestoration)
	{
		bAttemptedPinningAfterDataRestoration = true;
		if (bDataRestored)
		{
			if (AutoPinType == EMagicLeapAutoPinType::OnlyOnDataRestoration || AutoPinType == EMagicLeapAutoPinType::Always)
			{
				PinToRestoredOrSyncedID();
			}
		}
		else if (AutoPinType == EMagicLeapAutoPinType::Always)
		{
			PinToBestFit();
		}
	}

	bool bPinFoundInEnvironment = false;

	// if we want to attempt pinning, either because of auto pinning or explicit call to any of the PinTo***() functions
	if (PinnedSceneComponent != nullptr)
	{
		// if we havent found the PCF to attach to, or one hasnt been restored from a prev session
		// or one hasnt been set via PinToID(), look for the best possible fit.
		if (!bHasValidPin)
		{
			const EMagicLeapPassableWorldError Error = FindBestFitPin(ARPinImpl, PinnedCFUID);
			if (Error == EMagicLeapPassableWorldError::None)
			{
				if (TryInitOldTransformData())
				{
					bHasValidPin = true;
				}
			}
		}

		// if we have a pin, update the transform
		if (bHasValidPin)
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
		bHasValidPin = false;
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
	if (bHasValidPin)
	{
		// no point registering the delegate on EndPlay because by the time it gets called, this object would have been destroyed.
		SavePinData(PinnedCFUID, NewComponentWorldTransform, NewCFUIDTransform, false /* bRegisterDelegate */);
	}

	// Reset flags on EndPlay so consequetive VRPreview launches are respected.
	bAttemptedPinningAfterDataRestoration = false;
	bPinFoundInEnvironmentPrevFrame = false;
	PinData = nullptr;
	bHasValidPin = false;

	Super::EndPlay(EndPlayReason);
}

bool UMagicLeapARPinComponent::PinToID(const FGuid& PinID)
{
	PinnedCFUID = PinID;
	bHasValidPin = TryInitOldTransformData();
	UE_CLOG(!bHasValidPin, LogMagicLeapARPin, Error, TEXT("PinToID failed because specified pin %s was not found in environment."), *PinID.ToString());

	return bHasValidPin;
}

void UMagicLeapARPinComponent::PinToBestFit()
{
	SelectComponentToPin();
	bHasValidPin = false; // Will resolve in Tick
}

bool UMagicLeapARPinComponent::PinToRestoredOrSyncedID()
{
	if (bDataRestored)
	{
		SelectComponentToPin();
		bHasValidPin = true;
	}
	UE_CLOG(!bDataRestored, LogMagicLeapARPin, Error, TEXT("PinToRestoredOrSyncedID() failed. No pin has been restored or synced"));

	return bDataRestored;
}

bool UMagicLeapARPinComponent::PinSceneComponent(USceneComponent* ComponentToPin)
{
	if (ComponentToPin == nullptr)
	{
		UE_LOG(LogMagicLeapARPin, Error, TEXT("nullptr passed to UMagicLeapARPinComponent::PinSceneComponent(). Use UMagicLeapARPinComponent::UnPin() if you no longer wish for this component to be persistent or want to move the component around."));
		return false;
	}
	else if (ComponentToPin != this)
	{
		UE_LOG(LogMagicLeapARPin, Error, TEXT("PinSceneComponent() is deprecated and can no longer be used to pin any scene component other than itself. Use PinToBestFit(), PinToID() or PinToRestoredOrSyncedID() instead."));
		return false;
	}

	PinToBestFit();

	return true;
}

bool UMagicLeapARPinComponent::PinActor(AActor* ActorToPin)
{
	if (ActorToPin != GetOwner())
	{
		UE_LOG(LogMagicLeapARPin, Error, TEXT("PinActor() is deprecated and can no longer be used to pin any actor other than the owner of this component. Set bShouldPinActor to true and call PinToBestFit(), PinToID() or PinToRestoredOrSyncedID() instead."));
		return false;
	}

	bShouldPinActor = true;
	PinToBestFit();

	return true;
}

void UMagicLeapARPinComponent::UnPin()
{
	PinnedSceneComponent = nullptr;
	bHasValidPin = false;

	if (ObjectUID.Len() != 0)
	{
		IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
		if (ARPinImpl != nullptr)
		{
			ARPinImpl->RemoveContentBindingAsync(PinData->PinnedID, ObjectUID);
		}

		UGameplayStatics::DeleteGameInSlot(ObjectUID, UserIndex);
		PinData = nullptr;
		bDataRestored = false;
	}
}

bool UMagicLeapARPinComponent::IsPinned() const
{
	return bHasValidPin && bPinFoundInEnvironmentPrevFrame;
}

bool UMagicLeapARPinComponent::PinRestoredOrSynced() const
{
	return bDataRestored;
}

bool UMagicLeapARPinComponent::GetPinnedPinID(FGuid& PinID) const
{
	if (bHasValidPin)
	{
		PinID = PinnedCFUID;
	}
	return bHasValidPin;
}

UMagicLeapARPinSaveGame* UMagicLeapARPinComponent::GetPinData(TSubclassOf<UMagicLeapARPinSaveGame> InPinDataClass)
{
	checkf(InPinDataClass == PinDataClass, TEXT("Attempted to get user data object of different type than what was defined in MagicLeapARPinComponent!"));

	// Attempt to load the pin data first. If one doesnt exist, then create a new object.
	AttemptPinDataRestoration();

	if (PinData == nullptr)
	{
		PinData = Cast<UMagicLeapARPinSaveGame>(UGameplayStatics::CreateSaveGameObject(PinDataClass));
	}

	return PinData;
}

UMagicLeapARPinSaveGame* UMagicLeapARPinComponent::TryGetPinData(TSubclassOf<UMagicLeapARPinSaveGame> InPinDataClass, bool& OutPinDataValid)
{
	if (PinData == nullptr)
	{
		if (UGameplayStatics::DoesSaveGameExist(ObjectUID, UserIndex))
		{
			AttemptPinDataRestorationAsync();
		}
		else
		{
			PinData = Cast<UMagicLeapARPinSaveGame>(UGameplayStatics::CreateSaveGameObject(PinDataClass));
		}
	}

	OutPinDataValid = (PinData != nullptr);

	return PinData;
}

bool UMagicLeapARPinComponent::GetPinState(FMagicLeapARPinState& State) const
{
	EMagicLeapPassableWorldError Error = EMagicLeapPassableWorldError::Unavailable;
	if (bHasValidPin)
	{
		Error = UMagicLeapARPinFunctionLibrary::GetARPinState(PinnedCFUID, State);
	}
	return Error == EMagicLeapPassableWorldError::None;
}

bool UMagicLeapARPinComponent::AttemptPinDataRestoration()
{
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
		// Only restore once
		if (PinData == nullptr)
		{
			OnLoadGameFromSlot(ObjectUID, UserIndex, UGameplayStatics::LoadGameFromSlot(ObjectUID, UserIndex));
		}
	}
	else
	{
		UE_LOG(LogMagicLeapARPin, Warning, TEXT("ObjectUID is empty. A non-empty unique ID is required to make the object persistent."));
	}

	return (PinData != nullptr);
}

void UMagicLeapARPinComponent::AttemptPinDataRestorationAsync()
{
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
		// Only restore once
		if (PinData == nullptr)
		{
			UGameplayStatics::AsyncLoadGameFromSlot(ObjectUID, UserIndex, LoadGameDelegate);
		}
	}
	else
	{
		UE_LOG(LogMagicLeapARPin, Warning, TEXT("ObjectUID is empty. A non-empty unique ID is required to make the object persistent."));
	}
}

void UMagicLeapARPinComponent::SelectComponentToPin()
{
	if (bShouldPinActor && GetOwner() != nullptr)
	{
		PinnedSceneComponent = GetOwner()->GetRootComponent();
	}
	else
	{
		PinnedSceneComponent = this;
	}
}

void UMagicLeapARPinComponent::SavePinData(const FGuid& InPinID, const FTransform& InComponentToWorld, const FTransform& InPinTransform, bool bRegisterDelegate)
{
	if (ObjectUID.Len() != 0)
	{
		if (PinData == nullptr)
		{
			PinData = Cast<UMagicLeapARPinSaveGame>(UGameplayStatics::CreateSaveGameObject(PinDataClass));
		}
		check(PinData);

		PinData->PinnedID = InPinID;
		PinData->ComponentWorldTransform = InComponentToWorld;
		PinData->PinTransform = InPinTransform;
		PinData->bShouldPinActor = bShouldPinActor;

		if (bRegisterDelegate)
		{
			UGameplayStatics::AsyncSaveGameToSlot(PinData, ObjectUID, UserIndex, SaveGameDelegate);
		}
		else
		{
			UGameplayStatics::AsyncSaveGameToSlot(PinData, ObjectUID, UserIndex);
		}

		IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
		if (ARPinImpl != nullptr)
		{
			ARPinImpl->AddContentBindingAsync(InPinID, ObjectUID);
		}
	}
}

EMagicLeapPassableWorldError UMagicLeapARPinComponent::FindBestFitPin(IMagicLeapARPinFeature* ARPinImpl, FGuid& FoundPin)
{
	FMagicLeapARPinQuery Query;
	Query.Types = SearchPinTypes;
	Query.MaxResults = kMaxResultsForComponent;
	Query.TargetPoint = SearchVolume->GetComponentLocation();
	Query.Radius = SearchVolume->GetScaledSphereRadius();
	Query.bSorted = true;

	TArray<FGuid> FoundPins;
	EMagicLeapPassableWorldError Error = ARPinImpl->QueryARPins(Query, FoundPins);
	if (Error == EMagicLeapPassableWorldError::None && FoundPins.Num() > 0)
	{
		FoundPin = FoundPins[0];
	}
	else if (Error == EMagicLeapPassableWorldError::NotImplemented)
	{
		Error = UMagicLeapARPinFunctionLibrary::GetClosestARPin(PinnedSceneComponent->GetComponentLocation(), FoundPin);
	}

	return Error;
}

bool UMagicLeapARPinComponent::TryInitOldTransformData()
{
	FVector PinWorldPosition = FVector::ZeroVector;
	FRotator PinWorldOrientation = FRotator::ZeroRotator;
	bool bInEnvironment = false;
	const bool bCFUIDTransform = UMagicLeapARPinFunctionLibrary::GetARPinPositionAndOrientation(PinnedCFUID, PinWorldPosition, PinWorldOrientation, bInEnvironment);
	if (bCFUIDTransform)
	{
		bHasValidPin = true;
		OldComponentWorldTransform = PinnedSceneComponent->GetComponentToWorld();
		OldCFUIDTransform = FTransform(PinWorldOrientation, PinWorldPosition);
		SavePinData(PinnedCFUID, OldComponentWorldTransform, OldCFUIDTransform, true /* bRegisterDelegate */);
	}

	return bCFUIDTransform;
}

void UMagicLeapARPinComponent::OnSaveGameToSlot(const FString& InSlotName, const int32 InUserIndex, bool bDataSaved)
{
	UE_CLOG(!bDataSaved, LogMagicLeapARPin, Error, TEXT("Error saving persistent data for MagicLeapARPin %s."), *InSlotName);
}

void UMagicLeapARPinComponent::OnLoadGameFromSlot(const FString& InSlotName, const int32 InUserIndex, USaveGame* InSaveGameObj)
{
	if (InSaveGameObj != nullptr)
	{
		PinData = Cast<UMagicLeapARPinSaveGame>(InSaveGameObj);
		if (PinData)
		{
			PinnedCFUID = PinData->PinnedID;
			OldComponentWorldTransform = PinData->ComponentWorldTransform;
			OldCFUIDTransform = PinData->PinTransform;
			bShouldPinActor = PinData->bShouldPinActor;
			bDataRestored = true;
			bAttemptedPinningAfterDataRestoration = false;			
		}
	}

	OnPinDataLoadAttemptCompleted.Broadcast(PinData != nullptr);
}
