// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosCloth/ChaosClothingSimulationCollider.h"
#include "ChaosCloth/ChaosClothingSimulationSolver.h"
#include "ChaosCloth/ChaosClothingSimulationCloth.h"
#include "ChaosCloth/ChaosClothPrivate.h"
#include "Chaos/Plane.h"
#include "Chaos/Sphere.h"
#include "Chaos/TaperedCylinder.h"
#include "Chaos/Convex.h"
#include "ClothingSimulation.h" // For context
#include "ClothingAsset.h"
#include "HAL/PlatformMath.h"
#include "Containers/ArrayView.h"
#include "Containers/BitArray.h"

DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Update Collider"), STAT_ChaosClothingSimulationColliderUpdate, STATGROUP_ChaosCloth);

using namespace Chaos;

void FClothingSimulationCollider::FLODData::Add(
	FClothingSimulationSolver* Solver,
	FClothingSimulationCloth* Cloth,
	const FClothCollisionData& InClothCollisionData,
	const TArray<int32>& UsedBoneIndices)
{
	check(Solver);

	// Keep a list of all collisions
	ClothCollisionData = InClothCollisionData;

	// Calculate the number of geometries
	int32 NumSpheres = ClothCollisionData.Spheres.Num();
	TBitArray<TInlineAllocator<8>> CapsuleEnds(false, NumSpheres);  // Init on stack for 256 spheres, any number over this will be allocated

	const int32 NumCapsules = ClothCollisionData.SphereConnections.Num();
	for (int32 Index = 0; Index < NumCapsules; ++Index)
	{
		const FClothCollisionPrim_SphereConnection& Connection = ClothCollisionData.SphereConnections[Index];
		for (int32 SphereIndex : Connection.SphereIndices)
		{
			FBitReference IsCapsuleEnd = CapsuleEnds[SphereIndex];
			if (!IsCapsuleEnd)
			{
				IsCapsuleEnd = true;
				--NumSpheres;
			}
		}
	}

	const uint32 NumConvexes = ClothCollisionData.Convexes.Num();
	const int32 NumBoxes = ClothCollisionData.Boxes.Num();
	NumGeometries = NumSpheres + NumCapsules + NumConvexes + NumBoxes;

	// Retrieve cloth group Id, or use INDEX_NONE if this collider applies to all cloths (when Cloth == nullptr)
	const uint32 GroupId = Cloth ? Cloth->GetGroupId() : INDEX_NONE;

	// The offset will be set to the first collision particle's index
	// Try to reuse existing offsets when Add is called during the solver's collider update
	const TPair<const FClothingSimulationSolver*, const FClothingSimulationCloth*> SolverClothPair(Solver, Cloth);
	int32* const OffsetPtr = Offsets.Find(SolverClothPair);
	const bool bIsNewCollider = !OffsetPtr;
	check(!Cloth || bIsNewCollider);  // TODO: We need to be able to update cloth colliders too at some point, but this won't work as it is
	int32& Offset = bIsNewCollider ? Offsets.Add(SolverClothPair) : *OffsetPtr;
	Offset = Solver->AddCollisionParticles(NumGeometries, GroupId, bIsNewCollider ? INDEX_NONE : Offset);

	// Capsules
	const int32 CapsuleOffset = Offset;
	if (NumCapsules)
	{
		int32* const BoneIndices = Solver->GetCollisionBoneIndices(CapsuleOffset);
		TRigidTransform<float, 3>* const BaseTransforms = Solver->GetCollisionBaseTransforms(CapsuleOffset);

		for (int32 Index = 0; Index < NumCapsules; ++Index)
		{
			const FClothCollisionPrim_SphereConnection& Connection = ClothCollisionData.SphereConnections[Index];

			const int32 SphereIndex0 = Connection.SphereIndices[0];
			const int32 SphereIndex1 = Connection.SphereIndices[1];
			checkSlow(SphereIndex0 != SphereIndex1);
			const FClothCollisionPrim_Sphere& Sphere0 = ClothCollisionData.Spheres[SphereIndex0];
			const FClothCollisionPrim_Sphere& Sphere1 = ClothCollisionData.Spheres[SphereIndex1];

			BoneIndices[Index] = GetMappedBoneIndex(UsedBoneIndices, Sphere0.BoneIndex);
			checkSlow(Sphere0.BoneIndex == Sphere1.BoneIndex);
			UE_CLOG(Sphere0.BoneIndex != Sphere1.BoneIndex,
				LogChaosCloth, Warning, TEXT("Found a legacy cloth asset with a collision capsule spanning across two bones. This is not supported with the current system."));
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Found collision capsule on bone index %d."), BoneIndices[Index]);

			const TVector<float, 3> X0 = Sphere0.LocalPosition;
			const TVector<float, 3> X1 = Sphere1.LocalPosition;
			const TVector<float, 3> Axis = X1 - X0;
			const float AxisSize = Axis.Size();

			const float Radius0 = Sphere0.Radius;
			const float Radius1 = Sphere1.Radius;
			float MinRadius, MaxRadius;
			if (Radius0 <= Radius1) { MinRadius = Radius0; MaxRadius = Radius1; }
			else { MinRadius = Radius1; MaxRadius = Radius0; }

			if (AxisSize < KINDA_SMALL_NUMBER)
			{
				// Sphere
				BaseTransforms[Index] = TRigidTransform<float, 3>::Identity;

				Solver->SetCollisionGeometry(CapsuleOffset, Index,
					MakeUnique<TSphere<float, 3>>(
						X0,
						MaxRadius));
			}
			else if (MaxRadius - MinRadius < KINDA_SMALL_NUMBER)
			{
				// Capsule
				const TVector<float, 3> Center = (X0 + X1) * 0.5f;  // Construct a capsule centered at the origin along the Z axis
				const TRotation<float, 3> Rotation = TRotation<float, 3>::FromRotatedVector(
					TVector<float, 3>::AxisVector(2),
					Axis.GetSafeNormal());

				BaseTransforms[Index] = TRigidTransform<float, 3>(Center, Rotation);

				const float HalfHeight = AxisSize * 0.5f;
				Solver->SetCollisionGeometry(CapsuleOffset, Index,
					MakeUnique<TCapsule<float>>(
						TVector<float, 3>(0.f, 0.f, -HalfHeight), // Min
						TVector<float, 3>(0.f, 0.f, HalfHeight), // Max
						MaxRadius));
			}
			else
			{
				// Tapered capsule
				BaseTransforms[Index] = TRigidTransform<float, 3>::Identity;

				TArray<TUniquePtr<FImplicitObject>> Objects;
				Objects.Reserve(3);
				Objects.Add(TUniquePtr<FImplicitObject>(
					new TTaperedCylinder<float>(X0, X1, Radius0, Radius1)));
				Objects.Add(TUniquePtr<FImplicitObject>(
					new TSphere<float, 3>(X0, Radius0)));
				Objects.Add(TUniquePtr<FImplicitObject>(
					new TSphere<float, 3>(X1, Radius1)));
				Solver->SetCollisionGeometry(CapsuleOffset, Index,
					MakeUnique<FImplicitObjectUnion>(MoveTemp(Objects)));  // TODO(Kriss.Gossart): Replace this once a TTaperedCapsule implicit type is implemented (note: this tapered cylinder with spheres is an approximation of a real tapered capsule)
			}
		}
	}

	// Spheres
	const int32 SphereOffset = CapsuleOffset + NumCapsules;
	if (NumSpheres != 0)
	{
		int32* const BoneIndices = Solver->GetCollisionBoneIndices(SphereOffset);
		TRigidTransform<float, 3>* const BaseTransforms = Solver->GetCollisionBaseTransforms(SphereOffset);

		for (int32 Index = 0, SphereIndex = 0; SphereIndex < ClothCollisionData.Spheres.Num(); ++SphereIndex)
		{
			// Skip spheres that are the end caps of capsules.
			if (CapsuleEnds[SphereIndex])
			//if (CapsuleEnds.Contains(SphereIndex))
			{
				continue;
			}

			const FClothCollisionPrim_Sphere& Sphere = ClothCollisionData.Spheres[SphereIndex];

			BoneIndices[Index] = GetMappedBoneIndex(UsedBoneIndices, Sphere.BoneIndex);
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Found collision sphere on bone index %d."), BoneIndices[Index]);

			BaseTransforms[Index] = TRigidTransform<float, 3>::Identity;

			Solver->SetCollisionGeometry(SphereOffset, Index,
				MakeUnique<TSphere<float, 3>>(
					Sphere.LocalPosition,
					Sphere.Radius));

			++Index;
		}
	}

	// Convexes
	const int32 ConvexOffset = SphereOffset + NumSpheres;
	if (NumConvexes != 0)
	{
		int32* const BoneIndices = Solver->GetCollisionBoneIndices(ConvexOffset);
		TRigidTransform<float, 3>* const BaseTransforms = Solver->GetCollisionBaseTransforms(ConvexOffset);

		for (uint32 Index = 0; Index < NumConvexes; ++Index)
		{
			const FClothCollisionPrim_Convex& Convex = ClothCollisionData.Convexes[Index];

			// Always initialize the collision particle transforms before setting any geometry as otherwise NaNs gets detected during the bounding box updates
			BaseTransforms[Index] = TRigidTransform<float, 3>::Identity;

			BoneIndices[Index] = GetMappedBoneIndex(UsedBoneIndices, Convex.BoneIndex);
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Found collision convex on bone index %d."), BoneIndices[Index]);

			TArray<TPlaneConcrete<float, 3>> Planes;

			const int32 NumSurfacePoints = Convex.SurfacePoints.Num();
			const int32 NumPlanes = Convex.Planes.Num();

			if (NumSurfacePoints < 4)
			{
				UE_LOG(LogChaosCloth, Warning, TEXT("Invalid convex collision: not enough surface points."));
			}
			else if (NumPlanes < 4)
			{
				UE_LOG(LogChaosCloth, Warning, TEXT("Invalid convex collision: not enough planes."));
			}
			else
			{
				// Retrieve convex planes
				Planes.Reserve(NumPlanes);
				for (const FPlane& Plane : Convex.Planes)
				{
					FPlane NormalizedPlane(Plane);
					if (NormalizedPlane.Normalize())
					{
						const TVector<float, 3> Normal(static_cast<FVector>(NormalizedPlane));
						const TVector<float, 3> Base = Normal * NormalizedPlane.W;

						Planes.Add(TPlaneConcrete<float, 3>(Base, Normal));
					}
					else
					{
						UE_LOG(LogChaosCloth, Warning, TEXT("Invalid convex collision: bad plane normal."));
						break;
					}
				}
			}

			if (Planes.Num() == NumPlanes)
			{
				// Retrieve particles
				TParticles<float, 3> SurfaceParticles;
				SurfaceParticles.Resize(NumSurfacePoints);
				for (int32 ParticleIndex = 0; ParticleIndex < NumSurfacePoints; ++ParticleIndex)
				{
					SurfaceParticles.X(ParticleIndex) = Convex.SurfacePoints[ParticleIndex];
				}

				// Setup the collision particle geometry
				Solver->SetCollisionGeometry(ConvexOffset, Index, MakeUnique<FConvex>(MoveTemp(Planes), MoveTemp(SurfaceParticles)));
			}
			else
			{
				UE_LOG(LogChaosCloth, Warning, TEXT("Replacing invalid convex collision by a default unit sphere."));
				Solver->SetCollisionGeometry(ConvexOffset, Index, MakeUnique<TSphere<float, 3>>(TVector<float, 3>(0.0f), 1.0f));  // Default to a unit sphere to replace the faulty convex
			}
		}
	}

	// Boxes
	const int32 BoxOffset = ConvexOffset + NumConvexes;
	if (NumBoxes != 0)
	{
		int32* const BoneIndices = Solver->GetCollisionBoneIndices(BoxOffset);
		TRigidTransform<float, 3>* const BaseTransforms = Solver->GetCollisionBaseTransforms(BoxOffset);
		
		for (int32 Index = 0; Index < NumBoxes; ++Index)
		{
			const FClothCollisionPrim_Box& Box = ClothCollisionData.Boxes[Index];
			
			BaseTransforms[Index] = TRigidTransform<float, 3>(Box.LocalPosition, Box.LocalRotation);
			
			BoneIndices[Index] = GetMappedBoneIndex(UsedBoneIndices, Box.BoneIndex);
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Found collision box on bone index %d."), BoneIndices[Index]);

			Solver->SetCollisionGeometry(BoxOffset, Index, MakeUnique<TBox<float, 3>>(-Box.HalfExtents, Box.HalfExtents));
		}
	}

	UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Added collisions: %d spheres, %d capsules, %d convexes, %d boxes."), NumSpheres, NumCapsules, NumConvexes, NumBoxes);
}

void FClothingSimulationCollider::FLODData::Update(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, const FClothingSimulationContextCommon* Context)
{
	check(Solver);
	if (NumGeometries)
	{
		const TPair<const FClothingSimulationSolver*, const FClothingSimulationCloth*> SolverClothPair(Solver, Cloth);
		const int32 Offset = Offsets.FindChecked(SolverClothPair);
		const int32* const BoneIndices = Solver->GetCollisionBoneIndices(Offset);
		const TRigidTransform<float, 3>* BaseTransforms = Solver->GetCollisionBaseTransforms(Offset);
		TRigidTransform<float, 3>* const CollisionTransforms = Solver->GetCollisionTransforms(Offset);

		const TArrayView<const FTransform> BoneTransforms = Context ? Context->BoneTransforms : TArrayView<const FTransform>();

		FTransform ComponentToLocalSpace = Context ? Context->ComponentToWorld : FTransform::Identity;
		ComponentToLocalSpace.AddToTranslation(-Solver->GetLocalSpaceLocation());
	
		// Update the collision transforms
		for (int32 Index = 0; Index < NumGeometries; ++Index)
		{
			const int32 BoneIndex = BoneIndices[Index];
			CollisionTransforms[Index] = BoneTransforms.IsValidIndex(BoneIndex) ?
				BaseTransforms[Index] * BoneTransforms[BoneIndex] * ComponentToLocalSpace :
				BaseTransforms[Index] * ComponentToLocalSpace;  // External collisions don't map to a bone
		}
	}
}

void FClothingSimulationCollider::FLODData::Enable(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, bool bEnable)
{
	check(Solver);
	if (NumGeometries)
	{
		const TPair<const FClothingSimulationSolver*, const FClothingSimulationCloth*> SolverClothPair(Solver, Cloth);
		const int32 Offset = Offsets.FindChecked(SolverClothPair);
		Solver->EnableCollisionParticles(Offset, bEnable);
	}
}

void FClothingSimulationCollider::FLODData::ResetStartPose(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth)
{
	check(Solver);
	if (NumGeometries)
	{
		const TPair<const FClothingSimulationSolver*, const FClothingSimulationCloth*> SolverClothPair(Solver, Cloth);
		const int32 Offset = Offsets.FindChecked(SolverClothPair);
		const TRigidTransform<float, 3>* const CollisionTransforms = Solver->GetCollisionTransforms(Offset);
		TRigidTransform<float, 3>* const OldCollisionTransforms = Solver->GetOldCollisionTransforms(Offset);
		TRotation<float, 3>* const Rs = Solver->GetCollisionParticleRs(Offset);
		TVector<float, 3>* const Xs = Solver->GetCollisionParticleXs(Offset);

		for (int32 Index = 0; Index < NumGeometries; ++Index)
		{
			OldCollisionTransforms[Index] = CollisionTransforms[Index];
			Xs[Index] = CollisionTransforms[Index].GetTranslation();
			Rs[Index] = CollisionTransforms[Index].GetRotation();
		}
	}
}

FClothingSimulationCollider::FClothingSimulationCollider(
	const UClothingAssetCommon* InAsset,
	const USkeletalMeshComponent* InSkeletalMeshComponent,
	bool bInUseLODIndexOverride,
	int32 InLODIndexOverride)
	: Asset(InAsset)
	, SkeletalMeshComponent(InSkeletalMeshComponent)
	, CollisionData(nullptr)
	, bUseLODIndexOverride(bInUseLODIndexOverride)
	, LODIndexOverride(InLODIndexOverride)
	, LODIndex(INDEX_NONE)
{
	// Prepare LOD array
	const int32 NumLODs = Asset ? Asset->LodData.Num() : 0;
	LODData.SetNum((int32)ECollisionDataType::LODs + NumLODs);
}

FClothingSimulationCollider::~FClothingSimulationCollider()
{
}

FClothCollisionData FClothingSimulationCollider::GetCollisionData() const
{
	FClothCollisionData ClothCollisionData;
	ClothCollisionData.Append(LODData[(int32)ECollisionDataType::Global].ClothCollisionData);
	ClothCollisionData.Append(LODData[(int32)ECollisionDataType::Dynamic].ClothCollisionData);
	if (LODIndex >= (int32)ECollisionDataType::LODs)
	{
		ClothCollisionData.Append(LODData[LODIndex].ClothCollisionData);
	}
	return ClothCollisionData;
}

void FClothingSimulationCollider::ExtractPhysicsAssetCollision(FClothCollisionData& ClothCollisionData, TArray<int32>& UsedBoneIndices)
{
	ClothCollisionData.Reset();
	UsedBoneIndices.Reset();

	if(!Asset)
	{
		return;
	}

	if (const UPhysicsAsset* const PhysAsset = Asset->PhysicsAsset)
	{
		const USkeletalMesh* const TargetMesh = CastChecked<USkeletalMesh>(Asset->GetOuter());

		UsedBoneIndices.Reserve(PhysAsset->SkeletalBodySetups.Num());

		for (const USkeletalBodySetup* BodySetup : PhysAsset->SkeletalBodySetups)
		{
			if (!BodySetup)
				continue;

			const int32 MeshBoneIndex = TargetMesh->RefSkeleton.FindBoneIndex(BodySetup->BoneName);
			const int32 MappedBoneIndex = UsedBoneIndices.Add(MeshBoneIndex);
			
			// Add capsules
			const FKAggregateGeom& AggGeom = BodySetup->AggGeom;
			if (AggGeom.SphylElems.Num())
			{
				for (const FKSphylElem& SphylElem : AggGeom.SphylElems)
				{
					if (SphylElem.Length == 0.0f)
					{
						// Add extracted sphere collision data
						FClothCollisionPrim_Sphere Sphere;
						Sphere.LocalPosition = SphylElem.Center;
						Sphere.Radius = SphylElem.Radius;
						Sphere.BoneIndex = MappedBoneIndex;
						ClothCollisionData.Spheres.Add(Sphere);
					}
					else
					{
						// Add extracted spheres collision data
						FClothCollisionPrim_Sphere Sphere0;
						FClothCollisionPrim_Sphere Sphere1;
						const FVector OrientedDirection = SphylElem.Rotation.RotateVector(FVector::UpVector);
						const FVector HalfDim = OrientedDirection * (SphylElem.Length / 2.f);
						Sphere0.LocalPosition = SphylElem.Center - HalfDim;
						Sphere1.LocalPosition = SphylElem.Center + HalfDim;
						Sphere0.Radius = SphylElem.Radius;
						Sphere1.Radius = SphylElem.Radius;
						Sphere0.BoneIndex = MappedBoneIndex;
						Sphere1.BoneIndex = MappedBoneIndex;

						// Add extracted sphere connection collision data
						FClothCollisionPrim_SphereConnection SphereConnection;
						SphereConnection.SphereIndices[0] = ClothCollisionData.Spheres.Add(Sphere0);
						SphereConnection.SphereIndices[1] = ClothCollisionData.Spheres.Add(Sphere1);
						ClothCollisionData.SphereConnections.Add(SphereConnection);
					}
				}
			}

			// Add spheres
			for (const FKSphereElem& SphereElem : AggGeom.SphereElems)
			{
				// Add extracted sphere collision data
				FClothCollisionPrim_Sphere Sphere;
				Sphere.LocalPosition = SphereElem.Center;
				Sphere.Radius = SphereElem.Radius;
				Sphere.BoneIndex = MappedBoneIndex;
				ClothCollisionData.Spheres.Add(Sphere);
			}

			// Add boxes
			for (const FKBoxElem& BoxElem : AggGeom.BoxElems)
			{
				// Add extracted box collision data
				FClothCollisionPrim_Box Box;
				Box.LocalPosition = BoxElem.Center;
				Box.LocalRotation = BoxElem.Rotation.Quaternion();
				Box.HalfExtents = FVector(BoxElem.X, BoxElem.Y, BoxElem.Z) * 0.5f;
				Box.BoneIndex = MappedBoneIndex;
				ClothCollisionData.Boxes.Add(Box);
			}

			// Add tapered capsules
			for (const FKTaperedCapsuleElem& TaperedCapsuleElem : AggGeom.TaperedCapsuleElems)
			{
				if (TaperedCapsuleElem.Length == 0)
				{
					// Add extracted sphere collision data
					FClothCollisionPrim_Sphere Sphere;
					Sphere.LocalPosition = TaperedCapsuleElem.Center;
					Sphere.Radius = FMath::Max(TaperedCapsuleElem.Radius0, TaperedCapsuleElem.Radius1);
					Sphere.BoneIndex = MappedBoneIndex;
					ClothCollisionData.Spheres.Add(Sphere);
				}
				else
				{
					// Add extracted spheres collision data
					FClothCollisionPrim_Sphere Sphere0;
					FClothCollisionPrim_Sphere Sphere1;
					const FVector OrientedDirection = TaperedCapsuleElem.Rotation.RotateVector(FVector::UpVector);
					const FVector HalfDim = OrientedDirection * (TaperedCapsuleElem.Length / 2.f);
					Sphere0.LocalPosition = TaperedCapsuleElem.Center + HalfDim;
					Sphere1.LocalPosition = TaperedCapsuleElem.Center - HalfDim;
					Sphere0.Radius = TaperedCapsuleElem.Radius0;
					Sphere1.Radius = TaperedCapsuleElem.Radius1;
					Sphere0.BoneIndex = MappedBoneIndex;
					Sphere1.BoneIndex = MappedBoneIndex;

					// Add extracted sphere connection collision data
					FClothCollisionPrim_SphereConnection SphereConnection;
					SphereConnection.SphereIndices[0] = ClothCollisionData.Spheres.Add(Sphere0);
					SphereConnection.SphereIndices[1] = ClothCollisionData.Spheres.Add(Sphere1);
					ClothCollisionData.SphereConnections.Add(SphereConnection);
				}
			}

#if !PLATFORM_LUMIN && !PLATFORM_ANDROID  // TODO(Kriss.Gossart): Compile on Android and fix whatever errors the following code is causing
			// Add convexes
			for (const FKConvexElem& ConvexElem : AggGeom.ConvexElems)
			{
				// Add stub for extracted collision data
				FClothCollisionPrim_Convex Convex;
				Convex.BoneIndex = MappedBoneIndex;
#if PHYSICS_INTERFACE_PHYSX
				// Collision bodies are stored in PhysX specific data structures so they can only be imported if we enable PhysX.
				const physx::PxConvexMesh* const PhysXMesh = ConvexElem.GetConvexMesh();  // TODO(Kriss.Gossart): Deal with this legacy structure in a different place, so that there's only TConvex
				const int32 NumPolygons = int32(PhysXMesh->getNbPolygons());
				Convex.Planes.SetNumUninitialized(NumPolygons);
				for (int32 i = 0; i < NumPolygons; ++i)
				{
					physx::PxHullPolygon Poly;
					PhysXMesh->getPolygonData(i, Poly);
					check(Poly.mNbVerts == 3);
					const auto Indices = PhysXMesh->getIndexBuffer() + Poly.mIndexBase;

					Convex.Planes[i] = FPlane(
						ConvexElem.VertexData[Indices[0]],
						ConvexElem.VertexData[Indices[1]],
						ConvexElem.VertexData[Indices[2]]);
				}

				// Rebuild surface points
				Convex.RebuildSurfacePoints();

#elif WITH_CHAOS  // #if PHYSICS_INTERFACE_PHYSX
				const FImplicitObject& ChaosConvexMesh = *ConvexElem.GetChaosConvexMesh();
				const FConvex& ChaosConvex = ChaosConvexMesh.GetObjectChecked<FConvex>();

				// Copy planes
				const TArray<TPlaneConcrete<float, 3>>& Planes = ChaosConvex.GetFaces();
				Convex.Planes.Reserve(Planes.Num());
				for (const TPlaneConcrete<float, 3>& Plane : Planes)
				{
					Convex.Planes.Add(FPlane(Plane.X(), Plane.Normal()));
				}

				// Copy surface points
				const uint32 NumSurfacePoints = ChaosConvex.GetSurfaceParticles().Size();
				Convex.SurfacePoints.Reserve(NumSurfacePoints);
				for (uint32 ParticleIndex = 0; ParticleIndex < NumSurfacePoints; ++ParticleIndex)
				{
					Convex.SurfacePoints.Add(ChaosConvex.GetSurfaceParticles().X(ParticleIndex));
				}
#endif  // #if PHYSICS_INTERFACE_PHYSX #elif WITH_CHAOS

				// Add extracted collision data
				ClothCollisionData.Convexes.Add(Convex);
			}
#endif  // #if !PLATFORM_LUMIN && !PLATFORM_ANDROID

		}  // End for PhysAsset->SkeletalBodySetups
	}  // End if Asset->PhysicsAsset
}

void FClothingSimulationCollider::Add(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth)
{
	check(Solver);

	// Reset LOD
	LODIndex = INDEX_NONE;  // TODO: Have map of LODIndex per solver

	// Create physics asset collisions, this will affect all LODs, so store it at index 0
	FClothCollisionData PhysicsAssetCollisionData;
	TArray<int32> UsedBoneIndices;
	ExtractPhysicsAssetCollision(PhysicsAssetCollisionData, UsedBoneIndices);

	LODData[(int32)ECollisionDataType::Global].Add(Solver, Cloth, PhysicsAssetCollisionData, UsedBoneIndices);

	// Create user collision data
	LODData[(int32)ECollisionDataType::Dynamic].Add(Solver, Cloth, CollisionData ? *CollisionData : FClothCollisionData());

	// Create legacy asset LOD collisions
	const int32 NumLODs = Asset ? Asset->LodData.Num() : 0;
	for (int32 Index = 0; Index < NumLODs; ++Index)
	{
		// Warn about legacy apex collisions
		const FClothCollisionData& AssetCollisionData = Asset->LodData[Index].CollisionData;
		UE_CLOG(AssetCollisionData.Spheres.Num() > 0 || AssetCollisionData.SphereConnections.Num() > 0 || AssetCollisionData.Convexes.Num() > 0,
			LogChaosCloth, Warning, TEXT(
				"Actor '%s' component '%s' has %d sphere, %d capsule, and %d convex collision objects for"
				"physics authored as part of a LOD construct, probably by the Apex cloth authoring system."
				"This is deprecated. Please update your asset!"),
			SkeletalMeshComponent->GetOwner() ? *SkeletalMeshComponent->GetOwner()->GetName() : TEXT("None"),
			*SkeletalMeshComponent->GetName(),
			AssetCollisionData.Spheres.Num(),
			AssetCollisionData.SphereConnections.Num(),
			AssetCollisionData.Convexes.Num());

		// Add legacy collision
		LODData[(int32)ECollisionDataType::LODs + Index].Add(Solver, Cloth, AssetCollisionData, Asset->UsedBoneIndices);
	}
}

void FClothingSimulationCollider::Update(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth)
{
	check(Solver);

	SCOPE_CYCLE_COUNTER(STAT_ChaosClothingSimulationColliderUpdate);

	// Retrieve context (can be null when no skeletal mesh component is specified)
	const FClothingSimulationContextCommon* const Context = SkeletalMeshComponent ? static_cast<const FClothingSimulationContextCommon*>(SkeletalMeshComponent->GetClothingSimulationContext()) : nullptr;

	// Update the collision transforms
	// Note: All the LODs are updated at once, so that the previous transforms are always correct during LOD switching
	for (FLODData& LODDatum : LODData)
	{
		LODDatum.Update(Solver, Cloth, Context);
	}

	// Update current LOD index
	const int32 PrevLODIndex = LODIndex;
	LODIndex = (int32)ECollisionDataType::LODs + (bUseLODIndexOverride ? LODIndexOverride : Cloth ? Cloth->GetLODIndex(Solver) : INDEX_NONE);
	if (!LODData.IsValidIndex(LODIndex))
	{
		LODIndex = (int32)ECollisionDataType::LODs + INDEX_NONE;
	}

	// Enable/disable particle LOD
	if (LODIndex != PrevLODIndex)
	{
		if (PrevLODIndex == INDEX_NONE)
		{
			// Enable non LOD collisions at first initialization
			LODData[(int32)ECollisionDataType::Global].Enable(Solver, Cloth, true);

			// Update initial state for collisions
			LODData[(int32)ECollisionDataType::Global].ResetStartPose(Solver, Cloth);
		}
		else if (PrevLODIndex > 0)
		{
			// Disable previous LOD (can't be index 0, since it is not a LOD and therefore is always left enabled)
			LODData[PrevLODIndex].Enable(Solver, Cloth, false);
		}
		if (LODIndex >= (int32)ECollisionDataType::LODs)
		{
			// Enable new LOD
			LODData[LODIndex].Enable(Solver, Cloth, true);

			if (PrevLODIndex == INDEX_NONE)
			{
				// Update initial state for collisions
				LODData[LODIndex].ResetStartPose(Solver, Cloth);
			}
		}
	}
}

TConstArrayView<TVector<float, 3>> FClothingSimulationCollider::GetCollisionTranslations(const FClothingSimulationSolver* Solver, const FClothingSimulationCloth* Cloth, ECollisionDataType CollisionDataType) const
{
	check(Solver);

	const int32 Index = 
		(CollisionDataType < ECollisionDataType::LODs) ? (int32)CollisionDataType :
		(LODIndex >= (int32)ECollisionDataType::LODs) ? LODIndex : INDEX_NONE;

	const TPair<const FClothingSimulationSolver*, const FClothingSimulationCloth*> SolverClothPair(Solver, Cloth);

	return (Index != INDEX_NONE && LODData[Index].NumGeometries) ?
		TConstArrayView<TVector<float, 3>>(Solver->GetCollisionParticleXs(LODData[Index].Offsets.FindChecked(SolverClothPair)), LODData[Index].NumGeometries) :
		TConstArrayView<TVector<float, 3>>();
}

TConstArrayView<TRotation<float, 3>> FClothingSimulationCollider::GetCollisionRotations(const FClothingSimulationSolver* Solver, const FClothingSimulationCloth* Cloth, ECollisionDataType CollisionDataType) const
{
	check(Solver);
	const int32 Index = 
		(CollisionDataType < ECollisionDataType::LODs) ? (int32)CollisionDataType :
		(LODIndex >= (int32)ECollisionDataType::LODs) ? LODIndex : INDEX_NONE;

	const TPair<const FClothingSimulationSolver*, const FClothingSimulationCloth*> SolverClothPair(Solver, Cloth);

	return (Index != INDEX_NONE && LODData[Index].NumGeometries) ?
		TConstArrayView<TRotation<float, 3>>(Solver->GetCollisionParticleRs(LODData[Index].Offsets.FindChecked(SolverClothPair)), LODData[Index].NumGeometries) :
		TConstArrayView<TRotation<float, 3>>();
}

TConstArrayView<TUniquePtr<FImplicitObject>> FClothingSimulationCollider::GetCollisionGeometries(const FClothingSimulationSolver* Solver, const FClothingSimulationCloth* Cloth, ECollisionDataType CollisionDataType) const
{
	check(Solver);
	const int32 Index = 
		(CollisionDataType < ECollisionDataType::LODs) ? (int32)CollisionDataType :
		(LODIndex >= (int32)ECollisionDataType::LODs) ? LODIndex : INDEX_NONE;

	const TPair<const FClothingSimulationSolver*, const FClothingSimulationCloth*> SolverClothPair(Solver, Cloth);

	return (Index != INDEX_NONE && LODData[Index].NumGeometries) ?
		TConstArrayView<TUniquePtr<FImplicitObject>>(Solver->GetCollisionGeometries(LODData[Index].Offsets.FindChecked(SolverClothPair)), LODData[Index].NumGeometries) :
		TConstArrayView<TUniquePtr<FImplicitObject>>();
}
