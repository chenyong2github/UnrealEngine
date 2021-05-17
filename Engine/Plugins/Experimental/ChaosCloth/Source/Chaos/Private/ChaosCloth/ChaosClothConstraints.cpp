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

	TFunction<void(const FPBDParticles&, const FReal)>* const ConstraintInits = Evolution->ConstraintInits().GetData() + ConstraintInitOffset;
	TFunction<void(FPBDParticles&, const FReal)>* const ConstraintRules = Evolution->ConstraintRules().GetData() + ConstraintRuleOffset;

	int32 ConstraintInitIndex = 0;
	int32 ConstraintRuleIndex = 0;

	if (XEdgeConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& /*Particles*/, const FReal Dt)
			{
				XEdgeConstraints->Init();
				XEdgeConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};

		ConstraintRules[ConstraintRuleIndex++] = 
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				XEdgeConstraints->Apply(Particles, Dt);
			};
	}
	if (EdgeConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& /*Particles*/, const FReal Dt)
			{
				EdgeConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				EdgeConstraints->Apply(Particles, Dt);
			};
	}
	if (XBendingConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& /*Particles*/, const FReal Dt)
			{
				XBendingConstraints->Init();
				XBendingConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				XBendingConstraints->Apply(Particles, Dt);
			};
	}
	if (BendingConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& /*Particles*/, const FReal Dt)
			{
				BendingConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
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
			[this](const FPBDParticles& /*Particles*/, const FReal Dt)
			{
				XAreaConstraints->Init();
				XAreaConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				XAreaConstraints->Apply(Particles, Dt);
			};
	}
	if (AreaConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& /*Particles*/, const FReal Dt)
			{
				AreaConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
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
			[this](const FPBDParticles& /*Particles*/, const FReal Dt)
			{
				XLongRangeConstraints->Init();
				XLongRangeConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
				XLongRangeConstraints->Apply(Particles, Dt);
			};
	}
	if (LongRangeConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& /*Particles*/, const FReal Dt)
			{
				LongRangeConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
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
		ConstraintInits[ConstraintInitIndex++] =
			[this](const FPBDParticles& /*Particles*/, const FReal Dt)
			{
				AnimDriveConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};

		ConstraintRules[ConstraintRuleIndex++] =
			[this](FPBDParticles& Particles, const FReal Dt)
			{
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
			[this](const FPBDParticles& Particles, const FReal /*Dt*/)
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

void FClothConstraints::SetEdgeConstraints(const TArray<TVec3<int32>>& SurfaceElements, const TConstArrayView<FRealSingle>& StiffnessMultipliers, bool bUseXPBDConstraints)
{
	check(Evolution);

	if (bUseXPBDConstraints)
	{
		XEdgeConstraints = MakeShared<FXPBDSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset, NumParticles,
			SurfaceElements,
			StiffnessMultipliers,
			/*InStiffness =*/ FVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	else
	{
		EdgeConstraints = MakeShared<FPBDSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			SurfaceElements,
			StiffnessMultipliers,
			/*InStiffness =*/ FVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetBendingConstraints(const TArray<TVec2<int32>>& Edges, const TConstArrayView<FRealSingle>& StiffnessMultipliers, bool bUseXPBDConstraints)
{
	check(Evolution);

	if (bUseXPBDConstraints)
	{
		XBendingConstraints = MakeShared<FXPBDSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset, NumParticles,
			Edges,
			StiffnessMultipliers,
			/*InStiffness =*/ FVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	else
	{
		BendingConstraints = MakeShared<FPBDSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			Edges,
			StiffnessMultipliers,
			/*InStiffness =*/ FVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetBendingConstraints(TArray<TVec4<int32>>&& BendingElements, FReal BendingStiffness)
{
	check(Evolution);

	BendingElementConstraints = MakeShared<FPBDBendingConstraints>(Evolution->Particles(), MoveTemp(BendingElements), BendingStiffness);  // TODO: Strip kinematic constraints
	++NumConstraintRules;
}

void FClothConstraints::SetAreaConstraints(const TArray<TVec3<int32>>& SurfaceElements, const TConstArrayView<FRealSingle>& StiffnessMultipliers, bool bUseXPBDConstraints)
{
	check(Evolution);

	if (bUseXPBDConstraints)
	{
		XAreaConstraints = MakeShared<FXPBDAxialSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			SurfaceElements,
			StiffnessMultipliers,
			/*InStiffness =*/ FVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	else
	{
		AreaConstraints = MakeShared<FPBDAxialSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			SurfaceElements,
			StiffnessMultipliers,
			/*InStiffness =*/ FVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetVolumeConstraints(const TArray<TVec2<int32>>& DoubleBendingEdges, FReal VolumeStiffness)
{
	check(Evolution);

	ThinShellVolumeConstraints = MakeShared<FPBDSpringConstraints>(
		Evolution->Particles(),
		ParticleOffset,
		NumParticles,
		DoubleBendingEdges,
		TConstArrayView<FRealSingle>(),
		VolumeStiffness,
		/*bTrimKinematicConstraints =*/ true);
	++NumConstraintRules;
}

void FClothConstraints::SetVolumeConstraints(TArray<TVec3<int32>>&& SurfaceElements, FReal VolumeStiffness)
{
	check(Evolution);
	check(VolumeStiffness > 0.f && VolumeStiffness <= 1.f);

	VolumeConstraints = MakeShared<FPBDVolumeConstraint>(Evolution->Particles(), MoveTemp(SurfaceElements), VolumeStiffness);
	++NumConstraintRules;
}

void FClothConstraints::SetLongRangeConstraints(const TMap<int32, TSet<int32>>& PointToNeighborsMap,
	const TConstArrayView<FRealSingle>& TetherStiffnessMultipliers, const TConstArrayView<FRealSingle>& TetherScaleMultipliers,
	const FVec2& TetherScale, ETetherMode TetherMode, bool bUseXPBDConstraints)
{
	check(Evolution);

	static const int32 MaxNumTetherIslands = 4;  // The max number of connected neighbors per particle.

	if (bUseXPBDConstraints)
	{
		XLongRangeConstraints = MakeShared<FXPBDLongRangeConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			PointToNeighborsMap,
			TetherStiffnessMultipliers,
			TetherScaleMultipliers,
			MaxNumTetherIslands,
			/*InStiffness =*/ FVec2::UnitVector,
			TetherScale,
			TetherMode);
	}
	else
	{
		LongRangeConstraints = MakeShared<FPBDLongRangeConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			PointToNeighborsMap,
			TetherStiffnessMultipliers,
			TetherScaleMultipliers,
			MaxNumTetherIslands,
			/*InStiffness =*/ FVec2::UnitVector,
			TetherScale,
			TetherMode);
	}
	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetMaximumDistanceConstraints(const TConstArrayView<FRealSingle>& MaxDistances)
{
	MaximumDistanceConstraints = MakeShared<FPBDSphericalConstraint>(
		ParticleOffset,
		NumParticles,
		*AnimationPositions,
		MaxDistances);
	++NumConstraintRules;
}

void FClothConstraints::SetBackstopConstraints(const TConstArrayView<FRealSingle>& BackstopDistances, const TConstArrayView<FRealSingle>& BackstopRadiuses, bool bUseLegacyBackstop)
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

void FClothConstraints::SetAnimDriveConstraints(const TConstArrayView<FRealSingle>& AnimDriveStiffnessMultipliers, const TConstArrayView<FRealSingle>& AnimDriveDampingMultipliers)
{
	AnimDriveConstraints = MakeShared<FPBDAnimDriveConstraint>(
		ParticleOffset,
		NumParticles,
		*AnimationPositions,
		*OldAnimationPositions,
		AnimDriveStiffnessMultipliers,
		AnimDriveDampingMultipliers);
	++NumConstraintInits;  // Uses init to update the property tables
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

void FClothConstraints::SetEdgeProperties(const FVec2& EdgeStiffness)
{
	if (EdgeConstraints)
	{
		EdgeConstraints->SetProperties(EdgeStiffness);
	}
	if (XEdgeConstraints)
	{
		XEdgeConstraints->SetProperties(EdgeStiffness);
	}
}

void FClothConstraints::SetBendingProperties(const FVec2& BendingStiffness)
{
	if (BendingConstraints)
	{
		BendingConstraints->SetProperties(BendingStiffness);
	}
	if (XBendingConstraints)
	{
		XBendingConstraints->SetProperties(BendingStiffness);
	}
}

void FClothConstraints::SetAreaProperties(const FVec2& AreaStiffness)
{
	if (AreaConstraints)
	{
		AreaConstraints->SetProperties(AreaStiffness);
	}
	if (XAreaConstraints)
	{
		XAreaConstraints->SetProperties(AreaStiffness);
	}
}

void FClothConstraints::SetThinShellVolumeProperties(FReal VolumeStiffness)
{
	if (ThinShellVolumeConstraints)
	{
		ThinShellVolumeConstraints->SetProperties(VolumeStiffness);
	}
}

void FClothConstraints::SetVolumeProperties(FReal VolumeStiffness)
{
	if (VolumeConstraints)
	{
		VolumeConstraints->SetStiffness(VolumeStiffness);
	}
}

void FClothConstraints::SetLongRangeAttachmentProperties(const FVec2& TetherStiffness)
{
	if (LongRangeConstraints)
	{
		LongRangeConstraints->SetStiffness(TetherStiffness);
	}
	if (XLongRangeConstraints)
	{
		XLongRangeConstraints->SetStiffness(TetherStiffness);
	}
}

void FClothConstraints::SetMaximumDistanceProperties(FReal MaxDistancesMultiplier)
{
	if (MaximumDistanceConstraints)
	{
		MaximumDistanceConstraints->SetSphereRadiiMultiplier(MaxDistancesMultiplier);
	}
}

void FClothConstraints::SetAnimDriveProperties(const FVec2& AnimDriveStiffness, const FVec2& AnimDriveDamping)
{
	if (AnimDriveConstraints)
	{
		AnimDriveConstraints->SetProperties(AnimDriveStiffness, AnimDriveDamping);
	}
}

void FClothConstraints::SetSelfCollisionProperties(FReal SelfCollisionThickness)
{
	if (SelfCollisionConstraints)
	{
		SelfCollisionConstraints->SetThickness(SelfCollisionThickness);
	}
}
