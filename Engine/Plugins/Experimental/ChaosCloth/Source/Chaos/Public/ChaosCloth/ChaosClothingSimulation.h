// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothingSimulationInterface.h"

#include "ClothingAsset.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/PBDEvolution.h"
#include "Chaos/Transform.h"
#include "Chaos/TriangleMesh.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "Components/SkeletalMeshComponent.h"

namespace Chaos
{
	class ClothingSimulationContext : public IClothingSimulationContext  // TODO(Kriss.Gossart): Check whether this should inherit from FClothingSimulationContextBase
	{
	public:
		ClothingSimulationContext() {}
		~ClothingSimulationContext() {}

		float DeltaTime;
		TArray<FMatrix> RefToLocals;
		TArray<FTransform> BoneTransforms;
		FTransform ComponentToWorld;
	};

	class ClothingSimulation : public IClothingSimulation
#if WITH_EDITOR
		, public FGCObject  // Add garbage collection for debug cloth material
#endif  // #if WITH_EDITOR
	{
	public:
		ClothingSimulation();
		virtual ~ClothingSimulation();

		// Set the animation drive stiffness for all actors
		void SetAnimDriveSpringStiffness(float InStiffness);
		void SetGravityOverride(const FVector& InGravityOverride);
		void DisableGravityOverride();

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
		CHAOSCLOTH_API void DebugDrawAnimDrive(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
#endif  // #if WITH_EDITOR

	protected:
		// IClothingSimulation interface
		void Initialize() override;
		void CreateActor(USkeletalMeshComponent* InOwnerComponent, UClothingAssetBase* InAsset, int32 SimDataIndex) override;
		void PostActorCreationInitialize() override;
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

		void BuildMesh(TUniquePtr<Chaos::TTriangleMesh<float>>& OutChaosMesh, const FClothPhysicalMeshData& InPhysMesh, uint32 Offset) const;

		void SetParticleMasses(
			const UChaosClothConfig* ChaosClothSimConfig,
			TPBDParticles<float, 3>& Particles,
			uint32 Offset, // Offset into Particles array
			uint32 ParticlesCount, // Number of particles
			Chaos::TTriangleMesh<float>& Mesh, // Mesh with indices into Particles with offset already taken into account
			const FPointWeightMap& MaxDistances
			) const;

		void AddConstraints(const UChaosClothConfig* ChaosClothSimConfig, const FClothPhysicalMeshData& PhysMesh, const Chaos::TTriangleMesh<float>& Mesh, uint32 Offset, int32 InSimDataIndex);

		void AddSelfCollisions(const TUniquePtr<Chaos::TTriangleMesh<float>>& Mesh, const TPBDParticles<float, 3>& Particles, int32 Offset);

		// Extract the collisions from the physics asset referenced by the specified clothing asset
		void ExtractPhysicsAssetCollisions(UClothingAssetCommon* Asset);
		// Extract the collisions from the specified clothing asset 
		void ExtractLegacyAssetCollisions(UClothingAssetCommon* Asset, const USkeletalMeshComponent* InOwnerComponent);
		// Add collisions from a ClothCollisionData structure
		void AddCollisions(const FClothCollisionData& ClothCollisionData, const TArray<int32>& UsedBoneIndices);
		// Update the collision transforms using the specified context
		void UpdateCollisionTransforms(const ClothingSimulationContext& Context);
		// Return the correct bone index based on the asset used bone index array
		FORCEINLINE int32 GetMappedBoneIndex(const TArray<int32>& UsedBoneIndices, int32 BoneIndex)
		{
			return UsedBoneIndices.IsValidIndex(BoneIndex) ? UsedBoneIndices[BoneIndex] : INDEX_NONE;
		}

	private:
		// Assets
		TArray<UClothingAssetCommon*> Assets;
		UChaosClothSharedSimConfig* ClothSharedSimConfig;

		// Cloth Interaction Parameters
		// These simulation parameters can be changed through blueprints
		// They will only be updated when the simulation is not running (so are safe to use on any cloth thread)
		TArray<float> AnimDriveSpringStiffness; // One for every Asset

		// Collision Data
		FClothCollisionData ExtractedCollisions;  // Collisions extracted from the referenced physics asset
		FClothCollisionData ExternalCollisions;  // External collisions
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

#if WITH_EDITOR
		// Visualization material
		UMaterial* DebugClothMaterial;
		UMaterial* DebugClothMaterialVertex;
#endif  // #if WITH_EDITOR
	};
} // namespace Chaos