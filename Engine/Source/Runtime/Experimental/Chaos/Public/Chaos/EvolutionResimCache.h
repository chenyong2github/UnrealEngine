// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ResimCacheBase.h"
#include "Templates/UniquePtr.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	class FCollisionResimCache;

	class FEvolutionResimCache : public IResimCacheBase
	{
	public:
		FEvolutionResimCache(){}
		virtual ~FEvolutionResimCache() = default;
		void ResetCache()
		{
			SavedConstraints.Reset();
			WeakSinglePointConstraints.Reset();
			WeakSinglePointSweptConstraints.Reset();
		}

		void SaveConstraints(const FCollisionConstraintsArray& CollisionsArray)
		{
			SavedConstraints = CollisionsArray;

			//Create weak handles so we can make sure everything is alive later
			auto SaveArrayHelper = [](auto& Constraints,auto& WeakPairs)
			{
				WeakPairs.Empty(Constraints.Num());
				for(FCollisionConstraintBase& Constraint : Constraints)
				{
					WeakPairs.Add(FWeakConstraintPair{Constraint.Particle[0]->WeakParticleHandle(),Constraint.Particle[1]->WeakParticleHandle()});

					auto* A = Constraint.Particle[0];
					auto* B = Constraint.Particle[1];

					//Need to do this on save for any new constraints
					MarkSoftIfDesync(*A,*B);
				}
				check(Constraints.Num() == WeakPairs.Num());
			};

			SaveArrayHelper(SavedConstraints.SinglePointConstraints,WeakSinglePointConstraints);
			SaveArrayHelper(SavedConstraints.SinglePointSweptConstraints,WeakSinglePointSweptConstraints);
		}

		//Returns all constraints that are still valid (resim can invalidate constraints by either deleting particles, moving particles, etc...)
		const FCollisionConstraintsArray& GetAndSanitizeConstraints()
		{
			auto CleanupArrayHelper = [](auto& Constraints,auto& WeakPairs)
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
						//must do this on get in case particles are no longer constrained (means we won't see them during save above)
						bValidConstraint = !MarkSoftIfDesync(*A,*B);
					}

					if(!bValidConstraint)
					{
						Constraints.RemoveAtSwap(Idx);
						WeakPairs.RemoveAtSwap(Idx);
					}
				}
			};

			CleanupArrayHelper(SavedConstraints.SinglePointConstraints,WeakSinglePointConstraints);
			CleanupArrayHelper(SavedConstraints.SinglePointSweptConstraints,WeakSinglePointSweptConstraints);

			return SavedConstraints;
		}

	private:

		static bool MarkSoftIfDesync(TGeometryParticleHandle<FReal,3>& A,TGeometryParticleHandle<FReal,3>& B)
		{
			if(A.SyncState() == ESyncState::HardDesync || B.SyncState() == ESyncState::HardDesync)
			{
				if(A.SyncState() != ESyncState::HardDesync)
				{
					//Need to resim, but may end up still in sync
					A.SetSyncState(ESyncState::SoftDesync);
				}

				if(B.SyncState() != ESyncState::HardDesync)
				{
					//Need to resim, but may end up still in sync
					B.SetSyncState(ESyncState::SoftDesync);
				}

				return true;
			}

			return false;
		}

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
	};

} // namespace Chaos
