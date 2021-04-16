// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Vector.h"
#include "Containers/ContainersFwd.h"
#include "Templates/SharedPointer.h"
#include "Chaos/PBDLongRangeConstraints.h"
#include "Chaos/PBDCollisionSpringConstraints.h"

namespace Chaos
{
	class FPBDEvolution;

	class FPBDSpringConstraints;
	class FXPBDSpringConstraints;
	class FPBDBendingConstraints;
	class FPBDAxialSpringConstraints;
	class FXPBDAxialSpringConstraints;
	class FPBDVolumeConstraint;
	class FXPBDLongRangeConstraints;
	class FPBDSphericalConstraint;
	class FPBDSphericalBackstopConstraint;
	class FPBDAnimDriveConstraint;
	class FPBDShapeConstraints;
	class FPBDCollisionSpringConstraints;

	class FClothConstraints final
	{
	public:
		typedef FPBDLongRangeConstraints::EMode ETetherMode;

		FClothConstraints();
		~FClothConstraints();

		// ---- Solver interface ----
		void Initialize(
			FPBDEvolution* InEvolution,
			const TArray<FVec3>& InAnimationPositions,
			const TArray<FVec3>& InOldAnimationPositions,
			const TArray<FVec3>& InAnimationNormals,
			int32 InParticleOffset,
			int32 InNumParticles);
		// ---- End of Solver interface ----

		// ---- Cloth interface ----
		void SetEdgeConstraints(const TArray<TVec3<int32>>& SurfaceElements, FReal EdgeStiffness, bool bUseXPBDConstraints);
		void SetBendingConstraints(TArray<TVec2<int32>>&& Edges, FReal BendingStiffness, bool bUseXPBDConstraints);
		void SetBendingConstraints(TArray<TVec4<int32>>&& BendingElements, FReal BendingStiffness);
		void SetAreaConstraints(TArray<TVec3<int32>>&& SurfaceElements, FReal AreaStiffness, bool bUseXPBDConstraints);
		void SetVolumeConstraints(TArray<TVec2<int32>>&& DoubleBendingEdges, FReal VolumeStiffness);
		void SetVolumeConstraints(TArray<TVec3<int32>>&& SurfaceElements, FReal VolumeStiffness);
		void SetLongRangeConstraints(const TMap<int32, TSet<int32>>& PointToNeighborsMap, const TConstArrayView<FReal>& TetherStiffnessMultipliers, const FVec2& TetherStiffness, FReal LimitScale, ETetherMode TetherMode, bool bUseXPBDConstraints);
		void SetMaximumDistanceConstraints(const TConstArrayView<FReal>& MaxDistances);
		void SetBackstopConstraints(const TConstArrayView<FReal>& BackstopDistances, const TConstArrayView<FReal>& BackstopRadiuses, bool bUseLegacyBackstop);
		void SetAnimDriveConstraints(const TConstArrayView<FReal>& AnimDriveStiffnessMultipliers, const TConstArrayView<FReal>& AnimDriveDampingMultipliers);
		void SetShapeTargetConstraints(FReal ShapeTargetStiffness);
		void SetSelfCollisionConstraints(const TArray<TVec3<int32>>& SurfaceElements, TSet<TVec2<int32>>&& DisabledCollisionElements, FReal SelfCollisionThickness);

		void CreateRules();
		void Enable(bool bEnable);

		void SetEdgeProperties(FReal EdgeStiffness);
		void SetBendingProperties(FReal BendingStiffness);
		void SetAreaProperties(FReal AreaStiffness);
		void SetThinShellVolumeProperties(FReal VolumeStiffness);
		void SetVolumeProperties(FReal VolumeStiffness);
		void SetLongRangeAttachmentProperties(const FVec2& TetherStiffness);
		void SetMaximumDistanceProperties(FReal MaxDistancesMultiplier);
		void SetAnimDriveProperties(const FVec2& AnimDriveStiffness, const TVector<FReal, 2>& AnimDriveDamping);
		void SetSelfCollisionProperties(FReal SelfCollisionThickness);
		// ---- End of Cloth interface ----

		// ---- Debug functions ----
		const TSharedPtr<FPBDSpringConstraints> GetEdgeConstraints() const { return EdgeConstraints; }
		const TSharedPtr<FXPBDSpringConstraints>& GetXEdgeConstraints() const { return XEdgeConstraints; }
		const TSharedPtr<FPBDSpringConstraints>& GetBendingConstraints() const { return BendingConstraints; }  
		const TSharedPtr<FXPBDSpringConstraints>& GetXBendingConstraints() const { return XBendingConstraints; }
		const TSharedPtr<FPBDBendingConstraints>& GetBendingElementConstraints() const { return BendingElementConstraints; }
		const TSharedPtr<FPBDAxialSpringConstraints>& GetAreaConstraints() const { return AreaConstraints; }
		const TSharedPtr<FXPBDAxialSpringConstraints>& GetXAreaConstraints() const { return XAreaConstraints; }
		const TSharedPtr<FPBDSpringConstraints>& GetThinShellVolumeConstraints() const { return ThinShellVolumeConstraints; }
		const TSharedPtr<FPBDVolumeConstraint>& GetVolumeConstraints() const { return VolumeConstraints; }
		const TSharedPtr<FPBDLongRangeConstraints>& GetLongRangeConstraints() const { return LongRangeConstraints; }
		const TSharedPtr<FXPBDLongRangeConstraints>& GetXLongRangeConstraints() const { return XLongRangeConstraints; }
		const TSharedPtr<FPBDSphericalConstraint>& GetMaximumDistanceConstraints() const { return MaximumDistanceConstraints; }
		const TSharedPtr<FPBDSphericalBackstopConstraint>& GetBackstopConstraints() const { return BackstopConstraints; }
		const TSharedPtr<FPBDAnimDriveConstraint>& GetAnimDriveConstraints() const { return AnimDriveConstraints; }
		const TSharedPtr<FPBDShapeConstraints>& GetShapeConstraints() const { return ShapeConstraints; }
		const TSharedPtr<FPBDCollisionSpringConstraints>& GetSelfCollisionConstraints() const { return SelfCollisionConstraints; }
		// ---- End of debug functions ----

	private:
		TSharedPtr<FPBDSpringConstraints> EdgeConstraints;
		TSharedPtr<FXPBDSpringConstraints> XEdgeConstraints;
		TSharedPtr<FPBDSpringConstraints> BendingConstraints;
		TSharedPtr<FXPBDSpringConstraints> XBendingConstraints;
		TSharedPtr<FPBDBendingConstraints> BendingElementConstraints;
		TSharedPtr<FPBDAxialSpringConstraints> AreaConstraints;
		TSharedPtr<FXPBDAxialSpringConstraints> XAreaConstraints;
		TSharedPtr<FPBDSpringConstraints> ThinShellVolumeConstraints;
		TSharedPtr<FPBDVolumeConstraint> VolumeConstraints;
		TSharedPtr<FPBDLongRangeConstraints> LongRangeConstraints;
		TSharedPtr<FXPBDLongRangeConstraints> XLongRangeConstraints;
		TSharedPtr<FPBDSphericalConstraint> MaximumDistanceConstraints;
		TSharedPtr<FPBDSphericalBackstopConstraint> BackstopConstraints;
		TSharedPtr<FPBDAnimDriveConstraint> AnimDriveConstraints;
		TSharedPtr<FPBDShapeConstraints> ShapeConstraints;
		TSharedPtr<FPBDCollisionSpringConstraints> SelfCollisionConstraints;
		
		FPBDEvolution* Evolution;
		const TArray<FVec3>* AnimationPositions;
		const TArray<FVec3>* OldAnimationPositions;
		const TArray<FVec3>* AnimationNormals;

		int32 ParticleOffset;
		int32 NumParticles;
		int32 ConstraintInitOffset;
		int32 ConstraintRuleOffset;
		int32 NumConstraintInits;
		int32 NumConstraintRules;
	};
} // namespace Chaos
