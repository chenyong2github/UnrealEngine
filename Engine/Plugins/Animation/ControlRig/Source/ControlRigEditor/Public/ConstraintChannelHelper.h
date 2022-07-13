// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class UControlRig;
class ISequencer;
class UMovieScene3DTransformSection;
class UTransformableHandle;
class UTickableTransformConstraint;
class UTransformableControlHandle;
class UTransformableComponentHandle;
struct FMovieSceneFloatChannel;
struct FMovieSceneDoubleChannel;
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
	
	/** Adds an active key if needed and does the compensation when switching. */
	static void SmartConstraintKey(UTickableTransformConstraint* InConstraint);

	/** Adds a transform key on InHandle at InTime. */
	static void AddChildTransformKey(
		const UTransformableHandle* InHandle,
		const FFrameNumber& InTime,
		const TSharedPtr<ISequencer>& InSequencer);

	/** Compensate transform on handles when a constraint switches state. */
	static void Compensate(
		UTickableTransformConstraint* InConstraint,
		const bool bAllTimes = false);
	static void CompensateIfNeeded(
		const UControlRig* ControlRig,
		const TSharedPtr<ISequencer>& InSequencer,
		const UMovieSceneControlRigParameterSection* Section,
		const TOptional<FFrameNumber>& OptionalTime);
	
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
	static TArrayView<FMovieSceneFloatChannel*> GetTransformFloatChannels(
		const UTransformableHandle* InHandle,
		const TSharedPtr<ISequencer>& InSequencer);

	/** @todo documentation.  TransformComponents use doubles */
	static TArrayView<FMovieSceneDoubleChannel*> GetTransformDoubleChannels(
		const UTransformableHandle* InHandle,
		const TSharedPtr<ISequencer>& InSequencer);

	static bool bDoNotCompensate;
};
