// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothingSimulation.h"
#include "ClothingAsset.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Transform.h"
#include "Chaos/ChaosDebugDrawDeclares.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "Components/SkeletalMeshComponent.h"

namespace Chaos
{

	template <typename T>
	class TTriangleMesh;

	template <typename T, int d>
	class TPBDEvolution;

	typedef FClothingSimulationContextCommon ClothingSimulationContext;
	template<class T, int d> class TPBDLongRangeConstraintsBase;

	class ClothingSimulation : public FClothingSimulationCommon
#if WITH_EDITOR
		, public FGCObject  // Add garbage collection for debug cloth material
#endif  // #if WITH_EDITOR
	{
	public:
		ClothingSimulation();
		virtual ~ClothingSimulation() override;

		// Set the animation drive stiffness for all actors
		void SetAnimDriveSpringStiffness(float InStiffness);
		void SetGravityOverride(const FVector& InGravityOverride);
		void DisableGravityOverride();

		// Function to be called if any of the assets' configuration parameters have changed
		void RefreshClothConfig();
		// Function to be called if any of the assets' physics assets changes (colliders)
		// This seems to only happen when UPhysicsAsset::RefreshPhysicsAssetChange is called with
		// bFullClothRefresh set to false during changes created using the viewport manipulators.
		void RefreshPhysicsAsset();

#if WITH_EDITOR
		// FGCObject interface
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		// End of FGCObject interface

		// IClothingSimulation interface
		virtual int32 GetNumCloths() const { return NumCloths; }
		virtual int32 GetNumKinematicParticles() const { return NumKinemamicParticles; }
		virtual int32 GetNumDynamicParticles() const { return NumDynamicParticles; }
		virtual float GetSimulationTime() const { return SimulationTime; }
		// End of IClothingSimulation interface

		// Editor only debug draw function
		CHAOSCLOTH_API void DebugDrawPhysMeshShaded(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawParticleIndices(USkeletalMeshComponent* OwnerComponent, FCanvas* Canvas, const FSceneView* SceneView) const;
		CHAOSCLOTH_API void DebugDrawMaxDistanceValues(USkeletalMeshComponent* OwnerComponent, FCanvas* Canvas, const FSceneView* SceneView) const;
#endif  // #if WITH_EDITOR

#if WITH_EDITOR || CHAOS_DEBUG_DRAW
		// Editor & runtime debug draw functions
		CHAOSCLOTH_API void DebugDrawPhysMeshWired(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawPointNormals(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawInversedPointNormals(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawFaceNormals(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawInversedFaceNormals(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawCollision(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawBackstops(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawMaxDistances(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawAnimDrive(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawLongRangeConstraint(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawWindDragForces(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawLocalSpace(USkeletalMeshComponent* OwnerComponent = nullptr, FPrimitiveDrawInterface* PDI = nullptr) const;
#endif  // #if WITH_EDITOR || CHAOS_DEBUG_DRAW

	protected:
		// IClothingSimulation interface
		virtual void Initialize() override;
		virtual void CreateActor(USkeletalMeshComponent* InOwnerComponent, UClothingAssetBase* InAsset, int32 SimDataIndex) override;
		virtual void PostActorCreationInitialize() override;
		virtual IClothingSimulationContext* CreateContext() override { return new ClothingSimulationContext(); }
		virtual void Shutdown() override;
		virtual bool ShouldSimulate() const override { return true; }
		virtual void Simulate(IClothingSimulationContext* InContext) override;
		virtual void DestroyActors() override;
		virtual void DestroyContext(IClothingSimulationContext* InContext) override { delete InContext; }
		virtual void GetSimulationData(TMap<int32, FClothSimulData>& OutData, USkeletalMeshComponent* InOwnerComponent, USkinnedMeshComponent* InOverrideComponent) const override;

		// Return bounds in local space (or in world space if InOwnerComponent is null).
		virtual FBoxSphereBounds GetBounds(const USkeletalMeshComponent* InOwnerComponent) const override;

		virtual void AddExternalCollisions(const FClothCollisionData& InData) override;
		virtual void ClearExternalCollisions() override;
		virtual void GetCollisions(FClothCollisionData& OutCollisions, bool bIncludeExternal = true) const override;
		// End of IClothingSimulation interface

	private:
		void ResetStats();
		void UpdateStats(int32 InSimDataIndex);

#if CHAOS_DEBUG_DRAW
		// Runtime only debug draw functions
		void DebugDrawBounds() const;
		void DebugDrawGravity() const;
#endif  // #if CHAOS_DEBUG_DRAW

		void UpdateSimulationFromSharedSimConfig();
		void BuildMesh(const FClothPhysicalMeshData& PhysMesh, int32 InSimDataIndex);

		// Reset particle positions to animation positions, masses and velocities are cleared
		void ResetParticles(int32 InSimDataIndex);

		void SetParticleMasses(const UChaosClothConfig* ChaosClothSimConfig, const FClothPhysicalMeshData& PhysMesh, int32 InSimDataIndex);

		void AddConstraints(const UChaosClothConfig* ChaosClothSimConfig, const FClothPhysicalMeshData& PhysMesh, int32 InSimDataIndex);

		void AddSelfCollisions(int32 InSimDataIndex);

		// Extract all collisions (from the physics asset and from the legacy apex collisions)
		void ExtractCollisions(const UClothingAssetCommon* Asset, int32 InSimDataIndex);
		// Extract the collisions from the physics asset referenced by the specified clothing asset
		void ExtractPhysicsAssetCollisions(const UClothingAssetCommon* Asset, int32 InSimDataIndex);
		// Extract the legacy apex collisions from the specified clothing asset 
		void ExtractLegacyAssetCollisions(const UClothingAssetCommon* Asset, int32 InSimDataIndex);
		// Add collisions from a ClothCollisionData structure
		void AddCollisions(const FClothCollisionData& ClothCollisionData, const TArray<int32>& UsedBoneIndices, int32 InSimDataIndex);
		// Process the collision function for all collisions of the specified cloth
		void ForAllCollisions(TFunction<void(TGeometryClothParticles<float, 3>&, uint32)> CollisionFunction, int32 SimDataIndex);
		// Update the collision transforms using the specified context, also reset initial collision particle states if the number of collisions has changed
		void UpdateCollisionTransforms(const ClothingSimulationContext& Context, int32 InSimDataIndex);
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
		TArray<float> MaxDistancesMultipliers;

		// Collision Data
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

		TArray<Chaos::TVector<uint32, 2>> CollisionsRangeMap;  // Collisions first particle index and number of collisions for each simulated cloth
		TArray<TArray<Chaos::TVector<uint32, 2>>> ExternalCollisionsRangeMaps;  // External collisions first particle index and number of collisions for each simulated cloth
		uint32 ExternalCollisionsOffset;  // External collisions first particle index

		float Time;
		float DeltaTime;
		int32 NumSubsteps;

		bool bOverrideGravity;
		bool bUseConfigGravity;
		float GravityScale;
		FVector Gravity;
		FVector ConfigGravity;
		FVector WindVelocity;

		TArray<TSharedPtr<TPBDLongRangeConstraintsBase<float, 3>>> LongRangeConstraints;

		bool bUseLocalSpaceSimulation;  // The function of this is to help with floating point precision errors if the character is far away form the world origin
		FVector LocalSpaceLocation;  // This is used to translate between world space and simulation space. Add this to simulation space coordinates to get world space coordinates
		TArray<FTransform> RootBoneWorldTransforms;  // Reference bone transform used for teleportation and to decouple the simulation from reference bone's accelerations
		TArray<FVector> LinearDeltaRatios;  // Linear ratios applied to the reference bone transforms
		TArray<float> AngularDeltaRatios;  // Angular ratios factor applied to the reference bone transforms

#if WITH_EDITOR
		// Stats
		TAtomic<int32> NumCloths;
		TAtomic<int32> NumKinemamicParticles;
		TAtomic<int32> NumDynamicParticles;
		TAtomic<float> SimulationTime;

		// Visualization material
		UMaterial* DebugClothMaterial;
		UMaterial* DebugClothMaterialVertex;
#endif  // #if WITH_EDITOR
	};
} // namespace Chaos