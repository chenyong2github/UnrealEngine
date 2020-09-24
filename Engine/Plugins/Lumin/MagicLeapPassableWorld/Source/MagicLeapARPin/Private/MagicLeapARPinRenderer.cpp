// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debug/MagicLeapARPinRenderer.h"
#include "Debug/MagicLeapARPinInfoActorBase.h"
#include "UObject/ConstructorHelpers.h"

AMagicLeapARPinRenderer::AMagicLeapARPinRenderer()
: bInfoActorsVisibilityOverride(true)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	static ConstructorHelpers::FClassFinder<AMagicLeapARPinInfoActorBase> InfoActorClass(TEXT("/MagicLeapPassableWorld/MagicLeapARPinInfoActor"));
	ClassToSpawn = (InfoActorClass.Class != nullptr) ? InfoActorClass.Class : TSubclassOf<AMagicLeapARPinInfoActorBase>(AMagicLeapARPinInfoActorBase::StaticClass());
}

void AMagicLeapARPinRenderer::BeginPlay()
{
	Super::BeginPlay();

	IMagicLeapARPinFeature* ARPinFeature = IMagicLeapARPinFeature::Get();
	if (ARPinFeature != nullptr)
	{
		DelegateHandle = ARPinFeature->OnMagicLeapARPinUpdated().AddUObject(this, &AMagicLeapARPinRenderer::OnARPinsUpdated);

		if (ARPinFeature->IsTrackerValid())
		{
			TArray<FGuid> ExistingPins;
			EMagicLeapPassableWorldError Result = ARPinFeature->QueryARPins(ARPinFeature->GetGlobalQueryFilter(), ExistingPins);
			if (Result == EMagicLeapPassableWorldError::NotImplemented)
			{
				ARPinFeature->GetAvailableARPins(-1, ExistingPins);
			}

			OnARPinsUpdated(ExistingPins, TArray<FGuid>(), TArray<FGuid>());
		}
		else
		{
			ARPinFeature->CreateTracker();
		}
	}
}

void AMagicLeapARPinRenderer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	IMagicLeapARPinFeature* ARPinFeature = IMagicLeapARPinFeature::Get();
	if (ARPinFeature != nullptr)
	{
		ARPinFeature->OnMagicLeapARPinUpdated().Remove(DelegateHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void AMagicLeapARPinRenderer::OnARPinsUpdated(const TArray<FGuid>& Added, const TArray<FGuid>& Updated, const TArray<FGuid>& Deleted)
{
	for (const FGuid& DeletedPinID : Deleted)
	{
		AMagicLeapARPinInfoActorBase** ActorPtr = AllInfoActors.Find(DeletedPinID);
		if (ActorPtr != nullptr && *ActorPtr != nullptr)
		{
			(*ActorPtr)->K2_DestroyActor();
			AllInfoActors.Remove(DeletedPinID);
		}
	}

	for (const FGuid& AddedPinID : Added)
	{
		AMagicLeapARPinInfoActorBase* Actor = Cast<AMagicLeapARPinInfoActorBase>(GetWorld()->SpawnActor(*ClassToSpawn, &FTransform::Identity));
		if (Actor != nullptr)
		{
			AllInfoActors.Add(AddedPinID, Actor);
			Actor->PinID = AddedPinID;
			Actor->bVisibilityOverride = bInfoActorsVisibilityOverride;
			Actor->OnUpdateARPinState();
		}
	}

	for (const FGuid& UpdatedPinID : Updated)
	{
		AMagicLeapARPinInfoActorBase** ActorPtr = AllInfoActors.Find(UpdatedPinID);
		if (ActorPtr != nullptr && *ActorPtr != nullptr)
		{
			(*ActorPtr)->OnUpdateARPinState();
		}
	}
}

void AMagicLeapARPinRenderer::SetVisibilityOverride(const bool InVisibilityOverride)
{
	bInfoActorsVisibilityOverride = InVisibilityOverride;
	for (auto& Pair : AllInfoActors)
	{
		Pair.Value->bVisibilityOverride = bInfoActorsVisibilityOverride;
	}
}
