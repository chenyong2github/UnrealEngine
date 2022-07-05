// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/FrameNumber.h"
#include "Templates/SharedPointer.h"

class UTransformableHandle;
class UTickableTransformConstraint;
class ISequencer;
enum class EMovieSceneTransformChannel : uint32;

struct FConstraintBaker
{
public:
	/** @todo documentation. (subject to changes to handle start / stop as parameters) */
	static void DoIt(UTickableTransformConstraint* InConstraint);

	/** Stores InHandle local (or global) transforms at InFrames. */
	static void GetHandleTransforms(
		UWorld* InWorld,
		const TSharedPtr<ISequencer>& InSequencer,
		const UTransformableHandle* InHandle,
		const TArray<FFrameNumber>& InFrames,
		const bool bLocal,
		TArray<FTransform>& OutTransforms);
	
	/** Returns the channels to key based on the constraint's type. */
	static EMovieSceneTransformChannel GetChannelsToKey(const UTickableTransformConstraint* InConstraint);

	/** Add InTransforms keys at InFrames into the InHandle transform animation channels. */
	static void AddTransformKeys(
		const TSharedPtr<ISequencer>& InSequencer,
		UTransformableHandle* InHandle,
		const TArray<FFrameNumber>& InFrames,
		const TArray<FTransform>& InTransforms,
		const EMovieSceneTransformChannel& InChannels);
	
private:

	/** Evaluates the constraint at each frames and stores the resulting child transforms. */
	static void GetHandleTransforms(
		const TSharedPtr<ISequencer>& InSequencer,
		const UTransformableHandle* InHandle,
		const TArray<UTickableTransformConstraint*>& InConstraintsToEvaluate,
		const TArray<FFrameNumber>& InFrames,
		const bool bLocal,
		TArray<FTransform>& OutTransforms);
};

