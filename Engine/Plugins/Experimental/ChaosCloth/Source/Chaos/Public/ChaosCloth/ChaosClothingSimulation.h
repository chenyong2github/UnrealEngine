// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothingSimulationInterface.h"

#include "ClothingAsset.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/PBDEvolution.h"
#include "Chaos/Transform.h"
#include "Chaos/TriangleMesh.h"
#include "Components/SkeletalMeshComponent.h"

namespace Chaos
{
	class ClothingSimulationContext : public IClothingSimulationContext
	{
	public:
		ClothingSimulationContext() {}
		~ClothingSimulationContext() {}

		float DeltaTime;
		TArray<FMatrix> RefToLocals;
		TArray<FTransform> BoneTransforms;
		FTransform ComponentToWorld;
	};

	class ClothingSimulation
		: public IClothingSimulation
#if WITH_EDITOR
		, public FGCObject  // Add garbage collection for debug cloth material
#endif  // #if WITH_EDITOR
	{
	public:
		ClothingSimulation();
		virtual ~ClothingSimulation();

#if WITH_EDITOR
		// FGCObject interface
		void AddReferencedObjects(FReferenceCollector& Collector) override;
		// End of FGCObject interface

		CHAOSCLOTH_API void DebugDrawPhysMeshWired(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawPhysMeshShaded(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawPointNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawInversedPointNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawFaceNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawInversedFaceNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawCollision(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawBackstops(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawMaxDistances(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawSelfCollision(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawAnimDrive(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
#endif  // #if WITH_EDITOR

	protected:
		// IClothingSimulation interface
		void Initialize() override;
		void CreateActor(USkeletalMeshComponent* InOwnerComponent, UClothingAssetBase* InAsset, int32 SimDataIndex) override;
		IClothingSimulationContext* CreateContext() override { return new ClothingSimulationContext(); }
		void FillContext(USkeletalMeshComponent* InComponent, float InDeltaTime, IClothingSimulationContext* InOutContext) override;
		void Shutdown() override;
		bool ShouldSimulate() const override { return true; }
		void Simulate(IClothingSimulationContext* InContext) override;
		void DestroyActors() override;
		void DestroyContext(IClothingSimulationContext* InContext) override { delete InContext; }
		void GetSimulationData(TMap<int32, FClothSimulData>& OutData, USkeletalMeshComponent* InOwnerComponent, USkinnedMeshComponent* InOverrideComponent) const override;

		FBoxSphereBounds GetBounds(const USkeletalMeshComponent* InOwnerComponent) const override
		{ return FBoxSphereBounds(Evolution->Particles().X().GetData(), Evolution->Particles().Size()); }

		void AddExternalCollisions(const FClothCollisionData& InData) override;
		void ClearExternalCollisions() override;
		void GetCollisions(FClothCollisionData& OutCollisions, bool bIncludeExternal = true) const override;
		// End of IClothingSimulation interface

	private:
		// Extract the collisions from the physics asset referenced by the specified clothing asset
		void ExtractPhysicsAssetCollisions(UClothingAssetCommon* Asset);
		// Extract the collisions from the specified clothing asset 
		void ExtractLegacyAssetCollisions(UClothingAssetCommon* Asset, const USkeletalMeshComponent* InOwnerComponent);
		// Create the bone mapping for all the bones used by collision
		void RefreshBoneMapping(UClothingAssetCommon* Asset);

	private:
		// Assets
		TArray<UClothingAssetCommon*> Assets;

		// Collision Data
		FClothCollisionData ExtractedCollisions;  // Collisions extracted from the referenced physics asset
		FClothCollisionData ExternalCollisions;  // External collisions
		TArray<FName> CollisionBoneNames;  // List of the bone names used by collision
		TArray<int32> CollisionBoneIndices;  // List of the indices for the bones in UsedBoneNames, used for remapping
		TArray<Chaos::TRigidTransform<float, 3>> OldCollisionTransforms;  // Used for the kinematic collision transform update
		TArray<Chaos::TRigidTransform<float, 3>> CollisionTransforms;  // Used for the kinematic collision transform update
		Chaos::TArrayCollectionArray<int32> BoneIndices;
		Chaos::TArrayCollectionArray<Chaos::TRigidTransform<float, 3>> BaseTransforms;

		// Animation Data
		TArray<Chaos::TVector<float, 3>> OldAnimationPositions;
		TArray<Chaos::TVector<float, 3>> AnimationPositions;
		TArray<Chaos::TVector<float, 3>> AnimationNormals;

		// Sim Data
		TArray<Chaos::TVector<uint32, 2>> IndexToRangeMap;

		TArray<TUniquePtr<Chaos::TTriangleMesh<float>>> Meshes;
		mutable TArray<TArray<Chaos::TVector<float, 3>>> FaceNormals;
		mutable TArray<TArray<Chaos::TVector<float, 3>>> PointNormals;

		TUniquePtr<Chaos::TPBDEvolution<float, 3>> Evolution;

		uint32 ExternalCollisionsOffset;  // External collisions first particle index

		float Time;
		float DeltaTime;
		float MaxDeltaTime;
		float ClampDeltaTime;
		// Parameters that should be set in the ui
		int32 NumIterations;

		EClothMassMode MassMode;
		float UniformMass;
		float TotalMass;
		float Density;
		float MinMass;

		float EdgeStiffness;
		float BendingStiffness;
		float AreaStiffness;
		float VolumeStiffness;
		float StrainLimitingStiffness;
		float ShapeTargetStiffness;
		float SelfCollisionThickness;
		float CollisionThickness;
		float CoefficientOfFriction;
		float Damping;
		float GravityMagnitude;
		bool bUseBendingElements;
		bool bUseTetrahedralConstraints;
		bool bUseThinShellVolumeConstraints;
		bool bUseSelfCollisions;
		bool bUseContinuousCollisionDetection;

#if WITH_EDITOR
		// Visualization material
		UMaterial* DebugClothMaterial;
#endif  // #if WITH_EDITOR
	};
} // namespace Chaos