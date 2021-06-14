// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/ControlRigTweener.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "ControlRig.h"
#include "ISequencer.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "Rigs/RigControlHierarchy.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "IKeyArea.h"
#include "Channels/MovieSceneChannelProxy.h"

//category not used so far, todo
void FControlsToTween::Setup(const TArray<UControlRig*>& SelectedControlRigs, TWeakPtr<ISequencer>& InSequencer)
{
	if (InSequencer.IsValid() == false)
	{
		return;
	}
	ISequencer* Sequencer = InSequencer.Pin().Get();
	ControlRigChannelsMap.Reset();

	FQualifiedFrameTime CurrentTime = Sequencer->GetLocalTime();

	UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();

	const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
	for (const FMovieSceneBinding& Binding : Bindings)
	{
		UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None));
		if (Track && Track->GetControlRig() && SelectedControlRigs.Contains(Track->GetControlRig()))
		{
			for (UMovieSceneSection* MovieSection : Track->GetAllSections())
			{
				UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(MovieSection);
				if (Section && Section->IsActive() && Section->GetRange().Contains(CurrentTime.Time.GetFrame()))
				{
					Section->Modify();
					UControlRig* ControlRig = Track->GetControlRig();
					TArray<FRigControlElement*> CurrentControls;
					ControlRig->GetControlsInOrder(CurrentControls);
					URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
					FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();

					const FName FloatChannelTypeName = FMovieSceneFloatChannel::StaticStruct()->GetFName();

					FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
					TArrayView<FMovieSceneFloatChannel*> Channels = ChannelProxy.GetChannels<FMovieSceneFloatChannel>();
					//reuse these arrays
					TArray<FFrameNumber> KeyTimes;
					TArray<FKeyHandle> Handles;
					for (FRigControlElement* ControlElement : CurrentControls)
					{
						if (ControlElement->Settings.bAnimatable &&  ControlRig->IsControlSelected(ControlElement->GetName()))
						{
							FControlRigChannels ControlRigChannels;
							ControlRigChannels.NumChannels = 0;
							ControlRigChannels.Section = Section;
							KeyTimes.SetNum(0);
							Handles.SetNum(0);
							FChannelMapInfo* pChannelIndex = Section->ControlChannelMap.Find(ControlElement->GetName());
							int NumChannels = 0;
							switch (ControlElement->Settings.ControlType)
							{
								case ERigControlType::Float:
								{
									NumChannels = 1;
								}
								case ERigControlType::Vector2D:
								{
									NumChannels = 2;
									break;
								}
								case ERigControlType::Position:
								case ERigControlType::Scale:
								case ERigControlType::Rotator:
								{
									NumChannels = 3;
									break;
								}
								case ERigControlType::Transform:
								case ERigControlType::TransformNoScale:
								case ERigControlType::EulerTransform:
								{
									NumChannels = 9;
									break;
								}
							}
							int32 BoundIndex = 0;
							for (int ChannelIdx = pChannelIndex->ChannelIndex; ChannelIdx < (pChannelIndex->ChannelIndex + NumChannels); ++ChannelIdx)
							{
								FMovieSceneFloatChannel* Channel = Channels[ChannelIdx];
								SetupControlRigChannel(CurrentTime.Time.GetFrame(), KeyTimes, Handles, Channel,
									ControlRigChannels.KeyBounds[BoundIndex]);
								if (ControlRigChannels.KeyBounds[BoundIndex].bValid)
								{
									++ControlRigChannels.NumChannels;
								}
								++BoundIndex;
							}
							if (ControlRigChannels.NumChannels > 0)
							{
								ControlRigChannelsMap.Add(ControlElement->GetName(), ControlRigChannels);
							}
						}
					}
				}
			}
		}
	}
}

void FControlsToTween::Blend(TWeakPtr<ISequencer>& InSequencer, float BlendValue)
{
	if (InSequencer.IsValid() == false)
	{
		return;
	}
	ISequencer* Sequencer = InSequencer.Pin().Get();
	FFrameTime  FrameTime = Sequencer->GetLocalTime().Time;
	float FrameAsFloat = (float)FrameTime.GetFrame().Value; 
	Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->Modify();
	//BlendValue of 0.0 = half, 1.0f = Second, -1.0f = FirtValue;
	//NormalizedBlendValue goes from 0 to 1.0f
	for (const TPair<FName, FControlRigChannels>& Pair : ControlRigChannelsMap)
	{
		for (int Index = 0; Index < 9; ++Index)
		{
			float NormalizedBlendValue = (BlendValue + 1.0f) * 0.5f;

			if (Pair.Value.KeyBounds[Index].bValid)
			{
				Pair.Value.Section->Modify();
				if (Pair.Value.KeyBounds[Index].FirstFrame.Value != Pair.Value.KeyBounds[Index].SecondFrame.Value)
				{
					float FirstTime = (float)Pair.Value.KeyBounds[Index].FirstFrame.Value;
					float SecondTime = (float)Pair.Value.KeyBounds[Index].SecondFrame.Value;
					float TimeLocation = (FrameAsFloat - FirstTime) / (SecondTime - FirstTime);
					//use TimeLocation to modify the NormalizedBlendValue 
					if (NormalizedBlendValue > 0.5f)
					{
						NormalizedBlendValue = ((NormalizedBlendValue - .5f) / .5f) * (1.0f - TimeLocation) + TimeLocation;
					}
					else
					{
						NormalizedBlendValue = (NormalizedBlendValue / 0.5f) * TimeLocation;
					}
				}
				float FirstValue = Pair.Value.KeyBounds[Index].FirstValue;
				float SecondValue = Pair.Value.KeyBounds[Index].SecondValue;
				float Value = FirstValue + (SecondValue - FirstValue) * (NormalizedBlendValue);
				AddKeyToChannel(Pair.Value.KeyBounds[Index].Channel, FrameTime.GetFrame(), Value, Sequencer->GetKeyInterpolation());
			}
		}
	}
}

void FControlsToTween::SetupControlRigChannel(FFrameNumber CurrentFrame, TArray<FFrameNumber>& KeyTimes, TArray<FKeyHandle>& Handles, FMovieSceneFloatChannel* FloatChannel,
	FChannelKeyBounds& KeyBounds)
{
	KeyBounds.Channel = FloatChannel;
	KeyBounds.FirstIndex = INDEX_NONE;
	KeyBounds.SecondIndex = INDEX_NONE;
	KeyTimes.SetNum(0);
	Handles.SetNum(0);
	FloatChannel->GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
	if (KeyTimes.Num() > 0)
	{
		TArrayView<const FMovieSceneFloatValue> Values = FloatChannel->GetValues();

		for (int32 Index = 0; Index < KeyTimes.Num(); Index++)
		{
			FFrameNumber FrameNumber = KeyTimes[Index];
			if (FrameNumber < CurrentFrame)
			{
				KeyBounds.FirstIndex = Index;
				KeyBounds.FirstFrame = FrameNumber;
				KeyBounds.FirstValue = Values[Index].Value;
			}
			else if (FrameNumber > CurrentFrame)
			{
				KeyBounds.SecondIndex = Index;
				KeyBounds.SecondFrame = FrameNumber;
				KeyBounds.SecondValue = Values[Index].Value;
				break;
			}
		}
	}
	KeyBounds.bValid = (KeyBounds.FirstIndex != INDEX_NONE && KeyBounds.SecondIndex != INDEX_NONE
		&& KeyBounds.FirstIndex != KeyBounds.SecondIndex) ? true : false;
}