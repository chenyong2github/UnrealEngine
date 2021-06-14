// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/FrameNumber.h"
#include "Curves/KeyHandle.h"

struct FMovieSceneFloatChannel;
class UControlRig;
class ISequencer;
struct FRigControl;
class UMovieSceneSection;


//first and second key to blend between for each channel
struct FChannelKeyBounds
{
	FChannelKeyBounds() : bValid(false), Channel(nullptr), FirstIndex(INDEX_NONE), SecondIndex(INDEX_NONE), FirstFrame(0), SecondFrame(0), FirstValue(0.0f), SecondValue(0.0f) {}
	bool bValid;
	FMovieSceneFloatChannel* Channel;
	int32 FirstIndex;
	int32 SecondIndex;
	FFrameNumber FirstFrame;
	FFrameNumber SecondFrame;
	float FirstValue;
	float SecondValue;
};

struct FControlRigChannels
{
	FControlRigChannels() : Section(nullptr), NumChannels(0) {};
	FChannelKeyBounds  KeyBounds[9];
	UMovieSceneSection* Section;
	int NumChannels; //may not use all 9 channels.
};

struct FControlsToTween
{
	/**
	 Setup up controls to tween
	* @param InControlRig Control Rig to use
	* @param InSequencer Sequencer to get keys from
	*/
	void Setup(const TArray<UControlRig*>& SelectedControlRigs, TWeakPtr<ISequencer>& InSequencer);

	/**
	* @param InSequencer Sequencer to blend at current time
	* @param InSequencer BlendValue where in time to Blend in at Current Frame, 0.0 will be at current frame, -1.0 will value at previous key, 1.0 value at next key,
	* other values interpolated between.
	*/
	void Blend(TWeakPtr<ISequencer>& InSequencer,  float BlendValue);

private:
	void SetupControlRigChannel(FFrameNumber CurrentFrame, TArray<FFrameNumber>& KeyTimes, TArray<FKeyHandle>& Handles, FMovieSceneFloatChannel* FloatChannel,
		FChannelKeyBounds& KeyBounds);

	TMap<FName, FControlRigChannels>  ControlRigChannelsMap;
};






