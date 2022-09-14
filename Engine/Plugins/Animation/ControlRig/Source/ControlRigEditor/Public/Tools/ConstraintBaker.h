// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/FrameNumber.h"
#include "Templates/SharedPointer.h"

class UTransformableHandle;
class UTickableTransformConstraint;
class ISequencer;
class UWorld;
struct FFrameNumber;
enum class EMovieSceneTransformChannel : uint32;

struct FConstraintBaker
{
public:
	/** Bake constraint over specified frames, frames must be in order*/
	static void Bake(UWorld* InWorld, UTickableTransformConstraint* InConstraint, const TSharedPtr<ISequencer>& InSequencer, const TArray<FFrameNumber>& InFrames);

	
	/** Add InTransforms keys at InFrames into the InHandle transform animation channels. */
	static void AddTransformKeys(
		const TSharedPtr<ISequencer>& InSequencer,
		UTransformableHandle* InHandle,
		const TArray<FFrameNumber>& InFrames,
		const TArray<FTransform>& InTransforms,
		const EMovieSceneTransformChannel& InChannels);
	
private:

};

