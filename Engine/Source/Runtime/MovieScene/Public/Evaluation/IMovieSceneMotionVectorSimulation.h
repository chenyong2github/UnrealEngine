// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Math/Transform.h"
#include "UObject/NameTypes.h"

struct FPersistentEvaluationData;
struct FMovieSceneContext;
struct FFrameTime;

class USceneComponent;
class IMovieScenePlayer;


/**
 * Crude helper class for applying simulated transforms for all animated objects of a sequence on camera cut frames from sequencer
 * Simulated transforms are passed onto the renderer at the end of a sequence's evaluation and will be used for motion vector computation
 */
struct MOVIESCENE_API IMovieSceneMotionVectorSimulation
{
	virtual ~IMovieSceneMotionVectorSimulation() {}

	/**
	 * Add a new simulated transform for the specified component (and optionally a socket on that component). Persists only while PreserveSimulatedMotion is enabled.
	 *
	 * @param Component                   The component that should have the simulated transform associated with it
	 * @param SimulatedTransform          The simulated previous position, rotation and scale of the component last frame
	 * @param SocketName                  (Optional) The name of a socket on 'Component' which affects the simulated transform of all components attached to the socket.
	 */
	virtual void Add(USceneComponent* Component, const FTransform& SimulatedTransform, FName SocketName) = 0;


	/**
	 * Apply all the simulated transforms from this frame using the specified player, passing them onto the renderer for this frame
	 *
	 * @param Player                      The movie scene player currently playing back the sequence
	 */
	virtual void Apply(IMovieScenePlayer& Player) = 0;


	/**
	 * Indicate that all the currently stored simulated transforms should be preserved or reset once evaluated this frame
	 *
	 * @param bShouldPreserveTransforms   When true, simulated transforms will be preserved indefinitely (useful if a sequence is paused); when false, transforms will all be reset once evaluated this frame.
	 */
	virtual void PreserveSimulatedMotion(bool bShouldPreserveTransforms) = 0;

public:

	/**
	 * Check whether we should simulate motion vectors for the current evaluation.
	 * @return true if we should simulate motion vectors, false otherwise.
	 */
	static bool IsEnabled(const FPersistentEvaluationData& PersistentData, const FMovieSceneContext& Context);

	/**
	 * Enable simulated motion vectors for the current frame
	 */
	static void EnableThisFrame(FPersistentEvaluationData& PersistentData);

	/**
	 * Computes a time at which to simulate motion vectors for the current frame.
	 * @return A time one-frame in the future. Information for the previous frame should be extrapolated backwards from here to ensure correct simulations where no previous data exists (such as animations that start on frame 0).
	 */
	static FFrameTime GetSimulationTime(const FMovieSceneContext& Context);
};