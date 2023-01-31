// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/SimSweep.h"
#include "Chaos/ISpatialAcceleration.h"

namespace Chaos
{
	namespace Private
	{
		bool SimSweepParticleFirstHit(
			ISpatialAcceleration<FAccelerationStructureHandle, FReal, 3>* SpatialAcceleration,
			FIgnoreCollisionManager* InIgnoreCollisionManager,
			const FGeometryParticleHandle* SweptParticle,
			const FVec3& StartPos,
			const FRotation3& Rot,
			const FVec3& Dir,
			const FReal Length,
			FSimSweepParticleHit& OutHit,
			const FReal InHitDistanceEqualTolerance)
		{
			FSimSweepParticleFilterBroadPhase ParticleFilter(InIgnoreCollisionManager);
			FSimSweepShapeFilterNarrowPhase ShapeFilter;
			FSimSweepCollectorFirstHit HitCollector(InHitDistanceEqualTolerance, OutHit);

			SimSweepParticle(SpatialAcceleration, SweptParticle, StartPos, Rot, Dir, Length, ParticleFilter, ShapeFilter, HitCollector);

			return OutHit.IsHit();
		}
	}
}