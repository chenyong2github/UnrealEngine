// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

struct FChannelMapInfo;
struct FConstraintAndActiveChannel;
class UControlRig;
class ISequencer;
class UMovieScene3DTransformSection;
class UMovieSceneControlRigParameterSection;
class UMovieSceneSection;
class UTransformableHandle;
class UTickableTransformConstraint;
class UTransformableControlHandle;
class UTransformableComponentHandle;
struct FMovieSceneFloatChannel;
struct FMovieSceneDoubleChannel;
struct FMovieSceneConstraintChannel;
struct FFrameNumber;

/**
 * 
 */

struct FConstraintChannelHelper
{
public:
	/** @todo documentation. */
	static bool IsKeyframingAvailable();
	
	/** Adds an active key if needed and does the compensation when switching. */
	static void SmartConstraintKey(UTickableTransformConstraint* InConstraint);


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

	
	static bool bDoNotCompensate;
};
