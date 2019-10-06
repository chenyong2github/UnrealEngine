// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TemplateSequenceActor.h"
#include "TemplateSequence.h"
#include "Engine/World.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Net/UnrealNetwork.h"

ATemplateSequenceActor::ATemplateSequenceActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComp"));
	RootComponent = SceneComponent;

	SequencePlayer = ObjectInitializer.CreateDefaultSubobject<UTemplateSequencePlayer>(this, "AnimationPlayer");

	// Note: we don't set `bCanEverTick` because we add this actor to the list of level sequence actors in the world.
	//PrimaryActorTick.bCanEverTick = false;
}

void ATemplateSequenceActor::PostInitProperties()
{
	Super::PostInitProperties();

	// Have to initialize this here as any properties set on default subobjects inside the constructor
	// Get stomped by the CDO's properties when the constructor exits.
	SequencePlayer->SetPlaybackClient(this);
}

void ATemplateSequenceActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATemplateSequenceActor, SequencePlayer);
}

UTemplateSequencePlayer* ATemplateSequenceActor::GetSequencePlayer() const
{
	return SequencePlayer && SequencePlayer->GetSequence() ? SequencePlayer : nullptr;
}

void ATemplateSequenceActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	InitializePlayer();
}

void ATemplateSequenceActor::BeginPlay()
{
	GetWorld()->LevelSequenceActors.Add(this);

	Super::BeginPlay();
	
	if (PlaybackSettings.bAutoPlay)
	{
		SequencePlayer->Play();
	}
}

void ATemplateSequenceActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SequencePlayer)
	{
		// See comment in LevelSequenceActor.cpp
		SequencePlayer->Stop();
	}

	GetWorld()->LevelSequenceActors.Remove(this);

	Super::EndPlay(EndPlayReason);
}

void ATemplateSequenceActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (SequencePlayer)
	{
		SequencePlayer->Update(DeltaSeconds);
	}
}

UTemplateSequence* ATemplateSequenceActor::GetSequence() const
{
	return Cast<UTemplateSequence>(TemplateSequence.ResolveObject());
}

UTemplateSequence* ATemplateSequenceActor::LoadSequence() const
{
	return Cast<UTemplateSequence>(TemplateSequence.TryLoad());
}

void ATemplateSequenceActor::SetSequence(UTemplateSequence* InSequence)
{
	if (!SequencePlayer->IsPlaying())
	{
		TemplateSequence = InSequence;

		if (InSequence)
		{
			SequencePlayer->Initialize(InSequence, GetWorld(), PlaybackSettings);
		}
	}
}

void ATemplateSequenceActor::InitializePlayer()
{
	if (TemplateSequence.IsValid() && GetWorld()->IsGameWorld())
	{
		// Attempt to resolve the asset without loading it
		UTemplateSequence* TemplateSequenceAsset = GetSequence();
		if (TemplateSequenceAsset)
		{
			// Level sequence is already loaded. Initialize the player if it's not already initialized with this sequence
			if (TemplateSequenceAsset != SequencePlayer->GetSequence())
			{
				SequencePlayer->Initialize(TemplateSequenceAsset, GetWorld(), PlaybackSettings);
			}
		}
		else if (!IsAsyncLoading())
		{
			TemplateSequenceAsset = LoadSequence();
			if (TemplateSequenceAsset != SequencePlayer->GetSequence())
			{
				SequencePlayer->Initialize(TemplateSequenceAsset, GetWorld(), PlaybackSettings);
			}
		}
		else
		{
			LoadPackageAsync(TemplateSequence.GetLongPackageName(), FLoadPackageAsyncDelegate::CreateUObject(this, &ATemplateSequenceActor::OnSequenceLoaded));
		}
	}
}

void ATemplateSequenceActor::OnSequenceLoaded(const FName& PackageName, UPackage* Package, EAsyncLoadingResult::Type Result)
{
	if (Result == EAsyncLoadingResult::Succeeded)
	{
		UTemplateSequence* TemplateSequenceAsset = GetSequence();
		if (SequencePlayer && SequencePlayer->GetSequence() != TemplateSequenceAsset)
		{
			SequencePlayer->Initialize(TemplateSequenceAsset, GetWorld(), PlaybackSettings);
		}
	}
}

bool ATemplateSequenceActor::RetrieveBindingOverrides(const FGuid& InBindingId, FMovieSceneSequenceID InSequenceID, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	if (UObject* Object = BindingOverride.Object.Get())
	{
		OutObjects.Add(Object);
		return false;
	}

	// No binding overrides, use default binding.
	return true;
}

UObject* ATemplateSequenceActor::GetInstanceData() const
{
	return nullptr;
}

void ATemplateSequenceActor::SetBinding(AActor* Actor)
{
	BindingOverride.Object = Actor;

	UTemplateSequence* TemplateSequenceObject = GetSequence();
	if (SequencePlayer && TemplateSequenceObject)
	{
		UMovieScene* MovieScene = TemplateSequenceObject->GetMovieScene();
		if (MovieScene != nullptr && MovieScene->GetBindings().Num() > 0)
		{
			const FMovieSceneBinding& Binding = MovieScene->GetBindings()[0];
			SequencePlayer->State.Invalidate(Binding.GetObjectGuid(), MovieSceneSequenceID::Root);
		}
	}
}

#if WITH_EDITOR

bool ATemplateSequenceActor::GetReferencedContentObjects(TArray<UObject*>& Objects) const
{
	UTemplateSequence* TemplateSequenceAsset = LoadSequence();

	if (TemplateSequenceAsset)
	{
		Objects.Add(TemplateSequenceAsset);
	}

	Super::GetReferencedContentObjects(Objects);

	return true;
}

#endif // WITH_EDITOR
