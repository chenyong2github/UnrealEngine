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
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
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
		void Initialize();
		void CreateActor(USkeletalMeshComponent* InOwnerComponent, UClothingAssetBase* InAsset, int32 SimDataIndex);
		IClothingSimulationContext* CreateContext() { return new ClothingSimulationContext(); }
		void FillContext(USkeletalMeshComponent* InComponent, float InDeltaTime, IClothingSimulationContext* InOutContext);
		void Shutdown() {}
		bool ShouldSimulate() const { return true; }
		void Simulate(IClothingSimulationContext* InContext);
		void DestroyActors() {}
		void DestroyContext(IClothingSimulationContext* InContext) { delete InContext; }
		void GetSimulationData(TMap<int32, FClothSimulData>& OutData, USkeletalMeshComponent* InOwnerComponent, USkinnedMeshComponent* InOverrideComponent) const;

		FBoxSphereBounds GetBounds(const USkeletalMeshComponent* InOwnerComponent) const
		{ return FBoxSphereBounds(Evolution->Particles().X().GetData(), Evolution->Particles().Size()); }

		void AddExternalCollisions(const FClothCollisionData& InData);
		void ClearExternalCollisions();
		void GetCollisions(FClothCollisionData& OutCollisions, bool bIncludeExternal = true) const;

	private:
		// UE Collision Data (Needed only for GetCollisions)
		TArray<Pair<uint32, FClothCollisionPrim_Sphere>> IndexAndSphereCollisionMap;
		TArray<Pair<uint32, FClothCollisionPrim_SphereConnection>> IndexAndCapsuleCollisionMap;
		TArray<Pair<uint32, FClothCollisionPrim_Convex>> IndexAndConvexCollisionMap;
		// Animation Data
		TArray<UClothingAssetCommon*> Assets;
		TArray<Chaos::TRigidTransform<float, 3>> OldAnimationTransforms;
		TArray<Chaos::TRigidTransform<float, 3>> AnimationTransforms;
		TArray<Chaos::TVector<float, 3>> OldAnimationPositions;
		TArray<Chaos::TVector<float, 3>> AnimationPositions;
		TArray<Chaos::TVector<float, 3>> AnimationNormals;
		Chaos::TArrayCollectionArray<int32> BoneIndices;
		Chaos::TArrayCollectionArray<Chaos::TRigidTransform<float, 3>> BaseTransforms;
		// Sim Data
		TArray<Chaos::TVector<uint32, 2>> IndexToRangeMap;

		TArray<TUniquePtr<Chaos::TTriangleMesh<float>>> Meshes;
		mutable TArray<TArray<Chaos::TVector<float, 3>>> FaceNormals;
		mutable TArray<TArray<Chaos::TVector<float, 3>>> PointNormals;

		TUniquePtr<Chaos::TPBDEvolution<float, 3>> Evolution;
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
		UMaterial* DebugClothMaterial;
#endif  // #if WITH_EDITOR
	};
} // namespace Chaos