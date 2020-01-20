// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosCloth/ChaosClothingSimulation.h"
#include "ChaosCloth/ChaosClothPrivate.h"

#include "Async/ParallelFor.h"
#include "ClothingAsset.h"
#include "ClothingSimulation.h" // ClothingSystemRuntimeInterface
#include "Utils/ClothingMeshUtils.h" // ClothingSystemRuntimeCommon
#include "Components/SkeletalMeshComponent.h"
#include "Materials/Material.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Cylinder.h"
#include "Chaos/ImplicitObjectIntersection.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Levelset.h"
#include "Chaos/PBDAnimDriveConstraint.h"
#include "Chaos/PBDAxialSpringConstraints.h"
#include "Chaos/PBDBendingConstraints.h"
#include "Chaos/PBDLongRangeConstraints.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDSphericalConstraint.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PBDVolumeConstraint.h"
#include "Chaos/PBDShapeConstraints.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/Plane.h"
#include "Chaos/Sphere.h"
#include "Chaos/TaperedCylinder.h"
#include "Chaos/Convex.h"
#include "Chaos/Transform.h"
#include "Chaos/Utilities.h"
#include "Chaos/Vector.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "ChaosCloth/ChaosWeightMapTarget.h"
#include "ChaosCloth/ChaosClothPrivate.h"

#include "Chaos/XPBDLongRangeConstraints.h"
#include "Chaos/XPBDSpringConstraints.h"
#include "Chaos/XPBDAxialSpringConstraints.h"

#if WITH_PHYSX && !PLATFORM_LUMIN && !PLATFORM_ANDROID
#include "PhysXIncludes.h"
#endif

using namespace Chaos;

DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Simulate"), STAT_ChaosClothSimulate, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Get Animation Data"), STAT_ChaosClothGetAnimationData, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Update Collision Transforms"), STAT_ChaosClothUpdateCollisionTransforms, STATGROUP_ChaosCloth);

// Default parameters, will be overwritten when cloth assets are loaded
namespace ClothingSimulationDefault
{
	static const FVector Gravity(0.f, 0.f, -980.665f);
	static const int32 NumIterations = 1;
	static const float SelfCollisionThickness = 2.f;
	static const float CollisionThickness = 1.2f;
	static const float CoefficientOfFriction = 0.f;
	static const float Damping = 0.01f;
	static const float WindDrag = 0.f;
}

ClothingSimulation::ClothingSimulation()
	: ClothSharedSimConfig(nullptr)
	, ExternalCollisionsOffset(0)
	, Gravity(ClothingSimulationDefault::Gravity)
{
#if WITH_EDITOR
	DebugClothMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EditorMaterials/Cloth/CameraLitDoubleSided.CameraLitDoubleSided"), nullptr, LOAD_None, nullptr);  // LOAD_EditorOnly
	DebugClothMaterialVertex = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EditorMaterials/WidgetVertexColorMaterial"), nullptr, LOAD_None, nullptr);  // LOAD_EditorOnly
#endif  // #if WITH_EDITOR
}

ClothingSimulation::~ClothingSimulation()
{}

void ClothingSimulation::Initialize()
{
    Chaos::TPBDParticles<float, 3> LocalParticles;
    Chaos::TKinematicGeometryClothParticles<float, 3> TRigidParticles;
    Evolution.Reset(
		new Chaos::TPBDEvolution<float, 3>(
			MoveTemp(LocalParticles),
			MoveTemp(TRigidParticles),
			{}, // CollisionTriangles
			ClothingSimulationDefault::NumIterations,
			ClothingSimulationDefault::CollisionThickness,
			ClothingSimulationDefault::SelfCollisionThickness,
			ClothingSimulationDefault::CoefficientOfFriction,
			ClothingSimulationDefault::Damping));
    Evolution->CollisionParticles().AddArray(&BoneIndices);
	Evolution->CollisionParticles().AddArray(&BaseTransforms);
	Evolution->GetVelocityField().SetDrag(ClothingSimulationDefault::WindDrag);  // TODO: Per particle drag
    Evolution->GetGravityForces().SetAcceleration(Gravity);

    Evolution->SetKinematicUpdateFunction(
		[this](Chaos::TPBDParticles<float, 3>& ParticlesInput, const float Dt, const float LocalTime, const int32 Index)
		{
			if (!OldAnimationPositions.IsValidIndex(Index) || ParticlesInput.InvM(Index) > 0)
				return;
			const float Alpha = (LocalTime - Time) / DeltaTime;
			ParticlesInput.X(Index) = Alpha * AnimationPositions[Index] + (1.f - Alpha) * OldAnimationPositions[Index];
		});

	Evolution->SetCollisionKinematicUpdateFunction(
//		[&](Chaos::TKinematicGeometryParticles<float, 3>& ParticlesInput, const float Dt, const float LocalTime, const int32 Index)
		[this](Chaos::TKinematicGeometryClothParticles<float, 3>& ParticlesInput, const float Dt, const float LocalTime, const int32 Index)
		{
			checkSlow(Dt > SMALL_NUMBER && DeltaTime > SMALL_NUMBER);
			const float Alpha = (LocalTime - Time) / DeltaTime;
			const Chaos::TVector<float, 3> NewX =
				Alpha * CollisionTransforms[Index].GetTranslation() + (1.f - Alpha) * OldCollisionTransforms[Index].GetTranslation();
			ParticlesInput.V(Index) = (NewX - ParticlesInput.X(Index)) / Dt;
			ParticlesInput.X(Index) = NewX;
			Chaos::TRotation<float, 3> NewR = FQuat::Slerp(OldCollisionTransforms[Index].GetRotation(), CollisionTransforms[Index].GetRotation(), Alpha);
			Chaos::TRotation<float, 3> Delta = NewR * ParticlesInput.R(Index).Inverse();
			Chaos::TVector<float, 3> Axis;
			float Angle;
			Delta.ToAxisAndAngle(Axis, Angle);
			ParticlesInput.W(Index) = Axis * Angle / Dt;
			ParticlesInput.R(Index) = NewR;
		});

    Time = 0.f;
}

void ClothingSimulation::Shutdown()
{
	Assets.Reset();
	AnimDriveSpringStiffness.Reset();
	ExternalCollisions.Reset();
	OldCollisionTransforms.Reset();
	CollisionTransforms.Reset();
	BoneIndices.Reset();
	BaseTransforms.Reset();
	OldAnimationPositions.Reset();
	AnimationPositions.Reset();
	AnimationNormals.Reset();
	IndexToRangeMap.Reset();
	RootBoneWorldTransforms.Reset();
	Meshes.Reset();
	FaceNormals.Reset();
	PointNormals.Reset();
	Evolution.Reset();
	ExternalCollisionsOffset = 0;
	ClothSharedSimConfig = nullptr;
}

void ClothingSimulation::DestroyActors()
{
	Shutdown();
	Initialize();
}

void ClothingSimulation::CreateActor(USkeletalMeshComponent* InOwnerComponent, UClothingAssetBase* InAsset, int32 InSimDataIndex)
{
	UE_LOG(LogChaosCloth, Verbose, TEXT("Adding Cloth LOD asset to %s in sim slot %d"), InOwnerComponent->GetOwner() ? *InOwnerComponent->GetOwner()->GetName() : TEXT("None"), InSimDataIndex);

	//Evolution->SetCCD(ChaosClothSimConfig->bUseContinuousCollisionDetection);
	//Evolution->SetCCD(true); // ryan!!!

	UClothingAssetCommon* Asset = Cast<UClothingAssetCommon>(InAsset);
	const UChaosClothConfig* const ChaosClothSimConfig = Asset->GetClothConfig<UChaosClothConfig>();
	if (!ChaosClothSimConfig)
	{
		UE_LOG(LogChaosCloth, Warning, TEXT("Missing Chaos config Cloth LOD asset to %s in sim slot %d"), InOwnerComponent->GetOwner() ? *InOwnerComponent->GetOwner()->GetName() : TEXT("None"), InSimDataIndex);
		return;
	}

	ClothingSimulationContext Context;
	FillContext(InOwnerComponent, 0, &Context);

	if (Assets.Num() <= InSimDataIndex)
	{
		Assets.SetNumZeroed(InSimDataIndex + 1);
		AnimDriveSpringStiffness.SetNumZeroed(InSimDataIndex + 1);
	}
	Assets[InSimDataIndex] = Asset;

	if (Meshes.Num() <= InSimDataIndex)
	{
		Meshes.SetNum(InSimDataIndex + 1);
		FaceNormals.SetNum(InSimDataIndex + 1);
		PointNormals.SetNum(InSimDataIndex + 1);
	}

	check(Asset->GetNumLods() > 0);
	UE_CLOG(Asset->GetNumLods() != 1,
		LogChaosCloth, Warning, TEXT("More than one LOD with the current cloth asset %s in sim slot %d. Only LOD 0 is supported with Chaos Cloth for now."),
		InOwnerComponent->GetOwner() ? *InOwnerComponent->GetOwner()->GetName() : TEXT("None"), InSimDataIndex);
	const UClothLODDataCommon* const AssetLodData = Asset->ClothLodData[0];
	const FClothPhysicalMeshData& PhysMesh = AssetLodData->ClothPhysicalMeshData;

	// Add particles
	TPBDParticles<float, 3>& Particles = Evolution->Particles();
	const uint32 Offset = Particles.Size();
	Particles.AddParticles(PhysMesh.Vertices.Num());

	if (IndexToRangeMap.Num() <= InSimDataIndex)
	{
		IndexToRangeMap.SetNum(InSimDataIndex + 1);
		RootBoneWorldTransforms.SetNum(InSimDataIndex + 1);
	}
	IndexToRangeMap[InSimDataIndex] = Chaos::TVector<uint32, 2>(Offset, Particles.Size());
	RootBoneWorldTransforms[InSimDataIndex] = Context.BoneTransforms[Asset->ReferenceBoneIndex] * Context.ComponentToWorld;

	// ClothSharedSimConfig should either be a nullptr, or point to an object common to the whole skeletal mesh
	if (ClothSharedSimConfig == nullptr)
	{
		ClothSharedSimConfig = Asset->GetClothConfig<UChaosClothSharedSimConfig>();
	}
	else
	{
		check(ClothSharedSimConfig == Asset->GetClothConfig<UChaosClothSharedSimConfig>());
	}

	AnimationPositions.SetNum(Particles.Size());
	AnimationNormals.SetNum(Particles.Size());

	ClothingMeshUtils::SkinPhysicsMesh<true, false>(
		Asset->UsedBoneIndices,
		PhysMesh, // curr pos and norm
		Context.ComponentToWorld,
		Context.RefToLocals.GetData(),
		Context.RefToLocals.Num(),
		reinterpret_cast<TArray<FVector>&>(AnimationPositions),
		reinterpret_cast<TArray<FVector>&>(AnimationNormals),
		Offset);

	ResetParticles(InSimDataIndex);

	OldAnimationPositions = AnimationPositions;  // Also update the old positions array to avoid any interpolation issues

	BuildMesh(PhysMesh, InSimDataIndex);

	SetParticleMasses(ChaosClothSimConfig, PhysMesh, InSimDataIndex);

	AddConstraints(ChaosClothSimConfig, PhysMesh, InSimDataIndex);

	// Add Self Collisions
	if (ChaosClothSimConfig->bUseSelfCollisions)
	{
		AddSelfCollisions(InSimDataIndex);
	}

	// Warn about legacy apex collisions
	const FClothCollisionData& LodCollData = AssetLodData->CollisionData;
	UE_CLOG(LodCollData.Spheres.Num() > 0 || LodCollData.SphereConnections.Num() > 0 || LodCollData.Convexes.Num() > 0,
		LogChaosCloth, Warning, TEXT(
			"Actor '%s' component '%s' has %d sphere, %d capsule, and %d "
			"convex collision objects for physics authored as part of a LOD construct, "
			"probably by the Apex cloth authoring system.  This is deprecated.  "
			"Please update your asset!"),
		InOwnerComponent->GetOwner() ? *InOwnerComponent->GetOwner()->GetName() : TEXT("None"),
		*InOwnerComponent->GetName(),
		LodCollData.Spheres.Num(),
		LodCollData.SphereConnections.Num(),
		LodCollData.Convexes.Num());

	// Extract all collisions for this asset
	checkf(ExternalCollisions.Spheres.Num() == 0 &&
		ExternalCollisions.SphereConnections.Num() == 0 &&
		ExternalCollisions.Convexes.Num() == 0 &&
		ExternalCollisions.Boxes.Num() == 0, TEXT("There cannot be any external collisions added before all the cloth assets collisions are processed."));
	ExtractCollisions(Asset);

	// Update collision transforms, including initial state for particles' X & R
	UpdateCollisionTransforms(Context, /*bReinit =*/ false);  // Reinit is false, since we only need to update the new collision particles
}

void ClothingSimulation::ExtractCollisions(const UClothingAssetCommon* Asset)
{
	// Pull collisions from the specified physics asset inside the clothing asset
	ExtractPhysicsAssetCollisions(Asset);

	// Extract the legacy Apex collision from the clothing asset
	ExtractLegacyAssetCollisions(Asset);

	// Update the external collision offset
	ExternalCollisionsOffset = Evolution->CollisionParticles().Size();
}

void ClothingSimulation::PostActorCreationInitialize()
{
	UpdateSimulationFromSharedSimConfig();
}

void ClothingSimulation::UpdateSimulationFromSharedSimConfig()
{
	if (ClothSharedSimConfig) // ClothSharedSimConfig will be a null pointer if all cloth instances are disabled in which case we will use default Evolution parameters
	{
		// Now set all the common parameters on the simulation
		Evolution->SetIterations(ClothSharedSimConfig->IterationCount);
		Evolution->SetSelfCollisionThickness(ClothSharedSimConfig->SelfCollisionThickness);
		Evolution->SetCollisionThickness(ClothSharedSimConfig->CollisionThickness);
		Evolution->SetDamping(ClothSharedSimConfig->Damping);
		Evolution->GetVelocityField().SetDrag(ClothSharedSimConfig->WindDrag);  // TODO: Per particle drag
		Gravity = ClothSharedSimConfig->Gravity;
	}
}

void ClothingSimulation::BuildMesh(const FClothPhysicalMeshData& InPhysMesh, int32 InSimDataIndex)
{
	TUniquePtr<Chaos::TTriangleMesh<float>>& Mesh = Meshes[InSimDataIndex];

	const uint32 Offset = IndexToRangeMap[InSimDataIndex][0];

	const int32 NumTriangles = InPhysMesh.Indices.Num() / 3;
	TArray<Chaos::TVector<int32, 3>> InputSurfaceElements;
	InputSurfaceElements.Reserve(NumTriangles);
	for (int i = 0; i < NumTriangles; ++i)
	{
		const int32 Index = 3 * i;
		InputSurfaceElements.Add(
			{ static_cast<int32>(Offset + InPhysMesh.Indices[Index]),
			 static_cast<int32>(Offset + InPhysMesh.Indices[Index + 1]),
			 static_cast<int32>(Offset + InPhysMesh.Indices[Index + 2]) });
	}
	check(InputSurfaceElements.Num() == NumTriangles);
	Mesh.Reset(new Chaos::TTriangleMesh<float>(MoveTemp(InputSurfaceElements)));
	check(Mesh->GetNumElements() == NumTriangles);
	Mesh->GetPointToTriangleMap(); // Builds map for later use by GetPointNormals()
}

void ClothingSimulation::ResetParticles(int32 InSimDataIndex)
{
	const uint32 Offset = IndexToRangeMap[InSimDataIndex][0];
	const uint32 Range = IndexToRangeMap[InSimDataIndex][1];

	for (uint32 i = Offset; i < Range; ++i)
	{
		Evolution->Particles().P(i) = Evolution->Particles().X(i) = AnimationPositions[i];
		Evolution->Particles().V(i) = Chaos::TVector<float, 3>(0.f);
		Evolution->Particles().M(i) = 0.f;
	}
}

void ClothingSimulation::SetParticleMasses(const UChaosClothConfig* ChaosClothConfig, const FClothPhysicalMeshData& PhysMesh, int32 InSimDataIndex)
{
	const Chaos::TTriangleMesh<float>& Mesh = *Meshes[InSimDataIndex];
	TPBDParticles<float, 3>& Particles = Evolution->Particles();

	// Assign per particle mass proportional to connected area.
	const TArray<TVector<int32, 3>>& SurfaceElements = Mesh.GetSurfaceElements();
	float TotalArea = 0.0;
	for (const Chaos::TVector<int32, 3>& Tri : SurfaceElements)
	{
		const float TriArea = 0.5 * Chaos::TVector<float, 3>::CrossProduct(
			Particles.X(Tri[1]) - Particles.X(Tri[0]),
			Particles.X(Tri[2]) - Particles.X(Tri[0])).Size();
		TotalArea += TriArea;
		const float ThirdTriArea = TriArea / 3.0;
		Particles.M(Tri[0]) += ThirdTriArea;
		Particles.M(Tri[1]) += ThirdTriArea;
		Particles.M(Tri[2]) += ThirdTriArea;
	}
	const TSet<int32> Vertices = Mesh.GetVertices();
	switch (ChaosClothConfig->MassMode)
	{
	case EClothMassMode::UniformMass:
		for (const int32 Vertex : Vertices)
		{
			Particles.M(Vertex) = ChaosClothConfig->UniformMass;
		}
		break;
	case EClothMassMode::TotalMass:
	{
		const float MassPerUnitArea = TotalArea > 0.0 ? ChaosClothConfig->TotalMass / TotalArea : 1.0;
		for (const int32 Vertex : Vertices)
		{
			Particles.M(Vertex) *= MassPerUnitArea;
		}
		break;
	}
	case EClothMassMode::Density:
		for (const int32 Vertex : Vertices)
		{
			Particles.M(Vertex) *= ChaosClothConfig->Density;
		}
		break;
	};

	// Clamp and enslave
	const FPointWeightMap& MaxDistances = PhysMesh.GetWeightMap(EChaosWeightMapTarget::MaxDistance);
	const uint32 Offset = IndexToRangeMap[InSimDataIndex][0];
	const uint32 Range = IndexToRangeMap[InSimDataIndex][1];

	check(Particles.Size() >= Range);
	for (uint32 i = Offset; i < Range; i++)
	{
		Particles.M(i) = FMath::Max(Particles.M(i), ChaosClothConfig->MinPerParticleMass);
		Particles.InvM(i) = MaxDistances.IsBelowThreshold(i - Offset) ? 0.f : 1.f / Particles.M(i);
	}
}

void ClothingSimulation::AddConstraints(const UChaosClothConfig* ChaosClothSimConfig, const FClothPhysicalMeshData& PhysMesh, int32 InSimDataIndex)
{
	const Chaos::TTriangleMesh<float>& Mesh = *Meshes[InSimDataIndex];
	const TArray<TVector<int32, 3>>& SurfaceElements = Mesh.GetSurfaceElements();

	const uint32 Offset = IndexToRangeMap[InSimDataIndex][0];
	const uint32 ParticleCount = IndexToRangeMap[InSimDataIndex][1] - Offset;

	const bool bUseXPBDConstraints = ClothSharedSimConfig && ClothSharedSimConfig->bUseXPBDConstraints;

	if (ChaosClothSimConfig->ShapeTargetStiffness)
	{
		check(ChaosClothSimConfig->ShapeTargetStiffness > 0.f && ChaosClothSimConfig->ShapeTargetStiffness <= 1.f);
		Evolution->AddPBDConstraintFunction([ShapeConstraints = Chaos::TPBDShapeConstraints<float, 3>(Evolution->Particles(), Offset, ParticleCount, AnimationPositions, ChaosClothSimConfig->ShapeTargetStiffness)](TPBDParticles<float, 3>& InParticles, const float Dt)
		{
			ShapeConstraints.Apply(InParticles, Dt);
		});
	}
	if (ChaosClothSimConfig->EdgeStiffness)
	{
		check(ChaosClothSimConfig->EdgeStiffness > 0.f && ChaosClothSimConfig->EdgeStiffness <= 1.f);
		if (bUseXPBDConstraints)
		{
			const TSharedPtr<Chaos::TXPBDSpringConstraints<float, 3>> SpringConstraints =
				MakeShared<Chaos::TXPBDSpringConstraints<float, 3>>(Evolution->Particles(), SurfaceElements, ChaosClothSimConfig->EdgeStiffness);
			Evolution->AddXPBDConstraintFunctions(
				[SpringConstraints]()
				{
					SpringConstraints->Init();
				},
				[SpringConstraints](TPBDParticles<float, 3>& InParticles, const float Dt)
				{
					SpringConstraints->Apply(InParticles, Dt);
				});
		}
		else
		{
			Evolution->AddPBDConstraintFunction([SpringConstraints = Chaos::TPBDSpringConstraints<float, 3>(Evolution->Particles(), SurfaceElements, ChaosClothSimConfig->EdgeStiffness)](TPBDParticles<float, 3>& InParticles, const float Dt)
			{
				SpringConstraints.Apply(InParticles, Dt);
			});
		}
	}
	if (ChaosClothSimConfig->BendingStiffness)
	{
		check(ChaosClothSimConfig->BendingStiffness > 0.f && ChaosClothSimConfig->BendingStiffness <= 1.f);
		if (ChaosClothSimConfig->bUseBendingElements)
		{
			TArray<Chaos::TVector<int32, 4>> BendingConstraints = Mesh.GetUniqueAdjacentElements();
			Evolution->AddPBDConstraintFunction([BendConstraints = Chaos::TPBDBendingConstraints<float>(Evolution->Particles(), MoveTemp(BendingConstraints), ChaosClothSimConfig->BendingStiffness)](TPBDParticles<float, 3>& InParticles, const float Dt)
			{
				BendConstraints.Apply(InParticles, Dt);
			});
		}
		else
		{
			TArray<Chaos::TVector<int32, 2>> BendingConstraints = Mesh.GetUniqueAdjacentPoints();
			if (bUseXPBDConstraints)
			{
				const TSharedPtr<Chaos::TXPBDSpringConstraints<float, 3>> SpringConstraints =
					MakeShared<Chaos::TXPBDSpringConstraints<float, 3>>(Evolution->Particles(), MoveTemp(BendingConstraints), ChaosClothSimConfig->BendingStiffness);
				Evolution->AddXPBDConstraintFunctions(
					[SpringConstraints]()
					{
						SpringConstraints->Init();
					},
					[SpringConstraints](TPBDParticles<float, 3>& InParticles, const float Dt)
					{
						SpringConstraints->Apply(InParticles, Dt);
					});
			}
			else
			{
				Evolution->AddPBDConstraintFunction([SpringConstraints = Chaos::TPBDSpringConstraints<float, 3>(Evolution->Particles(), MoveTemp(BendingConstraints), ChaosClothSimConfig->BendingStiffness)](TPBDParticles<float, 3>& InParticles, const float Dt)
				{
					SpringConstraints.Apply(InParticles, Dt);
				});
			}
		}
	}
	if (ChaosClothSimConfig->AreaStiffness)
	{
		TArray<Chaos::TVector<int32, 3>> SurfaceConstraints = SurfaceElements;
		if (bUseXPBDConstraints)
		{
			const TSharedPtr<Chaos::TXPBDAxialSpringConstraints<float, 3>> AxialSpringConstraints =
				MakeShared<Chaos::TXPBDAxialSpringConstraints<float, 3>>(Evolution->Particles(), MoveTemp(SurfaceConstraints), ChaosClothSimConfig->AreaStiffness);
			Evolution->AddXPBDConstraintFunctions(
				[AxialSpringConstraints]()
				{
					AxialSpringConstraints->Init();
				},
				[AxialSpringConstraints](TPBDParticles<float, 3>& InParticles, const float Dt)
				{
					AxialSpringConstraints->Apply(InParticles, Dt);
				});
		}
		else
		{
			Evolution->AddPBDConstraintFunction([AxialSpringConstraints = Chaos::TPBDAxialSpringConstraints<float, 3>(Evolution->Particles(), MoveTemp(SurfaceConstraints), ChaosClothSimConfig->AreaStiffness)](TPBDParticles<float, 3>& InParticles, const float Dt)
			{
				AxialSpringConstraints.Apply(InParticles, Dt);
			});
		}
	}
	if (ChaosClothSimConfig->VolumeStiffness)
	{
		check(ChaosClothSimConfig->VolumeStiffness > 0.f && ChaosClothSimConfig->VolumeStiffness <= 1.f);
		if (ChaosClothSimConfig->bUseTetrahedralConstraints)
		{
			// TODO(mlentine): Need to tetrahedralize surface to support this
			check(false);
		}
		else if (ChaosClothSimConfig->bUseThinShellVolumeConstraints)
		{
			TArray<Chaos::TVector<int32, 2>> BendingConstraints = Mesh.GetUniqueAdjacentPoints();
			TArray<Chaos::TVector<int32, 2>> DoubleBendingConstraints;
			{
				TMap<int32, TArray<int32>> BendingHash;
				for (int32 i = 0; i < BendingConstraints.Num(); ++i)
				{
					BendingHash.FindOrAdd(BendingConstraints[i][0]).Add(BendingConstraints[i][1]);
					BendingHash.FindOrAdd(BendingConstraints[i][1]).Add(BendingConstraints[i][0]);
				}
				TSet<Chaos::TVector<int32, 2>> Visited;
				for (auto Elem : BendingHash)
				{
					for (int32 i = 0; i < Elem.Value.Num(); ++i)
					{
						for (int32 j = i + 1; j < Elem.Value.Num(); ++j)
						{
							if (Elem.Value[i] == Elem.Value[j])
								continue;
							auto NewElem = Chaos::TVector<int32, 2>(Elem.Value[i], Elem.Value[j]);
							if (!Visited.Contains(NewElem))
							{
								DoubleBendingConstraints.Add(NewElem);
								Visited.Add(NewElem);
								Visited.Add(Chaos::TVector<int32, 2>(Elem.Value[j], Elem.Value[i]));
							}
						}
					}
				}
			}
			Evolution->AddPBDConstraintFunction([SpringConstraints = Chaos::TPBDSpringConstraints<float, 3>(Evolution->Particles(), MoveTemp(DoubleBendingConstraints), ChaosClothSimConfig->VolumeStiffness)](TPBDParticles<float, 3>& InParticles, const float Dt) {
				SpringConstraints.Apply(InParticles, Dt);
			});
		}
		else
		{
			TArray<Chaos::TVector<int32, 3>> SurfaceConstraints = SurfaceElements;
			Chaos::TPBDVolumeConstraint<float> PBDVolumeConstraint(Evolution->Particles(), MoveTemp(SurfaceConstraints));
			Evolution->AddPBDConstraintFunction([PBDVolumeConstraint = MoveTemp(PBDVolumeConstraint)](TPBDParticles<float, 3>& InParticles, const float Dt)
			{
				PBDVolumeConstraint.Apply(InParticles, Dt);
			});
		}
	}
	if (ChaosClothSimConfig->StrainLimitingStiffness)
	{
		check(Mesh.GetNumElements() > 0);
		// PerFormance note: The Per constraint version of this function is quite a bit faster for smaller assets
		// There might be a cross-over point where the PerParticle version is faster: To be determined
		if (bUseXPBDConstraints)
		{
			const TSharedPtr<Chaos::TXPBDLongRangeConstraints<float, 3>> LongRangeConstraints = MakeShared<Chaos::TXPBDLongRangeConstraints<float, 3>>(
				Evolution->Particles(),
				Mesh.GetPointToNeighborsMap(),
				10, // The max number of connected neighbors per particle.  ryan - What should this be?  Was k...
				ChaosClothSimConfig->StrainLimitingStiffness);

			Evolution->AddXPBDConstraintFunctions(
				[LongRangeConstraints]()
				{
					LongRangeConstraints->Init();
				},
				[LongRangeConstraints](TPBDParticles<float, 3>& InParticles, const float Dt)
				{
					LongRangeConstraints->Apply(InParticles, Dt);
				});
		}
		else
		{
			Chaos::TPBDLongRangeConstraints<float, 3> LongRangeConstraints(
				Evolution->Particles(),
				Mesh.GetPointToNeighborsMap(),
				10, // The max number of connected neighbors per particle.  ryan - What should this be?  Was k...
				ChaosClothSimConfig->StrainLimitingStiffness);

			Evolution->AddPBDConstraintFunction([LongRangeConstraints = MoveTemp(LongRangeConstraints)](TPBDParticles<float, 3>& InParticles, const float Dt)
			{
				LongRangeConstraints.Apply(InParticles, Dt);
			});
		}
	}

	// Maximum Distance Constraints
	const FPointWeightMap& MaxDistances = PhysMesh.GetWeightMap(EChaosWeightMapTarget::MaxDistance);
	if (MaxDistances.Num() > 0)
	{
		check(Mesh.GetNumElements() > 0);
		Chaos::PBDSphericalConstraint<float, 3> SphericalContraint(Offset, MaxDistances.Num(), true, &AnimationPositions, &MaxDistances.Values);
		Evolution->AddPBDConstraintFunction([SphericalContraint = MoveTemp(SphericalContraint)](TPBDParticles<float, 3>& InParticles, const float Dt)
		{
			SphericalContraint.Apply(InParticles, Dt);
		});
	}

	// Backstop Constraints
	const FPointWeightMap& BackstopRadiuses = PhysMesh.GetWeightMap(EChaosWeightMapTarget::BackstopRadius);
	const FPointWeightMap& BackstopDistances = PhysMesh.GetWeightMap(EChaosWeightMapTarget::BackstopDistance);
	if (BackstopRadiuses.Num() > 0 && BackstopDistances.Num() > 0)
	{
		check(Mesh.GetNumElements() > 0);
		check(BackstopRadiuses.Num() == BackstopDistances.Num());

		Chaos::PBDSphericalConstraint<float, 3> SphericalContraint(Offset, BackstopRadiuses.Num(), false, &AnimationPositions,
			&BackstopRadiuses.Values, &BackstopDistances.Values, &AnimationNormals);
		Evolution->AddPBDConstraintFunction([SphericalContraint = MoveTemp(SphericalContraint)](TPBDParticles<float, 3>& InParticles, const float Dt)
		{
			SphericalContraint.Apply(InParticles, Dt);
		});
	}

	// Animation Drive Constraints
	AnimDriveSpringStiffness[InSimDataIndex] = ChaosClothSimConfig->AnimDriveSpringStiffness;
	const FPointWeightMap& AnimDriveMultipliers = PhysMesh.GetWeightMap(EChaosWeightMapTarget::AnimDriveMultiplier);
	if (AnimDriveMultipliers.Num() > 0)
	{
		check(Mesh.GetNumElements() > 0);
		TPBDAnimDriveConstraint<float, 3> PBDAnimDriveConstraint(Offset, &AnimationPositions, &AnimDriveMultipliers.Values, AnimDriveSpringStiffness[InSimDataIndex]);
		Evolution->AddPBDConstraintFunction(
			[PBDAnimDriveConstraint = MoveTemp(PBDAnimDriveConstraint), &Stiffness = AnimDriveSpringStiffness[InSimDataIndex]](TPBDParticles<float, 3>& InParticles, const float Dt) mutable
		{
			PBDAnimDriveConstraint.SetSpringStiffness(Stiffness);
			PBDAnimDriveConstraint.Apply(InParticles, Dt);
		});
	}
}

void ClothingSimulation::AddSelfCollisions(int32 InSimDataIndex) 
{
	// TODO(mlentine): Parallelize these for multiple meshes
	const Chaos::TTriangleMesh<float>& Mesh = *Meshes[InSimDataIndex];
	Evolution->CollisionTriangles().Append(Mesh.GetSurfaceElements());

	const uint32 Offset = IndexToRangeMap[InSimDataIndex][0];
	const uint32 Range = IndexToRangeMap[InSimDataIndex][1];
	for (uint32 i = Offset; i < Range; ++i)
	{
		const TSet<int32> Neighbors = Mesh.GetNRing(i, 5);
		for (int32 Element : Neighbors)
		{
			check(i != Element);
			Evolution->DisabledCollisionElements().Add(Chaos::TVector<int32, 2>(i, Element));
			Evolution->DisabledCollisionElements().Add(Chaos::TVector<int32, 2>(Element, i));
		}
	}
}

void ClothingSimulation::UpdateCollisionTransforms(const ClothingSimulationContext& Context, bool bReinit)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothUpdateCollisionTransforms);

	// Save collision transforms into the OldCollision transforms
	// before overwriting it in this function
	Swap(OldCollisionTransforms, CollisionTransforms);

	TGeometryClothParticles<float, 3>& CollisionParticles = Evolution->CollisionParticles();

	// Resize the transform arrays
	const int32 PrevNumCollisions = OldCollisionTransforms.Num();
	const int32 NumCollisions = BaseTransforms.Num();
	check(NumCollisions == int32(CollisionParticles.Size()));  // BaseTransforms should always automatically grow with the number of collision particles (collection array)

	if (NumCollisions != PrevNumCollisions)
	{
		CollisionTransforms.SetNum(NumCollisions);
		OldCollisionTransforms.SetNum(NumCollisions);
	}

	// Update the collision transforms
	for (int32 Index = 0; Index < NumCollisions; ++Index)
	{
		const int32 BoneIndex = BoneIndices[Index];
		if (Context.BoneTransforms.IsValidIndex(BoneIndex))
		{
			const FTransform& BoneTransform = Context.BoneTransforms[BoneIndex];
			CollisionTransforms[Index] = BaseTransforms[Index] * BoneTransform * Context.ComponentToWorld;
		}
		else
		{
			CollisionTransforms[Index] = BaseTransforms[Index] * Context.ComponentToWorld;  // External collisions often don't map to a bone
		}
	}

	// External collisions are reinitialized at every frame, but don't affect the size of the arrays if the same amount has been added/removed
	const int32 Offset = FMath::Min((int32)ExternalCollisionsOffset, PrevNumCollisions);

	// Reinit old collisions particles and transforms
	if (bReinit)
	{
		for (int32 Index = 0; Index < Offset; ++Index)
		{
			const Chaos::TRigidTransform<float, 3>& CollisionTransform = CollisionTransforms[Index];
			CollisionParticles.X(Index) = CollisionTransform.GetTranslation();
			CollisionParticles.R(Index) = CollisionTransform.GetRotation();
			OldCollisionTransforms[Index] = CollisionTransform;
		}
	}

	// Set the new collision particles and transforms initial state
	for (int32 Index = Offset; Index < NumCollisions; ++Index)
	{
		const Chaos::TRigidTransform<float, 3>& CollisionTransform = CollisionTransforms[Index];
		CollisionParticles.X(Index) = CollisionTransform.GetTranslation();
		CollisionParticles.R(Index) = CollisionTransform.GetRotation();
		OldCollisionTransforms[Index] = CollisionTransform;
	}
}

void ClothingSimulation::ExtractPhysicsAssetCollisions(const UClothingAssetCommon* Asset)
{
	FClothCollisionData ExtractedCollisions;

	TGeometryClothParticles<float, 3>& CollisionParticles = Evolution->CollisionParticles();

	// TODO(mlentine): Support collision body activation on a per particle basis, preferably using a map but also can be a particle attribute
	if (const UPhysicsAsset* const PhysAsset = Asset->PhysicsAsset)
	{
		const USkeletalMesh* const TargetMesh = CastChecked<USkeletalMesh>(Asset->GetOuter());

		TArray<int32> UsedBoneIndices;
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
						ExtractedCollisions.Spheres.Add(Sphere);
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
						SphereConnection.SphereIndices[0] = ExtractedCollisions.Spheres.Add(Sphere0);
						SphereConnection.SphereIndices[1] = ExtractedCollisions.Spheres.Add(Sphere1);
						ExtractedCollisions.SphereConnections.Add(SphereConnection);
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
				ExtractedCollisions.Spheres.Add(Sphere);
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
				ExtractedCollisions.Boxes.Add(Box);
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
					ExtractedCollisions.Spheres.Add(Sphere);
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
					SphereConnection.SphereIndices[0] = ExtractedCollisions.Spheres.Add(Sphere0);
					SphereConnection.SphereIndices[1] = ExtractedCollisions.Spheres.Add(Sphere1);
					ExtractedCollisions.SphereConnections.Add(SphereConnection);
				}
			}

#if !PLATFORM_LUMIN && !PLATFORM_ANDROID  // TODO(Kriss.Gossart): Compile on Android and fix whatever errors the following code is causing
			// Add convexes
			for (const FKConvexElem& ConvexElem : AggGeom.ConvexElems)
			{
				// Add stub for extracted collision data
				FClothCollisionPrim_Convex Convex;
				Convex.BoneIndex = MappedBoneIndex;
#if WITH_PHYSX
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

#elif WITH_CHAOS  // #if WITH_PHYSX
				const Chaos::FImplicitObject& ChaosConvexMesh = *ConvexElem.GetChaosConvexMesh();
				const Chaos::FConvex& ChaosConvex = ChaosConvexMesh.GetObjectChecked<Chaos::FConvex>();

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
#endif  // #if WITH_PHYSX #elif WITH_CHAOS

				// Add extracted collision data
				ExtractedCollisions.Convexes.Add(Convex);
			}
#endif  // #if !PLATFORM_LUMIN && !PLATFORM_ANDROID

		}  // End for PhysAsset->SkeletalBodySetups

		// Add collisions particles
		UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Adding physics asset collisions..."));
		AddCollisions(ExtractedCollisions, UsedBoneIndices);

	}  // End if Asset->PhysicsAsset
}

void ClothingSimulation::ExtractLegacyAssetCollisions(const UClothingAssetCommon* Asset)
{
	if (const UClothLODDataCommon* const AssetLodData = Asset->ClothLodData[0])
	{
		const FClothCollisionData& LodCollData = AssetLodData->CollisionData;
		if (LodCollData.Spheres.Num() || LodCollData.SphereConnections.Num() || LodCollData.Convexes.Num())
		{
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Adding legacy cloth asset collisions..."));
			AddCollisions(LodCollData, Asset->UsedBoneIndices);
		}
	}
}

void ClothingSimulation::AddCollisions(const FClothCollisionData& ClothCollisionData, const TArray<int32>& UsedBoneIndices)
{
	TGeometryClothParticles<float, 3>& CollisionParticles = Evolution->CollisionParticles();

	// Capsules
	TSet<int32> CapsuleEnds;
	const int32 NumCapsules = ClothCollisionData.SphereConnections.Num();
	if (NumCapsules)
	{
		const uint32 Offset = CollisionParticles.Size();
		CollisionParticles.AddParticles(NumCapsules);

		CapsuleEnds.Reserve(NumCapsules * 2);
		for (uint32 i = Offset; i < CollisionParticles.Size(); ++i)
		{
			const FClothCollisionPrim_SphereConnection& Connection = ClothCollisionData.SphereConnections[i - Offset];

			const int32 SphereIndex0 = Connection.SphereIndices[0];
			const int32 SphereIndex1 = Connection.SphereIndices[1];
			checkSlow(SphereIndex0 != SphereIndex1);
			const FClothCollisionPrim_Sphere& Sphere0 = ClothCollisionData.Spheres[SphereIndex0];
			const FClothCollisionPrim_Sphere& Sphere1 = ClothCollisionData.Spheres[SphereIndex1];

			const int32 MappedIndex = UsedBoneIndices.IsValidIndex(Sphere0.BoneIndex) ? UsedBoneIndices[Sphere0.BoneIndex] : INDEX_NONE;

			BoneIndices[i] = GetMappedBoneIndex(UsedBoneIndices, Sphere0.BoneIndex);
			checkSlow(Sphere0.BoneIndex == Sphere1.BoneIndex);
			UE_CLOG(Sphere0.BoneIndex != Sphere1.BoneIndex,
				LogChaosCloth, Warning, TEXT("Found a legacy Apex cloth asset with a collision capsule spanning across two bones. This is not supported with the current system."));
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Found collision capsule on bone index %d."), BoneIndices[i]);

			const Chaos::TVector<float, 3> X0 = Sphere0.LocalPosition;
			const Chaos::TVector<float, 3> X1 = Sphere1.LocalPosition;
			const Chaos::TVector<float, 3> Axis = X1 - X0;
			const float AxisSize = Axis.Size();

			const float Radius0 = Sphere0.Radius;
			const float Radius1 = Sphere1.Radius;
			float MinRadius, MaxRadius;
			if (Radius0 <= Radius1) { MinRadius = Radius0; MaxRadius = Radius1; }
			else { MinRadius = Radius1; MaxRadius = Radius0; }

			if (AxisSize < KINDA_SMALL_NUMBER)
			{
				// Sphere
				BaseTransforms[i] = Chaos::TRigidTransform<float, 3>(FTransform::Identity);

				CollisionParticles.SetDynamicGeometry(
					i,
					MakeUnique<Chaos::TSphere<float, 3>>(
						X0,
						MaxRadius));
			}
			else if (MaxRadius - MinRadius < KINDA_SMALL_NUMBER)
			{
				// Capsule
				const Chaos::TVector<float, 3> Center = (X0 + X1) * 0.5f;  // Construct a capsule centered at the origin along the Z axis
				const Chaos::TRotation<float, 3> Rotation = Chaos::TRotation<float, 3>::FromRotatedVector(
					Chaos::TVector<float, 3>::AxisVector(2),
					Axis.GetSafeNormal());

				BaseTransforms[i] = Chaos::TRigidTransform<float, 3>(Center, Rotation);

				const float HalfHeight = AxisSize * 0.5f;
				CollisionParticles.SetDynamicGeometry(
					i,
					MakeUnique<Chaos::TCapsule<float>>(
						Chaos::TVector<float, 3>(0.f, 0.f, -HalfHeight), // Min
						Chaos::TVector<float, 3>(0.f, 0.f, HalfHeight), // Max
						MaxRadius));
			}
			else
			{
				// Tapered capsule
				BaseTransforms[i] = Chaos::TRigidTransform<float, 3>(FTransform::Identity);

				TArray<TUniquePtr<Chaos::FImplicitObject>> Objects;
				Objects.Reserve(3);
				Objects.Add(TUniquePtr<Chaos::FImplicitObject>(
					new Chaos::TTaperedCylinder<float>(X0, X1, Radius0, Radius1)));
				Objects.Add(TUniquePtr<Chaos::FImplicitObject>(
					new Chaos::TSphere<float, 3>(X0, Radius0)));
				Objects.Add(TUniquePtr<Chaos::FImplicitObject>(
					new Chaos::TSphere<float, 3>(X1, Radius1)));
				CollisionParticles.SetDynamicGeometry(
					i,
					MakeUnique<Chaos::FImplicitObjectUnion>(MoveTemp(Objects)));  // TODO(Kriss.Gossart): Replace this once a TTaperedCapsule implicit type is implemented (note: this tapered cylinder with spheres is an approximation of a real tapered capsule)
			}

			// Skip spheres added as end caps for the capsule.
			CapsuleEnds.Add(SphereIndex0);
			CapsuleEnds.Add(SphereIndex1);
		}
	}

	// Spheres
	const int32 NumSpheres = ClothCollisionData.Spheres.Num() - CapsuleEnds.Num();
	if (NumSpheres != 0)
	{
		const uint32 Offset = CollisionParticles.Size();
		CollisionParticles.AddParticles(NumSpheres);
		// i = Spheres index, j = CollisionParticles index
		for (uint32 i = 0, j = Offset; i < (uint32)ClothCollisionData.Spheres.Num(); ++i)
		{
			// Skip spheres that are the end caps of capsules.
			if (CapsuleEnds.Contains(i))
				continue;

			const FClothCollisionPrim_Sphere& Sphere = ClothCollisionData.Spheres[i];

			BoneIndices[j] = GetMappedBoneIndex(UsedBoneIndices, Sphere.BoneIndex);
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Found collision sphere on bone index %d."), BoneIndices[i]);

			BaseTransforms[j] = Chaos::TRigidTransform<float, 3>(FTransform::Identity);

			CollisionParticles.SetDynamicGeometry(
				j,
				MakeUnique<Chaos::TSphere<float, 3>>(
					Sphere.LocalPosition,
					Sphere.Radius));

			++j;
		}
	}

	// Convexes
	const uint32 NumConvexes = ClothCollisionData.Convexes.Num();
	if (NumConvexes != 0)
	{
		const uint32 Offset = CollisionParticles.Size();
		CollisionParticles.AddParticles(NumConvexes);
		for (uint32 i = Offset; i < CollisionParticles.Size(); ++i)
		{
			const FClothCollisionPrim_Convex& Convex = ClothCollisionData.Convexes[i - Offset];

			BaseTransforms[i] = Chaos::TRigidTransform<float, 3>(FTransform::Identity);

			BoneIndices[i] = GetMappedBoneIndex(UsedBoneIndices, Convex.BoneIndex);
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Found collision convex on bone index %d."), BoneIndices[i]);

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
				TArray<TPlaneConcrete<float, 3>> Planes;
				Planes.Reserve(Convex.Planes.Num());
				for (const FPlane& Plane : Convex.Planes)
				{
					FPlane NormalizedPlane(Plane);
					if (NormalizedPlane.Normalize())
					{
						const Chaos::TVector<float, 3> Normal(static_cast<FVector>(NormalizedPlane));
						const Chaos::TVector<float, 3> Base = Normal * NormalizedPlane.W;

						Planes.Add(Chaos::TPlaneConcrete<float, 3>(Base, Normal));
					}
					else
					{
						UE_LOG(LogChaosCloth, Warning, TEXT("Invalid convex collision: bad plane normal."));
						break;
					}
				}

				if (Planes.Num() == Convex.Planes.Num())
				{
					// Retrieve particles
					TParticles<float, 3> SurfaceParticles;
					SurfaceParticles.Resize(NumSurfacePoints);
					for (int32 ParticleIndex = 0; ParticleIndex < NumSurfacePoints; ++ParticleIndex)
					{
						SurfaceParticles.X(ParticleIndex) = Convex.SurfacePoints[ParticleIndex];
					}

					// Setup the collision particle geometry
					CollisionParticles.SetDynamicGeometry(i, MakeUnique<Chaos::FConvex>(MoveTemp(Planes), MoveTemp(SurfaceParticles)));
				}
			}

			if (!CollisionParticles.DynamicGeometry(i))
			{
				UE_LOG(LogChaosCloth, Warning, TEXT("Replacing invalid convex collision by a default unit sphere."));
				CollisionParticles.SetDynamicGeometry(i, MakeUnique<Chaos::TSphere<float, 3>>(Chaos::TVector<float, 3>(0.0f), 1.0f));  // Default to a unit sphere to replace the faulty convex
			}
		}
	}

	// Boxes
	const uint32 NumBoxes = ClothCollisionData.Boxes.Num();
	if (NumBoxes != 0)
	{
		const uint32 Offset = CollisionParticles.Size();
		CollisionParticles.AddParticles(NumBoxes);
		for (uint32 i = Offset; i < CollisionParticles.Size(); ++i)
		{
			const FClothCollisionPrim_Box& Box = ClothCollisionData.Boxes[i - Offset];
			
			BaseTransforms[i] = Chaos::TRigidTransform<float, 3>(Box.LocalPosition, Box.LocalRotation);
			
			BoneIndices[i] = GetMappedBoneIndex(UsedBoneIndices, Box.BoneIndex);
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Found collision box on bone index %d."), BoneIndices[i]);

			CollisionParticles.SetDynamicGeometry(i, MakeUnique<Chaos::TBox<float, 3>>(-Box.HalfExtents, Box.HalfExtents));
		}
	}

	UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Added collisions: %d spheres, %d capsules, %d convexes, %d boxes."), NumSpheres, NumCapsules, NumConvexes, NumBoxes);
}

void ClothingSimulation::Simulate(IClothingSimulationContext* InContext)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothSimulate);
	const ClothingSimulationContext* const Context = static_cast<ClothingSimulationContext*>(InContext);
	if (Context->DeltaSeconds == 0.f)
	{
		return;
	}

	// Set gravity
	Evolution->GetGravityForces().SetAcceleration(Chaos::TVector<float, 3>(
		(ClothSharedSimConfig && !ClothSharedSimConfig->bUseGravityOverride) ?
			Context->WorldGravity * ClothSharedSimConfig->GravityScale:
			Gravity));

	// Set wind velocity
	static const float UnitConversionScale = 100.f;  // It would seem that in game wind velocity is in m/s and not cm/s as seen in NvClothSupport::Constants::UnitConversionScale
	Evolution->GetVelocityField().SetVelocity(Context->WindVelocity * UnitConversionScale);

	// Check teleport modes
	const bool bTeleport = (Context->TeleportMode > EClothingTeleportMode::None);
	const bool bTeleportAndReset = (Context->TeleportMode == EClothingTeleportMode::TeleportAndReset);
	UE_CLOG(bTeleport, LogChaosCloth, Verbose, TEXT("Teleport, reset: %d"), bTeleportAndReset);

	// Get New Animation Positions and Normals + deal with teleportation
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothGetAnimationData);

		Swap(OldAnimationPositions, AnimationPositions);
		check(OldAnimationPositions.Num() == AnimationPositions.Num());

		TPBDParticles<float, 3>& Particles = Evolution->Particles();

		for (int32 Index = 0; Index < IndexToRangeMap.Num(); ++Index)
		{
			const UClothingAssetCommon* const Asset = Assets[Index];
			if (!Asset)
			{
				continue;
			}

			const UClothLODDataCommon* AssetLodData = Asset->ClothLodData[0];
			const FClothPhysicalMeshData& PhysMesh = AssetLodData->ClothPhysicalMeshData;
			const uint32 PointCount = IndexToRangeMap[Index][1] - IndexToRangeMap[Index][0];

			// Optimization note:
			// This function usually receives the RootBoneTransform in order to transform the result from Component space to RootBone space.
			// (So the mesh vectors and positions (Mesh is in component space) is multiplied by Inv(RootBoneTransform) at the end of the function)
			// We actually require world space coordinates so will instead pass Inv(ComponentToWorld)
			// This saves a lot of Matrix multiplication work later
			const int32 Offset = IndexToRangeMap[Index][0];

			ClothingMeshUtils::SkinPhysicsMesh<true, false>(
				Asset->UsedBoneIndices,
				PhysMesh,
				Context->ComponentToWorld,
				Context->RefToLocals.GetData(),
				Context->RefToLocals.Num(),
				reinterpret_cast<TArray<FVector>&>(AnimationPositions),
				reinterpret_cast<TArray<FVector>&>(AnimationNormals), Offset);

			// Teleport
			const FTransform RootBoneTransform = Context->BoneTransforms[Asset->ReferenceBoneIndex];
			const FTransform RootBoneWorldTransform = RootBoneTransform * Context->ComponentToWorld;

			if (bTeleport)
			{
				UE_LOG(LogChaosCloth, Verbose, TEXT("Teleport before: %s, after: %s"), *RootBoneWorldTransforms[Index].ToString(), *RootBoneWorldTransform.ToString());
				const uint32 Range = IndexToRangeMap[Index][1];
				if (bTeleportAndReset)
				{
					// Teleport & reset
					for (uint32 i = Offset; i < Range; ++i)
					{
						Particles.P(i) = Particles.X(i) = AnimationPositions[i];
						Particles.V(i) = Chaos::TVector<float, 3>(0.f);
					}
				}
				else
				{
					// Teleport only
					const FTransform Delta = RootBoneWorldTransforms[Index].GetRelativeTransformReverse(RootBoneWorldTransform);
					for (uint32 i = Offset; i < Range; ++i)
					{
						Particles.X(i) = Delta.TransformPositionNoScale(Particles.X(i));
						Particles.V(i) = Delta.TransformVectorNoScale(Particles.V(i));
					}
				}
			}

			RootBoneWorldTransforms[Index] = RootBoneWorldTransform;
		}
	}

	// Update collision transforms
	UpdateCollisionTransforms(*Context, bTeleport);

	// Advance Sim
	DeltaTime = Context->DeltaSeconds;
	Evolution->AdvanceOneTimeStep(DeltaTime);
	Time += DeltaTime;
	UE_LOG(LogChaosCloth, VeryVerbose, TEXT("DeltaTime: %.6f, Time = %.6f,  MaxPhysicsDelta = %.6f"), DeltaTime, Time, FClothingSimulationCommon::MaxPhysicsDelta);
}

void ClothingSimulation::GetSimulationData(
	TMap<int32, FClothSimulData>& OutData,
	USkeletalMeshComponent* InOwnerComponent,
	USkinnedMeshComponent* InOverrideComponent) const
{
	const FTransform& OwnerTransform = InOwnerComponent->GetComponentTransform();
	for (int32 i = 0; i < IndexToRangeMap.Num(); ++i)
	{
		const TUniquePtr<Chaos::TTriangleMesh<float>>& Mesh = Meshes[i];
		if (!Mesh)
			continue;
		Mesh->GetFaceNormals(FaceNormals[i], Evolution->Particles().X(), false);  // No need to add a point index offset here since that is baked into the triangles
		Mesh->GetPointNormals(PointNormals[i], FaceNormals[i], /*bReturnEmptyOnError =*/ false, /*bFillAtStartIndex =*/ false);

		FClothSimulData& Data = OutData.FindOrAdd(i);
		Data.Reset();

		const UClothingAssetCommon* const Asset = Assets[i];
		if (!Asset)
			continue;

		const TArray<FTransform>& ComponentSpaceTransforms = InOverrideComponent ?
			InOverrideComponent->GetComponentSpaceTransforms() :
			InOwnerComponent->GetComponentSpaceTransforms();
		if (!ComponentSpaceTransforms.IsValidIndex(Asset->ReferenceBoneIndex))
		{
			UE_LOG(LogSkeletalMesh, Warning,
				TEXT("Failed to write back clothing simulation data for component '%s' as bone transforms are invalid."),
				*InOwnerComponent->GetName());
			check(false);
			continue;
		}

		FTransform RootBoneTransform = ComponentSpaceTransforms[Asset->ReferenceBoneIndex];
		RootBoneTransform.SetScale3D(FVector(1.0f));
		RootBoneTransform *= OwnerTransform;
		Data.Transform = RootBoneTransform;
		Data.ComponentRelativeTransform = OwnerTransform.Inverse();

		const Chaos::TVector<uint32, 2>& VertexDomain = IndexToRangeMap[i];
		const uint32 VertexRange = VertexDomain[1] - VertexDomain[0];
		Data.Positions.SetNum(VertexRange);
        Data.Normals.SetNum(VertexRange);
		for (uint32 j = VertexDomain[0]; j < VertexDomain[1]; ++j)
        {
			const uint32 LocalIndex = j - VertexDomain[0];
            Data.Positions[LocalIndex] = Evolution->Particles().X(j);
            Data.Normals[LocalIndex] = -PointNormals[i][LocalIndex]; // Note the Normals are inverted due to how barycentric coordinates are calculated (see GetPointBaryAndDist in ClothingMeshUtils.cpp)
		}
    }
}

void ClothingSimulation::AddExternalCollisions(const FClothCollisionData& InData)
{
	// Keep track of the external collisions data
	ExternalCollisions.Append(InData);

	// Setup the new collisions particles
	UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Adding external collisions..."));
	const TArray<int32> UsedBoneIndices;  // There is no bone mapping available for external collisions
	AddCollisions(InData, UsedBoneIndices);
}

void ClothingSimulation::ClearExternalCollisions()
{
	// Remove all external collision particles, starting from the external collision offset
	// But do not resize CollisionTransforms as it is only resized in UpdateCollisionTransforms()
	TGeometryClothParticles<float, 3>& CollisionParticles = Evolution->CollisionParticles();
	CollisionParticles.Resize(ExternalCollisionsOffset);  // This will also resize BoneIndices and BaseTransforms

	// Reset external collisions
	ExternalCollisions.Reset();

	UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Cleared all external collisions."));
}

void ClothingSimulation::GetCollisions(FClothCollisionData& OutCollisions, bool bIncludeExternal) const
{
	// This code only gathers old apex collisions that don't appear in the physics mesh
	// It is also never called with bIncludeExternal = true
	// This function is bound to be deprecated at some point

	OutCollisions.Reset();

	// Add internal asset collisions
	for (const UClothingAssetCommon* Asset : Assets)
	{
		if (const UClothLODDataCommon* const ClothLodData = !Asset ? nullptr : Asset->ClothLodData[0])
		{
			OutCollisions.Append(ClothLodData->CollisionData);
		}
	}

	// Add external asset collisions
	if (bIncludeExternal)
	{
		OutCollisions.Append(ExternalCollisions);
	}

	UE_LOG(LogChaosCloth, VeryVerbose, TEXT("GetCollisions returned collisions: %d spheres, %d capsules, %d convexes, %d boxes."), OutCollisions.Spheres.Num() - 2 * OutCollisions.SphereConnections.Num(), OutCollisions.SphereConnections.Num(), OutCollisions.Convexes.Num(), OutCollisions.Boxes.Num());
}

void ClothingSimulation::RefreshClothConfig()
{
	UpdateSimulationFromSharedSimConfig();

	Evolution->ResetConstraintRules();
	Evolution->ResetSelfCollision();

	for (int32 SimDataIndex = 0; SimDataIndex < Assets.Num(); ++SimDataIndex)
	{
		if (const UClothingAssetCommon* const Asset = Assets[SimDataIndex])
		{
			if (const UChaosClothConfig* const ChaosClothConfig = Asset->GetClothConfig<UChaosClothConfig>())
			{
				check(Asset->GetNumLods() > 0);
				const FClothPhysicalMeshData& PhysMesh = Asset->ClothLodData[0]->ClothPhysicalMeshData;

				ResetParticles(SimDataIndex);

				SetParticleMasses(ChaosClothConfig, PhysMesh, SimDataIndex);

				AddConstraints(ChaosClothConfig, PhysMesh, SimDataIndex);

				// Add Self Collisions
				if (ChaosClothConfig->bUseSelfCollisions)
				{
					AddSelfCollisions(SimDataIndex);
				}
			}
		}
	}
	UE_LOG(LogChaosCloth, VeryVerbose, TEXT("RefreshClothConfig, all constraints and self-collisions have been updated for all clothing assets"));
}

void ClothingSimulation::RefreshPhysicsAsset()
{
	// Clear all collisions
	TGeometryClothParticles<float, 3>& CollisionParticles = Evolution->CollisionParticles();
	CollisionParticles.Resize(0);  // This will also resize BoneIndices and BaseTransforms

	ExternalCollisions.Reset();
	ExternalCollisionsOffset = 0;

	// Re-extract all collisions from every cloth asset
	for (const UClothingAssetCommon* Asset : Assets)
	{
		ExtractCollisions(Asset);
	}
	UE_LOG(LogChaosCloth, VeryVerbose, TEXT("RefreshPhysicsAsset, all collisions have been re-added for all clothing assets"));
}

void ClothingSimulation::SetAnimDriveSpringStiffness(float InStiffness)
{
	for (float& stiffness : AnimDriveSpringStiffness)
	{
		stiffness = InStiffness;
	}
}

void ClothingSimulation::SetGravityOverride(const FVector& InGravityOverride)
{
	Gravity = InGravityOverride;
}

void ClothingSimulation::DisableGravityOverride()
{
	Gravity = !ClothSharedSimConfig ? ClothingSimulationDefault::Gravity : ClothSharedSimConfig->Gravity;
}

#if WITH_EDITOR
void ClothingSimulation::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DebugClothMaterial);
}

void ClothingSimulation::DebugDrawPhysMeshWired(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		if (const TUniquePtr<Chaos::TTriangleMesh<float>>& Mesh = Meshes[MeshIndex])
		{
			const TArray<TVector<int32, 3>>& Elements = Mesh->GetElements();

			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
			{
				const auto& Element = Elements[ElementIndex];

				const FVector& Pos0 = Particles.X(Element.X);
				const FVector& Pos1 = Particles.X(Element.Y);
				const FVector& Pos2 = Particles.X(Element.Z);

				PDI->DrawLine(Pos0, Pos1, FLinearColor::White, SDPG_World, 0.0f, 0.001f);
				PDI->DrawLine(Pos1, Pos2, FLinearColor::White, SDPG_World, 0.0f, 0.001f);
				PDI->DrawLine(Pos2, Pos0, FLinearColor::White, SDPG_World, 0.0f, 0.001f);
			}
		}
	}
}

void ClothingSimulation::DebugDrawPhysMeshShaded(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	if (!DebugClothMaterial) { return; }

	FDynamicMeshBuilder MeshBuilder(PDI->View->GetFeatureLevel());
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	int32 VertexIndex = 0;
	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		if (const TUniquePtr<Chaos::TTriangleMesh<float>>& Mesh = Meshes[MeshIndex])
		{
			const TArray<TVector<int32, 3>>& Elements = Mesh->GetElements();

			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex, VertexIndex += 3)
			{
				const auto& Element = Elements[ElementIndex];

				const FVector& Pos0 = Particles.X(Element.X);
				const FVector& Pos1 = Particles.X(Element.Y);
				const FVector& Pos2 = Particles.X(Element.Z);

				const FVector& Normal = FVector::CrossProduct(Pos2 - Pos0, Pos1 - Pos0).GetSafeNormal();
				const FVector Tangent = ((Pos1 + Pos2) * 0.5f - Pos0).GetSafeNormal();

				MeshBuilder.AddVertex(FDynamicMeshVertex(Pos0, Tangent, Normal, FVector2D(0.f, 0.f), FColor::White));
				MeshBuilder.AddVertex(FDynamicMeshVertex(Pos1, Tangent, Normal, FVector2D(0.f, 1.f), FColor::White));
				MeshBuilder.AddVertex(FDynamicMeshVertex(Pos2, Tangent, Normal, FVector2D(1.f, 1.f), FColor::White));
				MeshBuilder.AddTriangle(VertexIndex, VertexIndex + 1, VertexIndex + 2);
			}
		}
	}

	MeshBuilder.Draw(PDI, FMatrix::Identity, DebugClothMaterial->GetRenderProxy(), SDPG_World, false, false);
}

void ClothingSimulation::DebugDrawPointNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	check(Meshes.Num() == IndexToRangeMap.Num());

	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		if (Meshes[MeshIndex])
		{
			const Chaos::TVector<uint32, 2> Range = IndexToRangeMap[MeshIndex];
			const TArray<Chaos::TVector<float, 3>>& MeshPointNormals = PointNormals[MeshIndex];

			for (uint32 ParticleIndex = Range[0]; ParticleIndex < Range[1]; ++ParticleIndex)
			{
				const TVector<float, 3>& Pos = Particles.X(ParticleIndex);
				const TVector<float, 3>& Normal = MeshPointNormals[ParticleIndex - Range[0]];

				PDI->DrawLine(Pos, Pos + Normal * 20.0f, FLinearColor::White, SDPG_World, 0.0f, 0.001f);
			}
		}
	}
}

void ClothingSimulation::DebugDrawInversedPointNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	check(Meshes.Num() == IndexToRangeMap.Num());

	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		if (Meshes[MeshIndex])
		{
			const Chaos::TVector<uint32, 2> Range = IndexToRangeMap[MeshIndex];
			const TArray<Chaos::TVector<float, 3>>& MeshPointNormals = PointNormals[MeshIndex];

			for (uint32 ParticleIndex = Range[0]; ParticleIndex < Range[1]; ++ParticleIndex)
			{
				const TVector<float, 3>& Pos = Particles.X(ParticleIndex);
				const TVector<float, 3>& Normal = MeshPointNormals[ParticleIndex - Range[0]];

				PDI->DrawLine(Pos, Pos - Normal * 20.0f, FLinearColor::White, SDPG_World, 0.0f, 0.001f);
			}
		}
	}
}

void ClothingSimulation::DebugDrawFaceNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	check(Meshes.Num() == IndexToRangeMap.Num());

	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		if (const TUniquePtr<Chaos::TTriangleMesh<float>>& Mesh = Meshes[MeshIndex])
		{
			const TArray<Chaos::TVector<float, 3>>& MeshFaceNormals = FaceNormals[MeshIndex];

			const TArray<TVector<int32, 3>>& Elements = Mesh->GetElements();
			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
			{
				const TVector<int32, 3>& Element = Elements[ElementIndex];

				const TVector<float, 3> Pos = (
					Particles.X(Element.X) +
					Particles.X(Element.Y) +
					Particles.X(Element.Z)) / 3.f;
				const TVector<float, 3>& Normal = MeshFaceNormals[ElementIndex];

				PDI->DrawLine(Pos, Pos + Normal * 20.0f, FLinearColor::Yellow, SDPG_World, 0.0f, 0.001f);
			}
		}
	}
}

void ClothingSimulation::DebugDrawInversedFaceNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	check(Meshes.Num() == IndexToRangeMap.Num());

	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		if (const TUniquePtr<Chaos::TTriangleMesh<float>>& Mesh = Meshes[MeshIndex])
		{
			const TArray<Chaos::TVector<float, 3>>& MeshFaceNormals = FaceNormals[MeshIndex];

			const TArray<TVector<int32, 3>>& Elements = Mesh->GetElements();
			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
			{
				const TVector<int32, 3>& Element = Elements[ElementIndex];

				const TVector<float, 3> Pos = (
					Particles.X(Element.X) +
					Particles.X(Element.Y) +
					Particles.X(Element.Z)) / 3.f;
				const TVector<float, 3>& Normal = MeshFaceNormals[ElementIndex];

				PDI->DrawLine(Pos, Pos - Normal * 20.0f, FLinearColor::Yellow, SDPG_World, 0.0f, 0.001f);
			}
		}
	}
}

void ClothingSimulation::DebugDrawCollision(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	auto DrawSphere = [&PDI](const Chaos::TSphere<float, 3>& Sphere, const TRotation<float, 3>& Rotation, const Chaos::TVector<float, 3>& Position, const FLinearColor& Color)
	{
		const float Radius = Sphere.GetRadius();
		const Chaos::TVector<float, 3> Center = Sphere.GetCenter();
		const FTransform Transform(Rotation, Position + Rotation.RotateVector(Center));
		DrawWireSphere(PDI, Transform, Color, Radius, 12, SDPG_World, 0.0f, 0.001f, false);
	};

	auto DrawBox = [&PDI](const Chaos::TBox<float, 3>& Box, const TRotation<float, 3>& Rotation, const Chaos::TVector<float, 3>& Position, const FLinearColor& Color)
	{
		const FMatrix BoxToWorld = FTransform(Rotation, Position).ToMatrixNoScale();
		const FVector Radii = Box.Extents() * 0.5f;
		DrawWireBox(PDI, BoxToWorld, FBox(Box.Min(), Box.Max()), Color, SDPG_World, 0.0f, 0.001f, false);
	};

	auto DrawCapsule = [&PDI](const Chaos::TCapsule<float>& Capsule, const TRotation<float, 3>& Rotation, const Chaos::TVector<float, 3>& Position, const FLinearColor& Color)
	{
		const float HalfHeight = Capsule.GetHeight() * 0.5f;
		const float Radius = Capsule.GetRadius();
		const FVector X = Rotation.RotateVector(FVector::ForwardVector);
		const FVector Y = Rotation.RotateVector(FVector::RightVector);
		const FVector Z = Rotation.RotateVector(FVector::UpVector);
		DrawWireCapsule(PDI, Position, X, Y, Z, Color, Radius, HalfHeight + Radius, 12, SDPG_World, 0.0f, 0.001f, false);
	};

	auto DrawTaperedCylinder = [&PDI](const Chaos::TTaperedCylinder<float>& TaperedCylinder, const TRotation<float, 3>& Rotation, const Chaos::TVector<float, 3>& Position, const FLinearColor& Color)
	{
		const float HalfHeight = TaperedCylinder.GetHeight() * 0.5f;
		const float Radius1 = TaperedCylinder.GetRadius1();
		const float Radius2 = TaperedCylinder.GetRadius2();
		const FVector Position1 = Position + Rotation.RotateVector(TaperedCylinder.GetX1());
		const FVector Position2 = Position + Rotation.RotateVector(TaperedCylinder.GetX2());
		const FQuat Q = (Position2 - Position1).ToOrientationQuat();
		const FVector I = Q.GetRightVector();
		const FVector J = Q.GetUpVector();

		static const int32 NumSides = 12;
		static const float	AngleDelta = 2.0f * PI / NumSides;
		FVector	LastVertex1 = Position1 + I * Radius1;
		FVector	LastVertex2 = Position2 + I * Radius2;

		for (int32 SideIndex = 1; SideIndex <= NumSides; ++SideIndex)
		{
			const float Angle = AngleDelta * float(SideIndex);
			const FVector ArcPos = I * FMath::Cos(Angle) + J * FMath::Sin(Angle);
			const FVector Vertex1 = Position1 + ArcPos * Radius1;
			const FVector Vertex2 = Position2 + ArcPos * Radius2;

			PDI->DrawLine(LastVertex1, Vertex1, Color, SDPG_World, 0.0f, 0.001f, false);
			PDI->DrawLine(LastVertex2, Vertex2, Color, SDPG_World, 0.0f, 0.001f, false);
			PDI->DrawLine(LastVertex1, LastVertex2, Color, SDPG_World, 0.0f, 0.001f, false);

			LastVertex1 = Vertex1;
			LastVertex2 = Vertex2;
		}
	};

	auto DrawConvex = [&PDI](const Chaos::FConvex& Convex, const TRotation<float, 3>& Rotation, const Chaos::TVector<float, 3>& Position, const FLinearColor& Color)
	{
		const TArray<Chaos::TPlaneConcrete<float, 3>>& Planes = Convex.GetFaces();
		for (int32 PlaneIndex1 = 0; PlaneIndex1 < Planes.Num(); ++PlaneIndex1)
		{
			const Chaos::TPlaneConcrete<float, 3>& Plane1 = Planes[PlaneIndex1];

			for (int32 PlaneIndex2 = PlaneIndex1 + 1; PlaneIndex2 < Planes.Num(); ++PlaneIndex2)
			{
				const Chaos::TPlaneConcrete<float, 3>& Plane2 = Planes[PlaneIndex2];

				// Find the two surface points that belong to both Plane1 and Plane2
				uint32 ParticleIndex1 = INDEX_NONE;

				const Chaos::TParticles<float, 3>& SurfaceParticles = Convex.GetSurfaceParticles();
				for (uint32 ParticleIndex = 0; ParticleIndex < SurfaceParticles.Size(); ++ParticleIndex)
				{
					const Chaos::TVector<float, 3>& X = SurfaceParticles.X(ParticleIndex);

					if (FMath::Square(Plane1.SignedDistance(X)) < KINDA_SMALL_NUMBER && 
						FMath::Square(Plane2.SignedDistance(X)) < KINDA_SMALL_NUMBER)
					{
						if (ParticleIndex1 != INDEX_NONE)
						{
							const Chaos::TVector<float, 3>& X1 = SurfaceParticles.X(ParticleIndex1);
							const FVector Position1 = Position + Rotation.RotateVector(X1);
							const FVector Position2 = Position + Rotation.RotateVector(X);
							PDI->DrawLine(Position1, Position2, Color, SDPG_World, 0.0f, 0.001f, false);
							break;
						}
						ParticleIndex1 = ParticleIndex;
					}
				}
			}
		}
	};

	static const FLinearColor MappedColor(FColor::Cyan);
	static const FLinearColor UnmappedColor(FColor::Red);

	const TGeometryClothParticles<float, 3>& CollisionParticles = Evolution->CollisionParticles();

	for (uint32 Index = 0; Index < CollisionParticles.Size(); ++Index)
	{
		if (const Chaos::FImplicitObject* const Object = CollisionParticles.DynamicGeometry(Index).Get())
		{
			const uint32 BoneIndex = BoneIndices[Index];
			const FLinearColor Color = (BoneIndex != INDEX_NONE) ? MappedColor : UnmappedColor;

			const Chaos::TVector<float, 3>& Position = CollisionParticles.X(Index);
			const TRotation<float, 3>& Rotation = CollisionParticles.R(Index);

			switch (Object->GetType())
			{
			case Chaos::ImplicitObjectType::Sphere:
				DrawSphere(Object->GetObjectChecked<Chaos::TSphere<float, 3>>(), Rotation, Position, Color);
				break;

			case Chaos::ImplicitObjectType::Box:
				DrawBox(Object->GetObjectChecked<Chaos::TBox<float, 3>>(), Rotation, Position, Color);
				break;

			case Chaos::ImplicitObjectType::Capsule:
				DrawCapsule(Object->GetObjectChecked<Chaos::TCapsule<float>>(), Rotation, Position, Color);
				break;

			case Chaos::ImplicitObjectType::Union:  // Union only used as collision tappered capsules
				for (const TUniquePtr<Chaos::FImplicitObject>& SubObjectPtr : Object->GetObjectChecked<Chaos::FImplicitObjectUnion>().GetObjects())
				{
					if (const Chaos::FImplicitObject* const SubObject = SubObjectPtr.Get())
					{
						switch (SubObject->GetType())
						{
						case Chaos::ImplicitObjectType::Sphere:
							DrawSphere(SubObject->GetObjectChecked<Chaos::TSphere<float, 3>>(), Rotation, Position, Color);
							break;

						case Chaos::ImplicitObjectType::TaperedCylinder:
							DrawTaperedCylinder(SubObject->GetObjectChecked<Chaos::TTaperedCylinder<float>>(), Rotation, Position, Color);
							break;

						default:
							break;
						}
					}
				}
				break;
	
			case Chaos::ImplicitObjectType::Convex:
				DrawConvex(Object->GetObjectChecked<Chaos::FConvex>(), Rotation, Position, Color);
				break;

			default:
				DrawCoordinateSystem(PDI, Position, FRotator(Rotation), 10.0f, SDPG_World, 0.1f);  // Draw everything else as a coordinate for now
				break;
			}
		}
	}
}

void ClothingSimulation::DebugDrawBackstops(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	for (int32 i = 0; i < IndexToRangeMap.Num(); ++i)
	{
		const UClothingAssetCommon* const Asset = Assets[i];
		if (Asset == nullptr)
		{
			continue;
		}

		// Get Backstop Distances
		const UClothLODDataCommon* const AssetLodData = Asset->ClothLodData[0];
		check(AssetLodData);
		const FClothPhysicalMeshData& PhysMesh = AssetLodData->ClothPhysicalMeshData;
		const FPointWeightMap& BackstopDistances = PhysMesh.GetWeightMap(EChaosWeightMapTarget::BackstopDistance);
		const FPointWeightMap& BackstopRadiuses = PhysMesh.GetWeightMap(EChaosWeightMapTarget::BackstopRadius);
		if (BackstopDistances.Num() == 0 || BackstopRadiuses.Num() == 0)
		{
			continue;
		}

		for (uint32 ParticleIndex = IndexToRangeMap[i][0]; ParticleIndex < IndexToRangeMap[i][1]; ++ParticleIndex)
		{
			const uint32 WeightMapIndex = ParticleIndex - IndexToRangeMap[i][0];
			const float Radius = BackstopRadiuses[WeightMapIndex];
			const float Distance = BackstopDistances[WeightMapIndex];
			PDI->DrawLine(AnimationPositions[ParticleIndex], AnimationPositions[ParticleIndex] - AnimationNormals[ParticleIndex] * (Distance - Radius), FColor::White, SDPG_World, 0.0f, 0.001f);
			if (Radius > 0.0f)
			{
				const FVector& Normal = AnimationNormals[ParticleIndex];
				const FVector& Position = AnimationPositions[ParticleIndex];
				auto DrawBackstop = [Radius, Distance, &Normal, &Position, PDI](const FVector& Axis, const FColor& Color)
				{
					const float ArcLength = 5.0f; // Arch length in cm
					const float ArcAngle = ArcLength * 360.0f / (Radius * 2.0f * PI);
					
					const float MaxCosAngle = 0.99f;
					if (FMath::Abs(FVector::DotProduct(Normal, Axis)) < MaxCosAngle)
					{
						DrawArc(PDI, Position - Normal * Distance, Normal, FVector::CrossProduct(Axis, Normal).GetSafeNormal(), -ArcAngle / 2.0f, ArcAngle / 2.0f, Radius, 10, Color, SDPG_World);
					}
				};
				DrawBackstop(FVector::ForwardVector, FColor::Blue);
				DrawBackstop(FVector::UpVector, FColor::Blue);
				DrawBackstop(FVector::RightVector, FColor::Blue);
			}
		}
	}
}

void ClothingSimulation::DebugDrawMaxDistances(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();
	for (int32 i = 0; i < IndexToRangeMap.Num(); ++i)
	{
		const UClothingAssetCommon* const Asset = Assets[i];
		if (Asset == nullptr)
		{
			continue;
		}

		// Get Maximum Distances
		const UClothLODDataCommon* const AssetLodData = Asset->ClothLodData[0];
		check(AssetLodData);
		const FClothPhysicalMeshData& PhysMesh = AssetLodData->ClothPhysicalMeshData;
		const FPointWeightMap& MaxDistances = PhysMesh.GetWeightMap(EChaosWeightMapTarget::MaxDistance);
		if (MaxDistances.Num() == 0)
		{
			continue;
		}
		
		for (uint32 ParticleIndex = IndexToRangeMap[i][0]; ParticleIndex < IndexToRangeMap[i][1]; ++ParticleIndex)
		{
			const uint32 WeightMapIndex = ParticleIndex - IndexToRangeMap[i][0];
			const float Distance = MaxDistances[WeightMapIndex];
			if (Particles.InvM(ParticleIndex) == 0.0f)
			{
				const FMatrix& ViewMatrix = PDI->View->ViewMatrices.GetViewMatrix();
				const FVector& XAxis = ViewMatrix.GetColumn(0); // Just using transpose here (orthogonal transform assumed)
				const FVector& YAxis = ViewMatrix.GetColumn(1);
				DrawDisc(PDI, AnimationPositions[ParticleIndex], XAxis, YAxis, FColor::White, 0.2f, 10, DebugClothMaterialVertex->GetRenderProxy(), SDPG_World);
			}
			else
			{
				PDI->DrawLine(AnimationPositions[ParticleIndex], AnimationPositions[ParticleIndex] + AnimationNormals[ParticleIndex] * Distance, FColor::White, SDPG_World, 0.0f, 0.001f);
			}
		}
	}
}

void ClothingSimulation::DebugDrawAnimDrive(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();
	for (int32 i = 0; i < IndexToRangeMap.Num(); ++i)
	{
		const UClothingAssetCommon* const Asset = Assets[i];
		if (Asset == nullptr)
		{
			continue;
		}

		// Get Animdrive Multiplier
		const UClothLODDataCommon* const AssetLodData = Asset->ClothLodData[0];
		check(AssetLodData);
		const FClothPhysicalMeshData& PhysMesh = AssetLodData->ClothPhysicalMeshData;
		const FPointWeightMap& AnimDriveMultipliers = PhysMesh.GetWeightMap(EChaosWeightMapTarget::AnimDriveMultiplier);
		if (AnimDriveMultipliers.Num() == 0)
		{
			continue;
		}

		for (uint32 ParticleIndex = IndexToRangeMap[i][0]; ParticleIndex < IndexToRangeMap[i][1]; ++ParticleIndex)
		{
			const uint32 WeightMapIndex = ParticleIndex - IndexToRangeMap[i][0];
			const float Multiplier = AnimDriveMultipliers[WeightMapIndex];
			PDI->DrawLine(AnimationPositions[ParticleIndex], Particles.X(ParticleIndex), Multiplier * AnimDriveSpringStiffness[i] * FColor::Cyan, SDPG_World, 0.0f, 0.001f);
		}
	}
}
#endif  // #if WITH_EDITOR
