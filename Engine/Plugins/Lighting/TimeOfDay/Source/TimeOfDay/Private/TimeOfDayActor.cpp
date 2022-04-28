// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeOfDayActor.h"
#include "Engine/Texture2D.h"
#include "Components/BillboardComponent.h"
#include "Engine/ActorChannel.h"
#include "MovieSceneTimeHelpers.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Sections/MovieSceneSubSection.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "LevelSequenceActor.h"


ATimeOfDayActor::ATimeOfDayActor(const FObjectInitializer& Init)
: Super(Init)
, bRunDayCycle(true)
, DayLength(24, 0, 0, 0, false)
, TimePerCycle(0, 5, 0, 0, false)
, InitialTimeOfDay(6, 0, 0, 0, false)
{
	USceneComponent* SceneRootComponent = CreateDefaultSubobject<USceneComponent>(USceneComponent::GetDefaultSceneRootVariableName());
	SetRootComponent(SceneRootComponent);

#if WITH_EDITORONLY_DATA
	UBillboardComponent* SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));

	if (!IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> DecalTexture;
			FConstructorStatics() : DecalTexture(TEXT("/Engine/EditorResources/S_LevelSequence")) {}
		};
		static FConstructorStatics ConstructorStatics;

		if (SpriteComponent)
		{
			SpriteComponent->Sprite = ConstructorStatics.DecalTexture.Get();
			SpriteComponent->SetupAttachment(RootComponent);
			SpriteComponent->bIsScreenSizeScaled = true;
			SpriteComponent->SetUsingAbsoluteScale(true);
			SpriteComponent->bReceivesDecals = false;
			SpriteComponent->bHiddenInGame = true;
		}
	}

	bIsSpatiallyLoaded = false;

	TimeOfDayPreview = FTimecode(6, 0, 0, 0, false);
#endif // WITH_EDITORONLY_DATA

	// The TimeOfDayActor defaults to never ticking by the tick manager because it is ticked separately in LevelTick
	//PrimaryActorTick.bCanEverTick = false;

	// SequencePlayer must be a default sub object for it to be replicated correctly
	SequencePlayer = Init.CreateDefaultSubobject<ULevelSequencePlayer>(this, "AnimationPlayer");
	BindingOverrides = Init.CreateDefaultSubobject<UMovieSceneBindingOverrides>(this, "BindingOverrides");

	bReplicates = true;
	bReplicatePlayback = false;
	bReplicateUsingRegisteredSubObjectList = false;
}

void ATimeOfDayActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (HasAuthority())
	{
		SetReplicates(bReplicatePlayback);
	}
	
	// Initialize this player for tick as soon as possible to ensure that a persistent
	// reference to the tick manager is maintained
	SequencePlayer->InitializeForTick(this);

	InitializePlayer();
}

ULevelSequencePlayer* ATimeOfDayActor::GetSequencePlayer() const
{
	return SequencePlayer && SequencePlayer->GetSequence() ? SequencePlayer : nullptr;
}

ULevelSequence* ATimeOfDayActor::GetDaySequence() const
{
	return DaySequenceAsset;
}

void ATimeOfDayActor::SetDaySequence(ULevelSequence* InSequence)
{
	if (!SequencePlayer->IsPlaying())
	{
		UpdateDaySequence(InSequence);
	}
}

void ATimeOfDayActor::SetReplicatePlayback(bool bInReplicatePlayback)
{
	bReplicatePlayback = bInReplicatePlayback;
	SetReplicates(bReplicatePlayback);
}

bool ATimeOfDayActor::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool bWroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	bWroteSomething |= Channel->ReplicateSubobject(SequencePlayer, *Bunch, *RepFlags);

	return bWroteSomething;
}

void ATimeOfDayActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATimeOfDayActor, SequencePlayer);
}

void ATimeOfDayActor::PostInitProperties()
{
	Super::PostInitProperties();

	// Have to initialize this here as any properties set on default subobjects inside the constructor
	// get stomped by the CDO's properties when the constructor exits.
	SequencePlayer->SetPlaybackClient(this);
}

void ATimeOfDayActor::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	// Fix sprite component so that it's attached to the root component. In the past, the sprite component was the root component.
	UBillboardComponent* SpriteComponent = FindComponentByClass<UBillboardComponent>();
	if (SpriteComponent && SpriteComponent->GetAttachParent() != RootComponent)
	{
		SpriteComponent->SetupAttachment(RootComponent);
	}
#endif
}

void ATimeOfDayActor::BeginPlay()
{
	UMovieSceneSequenceTickManager* TickManager = SequencePlayer->GetTickManager();
	if (ensure(TickManager))
	{
		TickManager->RegisterSequenceActor(this);
	}
	
	Super::BeginPlay();

	// Day cycle playback settings will always play. Pause if RunDayCycle is
	// off to allow sequence spawnables and settings to be set from initial
	// time of day.
	SequencePlayer->Play();
	
	if (!bRunDayCycle)
	{
		SequencePlayer->Pause();
	}
}

void ATimeOfDayActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ATimeOfDayActor::RewindForReplay()
{
	Super::RewindForReplay();
}

void ATimeOfDayActor::TickFromSequenceTickManager(float DeltaSeconds)
{
	if (SequencePlayer)
	{
		SequencePlayer->UpdateAsync(DeltaSeconds);
	}
}

bool ATimeOfDayActor::RetrieveBindingOverrides(const FGuid& InBindingId, FMovieSceneSequenceID InSequenceID, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	// Specify MovieSceneSequenceID::Root while the sequence is always the top level master sequence.
	return BindingOverrides->LocateBoundObjects(InBindingId, MovieSceneSequenceID::Root, OutObjects);
}

UObject* ATimeOfDayActor::GetInstanceData() const
{
	return nullptr;
}

bool ATimeOfDayActor::GetIsReplicatedPlayback() const
{
	return bReplicatePlayback;
}

#if WITH_EDITOR
TSharedPtr<FStructOnScope> ATimeOfDayActor::GetObjectPickerProxy(TSharedPtr<IPropertyHandle> ObjectPropertyHandle)
{
	TSharedRef<FStructOnScope> Struct = MakeShared<FStructOnScope>(FBoundActorProxy::StaticStruct());
	reinterpret_cast<FBoundActorProxy*>(Struct->GetStructMemory())->Initialize(ObjectPropertyHandle);
	return Struct;
}

void ATimeOfDayActor::UpdateObjectFromProxy(FStructOnScope& Proxy, IPropertyHandle& ObjectPropertyHandle)
{
	UObject* BoundActor = reinterpret_cast<FBoundActorProxy*>(Proxy.GetStructMemory())->BoundActor;
	ObjectPropertyHandle.SetValue(BoundActor);
}

bool ATimeOfDayActor::GetReferencedContentObjects(TArray<UObject*>& Objects) const
{
	if (DaySequenceAsset)
	{
		Objects.Add(DaySequenceAsset);
	}
	Super::GetReferencedContentObjects(Objects);
	return true;
}
#endif

void ATimeOfDayActor::InitializePlayer()
{
	MasterSequence = NewObject<ULevelSequence>(this, NAME_None, RF_Transactional);
	MasterSequence->Initialize();
	MasterSequence->SetSequenceFlags(EMovieSceneSequenceFlags::Volatile);

	UMovieScene* MasterMovieScene = MasterSequence->GetMovieScene();
	const int32 MasterDuration = MasterMovieScene->GetTickResolution().AsFrameNumber(1).Value;
	MasterMovieScene->SetPlaybackRange(0, MasterDuration);
	MasterMovieScene->AddMasterTrack<UMovieSceneSubTrack>();

	SequencePlayer->Initialize(MasterSequence, GetLevel(), GetPlaybackSettings(MasterSequence), FLevelSequenceCameraSettings());

	UpdateDaySequence(DaySequenceAsset);
}

void ATimeOfDayActor::UpdateDaySequence(ULevelSequence* SequenceAsset)
{
	if (!MasterSequence)
	{
		return;
	}

	UMovieScene* MasterMovieScene = MasterSequence->GetMovieScene();
	const int32 MasterDuration = MasterMovieScene->GetPlaybackRange().GetUpperBoundValue().Value;
	
	if (SequenceAsset && GetWorld()->IsGameWorld())
	{
		UMovieSceneSubTrack* SubTrack = MasterMovieScene->FindMasterTrack<UMovieSceneSubTrack>();

		for (UMovieSceneSection* Section : SubTrack->GetAllSections())
		{
			UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section);
			if (SubSection && SubSection->GetSequence() == DaySequenceAsset)
			{
				SubTrack->RemoveSection(*SubSection);
				break;
			}
		}

		DaySequenceAsset = SequenceAsset;

		// Compute outer duration from subsequence asset.
		const FFrameRate TickResolution = DaySequenceAsset->GetMovieScene()->GetTickResolution();
		const FQualifiedFrameTime InnerDuration = FQualifiedFrameTime(
			UE::MovieScene::DiscreteSize(DaySequenceAsset->GetMovieScene()->GetPlaybackRange()),
			TickResolution);

		const FFrameRate OuterFrameRate = SubTrack->GetTypedOuter<UMovieScene>()->GetTickResolution();
		const int32      OuterDuration  = InnerDuration.ConvertTo(OuterFrameRate).FrameNumber.Value;
		
		UMovieSceneSubSection* SubSection = SubTrack->AddSequence(DaySequenceAsset, 0, OuterDuration);
		SubSection->Parameters.TimeScale = (float)OuterDuration / (float)MasterDuration;
	}
}

FMovieSceneSequencePlaybackSettings ATimeOfDayActor::GetPlaybackSettings(const ULevelSequence* Sequence) const
{
	FMovieSceneSequencePlaybackSettings Settings;
	Settings.bAutoPlay = true;
	Settings.LoopCount.Value = -1; // Loop indefinitely
	Settings.PlayRate = 1.0f;

	if (!Sequence)
	{
		return Settings;
	}

	// Update the PlayRate and StartOffset from the DayCycle settings.
	if (const UMovieScene* MovieScene = Sequence->GetMovieScene())
	{
		const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
		const FFrameRate TickResolution = MovieScene->GetTickResolution();
		const FFrameNumber FramesPerCycle = TimePerCycle.ToFrameNumber(DisplayRate);
		const FQualifiedFrameTime DayFrameTime(TimePerCycle, DisplayRate);
		
		const TRange<FFrameNumber> MoviePlaybackRange = MovieScene->GetPlaybackRange();
		if (MoviePlaybackRange.GetLowerBound().IsClosed() && MoviePlaybackRange.GetUpperBound().IsClosed() && FramesPerCycle.Value > 0)
		{
			const FFrameNumber SrcStartFrame = UE::MovieScene::DiscreteInclusiveLower(MoviePlaybackRange);
			const FFrameNumber SrcEndFrame   = UE::MovieScene::DiscreteExclusiveUpper(MoviePlaybackRange);

			const FFrameTime EndingTime = ConvertFrameTime(SrcEndFrame, TickResolution, DisplayRate);
			const FFrameNumber StartingFrame = ConvertFrameTime(SrcStartFrame, TickResolution, DisplayRate).FloorToFrame();
			const FFrameNumber EndingFrame   = EndingTime.FloorToFrame();
			
			const FQualifiedFrameTime MovieFrameTime(FTimecode(0,0,0,EndingFrame.Value - StartingFrame.Value, false), DisplayRate);
			Settings.PlayRate = MovieFrameTime.AsSeconds() / DayFrameTime.AsSeconds();

			const FFrameNumber FramesDayLength = DayLength.ToFrameNumber(DisplayRate);
			const FFrameNumber FramesStartTime = InitialTimeOfDay.ToFrameNumber(DisplayRate);
			if (FramesDayLength.Value > 0)
			{
				const FTimecode InitialTimeOfDayCorrected(0, 0, 0, FramesStartTime.Value % FramesDayLength.Value, false);
				const FQualifiedFrameTime DayLengthTime(DayLength, DisplayRate);
				const FQualifiedFrameTime StartTime(InitialTimeOfDayCorrected, DisplayRate);
				const double StartTimeRatio = StartTime.AsSeconds() / DayLengthTime.AsSeconds();

				// StartTime is in seconds in movie sequence time.
				Settings.StartTime = StartTimeRatio * MovieFrameTime.AsSeconds();
			}
		}
	}

	return Settings;
}


