// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "Templates/SubclassOf.h"

#include "MovieSceneBlenderSystem.generated.h"

/**
 * Base class for all systems that blend data from multiple entities/components into a single entity
 *
 * This system has direct coupling to TCompositePropertySystemManager and forms the basis for built-in
 * blend modes in Sequencer (Absolute, Relative and Additive). Blend 'channels' are allocated in this
 * system which define a many to one relationship by corresponding blend input(many)/output(one) components
 * added to the relevant entities. Blend input and output channel components are uint16.
 *
 * The class supports a maximum of 65535 blend channels.
 *
 * Additionally, blender systems tag their inputs using the built in Absolute, Relative and Additive tags
 * for efficient computation of each type of blending (this allows each blend type to be computed without branching).
 *
 * A simple example of 3 blended floats is as follows:
 *   Entity Data:
 *       Inputs:
 *         float [float component], uint16 [blend channel input], [Absolute Tag] => [ { 100.f, 0 }, { 200.f, 1 } ]
 *         float [float component], uint16 [blend channel input], [Additive Tag] => [ { 50.f, 1} ]
 *       Outputs:
 *         float [float component], uint16 [blend channel output]                => [ { 0.f, 0}, { 0.f, 1} ]
 * To perform blending for this data, accumulation buffers are allocated per-blend-type, and each blend accumulates
 * into the index of its blend channel input component. A final combination pass walks over blend channel outputs
 * a writes the results into the result component from the accumulation buffer.
 */
UCLASS(Abstract)
class MOVIESCENE_API UMovieSceneBlenderSystem : public UMovieSceneEntitySystem
{
public:

	GENERATED_BODY()

	/**
	 * Allocate a new blend channel.
	 * @note Must be released when it is no longer needed in order to prevent leaking channels.
	 */
	uint16 AllocateBlendChannel();


	/**
	 * Release a previously allocated blend channel.
	 */
	void ReleaseBlendChannel(uint16 BlendChannelID);


protected:

	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;

	/** Bit array specifying currently allocated blend channels */
	TBitArray<> AllocatedBlendChannels;
};
