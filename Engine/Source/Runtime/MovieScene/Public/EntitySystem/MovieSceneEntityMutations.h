// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "Delegates/IntegerSequence.h"

namespace UE
{
namespace MovieScene
{

struct IMovieSceneEntityMutation
{
	virtual ~IMovieSceneEntityMutation() {}
	virtual void CreateMutation(FEntityManager* EntityManager, FComponentMask* InOutEntityComponentTypes) const = 0;
	virtual void InitializeAllocation(FEntityAllocation* Allocation, const FComponentMask& AllocationType) const {}
};

struct FAddSingleMutation : IMovieSceneEntityMutation
{
	FComponentTypeID ComponentToAdd;
	FAddSingleMutation(FComponentTypeID InType)
		: ComponentToAdd(InType)
	{}

	virtual void CreateMutation(FEntityManager* EntityManager, FComponentMask* InOutEntityComponentTypes) const override;
};

struct FRemoveSingleMutation : IMovieSceneEntityMutation
{
	FComponentTypeID ComponentToRemove;

	FRemoveSingleMutation(FComponentTypeID InType)
		: ComponentToRemove(InType)
	{}

	virtual void CreateMutation(FEntityManager* EntityManager, FComponentMask* InOutEntityComponentTypes) const override;
};

struct FAddMultipleMutation : IMovieSceneEntityMutation
{
	FComponentMask MaskToAdd;

	virtual void CreateMutation(FEntityManager* EntityManager, FComponentMask* InOutEntityComponentTypes) const override;
};

struct FRemoveMultipleMutation : IMovieSceneEntityMutation
{

	void RemoveComponent(FComponentTypeID InComponentType);

	virtual void CreateMutation(FEntityManager* EntityManager, FComponentMask* InOutEntityComponentTypes) const override;

private:

	FComponentMask MaskToRemove;
};

} // namespace MovieScene
} // namespace UE