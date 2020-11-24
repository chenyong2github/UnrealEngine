// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Vector.h"
#include "Containers/ContainersFwd.h"
#include "Templates/SharedPointer.h"
#include "Chaos/PBDLongRangeConstraints.h"

namespace Chaos
{
	template<typename T, int d> class TPBDEvolution;

	class FPBDSpringConstraints;
	template<typename T, int d> class TXPBDSpringConstraints;
	template<typename T> class TPBDBendingConstraints;
	class FPBDAxialSpringConstraints;
	template<typename T, int d> class TXPBDAxialSpringConstraints;
	template<typename T> class TPBDVolumeConstraint;
	template<typename T, int d> class TXPBDLongRangeConstraints;
	template<typename T, int d> class TPBDSphericalConstraint;
	template<typename T, int d> class TPBDSphericalBackstopConstraint;
	template<typename T, int d> class TPBDAnimDriveConstraint;
	template<typename T, int d> class TPBDShapeConstraints;
	template<typename T, int d> class TPBDCollisionSpringConstraints;

	class FClothConstraints final
	{
	public:
		typedef TPBDLongRangeConstraints<float, 3>::EMode ETetherMode;

		FClothConstraints();
		~FClothConstraints();

		// ---- Solver interface ----
		void Initialize(
			TPBDEvolution<float, 3>* InEvolution,
			const TArray<TVector<float, 3>>& InAnimationPositions,
			const TArray<TVector<float, 3>>& InAnimationNormals,
			int32 InParticleOffset,
			int32 InNumParticles);
		// ---- End of Solver interface ----

		// ---- Cloth interface ----
		void SetEdgeConstraints(const TArray<TVector<int32, 3>>& SurfaceElements, float EdgeStiffness, bool bUseXPBDConstraints);
		void SetBendingConstraints(TArray<TVector<int32, 2>>&& Edges, float BendingStiffness, bool bUseXPBDConstraints);
		void SetBendingConstraints(TArray<TVector<int32, 4>>&& BendingElements, float BendingStiffness);
		void SetAreaConstraints(TArray<TVector<int32, 3>>&& SurfaceElements, float AreaStiffness, bool bUseXPBDConstraints);
		void SetVolumeConstraints(TArray<TVector<int32, 2>>&& DoubleBendingEdges, float VolumeStiffness);
		void SetVolumeConstraints(TArray<TVector<int32, 3>>&& SurfaceElements, float VolumeStiffness);
		void SetLongRangeConstraints(const TMap<int32, TSet<uint32>>& PointToNeighborsMap, float StrainLimitingStiffness, float LimitScale, ETetherMode TetherMode, bool bUseXPBDConstraints);
		void SetMaximumDistanceConstraints(const TConstArrayView<float>& MaxDistances);
		void SetBackstopConstraints(const TConstArrayView<float>& BackstopDistances, const TConstArrayView<float>& BackstopRadiuses, bool bUseLegacyBackstop);
		void SetAnimDriveConstraints(const TConstArrayView<float>& AnimDriveMultipliers);
		void SetShapeTargetConstraints(float ShapeTargetStiffness);
		void SetSelfCollisionConstraints(const TArray<TVector<int32, 3>>& SurfaceElements, TSet<TVector<int32, 2>>&& DisabledCollisionElements, float SelfCollisionThickness);

		void CreateRules();
		void Enable(bool bEnable);

		void SetMaxDistancesMultiplier(float InMaxDistancesMultiplier) { MaxDistancesMultiplier = InMaxDistancesMultiplier; }
		void SetAnimDriveSpringStiffness(float InAnimDriveSpringStiffness) { AnimDriveSpringStiffness = InAnimDriveSpringStiffness; }
		// ---- End of Cloth interface ----

		// ---- Debug functions ----
		const TSharedPtr<FPBDSpringConstraints> GetEdgeConstraints() const { return EdgeConstraints; }
		const TSharedPtr<TXPBDSpringConstraints<float, 3>>& GetXEdgeConstraints() const { return XEdgeConstraints; } 
		const TSharedPtr<FPBDSpringConstraints>& GetBendingConstraints() const { return BendingConstraints; }  
		const TSharedPtr<TXPBDSpringConstraints<float, 3>>& GetXBendingConstraints() const { return XBendingConstraints; }
		const TSharedPtr<TPBDBendingConstraints<float>>& GetBendingElementConstraints() const { return BendingElementConstraints; }
		const TSharedPtr<FPBDAxialSpringConstraints>& GetAreaConstraints() const { return AreaConstraints; }
		const TSharedPtr<TXPBDAxialSpringConstraints<float,3>>& GetXAreaConstraints() const { return XAreaConstraints; }
		const TSharedPtr<FPBDSpringConstraints>& GetThinShellVolumeConstraints() const { return ThinShellVolumeConstraints; }
		const TSharedPtr<TPBDVolumeConstraint<float>>& GetVolumeConstraints() const { return VolumeConstraints; } 
		const TSharedPtr<TPBDLongRangeConstraints<float, 3>>& GetLongRangeConstraints() const { return LongRangeConstraints; }
		const TSharedPtr<TXPBDLongRangeConstraints<float,3>>& GetXLongRangeConstraints() const { return XLongRangeConstraints; } 
		const TSharedPtr<TPBDSphericalConstraint<float, 3>>& GetMaximumDistanceConstraints() const { return MaximumDistanceConstraints; }
		const TSharedPtr<TPBDSphericalBackstopConstraint<float, 3>>& GetBackstopConstraints() const { return BackstopConstraints; }
		const TSharedPtr<TPBDAnimDriveConstraint<float, 3>>& GetAnimDriveConstraints() const { return AnimDriveConstraints; }
		const TSharedPtr<TPBDShapeConstraints<float, 3>>& GetShapeConstraints() const { return ShapeConstraints; }
		const TSharedPtr<TPBDCollisionSpringConstraints<float, 3>>& GetSelfCollisionConstraints() const { return SelfCollisionConstraints; }
		// ---- End of debug functions ----

	private:
		TSharedPtr<FPBDSpringConstraints> EdgeConstraints;
		TSharedPtr<TXPBDSpringConstraints<float, 3>> XEdgeConstraints;
		TSharedPtr<FPBDSpringConstraints> BendingConstraints;
		TSharedPtr<TXPBDSpringConstraints<float, 3>> XBendingConstraints;
		TSharedPtr<TPBDBendingConstraints<float>> BendingElementConstraints;
		TSharedPtr<FPBDAxialSpringConstraints> AreaConstraints;
		TSharedPtr<TXPBDAxialSpringConstraints<float, 3>> XAreaConstraints;
		TSharedPtr<FPBDSpringConstraints> ThinShellVolumeConstraints;
		TSharedPtr<TPBDVolumeConstraint<float>> VolumeConstraints;
		TSharedPtr<TPBDLongRangeConstraints<float, 3>> LongRangeConstraints;
		TSharedPtr<TXPBDLongRangeConstraints<float, 3>> XLongRangeConstraints;
		TSharedPtr<TPBDSphericalConstraint<float, 3>> MaximumDistanceConstraints;
		TSharedPtr<TPBDSphericalBackstopConstraint<float, 3>> BackstopConstraints;
		TSharedPtr<TPBDAnimDriveConstraint<float, 3>> AnimDriveConstraints;
		TSharedPtr<TPBDShapeConstraints<float, 3>> ShapeConstraints;
		TSharedPtr<TPBDCollisionSpringConstraints<float, 3>> SelfCollisionConstraints;
		
		TPBDEvolution<float, 3>* Evolution;
		const TArray<TVector<float, 3>>* AnimationPositions;
		const TArray<TVector<float, 3>>* AnimationNormals;

		int32 ParticleOffset;
		int32 NumParticles;
		int32 ConstraintInitOffset;
		int32 ConstraintRuleOffset;
		int32 NumConstraintInits;
		int32 NumConstraintRules;

		// Animatable parameters
		float MaxDistancesMultiplier;
		float AnimDriveSpringStiffness;
	};
} // namespace Chaos
