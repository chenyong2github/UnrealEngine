// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorSequenceComponent.h"
#include "ActorSequence.h"
#include "ActorSequencePlayer.h"

UActorSequenceComponent::UActorSequenceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;

	if (HasAnyFlags(RF_ClassDefaultObject) || GetArchetype() == GetDefault<UActorSequenceComponent>())
	{
		Sequence = ObjectInitializer.CreateDefaultSubobject<UActorSequence>(this, "Sequence");
		Sequence->SetFlags(RF_Public | RF_Transactional);
	}
}

void UActorSequenceComponent::PostInitProperties()
{
	Super::PostInitProperties();
}

void UActorSequenceComponent::BeginPlay()
{
	Super::BeginPlay();

	if (Sequence != nullptr)
	{
		SequencePlayer = NewObject<UActorSequencePlayer>(this, "SequencePlayer");

		SequencePlayer->SetPlaybackClient(this);

		// Initialize this player for tick as soon as possible to ensure that a persistent
		// reference to the tick manager is maintained
		SequencePlayer->InitializeForTick(this);

		SequencePlayer->Initialize(Sequence, PlaybackSettings);

		if (PlaybackSettings.bAutoPlay)
		{
			SequencePlayer->Play();
		}

		UMovieSceneSequenceTickManager* TickManager = SequencePlayer->GetTickManager();
		if (ensure(TickManager))
		{
			TickManager->RegisterSequenceActor(GetOwner(), this);
		}
	}
}

void UActorSequenceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SequencePlayer)
	{
		// Stop the internal sequence player during EndPlay when it's safer
		// to modify other actor's state during state restoration.
		SequencePlayer->Stop();

		if (UMovieSceneSequenceTickManager* TickManager = SequencePlayer->GetTickManager())
		{
			TickManager->UnregisterSequenceActor(GetOwner(), this);
		}
	}
	
	Super::EndPlay(EndPlayReason);
}

void UActorSequenceComponent::TickComponent(float DeltaSeconds, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);
}

void UActorSequenceComponent::TickFromSequenceTickManager(float DeltaSeconds)
{
	if (SequencePlayer)
	{
		SequencePlayer->UpdateAsync(DeltaSeconds);
	}
}

