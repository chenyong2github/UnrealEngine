// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ConstraintHandle.h"
#include "Chaos/PBDCollisionTypes.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Framework/BufferedData.h"

#include <memory>
#include <queue>
#include <sstream>
#include "BoundingVolume.h"
#include "AABBTree.h"

namespace Chaos
{
	template<typename T, int d>
	class TPBDCollisionConstraintHandle;


template<class T, int d>
class CHAOS_API TCollisionResolutionManifold
{
	typedef TPair<const FImplicitObject*, const FImplicitObject*> ImplicitPairs;

public:
	using FConstraintContainerHandle = TPBDCollisionConstraintHandle<T, d>;
	TCollisionResolutionManifold(const TVector<T, d>& InLocation, const TRotation<T, d>& InRotation, int32 InTimestamp = -INT_MAX)
		: Timestamp(InTimestamp)
		, Location(InLocation)
		, Rotation(InRotation)
	{}

	const TVector<T, d>& GetLocation() { return Location; }
	const TRotation<T, d>& GetRotation() { return Rotation; }

	void AddHandle(FConstraintContainerHandle* InHandle)
	{
		Implicits.Add(ImplicitPairs(InHandle->GetContact().Geometry[0], InHandle->GetContact().Geometry[1]));
		Handles.Push(InHandle);
	}
	void RemoveHandle(FConstraintContainerHandle* InHandle)
	{
		Handles.RemoveSingleSwap(InHandle);
		Implicits.Remove(ImplicitPairs(InHandle->GetContact().Geometry[0], InHandle->GetContact().Geometry[1]));
	}

	TArray<FConstraintContainerHandle*>& GetHandles() { return Handles; }
	const TArray<FConstraintContainerHandle*>& GetHandles() const { return Handles; }

	bool ContainsShapeConnection(const FImplicitObject* Implicit0In, const FImplicitObject* Implicit1In) { return Implicits.Contains(ImplicitPairs(Implicit0In, Implicit1In)); }

	int32 GetTimestamp() const { return Timestamp; }
	void SetTimestamp(int32 InTimestamp) { Timestamp = InTimestamp; }

private:
	int32 Timestamp;
	TVector<T, d> Location;
	TRotation<T, d> Rotation;
	TArray<FConstraintContainerHandle*> Handles;
	TSet<ImplicitPairs> Implicits;
};

}
