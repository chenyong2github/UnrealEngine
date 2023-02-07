// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionSolverSimd.h"

#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Evolution/SolverBodyContainer.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/Utilities.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	namespace CVars
	{
		extern bool bChaos_PBDCollisionSolver_Velocity_FrictionEnabled;
		extern float Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness;
	}

	namespace Private
	{

		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////////////////

		// NOTE: Currently only implemeted for 4-lane SIMD

		// Same as FPlatformMisc::PrefetchBlock() but auto-unrolls the loop and avoids the divide.
		// Somehow this makes things slower
		template<typename T>
		FORCEINLINE static void PrefetchObject(const T* Object)
		{
			constexpr int32 CacheLineSize = PLATFORM_CACHE_LINE_SIZE;
			constexpr int32 NumBytes = sizeof(T);
			constexpr int32 NumLines = (NumBytes + CacheLineSize - 1) / CacheLineSize;

			int32 Offset = 0;
			for (int32 Line = 0; Line < NumLines; ++Line)
			{
				FPlatformMisc::Prefetch(Object, Offset);
				Offset += CacheLineSize;
			}
		}

		FORCEINLINE void PrefetchManifoldPoint(const TArrayView<TPBDCollisionSolverManifoldPointsSimd<4>>& ManifoldPoints, const int32 Index)
		{
			if (Index < ManifoldPoints.Num())
			{
				//PrefetchObject(&ManifoldPoints[Index]);
				FPlatformMisc::PrefetchBlock(&ManifoldPoints[Index], sizeof(TPBDCollisionSolverManifoldPointsSimd<4>));
			}
		}

		FORCEINLINE void PrefetchPositionSolverBodies(const TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodies, const int32 Index)
		{
			if (Index < SolverBodies.Num())
			{
				for (int32 LaneIndex = 0; LaneIndex < 4; ++LaneIndex)
				{
					SolverBodies[Index].Body0.GetValue(LaneIndex)->PrefetchPositionSolverData();
					SolverBodies[Index].Body1.GetValue(LaneIndex)->PrefetchPositionSolverData();
				}
			}
		}


		FORCEINLINE void PrefetchVelocitySolverBodies(const TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodies, const int32 Index)
		{
			if (Index < SolverBodies.Num())
			{
				for (int32 LaneIndex = 0; LaneIndex < 4; ++LaneIndex)
				{
					SolverBodies[Index].Body0.GetValue(LaneIndex)->PrefetchVelocitySolverData();
					SolverBodies[Index].Body1.GetValue(LaneIndex)->PrefetchVelocitySolverData();
				}
			}
		}

		static const int32 PrefetchCount = 4;

		template<>
		void FPBDCollisionSolverHelperSimd::SolvePositionNoFriction<4>(
			const TArrayView<TPBDCollisionSolverManifoldPointsSimd<4>>& ManifoldPoints,
			const TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodies,
			const FSolverReal InDt,
			const FSolverReal InMaxPushOut)
		{
			const FSimd4Realf MaxPushOut = FSimd4Realf::Make(InMaxPushOut);

			for (int32 Index = 0; Index < PrefetchCount; ++Index)
			{
				PrefetchManifoldPoint(ManifoldPoints, Index);
				PrefetchPositionSolverBodies(SolverBodies, Index);
			}

			for (int32 Index = 0; Index < ManifoldPoints.Num(); ++Index)
			{
				PrefetchManifoldPoint(ManifoldPoints, Index + PrefetchCount);
				PrefetchPositionSolverBodies(SolverBodies, Index + PrefetchCount);

				ManifoldPoints[Index].SolvePositionNoFriction(SolverBodies[Index].Body0, SolverBodies[Index].Body1, MaxPushOut);
			}
		}

		template<>
		void FPBDCollisionSolverHelperSimd::SolvePositionWithFriction<4>(
			const TArrayView<TPBDCollisionSolverManifoldPointsSimd<4>>& ManifoldPoints,
			const TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodies,
			const FSolverReal InDt,
			const FSolverReal InMaxPushOut)
		{
			const FSimd4Realf MaxPushOut = FSimd4Realf::Make(InMaxPushOut);
			const FSimd4Realf FrictionStiffnessScale = FSimd4Realf::Make(CVars::Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness);

			for (int32 Index = 0; Index < PrefetchCount; ++Index)
			{
				PrefetchManifoldPoint(ManifoldPoints, Index);
				PrefetchPositionSolverBodies(SolverBodies, Index);
			}

			for (int32 Index = 0; Index < ManifoldPoints.Num(); ++Index)
			{
				PrefetchManifoldPoint(ManifoldPoints, Index + PrefetchCount);
				PrefetchPositionSolverBodies(SolverBodies, Index + PrefetchCount);

				ManifoldPoints[Index].SolvePositionWithFriction(SolverBodies[Index].Body0, SolverBodies[Index].Body1, MaxPushOut, FrictionStiffnessScale);
			}
		}

		template<>
		void FPBDCollisionSolverHelperSimd::SolveVelocityNoFriction<4>(
			const TArrayView<TPBDCollisionSolverManifoldPointsSimd<4>>& ManifoldPoints,
			const TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodies,
			const FSolverReal InDt)
		{
			const FSimd4Realf Dt = FSimd4Realf::Make(InDt);

			for (int32 Index = 0; Index < PrefetchCount; ++Index)
			{
				PrefetchManifoldPoint(ManifoldPoints, Index);
				PrefetchVelocitySolverBodies(SolverBodies, Index);
			}

			for (int32 Index = 0; Index < ManifoldPoints.Num(); ++Index)
			{
				PrefetchManifoldPoint(ManifoldPoints, Index + PrefetchCount);
				PrefetchVelocitySolverBodies(SolverBodies, Index + PrefetchCount);

				ManifoldPoints[Index].SolveVelocityNoFriction(SolverBodies[Index].Body0, SolverBodies[Index].Body1, Dt);
			}
		}

		template<>
		void FPBDCollisionSolverHelperSimd::SolveVelocityWithFriction<4>(
			const TArrayView<TPBDCollisionSolverManifoldPointsSimd<4>>& ManifoldPoints,
			const TArrayView<TSolverBodyPtrPairSimd<4>>& SolverBodies,
			const FSolverReal InDt)
		{
			if (!CVars::bChaos_PBDCollisionSolver_Velocity_FrictionEnabled)
			{
				SolveVelocityNoFriction(ManifoldPoints, SolverBodies, InDt);
				return;
			}

			for (int32 Index = 0; Index < PrefetchCount; ++Index)
			{
				PrefetchManifoldPoint(ManifoldPoints, Index);
				PrefetchVelocitySolverBodies(SolverBodies, Index);
			}

			const FSimd4Realf Dt = FSimd4Realf::Make(InDt);
			const FSimd4Realf FrictionStiffnessScale = FSimd4Realf::Make(CVars::Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness);

			for (int32 Index = 0; Index < ManifoldPoints.Num(); ++Index)
			{
				PrefetchManifoldPoint(ManifoldPoints, Index + PrefetchCount);
				PrefetchVelocitySolverBodies(SolverBodies, Index + PrefetchCount);

				ManifoldPoints[Index].SolveVelocityWithFriction(SolverBodies[Index].Body0, SolverBodies[Index].Body1, Dt, FrictionStiffnessScale);
			}
		}

		void FPBDCollisionSolverHelperSimd::CheckISPC()
		{
		}

	}	// namespace Private
}	// namespace Chaos