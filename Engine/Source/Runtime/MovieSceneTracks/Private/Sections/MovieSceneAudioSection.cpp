// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneAudioSection.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Sound/SoundBase.h"
#include "UObject/SequencerObjectVersion.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"

#if WITH_EDITOR

struct FAudioChannelEditorData
{
	FAudioChannelEditorData()
	{
		Data[0].SetIdentifiers("Volume", NSLOCTEXT("MovieSceneAudioSection", "SoundVolumeText", "Volume"));
		Data[1].SetIdentifiers("Pitch", NSLOCTEXT("MovieSceneAudioSection", "PitchText", "Pitch"));
		Data[2].SetIdentifiers("AttachActor", NSLOCTEXT("MovieSceneAudioSection", "AttachActorText", "Attach"));
	}

	FMovieSceneChannelMetaData Data[3];
};

#endif // WITH_EDITOR

namespace
{
	float AudioDeprecatedMagicNumber = TNumericLimits<float>::Lowest();

	FFrameNumber GetStartOffsetAtTrimTime(FQualifiedFrameTime TrimTime, FFrameNumber StartOffset, FFrameNumber StartFrame)
	{
		return StartOffset + TrimTime.Time.FrameNumber - StartFrame;
	}
}

UMovieSceneAudioSection::UMovieSceneAudioSection( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	Sound = nullptr;
	StartOffset_DEPRECATED = AudioDeprecatedMagicNumber;
	AudioStartTime_DEPRECATED = AudioDeprecatedMagicNumber;
	AudioDilationFactor_DEPRECATED = AudioDeprecatedMagicNumber;
	AudioVolume_DEPRECATED = AudioDeprecatedMagicNumber;
	bLooping = true;
	bSuppressSubtitles = false;
	bOverrideAttenuation = false;
	BlendType = EMovieSceneBlendType::Absolute;

	EvalOptions.EnableAndSetCompletionMode
		(GetLinkerCustomVersion(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::WhenFinishedDefaultsToProjectDefault ? 
			EMovieSceneCompletionMode::RestoreState : 
			EMovieSceneCompletionMode::ProjectDefault);

	SoundVolume.SetDefault(1.f);
	PitchMultiplier.SetDefault(1.f);
}

EMovieSceneChannelProxyType  UMovieSceneAudioSection::CacheChannelProxy()
{
	// Set up the channel proxy
	FMovieSceneChannelProxyData Channels;

	UMovieSceneAudioTrack* AudioTrack = Cast<UMovieSceneAudioTrack>(GetOuter());

#if WITH_EDITOR

	FAudioChannelEditorData EditorData;
	Channels.Add(SoundVolume,     EditorData.Data[0], TMovieSceneExternalValue<float>());
	Channels.Add(PitchMultiplier, EditorData.Data[1], TMovieSceneExternalValue<float>());

	if (AudioTrack && AudioTrack->IsAMasterTrack())
	{
		Channels.Add(AttachActorData, EditorData.Data[2]);
	}

#else

	Channels.Add(SoundVolume);
	Channels.Add(PitchMultiplier);
	if (AudioTrack && AudioTrack->IsAMasterTrack())
	{
		Channels.Add(AttachActorData);
	}

#endif

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));

	return EMovieSceneChannelProxyType::Dynamic;
}

TOptional<FFrameTime> UMovieSceneAudioSection::GetOffsetTime() const
{
	return TOptional<FFrameTime>(StartFrameOffset);
}

void UMovieSceneAudioSection::PostLoad()
{
	Super::PostLoad();

	if (AudioDilationFactor_DEPRECATED != AudioDeprecatedMagicNumber)
	{
		PitchMultiplier.SetDefault(AudioDilationFactor_DEPRECATED);

		AudioDilationFactor_DEPRECATED = AudioDeprecatedMagicNumber;
	}

	if (AudioVolume_DEPRECATED != AudioDeprecatedMagicNumber)
	{
		SoundVolume.SetDefault(AudioVolume_DEPRECATED);

		AudioVolume_DEPRECATED = AudioDeprecatedMagicNumber;
	}

	TOptional<double> StartOffsetToUpgrade;
	if (AudioStartTime_DEPRECATED != AudioDeprecatedMagicNumber)
	{
		// Previously, start time in relation to the sequence. Start time was used to calculate the offset into the 
		// clip at the start of the section evaluation as such: Section Start Time - Start Time. 
		if (AudioStartTime_DEPRECATED != 0.f && HasStartFrame())
		{
			StartOffsetToUpgrade = GetInclusiveStartFrame() / GetTypedOuter<UMovieScene>()->GetTickResolution() - AudioStartTime_DEPRECATED;
		}
		AudioStartTime_DEPRECATED = AudioDeprecatedMagicNumber;
	}

	if (StartOffset_DEPRECATED != AudioDeprecatedMagicNumber)
	{
		StartOffsetToUpgrade = StartOffset_DEPRECATED;

		StartOffset_DEPRECATED = AudioDeprecatedMagicNumber;
	}

	FFrameRate LegacyFrameRate = GetLegacyConversionFrameRate();

	if (StartOffsetToUpgrade.IsSet())
	{
		FFrameRate DisplayRate = GetTypedOuter<UMovieScene>()->GetDisplayRate();
		FFrameRate TickResolution = GetTypedOuter<UMovieScene>()->GetTickResolution();

		StartFrameOffset = ConvertFrameTime(FFrameTime::FromDecimal(DisplayRate.AsDecimal() * StartOffsetToUpgrade.GetValue()), DisplayRate, TickResolution).FrameNumber;
	}
}
	
TOptional<TRange<FFrameNumber> > UMovieSceneAudioSection::GetAutoSizeRange() const
{
	if (!Sound)
	{
		return TRange<FFrameNumber>();
	}

	float SoundDuration = MovieSceneHelpers::GetSoundDuration(Sound);

	FFrameRate FrameRate = GetTypedOuter<UMovieScene>()->GetTickResolution();

	// determine initial duration
	// @todo Once we have infinite sections, we can remove this
	// @todo ^^ Why? Infinte sections would mean there's no starting time?
	FFrameTime DurationToUse = 1.f * FrameRate; // if all else fails, use 1 second duration

	if (SoundDuration != INDEFINITELY_LOOPING_DURATION)
	{
		DurationToUse = SoundDuration * FrameRate;
	}

	return TRange<FFrameNumber>(GetInclusiveStartFrame(), GetInclusiveStartFrame() + DurationToUse.FrameNumber);
}

	
void UMovieSceneAudioSection::TrimSection(FQualifiedFrameTime TrimTime, bool bTrimLeft, bool bDeleteKeys)
{
	SetFlags(RF_Transactional);

	if (TryModify())
	{
		if (bTrimLeft)
		{
			StartFrameOffset = HasStartFrame() ? GetStartOffsetAtTrimTime(TrimTime, StartFrameOffset, GetInclusiveStartFrame()) : 0;
		}

		Super::TrimSection(TrimTime, bTrimLeft, bDeleteKeys);
	}
}

UMovieSceneSection* UMovieSceneAudioSection::SplitSection(FQualifiedFrameTime SplitTime, bool bDeleteKeys)
{
	const FFrameNumber InitialStartFrameOffset = StartFrameOffset;

	const FFrameNumber NewOffset = HasStartFrame() ? GetStartOffsetAtTrimTime(SplitTime, StartFrameOffset, GetInclusiveStartFrame()) : 0;

	UMovieSceneSection* NewSection = Super::SplitSection(SplitTime, bDeleteKeys);
	if (NewSection != nullptr)
	{
		UMovieSceneAudioSection* NewAudioSection = Cast<UMovieSceneAudioSection>(NewSection);
		NewAudioSection->StartFrameOffset = NewOffset;
	}

	// Restore original offset modified by splitting
	StartFrameOffset = InitialStartFrameOffset;

	return NewSection;
}


USceneComponent* UMovieSceneAudioSection::GetAttachComponent(const AActor* InParentActor, const FMovieSceneActorReferenceKey& Key) const
{
	FName AttachComponentName = Key.ComponentName;
	FName AttachSocketName = Key.SocketName;

	if (AttachSocketName != NAME_None)
	{
		if (AttachComponentName != NAME_None)
		{
			TInlineComponentArray<USceneComponent*> PotentialAttachComponents(InParentActor);
			for (USceneComponent* PotentialAttachComponent : PotentialAttachComponents)
			{
				if (PotentialAttachComponent->GetFName() == AttachComponentName && PotentialAttachComponent->DoesSocketExist(AttachSocketName))
				{
					return PotentialAttachComponent;
				}
			}
		}
		else if (InParentActor->GetRootComponent()->DoesSocketExist(AttachSocketName))
		{
			return InParentActor->GetRootComponent();
		}
	}
	else if (AttachComponentName != NAME_None)
	{
		TInlineComponentArray<USceneComponent*> PotentialAttachComponents(InParentActor);
		for (USceneComponent* PotentialAttachComponent : PotentialAttachComponents)
		{
			if (PotentialAttachComponent->GetFName() == AttachComponentName)
			{
				return PotentialAttachComponent;
			}
		}
	}

	if (InParentActor->GetDefaultAttachComponent())
	{
		return InParentActor->GetDefaultAttachComponent();
	}
	else
	{
		return InParentActor->GetRootComponent();
	}
}

