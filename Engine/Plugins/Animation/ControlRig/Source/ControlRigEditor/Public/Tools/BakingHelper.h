// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/FrameNumber.h"
#include "Templates/SharedPointer.h"

class UMovieScene3DTransformSection;
class AActor;
class ISequencer;
struct FGuid;
class UMovieScene;
class UControlRig;
struct FFrameRate;
enum class EMovieSceneTransformChannel : uint32;

/**
 * FBakingHelper
 */

struct FBakingHelper
{
	/** Returns the current sequencer. */
	static TWeakPtr<ISequencer> GetSequencer();
	
	/** Returns the frame numbers between start and end. */
	static void CalculateFramesBetween(
		const UMovieScene* MovieScene,
		FFrameNumber StartFrame,
		FFrameNumber EndFrame,
		TArray<FFrameNumber>& OutFrames);
	
	/** Returns the transform section for that guid. */
	static UMovieScene3DTransformSection* GetTransformSection(
		const ISequencer* InSequencer,
		const FGuid& InGuid,
		const FTransform& InDefaultTransform = FTransform::Identity);

	/** Adds transform keys to the section based on the channels filters. */
	static bool AddTransformKeys(
		const UMovieScene3DTransformSection* InTransformSection,
		const TArray<FFrameNumber>& Frames,
		const TArray<FTransform>& InLocalTransforms,
		const EMovieSceneTransformChannel& InChannels);

	/** Adds transform keys to control based on the channels filters. */
	static bool AddTransformKeys(
		UControlRig* InControlRig,
		const FName& InControlName,
		const TArray<FFrameNumber>& InFrames,
		const TArray<FTransform>& InLocalTransforms,
		const EMovieSceneTransformChannel& InChannels,
		const FFrameRate& InTickResolution);
};
