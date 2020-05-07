// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	class FCollisionResimCache
	{

	public:
		void SaveConstraints(const FCollisionConstraintsArray& CollisionsArray)
		{
			SavedConstraints = CollisionsArray;

			//Create weak handles so we can make sure everything is alive later
			auto SaveArrayHelper = [](auto& Constraints, auto& WeakPairs)
			{
				WeakPairs.Reserve(Constraints.Num());
				for(FCollisionConstraintBase& Constraint : Constraints)
				{
					WeakPairs.Add(FWeakConstraintPair{Constraint.Particle[0]->WeakParticleHandle(),Constraint.Particle[1]->WeakParticleHandle()});
				}
				check(Constraints.Num() == WeakPairs.Num());
			};

			SaveArrayHelper(SavedConstraints.SinglePointConstraints, WeakSinglePointConstraints);
			SaveArrayHelper(SavedConstraints.SinglePointSweptConstraints, WeakSinglePointSweptConstraints);
			SaveArrayHelper(SavedConstraints.MultiPointConstraints, WeakMultiPointConstraints);
		}

		//Returns all constraints that are still valid (resim can invalidate constraints by either deleting particles, moving particles, etc...)
		const FCollisionConstraintsArray& GetAndSanitizeConstraints()
		{
			auto CleanupArrayHelper = [](auto& Constraints, auto& WeakPairs)
			{
				check(Constraints.Num() == WeakPairs.Num());
				for(int32 Idx = Constraints.Num() - 1; Idx >= 0; --Idx)
				{
					FCollisionConstraintBase& Constraint = Constraints[Idx];
					FWeakConstraintPair& WeakPair = WeakPairs[Idx];

					TGeometryParticleHandle<FReal,3>* A = WeakPair.A.GetHandleUnsafe();
					TGeometryParticleHandle<FReal,3>* B = WeakPair.B.GetHandleUnsafe();

					bool bValidConstraint = A != nullptr && B != nullptr;
					if(bValidConstraint)
					{
						//Should we desync constrained particle here? Leaving as is for now but might be cheapest place to do it
						if(A->SyncState() == ESyncState::HardDesync || B->SyncState() == ESyncState::HardDesync )
						{
							bValidConstraint = false;
						}
					}

					if(!bValidConstraint)
					{
						Constraints.RemoveAtSwap(Idx);
						WeakPairs.RemoveAtSwap(Idx);
					}
				}
			};

			CleanupArrayHelper(SavedConstraints.SinglePointConstraints, WeakSinglePointConstraints);
			CleanupArrayHelper(SavedConstraints.SinglePointSweptConstraints, WeakSinglePointSweptConstraints);
			CleanupArrayHelper(SavedConstraints.MultiPointConstraints, WeakMultiPointConstraints);

			return SavedConstraints;
		}

		void Reset()
		{
			SavedConstraints.Reset();
			WeakSinglePointConstraints.Reset();
			WeakSinglePointSweptConstraints.Reset();
			WeakMultiPointConstraints.Reset();
		}

	private:
		
		//NOTE: You must sanitize this before using. This can contain dangling pointers or invalid constraints
		FCollisionConstraintsArray SavedConstraints;

		struct FWeakConstraintPair
		{
			FWeakParticleHandle A;
			FWeakParticleHandle B;
		};

		//TODO: better way to handle this?
		TArray<FWeakConstraintPair> WeakSinglePointConstraints;
		TArray<FWeakConstraintPair> WeakSinglePointSweptConstraints;
		TArray<FWeakConstraintPair> WeakMultiPointConstraints;
	};
}