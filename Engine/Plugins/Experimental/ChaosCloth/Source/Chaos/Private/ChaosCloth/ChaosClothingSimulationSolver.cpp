// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosCloth/ChaosClothingSimulationSolver.h"
#include "ChaosCloth/ChaosClothingSimulationCloth.h"
#include "ChaosCloth/ChaosClothingSimulationCollider.h"
#include "ChaosCloth/ChaosClothingSimulationMesh.h"
#include "ChaosCloth/ChaosClothingSimulation.h"
#include "ChaosCloth/ChaosClothPrivate.h"
#include "Chaos/PBDEvolution.h"

#if !UE_BUILD_SHIPPING
#include "FramePro/FramePro.h"
#include "HAL/IConsoleManager.h"
#else
#define FRAMEPRO_ENABLED 0
#endif

using namespace Chaos;

DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update"), STAT_ChaosClothSolverUpdate, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Cloths"), STAT_ChaosClothSolverUpdateCloths, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Pre Solver Step"), STAT_ChaosClothSolverUpdatePreSolverStep, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Solver Step"), STAT_ChaosClothSolverUpdateSolverStep, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Post Solver Step"), STAT_ChaosClothSolverUpdatePostSolverStep, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Calculate Bounds"), STAT_ChaosClothSolverCalculateBounds, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Particle Pre Simulation Transforms"), STAT_ChaosClothParticlePreSimulationTransforms, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Collision Pre Simulation Transforms"), STAT_ChaosClothCollisionPreSimulationTransforms, STATGROUP_ChaosCloth);

static int32 ChaosClothSolverMinParallelBatchSize = 1000;
static bool bChaosClothSolverParallelClothPreUpdate = false;  // TODO: Doesn't seem to improve much here. Review this after the ISPC implementation.
static bool bChaosClothSolverParallelClothUpdate = true;
static bool bChaosClothSolverParallelClothPostUpdate = true;

#if !UE_BUILD_SHIPPING
FAutoConsoleVariableRef CVarChaosClothSolverMinParallelBatchSize(TEXT("p.ChaosCloth.Solver.MinParallelBatchSize"), ChaosClothSolverMinParallelBatchSize, TEXT("The minimum number of particle to process in parallel batch by the solver."));
FAutoConsoleVariableRef CVarChaosClothSolverParallelClothPreUpdate(TEXT("p.ChaosCloth.Solver.ParallelClothPreUpdate"), bChaosClothSolverParallelClothPreUpdate, TEXT("Pre-transform the cloth particles for each cloth in parallel."));
FAutoConsoleVariableRef CVarChaosClothSolverParallelClothUpdate(TEXT("p.ChaosCloth.Solver.ParallelClothUpdate"), bChaosClothSolverParallelClothUpdate, TEXT("Skin the physics mesh and do the other cloth update for each cloth in parallel."));
FAutoConsoleVariableRef CVarChaosClothSolverParallelClothPostUpdate(TEXT("p.ChaosCloth.Solver.ParallelClothPostUpdate"), bChaosClothSolverParallelClothPostUpdate, TEXT("Pre-transform the cloth particles for each cloth in parallel."));
#endif

namespace ChaosClothingSimulationSolverDefault
{
	static const FVec3 Gravity(0.f, 0.f, -980.665f);  // cm/s^2
	static const FVec3 WindVelocity(0.f);
	static const FRealSingle WindFluidDensity = 1.225e-6f;  // kg/cm^3
	static const int32 NumIterations = 1;
	static const int32 NumSubsteps = 1;
	static const FRealSingle SelfCollisionThickness = 2.f;
	static const FRealSingle CollisionThickness = 1.2f;
	static const FRealSingle FrictionCoefficient = 0.2f;
	static const FRealSingle DampingCoefficient = 0.01f;
}

namespace ChaosClothingSimulationSolverConstant
{
	static const FReal WorldScale = 100.f;  // World is in cm, but values like wind speed and density are in SI unit and relates to m.
	static const FReal StartDeltaTime = 1.f / 30.f;  // Initialize filtered timestep at 30fps
}

FClothingSimulationSolver::FClothingSimulationSolver()
	: OldLocalSpaceLocation(0.f)
	, LocalSpaceLocation(0.f)
	, Time(0)
	, DeltaTime(ChaosClothingSimulationSolverConstant::StartDeltaTime)
	, NumIterations(ChaosClothingSimulationSolverDefault::NumIterations)
	, NumSubsteps(ChaosClothingSimulationSolverDefault::NumSubsteps)
	, CollisionParticlesOffset(0)
	, CollisionParticlesSize(0)
	, Gravity(ChaosClothingSimulationSolverDefault::Gravity)
	, WindVelocity(ChaosClothingSimulationSolverDefault::WindVelocity)
	, LegacyWindAdaption(0.f)
	, WindFluidDensity(ChaosClothingSimulationSolverDefault::WindFluidDensity)
	, bIsClothGravityOverrideEnabled(false)
{
	FPBDParticles LocalParticles;
	FKinematicGeometryClothParticles TRigidParticles;
	Evolution.Reset(
		new FPBDEvolution(
			MoveTemp(LocalParticles),
			MoveTemp(TRigidParticles),
			{}, // CollisionTriangles
			ChaosClothingSimulationSolverDefault::NumIterations,
			ChaosClothingSimulationSolverDefault::CollisionThickness,
			ChaosClothingSimulationSolverDefault::SelfCollisionThickness,
			ChaosClothingSimulationSolverDefault::FrictionCoefficient,
			ChaosClothingSimulationSolverDefault::DampingCoefficient));

	// Add simulation groups arrays
	Evolution->AddArray(&PreSimulationTransforms);
	Evolution->AddArray(&FictitiousAngularDisplacement);

	Evolution->Particles().AddArray(&Normals);
	Evolution->Particles().AddArray(&OldAnimationPositions);
	Evolution->Particles().AddArray(&AnimationPositions);
	Evolution->Particles().AddArray(&AnimationNormals);

	Evolution->CollisionParticles().AddArray(&CollisionBoneIndices);
	Evolution->CollisionParticles().AddArray(&CollisionBaseTransforms);
	Evolution->CollisionParticles().AddArray(&OldCollisionTransforms);
	Evolution->CollisionParticles().AddArray(&CollisionTransforms);

	Evolution->SetKinematicUpdateFunction(
		[this](FPBDParticles& ParticlesInput, const FReal Dt, const FReal LocalTime, const int32 Index)
		{
			const FReal Alpha = (LocalTime - Time) / DeltaTime;
			ParticlesInput.P(Index) = Alpha * AnimationPositions[Index] + (1.f - Alpha) * OldAnimationPositions[Index];  // X is the step initial condition, here it's P that needs to be updated so that constraints works with the correct step target
		});

	Evolution->SetCollisionKinematicUpdateFunction(
		[this](FKinematicGeometryClothParticles& ParticlesInput, const FReal Dt, const FReal LocalTime, const int32 Index)
		{
			checkSlow(Dt > SMALL_NUMBER && DeltaTime > SMALL_NUMBER);
			const FReal Alpha = (LocalTime - Time) / DeltaTime;
			const FVec3 NewX =
				Alpha * CollisionTransforms[Index].GetTranslation() + (1.f - Alpha) * OldCollisionTransforms[Index].GetTranslation();
			ParticlesInput.V(Index) = (NewX - ParticlesInput.X(Index)) / Dt;
			ParticlesInput.X(Index) = NewX;
			FRotation3 NewR = FQuat::Slerp(OldCollisionTransforms[Index].GetRotation(), CollisionTransforms[Index].GetRotation(), Alpha);
			FRotation3 Delta = NewR * ParticlesInput.R(Index).Inverse();
			FVec3 Axis;
			FReal Angle;
			Delta.ToAxisAndAngle(Axis, Angle);
			ParticlesInput.W(Index) = Axis * Angle / Dt;
			ParticlesInput.R(Index) = NewR;
		});
}

FClothingSimulationSolver::~FClothingSimulationSolver()
{
}

void FClothingSimulationSolver::SetLocalSpaceLocation(const FVec3& InLocalSpaceLocation, bool bReset)
{
	LocalSpaceLocation = InLocalSpaceLocation;
	if (bReset)
	{
		OldLocalSpaceLocation = InLocalSpaceLocation;
	}
}

void FClothingSimulationSolver::SetCloths(TArray<FClothingSimulationCloth*>&& InCloths)
{
	// Remove old cloths
	RemoveCloths();

	// Update array
	Cloths = MoveTemp(InCloths);

	// Add the new cloths' particles
	for (FClothingSimulationCloth* const Cloth : Cloths)
	{
		check(Cloth);

		// Add the cloth's particles
		Cloth->Add(this);

		// Set initial state
		Cloth->PreUpdate(this);
		Cloth->Update(this);
	}

	// Update external collision's offset
	CollisionParticlesOffset = Evolution->CollisionParticles().Size();
}

void FClothingSimulationSolver::AddCloth(FClothingSimulationCloth* InCloth)
{
	check(InCloth);

	if (Cloths.Find(InCloth) != INDEX_NONE)
	{
		return;
	}

	// Add the cloth to the solver update array
	Cloths.Emplace(InCloth);

	// Reset external collisions so that there is never any external collision particles below cloth's ones
	ResetCollisionParticles(CollisionParticlesOffset);

	// Add the cloth's particles
	InCloth->Add(this);

	// Set initial state
	InCloth->PreUpdate(this);
	InCloth->Update(this);

	// Update external collision's offset
	CollisionParticlesOffset = Evolution->CollisionParticles().Size();
}

void FClothingSimulationSolver::RemoveCloth(FClothingSimulationCloth* InCloth)
{
	if (Cloths.Find(InCloth) == INDEX_NONE)
	{
		return;
	}

	// Remove reference to this solver
	InCloth->Remove(this);

	// Remove collider from array
	Cloths.RemoveSwap(InCloth);

	// Reset collisions so that there is never any external collision particles below the cloth's ones
	ResetCollisionParticles();

	// Reset cloth particles and associated elements
	ResetParticles();

	// Re-add the remaining cloths' particles
	for (FClothingSimulationCloth* const Cloth : Cloths)
	{
		// Add the cloth's particles
		Cloth->Add(this);

		// Set initial state
		Cloth->PreUpdate(this);
		Cloth->Update(this);
	}

	// Update external collision's offset
	CollisionParticlesOffset = Evolution->CollisionParticles().Size();
}

void FClothingSimulationSolver::RemoveCloths()
{
	// Remove all cloths from array
	for (FClothingSimulationCloth* const Cloth : Cloths)
	{
		Cloth->Remove(this);
	}
	Cloths.Reset();

	// Reset solver collisions
	ResetCollisionParticles();

	// Reset cloth particles and associated elements
	ResetParticles();
}

void FClothingSimulationSolver::RefreshCloth(FClothingSimulationCloth* InCloth)
{
	if (Cloths.Find(InCloth) == INDEX_NONE)
	{
		return;
	}

	// TODO: Add different ways to refresh cloths without recreating everything (collisions, constraints, particles)
	RefreshCloths();
}

void FClothingSimulationSolver::RefreshCloths()
{
	// Remove the cloths' & collisions' particles
	for (FClothingSimulationCloth* const Cloth : Cloths)
	{
		// Remove any solver data held by the cloth 
		Cloth->Remove(this);
	}

	// Reset collision particles
	ResetCollisionParticles();

	// Reset cloth particles and associated elements
	ResetParticles();

	// Re-add the cloths' & collisions' particles
	for (FClothingSimulationCloth* const Cloth : Cloths)
	{
		// Re-Add the cloth's and collisions' particles
		Cloth->Add(this);

		// Set initial state
		Cloth->PreUpdate(this);
		Cloth->Update(this);
	}

	// Update solver collider's offset
	CollisionParticlesOffset = Evolution->CollisionParticles().Size();
}

void FClothingSimulationSolver::ResetParticles()
{
	Evolution->ResetParticles();
	Evolution->ResetConstraintRules();
	Evolution->ResetSelfCollision();
	ClothsConstraints.Reset();
}

int32 FClothingSimulationSolver::AddParticles(int32 NumParticles, uint32 GroupId)
{
	if (!NumParticles)
	{
		return INDEX_NONE;
	}
	const int32 Offset = Evolution->AddParticleRange(NumParticles, GroupId, /*bActivate =*/ false);

	// Add an empty constraints container for this range
	check(!ClothsConstraints.Find(Offset));  // We cannot already have this Offset in the map, particle ranges are always added, never removed (unless reset)

	ClothsConstraints.Emplace(Offset, MakeUnique<FClothConstraints>())
		->Initialize(Evolution.Get(), AnimationPositions, OldAnimationPositions, AnimationNormals, Offset, NumParticles);

	// Always starts with particles disabled
	EnableParticles(Offset, false);

	return Offset;
}

void FClothingSimulationSolver::EnableParticles(int32 Offset, bool bEnable)
{
	Evolution->ActivateParticleRange(Offset, bEnable);
	GetClothConstraints(Offset).Enable(bEnable);
}

const FVec3* FClothingSimulationSolver::GetParticlePs(int32 Offset) const
{
	return &Evolution->Particles().P(Offset);
}

FVec3* FClothingSimulationSolver::GetParticlePs(int32 Offset)
{
	return &Evolution->Particles().P(Offset);
}

const FVec3* FClothingSimulationSolver::GetParticleXs(int32 Offset) const
{
	return &Evolution->Particles().X(Offset);
}

FVec3* FClothingSimulationSolver::GetParticleXs(int32 Offset)
{
	return &Evolution->Particles().X(Offset);
}

const FVec3* FClothingSimulationSolver::GetParticleVs(int32 Offset) const
{
	return &Evolution->Particles().V(Offset);
}

FVec3* FClothingSimulationSolver::GetParticleVs(int32 Offset)
{
	return &Evolution->Particles().V(Offset);
}

const FReal* FClothingSimulationSolver::GetParticleInvMasses(int32 Offset) const
{
	return &Evolution->Particles().InvM(Offset);
}

void FClothingSimulationSolver::ResetCollisionParticles(int32 InCollisionParticlesOffset)
{
	Evolution->ResetCollisionParticles(InCollisionParticlesOffset);
	CollisionParticlesOffset = InCollisionParticlesOffset;
	CollisionParticlesSize = 0;
}

int32 FClothingSimulationSolver::AddCollisionParticles(int32 NumCollisionParticles, uint32 GroupId, int32 RecycledOffset)
{
	// Try reusing the particle range
	// This is used by external collisions so that they can be added/removed between every solver update.
	// If it doesn't match then remove all ranges above the given offset to start again.
	// This rely on the assumption that these ranges are added again in the same update order.
	if (RecycledOffset == CollisionParticlesOffset + CollisionParticlesSize)
	{
		CollisionParticlesSize += NumCollisionParticles;

		// Check that the range still exists
		if (CollisionParticlesOffset + CollisionParticlesSize <= (int32)Evolution->CollisionParticles().Size() &&  // Check first that the range hasn't been reset
			NumCollisionParticles == Evolution->GetCollisionParticleRangeSize(RecycledOffset))  // This will assert if range has been reset
		{
			return RecycledOffset;
		}
		// Size has changed. must reset this collision range (and all of those following up) and reallocate some new particles
		Evolution->ResetCollisionParticles(RecycledOffset);
	}

	if (!NumCollisionParticles)
	{
		return INDEX_NONE;
	}

	const int32 Offset = Evolution->AddCollisionParticleRange(NumCollisionParticles, GroupId, /*bActivate =*/ false);

	// Always initialize the collision particle's transforms as otherwise setting the geometry will get NaNs detected during the bounding box updates
	FRotation3* const Rs = GetCollisionParticleRs(Offset);
	FVec3* const Xs = GetCollisionParticleXs(Offset);

	for (int32 Index = 0; Index < NumCollisionParticles; ++Index)
	{
		Xs[Index] = FVec3(0.f);
		Rs[Index] = FRotation3::FromIdentity();
	}

	// Always starts with particles disabled
	EnableCollisionParticles(Offset, false);

	return Offset;
}

void FClothingSimulationSolver::EnableCollisionParticles(int32 Offset, bool bEnable)
{
	Evolution->ActivateCollisionParticleRange(Offset, bEnable);
}

const FVec3* FClothingSimulationSolver::GetCollisionParticleXs(int32 Offset) const
{
	return &Evolution->CollisionParticles().X(Offset);
}

FVec3* FClothingSimulationSolver::GetCollisionParticleXs(int32 Offset)
{
	return &Evolution->CollisionParticles().X(Offset);
}

const FRotation3* FClothingSimulationSolver::GetCollisionParticleRs(int32 Offset) const
{
	return &Evolution->CollisionParticles().R(Offset);
}

FRotation3* FClothingSimulationSolver::GetCollisionParticleRs(int32 Offset)
{
	return &Evolution->CollisionParticles().R(Offset);
}

void FClothingSimulationSolver::SetCollisionGeometry(int32 Offset, int32 Index, TUniquePtr<FImplicitObject>&& Geometry)
{
	Evolution->CollisionParticles().SetDynamicGeometry(Offset + Index, MoveTemp(Geometry));
}

const TUniquePtr<FImplicitObject>* FClothingSimulationSolver::GetCollisionGeometries(int32 Offset) const
{
	return &Evolution->CollisionParticles().DynamicGeometry(Offset);
}

const bool* FClothingSimulationSolver::GetCollisionStatus(int32 Offset) const
{
	return Evolution->GetCollisionStatus().GetData() + Offset;
}

const TArray<FVec3>& FClothingSimulationSolver::GetCollisionContacts() const
{
	return Evolution->GetCollisionContacts();
}

const TArray<FVec3>& FClothingSimulationSolver::GetCollisionNormals() const
{
	return Evolution->GetCollisionNormals();
}

void FClothingSimulationSolver::SetParticleMassUniform(int32 Offset, FReal UniformMass, FReal MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	// Retrieve the particle block size
	const int32 Size = Evolution->GetParticleRangeSize(Offset);

	// Set mass from uniform mass
	const TSet<int32> Vertices = Mesh.GetVertices();
	FPBDParticles& Particles = Evolution->Particles();
	for (int32 Index = Offset; Index < Offset + Size; ++Index)
	{
		Particles.M(Index) = Vertices.Contains(Index) ? UniformMass : 0.f;
	}

	// Clamp and enslave
	ParticleMassClampAndEnslave(Offset, Size, MinPerParticleMass, KinematicPredicate);
}

void FClothingSimulationSolver::SetParticleMassFromTotalMass(int32 Offset, FReal TotalMass, FReal MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	// Retrieve the particle block size
	const int32 Size = Evolution->GetParticleRangeSize(Offset);

	// Set mass per area
	const FReal TotalArea = SetParticleMassPerArea(Offset, Size, Mesh);

	// Find density
	const FReal Density = TotalArea > 0.f ? TotalMass / TotalArea : 1.f;

	// Update mass from mesh and density
	ParticleMassUpdateDensity(Mesh, Density);

	// Clamp and enslave
	ParticleMassClampAndEnslave(Offset, Size, MinPerParticleMass, KinematicPredicate);
}

void FClothingSimulationSolver::SetParticleMassFromDensity(int32 Offset, FReal Density, FReal MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	// Retrieve the particle block size
	const int32 Size = Evolution->GetParticleRangeSize(Offset);

	// Set mass per area
	const FReal TotalArea = SetParticleMassPerArea(Offset, Size, Mesh);

	// Set density from cm2 to m2
	Density /= FMath::Square(ChaosClothingSimulationSolverConstant::WorldScale);

	// Update mass from mesh and density
	ParticleMassUpdateDensity(Mesh, Density);

	// Clamp and enslave
	ParticleMassClampAndEnslave(Offset, Size, MinPerParticleMass, KinematicPredicate);
}

void FClothingSimulationSolver::SetReferenceVelocityScale(
	uint32 GroupId,
	const FRigidTransform3& OldReferenceSpaceTransform,
	const FRigidTransform3& ReferenceSpaceTransform,
	const FVec3& LinearVelocityScale,
	FReal AngularVelocityScale,
	FReal FictitiousAngularScale)
{
	FRigidTransform3 OldRootBoneLocalTransform = OldReferenceSpaceTransform;
	OldRootBoneLocalTransform.AddToTranslation(-OldLocalSpaceLocation);

	// Calculate deltas
	const FTransform DeltaTransform = ReferenceSpaceTransform.GetRelativeTransform(OldReferenceSpaceTransform);

	// Apply linear velocity scale
	const FVec3 LinearRatio = FVec3(1.f) - LinearVelocityScale.BoundToBox(FVec3(0.f), FVec3(1.f));
	const FVec3 DeltaPosition = LinearRatio * DeltaTransform.GetTranslation();

	// Apply angular velocity scale
	FRotation3 DeltaRotation = DeltaTransform.GetRotation();
	FVec3 Axis;
	FReal DeltaAngle;
	DeltaRotation.ToAxisAndAngle(Axis, DeltaAngle);
	if (DeltaAngle > PI)
	{
		DeltaAngle -= 2.f * PI;
	}

	const FReal PartialDeltaAngle = DeltaAngle * FMath::Clamp(1.f - AngularVelocityScale, 0.f, 1.f);
	DeltaRotation = FQuat(Axis, PartialDeltaAngle);

	// Transform points back into the previous frame of reference before applying the adjusted deltas 
	PreSimulationTransforms[GroupId] = OldRootBoneLocalTransform.Inverse() * FTransform(DeltaRotation, DeltaPosition) * OldRootBoneLocalTransform;

	// Save the reference bone relative angular velocity for calculating the fictitious forces
	FictitiousAngularDisplacement[GroupId] = ReferenceSpaceTransform.TransformVector(Axis * PartialDeltaAngle * FMath::Min(2.f, FictitiousAngularScale));  // Clamp to 2x the delta angle
}

FReal FClothingSimulationSolver::SetParticleMassPerArea(int32 Offset, int32 Size, const FTriangleMesh& Mesh)
{
	// Zero out masses
	FPBDParticles& Particles = Evolution->Particles();
	for (int32 Index = Offset; Index < Offset + Size; ++Index)
	{
		Particles.M(Index) = 0.f;
	}

	// Assign per particle mass proportional to connected area.
	const TArray<TVec3<int32>>& SurfaceElements = Mesh.GetSurfaceElements();
	FReal TotalArea = (FReal)0.f;
	for (const TVec3<int32>& Tri : SurfaceElements)
	{
		const FReal TriArea = 0.5f * FVec3::CrossProduct(
			Particles.X(Tri[1]) - Particles.X(Tri[0]),
			Particles.X(Tri[2]) - Particles.X(Tri[0])).Size();
		TotalArea += TriArea;
		const FReal ThirdTriArea = TriArea / 3.f;
		Particles.M(Tri[0]) += ThirdTriArea;
		Particles.M(Tri[1]) += ThirdTriArea;
		Particles.M(Tri[2]) += ThirdTriArea;
	}

	UE_LOG(LogChaosCloth, Verbose, TEXT("Total area: %f, SI total area: %f"), TotalArea, TotalArea / FMath::Square(ChaosClothingSimulationSolverConstant::WorldScale));
	return TotalArea;
}

void FClothingSimulationSolver::ParticleMassUpdateDensity(const FTriangleMesh& Mesh, FReal Density)
{
	const TSet<int32> Vertices = Mesh.GetVertices();
	FPBDParticles& Particles = Evolution->Particles();
	FReal TotalMass = 0.f;
	for (const int32 Vertex : Vertices)
	{
		Particles.M(Vertex) *= Density;
		TotalMass += Particles.M(Vertex);
	}

	UE_LOG(LogChaosCloth, Verbose, TEXT("Total mass: %f, "), TotalMass);
}

void FClothingSimulationSolver::ParticleMassClampAndEnslave(int32 Offset, int32 Size, FReal MinPerParticleMass, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	FPBDParticles& Particles = Evolution->Particles();
	for (int32 Index = Offset; Index < Offset + Size; ++Index)
	{
		Particles.M(Index) = FMath::Max(Particles.M(Index), MinPerParticleMass);
		Particles.InvM(Index) = KinematicPredicate(Index - Offset) ? 0.f : 1.f / Particles.M(Index);
	}
}

void FClothingSimulationSolver::SetProperties(uint32 GroupId, FRealSingle DampingCoefficient, FRealSingle CollisionThickness, FRealSingle FrictionCoefficient)
{
	Evolution->SetDamping(DampingCoefficient, GroupId);
	Evolution->SetCollisionThickness(CollisionThickness, GroupId);
	Evolution->SetCoefficientOfFriction(FrictionCoefficient, GroupId);
}

void FClothingSimulationSolver::SetUseCCD(uint32 GroupId, bool bUseCCD)
{
	Evolution->SetUseCCD(bUseCCD, GroupId);
}

void FClothingSimulationSolver::SetGravity(uint32 GroupId, const FVec3& InGravity)
{
	Evolution->GetGravityForces(GroupId).SetAcceleration(InGravity);
}

void FClothingSimulationSolver::SetWindVelocity(const FVec3& InWindVelocity, FRealSingle InLegacyWindAdaption)
{
	WindVelocity = InWindVelocity * ChaosClothingSimulationSolverConstant::WorldScale;
	LegacyWindAdaption = InLegacyWindAdaption;
}

void FClothingSimulationSolver::SetWindVelocity(uint32 GroupId, const FVec3& InWindVelocity)
{
	FVelocityField& VelocityField = Evolution->GetVelocityField(GroupId);
	VelocityField.SetVelocity(InWindVelocity);
}

void FClothingSimulationSolver::SetWindVelocityField(uint32 GroupId, FRealSingle DragCoefficient, FRealSingle LiftCoefficient, const FTriangleMesh* TriangleMesh)
{
	FVelocityField& VelocityField = Evolution->GetVelocityField(GroupId);
	VelocityField.SetGeometry(TriangleMesh);
	VelocityField.SetCoefficients(DragCoefficient, LiftCoefficient);
}

const FVelocityField& FClothingSimulationSolver::GetWindVelocityField(uint32 GroupId)
{
	return Evolution->GetVelocityField(GroupId);
}

void FClothingSimulationSolver::AddExternalForces(uint32 GroupId, bool bUseLegacyWind)
{
	if (Evolution)
	{
		bool bHasVelocityField = false;
		bool bHasForceField = false;

		if (!PerSolverField.IsEmpty())
		{
			TArray<FVector>& SamplePositions = PerSolverField.GetSamplePositions();
			TArray<FFieldContextIndex>& SampleIndices = PerSolverField.GetSampleIndices();

			const uint32 NumParticles = Evolution->Particles().Size();

			SamplePositions.SetNum(NumParticles,false);
			SampleIndices.SetNum(NumParticles,false);

			for (uint32 Index = 0; Index < NumParticles; ++Index)
			{
				SamplePositions[Index] = Evolution->Particles().X(Index) + LocalSpaceLocation;
				SampleIndices[Index] = FFieldContextIndex(Index, Index);
			}
			PerSolverField.ComputeFieldLinearImpulse(GetTime());

			if (PerSolverField.GetVectorResults(EFieldVectorType::Vector_LinearVelocity).Num() > 0)
			{
				bHasVelocityField = true;
			}
			if (PerSolverField.GetVectorResults(EFieldVectorType::Vector_LinearForce).Num() > 0)
			{
				bHasForceField = true;
			}
		}

		const FVec3& AngularDisplacement = FictitiousAngularDisplacement[GroupId];
		const bool bHasFictitiousForces = !AngularDisplacement.IsNearlyZero();

		static const FReal LegacyWindMultiplier = (FReal)25.;
		const FVec3 LegacyWindVelocity = WindVelocity * LegacyWindMultiplier;

		Evolution->GetForceFunction(GroupId) =
			[this, bHasVelocityField, bHasForceField, bHasFictitiousForces, bUseLegacyWind, LegacyWindVelocity, AngularDisplacement](FPBDParticles& Particles, const FReal Dt, const int32 Index)
			{
				FVec3 Forces((FReal)0.);

				if (bHasVelocityField)
				{
					const TArray<FVector>& LinearVelocities = PerSolverField.GetVectorResults(EFieldVectorType::Vector_LinearVelocity);
					Forces += LinearVelocities[Index] * Particles.M(Index) / Dt;
				}

				if (bHasForceField)
				{
					const TArray<FVector>& LinearForces = PerSolverField.GetVectorResults(EFieldVectorType::Vector_LinearForce);
					Forces += LinearForces[Index];
				}

				if (bHasFictitiousForces)
				{
					const FVec3& X = Particles.X(Index);
					const FVec3 W = AngularDisplacement / Dt;
					const FReal& M = Particles.M(Index);
#if 0
					// Coriolis + Centrifugal seems a bit overkilled, but let's keep the code around in case it's ever required
					const FVec3& V = Particles.V(Index);
					Forces -= (FVec3::CrossProduct(W, V) * 2.f + FVec3::CrossProduct(W, FVec3::CrossProduct(W, X))) * M;
#else
					// Centrifugal force
					Forces -= FVec3::CrossProduct(W, FVec3::CrossProduct(W, X)) * M;
#endif
				}
				
				if (bUseLegacyWind)
				{
					// Calculate wind velocity delta
					const FVec3 VelocityDelta = LegacyWindVelocity - Particles.V(Index);

					FVec3 Direction = VelocityDelta;
					if (Direction.Normalize())
					{
						// Scale by angle
						const FReal DirectionDot = FVec3::DotProduct(Direction, Normals[Index]);
						const FReal ScaleFactor = FMath::Min(1.f, FMath::Abs(DirectionDot) * LegacyWindAdaption);
						Forces += VelocityDelta * ScaleFactor * Particles.M(Index);
					}
				}

				Particles.F(Index) += Forces;
			};
	}
}

void FClothingSimulationSolver::ApplyPreSimulationTransforms()
{
	const FVec3 DeltaLocalSpaceLocation = LocalSpaceLocation - OldLocalSpaceLocation;

	const TPBDActiveView<FPBDParticles>& ParticlesActiveView = Evolution->ParticlesActiveView();
	const TArray<uint32>& ParticleGroupIds = Evolution->ParticleGroupIds();

	ParticlesActiveView.RangeFor(
		[this, &ParticleGroupIds, &DeltaLocalSpaceLocation](FPBDParticles& Particles, int32 Offset, int32 Range)
		{
			SCOPE_CYCLE_COUNTER(STAT_ChaosClothParticlePreSimulationTransforms);

			const int32 RangeSize = Range - Offset;

			PhysicsParallelFor(RangeSize,
				[this, &ParticleGroupIds, &DeltaLocalSpaceLocation, &Particles, Offset](int32 i)
				{
					const int32 Index = Offset + i;
					const FRigidTransform3& GroupSpaceTransform = PreSimulationTransforms[ParticleGroupIds[Index]];

					// Update initial state for particles
					Particles.P(Index) = Particles.X(Index) = GroupSpaceTransform.TransformPositionNoScale(Particles.X(Index)) - DeltaLocalSpaceLocation;
					Particles.V(Index) = GroupSpaceTransform.TransformVector(Particles.V(Index));

					// Update anim initial state (target updated by skinning)
					OldAnimationPositions[Index] = GroupSpaceTransform.TransformPositionNoScale(OldAnimationPositions[Index]) - DeltaLocalSpaceLocation;
				}, RangeSize < ChaosClothSolverMinParallelBatchSize);
		}, /*bForceSingleThreaded =*/ !bChaosClothSolverParallelClothPreUpdate);

#if FRAMEPRO_ENABLED
	FRAMEPRO_CUSTOM_STAT("ChaosClothSolverMinParallelBatchSize", ChaosClothSolverMinParallelBatchSize, "ChaosClothSolver", "Particles", FRAMEPRO_COLOUR(128,0,255));
	FRAMEPRO_CUSTOM_STAT("ChaosClothSolverParallelClothPreUpdate", bChaosClothSolverParallelClothPreUpdate, "ChaosClothSolver", "Enabled", FRAMEPRO_COLOUR(128, 128, 64));
#endif

	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothCollisionPreSimulationTransforms);

		const TPBDActiveView<FKinematicGeometryClothParticles>& CollisionParticlesActiveView = Evolution->CollisionParticlesActiveView();
		const TArray<uint32>& CollisionParticleGroupIds = Evolution->CollisionParticleGroupIds();

		CollisionParticlesActiveView.SequentialFor(  // There's unlikely to ever have enough collision particles for a parallel for
			[this, &CollisionParticleGroupIds, &DeltaLocalSpaceLocation](FKinematicGeometryClothParticles& CollisionParticles, int32 Index)
			{
				const FRigidTransform3& GroupSpaceTransform = PreSimulationTransforms[CollisionParticleGroupIds[Index]];

				// Update initial state for collisions
				OldCollisionTransforms[Index] = OldCollisionTransforms[Index] * GroupSpaceTransform;
				OldCollisionTransforms[Index].AddToTranslation(-DeltaLocalSpaceLocation);
				CollisionParticles.X(Index) = OldCollisionTransforms[Index].GetTranslation();
				CollisionParticles.R(Index) = OldCollisionTransforms[Index].GetRotation();
			});
	}
}

void FClothingSimulationSolver::Update(FReal InDeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdate);

	// Filter delta time to smoothen time variations and prevent unwanted vibrations
	static const FReal DeltaTimeDecay = 0.1f;
	const FReal PrevDeltaTime = DeltaTime;
	DeltaTime = DeltaTime + (InDeltaTime - DeltaTime) * DeltaTimeDecay;

	// Update Cloths and cloth colliders
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdateCloths);

		Swap(OldCollisionTransforms, CollisionTransforms);
		Swap(OldAnimationPositions, AnimationPositions);

		// Clear external collisions so that they can be re-added
		CollisionParticlesSize = 0;

		// Run sequential pre-updates first
		for (FClothingSimulationCloth* const Cloth : Cloths)
		{
			Cloth->PreUpdate(this);
		}

		// Run parallel update
		PhysicsParallelFor(Cloths.Num(), [this](int32 ClothIndex)
		{
			FClothingSimulationCloth* const Cloth = Cloths[ClothIndex];
			const uint32 GroupId = Cloth->GetGroupId();

			// Pre-update overridable solver properties first
			Evolution->GetGravityForces(GroupId).SetAcceleration(Gravity);
			Evolution->GetVelocityField(GroupId).SetVelocity(WindVelocity);
			Evolution->GetVelocityField(GroupId).SetFluidDensity(WindFluidDensity);

			Cloth->Update(this);
		}, /*bForceSingleThreaded =*/ !bChaosClothSolverParallelClothUpdate);
	}

	// Pre solver step, apply group space transforms for teleport and linear/delta ratios, ...etc
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdatePreSolverStep);

		ApplyPreSimulationTransforms();
	}

	// Advance Sim
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdateSolverStep);
		SCOPE_CYCLE_COUNTER(STAT_ClothInternalSolve);

		Evolution->SetIterations(NumIterations);

		const FReal SubstepDeltaTime = DeltaTime / (FReal)NumSubsteps;
	
		for (int32 i = 0; i < NumSubsteps; ++i)
		{
			Evolution->AdvanceOneTimeStep(SubstepDeltaTime);
		}

		Time = Evolution->GetTime();
		UE_LOG(LogChaosCloth, VeryVerbose, TEXT("DeltaTime: %.6f, FilteredDeltaTime: %.6f, Time = %.6f"), InDeltaTime, DeltaTime, Time);
	}

	// Post solver step, update normals, ...etc
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdatePostSolverStep);
		SCOPE_CYCLE_COUNTER(STAT_ClothComputeNormals);

		PhysicsParallelFor(Cloths.Num(), [this](int32 ClothIndex)
		{
			FClothingSimulationCloth* const Cloth = Cloths[ClothIndex];
			Cloth->PostUpdate(this);
		}, /*bForceSingleThreaded =*/ !bChaosClothSolverParallelClothPostUpdate);
	}

	// Save old space location for next update
	OldLocalSpaceLocation = LocalSpaceLocation;
}

FBoxSphereBounds FClothingSimulationSolver::CalculateBounds() const
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverCalculateBounds);

	const TPBDActiveView<FPBDParticles>& ParticlesActiveView = Evolution->ParticlesActiveView();

	if (ParticlesActiveView.HasActiveRange())
	{
		// Calculate bounding box
		FAABB3 BoundingBox = FAABB3::EmptyAABB();

		ParticlesActiveView.SequentialFor(
			[&BoundingBox](FPBDParticles& Particles, int32 Index)
			{
				BoundingBox.GrowToInclude(Particles.X(Index));
			});

		// Calculate (squared) radius
		const FVec3 Center = BoundingBox.Center();
		FReal SquaredRadius = 0.f;

		ParticlesActiveView.SequentialFor(
			[&SquaredRadius, &Center](FPBDParticles& Particles, int32 Index)
			{
				SquaredRadius = FMath::Max(SquaredRadius, (Particles.X(Index) - Center).SizeSquared());
			});

		// Update bounds with this cloth
		return FBoxSphereBounds(LocalSpaceLocation + BoundingBox.Center(), BoundingBox.Extents() * 0.5f, FMath::Sqrt(SquaredRadius));
	}

	return FBoxSphereBounds(LocalSpaceLocation, FVector(0.f), 0.f);
}
