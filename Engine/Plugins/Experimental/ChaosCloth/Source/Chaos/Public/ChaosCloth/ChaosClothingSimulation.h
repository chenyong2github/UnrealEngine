// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothingSimulation.h"
#include "ClothingAsset.h"
#include "ClothCollisionData.h"
#include "Chaos/ChaosDebugDrawDeclares.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "Templates/Atomic.h"
#include "Templates/UniquePtr.h"

class USkeletalMeshComponent;
class FClothingSimulationContextCommon;
#if WITH_EDITOR
class UMaterial;
#endif

namespace Chaos
{
	class FTriangleMesh;
	class FPBDLongRangeConstraintsBase;
	class FPBDEvolution;
	class FClothingSimulationSolver;
	class FClothingSimulationMesh;
	class FClothingSimulationCloth;
	class FClothingSimulationCollider;

	typedef FClothingSimulationContextCommon FClothingSimulationContext;

	class FClothingSimulation : public FClothingSimulationCommon
#if WITH_EDITOR
		, public FGCObject  // Add garbage collection for debug cloth material
#endif  // #if WITH_EDITOR
	{
	public:
		FClothingSimulation();
		virtual ~FClothingSimulation() override;

	protected:
		// IClothingSimulation interface
		virtual void Initialize() override;
		virtual void Shutdown() override;

		virtual IClothingSimulationContext* CreateContext() override;
		virtual void DestroyContext(IClothingSimulationContext* InContext) override { delete InContext; }

		virtual void CreateActor(USkeletalMeshComponent* InOwnerComponent, UClothingAssetBase* InAsset, int32 SimDataIndex) override;
		virtual void DestroyActors() override;

		virtual bool ShouldSimulate() const override;
		virtual void Simulate(IClothingSimulationContext* InContext) override;
		virtual void GetSimulationData(TMap<int32, FClothSimulData>& OutData, USkeletalMeshComponent* InOwnerComponent, USkinnedMeshComponent* InOverrideComponent) const override;

		// Return bounds in local space (or in world space if InOwnerComponent is null).
		virtual FBoxSphereBounds GetBounds(const USkeletalMeshComponent* InOwnerComponent) const override;

		virtual void AddExternalCollisions(const FClothCollisionData& InData) override;
		virtual void ClearExternalCollisions() override;
		virtual void GetCollisions(FClothCollisionData& OutCollisions, bool bIncludeExternal = true) const override;
		// End of IClothingSimulation interface

	public:
		void SetGravityOverride(const FVector& InGravityOverride);
		void DisableGravityOverride();

		// Function to be called if any of the assets' configuration parameters have changed
		void RefreshClothConfig(const IClothingSimulationContext* InContext);
		// Function to be called if any of the assets' physics assets changes (colliders)
		// This seems to only happen when UPhysicsAsset::RefreshPhysicsAssetChange is called with
		// bFullClothRefresh set to false during changes created using the viewport manipulators.
		void RefreshPhysicsAsset();

		// IClothingSimulation interface
		virtual void SetNumIterations(int32 NumIterations) override;
		virtual void SetNumSubsteps(int32 NumSubsteps) override;
		virtual int32 GetNumCloths() const override { return NumCloths; }
		virtual int32 GetNumKinematicParticles() const override { return NumKinematicParticles; }
		virtual int32 GetNumDynamicParticles() const override { return NumDynamicParticles; }
		virtual int32 GetNumIterations() const override { return NumIterations; }
		virtual int32 GetNumSubsteps() const override { return NumSubsteps; }
		virtual FReal GetSimulationTime() const override { return SimulationTime; }
		virtual bool IsTeleported() const override { return bIsTeleported; }
		virtual void UpdateWorldForces(const USkeletalMeshComponent* OwnerComponent) override;
		// End of IClothingSimulation interface

		FClothingSimulationCloth* GetCloth(int32 ClothId);

#if WITH_EDITOR
		// FGCObject interface
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		// End of FGCObject interface

		// Editor only debug draw function
		CHAOSCLOTH_API void DebugDrawPhysMeshShaded(FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DebugDrawParticleIndices(FCanvas* Canvas, const FSceneView* SceneView) const;
		CHAOSCLOTH_API void DebugDrawElementIndices(FCanvas* Canvas, const FSceneView* SceneView) const;
		CHAOSCLOTH_API void DebugDrawMaxDistanceValues(FCanvas* Canvas, const FSceneView* SceneView) const;
#endif  // #if WITH_EDITOR

#if WITH_EDITOR || CHAOS_DEBUG_DRAW
		// Editor & runtime debug draw functions
		CHAOSCLOTH_API void DebugDrawPhysMeshWired(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawAnimMeshWired(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawPointNormals(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawInversedPointNormals(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawCollision(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawBackstops(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawBackstopDistances(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawMaxDistances(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawAnimDrive(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawBendingConstraint(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawLongRangeConstraint(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawWindForces(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawLocalSpace(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DebugDrawSelfCollision(FPrimitiveDrawInterface* PDI = nullptr) const;
#endif  // #if WITH_EDITOR || CHAOS_DEBUG_DRAW

	private:
		void ResetStats();
		void UpdateStats(const FClothingSimulationCloth* Cloth);

		void UpdateSimulationFromSharedSimConfig();

#if CHAOS_DEBUG_DRAW
		// Runtime only debug draw functions
		void DebugDrawBounds() const;
		void DebugDrawGravity() const;
#endif  // #if CHAOS_DEBUG_DRAW

	private:
		// Simulation objects
		TUniquePtr<FClothingSimulationSolver> Solver;  // Default solver
		TArray<TUniquePtr<FClothingSimulationMesh>> Meshes;
		TArray<TUniquePtr<FClothingSimulationCloth>> Cloths;
		TArray<TUniquePtr<FClothingSimulationCollider>> Colliders;

		// External collision Data
		FClothCollisionData ExternalCollisionData;

		// Shared cloth config
		UChaosClothSharedSimConfig* ClothSharedSimConfig;

		// Properties that must be readable from all threads
		TAtomic<int32> NumCloths;
		TAtomic<int32> NumKinematicParticles;
		TAtomic<int32> NumDynamicParticles;
		TAtomic<int32> NumIterations;
		TAtomic<int32> NumSubsteps;
		TAtomic<FReal> SimulationTime;
		TAtomic<bool> bIsTeleported;

		// Overrides
		bool bUseLocalSpaceSimulation;
		bool bUseGravityOverride;
		FVector GravityOverride;
		FReal MaxDistancesMultipliers;

#if WITH_EDITOR
		// Visualization material
		UMaterial* DebugClothMaterial;
		UMaterial* DebugClothMaterialVertex;
#endif

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		int32 StepCount;
		int32 ResetCount;
#endif
	};
} // namespace Chaos

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_GetSimData_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_GetSimData_ISPC_Enabled = true;
#else
extern bool bChaos_GetSimData_ISPC_Enabled;
#endif
