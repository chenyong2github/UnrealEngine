// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosCloth/ChaosClothConstraints.h"
#include "ChaosCloth/ChaosWeightMapTarget.h"
#include "ChaosCloth/ChaosClothingPatternData.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/XPBDSpringConstraints.h"
#include "Chaos/XPBDStretchBiasElementConstraints.h"
#include "Chaos/PBDBendingConstraints.h"
#include "Chaos/XPBDBendingConstraints.h"
#include "Chaos/XPBDAnisotropicBendingConstraints.h"
#include "Chaos/PBDAxialSpringConstraints.h"
#include "Chaos/XPBDAxialSpringConstraints.h"
#include "Chaos/PBDVolumeConstraint.h"
#include "Chaos/XPBDLongRangeConstraints.h"
#include "Chaos/PBDSphericalConstraint.h"
#include "Chaos/PBDAnimDriveConstraint.h"
#include "Chaos/PBDShapeConstraints.h"
#include "Chaos/PBDCollisionSpringConstraints.h"
#include "Chaos/PBDTriangleMeshCollisions.h"
#include "Chaos/PBDTriangleMeshIntersections.h"
#include "Chaos/PBDEvolution.h"
#include "Chaos/CollectionPropertyFacade.h"

namespace Chaos {

FClothConstraints::FClothConstraints()
	: Evolution(nullptr)
	, AnimationPositions(nullptr)
	, OldAnimationPositions_Deprecated(nullptr)
	, AnimationNormals(nullptr)
	, AnimationVelocities(nullptr)
	, ParticleOffset(0)
	, NumParticles(0)
	, ConstraintInitOffset(INDEX_NONE)
	, ConstraintRuleOffset(INDEX_NONE)
	, PostCollisionConstraintRuleOffset(INDEX_NONE)
	, NumConstraintInits(0)
	, NumConstraintRules(0)
	, NumPostCollisionConstraintRules(0)
{
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FClothConstraints::~FClothConstraints()
{
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FClothConstraints::Initialize(
	Softs::FPBDEvolution* InEvolution,
	const TArray<Softs::FSolverVec3>& InAnimationPositions,
	const TArray<Softs::FSolverVec3>& InOldAnimationPositions,
	const TArray<Softs::FSolverVec3>& InAnimationNormals,
	int32 InParticleOffset,
	int32 InNumParticles)
{
	Evolution = InEvolution;
	AnimationPositions = &InAnimationPositions;
	OldAnimationPositions_Deprecated = &InOldAnimationPositions;
	AnimationNormals = &InAnimationNormals;
	AnimationVelocities = nullptr;
	ParticleOffset = InParticleOffset;
	NumParticles = InNumParticles;
}

void FClothConstraints::Initialize(
	Softs::FPBDEvolution* InEvolution,
	const TArray<Softs::FSolverVec3>& InInterpolatedAnimationPositions,
	const TArray<Softs::FSolverVec3>& /*InOldAnimationPositions*/, // deprecated
	const TArray<Softs::FSolverVec3>& InInterpolatedAnimationNormals,
	const TArray<Softs::FSolverVec3>& InAnimationVelocities,
	int32 InParticleOffset,
	int32 InNumParticles)
{
	Evolution = InEvolution;
	AnimationPositions = &InInterpolatedAnimationPositions;
	OldAnimationPositions_Deprecated = nullptr;
	AnimationNormals = &InInterpolatedAnimationNormals;
	AnimationVelocities = &InAnimationVelocities;
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
	if (PostCollisionConstraintRuleOffset != INDEX_NONE)
	{
		Evolution->ActivatePostCollisionConstraintRuleRange(PostCollisionConstraintRuleOffset, bEnable);
	}
}

void FClothConstraints::AddRules(
	const Softs::FCollectionPropertyConstFacade& ConfigProperties,
	const FTriangleMesh& TriangleMesh,
	const TArray<TConstArrayView<FRealSingle>>& WeightMaps,
	const TArray<TConstArrayView<TTuple<int32, int32, FRealSingle>>>& Tethers,
	Softs::FSolverReal MeshScale,
	bool bEnabled,
	const FClothingPatternData* PatternData)
{
	// Self collisions
	CreateSelfCollisionConstraints(ConfigProperties, TriangleMesh);

	// Edge constraints
	CreateStretchConstraints(ConfigProperties, WeightMaps, TriangleMesh, PatternData);

	// Bending constraints
	CreateBendingConstraints(ConfigProperties, WeightMaps, TriangleMesh, PatternData);

	// Area constraints
	CreateAreaConstraints(ConfigProperties, WeightMaps, TriangleMesh);

	// Long range constraints
	CreateLongRangeConstraints(ConfigProperties, WeightMaps, Tethers, MeshScale);

	// Max distances
	CreateMaxDistanceConstraints(ConfigProperties, WeightMaps, MeshScale);

	// Backstop Constraints
	CreateBackstopConstraints(ConfigProperties, WeightMaps, MeshScale);

	// Animation Drive Constraints
	CreateAnimDriveConstraints(ConfigProperties, WeightMaps);

	// Commit rules to solver
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	CreateRules();  // TODO: Move CreateRules to private
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// Enable or disable constraints as requested
	Enable(bEnabled);
}

void FClothConstraints::CreateSelfCollisionConstraints(const Softs::FCollectionPropertyConstFacade& ConfigProperties, const FTriangleMesh& TriangleMesh)
{
	const bool bUseSelfCollisions = ConfigProperties.GetValue<bool>(TEXT("UseSelfCollisions"));

	if (bUseSelfCollisions)
	{
		static const int32 DisabledCollisionElementsN = 5;  // TODO: Make this a parameter?
		TSet<TVec2<int32>> DisabledCollisionElements;  // TODO: Is this needed? Turn this into a bit array?

		const TVec2<int32> Range = TriangleMesh.GetVertexRange();
		for (int32 Index = Range[0]; Index <= Range[1]; ++Index)
		{
			const TSet<int32> Neighbors = TriangleMesh.GetNRing(Index, DisabledCollisionElementsN);
			for (int32 Element : Neighbors)
			{
				check(Index != Element);
				DisabledCollisionElements.Emplace(TVec2<int32>(Index, Element));
				DisabledCollisionElements.Emplace(TVec2<int32>(Element, Index));
			}
		}

		const bool bUseSelfIntersections = ConfigProperties.GetValue<bool>(TEXT("UseSelfIntersections"));

		SelfCollisionInit = MakeShared<Softs::FPBDTriangleMeshCollisions>(
			ParticleOffset,
			NumParticles,
			TriangleMesh,
			ConfigProperties);

		SelfCollisionConstraints = MakeShared<Softs::FPBDCollisionSpringConstraints>(
			ParticleOffset,
			NumParticles,
			TriangleMesh,
			AnimationPositions,
			MoveTemp(DisabledCollisionElements),
			ConfigProperties);

		++NumConstraintInits;
		++NumPostCollisionConstraintRules;

		SelfIntersectionConstraints = MakeShared<Softs::FPBDTriangleMeshIntersections>(
			ParticleOffset,
			NumParticles,
			TriangleMesh);
		++NumConstraintInits;
	}
}

void FClothConstraints::CreateStretchConstraints(
	const Softs::FCollectionPropertyConstFacade& ConfigProperties,
	const TArray<TConstArrayView<FRealSingle>>& WeightMaps,
	const FTriangleMesh& TriangleMesh,
	const FClothingPatternData* PatternData)
{
	if (PatternData && PatternData->PatternPositions.Num() && Softs::FXPBDStretchBiasElementConstraints::IsEnabled(ConfigProperties))
	{
		// TODO: separate warp, weft, Bias multipliers
		const TConstArrayView<FRealSingle>& EdgeStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::EdgeStiffness];
		const TConstArrayView<FRealSingle> DampingMultipliers;  // TODO: Damping multiplier
		const TConstArrayView<FRealSingle> WarpWeftScaleMultipliers; // TODO: separate multipliers for warp and weft scale

		XStretchBiasConstraints = MakeShared<Softs::FXPBDStretchBiasElementConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			TriangleMesh,
			PatternData->WeldedFaceVertexPatternPositions,
			EdgeStiffnessMultipliers,
			EdgeStiffnessMultipliers,
			EdgeStiffnessMultipliers,
			DampingMultipliers,
			WarpWeftScaleMultipliers,
			WarpWeftScaleMultipliers,
			ConfigProperties,
			/*bTrimKinematicConstraints =*/ true);

		++NumConstraintInits;  // Uses init to update the property tables
		++NumConstraintRules;
	}
	else if (Softs::FXPBDEdgeSpringConstraints::IsEnabled(ConfigProperties))
	{
		const TConstArrayView<FRealSingle>& EdgeStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::EdgeStiffness];
		const TConstArrayView<FRealSingle> DampingMultipliers;  // TODO: Damping multiplier

		XEdgeConstraints = MakeShared<Softs::FXPBDEdgeSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			TriangleMesh.GetSurfaceElements(),
			EdgeStiffnessMultipliers,
			DampingMultipliers,
			ConfigProperties,
			/*bTrimKinematicConstraints =*/ true);

		++NumConstraintInits;  // Uses init to update the property tables
		++NumConstraintRules;
	}
	else if (Softs::FPBDEdgeSpringConstraints::IsEnabled(ConfigProperties))
	{
		const TConstArrayView<FRealSingle>& EdgeStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::EdgeStiffness];

		EdgeConstraints = MakeShared<Softs::FPBDEdgeSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			TriangleMesh.GetSurfaceElements(),
			EdgeStiffnessMultipliers,
			ConfigProperties,
			/*bTrimKinematicConstraints =*/ true);

		++NumConstraintInits;  // Uses init to update the property tables
		++NumConstraintRules;
	}
}

void FClothConstraints::CreateBendingConstraints(
	const Softs::FCollectionPropertyConstFacade& ConfigProperties,
	const TArray<TConstArrayView<FRealSingle>>& WeightMaps,
	const FTriangleMesh& TriangleMesh,
	const FClothingPatternData* PatternData)
{
	if (PatternData && PatternData->PatternPositions.Num() && Softs::FXPBDAnisotropicBendingConstraints::IsEnabled(ConfigProperties))
	{
		const TConstArrayView<FRealSingle>& BendingStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::BendingStiffness];
		const TConstArrayView<FRealSingle>& BucklingStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::BucklingStiffness];
		const TConstArrayView<FRealSingle> DampingMultipliers;
		XAnisoBendingElementConstraints = MakeShared<Softs::FXPBDAnisotropicBendingConstraints>(
			Evolution->Particles(),
			ParticleOffset, NumParticles,
			TriangleMesh,
			PatternData->WeldedFaceVertexPatternPositions,
			BendingStiffnessMultipliers,
			BendingStiffnessMultipliers,
			BendingStiffnessMultipliers,
			BucklingStiffnessMultipliers,
			BucklingStiffnessMultipliers,
			BucklingStiffnessMultipliers,
			DampingMultipliers,
			ConfigProperties,
			/*bTrimKinematicConstraints =*/ true);

		++NumConstraintInits;  // Uses init to update the property tables
		++NumConstraintRules;
	}
	else if (Softs::FXPBDBendingConstraints::IsEnabled(ConfigProperties))
	{
		TArray<Chaos::TVec4<int32>> BendingElements = TriangleMesh.GetUniqueAdjacentElements();
		const TConstArrayView<FRealSingle>& BendingStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::BendingStiffness];
		const TConstArrayView<FRealSingle>& BucklingStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::BucklingStiffness];
		const TConstArrayView<FRealSingle> DampingMultipliers;

		XBendingElementConstraints = MakeShared<Softs::FXPBDBendingConstraints>(
			Evolution->Particles(),
			ParticleOffset, NumParticles,
			MoveTemp(BendingElements),
			BendingStiffnessMultipliers,
			BucklingStiffnessMultipliers,
			DampingMultipliers,
			ConfigProperties,
			/*bTrimKinematicConstraints =*/ true);

		++NumConstraintInits;  // Uses init to update the property tables
		++NumConstraintRules;
	}
	else if (Softs::FPBDBendingConstraints::IsEnabled(ConfigProperties))
	{
		TArray<Chaos::TVec4<int32>> BendingElements = TriangleMesh.GetUniqueAdjacentElements();
		const TConstArrayView<FRealSingle>& BendingStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::BendingStiffness];
		const TConstArrayView<FRealSingle>& BucklingStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::BucklingStiffness];

		BendingElementConstraints = MakeShared<Softs::FPBDBendingConstraints>(
			Evolution->Particles(),
			ParticleOffset, NumParticles,
			MoveTemp(BendingElements),
			BendingStiffnessMultipliers,
			BucklingStiffnessMultipliers,
			ConfigProperties,
			/*bTrimKinematicConstraints =*/ true);

		++NumConstraintInits;  // Uses init to update the property tables
		++NumConstraintRules;
	}
	else if (Softs::FXPBDBendingSpringConstraints::IsEnabled(ConfigProperties))
	{
		const TArray<Chaos::TVec2<int32>> CrossEdges = TriangleMesh.GetUniqueAdjacentPoints();
		const TConstArrayView<FRealSingle>& BendingStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::BendingStiffness];
		const TConstArrayView<FRealSingle> DampingMultipliers;

		XBendingConstraints = MakeShared<Softs::FXPBDBendingSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset, NumParticles,
			CrossEdges,
			BendingStiffnessMultipliers,
			DampingMultipliers,
			ConfigProperties,
			/*bTrimKinematicConstraints =*/ true);

		++NumConstraintInits;  // Uses init to update the property tables
		++NumConstraintRules;
	}
	else if (Softs::FPBDBendingSpringConstraints::IsEnabled(ConfigProperties))
	{
		const TArray<Chaos::TVec2<int32>> CrossEdges = TriangleMesh.GetUniqueAdjacentPoints();
		const TConstArrayView<FRealSingle>& BendingStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::BendingStiffness];

		BendingConstraints = MakeShared<Softs::FPBDBendingSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			CrossEdges,
			BendingStiffnessMultipliers,
			ConfigProperties,
			/*bTrimKinematicConstraints =*/ true);

		++NumConstraintInits;  // Uses init to update the property tables
		++NumConstraintRules;
	}
}

void FClothConstraints::CreateAreaConstraints(
	const Softs::FCollectionPropertyConstFacade& ConfigProperties,
	const TArray<TConstArrayView<FRealSingle>>& WeightMaps,
	const FTriangleMesh& TriangleMesh)
{
	if (Softs::FXPBDAreaSpringConstraints::IsEnabled(ConfigProperties))
	{
		const TConstArrayView<FRealSingle>& AreaStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::AreaStiffness];

		XAreaConstraints = MakeShared<Softs::FXPBDAreaSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			TriangleMesh.GetSurfaceElements(),
			AreaStiffnessMultipliers,
			ConfigProperties,
			/*bTrimKinematicConstraints =*/ true);

		++NumConstraintInits;  // Uses init to update the property tables
		++NumConstraintRules;
	}
	else if (Softs::FPBDAreaSpringConstraints::IsEnabled(ConfigProperties))
	{
		const TConstArrayView<FRealSingle>& AreaStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::AreaStiffness];

		AreaConstraints = MakeShared<Softs::FPBDAreaSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			TriangleMesh.GetSurfaceElements(),
			AreaStiffnessMultipliers,
			ConfigProperties,
			/*bTrimKinematicConstraints =*/ true);

		++NumConstraintInits;  // Uses init to update the property tables
		++NumConstraintRules;
	}
}

void FClothConstraints::CreateLongRangeConstraints(
	const Softs::FCollectionPropertyConstFacade& ConfigProperties,
	const TArray<TConstArrayView<FRealSingle>>& WeightMaps,
	const TArray<TConstArrayView<TTuple<int32, int32, FRealSingle>>>& Tethers,
	Softs::FSolverReal MeshScale)
{
	if (Softs::FPBDLongRangeConstraints::IsEnabled(ConfigProperties))
	{
		const TConstArrayView<FRealSingle>& TetherStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::TetherStiffness];
		const TConstArrayView<FRealSingle>& TetherScaleMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::TetherScale];

		//  Now that we're only doing a single iteration of Long range constraints, and they're more of a fake constraint to jump start our initial guess, it's not clear that using XPBD makes sense here.
		LongRangeConstraints = MakeShared<Softs::FPBDLongRangeConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			Tethers,
			TetherStiffnessMultipliers,
			TetherScaleMultipliers,
			ConfigProperties,
			MeshScale);

		++NumConstraintInits;  // Uses init to both update the property tables and apply the constraint
	}
}

void FClothConstraints::CreateMaxDistanceConstraints(
	const Softs::FCollectionPropertyConstFacade& ConfigProperties,
	const TArray<TConstArrayView<FRealSingle>>& WeightMaps,
	Softs::FSolverReal MeshScale)
{
	const TConstArrayView<FRealSingle>& MaxDistances = WeightMaps[(int32)EChaosWeightMapTarget::MaxDistance];
	if (MaxDistances.Num() == NumParticles && Softs::FPBDSphericalConstraint::IsEnabled(ConfigProperties))
	{
		MaximumDistanceConstraints = MakeShared<Softs::FPBDSphericalConstraint>(
			ParticleOffset,
			NumParticles,
			*AnimationPositions,
			MaxDistances,
			ConfigProperties,
			MeshScale);

		++NumConstraintRules;
	}
}

void FClothConstraints::CreateBackstopConstraints(
	const Softs::FCollectionPropertyConstFacade& ConfigProperties,
	const TArray<TConstArrayView<FRealSingle>>& WeightMaps,
	Softs::FSolverReal MeshScale)
{
	if (Softs::FPBDSphericalBackstopConstraint::IsEnabled(ConfigProperties))
	{
		const TConstArrayView<FRealSingle>& BackstopDistances = WeightMaps[(int32)EChaosWeightMapTarget::BackstopDistance];
		const TConstArrayView<FRealSingle>& BackstopRadiuses = WeightMaps[(int32)EChaosWeightMapTarget::BackstopRadius];

		if (BackstopRadiuses.Num() == NumParticles && BackstopDistances.Num() == NumParticles)
		{
			BackstopConstraints = MakeShared<Softs::FPBDSphericalBackstopConstraint>(
				ParticleOffset,
				NumParticles,
				*AnimationPositions,
				*AnimationNormals,
				BackstopRadiuses,
				BackstopDistances,
				ConfigProperties,
				MeshScale);

			++NumConstraintRules;
		}
	}
}

void FClothConstraints::CreateAnimDriveConstraints(const Softs::FCollectionPropertyConstFacade& ConfigProperties, const TArray<TConstArrayView<FRealSingle>>& WeightMaps)
{
	if (Softs::FPBDAnimDriveConstraint::IsEnabled(ConfigProperties))
	{
		const TConstArrayView<FRealSingle>& AnimDriveStiffnessMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::AnimDriveStiffness];
		const TConstArrayView<FRealSingle>& AnimDriveDampingMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::AnimDriveDamping];
		check(AnimationVelocities); // Legacy code didn't use to have AnimationVelocities

		AnimDriveConstraints = MakeShared<Softs::FPBDAnimDriveConstraint>(
			ParticleOffset,
			NumParticles,
			*AnimationPositions,
			*AnimationVelocities,
			AnimDriveStiffnessMultipliers,
			AnimDriveDampingMultipliers,
			ConfigProperties);

		++NumConstraintInits;  // Uses init to update the property tables
		++NumConstraintRules;
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
	check(PostCollisionConstraintRuleOffset == INDEX_NONE);
	if (NumPostCollisionConstraintRules)
	{
		PostCollisionConstraintRuleOffset = Evolution->AddPostCollisionConstraintRuleRange(NumPostCollisionConstraintRules, false);
	}

	TFunction<void(Softs::FSolverParticles&, const Softs::FSolverReal)>* const ConstraintInits = Evolution->ConstraintInits().GetData() + ConstraintInitOffset;
	TFunction<void(Softs::FSolverParticles&, const Softs::FSolverReal)>* const ConstraintRules = Evolution->ConstraintRules().GetData() + ConstraintRuleOffset;
	TFunction<void(Softs::FSolverParticles&, const Softs::FSolverReal)>* const PostCollisionConstraintRules = Evolution->PostCollisionConstraintRules().GetData() + PostCollisionConstraintRuleOffset;

	int32 ConstraintInitIndex = 0;
	int32 ConstraintRuleIndex = 0;
	int32 PostCollisionConstraintRuleIndex = 0;

	if (XStretchBiasConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
		{
			XStretchBiasConstraints->Init();
			XStretchBiasConstraints->ApplyProperties(Dt, Evolution->GetIterations());
		};

		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			XStretchBiasConstraints->Apply(Particles, Dt);
		};
	}
	if (XEdgeConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
			{
				XEdgeConstraints->Init();
				XEdgeConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};

		ConstraintRules[ConstraintRuleIndex++] = 
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				XEdgeConstraints->Apply(Particles, Dt);
			};
	}
	if (XEdgeConstraints_Deprecated)  // TODO: Remove for 5.4
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
		{
			XEdgeConstraints_Deprecated->Init();
			XEdgeConstraints_Deprecated->ApplyProperties(Dt, Evolution->GetIterations());
		};

		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			XEdgeConstraints_Deprecated->Apply(Particles, Dt);
		};
	}
	if (EdgeConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
			{
				EdgeConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				EdgeConstraints->Apply(Particles, Dt);
			};
	}
	if (EdgeConstraints_Deprecated)  // TODO: Remove for 5.4
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
		{
			EdgeConstraints_Deprecated->ApplyProperties(Dt, Evolution->GetIterations());
		};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			EdgeConstraints_Deprecated->Apply(Particles, Dt);
		};
	}
	if (XBendingConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
			{
				XBendingConstraints->Init();
				XBendingConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				XBendingConstraints->Apply(Particles, Dt);
			};
	}
	if (XBendingConstraints_Deprecated)  // TODO: Remove for 5.4
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
		{
			XBendingConstraints_Deprecated->Init();
			XBendingConstraints_Deprecated->ApplyProperties(Dt, Evolution->GetIterations());
		};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			XBendingConstraints_Deprecated->Apply(Particles, Dt);
		};
	}
	if (BendingConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
			{
				BendingConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				BendingConstraints->Apply(Particles, Dt);
			};
	}
	if (BendingConstraints_Deprecated)  // TODO: Remove for 5.4
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
		{
			BendingConstraints_Deprecated->ApplyProperties(Dt, Evolution->GetIterations());
		};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			BendingConstraints_Deprecated->Apply(Particles, Dt);
		};
	}
	if (BendingElementConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				BendingElementConstraints->Init(Particles);
				BendingElementConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				BendingElementConstraints->Apply(Particles, Dt);
			};
	}
	if (XBendingElementConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			XBendingElementConstraints->Init(Particles);
			XBendingElementConstraints->ApplyProperties(Dt, Evolution->GetIterations());
		};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			XBendingElementConstraints->Apply(Particles, Dt);
		};
	}
	if (XAnisoBendingElementConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			XAnisoBendingElementConstraints->Init(Particles);
			XAnisoBendingElementConstraints->ApplyProperties(Dt, Evolution->GetIterations());
		};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			XAnisoBendingElementConstraints->Apply(Particles, Dt);
		};
	}
	if (XAreaConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
			{
				XAreaConstraints->Init();
				XAreaConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				XAreaConstraints->Apply(Particles, Dt);
			};
	}
	if (XAreaConstraints_Deprecated)  // TODO: Remove for 5.4
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
		{
			XAreaConstraints_Deprecated->Init();
			XAreaConstraints_Deprecated->ApplyProperties(Dt, Evolution->GetIterations());
		};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			XAreaConstraints_Deprecated->Apply(Particles, Dt);
		};
	}
	if (AreaConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
			{
				AreaConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				AreaConstraints->Apply(Particles, Dt);
			};
	}
	if (AreaConstraints_Deprecated)  // TODO: Remove for 5.4
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
		{
			AreaConstraints_Deprecated->ApplyProperties(Dt, Evolution->GetIterations());
		};
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			AreaConstraints_Deprecated->Apply(Particles, Dt);
		};
	}
	if (ThinShellVolumeConstraints_Deprecated)  // TODO: Remove for 5.4
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			ThinShellVolumeConstraints_Deprecated->Apply(Particles, Dt);
		};
	}
	if (VolumeConstraints_Deprecated)  // TODO: Remove for 5.4
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
		{
			VolumeConstraints_Deprecated->Apply(Particles, Dt);
		};
	}
	if (MaximumDistanceConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				MaximumDistanceConstraints->Apply(Particles, Dt);
			};
	}
	if (BackstopConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				BackstopConstraints->Apply(Particles, Dt);
			};
	}
	if (AnimDriveConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& /*Particles*/, const Softs::FSolverReal Dt)
			{
				AnimDriveConstraints->ApplyProperties(Dt, Evolution->GetIterations());
			};

		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				AnimDriveConstraints->Apply(Particles, Dt);
			};
	}
	if (ShapeConstraints)
	{
		ConstraintRules[ConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				ShapeConstraints->Apply(Particles, Dt);
			};
	}

	if (SelfCollisionInit && SelfCollisionConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal /*Dt*/)
			{
				SelfCollisionInit->Init(Particles);
				SelfCollisionConstraints->Init(Particles, SelfCollisionInit->GetSpatialHash(), SelfCollisionInit->GetVertexGIAColors(), SelfCollisionInit->GetTriangleGIAColors());
			};

		PostCollisionConstraintRules[PostCollisionConstraintRuleIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				SelfCollisionConstraints->Apply(Particles, Dt);
			};
	}

	// The following constraints only run once per subframe, so we do their Apply as part of the Init() which modifies P
	// To avoid possible dependency order issues, add them last
	if (SelfCollisionInit && SelfIntersectionConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{
				SelfIntersectionConstraints->Apply(Particles, SelfCollisionInit->GetContourMinimizationIntersections(), Dt);
			};
	}

	// Long range constraints modify particle P as part of Init. To avoid possible dependency order issues,
	// add them last
	if (LongRangeConstraints)
	{
		ConstraintInits[ConstraintInitIndex++] =
			[this](Softs::FSolverParticles& Particles, const Softs::FSolverReal Dt)
			{	
				// Only doing one iteration.
				constexpr int32 NumLRAIterations = 1;
				LongRangeConstraints->ApplyProperties(Dt, NumLRAIterations);
				LongRangeConstraints->Apply(Particles, Dt);  // Run the LRA constraint only once per timestep
			};
	}
	check(ConstraintInitIndex == NumConstraintInits);
	check(ConstraintRuleIndex == NumConstraintRules);
	check(PostCollisionConstraintRuleIndex == NumPostCollisionConstraintRules);
}

void FClothConstraints::SetEdgeConstraints(const TArray<TVec3<int32>>& SurfaceElements, const TConstArrayView<FRealSingle>& StiffnessMultipliers, bool bUseXPBDConstraints)
{
	check(Evolution);

	if (bUseXPBDConstraints)
	{
		const TConstArrayView<FRealSingle> DampingMultipliers;

		XEdgeConstraints_Deprecated = MakeShared<Softs::FXPBDSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset, NumParticles,
			SurfaceElements,
			StiffnessMultipliers,
			DampingMultipliers,
			/*InStiffness =*/ Softs::FSolverVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	else
	{
		EdgeConstraints_Deprecated = MakeShared<Softs::FPBDSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			SurfaceElements,
			StiffnessMultipliers,
			/*InStiffness =*/ Softs::FSolverVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetXPBDEdgeConstraints(const TArray<TVec3<int32>>& SurfaceElements, const TConstArrayView<FRealSingle>& StiffnessMultipliers, const TConstArrayView<FRealSingle>& DampingRatioMultipliers)
{
	check(Evolution);
	XEdgeConstraints_Deprecated = MakeShared<Softs::FXPBDSpringConstraints>(
		Evolution->Particles(),
		ParticleOffset, NumParticles,
		SurfaceElements,
		StiffnessMultipliers,
		DampingRatioMultipliers,
		/*InStiffness =*/ Softs::FSolverVec2::UnitVector,
		/*InDampingRatio =*/ Softs::FSolverVec2::ZeroVector,
		/*bTrimKinematicConstraints =*/ true);
	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetBendingConstraints(const TArray<TVec2<int32>>& Edges, const TConstArrayView<FRealSingle>& StiffnessMultipliers, bool bUseXPBDConstraints)
{
	check(Evolution);

	if (bUseXPBDConstraints)
	{
		const TConstArrayView<FRealSingle> DampingMultipliers;

		XBendingConstraints_Deprecated = MakeShared<Softs::FXPBDSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset, NumParticles,
			Edges,
			StiffnessMultipliers,
			DampingMultipliers,
			/*InStiffness =*/ Softs::FSolverVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	else
	{
		BendingConstraints_Deprecated = MakeShared<Softs::FPBDSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			Edges,
			StiffnessMultipliers,
			/*InStiffness =*/ Softs::FSolverVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetBendingConstraints(TArray<TVec4<int32>>&& BendingElements, const TConstArrayView<FRealSingle>& StiffnessMultipliers, const TConstArrayView<FRealSingle>& BucklingStiffnessMultipliers, bool bUseXPBDConstraints)
{
	check(Evolution);

	if (bUseXPBDConstraints)
	{
		const TConstArrayView<FRealSingle> DampingMultipliers;

		XBendingElementConstraints = MakeShared<Softs::FXPBDBendingConstraints>(
			Evolution->Particles(),
			ParticleOffset, NumParticles,
			MoveTemp(BendingElements),
			StiffnessMultipliers,
			BucklingStiffnessMultipliers,
			DampingMultipliers,
			/*InStiffness =*/ Softs::FSolverVec2::UnitVector,
			/*InBucklingRatio=*/ (Softs::FSolverReal)0.f,
			/*InBucklingStiffness =*/ Softs::FSolverVec2::UnitVector,
			/*InDampingRatio =*/ Softs::FSolverVec2::ZeroVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	else
	{
		BendingElementConstraints = MakeShared<Softs::FPBDBendingConstraints>(
			Evolution->Particles(),
			ParticleOffset, NumParticles,
			MoveTemp(BendingElements),
			StiffnessMultipliers,
			BucklingStiffnessMultipliers,
			/*InStiffness =*/ Softs::FSolverVec2::UnitVector,
			/*InBucklingRatio=*/ (Softs::FSolverReal)0.f,
			/*InBucklingStiffness =*/ Softs::FSolverVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);

	}
	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetXPBDBendingConstraints(TArray<TVec4<int32>>&& BendingElements, const TConstArrayView<FRealSingle>& StiffnessMultipliers, const TConstArrayView<FRealSingle>& BucklingStiffnessMultipliers, const TConstArrayView<FRealSingle>& DampingRatioMultipliers)
{
	check(Evolution);
	XBendingElementConstraints = MakeShared<Softs::FXPBDBendingConstraints>(
		Evolution->Particles(),
		ParticleOffset, NumParticles,
		MoveTemp(BendingElements),
		StiffnessMultipliers,
		BucklingStiffnessMultipliers,
		DampingRatioMultipliers,
		/*InStiffness =*/ Softs::FSolverVec2::UnitVector,
		/*InBucklingRatio=*/ (Softs::FSolverReal)0.f,
		/*InBucklingStiffness =*/ Softs::FSolverVec2::UnitVector,
		/*InDampingRatio =*/ Softs::FSolverVec2::ZeroVector,
		/*bTrimKinematicConstraints =*/ true);
	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetBendingConstraints(TArray<TVec4<int32>>&& BendingElements, Softs::FSolverReal BendingStiffness)
{
	// Deprecated 5.1
	check(Evolution);

	BendingElementConstraints = MakeShared<Softs::FPBDBendingConstraints>(
		Evolution->Particles(),
		ParticleOffset,
		NumParticles,
		MoveTemp(BendingElements),
		/*StiffnessMultipliers =*/ TConstArrayView<FRealSingle>(),
		/*BucklingStiffnessMultipliers =*/ TConstArrayView<FRealSingle>(),
		/*InStiffness =*/ BendingStiffness,
		/*InBucklingRatio=*/ (Softs::FSolverReal)0.f,
		/*InBucklingStiffness =*/ Softs::FSolverVec2::UnitVector,
		/*bTrimKinematicConstraints =*/ true);

	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetAreaConstraints(const TArray<TVec3<int32>>& SurfaceElements, const TConstArrayView<FRealSingle>& StiffnessMultipliers, bool bUseXPBDConstraints)
{
	check(Evolution);

	if (bUseXPBDConstraints)
	{
		XAreaConstraints_Deprecated = MakeShared<Softs::FXPBDAxialSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			SurfaceElements,
			StiffnessMultipliers,
			/*InStiffness =*/ Softs::FSolverVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	else
	{
		AreaConstraints_Deprecated = MakeShared<Softs::FPBDAxialSpringConstraints>(
			Evolution->Particles(),
			ParticleOffset,
			NumParticles,
			SurfaceElements,
			StiffnessMultipliers,
			/*InStiffness =*/ Softs::FSolverVec2::UnitVector,
			/*bTrimKinematicConstraints =*/ true);
	}
	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetVolumeConstraints(const TArray<TVec2<int32>>& DoubleBendingEdges, Softs::FSolverReal VolumeStiffness)
{
	check(Evolution);

	ThinShellVolumeConstraints_Deprecated = MakeShared<Softs::FPBDSpringConstraints>(
		Evolution->Particles(),
		ParticleOffset,
		NumParticles,
		DoubleBendingEdges,
		TConstArrayView<FRealSingle>(),
		VolumeStiffness,
		/*bTrimKinematicConstraints =*/ true);
	++NumConstraintRules;
}

void FClothConstraints::SetVolumeConstraints(TArray<TVec3<int32>>&& SurfaceElements, Softs::FSolverReal VolumeStiffness)
{
	check(Evolution);
	check(VolumeStiffness > 0.f && VolumeStiffness <= 1.f);

	VolumeConstraints_Deprecated = MakeShared<Softs::FPBDVolumeConstraint>(Evolution->Particles(), MoveTemp(SurfaceElements), VolumeStiffness);
	++NumConstraintRules;
}

void FClothConstraints::SetLongRangeConstraints(
	const TArray<TConstArrayView<TTuple<int32, int32, FRealSingle>>>& Tethers,
	const TConstArrayView<FRealSingle>& TetherStiffnessMultipliers,
	const TConstArrayView<FRealSingle>& TetherScaleMultipliers,
	const Softs::FSolverVec2& TetherScale,
	Softs::FSolverReal MeshScale)
{
	check(Evolution);
	//  Now that we're only doing a single iteration of Long range constraints, and they're more of a fake constraint to jump start our initial guess, it's not clear that using XPBD makes sense here.
	LongRangeConstraints = MakeShared<Softs::FPBDLongRangeConstraints>(
		Evolution->Particles(),
		ParticleOffset,
		NumParticles,
		Tethers,
		TetherStiffnessMultipliers,
		TetherScaleMultipliers,
		/*InStiffness =*/ Softs::FSolverVec2::UnitVector,
		TetherScale,
		MeshScale);
	++NumConstraintInits;  // Uses init to both update the property tables and apply the constraint
}

// Deprecated in 5.1
void FClothConstraints::SetLongRangeConstraints(
	const TArray<TConstArrayView<TTuple<int32,
	int32, FRealSingle>>>& Tethers,
	const TConstArrayView<FRealSingle>& TetherStiffnessMultipliers,
	const TConstArrayView<FRealSingle>& TetherScaleMultipliers,
	const Softs::FSolverVec2& TetherScale,
	bool bUseXPBDConstraints,
	Softs::FSolverReal MeshScale)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return SetLongRangeConstraints(Tethers, TetherStiffnessMultipliers, TetherScaleMultipliers, TetherScale, MeshScale);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FClothConstraints::SetMaximumDistanceConstraints(const TConstArrayView<FRealSingle>& MaxDistances)
{
	MaximumDistanceConstraints = MakeShared<Softs::FPBDSphericalConstraint>(
		ParticleOffset,
		NumParticles,
		*AnimationPositions,
		MaxDistances);
	++NumConstraintRules;
}

void FClothConstraints::SetBackstopConstraints(const TConstArrayView<FRealSingle>& BackstopDistances, const TConstArrayView<FRealSingle>& BackstopRadiuses, bool bUseLegacyBackstop)
{
	BackstopConstraints = MakeShared<Softs::FPBDSphericalBackstopConstraint>(
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
	if (AnimationVelocities)
	{
		AnimDriveConstraints = MakeShared<Softs::FPBDAnimDriveConstraint>(
			ParticleOffset,
			NumParticles,
			*AnimationPositions,
			TArray<Softs::FSolverVec3>(),
			*AnimationVelocities,
			AnimDriveStiffnessMultipliers,
			AnimDriveDampingMultipliers);
	}
	else
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		// Deprecated behavior until old Initialize can be removed
		AnimDriveConstraints = MakeShareable( new Softs::FPBDAnimDriveConstraint(
			ParticleOffset,
			NumParticles,
			*AnimationPositions,
			*OldAnimationPositions_Deprecated,
			AnimDriveStiffnessMultipliers,
			AnimDriveDampingMultipliers));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	++NumConstraintInits;  // Uses init to update the property tables
	++NumConstraintRules;
}

void FClothConstraints::SetShapeTargetConstraints(Softs::FSolverReal ShapeTargetStiffness)
{
	// TODO: Review this constraint. Currently does nothing more than the anim drive with less controls
	check(ShapeTargetStiffness > 0.f && ShapeTargetStiffness <= 1.f);

	ShapeConstraints = MakeShared<Softs::FPBDShapeConstraints>(
		ParticleOffset,
		NumParticles,
		*AnimationPositions,
		*AnimationPositions,
		ShapeTargetStiffness);
	++NumConstraintRules;
}

void FClothConstraints::SetSelfCollisionConstraints(const FTriangleMesh& TriangleMesh, TSet<TVec2<int32>>&& DisabledCollisionElements, Softs::FSolverReal SelfCollisionThickness, Softs::FSolverReal SelfCollisionFrictionCoefficient, bool bGlobalIntersectionAnalysis, bool bContourMinimization)
{
	SelfCollisionInit = MakeShared<Softs::FPBDTriangleMeshCollisions>(
		ParticleOffset,
		NumParticles,
		TriangleMesh,
		bGlobalIntersectionAnalysis,
		bContourMinimization);

	SelfCollisionConstraints = MakeShared<Softs::FPBDCollisionSpringConstraints>(
		ParticleOffset,
		NumParticles,
		TriangleMesh,
		AnimationPositions,
		MoveTemp(DisabledCollisionElements),
		SelfCollisionThickness,
		Softs::FPBDCollisionSpringConstraintsBase::BackCompatStiffness,
		SelfCollisionFrictionCoefficient);

	++NumConstraintInits;
	++NumPostCollisionConstraintRules;

	SelfIntersectionConstraints = MakeShared<Softs::FPBDTriangleMeshIntersections>(
		ParticleOffset,
		NumParticles,
		TriangleMesh);
	++NumConstraintInits;
}

void FClothConstraints::Update(
	const Softs::FCollectionPropertyConstFacade& ConfigProperties,
	Softs::FSolverReal MeshScale,
	Softs::FSolverReal MaxDistancesScale)
{
	if (XEdgeConstraints)
	{
		XEdgeConstraints->SetProperties(ConfigProperties);
	}
	else if (EdgeConstraints)
	{
		EdgeConstraints->SetProperties(ConfigProperties);
	}
	if (XBendingConstraints)
	{
		XBendingConstraints->SetProperties(ConfigProperties);
	}
	else if (BendingConstraints)
	{
		BendingConstraints->SetProperties(ConfigProperties);
	}
	if (XBendingElementConstraints)
	{
		XBendingElementConstraints->SetProperties(ConfigProperties);
	}
	else if (BendingElementConstraints)
	{
		BendingElementConstraints->SetProperties(ConfigProperties);
	}
	if (XAreaConstraints)
	{
		XAreaConstraints->SetProperties(ConfigProperties);
	}
	else if (AreaConstraints)
	{
		AreaConstraints->SetProperties(ConfigProperties);
	}
	if (LongRangeConstraints)
	{
		LongRangeConstraints->SetProperties(ConfigProperties, MeshScale);
	}
	if (MaximumDistanceConstraints)
	{
		MaximumDistanceConstraints->SetProperties(ConfigProperties, MeshScale * MaxDistancesScale);
	}
	if (AnimDriveConstraints)
	{
		AnimDriveConstraints->SetProperties(ConfigProperties);
	}
	if (SelfCollisionConstraints)
	{
		SelfCollisionConstraints->SetProperties(ConfigProperties);
	}
	if (BackstopConstraints)
	{
		BackstopConstraints->SetProperties(ConfigProperties, MeshScale);
	}
}

void FClothConstraints::SetEdgeProperties(const Softs::FSolverVec2& EdgeStiffness, const Softs::FSolverVec2& DampingRatio)
{
	if (EdgeConstraints)
	{
		static_cast<Softs::FPBDSpringConstraints*>(EdgeConstraints.Get())->SetProperties(EdgeStiffness);
	}
	if (XEdgeConstraints)
	{
		static_cast<Softs::FXPBDSpringConstraints*>(XEdgeConstraints.Get())->SetProperties(EdgeStiffness, DampingRatio);
	}
}

void FClothConstraints::SetBendingProperties(const Softs::FSolverVec2& BendingStiffness, Softs::FSolverReal BucklingRatio, const Softs::FSolverVec2& BucklingStiffness, const Softs::FSolverVec2& BendingDampingRatio)
{
	if (BendingConstraints)
	{
		static_cast<Softs::FPBDSpringConstraints*>(BendingConstraints.Get())->SetProperties(BendingStiffness);
	}
	if (XBendingConstraints)
	{
		static_cast<Softs::FXPBDSpringConstraints*>(XBendingConstraints.Get())->SetProperties(BendingStiffness);
	}
	if (BendingElementConstraints)
	{
		BendingElementConstraints->SetProperties(BendingStiffness, BucklingRatio, BucklingStiffness);
	}
	if (XBendingElementConstraints)
	{
		XBendingElementConstraints->SetProperties(BendingStiffness, BucklingRatio, BucklingStiffness, BendingDampingRatio);
	}
}

void FClothConstraints::SetAreaProperties(const Softs::FSolverVec2& AreaStiffness)
{
	if (AreaConstraints)
	{
		static_cast<Softs::FPBDAxialSpringConstraints*>(AreaConstraints.Get())->SetProperties(AreaStiffness);
	}
	if (XAreaConstraints)
	{
		static_cast<Softs::FXPBDAxialSpringConstraints*>(XAreaConstraints.Get())->SetProperties(AreaStiffness);
	}
}

void FClothConstraints::SetThinShellVolumeProperties(Softs::FSolverReal VolumeStiffness)
{
	if (ThinShellVolumeConstraints_Deprecated)
	{
		ThinShellVolumeConstraints_Deprecated->SetProperties(VolumeStiffness);
	}
}

void FClothConstraints::SetVolumeProperties(Softs::FSolverReal VolumeStiffness)
{
	if (VolumeConstraints_Deprecated)
	{
		VolumeConstraints_Deprecated->SetStiffness(VolumeStiffness);
	}
}

void FClothConstraints::SetLongRangeAttachmentProperties(
	const Softs::FSolverVec2& TetherStiffness,
	const Softs::FSolverVec2& TetherScale,
	Softs::FSolverReal MeshScale)
{
	if (LongRangeConstraints)
	{
		LongRangeConstraints->SetProperties(TetherStiffness, TetherScale, MeshScale);
	}
}

void FClothConstraints::SetMaximumDistanceProperties(Softs::FSolverReal MeshScale)
{
	if (MaximumDistanceConstraints)
	{
		constexpr Softs::FSolverReal MaxDistancesMultiplier = (Softs::FSolverReal)1.;
		MaximumDistanceConstraints->SetScale(MaxDistancesMultiplier, MeshScale);
	}
}

void FClothConstraints::SetAnimDriveProperties(const Softs::FSolverVec2& AnimDriveStiffness, const Softs::FSolverVec2& AnimDriveDamping)
{
	if (AnimDriveConstraints)
	{
		AnimDriveConstraints->SetProperties(AnimDriveStiffness, AnimDriveDamping);
	}
}

void FClothConstraints::SetSelfCollisionProperties(Softs::FSolverReal SelfCollisionThickness, Softs::FSolverReal SelfCollisionFrictionCoefficient, bool bGlobalIntersectionAnalysis, bool bContourMinimization)
{
	if (SelfCollisionInit)
	{
		SelfCollisionInit->SetGlobalIntersectionAnalysis(bGlobalIntersectionAnalysis);
		SelfCollisionInit->SetContourMinimization(bContourMinimization);
	}
	if (SelfCollisionConstraints)
	{
		SelfCollisionConstraints->SetThickness(SelfCollisionThickness);
		SelfCollisionConstraints->SetFrictionCoefficient(SelfCollisionFrictionCoefficient);
	}
}

void FClothConstraints::SetBackstopProperties(bool bEnabled, Softs::FSolverReal MeshScale)
{
	if (BackstopConstraints)
	{
		BackstopConstraints->SetEnabled(bEnabled);
		constexpr Softs::FSolverReal BackstopDistancesMultiplier = (Softs::FSolverReal)1.;
		BackstopConstraints->SetScale(BackstopDistancesMultiplier, MeshScale);
	}
}

}  // End namespace Chaos
