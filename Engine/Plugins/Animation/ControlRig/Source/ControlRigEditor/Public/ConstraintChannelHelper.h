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

	/** @todo documentation. */
	static void HandleConstraintRemoved(
		UTickableConstraint* InConstraint,
		const FMovieSceneConstraintChannel* InChannel,
		const TSharedPtr<ISequencer>& InSequencer,
		const UMovieSceneSection* InSection);

	/** @todo documentation. */
	static void HandleConstraintKeyDeleted(
		UTickableTransformConstraint* InConstraint,
		const FMovieSceneConstraintChannel* InConstraintChannel,
		const TSharedPtr<ISequencer>& InSequencer,
		const UMovieSceneSection* InSection,
		const FFrameNumber& InTime);

	/** @todo documentation. */
	static void HandleConstraintKeyMoved(
		const UTickableTransformConstraint* InConstraint,
		const FMovieSceneConstraintChannel* InConstraintChannel,
		const UMovieSceneSection* InSection,
		const FFrameNumber& InCurrentFrame, const FFrameNumber& InNextFrame);
	
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
	static TArrayView<FMovieSceneFloatChannel*> GetTransformFloatChannels(
		const UTransformableHandle* InHandle,
		const UMovieSceneControlRigParameterSection* InSection);

	/** @todo documentation.  TransformComponents use doubles */
	static TArrayView<FMovieSceneDoubleChannel*> GetTransformDoubleChannels(
		const UTransformableHandle* InHandle,
		const TSharedPtr<ISequencer>& InSequencer);

	/** @todo factorize. */
	static TPair<const FChannelMapInfo*, int32> GetInfoAndNumFloatChannels(
		const UControlRig* InControlRig,
		const FName& InControlName,
		const UMovieSceneControlRigParameterSection* InSection);

	/** @todo documentation. */
	template<typename ChannelType>
	static void GetFramesToCompensate(
		const FMovieSceneConstraintChannel& InActiveChannel,
		const bool InActiveValueToBeSet,
		const FFrameNumber& InTime,
		const TArrayView<ChannelType*>& InChannels,
		TArray<FFrameNumber>& OutFramesAfter);

	/** @todo documentation. */
	template< typename ChannelType >
	static void GetFramesAfter(
		const FMovieSceneConstraintChannel& InActiveChannel,
		const FFrameNumber& InTime,
		const TArrayView<ChannelType*>& InChannels,
		TArray<FFrameNumber>& OutFrames);
		
	/** @todo documentation. */
	template< typename ChannelType >
    static void GetFramesWithinActiveState(
	    const FMovieSceneConstraintChannel& InActiveChannel,
	    const TArrayView<ChannelType*>& InChannels,
	    TArray<FFrameNumber>& OutFrames);
	
	/** @todo documentation. */
	template< typename ChannelType >
	static void MoveTransformKeys(
		const TArrayView<ChannelType*>& InChannels,
		const FFrameNumber& InCurrentTime,
		const FFrameNumber& InNextTime);

	/** @todo documentation. */
	template< typename ChannelType >
	static void DeleteTransformKeys(
		const TArrayView<ChannelType*>& InChannels,
		const FFrameNumber& InTime);
	
	static bool bDoNotCompensate;
};
