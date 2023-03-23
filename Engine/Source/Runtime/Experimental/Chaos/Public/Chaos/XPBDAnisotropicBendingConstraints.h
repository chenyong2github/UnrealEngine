// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDBendingConstraintsBase.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "ChaosStats.h"

namespace Chaos
{
class FTriangleMesh;
}

namespace Chaos::Softs
{

class CHAOS_API FXPBDAnisotropicBendingConstraints final : public FPBDBendingConstraintsBase
{
	typedef FPBDBendingConstraintsBase Base;

public:
	// Stiffness is in kg cm^2 / rad^2 s^2
	static constexpr FSolverReal MinStiffness = (FSolverReal)1e-4; // Stiffness below this will be considered 0 since all of our calculations are actually based on 1 / stiffness.
	static constexpr FSolverReal MaxStiffness = (FSolverReal)1e7;
	static constexpr FSolverReal MinDamping = (FSolverReal)0.;
	static constexpr FSolverReal MaxDamping = (FSolverReal)1000.;

	static bool IsEnabled(const FCollectionPropertyConstFacade& PropertyCollection)
	{
		return IsXPBDBendingElementStiffnessWarpEnabled(PropertyCollection, false);
	}

	FXPBDAnisotropicBendingConstraints(const FSolverParticles& InParticles,
		int32 ParticleOffset,
		int32 ParticleCount,
		const FTriangleMesh& TriangleMesh,
		const TArray<TVec3<FVec2f>>& FaceVertexPatternPositions,
		const TConstArrayView<FRealSingle>& StiffnessWarpMultipliers,
		const TConstArrayView<FRealSingle>& StiffnessWeftMultipliers,
		const TConstArrayView<FRealSingle>& StiffnessBiasMultipliers,
		const TConstArrayView<FRealSingle>& BucklingStiffnessWarpMultipliers,
		const TConstArrayView<FRealSingle>& BucklingStiffnessWeftMultipliers,
		const TConstArrayView<FRealSingle>& BucklingStiffnessBiasMultipliers,
		const TConstArrayView<FRealSingle>& DampingMultipliers,
		const FCollectionPropertyConstFacade& PropertyCollection,
		bool bTrimKinematicConstraints = false);

	FXPBDAnisotropicBendingConstraints(const FSolverParticles& InParticles,
		int32 ParticleOffset,
		int32 ParticleCount,
		const FTriangleMesh& TriangleMesh,
		const TArray<TVec3<FVec2f>>& FaceVertexPatternPositions,
		const TConstArrayView<FRealSingle>& StiffnessWarpMultipliers,
		const TConstArrayView<FRealSingle>& StiffnessWeftMultipliers,
		const TConstArrayView<FRealSingle>& StiffnessBiasMultipliers,
		const TConstArrayView<FRealSingle>& BucklingStiffnessWarpMultipliers,
		const TConstArrayView<FRealSingle>& BucklingStiffnessWeftMultipliers,
		const TConstArrayView<FRealSingle>& BucklingStiffnessBiasMultipliers,
		const TConstArrayView<FRealSingle>& DampingMultipliers,
		const FSolverVec2& InStiffnessWarp,
		const FSolverVec2& InStiffnessWeft,
		const FSolverVec2& InStiffnessBias,
		const FSolverReal InBucklingRatio,
		const FSolverVec2& InBucklingStiffnessWarp,
		const FSolverVec2& InBucklingStiffnessWeft,
		const FSolverVec2& InBucklingStiffnessBias,
		const FSolverVec2& InDampingRatio,
		bool bTrimKinematicConstraints = false);

	virtual ~FXPBDAnisotropicBendingConstraints() override {}

	void Init(const FSolverParticles& InParticles)
	{ 
		Lambdas.Reset();
		Lambdas.AddZeroed(Constraints.Num());
		FPBDBendingConstraintsBase::Init(InParticles);
	}

	void SetProperties(const FCollectionPropertyConstFacade& PropertyCollection)
	{
		if (IsXPBDBendingElementStiffnessWarpMutable(PropertyCollection))
		{
			Stiffness.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDBendingElementStiffnessWarp(PropertyCollection)), MaxStiffness);
		}
		if (IsXPBDBendingElementStiffnessWeftMutable(PropertyCollection))
		{
			Stiffness.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDBendingElementStiffnessWeft(PropertyCollection)), MaxStiffness);
		}
		if (IsXPBDBendingElementStiffnessBiasMutable(PropertyCollection))
		{
			Stiffness.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDBendingElementStiffnessBias(PropertyCollection)), MaxStiffness);
		}
		if (IsXPBDBucklingRatioMutable(PropertyCollection))
		{
			BucklingRatio = FMath::Clamp(GetXPBDBucklingRatio(PropertyCollection), (FSolverReal)0., (FSolverReal)1.);
		}
		if (IsXPBDBucklingStiffnessWarpMutable(PropertyCollection))
		{
			BucklingStiffness.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDBucklingStiffnessWarp(PropertyCollection)), MaxStiffness);
		}
		if (IsXPBDBucklingStiffnessWeftMutable(PropertyCollection))
		{
			BucklingStiffness.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDBucklingStiffnessWeft(PropertyCollection)), MaxStiffness);
		}
		if (IsXPBDBucklingStiffnessBiasMutable(PropertyCollection))
		{
			BucklingStiffness.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDBucklingStiffnessBias(PropertyCollection)), MaxStiffness);
		}
		if (IsXPBDBendingElementDampingMutable(PropertyCollection))
		{
			DampingRatio.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDBendingElementDamping(PropertyCollection)).ClampAxes(MinDamping, MaxDamping));
		}
	}

	// Update stiffness table, as well as the simulation stiffness exponent
	void ApplyProperties(const FSolverReal /*Dt*/, const int32 /*NumIterations*/)
	{
		Stiffness.ApplyXPBDValues(MaxStiffness);
		StiffnessWeft.ApplyXPBDValues(MaxStiffness);
		StiffnessBias.ApplyXPBDValues(MaxStiffness);
		BucklingStiffness.ApplyXPBDValues(MaxStiffness);
		BucklingStiffnessWeft.ApplyXPBDValues(MaxStiffness);
		BucklingStiffnessBias.ApplyXPBDValues(MaxStiffness);
		DampingRatio.ApplyValues();
	}

	void Apply(FSolverParticles& Particles, const FSolverReal Dt) const;

private:
	void InitColor(const FSolverParticles& InParticles, const int32 ParticleOffset, const int32 ParticleCount);
	void ApplyHelper(FSolverParticles& Particles, const FSolverReal Dt, const int32 ConstraintIndex, const FSolverVec3& ExpStiffnessValues, 
		const FSolverVec3& ExpBucklingStiffnessValues, const FSolverReal DampingRatioValue) const;

	TArray<FSolverVec3> GenerateWarpWeftBiasBaseMultipliers(const TArray<TVec3<FVec2f>>& FaceVertexPatternPositions, const FTriangleMesh& TriangleMesh) const;

	using Base::Constraints;
	using Base::RestAngles;
	using Base::Stiffness; // Warp
	using Base::BucklingStiffness; // Warp
	
	FPBDStiffness StiffnessWeft;
	FPBDStiffness StiffnessBias;
	FPBDStiffness BucklingStiffnessWeft;
	FPBDStiffness BucklingStiffnessBias;

	FPBDWeightMap DampingRatio;
	mutable TArray<FSolverReal> Lambdas;
	TArray<int32> ConstraintsPerColorStartIndex; // Constraints are ordered so each batch is contiguous. This is ColorNum + 1 length so it can be used as start and end.

	TArray<FSolverVec3> WarpWeftBiasBaseMultipliers;

	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDBendingElementStiffnessWarp, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDBendingElementStiffnessWeft, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDBendingElementStiffnessBias, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDBendingElementDamping, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDBucklingRatio, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDBucklingStiffnessWarp, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDBucklingStiffnessWeft, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDBucklingStiffnessBias, float);
};

}  // End namespace Chaos::Softs
