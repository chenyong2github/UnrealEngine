// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PerParticlePBDCollisionConstraint.h"
#include "ChaosStats.h"
#if INTEL_ISPC
#include "PerParticlePBDCollisionConstraint.ispc.generated.h"
#endif

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_PerParticleCollision_ISPC_Enabled = false;  // TODO: Enable once ISPC collision works
FAutoConsoleVariableRef CVarChaosPerParticleCollisionISPCEnabled(TEXT("p.Chaos.PerParticleCollision.ISPC"), bChaos_PerParticleCollision_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in per particle collisions"));
#endif

int32 Chaos_PerParticleCollision_ISPC_ParallelBatchSize = 128;
FAutoConsoleVariableRef CVarChaosPerParticleCollisionISPCParallelBatchSize(TEXT("p.Chaos.PerParticleCollision.ISPC.ParallelBatchSize"), Chaos_PerParticleCollision_ISPC_ParallelBatchSize, TEXT("Parallel batch size for ISPC"));

using namespace Chaos;

template<class T, int d, EGeometryParticlesSimType SimType>
void TPerParticlePBDCollisionConstraint<T, d, SimType>::ApplyHelperISPC(TPBDParticles<T, d>& Particles, const T Dt, int32 Offset, int32 Range) const
{
	PhysicsParallelFor(Range - Offset, [&](int32 Index)
	{
		Apply(Particles, Dt, Index + Offset);
	});
}

template<>
void TPerParticlePBDCollisionConstraint<float, 3, EGeometryParticlesSimType::RigidBodySim>::ApplyHelperISPC(TPBDParticles <float, 3> & Particles, const float Dt, int32 Offset, int32 Range) const
{
	PhysicsParallelFor(Range - Offset, [&](int32 Index)
	{
		Apply(Particles, Dt, Index + Offset);
	});
}

// Helper function to call PhiWithNormal and return data to ISPC
extern "C" void GetPhiWithNormal(const uint8* CollisionParticles, const float* InV, float* Normal, float* Phi, const int32 i, const int32 ProgramCount, const int32 Mask)
{
	const TKinematicGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>& C = *(const TKinematicGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>*)CollisionParticles;
	
	for (int32 Index = 0; Index < ProgramCount; ++Index)
	{
		if (Mask & 1 << Index)
		{
			TVector<float, 3> V;

			// aos_to_soa3
			V.X = InV[Index];
			V.Y = InV[Index + ProgramCount];
			V.Z = InV[Index + 2 * ProgramCount];

			TVector<float, 3> Norm;
			Phi[Index] = C.Geometry(i)->PhiWithNormal(V, Norm);

			// aos_to_soa3
			Normal[Index] = Norm.X;
			Normal[Index + ProgramCount] = Norm.Y;
			Normal[Index + 2 * ProgramCount] = Norm.Z;
		}
	}
}

template<>
void TPerParticlePBDCollisionConstraint<float, 3, EGeometryParticlesSimType::Other>::ApplyHelperISPC(TPBDParticles <float, 3>& InParticles, const float Dt, int32 Offset, int32 Range) const
{
	FCollisionParticles& CollisionParticles = MCollisionParticlesActiveView.GetItems();

	const int32 NumBatches = FMath::CeilToInt((Range - Offset) / (float)Chaos_PerParticleCollision_ISPC_ParallelBatchSize);

	PhysicsParallelFor(NumBatches, [this, &InParticles, Dt, Offset, Range](int32 BatchNumber)
	{
		const int32 BatchBegin = Offset + (Chaos_PerParticleCollision_ISPC_ParallelBatchSize * BatchNumber);
		const int32 BatchEnd = FMath::Min(Range, BatchBegin + Chaos_PerParticleCollision_ISPC_ParallelBatchSize);

#if INTEL_ISPC
		MCollisionParticlesActiveView.RangeFor(
			[this, &InParticles, Dt, BatchBegin, BatchEnd](FCollisionParticles& CollisionParticles, int32 CollisionOffset, int32 CollisionRange)
			{
				ispc::ApplyPerParticleCollision(
					(ispc::FVector*)InParticles.GetP().GetData(),
					(const ispc::FVector*)InParticles.XArray().GetData(),
					InParticles.GetInvM().GetData(),
					(const ispc::FVector*)CollisionParticles.AllV().GetData(),
					(const ispc::FVector*)CollisionParticles.XArray().GetData(),
					(const ispc::FVector*)CollisionParticles.AllW().GetData(),
					(const ispc::FVector4*)CollisionParticles.AllR().GetData(),
					MDynamicGroupIds.GetData(),
					MKinematicGroupIds.GetData(),
					MPerGroupFriction.GetData(),
					MPerGroupThickness.GetData(),
					(const uint8*)&CollisionParticles,
					(const uint8*)CollisionParticles.GetAllGeometry().GetData(),
					sizeof(FImplicitObject),
					CollisionParticles.Geometry(0)->GetOffsetOfType(),
					Dt,
					CollisionOffset,
					CollisionRange,
					BatchBegin,
					BatchEnd);
			});
#endif
	});
}

template class Chaos::TPerParticlePBDCollisionConstraint<float, 3, EGeometryParticlesSimType::RigidBodySim>;
template class Chaos::TPerParticlePBDCollisionConstraint<float, 3, EGeometryParticlesSimType::Other>;
