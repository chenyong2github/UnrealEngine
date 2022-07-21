// Copyright Epic Games, Inc. All Rights Reserved.


#include "Tools/BakingHelper.h"

#include "ControlRig.h"
#include "ILevelSequenceEditorToolkit.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieSceneSequence.h"
#include "MovieSceneToolHelpers.h"
#include "Channels/MovieSceneChannelProxy.h"

TWeakPtr<ISequencer> FBakingHelper::GetSequencer()
{
	// if getting sequencer from level sequence need to use the current(leader), not the focused
	if (ULevelSequence* LevelSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence())
	{
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			static constexpr bool bFocusIfOpen = false;
			IAssetEditorInstance* AssetEditor = AssetEditorSubsystem->FindEditorForAsset(LevelSequence, bFocusIfOpen);
			const ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
			return LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;
		}
	}
	return nullptr;
}


void FBakingHelper::CalculateFramesBetween(
	const UMovieScene* MovieScene,
	FFrameNumber StartFrame,
	FFrameNumber EndFrame,
	TArray<FFrameNumber>& OutFrames)
{
	if(StartFrame > EndFrame)
	{
		Swap(StartFrame, EndFrame);
	}
	
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const FFrameRate DisplayResolution = MovieScene->GetDisplayRate();

	const FFrameNumber StartTimeInDisplay = FFrameRate::TransformTime(FFrameTime(StartFrame), TickResolution, DisplayResolution).FloorToFrame();
	const FFrameNumber EndTimeInDisplay = FFrameRate::TransformTime(FFrameTime(EndFrame), TickResolution, DisplayResolution).CeilToFrame();

	OutFrames.Reserve(EndTimeInDisplay.Value - StartTimeInDisplay.Value + 1);
	for (FFrameNumber DisplayFrameNumber = StartTimeInDisplay; DisplayFrameNumber <= EndTimeInDisplay; ++DisplayFrameNumber)
	{
		FFrameNumber TickFrameNumber = FFrameRate::TransformTime(FFrameTime(DisplayFrameNumber), DisplayResolution, TickResolution).FrameNumber;
		OutFrames.Add(TickFrameNumber);
	}
}

UMovieScene3DTransformSection* FBakingHelper::GetTransformSection(
	const ISequencer* InSequencer,
	const FGuid& InGuid,
	const FTransform& InDefaultTransform)
{
	if (!InSequencer || !InSequencer->GetFocusedMovieSceneSequence())
	{
		return nullptr;
	}
	
	UMovieScene* MovieScene = InSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!MovieScene)
	{
		return nullptr;
	}

	UMovieScene3DTransformTrack* TransformTrack = MovieScene->FindTrack<UMovieScene3DTransformTrack>(InGuid);
	if (!TransformTrack)
	{
		MovieScene->Modify();
		TransformTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(InGuid);
	}
	TransformTrack->Modify();

	static constexpr FFrameNumber Frame0;
	bool bSectionAdded = false;
	UMovieScene3DTransformSection* TransformSection =
				Cast<UMovieScene3DTransformSection>(TransformTrack->FindOrAddSection(Frame0, bSectionAdded));
	if (!TransformSection)
	{
		return nullptr;
	}

	TransformSection->Modify();
	if (bSectionAdded)
	{
		TransformSection->SetRange(TRange<FFrameNumber>::All());

		const FVector Location0 = InDefaultTransform.GetLocation();
		const FRotator Rotation0 = InDefaultTransform.GetRotation().Rotator();
		const FVector Scale3D0 = InDefaultTransform.GetScale3D();

		const TArrayView<FMovieSceneDoubleChannel*> Channels =
			TransformSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		Channels[0]->SetDefault(Location0.X);
		Channels[1]->SetDefault(Location0.Y);
		Channels[2]->SetDefault(Location0.Z);
		Channels[3]->SetDefault(Rotation0.Roll);
		Channels[4]->SetDefault(Rotation0.Pitch);
		Channels[5]->SetDefault(Rotation0.Yaw);
		Channels[6]->SetDefault(Scale3D0.X);
		Channels[7]->SetDefault(Scale3D0.Y);
		Channels[8]->SetDefault(Scale3D0.Z);
	}
	
	return TransformSection;
}

bool FBakingHelper::AddTransformKeys(
	const UMovieScene3DTransformSection* InTransformSection,
	const TArray<FFrameNumber>& Frames,
	const TArray<FTransform>& InLocalTransforms,
	const EMovieSceneTransformChannel& InChannels)
{
	if (!InTransformSection)
	{
		return false;	
	}

	if (Frames.IsEmpty() || Frames.Num() != InLocalTransforms.Num())
	{
		return false;
	}

	auto GetValue = [](const uint32 Index,const FVector& InLocation, const FRotator& InRotation, const FVector& InScale)
	{
		switch (Index)
		{
		case 0:
			return InLocation.X;
		case 1:
			return InLocation.Y;
		case 2:
			return InLocation.Z;
		case 3:
			return InRotation.Roll;
		case 4:
			return InRotation.Pitch;
		case 5:
			return InRotation.Yaw;
		case 6:
			return InScale.X;
		case 7:
			return InScale.Y;
		case 8:
			return InScale.Z;
		default:
			ensure(false);
			break;
		}
		return 0.0;
	};

	const bool bKeyTranslation = EnumHasAllFlags(InChannels, EMovieSceneTransformChannel::Translation);
	const bool bKeyRotation = EnumHasAllFlags(InChannels, EMovieSceneTransformChannel::Rotation);
	const bool bKeyScale = EnumHasAllFlags(InChannels, EMovieSceneTransformChannel::Scale);

	TArray<uint32> ChannelsIndexToKey;
	if (bKeyTranslation)
	{
		ChannelsIndexToKey.Append({0,1,2});
	}
	if (bKeyRotation)
	{
		ChannelsIndexToKey.Append({3,4,5});
	}
	if (bKeyScale)
	{
		ChannelsIndexToKey.Append({6,7,8});
	}

	const TArrayView<FMovieSceneDoubleChannel*> Channels =
		InTransformSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();

	// set default
	const FTransform& LocalTransform0 = InLocalTransforms[0];
	const FVector Location0 = LocalTransform0.GetLocation();
	const FRotator Rotation0 = LocalTransform0.GetRotation().Rotator();
	const FVector Scale3D0 = LocalTransform0.GetScale3D();
	
	for (int32 ChannelIndex = 0; ChannelIndex < 9; ChannelIndex++)
	{
		if (!Channels[ChannelIndex]->GetDefault().IsSet())
		{
			const double Value = GetValue(ChannelIndex, Location0, Rotation0, Scale3D0);
			Channels[ChannelIndex]->SetDefault(Value);
		}
	}

	// add keys
	for (int32 Index = 0; Index < Frames.Num(); ++Index)
	{
		const FFrameNumber& Frame = Frames[Index];
		const FTransform& LocalTransform = InLocalTransforms[Index];
		
		const FVector Location = LocalTransform.GetLocation();
		const FRotator Rotation = LocalTransform.GetRotation().Rotator();
		const FVector Scale3D = LocalTransform.GetScale3D();

		for (const int32 ChannelIndex: ChannelsIndexToKey)
		{
			const double Value = GetValue(ChannelIndex, Location, Rotation, Scale3D);
			TMovieSceneChannelData<FMovieSceneDoubleValue> ChannelData = Channels[ChannelIndex]->GetData();
			MovieSceneToolHelpers::SetOrAddKey(ChannelData, Frame, Value);
		}
	}

	//now we need to set auto tangents
	for (const int32 ChannelIndex: ChannelsIndexToKey)
	{
		Channels[ChannelIndex]->AutoSetTangents();
	}

	return true;
}

bool FBakingHelper::AddTransformKeys(
	UControlRig* InControlRig, const FName& InControlName,
	const TArray<FFrameNumber>& InFrames,
	const TArray<FTransform>& InTransforms,
	const EMovieSceneTransformChannel& InChannels,
	const FFrameRate& InTickResolution,
	const bool bLocal)
{
	if (InFrames.IsEmpty() || InFrames.Num() != InTransforms.Num())
	{
		return false;
	}

	auto KeyframeFunc = [InControlRig, InControlName, bLocal](const FTransform& InTransform, const FRigControlModifiedContext& InKeyframeContext)
	{
		static constexpr bool bNotify = true;
		static constexpr bool bUndo = false;
		static constexpr bool bFixEuler = true;

		if (bLocal)
		{
			return InControlRig->SetControlLocalTransform(InControlName, InTransform, bNotify, InKeyframeContext, bUndo, bFixEuler);
		}
		InControlRig->SetControlGlobalTransform(InControlName, InTransform, bNotify, InKeyframeContext, bUndo, bFixEuler);
	};

	FRigControlModifiedContext KeyframeContext;
	KeyframeContext.SetKey = EControlRigSetKey::Always;
	KeyframeContext.KeyMask = static_cast<uint32>(InChannels);
	
	for (int32 Index = 0; Index < InFrames.Num(); ++Index)
	{
		const FFrameNumber& Frame = InFrames[Index];
		KeyframeContext.LocalTime = InTickResolution.AsSeconds(FFrameTime(Frame));
		
		KeyframeFunc(InTransforms[Index], KeyframeContext);
	}

	return true;
}