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

	/**
	 * A pair that can be used as a key, where the key value is independent of the pair order,
	 * i.e., TSymmetricPairKey(A, B) == TSymmetricPairKey(B, A)
	 */
	template<typename T>
	class TSymmetricPairKey
	{
	public:
		using Item = T;
		using FPair = TPair<Item, Item>;

		static TSymmetricPairKey Make(const T& Item0, const T& Item1)
		{
			T ItemMin = FMath::Min(Item0, Item1);
			T ItemMax = FMath::Max(Item0, Item1);
			return TSymmetricPairKey<T>(ItemMin, ItemMax);
		}

		friend inline uint32 GetTypeHash(const TSymmetricPairKey<T>& Item)
		{
			return GetTypeHash(Item.Pair);
		}

		friend inline bool operator==(const TSymmetricPairKey<T>& L, const TSymmetricPairKey<T>& R)
		{
			return L.Pair == R.Pair;
		}

	private:
		TSymmetricPairKey(const T& Item0, const T& Item1)
			: Pair(Item0, Item1)
		{
			check(Item0 < Item1);
		}

		FPair Pair;
	};


	template<class T, int d>
	class CHAOS_API TCollisionResolutionManifold
	{
		using FImplicitPairKey = TSymmetricPairKey<const FImplicitObject*>;

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
			Implicits.Add(FImplicitPairKey::Make(InHandle->GetContact().Geometry[0], InHandle->GetContact().Geometry[1]));
			ConstraintHandles.Push(InHandle);
		}
		void RemoveHandle(FConstraintContainerHandle* InHandle)
		{
			ConstraintHandles.RemoveSingleSwap(InHandle);
			Implicits.Remove(FImplicitPairKey::Make(InHandle->GetContact().Geometry[0], InHandle->GetContact().Geometry[1]));
		}

		TArray<FConstraintContainerHandle*>& GetHandles() { return ConstraintHandles; }
		const TArray<FConstraintContainerHandle*>& GetHandles() const { return ConstraintHandles; }

		bool ContainsShapeConnection(const FImplicitObject* Implicit0In, const FImplicitObject* Implicit1In) { return Implicits.Contains(FImplicitPairKey::Make(Implicit0In, Implicit1In)); }

		int32 GetTimestamp() const { return Timestamp; }
		void SetTimestamp(int32 InTimestamp) { Timestamp = InTimestamp; }

	private:
		int32 Timestamp;
		TVector<T, d> Location;
		TRotation<T, d> Rotation;
		TArray<FConstraintContainerHandle*> ConstraintHandles;
		TSet< FImplicitPairKey > Implicits;
	};

}
