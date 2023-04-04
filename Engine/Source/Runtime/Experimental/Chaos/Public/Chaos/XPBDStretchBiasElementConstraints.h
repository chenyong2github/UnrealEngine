// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/CollectionPropertyFacade.h"
#include "Chaos/PBDSoftsEvolutionFwd.h"
#include "Chaos/PBDSoftsSolverParticles.h"
#include "Chaos/PBDStiffness.h"
#include "Chaos/ParticleRule.h"
#include "ChaosStats.h"

namespace Chaos
{
class FTriangleMesh;
}

namespace Chaos::Softs
{

class CHAOS_API FXPBDStretchBiasElementConstraints
{
public:
	// Stiffness is in kg cm / s^2 for stretch, kg cm^2 / s^2 for Bias
	static constexpr FSolverReal MinStiffness = (FSolverReal)1e-4; // Stiffness below this will be considered 0 since all of our calculations are actually based on 1 / stiffness.
	static constexpr FSolverReal MaxStiffness = (FSolverReal)1e7;
	static constexpr FSolverReal MinDamping = (FSolverReal)0.;
	static constexpr FSolverReal MaxDamping = (FSolverReal)1000.;
	static constexpr bool bDefaultUse3dRestLengths = true;
	static constexpr FSolverReal MinWarpWeftScale = (FSolverReal)0.; 
	static constexpr FSolverReal MaxWarpWeftScale = (FSolverReal)1e7; // No particular reason for this number. Just can't imagine wanting something bigger?
	static constexpr FSolverReal DefaultWarpWeftScale = (FSolverReal)1.;

	static bool IsEnabled(const FCollectionPropertyConstFacade& PropertyCollection)
	{
		return IsXPBDStretchBiasElementStiffnessWarpEnabled(PropertyCollection, false);
	}

	FXPBDStretchBiasElementConstraints(const FSolverParticles& InParticles,
		int32 ParticleOffset,
		int32 ParticleCount,
		const FTriangleMesh& TriangleMesh,
		const TArray<TVec3<FVec2f>>& FaceVertexUVs,
		const TConstArrayView<FRealSingle>& StiffnessWarpMultipliers,
		const TConstArrayView<FRealSingle>& StiffnessWeftMultipliers,
		const TConstArrayView<FRealSingle>& StiffnessBiasMultipliers,
		const TConstArrayView<FRealSingle>& DampingMultipliers,
		const TConstArrayView<FRealSingle>& WarpScaleMultipliers,
		const TConstArrayView<FRealSingle>& WeftScaleMultipliers,
		const FCollectionPropertyConstFacade& PropertyCollection,
		bool bTrimKinematicConstraints = false);

	FXPBDStretchBiasElementConstraints(const FSolverParticles& InParticles,
		int32 ParticleOffset,
		int32 ParticleCount,
		const FTriangleMesh& TriangleMesh,
		const TArray<TVec3<FVec2f>>& FaceVertexUVs,
		const TConstArrayView<FRealSingle>& StiffnessWarpMultipliers,
		const TConstArrayView<FRealSingle>& StiffnessWeftMultipliers,
		const TConstArrayView<FRealSingle>& StiffnessBiasMultipliers,
		const TConstArrayView<FRealSingle>& DampingMultipliers,
		const TConstArrayView<FRealSingle>& WarpScaleMultipliers,
		const TConstArrayView<FRealSingle>& WeftScaleMultipliers,
		const FSolverVec2& InStiffnessWarp,
		const FSolverVec2& InStiffnessWeft,
		const FSolverVec2& InStiffnessBias,
		const FSolverVec2& InDampingRatio,
		const FSolverVec2& InWarpScale,
		const FSolverVec2& InWeftScale,
		bool bUse3dRestLengths,
		bool bTrimKinematicConstraints = false);

	virtual ~FXPBDStretchBiasElementConstraints() {}

	void Init()
	{ 
		Lambdas.Reset();
		Lambdas.AddZeroed(Constraints.Num());
	}

	void SetProperties(const FCollectionPropertyConstFacade& PropertyCollection)
	{
		if (IsXPBDStretchBiasElementStiffnessWarpMutable(PropertyCollection))
		{
			StiffnessWarp.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDStretchBiasElementStiffnessWarp(PropertyCollection)), MaxStiffness);
		}
		if (IsXPBDStretchBiasElementStiffnessWeftMutable(PropertyCollection))
		{
			StiffnessWeft.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDStretchBiasElementStiffnessWeft(PropertyCollection)), MaxStiffness);
		}
		if (IsXPBDStretchBiasElementStiffnessBiasMutable(PropertyCollection))
		{
			StiffnessBias.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDStretchBiasElementStiffnessBias(PropertyCollection)), MaxStiffness);
		}
		if (IsXPBDStretchBiasElementDampingMutable(PropertyCollection))
		{
			DampingRatio.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDStretchBiasElementDamping(PropertyCollection)).ClampAxes(MinDamping, MaxDamping));
		}
		if (IsXPBDStretchBiasElementWarpScaleMutable(PropertyCollection))
		{
			WarpScale.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDStretchBiasElementWarpScale(PropertyCollection)).ClampAxes(MinWarpWeftScale, MaxWarpWeftScale));
		}
		if (IsXPBDStretchBiasElementWeftScaleMutable(PropertyCollection))
		{
			WeftScale.SetWeightedValue(FSolverVec2(GetWeightedFloatXPBDStretchBiasElementWeftScale(PropertyCollection)).ClampAxes(MinWarpWeftScale, MaxWarpWeftScale));
		}
	}

	void SetProperties(const FSolverVec2& InStiffnessWarp, const FSolverVec2& InStiffnessWeft, const FSolverVec2& InStiffnessBias, const FSolverVec2& InDampingRatio, const FSolverVec2& InWarpScale, const FSolverVec2& InWeftScale)
	{
		StiffnessWarp.SetWeightedValue(InStiffnessWarp, MaxStiffness);
		StiffnessWeft.SetWeightedValue(InStiffnessWeft, MaxStiffness);
		StiffnessBias.SetWeightedValue(InStiffnessBias, MaxStiffness);
		DampingRatio.SetWeightedValue(InDampingRatio.ClampAxes(MinDamping, MaxDamping));
		WarpScale.SetWeightedValue(InWarpScale.ClampAxes(MinWarpWeftScale, MaxWarpWeftScale));
		WeftScale.SetWeightedValue(InWeftScale.ClampAxes(MinWarpWeftScale, MaxWarpWeftScale));
	}

	// Update stiffness table, as well as the simulation stiffness exponent
	void ApplyProperties(const FSolverReal /*Dt*/, const int32 /*NumIterations*/)
	{
		StiffnessWarp.ApplyXPBDValues(MaxStiffness);
		StiffnessWeft.ApplyXPBDValues(MaxStiffness);
		StiffnessBias.ApplyXPBDValues(MaxStiffness);
		DampingRatio.ApplyValues();
		WarpScale.ApplyValues();
		WeftScale.ApplyValues();
	}

	void Apply(FSolverParticles& Particles, const FSolverReal Dt) const;

private:
	void InitConstraintsAndRestData(const FSolverParticles& InParticles, const FTriangleMesh& TriangleMesh,
		const TArray<TVec3<FVec2f>>& FaceVertexUVs, const bool bUse3dRestLengths, const bool bTrimKinematicConstraints);
	void InitColor(const FSolverParticles& InParticles, const int32 ParticleOffset, const int32 ParticleCount);
	void ApplyHelper(FSolverParticles& Particles, const FSolverReal Dt, const int32 ConstraintIndex, const FSolverVec3& ExpStiffnessValue, const FSolverReal DampingRatioValue, const FSolverReal WarpScaleValue, const FSolverReal WeftScaleValue) const;

	TArray<TVec3<int32>> Constraints;

	FPBDStiffness StiffnessWarp;
	FPBDStiffness StiffnessWeft;
	FPBDStiffness StiffnessBias;
	FPBDWeightMap DampingRatio;
	FPBDWeightMap WarpScale;
	FPBDWeightMap WeftScale;
	mutable TArray<FSolverVec3> Lambdas; // separate for stretchU, stretchV, Bias
	TArray<int32> ConstraintsPerColorStartIndex; // Constraints are ordered so each batch is contiguous. This is ColorNum + 1 length so it can be used as start and end.

	TArray<FSolverMatrix22> DeltaUVInverse; // Used to convert from DeltaX to dX/dU and dX/dV
	TArray<FSolverVec2> RestStretchLengths;
	TArray<FSolverVec3> StiffnessScales; // Used to make everything resolution independent.

	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDStretchBiasElementUse3dRestLengths, bool);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDStretchBiasElementStiffnessWarp, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDStretchBiasElementStiffnessWeft, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDStretchBiasElementStiffnessBias, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDStretchBiasElementDamping, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDStretchBiasElementWarpScale, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(XPBDStretchBiasElementWeftScale, float);
};

}  // End namespace Chaos::Softs

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_XPBDStretchBiasElement_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_XPBDStretchBiasElement_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_XPBDStretchBiasElement_ISPC_Enabled;
#endif
