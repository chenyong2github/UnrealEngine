// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IntegerSequence.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"

namespace UE
{
namespace MovieScene
{
class FEntityManager;
struct FEntityAllocation;

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

	MOVIESCENE_API virtual void CreateMutation(FEntityManager* EntityManager, FComponentMask* InOutEntityComponentTypes) const override;
};

struct FRemoveSingleMutation : IMovieSceneEntityMutation
{
	FComponentTypeID ComponentToRemove;

	FRemoveSingleMutation(FComponentTypeID InType)
		: ComponentToRemove(InType)
	{}

	MOVIESCENE_API virtual void CreateMutation(FEntityManager* EntityManager, FComponentMask* InOutEntityComponentTypes) const override;
};

struct FAddMultipleMutation : IMovieSceneEntityMutation
{
	FComponentMask MaskToAdd;

	MOVIESCENE_API virtual void CreateMutation(FEntityManager* EntityManager, FComponentMask* InOutEntityComponentTypes) const override;
};

struct FRemoveMultipleMutation : IMovieSceneEntityMutation
{

	void RemoveComponent(FComponentTypeID InComponentType);

	MOVIESCENE_API virtual void CreateMutation(FEntityManager* EntityManager, FComponentMask* InOutEntityComponentTypes) const override;

private:

	FComponentMask MaskToRemove;
};

} // namespace MovieScene
} // namespace UE