// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosCloth/ChaosClothConstraints.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/XPBDSpringConstraints.h"
#include "Chaos/PBDBendingConstraints.h"
#include "Chaos/PBDAxialSpringConstraints.h"
#include "Chaos/XPBDAxialSpringConstraints.h"
#include "Chaos/PBDVolumeConstraint.h"
#include "Chaos/XPBDLongRangeConstraints.h"
#include "Chaos/PBDSphericalConstraint.h"
#include "Chaos/PBDAnimDriveConstraint.h"
#include "Chaos/PBDShapeConstraints.h"
#include "Chaos/PBDCollisionSpringConstraints.h"
#include "Chaos/PBDEvolution.h"

using namespace Chaos;

FClothConstraints::FClothConstraints()
	: Evolution(nullptr)
	, AnimationPositions(nullptr)
	, OldAnimationPositions(nullptr)
	, AnimationNormals(nullptr)
	, ParticleOffset(0)
	, NumParticles(0)
	, ConstraintInitOffset(INDEX_NONE)
	, ConstraintRuleOffset(INDEX_NONE)
	, NumConstraintInits(0)
	, NumConstraintRules(0)
	, MaxDistancesMultiplier(1.f)
	, AnimDriveStiffness(0.f)
	, AnimDriveDamping(0.f)
{
}

FClothConstraints::~FClothConstraints()
{
}

void FClothConstraints::Initialize(
	FPBDEvolution* InEvolution,
	const TArray<FVec3>& InAnimationPositions,
	const TArray<FVec3>& InOldAnimationPositions,
	const TArray<FVec3>& InAnimationNormals,
	int32 InParticleOffset,
	int32 InNumParticles)
{
	Evolution = InEvolution;
	AnimationPositions = &InAnimationPositions;
	OldAnimationPositions = &InOldAnimationPositions;
	AnimationNormals = &InAnimationNormals;
	ParticleOffset = InParticleOffset;
	NumParticles = InNumParticles;
}

void FClothConstraints::Enable(bool bEnable)
{
	check(Evolution);
	if (ConstraintInitOffset != INDEX_NONE)
	{
		Evolution->ActivateConstraintInitRange(ConstraintInitOffset, bEnable);
	}
	if (ConstraintRuleOffset != INDEX_NONE)
	{
		Evolution->ActivateConstraintRuleRange(ConstraintRuleOffset, bEnable);
	}
}

void FClothConstraints::CreateRules()
{
	check(Evolution);
	check(ConstraintInitOffset == INDEX_NONE)
	if (NumConstraintInits)
	{
		ConstraintInitOffset = Evolution->AddConstraintInitRange(NumConstraintInits, false);
	}
	check(ConstraintRuleOffset == INDEX_NONE)
	if (NumConstraintRules)
	{
		ConstraintRuleOffset = Evolution->AddConstraintRuleRange(NumConstraintRules, false);
	}

	TFunction<void(const FPBDParticles&)>* const ConstraintInits = Evolution->ConstraintInits().GetData() + ConstraintInitOffset;
	TFunction<void(FPBDParticles&, const FReal)>* const ConstraintRules = Evolution->ConstraintRules().GetData() + ConstraintRuleOffset;

	int32 ConstraintInitIndex = 0;
	int32 ConstraintRuleIndex = 0;

	if (XEdgeConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& /*Particles*/)
			{
				XEdgeConstraints->Init();
			};

		ConstraintRules[ConstraintRuleIndex++] = 
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				XEdgeConstraints->Apply(Particles, Dt);
			};
	}
	if (EdgeConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				EdgeConstraints->Apply(Particles, Dt);
			};
	}
	if (XBendingConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& /*Particles*/)
			{
				XBendingConstraints->Init();
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				XBendingConstraints->Apply(Particles, Dt);
			};
	}
	if (BendingConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				BendingConstraints->Apply(Particles, Dt);
			};
	}
	if (BendingElementConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				BendingElementConstraints->Apply(Particles, Dt);
			};
	}
	if (XAreaConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& /*Particles*/)
			{
				XAreaConstraints->Init();
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				XAreaConstraints->Apply(Particles, Dt);
			};
	}
	if (AreaConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				AreaConstraints->Apply(Particles, Dt);
			};
	}
	if (ThinShellVolumeConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				ThinShellVolumeConstraints->Apply(Particles, Dt);
			};
	}
	if (VolumeConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				VolumeConstraints->Apply(Particles, Dt);
			};
	}
	if (XLongRangeConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& /*Particles*/)
			{
				XLongRangeConstraints->Init();
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				XLongRangeConstraints->Apply(Particles, Dt);
			};
	}
	if (LongRangeConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				LongRangeConstraints->Apply(Particles, Dt);
			};
	}
	if (MaximumDistanceConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				MaximumDistanceConstraints->SetSphereRadiiMultiplier(FMath::Max(0.f, MaxDistancesMultiplier));
				MaximumDistanceConstraints->Apply(Particles, Dt);
			};
	}
	if (BackstopConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				BackstopConstraints->Apply(Particles, Dt);
			};
	}
	if (AnimDriveConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				AnimDriveConstraints->SetProperties(AnimDriveStiffness, AnimDriveDamping, Dt, Evolution->GetIterations());  // TODO: Move to init function, this isn't a per iteration call
				AnimDriveConstraints->Apply(Particles, Dt);
			};
	}
	if (ShapeConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				ShapeConstraints->Apply(Particles, Dt);
			};
	}
	if (SelfCollisionConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& Particles)
			{
				SelfCollisionConstraints->Init(Particles);
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				SelfCollisionConstraints->Apply(Particles, Dt);
			};
	}
	check(ConstraintInitIndex == NumConstraintInits);
	check(ConstraintRuleIndex == NumConstraintRules);
}

void FClothConstraints::SetEdgeConstraints(const TArray<TVec3<int32>>& SurfaceElements, FReal EdgeStiffness, bool bUseXPBDConstraints)
{
	check(Evolution);
	check(EdgeStiffness > 0.f && EdgeStiffness <= 1.f);

	if (bUseXPBDConstraints)
	{
		XEdgeConstraints = MakeShared<FXPBDSpringConstraints>(Evolution->Particles(), SurfaceElements, EdgeStiffness, /*bStripKinematicConstraints =*/ true);
		++NumConstraintInits;
	}
	else
	{
		EdgeConstraints = MakeShared<FPBDSpringConstraints>(Evolution->Particles(), SurfaceElements, EdgeStiffness, /*bStripKinematicConstraints =*/ true);
	}
	++NumConstraintRules;
}

void FClothConstraints::SetBendingConstraints(TArray<TVec2<int32>>&& Edges, FReal BendingStiffness, bool bUseXPBDConstraints)
{
	check(Evolution);

	if (bUseXPBDConstraints)
	{
		XBendingConstraints = MakeShared<FXPBDSpringConstraints>(Evolution->Particles(), MoveTemp(Edges), BendingStiffness, /*bStripKinematicConstraints =*/ true);
		++NumConstraintInits;
	}
	else
	{
		BendingConstraints = MakeShared<FPBDSpringConstraints>(Evolution->Particles(), MoveTemp(Edges), BendingStiffness, /*bStripKinematicConstraints =*/ true);
	}
	++NumConstraintRules;
}

void FClothConstraints::SetBendingConstraints(TArray<TVec4<int32>>&& BendingElements, FReal BendingStiffness)
{
	check(Evolution);
	check(BendingStiffness > 0.f && BendingStiffness <= 1.f);

	BendingElementConstraints = MakeShared<FPBDBendingConstraints>(Evolution->Particles(), MoveTemp(BendingElements), BendingStiffness);  // TODO: Strip kinematic constraints
	++NumConstraintRules;
}

void FClothConstraints::SetAreaConstraints(TArray<TVec3<int32>>&& SurfaceElements, FReal AreaStiffness, bool bUseXPBDConstraints)
{
	check(Evolution);
	check(AreaStiffness > 0.f && AreaStiffness <= 1.f);

	if (bUseXPBDConstraints)
	{
		XAreaConstraints = MakeShared<FXPBDAxialSpringConstraints>(Evolution->Particles(), MoveTemp(SurfaceElements), AreaStiffness);
		++NumConstraintInits;
	}
	else
	{
		AreaConstraints = MakeShared<FPBDAxialSpringConstraints>(Evolution->Particles(), MoveTemp(SurfaceElements), AreaStiffness);
	}
	++NumConstraintRules;
}

void FClothConstraints::SetVolumeConstraints(TArray<TVec2<int32>>&& DoubleBendingEdges, FReal VolumeStiffness)
{
	check(Evolution);
	check(VolumeStiffness > 0.f && VolumeStiffness <= 1.f);

	ThinShellVolumeConstraints = MakeShared<FPBDSpringConstraints>(Evolution->Particles(), MoveTemp(DoubleBendingEdges), VolumeStiffness);
	++NumConstraintRules;
}

void FClothConstraints::SetVolumeConstraints(TArray<TVec3<int32>>&& SurfaceElements, FReal VolumeStiffness)
{
	check(Evolution);
	check(VolumeStiffness > 0.f && VolumeStiffness <= 1.f);

	VolumeConstraints = MakeShared<FPBDVolumeConstraint>(Evolution->Particles(), MoveTemp(SurfaceElements), VolumeStiffness);
	++NumConstraintRules;
}

void FClothConstraints::SetLongRangeConstraints(const TMap<int32, TSet<uint32>>& PointToNeighborsMap, FReal StrainLimitingStiffness, FReal LimitScale, ETetherMode TetherMode, bool bUseXPBDConstraints)
{
	check(Evolution);
	check(StrainLimitingStiffness > 0.f && StrainLimitingStiffness <= 1.f);

	if (bUseXPBDConstraints)
	{
		XLongRangeConstraints = MakeShared<FXPBDLongRangeConstraints>(
			Evolution->Particles(),
			PointToNeighborsMap, 
			10, // The max number of connected neighbors per particle.
			StrainLimitingStiffness);  // TODO: Add LimitScale to the XPBD constraint
		++NumConstraintInits;
	}
	else
	{
		LongRangeConstraints = MakeShared<FPBDLongRangeConstraints>(
			Evolution->Particles(),
			PointToNeighborsMap,
			10, // The max number of connected neighbors per particle.
			StrainLimitingStiffness,
			LimitScale,
			TetherMode);
	}
	++NumConstraintRules;
}

void FClothConstraints::SetMaximumDistanceConstraints(const TConstArrayView<FReal>& MaxDistances)
{
	MaximumDistanceConstraints = MakeShared<FPBDSphericalConstraint>(
		ParticleOffset,
		NumParticles,
		*AnimationPositions,
		MaxDistances);
	++NumConstraintRules;
}

void FClothConstraints::SetBackstopConstraints(const TConstArrayView<FReal>& BackstopDistances, const TConstArrayView<FReal>& BackstopRadiuses, bool bUseLegacyBackstop)
{
	BackstopConstraints = MakeShared<FPBDSphericalBackstopConstraint>(
		ParticleOffset,
		NumParticles,
		*AnimationPositions,
		*AnimationNormals,
		BackstopRadiuses,
		BackstopDistances,
		bUseLegacyBackstop);
	++NumConstraintRules;
}

void FClothConstraints::SetAnimDriveConstraints(const TConstArrayView<FReal>& AnimDriveStiffnessMultipliers, const TConstArrayView<FReal>& AnimDriveDampingMultipliers)
{
	AnimDriveConstraints = MakeShared<FPBDAnimDriveConstraint>(
		ParticleOffset,
		NumParticles,
		*AnimationPositions,
		*OldAnimationPositions,
		AnimDriveStiffnessMultipliers,
		AnimDriveDampingMultipliers);
	++NumConstraintRules;
}

void FClothConstraints::SetShapeTargetConstraints(FReal ShapeTargetStiffness)
{
	// TODO: Review this constraint. Currently does nothing more than the anim drive with less controls
	check(ShapeTargetStiffness > 0.f && ShapeTargetStiffness <= 1.f);

	ShapeConstraints = MakeShared<FPBDShapeConstraints>(
		ParticleOffset,
		NumParticles,
		*AnimationPositions,
		*AnimationPositions,
		ShapeTargetStiffness);
	++NumConstraintRules;
}

void FClothConstraints::SetSelfCollisionConstraints(const TArray<TVec3<int32>>& SurfaceElements, TSet<TVec2<int32>>&& DisabledCollisionElements, FReal SelfCollisionThickness)
{
	SelfCollisionConstraints = MakeShared<FPBDCollisionSpringConstraints>(
		ParticleOffset,
		NumParticles,
		SurfaceElements,
		MoveTemp(DisabledCollisionElements),
		SelfCollisionThickness,
		/*Stiffness =*/ 1.f);
	++NumConstraintInits;  // Self collision has an init
	++NumConstraintRules;  // and a rule
}

