// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectKey.h"
#include "Evaluation/IMovieSceneMotionVectorSimulation.h"
#include "Math/Transform.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"

class USceneComponent;


struct FMovieSceneMotionVectorSimulation : IMovieSceneMotionVectorSimulation
{
	virtual void PreserveSimulatedMotion(bool bShouldPreserveTransforms) override;

	virtual void Add(USceneComponent* Component, const FTransform& SimulatedTransform, FName SocketName) override;

	virtual void Apply(IMovieScenePlayer& Player) override;

private:

	FTransform GetTransform(USceneComponent* Component);

	FTransform GetSocketTransform(USceneComponent* Component, FName SocketName);

	bool HavePreviousTransformForParent(USceneComponent* InComponent) const;

	void ApplySimulatedTransforms(USceneComponent* InComponent, const FTransform& InPreviousTransform);

private:

	/** Transform data relating to a specific object or socket */
	struct FSimulatedTransform
	{
		FSimulatedTransform(const FTransform& InTransform, FName InSocketName = NAME_None)
			: Transform(InTransform), SocketName(InSocketName)
		{}

		/** The transform for the object */
		FTransform Transform;

		/** The socket name to which this relates */
		FName SocketName;
	};

	/** Map of object -> previous transform data */
	TMultiMap<FObjectKey, FSimulatedTransform> TransformData;

	/** Whether to reset TransformData at the end of the frame or not */
	bool bPreserveTransforms = false;
};