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
	
private:

	/** Evaluates the constraint at each frames and stores the resulting child transforms. */
	static void GetChildTransforms(
		const TSharedPtr<ISequencer>& InSequencer,
		const UTickableTransformConstraint& InConstraint,
		const TArray<FFrameNumber>& InFrames,
		const bool bLocal,
		TArray<FTransform>& OutTransforms);

	/** Bake the resulting transforms into the child's transform animation channels. */
	static void BakeChild(
		const TSharedPtr<ISequencer>& InSequencer,
		UTransformableHandle* InHandle,
		const TArray<FFrameNumber>& InFrames,
		const TArray<FTransform>& InTransforms,
		const EMovieSceneTransformChannel& InChannels);
};

