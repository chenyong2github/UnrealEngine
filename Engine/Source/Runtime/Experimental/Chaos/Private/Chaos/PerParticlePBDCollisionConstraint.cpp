// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PerParticlePBDCollisionConstraint.h"
#include "ChaosStats.h"
#include "HAL/IConsoleManager.h"
#if INTEL_ISPC
#include "PerParticlePBDCollisionConstraint.ispc.generated.h"
#endif

#if INTEL_ISPC && !UE_BUILD_SHIPPING
static_assert(sizeof(ispc::FVector3f) == sizeof(Chaos::Softs::FSolverVec3), "sizeof(ispc::FVector3f) != sizeof(Chaos::Softs::FSolverVec3)");
static_assert(sizeof(ispc::FVector4f) == sizeof(Chaos::Softs::FSolverRotation3), "sizeof(ispc::FVector4f) != sizeof(Chaos::Softs::FSolverRotation3)");

bool bChaos_PerParticleCollision_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosPerParticleCollisionISPCEnabled(TEXT("p.Chaos.PerParticleCollision.ISPC"), bChaos_PerParticleCollision_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in per particle collisions"));
#endif

static int32 Chaos_PerParticleCollision_ISPC_ParallelBatchSize = 128;
static bool Chaos_PerParticleCollision_ISPC_FastFriction = true;
#if !UE_BUILD_SHIPPING
FAutoConsoleVariableRef CVarChaosPerParticleCollisionISPCParallelBatchSize(TEXT("p.Chaos.PerParticleCollision.ISPC.ParallelBatchSize"), Chaos_PerParticleCollision_ISPC_ParallelBatchSize, TEXT("Parallel batch size for ISPC"));
FAutoConsoleVariableRef CVarChaosPerParticleCollisionISPCFastFriction(TEXT("p.Chaos.PerParticleCollision.ISPC.FastFriction"), Chaos_PerParticleCollision_ISPC_FastFriction, TEXT("Faster friction ISPC"));
#endif

namespace Chaos::Softs {

// Helper function to call PhiWithNormal and return data to ISPC
extern "C" void GetPhiWithNormal(const uint8* CollisionParticles, const FSolverReal* InV, FSolverReal* Normal, FSolverReal* Phi, const int32 i, const int32 ProgramCount, const int32 Mask)
{
	const TKinematicGeometryParticlesImp<FSolverReal, 3, EGeometryParticlesSimType::Other>& C = *(const TKinematicGeometryParticlesImp<FSolverReal, 3, EGeometryParticlesSimType::Other>*)CollisionParticles;
	
	for (int32 Index = 0; Index < ProgramCount; ++Index)
	{
		if (Mask & (1 << Index))
		{
			FSolverVec3 V;

			// aos_to_soa3
			V.X = InV[Index];
			V.Y = InV[Index + ProgramCount];
			V.Z = InV[Index + 2 * ProgramCount];

			FVec3 ImplicitNormal;
			Phi[Index] = (FSolverReal)C.Geometry(i)->PhiWithNormal(FVec3(V), ImplicitNormal);
			FSolverVec3 Norm(ImplicitNormal);

			// aos_to_soa3
			Normal[Index] = Norm.X;
			Normal[Index + ProgramCount] = Norm.Y;
			Normal[Index + 2 * ProgramCount] = Norm.Z;
		}
	}
}

void FPerParticlePBDCollisionConstraint::ApplyHelperISPC(FSolverParticles& InParticles, const FSolverReal Dt, const int32 Offset, const int32 Range) const
{
	check(bRealTypeCompatibleWithISPC);

	const uint32 DynamicGroupId = MDynamicGroupIds[Offset];
	const FSolverReal PerGroupFriction = MPerGroupFriction[DynamicGroupId];
	const FSolverReal PerGroupThickness = MPerGroupThickness[DynamicGroupId];

	const int32 NumBatches = FMath::CeilToInt((FSolverReal)(Range - Offset) / (FSolverReal)Chaos_PerParticleCollision_ISPC_ParallelBatchSize);

	if (Chaos_PerParticleCollision_ISPC_FastFriction)
	{
		if (PerGroupFriction > KINDA_SMALL_NUMBER)  // Fast friction
		{
			PhysicsParallelFor(NumBatches, [this, &InParticles, Dt, Offset, Range, DynamicGroupId, PerGroupFriction, PerGroupThickness](int32 BatchNumber)
			{
				const int32 BatchBegin = Offset + (Chaos_PerParticleCollision_ISPC_ParallelBatchSize * BatchNumber);
				const int32 BatchEnd = FMath::Min(Range, BatchBegin + Chaos_PerParticleCollision_ISPC_ParallelBatchSize);

#if INTEL_ISPC
				MCollisionParticlesActiveView.RangeFor(
					[this, &InParticles, Dt, BatchBegin, BatchEnd, DynamicGroupId, PerGroupFriction, PerGroupThickness](FSolverRigidParticles& CollisionParticles, int32 CollisionOffset, int32 CollisionRange)
					{
						ispc::ApplyPerParticleCollisionFastFriction(
							(ispc::FVector4f*)InParticles.GetPAndInvM().GetData(),
							(const ispc::FVector3f*)InParticles.XArray().GetData(),
							(const ispc::FVector3f*)CollisionParticles.AllV().GetData(),
							(const ispc::FVector3f*)CollisionParticles.XArray().GetData(),
							(const ispc::FVector3f*)CollisionParticles.AllW().GetData(),
							(const ispc::FVector4f*)CollisionParticles.AllR().GetData(),
							DynamicGroupId,
							MKinematicGroupIds.GetData(),
							PerGroupFriction,
							PerGroupThickness,
							(const uint8*)&CollisionParticles,
							(const uint8*)CollisionParticles.GetAllGeometry().GetData(),
							sizeof(FImplicitObject),
							FImplicitObject::GetOffsetOfType(),
							FImplicitObject::GetOffsetOfMargin(),
							Dt,
							CollisionOffset,
							CollisionRange,
							BatchBegin,
							BatchEnd);
					});
#endif  // #if INTEL_ISPC
				});
		}
		else  // No friction
		{
			PhysicsParallelFor(NumBatches, [this, &InParticles, Dt, Offset, Range, DynamicGroupId, PerGroupFriction, PerGroupThickness](int32 BatchNumber)
			{
				const int32 BatchBegin = Offset + (Chaos_PerParticleCollision_ISPC_ParallelBatchSize * BatchNumber);
				const int32 BatchEnd = FMath::Min(Range, BatchBegin + Chaos_PerParticleCollision_ISPC_ParallelBatchSize);

#if INTEL_ISPC
				MCollisionParticlesActiveView.RangeFor(
					[this, &InParticles, Dt, BatchBegin, BatchEnd, DynamicGroupId, PerGroupThickness](FSolverRigidParticles& CollisionParticles, int32 CollisionOffset, int32 CollisionRange)
					{
						ispc::ApplyPerParticleCollisionNoFriction(
							(ispc::FVector4f*)InParticles.GetPAndInvM().GetData(),
							(const ispc::FVector3f*)InParticles.XArray().GetData(),
							(const ispc::FVector3f*)CollisionParticles.AllV().GetData(),
							(const ispc::FVector3f*)CollisionParticles.XArray().GetData(),
							(const ispc::FVector3f*)CollisionParticles.AllW().GetData(),
							(const ispc::FVector4f*)CollisionParticles.AllR().GetData(),
							DynamicGroupId,
							MKinematicGroupIds.GetData(),
							PerGroupThickness,
							(const uint8*)&CollisionParticles,
							(const uint8*)CollisionParticles.GetAllGeometry().GetData(),
							sizeof(FImplicitObject),
							FImplicitObject::GetOffsetOfType(),
							FImplicitObject::GetOffsetOfMargin(),
							Dt,
							CollisionOffset,
							CollisionRange,
							BatchBegin,
							BatchEnd);
					});
#endif  // #if INTEL_ISPC
				});
		}
	}
	else
	{
		PhysicsParallelFor(NumBatches, [this, &InParticles, Dt, Offset, Range](int32 BatchNumber)
		{
			const int32 BatchBegin = Offset + (Chaos_PerParticleCollision_ISPC_ParallelBatchSize * BatchNumber);
			const int32 BatchEnd = FMath::Min(Range, BatchBegin + Chaos_PerParticleCollision_ISPC_ParallelBatchSize);

#if INTEL_ISPC
			MCollisionParticlesActiveView.RangeFor(
				[this, &InParticles, Dt, BatchBegin, BatchEnd](FSolverRigidParticles& CollisionParticles, int32 CollisionOffset, int32 CollisionRange)
				{
					ispc::ApplyPerParticleCollision(
						(ispc::FVector4f*)InParticles.GetPAndInvM().GetData(),
						(const ispc::FVector3f*)InParticles.XArray().GetData(),
						(const ispc::FVector3f*)CollisionParticles.AllV().GetData(),
						(const ispc::FVector3f*)CollisionParticles.XArray().GetData(),
						(const ispc::FVector3f*)CollisionParticles.AllW().GetData(),
						(const ispc::FVector4f*)CollisionParticles.AllR().GetData(),
						MDynamicGroupIds.GetData(),
						MKinematicGroupIds.GetData(),
						MPerGroupFriction.GetData(),
						MPerGroupThickness.GetData(),
						(const uint8*)&CollisionParticles,
						(const uint8*)CollisionParticles.GetAllGeometry().GetData(),
						sizeof(FImplicitObject),
						FImplicitObject::GetOffsetOfType(),
						FImplicitObject::GetOffsetOfMargin(),
						Dt,
						CollisionOffset,
						CollisionRange,
						BatchBegin,
						BatchEnd);
				});
#endif  // #if INTEL_ISPC
		});
	}
}

}  // End namespace Chaos::Softs
