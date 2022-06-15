// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class ISequencer;
class UMovieScene3DTransformSection;
class UTransformableHandle;
class UTickableTransformConstraint;
class UTransformableControlHandle;
class UTransformableComponentHandle;
struct FMovieSceneFloatChannel;
struct FMovieSceneConstraintChannel;
class UMovieSceneControlRigParameterSection;
struct FFrameNumber;

/**
 * 
 */

struct FConstraintChannelHelper
{
public:
	/** @todo documentation. */
	static void AddConstraintKey(UTickableTransformConstraint* InConstraint);
	static void SmartConstraintKey(UTickableTransformConstraint* InConstraint);

	/** @todo documentation. */
	static void AddChildTransformKey(
		const UTransformableHandle* InHandle,
		const FFrameNumber& InTime,
		const TSharedPtr<ISequencer>& InSequencer);

private:

	/** BEGIN CONTROL RIG SECTION */
	
	/** @todo documentation. */
	static void SmartControlConstraintKey(
		UTickableTransformConstraint* InConstraint,
		const TSharedPtr<ISequencer>& InSequencer);

	/** @todo documentation. */
	static UMovieSceneControlRigParameterSection* GetControlSection(
		const UTransformableControlHandle* InHandle,
		const TSharedPtr<ISequencer>& InSequencer);
	
	/** END CONTROL RIG SECTION */

	/** BEGIN COMPONENT SECTION */

	/** @todo documentation. */
	static void SmartComponentConstraintKey(
		UTickableTransformConstraint* InConstraint,
		const TSharedPtr<ISequencer>& InSequencer);

	/** @todo documentation. */
	static UMovieScene3DTransformSection* GetTransformSection(
		const UTransformableComponentHandle* InHandle,
		const TSharedPtr<ISequencer>& InSequencer);

	/** END COMPONENT SECTION */

	/** @todo documentation. */
	static void GetFramesToCompensate(
		const FMovieSceneConstraintChannel& InActiveChannel,
		const bool InValue,
		const FFrameNumber& InTime,
		const TArrayView<FMovieSceneFloatChannel*>& InChannels,
		TArray<FFrameNumber>& OutFramesAfter);

	/** @todo documentation. */
	static TArrayView<FMovieSceneFloatChannel*> GetTransformFloatChannels(
		const UTransformableHandle* InHandle,
		const TSharedPtr<ISequencer>& InSequencer);
};

/** Key drawing overrides */
// void DrawKeys(
// 	FMovieSceneConstraintChannel* Channel,
// 	TArrayView<const FKeyHandle> InKeyHandles,
// 	const UMovieSceneSection* InOwner,
// 	TArrayView<FKeyDrawParams> OutKeyDrawParams);
//
// void DrawExtra(
// 	FMovieSceneConstraintChannel* Channel,
// 	const UMovieSceneSection* Owner,
// 	const FGeometry& AllottedGeometry,
// 	FSequencerSectionPainter& Painter);
