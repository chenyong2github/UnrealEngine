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

#include "Chaos/PBDEvolution.h"
#include "Chaos/TriangleMesh.h"

#if PHYSICS_INTERFACE_PHYSX && !PLATFORM_LUMIN && !PLATFORM_ANDROID
#include "PhysXIncludes.h"
#endif

#if CHAOS_DEBUG_DRAW
#include "Chaos/DebugDrawQueue.h"
#include "HAL/IConsoleManager.h"

namespace ChaosClothingSimulationConsoleVariables
{
	TAutoConsoleVariable<bool> CVarDebugDrawLocalSpace      (TEXT("p.ChaosCloth.DebugDrawLocalSpace"          ), false, TEXT("Whether to debug draw the Chaos Cloth local space"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugDrawBounds          (TEXT("p.ChaosCloth.DebugDrawBounds"              ), false, TEXT("Whether to debug draw the Chaos Cloth bounds"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugDrawGravity         (TEXT("p.ChaosCloth.DebugDrawGravity"             ), false, TEXT("Whether to debug draw the Chaos Cloth gravity acceleration vector"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugDrawPhysMeshWired   (TEXT("p.ChaosCloth.DebugDrawPhysMeshWired"       ), false, TEXT("Whether to debug draw the Chaos Cloth wireframe meshes"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugPointNormals        (TEXT("p.ChaosCloth.DebugDrawPointNormals"        ), false, TEXT("Whether to debug draw the Chaos Cloth point normals"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugInversedPointNormals(TEXT("p.ChaosCloth.DebugDrawInversedPointNormals"), false, TEXT("Whether to debug draw the Chaos Cloth inversed point normals"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugFaceNormals         (TEXT("p.ChaosCloth.DebugDrawFaceNormals"         ), false, TEXT("Whether to debug draw the Chaos Cloth face normals"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugInversedFaceNormals (TEXT("p.ChaosCloth.DebugDrawInversedFaceNormals" ), false, TEXT("Whether to debug draw the Chaos Cloth inversed face normals"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugCollision           (TEXT("p.ChaosCloth.DebugDrawCollision"           ), false, TEXT("Whether to debug draw the Chaos Cloth collisions"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugBackstops           (TEXT("p.ChaosCloth.DebugDrawBackstops"           ), false, TEXT("Whether to debug draw the Chaos Cloth backstops"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugMaxDistances        (TEXT("p.ChaosCloth.DebugDrawMaxDistances"        ), false, TEXT("Whether to debug draw the Chaos Cloth max distances"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugAnimDrive           (TEXT("p.ChaosCloth.DebugDrawAnimDrive"           ), false, TEXT("Whether to debug draw the Chaos Cloth anim drive"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugLongRangeConstraint (TEXT("p.ChaosCloth.DebugDrawLongRangeConstraint" ), false, TEXT("Whether to debug draw the Chaos Cloth long range constraint (aka tether constraint)"), ECVF_Cheat);
	TAutoConsoleVariable<bool> CVarDebugWindDragForces      (TEXT("p.ChaosCloth.DebugDrawWindDragForces"      ), false, TEXT("Whether to debug draw the Chaos Cloth wind drag forces"), ECVF_Cheat);
}
#endif  // #if CHAOS_DEBUG_DRAW

using namespace Chaos;

DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Simulate"), STAT_ChaosClothSimulate, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Get Animation Data"), STAT_ChaosClothGetAnimationData, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Update Collision Transforms"), STAT_ChaosClothUpdateCollisionTransforms, STATGROUP_ChaosCloth);

// Default parameters, will be overwritten when cloth assets are loaded
namespace ChaosClothingSimulationDefault
{
	static const FVector Gravity(0.f, 0.f, -980.665f);
	static const int32 NumIterations = 1;
	static const float SelfCollisionThickness = 2.f;
	static const float CollisionThickness = 1.2f;
	static const float FrictionCoefficient = 0.2f;
	static const float DampingCoefficient = 0.01f;
	static const float WorldScale = 100.f;  // World is in cm, but values like wind speed and density are in SI unit and relates to m.
}

ClothingSimulation::ClothingSimulation()
	: ClothSharedSimConfig(nullptr)
	, ExternalCollisionsOffset(0)
	, NumSubsteps(1)
	, bOverrideGravity(false)
	, bUseConfigGravity(false)
	, GravityScale(1.f)
	, Gravity(ChaosClothingSimulationDefault::Gravity)
	, ConfigGravity(ChaosClothingSimulationDefault::Gravity)
	, WindVelocity(FVector::ZeroVector)
	, bUseLocalSpaceSimulation(false)
	, LocalSpaceLocation(FVector::ZeroVector)
{
	ResetStats();

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
			ChaosClothingSimulationDefault::NumIterations,
			ChaosClothingSimulationDefault::CollisionThickness,
			ChaosClothingSimulationDefault::SelfCollisionThickness,
			ChaosClothingSimulationDefault::FrictionCoefficient,
			ChaosClothingSimulationDefault::DampingCoefficient));
    Evolution->CollisionParticles().AddArray(&BoneIndices);
	Evolution->CollisionParticles().AddArray(&BaseTransforms);
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

    Time = Evolution->GetTime();
	DeltaTime = 1.f / 30.f;  // Initialize filtered timestep at 30fps 
}

void ClothingSimulation::Shutdown()
{
	Assets.Reset();
	AnimDriveSpringStiffness.Reset();
	MaxDistancesMultipliers.Reset();
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
	LinearDeltaRatios.Reset();
	AngularDeltaRatios.Reset();
	Meshes.Reset();
	FaceNormals.Reset();
	PointNormals.Reset();
	Evolution.Reset();
	CollisionsRangeMap.Reset();
	ExternalCollisionsRangeMaps.Reset();
	ExternalCollisionsOffset = 0;
	ClothSharedSimConfig = nullptr;
	LongRangeConstraints.Reset();

	ResetStats();
}

void ClothingSimulation::DestroyActors()
{
	Shutdown();
	Initialize();
}

void ClothingSimulation::CreateActor(USkeletalMeshComponent* InOwnerComponent, UClothingAssetBase* InAsset, int32 InSimDataIndex)
{
	UE_LOG(LogChaosCloth, Verbose, TEXT("Adding Cloth LOD asset to %s in sim slot %d"), InOwnerComponent->GetOwner() ? *InOwnerComponent->GetOwner()->GetName() : TEXT("None"), InSimDataIndex);

	UClothingAssetCommon* const Asset = Cast<UClothingAssetCommon>(InAsset);
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
		const int32 NumAssets = InSimDataIndex + 1;

		// TODO: Refactor all these arrays into a single cloth runtime asset structure
		Assets.SetNumZeroed(NumAssets);
		AnimDriveSpringStiffness.SetNumZeroed(NumAssets);
		MaxDistancesMultipliers.SetNumZeroed(NumAssets);

		Meshes.SetNum(NumAssets);
		FaceNormals.SetNum(NumAssets);
		PointNormals.SetNum(NumAssets);

		IndexToRangeMap.SetNum(NumAssets);

		LongRangeConstraints.SetNum(NumAssets);

		RootBoneWorldTransforms.SetNum(NumAssets);
		LinearDeltaRatios.SetNum(NumAssets);
		AngularDeltaRatios.SetNum(NumAssets);

		CollisionsRangeMap.SetNumZeroed(NumAssets);
	}
	Assets[InSimDataIndex] = Asset;

	check(Asset->GetNumLods() > 0);
	UE_CLOG(Asset->GetNumLods() != 1,
		LogChaosCloth, Warning, TEXT("More than one LOD with the current cloth asset %s in sim slot %d. Only LOD 0 is supported with Chaos Cloth for now."),
		InOwnerComponent->GetOwner() ? *InOwnerComponent->GetOwner()->GetName() : TEXT("None"), InSimDataIndex);
	const FClothLODDataCommon& AssetLodData = Asset->LodData[0];
	const FClothPhysicalMeshData& PhysMesh = AssetLodData.PhysicalMeshData;

	// Add particles
	TPBDParticles<float, 3>& Particles = Evolution->Particles();
	const uint32 Offset = Evolution->AddParticles(PhysMesh.Vertices.Num(), (uint32)InSimDataIndex);

	IndexToRangeMap[InSimDataIndex] = Chaos::TVector<uint32, 2>(Offset, Particles.Size());

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

	// Initialize the local simulation space transform
	FTransform ComponentToLocalSpace = Context.ComponentToWorld;
	if (Offset == 0) // Only initialize this once for all cloth instances
	{
		// Set local offset
		if (bUseLocalSpaceSimulation)
		{
			LocalSpaceLocation = ComponentToLocalSpace.GetTranslation();
		}
		else
		{
			LocalSpaceLocation = FVector::ZeroVector;
		}
	}
	ComponentToLocalSpace.AddToTranslation(-LocalSpaceLocation);

	// Init local cloth sim space & teleport transform
	const FTransform RootBoneTransform = Context.BoneTransforms[Asset->ReferenceBoneIndex];
	RootBoneWorldTransforms[InSimDataIndex] = RootBoneTransform * Context.ComponentToWorld;  // Velocity scale deltas are calculated in world space
	LinearDeltaRatios[InSimDataIndex] = FVector::OneVector - ChaosClothSimConfig->LinearVelocityScale.BoundToBox(FVector::ZeroVector, FVector::OneVector);
	AngularDeltaRatios[InSimDataIndex] = 1.f - FMath::Clamp(ChaosClothSimConfig->AngularVelocityScale, 0.f, 1.f);

	// Skin start pose
	ClothingMeshUtils::SkinPhysicsMesh<true, false>(
		Asset->UsedBoneIndices,
		PhysMesh, // curr pos and norm
		ComponentToLocalSpace,
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

	// Set this cloth damping, collision thickness, friction
	Evolution->SetDamping(ChaosClothSimConfig->DampingCoefficient, InSimDataIndex);
	Evolution->SetCollisionThickness(ChaosClothSimConfig->CollisionThickness, InSimDataIndex);
	Evolution->SetCoefficientOfFriction(ChaosClothSimConfig->FrictionCoefficient, InSimDataIndex);

	// Add velocity field
	auto GetVelocity = [this](const TVector<float, 3>&)->TVector<float, 3>
	{
		return WindVelocity;
	};
	Evolution->GetVelocityFields().Emplace(
		*Meshes[InSimDataIndex],
		GetVelocity,
		/*bInIsUniform =*/ true,
		ChaosClothSimConfig->DragCoefficient);

	// Add Self Collisions
	if (ChaosClothSimConfig->bUseSelfCollisions)
	{
		AddSelfCollisions(InSimDataIndex);
	}

	// Warn about legacy apex collisions
	const FClothCollisionData& LodCollData = AssetLodData.CollisionData;
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
	ExtractCollisions(Asset, InSimDataIndex);

	// Update collision transforms, including initial state for particles' X & R
	UpdateCollisionTransforms(Context, InSimDataIndex);

	// Update stats
	UpdateStats(InSimDataIndex);
}

void ClothingSimulation::ResetStats()
{
#if WITH_EDITOR
	NumCloths = 0;
	NumKinemamicParticles = 0;
	NumDynamicParticles = 0;
	SimulationTime = 0.f;
#endif  // #if WITH_EDITOR
}

void ClothingSimulation::UpdateStats(int32 InSimDataIndex)
{
#if WITH_EDITOR
	const uint32 Offset = IndexToRangeMap[InSimDataIndex][0];
	const uint32 Range = IndexToRangeMap[InSimDataIndex][1];
	if (const int32 AddedParticleCount = Range - Offset)
	{
		++NumCloths;
	}

	TPBDParticles<float, 3>& Particles = Evolution->Particles();
	int32 NumAddedKinenamicParticles = 0;
	int32 NumAddedDynamicParticles = 0;
	for (uint32 i = Offset; i < Range; ++i)
	{
		if (Particles.InvM(i) == 0.f)
		{
			++NumAddedKinenamicParticles;
		}
		else
		{
			++NumAddedDynamicParticles;
		}
	}
	NumKinemamicParticles += NumAddedKinenamicParticles;
	NumDynamicParticles += NumAddedDynamicParticles;
#endif  // #if WITH_EDITOR
}

void ClothingSimulation::ExtractCollisions(const UClothingAssetCommon* Asset, int32 InSimDataIndex)
{
	CollisionsRangeMap[InSimDataIndex][0] = Evolution->CollisionParticles().Size();

	// Pull collisions from the specified physics asset inside the clothing asset
	ExtractPhysicsAssetCollisions(Asset, InSimDataIndex);

	// Extract the legacy Apex collision from the clothing asset
	ExtractLegacyAssetCollisions(Asset, InSimDataIndex);

	// Update the external collision offset and collision range for this asset
	CollisionsRangeMap[InSimDataIndex][1] = ExternalCollisionsOffset = Evolution->CollisionParticles().Size();
}

void ClothingSimulation::PostActorCreationInitialize()
{
	UpdateSimulationFromSharedSimConfig();
}

void ClothingSimulation::UpdateSimulationFromSharedSimConfig()
{
	if (ClothSharedSimConfig) // ClothSharedSimConfig will be a null pointer if all cloth instances are disabled in which case we will use default Evolution parameters
	{
		// Update local space simulation switch
		bUseLocalSpaceSimulation = ClothSharedSimConfig->bUseLocalSpaceSimulation;

		// Update gravity related config values
		ConfigGravity = ClothSharedSimConfig->Gravity;
		GravityScale = ClothSharedSimConfig->GravityScale;
		bUseConfigGravity = ClothSharedSimConfig->bUseGravityOverride;

		// Now set all the common parameters on the simulation
		NumSubsteps = ClothSharedSimConfig->SubdivisionCount;
		Evolution->SetIterations(ClothSharedSimConfig->IterationCount);
		Evolution->SetSelfCollisionThickness(ClothSharedSimConfig->SelfCollisionThickness);
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
	float TotalArea = 0.f;
	for (const Chaos::TVector<int32, 3>& Tri : SurfaceElements)
	{
		const float TriArea = 0.5f * Chaos::TVector<float, 3>::CrossProduct(
			Particles.X(Tri[1]) - Particles.X(Tri[0]),
			Particles.X(Tri[2]) - Particles.X(Tri[0])).Size();
		TotalArea += TriArea;
		const float ThirdTriArea = TriArea / 3.f;
		Particles.M(Tri[0]) += ThirdTriArea;
		Particles.M(Tri[1]) += ThirdTriArea;
		Particles.M(Tri[2]) += ThirdTriArea;
	}
	const TSet<int32> Vertices = Mesh.GetVertices();
	float TotalMass = 0.f;
	switch (ChaosClothConfig->MassMode)
	{
	case EClothMassMode::UniformMass:
		for (const int32 Vertex : Vertices)
		{
			Particles.M(Vertex) = ChaosClothConfig->UniformMass;
			TotalMass += Particles.M(Vertex);
		}
		break;
	case EClothMassMode::TotalMass:
	{
		const float MassPerUnitArea = TotalArea > 0.f ? ChaosClothConfig->TotalMass / TotalArea : 1.f;
		for (const int32 Vertex : Vertices)
		{
			Particles.M(Vertex) *= MassPerUnitArea;
			TotalMass += Particles.M(Vertex);
		}
		break;
	}
	case EClothMassMode::Density:
	{
		const float Density = ChaosClothConfig->Density / FMath::Square(ChaosClothingSimulationDefault::WorldScale);
		for (const int32 Vertex : Vertices)
		{
			Particles.M(Vertex) *= Density;
			TotalMass += Particles.M(Vertex);
		}
		break;
	}
	};
	UE_LOG(LogChaosCloth, Verbose, TEXT("Density: %f, Total surface: %f, Total mass: %f, "), TotalArea > 0.f ? TotalMass / TotalArea : 1.f, TotalArea, TotalMass);
	UE_LOG(LogChaosCloth, Verbose, TEXT("SI Density: %f, SI Total surface: %f, SI Total mass: %f, "), (TotalArea > 0.f ? TotalMass / TotalArea : 1.f) * FMath::Square(ChaosClothingSimulationDefault::WorldScale), TotalArea / FMath::Square(ChaosClothingSimulationDefault::WorldScale), TotalMass);

	// Clamp and enslave
	const FPointWeightMap& MaxDistances = PhysMesh.GetWeightMap(EChaosWeightMapTarget::MaxDistance);
	const uint32 Offset = IndexToRangeMap[InSimDataIndex][0];
	const uint32 Range = IndexToRangeMap[InSimDataIndex][1];

	check(Particles.Size() >= Range);
	for (uint32 i = Offset; i < Range; ++i)
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
			TArray<TVector<int32, 3>> DynamicSurfaceElements;
			TArray<TVector<int32, 2>> Attachments;
			for (const TVector<int32, 3>& SurfaceElement : SurfaceElements)
			{
				const bool bIsKinematic0 = (Evolution->Particles().InvM(SurfaceElement[0]) == 0.f);
				const bool bIsKinematic1 = (Evolution->Particles().InvM(SurfaceElement[1]) == 0.f);
				const bool bIsKinematic2 = (Evolution->Particles().InvM(SurfaceElement[2]) == 0.f);
				bool bIsAttachment = false;
				if (bIsKinematic0 != bIsKinematic1)
				{
					Attachments.Emplace(SurfaceElement[0], SurfaceElement[1]);
					bIsAttachment = true;
				}
				if (bIsKinematic1 != bIsKinematic2)
				{
					Attachments.Emplace(SurfaceElement[1], SurfaceElement[2]);
					bIsAttachment = true;
				}
				if (bIsKinematic2 != bIsKinematic0)
				{
					Attachments.Emplace(SurfaceElement[2], SurfaceElement[0]);
					bIsAttachment = true;
				}
				if (!bIsAttachment)
				{
					DynamicSurfaceElements.Add(SurfaceElement);
				}
			}
			if (Attachments.Num())
			{
				Evolution->AddPBDConstraintFunction([AttachmentConstraints = Chaos::TPBDSpringConstraints<float, 3>(Evolution->Particles(), MoveTemp(Attachments), ChaosClothSimConfig->EdgeStiffness)](TPBDParticles<float, 3>& InParticles, const float Dt)
				{
					AttachmentConstraints.Apply(InParticles, Dt);
				});
			}
			if (DynamicSurfaceElements.Num())
			{
				Evolution->AddPBDConstraintFunction([SpringConstraints = Chaos::TPBDSpringConstraints<float, 3>(Evolution->Particles(), DynamicSurfaceElements, ChaosClothSimConfig->EdgeStiffness)](TPBDParticles<float, 3>& InParticles, const float Dt)
				{
					SpringConstraints.Apply(InParticles, Dt);
				});
			}
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
			LongRangeConstraints[InSimDataIndex] = MakeShared<Chaos::TXPBDLongRangeConstraints<float, 3>>(
				Evolution->Particles(),
				Mesh.GetPointToNeighborsMap(),
				10, // The max number of connected neighbors per particle.  ryan - What should this be?  Was k...
				ChaosClothSimConfig->StrainLimitingStiffness);  // TODO(Kriss.Gossart): Add LimitScale and Geodesic mode if ever of use

			Evolution->AddXPBDConstraintFunctions(
				[this, InSimDataIndex]()
				{
					static_cast<Chaos::TXPBDLongRangeConstraints<float, 3>&>(*LongRangeConstraints[InSimDataIndex]).Init();
				},
				[this, InSimDataIndex](TPBDParticles<float, 3>& InParticles, const float Dt)
				{
					static_cast<Chaos::TXPBDLongRangeConstraints<float, 3>&>(*LongRangeConstraints[InSimDataIndex]).Apply(InParticles, Dt);
				});
		}
		else
		{
			LongRangeConstraints[InSimDataIndex] = MakeShared<Chaos::TPBDLongRangeConstraints<float, 3>>(
				Evolution->Particles(),
				Mesh.GetPointToNeighborsMap(),
				10, // The max number of connected neighbors per particle.  ryan - What should this be?  Was k...
				ChaosClothSimConfig->StrainLimitingStiffness,
				ChaosClothSimConfig->LimitScale,
				ChaosClothSimConfig->bUseGeodesicDistance);

			Evolution->AddPBDConstraintFunction([this, InSimDataIndex](TPBDParticles<float, 3>& InParticles, const float Dt)
			{
				static_cast<Chaos::TPBDLongRangeConstraints<float, 3>&>(*LongRangeConstraints[InSimDataIndex]).Apply(InParticles, Dt);
			});
		}
	}

	// Maximum Distance Constraints
	const FPointWeightMap& MaxDistances = PhysMesh.GetWeightMap(EChaosWeightMapTarget::MaxDistance);
	if (MaxDistances.Num() > 0)
	{
		// Initialize the interactor's multiplier
		MaxDistancesMultipliers[InSimDataIndex] = 1.f;

		check(Mesh.GetNumElements() > 0);
		Chaos::PBDSphericalConstraint<float, 3> SphericalContraint(Offset, MaxDistances.Num(), true, &AnimationPositions, &MaxDistances.Values);
		Evolution->AddPBDConstraintFunction([
			&SphereRadiiMultiplier = MaxDistancesMultipliers[InSimDataIndex],
			SphericalContraint = MoveTemp(SphericalContraint)](TPBDParticles<float, 3>& InParticles, const float Dt) mutable
		{
			SphericalContraint.SetSphereRadiiMultiplier(SphereRadiiMultiplier);
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

void ClothingSimulation::ForAllCollisions(TFunction<void(TGeometryClothParticles<float, 3>&, uint32)> CollisionFunction, int32 SimDataIndex)
{
	TGeometryClothParticles<float, 3>& CollisionParticles = Evolution->CollisionParticles();

	for (uint32 Index = CollisionsRangeMap[SimDataIndex][0]; Index < CollisionsRangeMap[SimDataIndex][1]; ++Index)
	{
		CollisionFunction(CollisionParticles, Index);
	}
	for (const TArray<TVector<uint32, 2>>& ExternalCollisionsRangeMap : ExternalCollisionsRangeMaps)
	{
		for (uint32 Index = ExternalCollisionsRangeMap[SimDataIndex][0]; Index < ExternalCollisionsRangeMap[SimDataIndex][1]; ++Index)
		{
			CollisionFunction(CollisionParticles, Index);
		}
	}
}

void ClothingSimulation::UpdateCollisionTransforms(const ClothingSimulationContext& Context, int32 InSimDataIndex)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothUpdateCollisionTransforms);

	// Resize the transform arrays if collision have changed
	const int32 PrevNumCollisions = OldCollisionTransforms.Num();
	const int32 NumCollisions = BaseTransforms.Num();
	check(NumCollisions == int32(Evolution->CollisionParticles().Size()));  // BaseTransforms should always automatically grow with the number of collision particles (collection array)

	const bool bHasNumCollisionsChanged = (NumCollisions != PrevNumCollisions);
	if (bHasNumCollisionsChanged)
	{
		CollisionTransforms.SetNum(NumCollisions);
		OldCollisionTransforms.SetNum(NumCollisions);
	}

	// Update the collision transforms
	FTransform ComponentToLocalSimulationSpace = Context.ComponentToWorld;
	ComponentToLocalSimulationSpace.AddToTranslation(-LocalSpaceLocation);

	ForAllCollisions([this, &Context, &ComponentToLocalSimulationSpace, bHasNumCollisionsChanged](TGeometryClothParticles<float, 3>& CollisionParticles, uint32 Index)
	{
		// Update the collision transforms
		const int32 BoneIndex = BoneIndices[Index];
		Chaos::TRigidTransform<float, 3>& CollisionTransform = CollisionTransforms[Index];
		if (Context.BoneTransforms.IsValidIndex(BoneIndex))
		{
			const FTransform& BoneTransform = Context.BoneTransforms[BoneIndex];
			CollisionTransform = BaseTransforms[Index] * BoneTransform * ComponentToLocalSimulationSpace;
		}
		else
		{
			CollisionTransform = BaseTransforms[Index] * ComponentToLocalSimulationSpace;  // External collisions often don't map to a bone
		}
		// Reset initial states if required
		if (bHasNumCollisionsChanged)
		{
			CollisionParticles.X(Index) = CollisionTransform.GetTranslation();
			CollisionParticles.R(Index) = CollisionTransform.GetRotation();
			OldCollisionTransforms[Index] = CollisionTransform;
		}
	}, InSimDataIndex);
}

void ClothingSimulation::ExtractPhysicsAssetCollisions(const UClothingAssetCommon* Asset, int32 InSimDataIndex)
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
#endif  // #if PHYSICS_INTERFACE_PHYSX #elif WITH_CHAOS

				// Add extracted collision data
				ExtractedCollisions.Convexes.Add(Convex);
			}
#endif  // #if !PLATFORM_LUMIN && !PLATFORM_ANDROID

		}  // End for PhysAsset->SkeletalBodySetups

		// Add collisions particles
		UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Adding physics asset collisions..."));
		AddCollisions(ExtractedCollisions, UsedBoneIndices, InSimDataIndex);

	}  // End if Asset->PhysicsAsset
}

void ClothingSimulation::ExtractLegacyAssetCollisions(const UClothingAssetCommon* Asset, int32 InSimDataIndex)
{
	const FClothLODDataCommon& AssetLodData = Asset->LodData[0];
	const FClothCollisionData& LodCollData = AssetLodData.CollisionData;
	if (LodCollData.Spheres.Num() || LodCollData.SphereConnections.Num() || LodCollData.Convexes.Num())
	{
		UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Adding legacy cloth asset collisions..."));
		AddCollisions(LodCollData, Asset->UsedBoneIndices, InSimDataIndex);
	}
}

void ClothingSimulation::AddCollisions(const FClothCollisionData& ClothCollisionData, const TArray<int32>& UsedBoneIndices, int32 InSimDataIndex)
{
	TGeometryClothParticles<float, 3>& CollisionParticles = Evolution->CollisionParticles();

	// Capsules
	TSet<int32> CapsuleEnds;
	const int32 NumCapsules = ClothCollisionData.SphereConnections.Num();
	if (NumCapsules)
	{
		const uint32 Offset = Evolution->AddCollisionParticles(NumCapsules, InSimDataIndex);

		CapsuleEnds.Reserve(NumCapsules * 2);
		for (uint32 i = Offset; i < CollisionParticles.Size(); ++i)
		{
			// Always initialize the collision particle transforms before setting any geometry as otherwise NaNs gets detected during the bounding box updates
			CollisionParticles.X(i) = Chaos::TVector<float, 3>(0.f);
			CollisionParticles.R(i) = Chaos::TRotation<float, 3>::FromIdentity();

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
		const uint32 Offset = Evolution->AddCollisionParticles(NumSpheres, InSimDataIndex);
		// i = CollisionParticles index, j = Spheres index
		for (uint32 i = Offset, j = 0; j < (uint32)ClothCollisionData.Spheres.Num(); ++j)
		{
			// Skip spheres that are the end caps of capsules.
			if (CapsuleEnds.Contains(j))
				continue;

			const FClothCollisionPrim_Sphere& Sphere = ClothCollisionData.Spheres[j];

			// Always initialize the collision particle transforms before setting any geometry as otherwise NaNs gets detected during the bounding box updates
			CollisionParticles.X(i) = Chaos::TVector<float, 3>(0.f);
			CollisionParticles.R(i) = Chaos::TRotation<float, 3>::FromIdentity();

			BoneIndices[i] = GetMappedBoneIndex(UsedBoneIndices, Sphere.BoneIndex);
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Found collision sphere on bone index %d."), BoneIndices[i]);

			BaseTransforms[i] = Chaos::TRigidTransform<float, 3>(FTransform::Identity);

			CollisionParticles.SetDynamicGeometry(
				i,
				MakeUnique<Chaos::TSphere<float, 3>>(
					Sphere.LocalPosition,
					Sphere.Radius));

			++i;
		}
	}

	// Convexes
	const uint32 NumConvexes = ClothCollisionData.Convexes.Num();
	if (NumConvexes != 0)
	{
		const uint32 Offset = Evolution->AddCollisionParticles(NumConvexes, InSimDataIndex);
		for (uint32 i = Offset; i < CollisionParticles.Size(); ++i)
		{
			const FClothCollisionPrim_Convex& Convex = ClothCollisionData.Convexes[i - Offset];

			// Always initialize the collision particle transforms before setting any geometry as otherwise NaNs gets detected during the bounding box updates
			CollisionParticles.X(i) = Chaos::TVector<float, 3>(0.f);
			CollisionParticles.R(i) = Chaos::TRotation<float, 3>::FromIdentity();

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
		const uint32 Offset = Evolution->AddCollisionParticles(NumBoxes, InSimDataIndex);
		for (uint32 i = Offset; i < CollisionParticles.Size(); ++i)
		{
			const FClothCollisionPrim_Box& Box = ClothCollisionData.Boxes[i - Offset];
			
			// Always initialize the collision particle transforms before setting any geometry as otherwise NaNs gets detected during the bounding box updates
			CollisionParticles.X(i) = Chaos::TVector<float, 3>(0.f);
			CollisionParticles.R(i) = Chaos::TRotation<float, 3>::FromIdentity();

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

#if WITH_EDITOR
	const double StartTime = FPlatformTime::Seconds();
#endif 

	// Filter delta time to smoothen time variations and prevent unwanted vibrations
	static const float DeltaTimeDecay = 0.1f;
	DeltaTime = DeltaTime + (Context->DeltaSeconds - DeltaTime) * DeltaTimeDecay;

	// Set gravity, using the legacy priority: 1) game override, 2) config override, 3) world gravity
	Evolution->GetGravityForces().SetAcceleration(Chaos::TVector<float, 3>(
		bOverrideGravity ? Gravity * GravityScale :
		bUseConfigGravity ? ConfigGravity :  // Config gravity is not subject to scale
		Context->WorldGravity * GravityScale));

	// Set wind velocity, used by the velocity field lambda
	WindVelocity = Context->WindVelocity * ChaosClothingSimulationDefault::WorldScale;  // Wind speed is set in m/s and need to be converted to cm/s

	// Check teleport modes
	const bool bTeleport = (Context->TeleportMode > EClothingTeleportMode::None);
	const bool bTeleportAndReset = (Context->TeleportMode == EClothingTeleportMode::TeleportAndReset);

	// Get New Animation Positions and Normals + deal with local space & teleportation
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothGetAnimationData);

		check(OldAnimationPositions.Num() == AnimationPositions.Num());
		Swap(OldAnimationPositions, AnimationPositions);

		check(OldCollisionTransforms.Num() == CollisionTransforms.Num());
		Swap(OldCollisionTransforms, CollisionTransforms);

		// Update the local space transform
		const FVector PrevLocalSpaceLocation = LocalSpaceLocation;
		if (bUseLocalSpaceSimulation)
		{
			LocalSpaceLocation = Context->ComponentToWorld.GetLocation();
		}
		const FVector DeltaLocalSpaceLocation = LocalSpaceLocation - PrevLocalSpaceLocation;

		// Iterate all cloths
		TPBDParticles<float, 3>& Particles = Evolution->Particles();

		for (int32 Index = 0; Index < Assets.Num(); ++Index)
		{
			const UClothingAssetCommon* const Asset = Assets[Index];
			if (!Asset) { continue; }

			const uint32 Offset = IndexToRangeMap[Index][0];
			const uint32 Range = IndexToRangeMap[Index][1];

			// Update collision transforms using new local space transform
			UpdateCollisionTransforms(*Context, Index);

			// Update animation transforms via skinning
			// Optimization note:
			// This function usually receives the RootBoneTransform in order to transform the result from Component space to RootBone space.
			// (So the mesh vectors and positions (Mesh is in component space) is multiplied by Inv(RootBoneTransform) at the end of the function)
			// We actually require world space coordinates so will instead pass Inv(ComponentToWorld)
			// This saves a lot of Matrix multiplication work later
			FTransform ComponentToLocalSpace = Context->ComponentToWorld;
			ComponentToLocalSpace.AddToTranslation(-LocalSpaceLocation);

			ClothingMeshUtils::SkinPhysicsMesh<true, false>(
				Asset->UsedBoneIndices,
				Asset->LodData[0].PhysicalMeshData,
				ComponentToLocalSpace,
				Context->RefToLocals.GetData(),
				Context->RefToLocals.Num(),
				reinterpret_cast<TArray<FVector>&>(AnimationPositions),
				reinterpret_cast<TArray<FVector>&>(AnimationNormals), Offset);

			// Update root bone reference transforms
			const FTransform RootBoneTransform = Context->BoneTransforms[Asset->ReferenceBoneIndex];
			const FTransform PrevRootBoneWorldTransform = RootBoneWorldTransforms[Index];
			RootBoneWorldTransforms[Index] = RootBoneTransform * Context->ComponentToWorld;

			FTransform PrevRootBoneLocalTransform = PrevRootBoneWorldTransform;
			PrevRootBoneLocalTransform.AddToTranslation(-PrevLocalSpaceLocation);

			// Teleport & reset
			if (bTeleportAndReset)
			{
				UE_LOG(LogChaosCloth, Verbose, TEXT("Teleport & Reset"));
				for (uint32 i = Offset; i < Range; ++i)
				{
					// Update initial state for particles
					Particles.P(i) = Particles.X(i) = AnimationPositions[i];
					Particles.V(i) = Chaos::TVector<float, 3>(0.f);

					// Update anim initial state (target updated by skinning)
					OldAnimationPositions[i] = AnimationPositions[i];
				}
				ForAllCollisions([this](TGeometryClothParticles<float, 3>& CollisionParticles, uint32 i)
				{
					// Update initial state for collisions
					OldCollisionTransforms[i] = CollisionTransforms[i];
					CollisionParticles.X(i) = CollisionTransforms[i].GetTranslation();
					CollisionParticles.R(i) = CollisionTransforms[i].GetRotation();
				}, Index);
			}
			// Teleport only
			else if (bTeleport)
			{
				UE_LOG(LogChaosCloth, Verbose, TEXT("Teleport before: %s, after: %s"), *PrevRootBoneWorldTransform.ToString(), *RootBoneWorldTransforms[Index].ToString());
				const FTransform DeltaTransform = RootBoneWorldTransforms[Index].GetRelativeTransform(PrevRootBoneWorldTransform);
				const FMatrix Matrix = (PrevRootBoneLocalTransform.Inverse() * DeltaTransform * PrevRootBoneLocalTransform).ToMatrixNoScale();

				for (uint32 i = Offset; i < Range; ++i)
				{
					// Update initial state for particles
					Particles.P(i) = Particles.X(i) = Matrix.TransformPosition(Particles.X(i)) - DeltaLocalSpaceLocation;
					Particles.V(i) = Matrix.TransformVector(Particles.V(i));

					// Update anim initial state (target updated by skinning)
					OldAnimationPositions[i] = Matrix.TransformPosition(OldAnimationPositions[i]) - DeltaLocalSpaceLocation;
				}

				ForAllCollisions([this, &Matrix, &DeltaLocalSpaceLocation](TGeometryClothParticles<float, 3>& CollisionParticles, uint32 i)
				{
					// Update initial state for collisions
					OldCollisionTransforms[i] = Matrix * OldCollisionTransforms[i];
					OldCollisionTransforms[i].AddToTranslation(-DeltaLocalSpaceLocation);
					CollisionParticles.X(i) = OldCollisionTransforms[i].GetTranslation();
					CollisionParticles.R(i) = OldCollisionTransforms[i].GetRotation();
				}, Index);
			}
			// Apply reference space velocity scales
			else if (AngularDeltaRatios[Index] > KINDA_SMALL_NUMBER ||
				LinearDeltaRatios[Index].X > KINDA_SMALL_NUMBER ||
				LinearDeltaRatios[Index].Y > KINDA_SMALL_NUMBER ||
				LinearDeltaRatios[Index].Z > KINDA_SMALL_NUMBER)
			{
				// Calculate deltas
				const FTransform DeltaTransform = RootBoneWorldTransforms[Index].GetRelativeTransform(PrevRootBoneWorldTransform);

				const FVector DeltaPosition = LinearDeltaRatios[Index] * DeltaTransform.GetTranslation();

				FQuat DeltaRotation = DeltaTransform.GetRotation();
				FVector Axis;
				float DeltaAngle;
				DeltaRotation.ToAxisAndAngle(Axis, DeltaAngle);
				if (DeltaAngle > PI) { DeltaAngle -= 2.f * PI; }
				DeltaAngle *= AngularDeltaRatios[Index];
				DeltaRotation = FQuat(Axis, DeltaAngle);
				DeltaRotation.Normalize();  // ToMatrixNoScale does not like quaternions built straight from axis angles without being normalized (although they should have been already).

				// Transform points back into the previous frame of reference before applying the adjusted deltas 
				const FMatrix Matrix = (PrevRootBoneLocalTransform.Inverse() * FTransform(DeltaRotation, DeltaPosition) * PrevRootBoneLocalTransform).ToMatrixNoScale();

				for (uint32 i = Offset; i < Range; ++i)
				{
					// Update initial state for particles
					Particles.P(i) = Particles.X(i) = Matrix.TransformPosition(Particles.X(i)) - DeltaLocalSpaceLocation;
					Particles.V(i) = Matrix.TransformVector(Particles.V(i));

					// Update anim initial state (target updated by skinning)
					OldAnimationPositions[i] = Matrix.TransformPosition(OldAnimationPositions[i]) - DeltaLocalSpaceLocation;
				}

				ForAllCollisions([this, &Matrix, &DeltaLocalSpaceLocation](TGeometryClothParticles<float, 3>& CollisionParticles, uint32 i)
				{
					// Update initial state for collisions
					OldCollisionTransforms[i] = Matrix * OldCollisionTransforms[i];
					OldCollisionTransforms[i].AddToTranslation(-DeltaLocalSpaceLocation);

					CollisionParticles.X(i) = OldCollisionTransforms[i].GetTranslation();
					CollisionParticles.R(i) = OldCollisionTransforms[i].GetRotation();
				}, Index);
			}
			else if (bUseLocalSpaceSimulation)
			{
				for (uint32 i = Offset; i < Range; ++i)
				{
					// Update initial state for particles
					Particles.P(i) = Particles.X(i) -= DeltaLocalSpaceLocation;

					// Update anim initial state (target updated by skinning)
					OldAnimationPositions[i] -= DeltaLocalSpaceLocation;
				}

				ForAllCollisions([this, &DeltaLocalSpaceLocation](TGeometryClothParticles<float, 3>& CollisionParticles, uint32 i)
				{
					// Update initial state for collisions
					OldCollisionTransforms[i].AddToTranslation(-DeltaLocalSpaceLocation);
					CollisionParticles.X(i) = OldCollisionTransforms[i].GetTranslation();
				}, Index);
			}

			// Update max distance multiplier
			MaxDistancesMultipliers[Index] = Context->MaxDistanceScale;
		}
	}

	// Advance Sim
	const float SubstepDeltaTime = DeltaTime / (float)NumSubsteps;
	
	for (int32 i = 0; i < NumSubsteps; ++i)
	{
		Evolution->AdvanceOneTimeStep(SubstepDeltaTime);
	}

	Time = Evolution->GetTime();
	UE_LOG(LogChaosCloth, VeryVerbose, TEXT("DeltaTime: %.6f, FilteredDeltaTime: %.6f, Time = %.6f,  MaxPhysicsDelta = %.6f"), Context->DeltaSeconds, DeltaTime, Time, FClothingSimulationCommon::MaxPhysicsDelta);

#if WITH_EDITOR
	// Update simulation time in ms (and provide an instant average instead of the value in real-time)
	const float PrevSimulationTime = SimulationTime;  // Copy the atomic to prevent a re-read
	const float CurrSimulationTime = (float)((FPlatformTime::Seconds() - StartTime) * 1000.);
	static const float SimulationTimeDecay = 0.03f; // 0.03 seems to provide a good rate of update for the instant average
	SimulationTime = PrevSimulationTime ? PrevSimulationTime + (CurrSimulationTime - PrevSimulationTime) * SimulationTimeDecay : CurrSimulationTime;
#endif  // #if WITH_EDITOR

	// Debug draw
#if CHAOS_DEBUG_DRAW
	if (ChaosClothingSimulationConsoleVariables::CVarDebugDrawLocalSpace      .GetValueOnAnyThread()) { DebugDrawLocalSpace          (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugDrawBounds          .GetValueOnAnyThread()) { DebugDrawBounds              (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugDrawGravity         .GetValueOnAnyThread()) { DebugDrawGravity             (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugDrawPhysMeshWired   .GetValueOnAnyThread()) { DebugDrawPhysMeshWired       (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugPointNormals        .GetValueOnAnyThread()) { DebugDrawPointNormals        (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugInversedPointNormals.GetValueOnAnyThread()) { DebugDrawInversedPointNormals(); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugFaceNormals         .GetValueOnAnyThread()) { DebugDrawFaceNormals         (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugInversedFaceNormals .GetValueOnAnyThread()) { DebugDrawInversedFaceNormals (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugCollision           .GetValueOnAnyThread()) { DebugDrawCollision           (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugBackstops           .GetValueOnAnyThread()) { DebugDrawBackstops           (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugMaxDistances        .GetValueOnAnyThread()) { DebugDrawMaxDistances        (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugAnimDrive           .GetValueOnAnyThread()) { DebugDrawAnimDrive           (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugLongRangeConstraint .GetValueOnAnyThread()) { DebugDrawLongRangeConstraint (); }
	if (ChaosClothingSimulationConsoleVariables::CVarDebugWindDragForces      .GetValueOnAnyThread()) { DebugDrawWindDragForces      (); }
#endif  // #if CHAOS_DEBUG_DRAW
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
            Data.Positions[LocalIndex] = Evolution->Particles().X(j) + LocalSpaceLocation;
            Data.Normals[LocalIndex] = -PointNormals[i][LocalIndex]; // Note the Normals are inverted due to how barycentric coordinates are calculated (see GetPointBaryAndDist in ClothingMeshUtils.cpp)
		}
    }
}

FBoxSphereBounds ClothingSimulation::GetBounds(const USkeletalMeshComponent* InOwnerComponent) const
{
	FBoxSphereBounds Bounds(EForceInit::ForceInit);

	// Calculate simulation bounds (in world space)
	uint32 NumBoundedCloths = 0;
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if (const UClothingAssetCommon* const Asset = Assets[Index])
		{
			const TVector<uint32, 2> Range = IndexToRangeMap[Index];

			// Find bounds
			TAABB<float, 3> BoundingBox = TAABB<float, 3>::EmptyAABB();
			for (uint32 ParticleIndex = Range[0]; ParticleIndex < Range[1]; ++ParticleIndex)
			{
				BoundingBox.GrowToInclude(Particles.X(ParticleIndex));
			}

			// Find (squared) radius
			const TVector<float, 3> Center = BoundingBox.Center();
			float SquaredRadius = 0.f;
			for (uint32 ParticleIndex = Range[0]; ParticleIndex < Range[1]; ++ParticleIndex)
			{
				SquaredRadius = FMath::Max(SquaredRadius, (Particles.X(ParticleIndex) - Center).SizeSquared());
			}

			// Update bounds with this cloth
			const FBoxSphereBounds ClothBounds(BoundingBox.Center(), BoundingBox.Extents() * 0.5f, FMath::Sqrt(SquaredRadius));
			Bounds = (NumBoundedCloths++ == 0) ? ClothBounds : Bounds + ClothBounds;
		}
	}

	if (!bUseLocalSpaceSimulation && NumBoundedCloths && InOwnerComponent)
	{
		// Retrieve the master component (unlike the one passed to the context, this could be a slave component)
		const bool bIsUsingMaster = InOwnerComponent->MasterPoseComponent.IsValid();
		const USkinnedMeshComponent* const OwnerComponent = bIsUsingMaster ? InOwnerComponent->MasterPoseComponent.Get() : InOwnerComponent;

		// Return local bounds
		return Bounds.TransformBy(OwnerComponent->GetComponentTransform().Inverse());
	}
	return Bounds;
}

void ClothingSimulation::AddExternalCollisions(const FClothCollisionData& InData)
{
	// Keep track of the external collisions data
	ExternalCollisions.Append(InData);

	// Add new map entry
	const int32 MapIndex = ExternalCollisionsRangeMaps.AddDefaulted();
	ExternalCollisionsRangeMaps[MapIndex].AddUninitialized(Assets.Num());

	// Setup the new collisions particles for all cloths
	UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Adding external collisions..."));
	static const TArray<int32> EmptyUsedBoneIndices;  // There is no bone mapping available for external collisions
	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		ExternalCollisionsRangeMaps[MapIndex][Index][0] = Evolution->CollisionParticles().Size();
		if (Assets[Index])
		{
			AddCollisions(InData, EmptyUsedBoneIndices, Index);
		}
		ExternalCollisionsRangeMaps[MapIndex][Index][1] = Evolution->CollisionParticles().Size();

		// Keep collision transforms from previous frame if they exist
		TGeometryClothParticles<float, 3>& CollisionParticles = Evolution->CollisionParticles();
		for (uint32 i = ExternalCollisionsRangeMaps[MapIndex][Index][0];
			i < FMath::Min(ExternalCollisionsRangeMaps[MapIndex][Index][1], (uint32)CollisionTransforms.Num());
			++i)
		{
			CollisionParticles.X(i) = CollisionTransforms[i].GetLocation();
			CollisionParticles.R(i) = CollisionTransforms[i].GetRotation();
		}
	}
}

void ClothingSimulation::ClearExternalCollisions()
{
	// Remove all external collision particles, starting from the external collision offset
	// But do not resize CollisionTransforms as it is only resized in UpdateCollisionTransforms() to keep old transforms in between frames
	TGeometryClothParticles<float, 3>& CollisionParticles = Evolution->CollisionParticles();
	CollisionParticles.Resize(ExternalCollisionsOffset);  // This will also resize GroupIds, BoneIndices and BaseTransforms

	// Reset external collisions
	ExternalCollisions.Reset();

	// Reset external collision maps
	ExternalCollisionsRangeMaps.Reset();

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
		if (Asset)
		{
			const FClothLODDataCommon& ClothLodData = Asset->LodData[0];
			OutCollisions.Append(ClothLodData.CollisionData);
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
	Evolution->ResetVelocityFields();

	// Reset stats
	ResetStats();

	for (int32 SimDataIndex = 0; SimDataIndex < Assets.Num(); ++SimDataIndex)
	{
		if (const UClothingAssetCommon* const Asset = Assets[SimDataIndex])
		{
			if (const UChaosClothConfig* const ChaosClothConfig = Asset->GetClothConfig<UChaosClothConfig>())
			{
				check(Asset->GetNumLods() > 0);
				const FClothPhysicalMeshData& PhysMesh = Asset->LodData[0].PhysicalMeshData;

				ResetParticles(SimDataIndex);

				SetParticleMasses(ChaosClothConfig, PhysMesh, SimDataIndex);

				AddConstraints(ChaosClothConfig, PhysMesh, SimDataIndex);

				LinearDeltaRatios[SimDataIndex] = FVector::OneVector - ChaosClothConfig->LinearVelocityScale.BoundToBox(FVector::ZeroVector, FVector::OneVector);
				AngularDeltaRatios[SimDataIndex] = 1.f - FMath::Clamp(ChaosClothConfig->AngularVelocityScale, 0.f, 1.f);

				// Set per cloth damping, collision thickness, and friction
				Evolution->SetDamping(ChaosClothConfig->DampingCoefficient, SimDataIndex);
				Evolution->SetCollisionThickness(ChaosClothConfig->CollisionThickness, SimDataIndex);
				Evolution->SetCoefficientOfFriction(ChaosClothConfig->FrictionCoefficient, SimDataIndex);

				// Add Velocity field
				auto GetVelocity = [this](const TVector<float, 3>&)->TVector<float, 3>
				{
					return WindVelocity;
				};
				Evolution->GetVelocityFields().Emplace(
					*Meshes[SimDataIndex],
					GetVelocity,
					/*bInIsUniform =*/ true,
					ChaosClothConfig->DragCoefficient);

				// Add Self Collisions
				if (ChaosClothConfig->bUseSelfCollisions)
				{
					AddSelfCollisions(SimDataIndex);
				}

				// Update stats
				UpdateStats(SimDataIndex);
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
	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if (const UClothingAssetCommon* const Asset = Assets[Index])
		{
			ExtractCollisions(Asset, Index);
		}
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
	bOverrideGravity = true;
	Gravity = InGravityOverride;
}

void ClothingSimulation::DisableGravityOverride()
{
	bOverrideGravity = false;
}

#if WITH_EDITOR
void ClothingSimulation::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DebugClothMaterial);
}

void ClothingSimulation::DebugDrawPhysMeshShaded(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	if (!DebugClothMaterial) { return; }

	FDynamicMeshBuilder MeshBuilder(PDI->View->GetFeatureLevel());
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	int32 VertexIndex = 0;

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if (!Assets[Index]) { continue; }

		if (const TUniquePtr<Chaos::TTriangleMesh<float>>& Mesh = Meshes[Index])
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

	FMatrix LocalSimSpaceToWorld(FMatrix::Identity);
	LocalSimSpaceToWorld.SetOrigin(LocalSpaceLocation);
	MeshBuilder.Draw(PDI, LocalSimSpaceToWorld, DebugClothMaterial->GetRenderProxy(), SDPG_World, false, false);
}
#endif  // #if WITH_EDITOR

#if WITH_EDITOR || CHAOS_DEBUG_DRAW
static void DrawPoint(FPrimitiveDrawInterface* PDI, const FVector& Pos, const FLinearColor& Color, UMaterial* DebugClothMaterialVertex)  // USe color or material
{
#if CHAOS_DEBUG_DRAW
	if (!PDI)
	{
		FDebugDrawQueue::GetInstance().DrawDebugPoint(Pos, Color.ToFColor(true), false, KINDA_SMALL_NUMBER, SDPG_Foreground, 1.f);
		return;
	}
#endif
#if WITH_EDITOR
	const FMatrix& ViewMatrix = PDI->View->ViewMatrices.GetViewMatrix();
	const FVector XAxis = ViewMatrix.GetColumn(0); // Just using transpose here (orthogonal transform assumed)
	const FVector YAxis = ViewMatrix.GetColumn(1);
	DrawDisc(PDI, Pos, XAxis, YAxis, FColor::White, 0.2f, 10, DebugClothMaterialVertex->GetRenderProxy(), SDPG_World);
#endif
}

static void DrawLine(FPrimitiveDrawInterface* PDI, const FVector& Pos0, const FVector& Pos1, const FLinearColor& Color)
{
#if CHAOS_DEBUG_DRAW
	if (!PDI)
	{
		FDebugDrawQueue::GetInstance().DrawDebugLine(Pos0, Pos1, Color.ToFColor(true), false, KINDA_SMALL_NUMBER, SDPG_Foreground, 0.f);
		return;
	}
#endif
#if WITH_EDITOR
	PDI->DrawLine(Pos0, Pos1, Color, SDPG_World, 0.0f, 0.001f);
#endif
}

static void DrawArc(FPrimitiveDrawInterface* PDI, const FVector& Base, const FVector& X, const FVector& Y, float MinAngle, float MaxAngle, float Radius, const FLinearColor& Color)
{
	static const int32 Sections = 10;
	const float AngleStep = FMath::DegreesToRadians((MaxAngle - MinAngle) / (float)Sections);
	float CurrentAngle = FMath::DegreesToRadians(MinAngle);
	FVector LastVertex = Base + Radius * (FMath::Cos(CurrentAngle) * X + FMath::Sin(CurrentAngle) * Y);

	for(int32 i = 0; i < Sections; i++)
	{
		CurrentAngle += AngleStep;
		const FVector ThisVertex = Base + Radius * (FMath::Cos(CurrentAngle) * X + FMath::Sin(CurrentAngle) * Y);
		DrawLine(PDI, LastVertex, ThisVertex, Color);
		LastVertex = ThisVertex;
	}
}

static void DrawSphere(FPrimitiveDrawInterface* PDI, const TSphere<float, 3>& Sphere, const FQuat& Rotation, const FVector& Position, const FLinearColor& Color)
{
	const float Radius = Sphere.GetRadius();
	const TVector<float, 3> Center = Position + Rotation.RotateVector(Sphere.GetCenter());
#if CHAOS_DEBUG_DRAW
	if (!PDI)
	{
		FDebugDrawQueue::GetInstance().DrawDebugSphere(Center, Radius, 12, Color.ToFColor(true), false, KINDA_SMALL_NUMBER, SDPG_Foreground, 0.f);
		return;
	}
#endif
#if WITH_EDITOR
	const FTransform Transform(Rotation, Center);
	DrawWireSphere(PDI, Transform, Color, Radius, 12, SDPG_World, 0.0f, 0.001f, false);
#endif
}

static void DrawBox(FPrimitiveDrawInterface* PDI, const TBox<float, 3>& Box, const FQuat& Rotation, const FVector& Position, const FLinearColor& Color)
{
#if CHAOS_DEBUG_DRAW
	if (!PDI)
	{
		FDebugDrawQueue::GetInstance().DrawDebugBox(Position, Box.Extents() * 0.5f, Rotation, Color.ToFColor(true), false, KINDA_SMALL_NUMBER, SDPG_Foreground, 0.f);
		return;
	}
#endif
#if WITH_EDITOR
	const FMatrix BoxToWorld = FTransform(Rotation, Position).ToMatrixNoScale();
	DrawWireBox(PDI, BoxToWorld, FBox(Box.Min(), Box.Max()), Color, SDPG_World, 0.0f, 0.001f, false);
#endif
}

static void DrawCapsule(FPrimitiveDrawInterface* PDI, const TCapsule<float>& Capsule, const FQuat& Rotation, const FVector& Position, const FLinearColor& Color)
{
	const float Radius = Capsule.GetRadius();
	const float HalfHeight = Capsule.GetHeight() * 0.5f + Radius;
#if CHAOS_DEBUG_DRAW
	if (!PDI)
	{
		FDebugDrawQueue::GetInstance().DrawDebugCapsule(Position, HalfHeight, Radius, Rotation, Color.ToFColor(true), false, KINDA_SMALL_NUMBER, SDPG_Foreground, 0.f);
		return;
	}
#endif
#if WITH_EDITOR
	const FVector X = Rotation.RotateVector(FVector::ForwardVector);
	const FVector Y = Rotation.RotateVector(FVector::RightVector);
	const FVector Z = Rotation.RotateVector(FVector::UpVector);
	DrawWireCapsule(PDI, Position, X, Y, Z, Color, Radius, HalfHeight, 12, SDPG_World, 0.0f, 0.001f, false);
#endif
}

static void DrawTaperedCylinder(FPrimitiveDrawInterface* PDI, const TTaperedCylinder<float>& TaperedCylinder, const FQuat& Rotation, const FVector& Position, const FLinearColor& Color)
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

		DrawLine(PDI, LastVertex1, Vertex1, Color);
		DrawLine(PDI, LastVertex2, Vertex2, Color);
		DrawLine(PDI, LastVertex1, LastVertex2, Color);

		LastVertex1 = Vertex1;
		LastVertex2 = Vertex2;
	}
}

static void DrawConvex(FPrimitiveDrawInterface* PDI, const FConvex& Convex, const FQuat& Rotation, const FVector& Position, const FLinearColor& Color)
{
	const TArray<TPlaneConcrete<float, 3>>& Planes = Convex.GetFaces();
	for (int32 PlaneIndex1 = 0; PlaneIndex1 < Planes.Num(); ++PlaneIndex1)
	{
		const TPlaneConcrete<float, 3>& Plane1 = Planes[PlaneIndex1];

		for (int32 PlaneIndex2 = PlaneIndex1 + 1; PlaneIndex2 < Planes.Num(); ++PlaneIndex2)
		{
			const TPlaneConcrete<float, 3>& Plane2 = Planes[PlaneIndex2];

			// Find the two surface points that belong to both Plane1 and Plane2
			uint32 ParticleIndex1 = INDEX_NONE;

			const TParticles<float, 3>& SurfaceParticles = Convex.GetSurfaceParticles();
			for (uint32 ParticleIndex = 0; ParticleIndex < SurfaceParticles.Size(); ++ParticleIndex)
			{
				const TVector<float, 3>& X = SurfaceParticles.X(ParticleIndex);

				if (FMath::Square(Plane1.SignedDistance(X)) < KINDA_SMALL_NUMBER && 
					FMath::Square(Plane2.SignedDistance(X)) < KINDA_SMALL_NUMBER)
				{
					if (ParticleIndex1 != INDEX_NONE)
					{
						const TVector<float, 3>& X1 = SurfaceParticles.X(ParticleIndex1);
						const FVector Position1 = Position + Rotation.RotateVector(X1);
						const FVector Position2 = Position + Rotation.RotateVector(X);
						DrawLine(PDI, Position1, Position2, Color);
						break;
					}
					ParticleIndex1 = ParticleIndex;
				}
			}
		}
	}
}

static void DrawCoordinateSystem(FPrimitiveDrawInterface* PDI, const FQuat& Rotation, const FVector& Position)
{
	const FVector X = Rotation.RotateVector(FVector::ForwardVector) * 10.f;
	const FVector Y = Rotation.RotateVector(FVector::RightVector) * 10.f;
	const FVector Z = Rotation.RotateVector(FVector::UpVector) * 10.f;

	DrawLine(PDI, Position, Position + X, FLinearColor::Red);
	DrawLine(PDI, Position, Position + Y, FLinearColor::Green);
	DrawLine(PDI, Position, Position + Z, FLinearColor::Blue);
}

#if CHAOS_DEBUG_DRAW
void ClothingSimulation::DebugDrawBounds() const
{
	// Calculate World space bounds
	const FBoxSphereBounds Bounds = GetBounds(nullptr);

	// Draw bounds
	DrawBox(nullptr, TBox<float, 3>(-Bounds.BoxExtent, Bounds.BoxExtent), FQuat::Identity, LocalSpaceLocation + Bounds.Origin, FLinearColor(FColor::Purple));
	DrawSphere(nullptr, TSphere<float, 3>(FVector::ZeroVector, Bounds.SphereRadius), FQuat::Identity, LocalSpaceLocation + Bounds.Origin, FLinearColor(FColor::Orange));
}

void ClothingSimulation::DebugDrawGravity() const
{
	// Calculate World space bounds
	const FBoxSphereBounds Bounds = GetBounds(nullptr);

	// Draw gravity
	const FVector Pos0 = LocalSpaceLocation + Bounds.Origin;
	const FVector Pos1 = Pos0 + Evolution->GetGravityForces().GetAcceleration();
	DrawLine(nullptr, Pos0, Pos1, FLinearColor::Red);
}
#endif  // #if CHAOS_DEBUG_DRAW

void ClothingSimulation::DebugDrawPhysMeshWired(USkeletalMeshComponent* /*OwnerComponent*/, FPrimitiveDrawInterface* PDI) const
{
	static const FLinearColor DynamicColor = FColor::White;
	static const FLinearColor KinematicColor = FColor::Purple;

	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if (Assets[Index] && Meshes[Index])
		{
			const TArray<TVector<int32, 3>>& Elements = Meshes[Index]->GetElements();

			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
			{
				const auto& Element = Elements[ElementIndex];

				const FVector Pos0 = LocalSpaceLocation + Particles.X(Element.X);
				const FVector Pos1 = LocalSpaceLocation + Particles.X(Element.Y);
				const FVector Pos2 = LocalSpaceLocation + Particles.X(Element.Z);

				const bool bIsKinematic0 = Particles.InvM(Element.X) == 0.f;
				const bool bIsKinematic1 = Particles.InvM(Element.Y) == 0.f;
				const bool bIsKinematic2 = Particles.InvM(Element.Z) == 0.f;

				DrawLine(PDI, Pos0, Pos1, bIsKinematic0 && bIsKinematic1 ? KinematicColor : DynamicColor);
				DrawLine(PDI, Pos1, Pos2, bIsKinematic1 && bIsKinematic2 ? KinematicColor : DynamicColor);
				DrawLine(PDI, Pos2, Pos0, bIsKinematic2 && bIsKinematic0 ? KinematicColor : DynamicColor);
			}
		}
	}
}

void ClothingSimulation::DebugDrawPointNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if (Assets[Index])
		{
			const TVector<uint32, 2> Range = IndexToRangeMap[Index];
			const TArray<TVector<float, 3>>& MeshPointNormals = PointNormals[Index];

			for (uint32 ParticleIndex = Range[0]; ParticleIndex < Range[1]; ++ParticleIndex)
			{
				const FVector Pos0 = LocalSpaceLocation + Particles.X(ParticleIndex);
				const FVector Pos1 = Pos0 + MeshPointNormals[ParticleIndex - Range[0]] * 20.0f;

				DrawLine(PDI, Pos0, Pos1, FLinearColor::White);
			}
		}
	}
}

void ClothingSimulation::DebugDrawInversedPointNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if (Assets[Index])
		{
			const TVector<uint32, 2> Range = IndexToRangeMap[Index];
			const TArray<TVector<float, 3>>& MeshPointNormals = PointNormals[Index];

			for (uint32 ParticleIndex = Range[0]; ParticleIndex < Range[1]; ++ParticleIndex)
			{
				const FVector Pos0 = LocalSpaceLocation + Particles.X(ParticleIndex);
				const FVector Pos1 = Pos0 - MeshPointNormals[ParticleIndex - Range[0]] * 20.0f;

				DrawLine(PDI, Pos0, Pos1, FLinearColor::White);
			}
		}
	}
}

void ClothingSimulation::DebugDrawFaceNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if (Assets[Index])
		{
			const TArray<TVector<float, 3>>& MeshFaceNormals = FaceNormals[Index];

			const TArray<TVector<int32, 3>>& Elements = Meshes[Index]->GetElements();
			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
			{
				const TVector<int32, 3>& Element = Elements[ElementIndex];

				const FVector Pos0 = LocalSpaceLocation + (
					Particles.X(Element.X) +
					Particles.X(Element.Y) +
					Particles.X(Element.Z)) / 3.f;
				const FVector Pos1 = Pos0 + MeshFaceNormals[ElementIndex] * 20.0f;

				DrawLine(PDI, Pos0, Pos1, FLinearColor::Yellow);
			}
		}
	}
}

void ClothingSimulation::DebugDrawInversedFaceNormals(USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI) const
{
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if (Assets[Index])
		{
			const TArray<TVector<float, 3>>& MeshFaceNormals = FaceNormals[Index];

			const TArray<TVector<int32, 3>>& Elements = Meshes[Index]->GetElements();
			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
			{
				const TVector<int32, 3>& Element = Elements[ElementIndex];

				const FVector Pos0 = LocalSpaceLocation + (
					Particles.X(Element.X) +
					Particles.X(Element.Y) +
					Particles.X(Element.Z)) / 3.f;
				const FVector Pos1 = Pos0 - MeshFaceNormals[ElementIndex] * 20.0f;

				DrawLine(PDI, Pos0, Pos1, FLinearColor::Yellow);
			}
		}
	}
}

void ClothingSimulation::DebugDrawCollision(USkeletalMeshComponent* /*OwnerComponent*/, FPrimitiveDrawInterface* PDI) const
{
	auto DrawCollision = [this, PDI](const TVector<uint32, 2>& Ranges)
	{
		static const FLinearColor MappedColor(FColor::Cyan);
		static const FLinearColor UnmappedColor(FColor::Red);

		const TGeometryClothParticles<float, 3>& CollisionParticles = Evolution->CollisionParticles();
		for (uint32 Index = Ranges[0]; Index < Ranges[1]; ++Index)
		{
			if (const FImplicitObject* const Object = CollisionParticles.DynamicGeometry(Index).Get())
			{
				const uint32 BoneIndex = BoneIndices[Index];
				const FLinearColor Color = (BoneIndex != INDEX_NONE) ? MappedColor : UnmappedColor;

				const TVector<float, 3> Position = LocalSpaceLocation + CollisionParticles.X(Index);
				const TRotation<float, 3>& Rotation = CollisionParticles.R(Index);

				switch (Object->GetType())
				{
				case ImplicitObjectType::Sphere:
					DrawSphere(PDI, Object->GetObjectChecked<TSphere<float, 3>>(), Rotation, Position, Color);
					break;

				case ImplicitObjectType::Box:
					DrawBox(PDI, Object->GetObjectChecked<TBox<float, 3>>(), Rotation, Position, Color);
					break;

				case ImplicitObjectType::Capsule:
					DrawCapsule(PDI, Object->GetObjectChecked<TCapsule<float>>(), Rotation, Position, Color);
					break;

				case ImplicitObjectType::Union:  // Union only used as collision tapered capsules
					for (const TUniquePtr<FImplicitObject>& SubObjectPtr : Object->GetObjectChecked<FImplicitObjectUnion>().GetObjects())
					{
						if (const FImplicitObject* const SubObject = SubObjectPtr.Get())
						{
							switch (SubObject->GetType())
							{
							case ImplicitObjectType::Sphere:
								DrawSphere(PDI, SubObject->GetObjectChecked<TSphere<float, 3>>(), Rotation, Position, Color);
								break;

							case ImplicitObjectType::TaperedCylinder:
								DrawTaperedCylinder(PDI, SubObject->GetObjectChecked<TTaperedCylinder<float>>(), Rotation, Position, Color);
								break;

							default:
								break;
							}
						}
					}
					break;

				case ImplicitObjectType::Convex:
					DrawConvex(PDI, Object->GetObjectChecked<FConvex>(), Rotation, Position, Color);
					break;

				default:
					DrawCoordinateSystem(PDI, Rotation, Position);  // Draw everything else as a coordinate for now
					break;
				}
			}
		}
	};

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if (Assets[Index])
		{
			// Draw collisions
			DrawCollision(CollisionsRangeMap[Index]);

			// Draw external collisions
			for (const TArray<TVector<uint32, 2>>& ExternalCollisionsRangeMap : ExternalCollisionsRangeMaps)
			{
				DrawCollision(ExternalCollisionsRangeMap[Index]);
			}
		}
	}
}

void ClothingSimulation::DebugDrawBackstops(USkeletalMeshComponent* /*OwnerComponent*/, FPrimitiveDrawInterface* PDI) const
{
	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		const UClothingAssetCommon* const Asset = Assets[Index];
		if (!Asset) { continue; }

		// Get Backstop Distances
		const FClothLODDataCommon& AssetLodData = Asset->LodData[0];
		const FClothPhysicalMeshData& PhysMesh = AssetLodData.PhysicalMeshData;
		const FPointWeightMap& BackstopDistances = PhysMesh.GetWeightMap(EChaosWeightMapTarget::BackstopDistance);
		const FPointWeightMap& BackstopRadiuses = PhysMesh.GetWeightMap(EChaosWeightMapTarget::BackstopRadius);
		if (BackstopDistances.Num() == 0 || BackstopRadiuses.Num() == 0)
		{
			continue;
		}

		for (uint32 ParticleIndex = IndexToRangeMap[Index][0]; ParticleIndex < IndexToRangeMap[Index][1]; ++ParticleIndex)
		{
			const uint32 WeightMapIndex = ParticleIndex - IndexToRangeMap[Index][0];
			const float Radius = BackstopRadiuses[WeightMapIndex];
			const float Distance = BackstopDistances[WeightMapIndex];
			const FVector Position = LocalSpaceLocation + AnimationPositions[ParticleIndex];
			const FVector& Normal = AnimationNormals[ParticleIndex];
			DrawLine(PDI, Position, Position - Normal * (Distance - Radius), FLinearColor::White);
			if (Radius > 0.0f)
			{
				auto DrawBackstop = [Radius, Distance, &Normal, &Position, PDI](const FVector& Axis, const FLinearColor& Color)
				{
					const float ArcLength = 5.0f; // Arch length in cm
					const float ArcAngle = ArcLength * 360.0f / (Radius * 2.0f * PI);
					
					const float MaxCosAngle = 0.99f;
					if (FMath::Abs(FVector::DotProduct(Normal, Axis)) < MaxCosAngle)
					{
						DrawArc(PDI, Position - Normal * Distance, Normal, FVector::CrossProduct(Axis, Normal).GetSafeNormal(), -ArcAngle / 2.0f, ArcAngle / 2.0f, Radius, Color);
					}
				};
				DrawBackstop(FVector::ForwardVector, FLinearColor::Blue);
				DrawBackstop(FVector::UpVector, FLinearColor::Blue);
				DrawBackstop(FVector::RightVector, FLinearColor::Blue);
			}
		}
	}
}

void ClothingSimulation::DebugDrawMaxDistances(USkeletalMeshComponent* /*OwnerComponent*/, FPrimitiveDrawInterface* PDI) const
{
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();
	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		const UClothingAssetCommon* const Asset = Assets[Index];
		if (!Asset) { continue; }

		// Get Maximum Distances
		const FClothLODDataCommon& AssetLodData = Asset->LodData[0];
		const FClothPhysicalMeshData& PhysMesh = AssetLodData.PhysicalMeshData;
		const FPointWeightMap& MaxDistances = PhysMesh.GetWeightMap(EChaosWeightMapTarget::MaxDistance);
		if (MaxDistances.Num() == 0)
		{
			continue;
		}
		
		for (uint32 ParticleIndex = IndexToRangeMap[Index][0]; ParticleIndex < IndexToRangeMap[Index][1]; ++ParticleIndex)
		{
			const uint32 WeightMapIndex = ParticleIndex - IndexToRangeMap[Index][0];
			const float Distance = MaxDistances[WeightMapIndex];
			const FVector Position = LocalSpaceLocation + AnimationPositions[ParticleIndex];
			if (Particles.InvM(ParticleIndex) == 0.0f)
			{
#if WITH_EDITOR
				DrawPoint(PDI, Position, FLinearColor::Red, DebugClothMaterialVertex);
#endif
			}
			else
			{
				DrawLine(PDI, Position, Position + AnimationNormals[ParticleIndex] * Distance, FLinearColor::White);
			}
		}
	}
}

void ClothingSimulation::DebugDrawAnimDrive(USkeletalMeshComponent* /*OwnerComponent*/, FPrimitiveDrawInterface* PDI) const
{
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();
	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		const UClothingAssetCommon* const Asset = Assets[Index];
		if (!Asset) { continue; }

		// Get anim drive multiplier
		const FClothLODDataCommon& AssetLodData = Asset->LodData[0];
		const FClothPhysicalMeshData& PhysMesh = AssetLodData.PhysicalMeshData;
		const FPointWeightMap& AnimDriveMultipliers = PhysMesh.GetWeightMap(EChaosWeightMapTarget::AnimDriveMultiplier);
		if (AnimDriveMultipliers.Num() == 0)
		{
			continue;
		}

		for (uint32 ParticleIndex = IndexToRangeMap[Index][0]; ParticleIndex < IndexToRangeMap[Index][1]; ++ParticleIndex)
		{
			const uint32 WeightMapIndex = ParticleIndex - IndexToRangeMap[Index][0];
			const float Multiplier = AnimDriveMultipliers[WeightMapIndex];
			DrawLine(PDI, AnimationPositions[ParticleIndex] + LocalSpaceLocation, Particles.X(ParticleIndex) + LocalSpaceLocation, FLinearColor(FColor::Cyan) * Multiplier * AnimDriveSpringStiffness[Index]);
		}
	}
}

void ClothingSimulation::DebugDrawLongRangeConstraint(USkeletalMeshComponent* /*OwnerComponent*/, FPrimitiveDrawInterface* PDI) const
{
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		const UClothingAssetCommon* const Asset = Assets[Index];
		if (!Asset || !LongRangeConstraints[Index]) { continue; }

		const TArray<TArray<uint32>>& Constraints = LongRangeConstraints[Index]->GetConstraints();
		const TArray<float>& Dists = LongRangeConstraints[Index]->GetDists();

		for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
		{
			const TArray<uint32>& Path = Constraints[ConstraintIndex];
			const float RefDist = Dists[ConstraintIndex];
			const float CurDist = TPBDLongRangeConstraintsBase<float, 3>::ComputeGeodesicDistance(Particles, Path);
			const float Offset = CurDist - RefDist;

			const TVector<float, 3> P0 = Particles.X(Path[0]) + LocalSpaceLocation;  // Kinematic particle
			const TVector<float, 3> P1 = Particles.X(Path[Path.Num() - 1]) + LocalSpaceLocation;  // Target particle

			const TVector<float, 3> Direction = (LocalSpaceLocation + Particles.X(Path[Path.Num() - 2]) - P1).GetSafeNormal();
			const TVector<float, 3> P2 = P1 + Direction * Offset;

			DrawLine(PDI, P0, P1, FLinearColor(FColor::Purple));
			DrawLine(PDI, P1, P2, FLinearColor::Black);
		}
	}
}

void ClothingSimulation::DebugDrawWindDragForces(USkeletalMeshComponent* /*OwnerComponent*/, FPrimitiveDrawInterface* PDI) const
{
	const TPBDParticles<float, 3>& Particles = Evolution->Particles();
	const TArray<TVelocityField<float, 3>>& VelocityFields = Evolution->GetVelocityFields();

	for (const TVelocityField<float, 3>& VelocityField : VelocityFields)
	{
		const TArray<TVector<int32, 3>>& Elements = VelocityField.GetElements();
		const TArray<TVector<float, 3>>& Forces = VelocityField.GetForces();

		for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
		{
			const TVector<int32, 3>& Element = Elements[ElementIndex];
			const TVector<float, 3> Position = LocalSpaceLocation + (
				Particles.X(Element[0]) +
				Particles.X(Element[1]) +
				Particles.X(Element[2])) / 3.f;
			const TVector<float, 3>& Force = Forces[ElementIndex];
			DrawLine(PDI, Position, Position + Force, FColor::Green);
		}
	}
}

void ClothingSimulation::DebugDrawLocalSpace(USkeletalMeshComponent* /*OwnerComponent*/, FPrimitiveDrawInterface* PDI) const
{
	// Draw local space
	DrawCoordinateSystem(PDI, FQuat::Identity, LocalSpaceLocation);

	// Draw reference spaces
	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if (Assets[Index])
		{
			DrawCoordinateSystem(PDI, RootBoneWorldTransforms[Index].GetRotation(), RootBoneWorldTransforms[Index].GetLocation());
		}
	}
}
#endif  // #if WITH_EDITOR || CHAOS_DEBUG_DRAW
