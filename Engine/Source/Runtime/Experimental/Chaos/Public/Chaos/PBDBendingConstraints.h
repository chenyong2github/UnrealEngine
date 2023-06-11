// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDBendingConstraintsBase.h"
#include "Chaos/CollectionPropertyFacade.h"

namespace Chaos::Softs
{

class FPBDBendingConstraints : public FPBDBendingConstraintsBase
{
	typedef FPBDBendingConstraintsBase Base;

public:
	static bool IsEnabled(const FCollectionPropertyConstFacade& PropertyCollection)
	{
		return IsBendingElementStiffnessEnabled(PropertyCollection, false);
	}

	FPBDBendingConstraints(const FSolverParticles& InParticles,
		int32 InParticleOffset,
		int32 InParticleCount,
		TArray<TVec4<int32>>&& InConstraints,
		const TMap<FString, TConstArrayView<FRealSingle>>& WeightMaps,
		const FCollectionPropertyConstFacade& PropertyCollection,
		bool bTrimKinematicConstraints = false)
		: Base(
			InParticles,
			InParticleOffset,
			InParticleCount,
			MoveTemp(InConstraints),
			WeightMaps.FindRef(GetBendingElementStiffnessString(PropertyCollection, BendingElementStiffnessName.ToString())),
			WeightMaps.FindRef(GetBucklingStiffnessString(PropertyCollection, BucklingStiffnessName.ToString())),
			FSolverVec2(GetWeightedFloatBendingElementStiffness(PropertyCollection, 1.f)),
			(FSolverReal)GetBucklingRatio(PropertyCollection, 0.f),  // BucklingRatio is clamped in base class
			FSolverVec2(GetWeightedFloatBucklingStiffness(PropertyCollection, 1.f)),
			bTrimKinematicConstraints)
	{
		InitColor(InParticles);
	}

	UE_DEPRECATED(5.3, "Use weight map constructor instead.")
	FPBDBendingConstraints(const FSolverParticles& InParticles,
		int32 InParticleOffset,
		int32 InParticleCount,
		TArray<TVec4<int32>>&& InConstraints,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const TConstArrayView<FRealSingle>& BucklingStiffnessMultipliers,
		const FCollectionPropertyConstFacade& PropertyCollection,
		bool bTrimKinematicConstraints = false)
		: Base(
			InParticles,
			InParticleOffset,
			InParticleCount,
			MoveTemp(InConstraints),
			StiffnessMultipliers,
			BucklingStiffnessMultipliers,
			FSolverVec2(GetWeightedFloatBendingElementStiffness(PropertyCollection, 1.f)),
			(FSolverReal)GetBucklingRatio(PropertyCollection, 0.f),  // BucklingRatio is clamped in base class
			FSolverVec2(GetWeightedFloatBucklingStiffness(PropertyCollection, 1.f)),
			bTrimKinematicConstraints) 
	{
		InitColor(InParticles);
	}

	FPBDBendingConstraints(const FSolverParticles& InParticles,
		int32 ParticleOffset,
		int32 ParticleCount,
		TArray<TVec4<int32>>&& InConstraints,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const TConstArrayView<FRealSingle>& BucklingStiffnessMultipliers,
		const FSolverVec2& InStiffness,
		const FSolverReal InBucklingRatio,
		const FSolverVec2& InBucklingStiffness,
		bool bTrimKinematicConstraints = false)
		: Base(
			InParticles,
			ParticleOffset,
			ParticleCount,
			MoveTemp(InConstraints),
			StiffnessMultipliers,
			BucklingStiffnessMultipliers,
			InStiffness,
			InBucklingRatio,
			InBucklingStiffness,
			bTrimKinematicConstraints) 
	{
		InitColor(InParticles);
	}

	UE_DEPRECATED(5.2, "Use one of the other constructors instead.")
	CHAOS_API FPBDBendingConstraints(
		const FSolverParticles& InParticles,
		TArray<TVec4<int32>>&& InConstraints,
		const FSolverReal InStiffness = (FSolverReal)1.)
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		: Base(InParticles, MoveTemp(InConstraints), InStiffness) {}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	virtual ~FPBDBendingConstraints() override {}

	using Base::SetProperties;

	CHAOS_API void SetProperties(
		const FCollectionPropertyConstFacade& PropertyCollection,
		const TMap<FString, TConstArrayView<FRealSingle>>& WeightMaps);

	UE_DEPRECATED(5.3, "Use SetProperties(const FCollectionPropertyConstFacade&, const TMap<FString, TConstArrayView<FRealSingle>>&, FSolverReal) instead.")
	void SetProperties(const FCollectionPropertyConstFacade& PropertyCollection)
	{
		SetProperties(PropertyCollection, TMap<FString, TConstArrayView<FRealSingle>>());
	}

	CHAOS_API void Apply(FSolverParticles& InParticles, const FSolverReal Dt) const;

	const TArray<int32>& GetConstraintsPerColorStartIndex() const { return ConstraintsPerColorStartIndex; }

private:
	CHAOS_API void InitColor(const FSolverParticles& InParticles);
	CHAOS_API void ApplyHelper(FSolverParticles& InParticles, const FSolverReal Dt, const int32 ConstraintIndex, const FSolverReal ExpStiffnessValue, const FSolverReal ExpBucklingValue) const;

	using Base::Constraints;
	using Base::ParticleOffset;
	using Base::ParticleCount;
	using Base::Stiffness;
	using Base::BucklingRatio;
	using Base::BucklingStiffness;

	TArray<int32> ConstraintsPerColorStartIndex; // Constraints are ordered so each batch is contiguous. This is ColorNum + 1 length so it can be used as start and end.

	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(BendingElementStiffness, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(BucklingRatio, float);
	UE_CHAOS_DECLARE_PROPERTYCOLLECTION_NAME(BucklingStiffness, float);
};

}  // End namespace Chaos::Softs

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_Bending_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_Bending_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_Bending_ISPC_Enabled;
#endif
