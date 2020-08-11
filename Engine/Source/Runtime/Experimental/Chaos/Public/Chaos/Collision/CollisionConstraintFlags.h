// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/GeometryParticles.h"

namespace Chaos
{

	enum class ECollisionConstraintFlags : uint32
	{
		CCF_None                       = 0x0,
		CCF_BroadPhaseIgnoreCollisions = 0x1,
		CCF_DummyFlag
	};

	class CHAOS_API FIgnoreCollisionManager
	{
	public:

		using FHandleID = FUniqueIdx;

		FIgnoreCollisionManager() {};

		bool ContainsHandle(FHandleID Body0)
		{
			return IgnoreCollisionsList.Contains(Body0);
		}

		bool IgnoresCollision(FHandleID Body0, FHandleID Body1)
		{
			if (IgnoreCollisionsList.Contains(Body0))
			{
				return IgnoreCollisionsList[Body0].Contains(Body1);
			}
			return false;
		}

		int32 NumIgnoredCollision(FHandleID Body0)
		{
			if (IgnoreCollisionsList.Contains(Body0))
			{
				return IgnoreCollisionsList[Body0].Num();
			}
			return 0;
		}

		void AddIgnoreCollisionsFor(FHandleID Body0, FHandleID Body1)
		{
			if (!IgnoreCollisionsList.Contains(Body0))
			{
				IgnoreCollisionsList.Add(Body0, TArray<FHandleID>());
			}
			IgnoreCollisionsList[Body0].Add(Body1);

		}
		void RemoveIgnoreCollisionsFor(FHandleID Body0, FHandleID Body1)
		{
			if (IgnoreCollisionsList.Contains(Body0))
			{
				IgnoreCollisionsList[Body0].Remove(Body1);
			}
		}

	private:
		TMap<FHandleID, TArray<FHandleID> > IgnoreCollisionsList;
	};

} // Chaos
