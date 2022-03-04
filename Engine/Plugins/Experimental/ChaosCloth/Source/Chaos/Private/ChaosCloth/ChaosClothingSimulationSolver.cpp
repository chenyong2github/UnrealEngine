// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosCloth/ChaosClothingSimulationSolver.h"
#include "ChaosCloth/ChaosClothingSimulationCloth.h"
#include "ChaosCloth/ChaosClothingSimulationCollider.h"
#include "ChaosCloth/ChaosClothingSimulationMesh.h"
#include "ChaosCloth/ChaosClothingSimulation.h"
#include "ChaosCloth/ChaosClothPrivate.h"
#include "Chaos/PBDEvolution.h"
#if INTEL_ISPC
#include "ChaosClothingSimulationSolver.ispc.generated.h"
#endif

#if !UE_BUILD_SHIPPING
#include "FramePro/FramePro.h"
#include "HAL/IConsoleManager.h"
#else
#define FRAMEPRO_ENABLED 0
#endif

DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update"), STAT_ChaosClothSolverUpdate, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Cloths"), STAT_ChaosClothSolverUpdateCloths, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Pre Solver Step"), STAT_ChaosClothSolverUpdatePreSolverStep, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Solver Step"), STAT_ChaosClothSolverUpdateSolverStep, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Post Solver Step"), STAT_ChaosClothSolverUpdatePostSolverStep, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Calculate Bounds"), STAT_ChaosClothSolverCalculateBounds, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Particle Pre Simulation Transforms"), STAT_ChaosClothParticlePreSimulationTransforms, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Collision Pre Simulation Transforms"), STAT_ChaosClothCollisionPreSimulationTransforms, STATGROUP_ChaosCloth);

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_PreSimulationTransforms_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosPreSimulationTransformsISPCEnabled(TEXT("p.Chaos.PreSimulationTransforms.ISPC"), bChaos_PreSimulationTransforms_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in ApplySimulationTransforms"));
bool bChaos_CalculateBounds_ISPC_Enabled = bChaos_CalculateBounds_ISPC_Enable;  // Disabled by default
FAutoConsoleVariableRef CVarChaosCalculateBoundsISPCEnabled(TEXT("p.Chaos.CalculateBounds.ISPC"), bChaos_CalculateBounds_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in CalculateBounds"));

static_assert(sizeof(ispc::FVector3f) == sizeof(Chaos::Softs::FSolverVec3), "sizeof(ispc::FVector) != sizeof(Chaos::Softs::FSolverVec3)");
static_assert(sizeof(ispc::FTransform3f) == sizeof(Chaos::Softs::FSolverRigidTransform3), "sizeof(ispc::FVector) != sizeof(Chaos::Softs::FSolverRigidTransform3)");
#endif

namespace Chaos
{

static int32 ClothSolverMinParallelBatchSize = 1000;
static bool bClothSolverParallelClothPreUpdate = true;
static bool bClothSolverParallelClothUpdate = true;
static bool bClothSolverParallelClothPostUpdate = true;
static bool bClothSolverUseImprovedTimeStepSmoothing = true;
static bool bClothSolverDisableTimeDependentNumIterations = false;

#if !UE_BUILD_SHIPPING
static int32 ClothSolverDebugHitchLength = 0;
static int32 ClothSolverDebugHitchInterval = 0;
static bool bClothSolverDisableCollision = false;

FAutoConsoleVariableRef CVarClothSolverMinParallelBatchSize(TEXT("p.ChaosCloth.Solver.MinParallelBatchSize"), ClothSolverMinParallelBatchSize, TEXT("The minimum number of particle to process in parallel batch by the solver."));
FAutoConsoleVariableRef CVarClothSolverParallelClothPreUpdate(TEXT("p.ChaosCloth.Solver.ParallelClothPreUpdate"), bClothSolverParallelClothPreUpdate, TEXT("Pre-transform the cloth particles for each cloth in parallel."));
FAutoConsoleVariableRef CVarClothSolverParallelClothUpdate(TEXT("p.ChaosCloth.Solver.ParallelClothUpdate"), bClothSolverParallelClothUpdate, TEXT("Skin the physics mesh and do the other cloth update for each cloth in parallel."));
FAutoConsoleVariableRef CVarClothSolverParallelClothPostUpdate(TEXT("p.ChaosCloth.Solver.ParallelClothPostUpdate"), bClothSolverParallelClothPostUpdate, TEXT("Pre-transform the cloth particles for each cloth in parallel."));
FAutoConsoleVariableRef CVarClothSolverDebugHitchLength(TEXT("p.ChaosCloth.Solver.DebugHitchLength"), ClothSolverDebugHitchLength, TEXT("Hitch length in ms. Create artificial hitches to debug simulation jitter. 0 to disable"));
FAutoConsoleVariableRef CVarClothSolverDebugHitchInterval(TEXT("p.ChaosCloth.Solver.DebugHitchInterval"), ClothSolverDebugHitchInterval, TEXT("Hitch interval in frames. Create artificial hitches to debug simulation jitter. 0 to disable"));
FAutoConsoleVariableRef CVarClothSolverDisableCollision(TEXT("p.ChaosCloth.Solver.DisableCollision"), bClothSolverDisableCollision, TEXT("Disable all collision particles. Needs reset of the simulation (p.ChaosCloth.Reset)."));
#endif

FAutoConsoleVariableRef CVarClothSolverUseImprovedTimeStepSmoothing(TEXT("p.ChaosCloth.Solver.UseImprovedTimeStepSmoothing"), bClothSolverUseImprovedTimeStepSmoothing, TEXT("Use the time step smoothing on input forces only rather than on the entire cloth solver, in order to avoid miscalculating velocities."));
FAutoConsoleVariableRef CVarClothSolverDisableTimeDependentNumIterations(TEXT("p.ChaosCloth.Solver.DisableTimeDependentNumIterations"), bClothSolverDisableTimeDependentNumIterations, TEXT("Make the number of iterations independent from the time step."));

namespace ClothingSimulationSolverDefault
{
	static const Softs::FSolverVec3 Gravity((Softs::FSolverReal)0., (Softs::FSolverReal)0., (Softs::FSolverReal)-980.665);  // cm/s^2
	static const Softs::FSolverVec3 WindVelocity((Softs::FSolverReal)0.);
	static const int32 NumIterations = 1;
	static const int32 MaxNumIterations = 10;
	static const int32 NumSubsteps = 1;
	static const FRealSingle SelfCollisionThickness = 2.f;
	static const FRealSingle CollisionThickness = 1.2f;
	static const FRealSingle FrictionCoefficient = 0.2f;
	static const FRealSingle DampingCoefficient = 0.01f;
	static const FRealSingle LocalDampingCoefficient = 0.f;
}

namespace ClothingSimulationSolverConstant
{
	static const FRealSingle WorldScale = 100.f;  // World is in cm, but values like wind speed and density are in SI unit and relates to m.
	static const Softs::FSolverReal StartDeltaTime = (Softs::FSolverReal)(1. / 30.);  // Initialize filtered timestep at 30fps
}

FClothingSimulationSolver::FClothingSimulationSolver()
	: OldLocalSpaceLocation(0.)
	, LocalSpaceLocation(0.)
	, Time(0.)
	, DeltaTime(ClothingSimulationSolverConstant::StartDeltaTime)
	, NumIterations(ClothingSimulationSolverDefault::NumIterations)
	, MaxNumIterations(ClothingSimulationSolverDefault::MaxNumIterations)
	, NumSubsteps(ClothingSimulationSolverDefault::NumSubsteps)
	, CollisionParticlesOffset(0)
	, CollisionParticlesSize(0)
	, Gravity(ClothingSimulationSolverDefault::Gravity)
	, WindVelocity(ClothingSimulationSolverDefault::WindVelocity)
	, LegacyWindAdaption(0.f)
	, bIsClothGravityOverrideEnabled(false)
{
	Softs::FSolverParticles LocalParticles;
	Softs::FSolverRigidParticles RigidParticles;
	Evolution.Reset(
		new Softs::FPBDEvolution(
			MoveTemp(LocalParticles),
			MoveTemp(RigidParticles),
			{}, // CollisionTriangles
			FMath::Min(ClothingSimulationSolverDefault::NumIterations, ClothingSimulationSolverDefault::MaxNumIterations),
			(Softs::FSolverReal)ClothingSimulationSolverDefault::CollisionThickness,
			(Softs::FSolverReal)ClothingSimulationSolverDefault::SelfCollisionThickness,
			(Softs::FSolverReal)ClothingSimulationSolverDefault::FrictionCoefficient,
			(Softs::FSolverReal)ClothingSimulationSolverDefault::DampingCoefficient,
			(Softs::FSolverReal)ClothingSimulationSolverDefault::LocalDampingCoefficient));

	// Add simulation groups arrays
	Evolution->AddArray(&PreSimulationTransforms);
	Evolution->AddArray(&FictitiousAngularDisplacements);

	Evolution->Particles().AddArray(&Normals);
	Evolution->Particles().AddArray(&OldAnimationPositions);
	Evolution->Particles().AddArray(&AnimationPositions);
	Evolution->Particles().AddArray(&AnimationNormals);

	Evolution->CollisionParticles().AddArray(&CollisionBoneIndices);
	Evolution->CollisionParticles().AddArray(&CollisionBaseTransforms);
	Evolution->CollisionParticles().AddArray(&OldCollisionTransforms);
	Evolution->CollisionParticles().AddArray(&CollisionTransforms);

	Evolution->SetKinematicUpdateFunction(
		[this](Softs::FSolverParticles& ParticlesInput, const Softs::FSolverReal Dt, const Softs::FSolverReal LocalTime, const int32 Index)
		{
			const Softs::FSolverReal Alpha = (LocalTime - Time) / DeltaTime;
			ParticlesInput.P(Index) = Alpha * AnimationPositions[Index] + ((Softs::FSolverReal)1. - Alpha) * OldAnimationPositions[Index];  // X is the step initial condition, here it's P that needs to be updated so that constraints works with the correct step target
		});

	Evolution->SetCollisionKinematicUpdateFunction(
		[this](Softs::FSolverRigidParticles& ParticlesInput, const Softs::FSolverReal Dt, const Softs::FSolverReal LocalTime, const int32 Index)
		{
			checkSlow(Dt > SMALL_NUMBER && DeltaTime > SMALL_NUMBER);
			const Softs::FSolverReal Alpha = (LocalTime - Time) / DeltaTime;
			const Softs::FSolverVec3 NewX =
				Alpha * CollisionTransforms[Index].GetTranslation() + ((Softs::FSolverReal)1. - Alpha) * OldCollisionTransforms[Index].GetTranslation();
			ParticlesInput.V(Index) = (NewX - ParticlesInput.X(Index)) / Dt;
			ParticlesInput.X(Index) = NewX;
			const Softs::FSolverRotation3 NewR = Softs::FSolverRotation3::Slerp(OldCollisionTransforms[Index].GetRotation(), CollisionTransforms[Index].GetRotation(), Alpha);
			const Softs::FSolverRotation3 Delta = NewR * ParticlesInput.R(Index).Inverse();
			const Softs::FSolverReal Angle = Delta.GetAngle();
			const Softs::FSolverVec3 Axis = Delta.GetRotationAxis();
			ParticlesInput.W(Index) = (Softs::FSolverVec3)Axis * Angle / Dt;
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

void FClothingSimulationSolver::ResetStartPose(int32 Offset, int32 NumParticles)
{
	Softs::FPAndInvM* const PandInvMs = GetParticlePandInvMs(Offset);
	Softs::FSolverVec3* const Xs = GetParticleXs(Offset);
	Softs::FSolverVec3* const Vs = GetParticleVs(Offset);
	const Softs::FSolverVec3* const Positions = GetAnimationPositions(Offset);
	Softs::FSolverVec3* const OldPositions = GetOldAnimationPositions(Offset);

	for (int32 Index = 0; Index < NumParticles; ++Index)
	{
		PandInvMs[Index].P = Xs[Index] = OldPositions[Index] = Positions[Index];
		Vs[Index] = Softs::FSolverVec3(0.);
	}
}

const Softs::FPAndInvM* FClothingSimulationSolver::GetParticlePandInvMs(int32 Offset) const
{
	return &Evolution->Particles().PAndInvM(Offset);
}

Softs::FPAndInvM* FClothingSimulationSolver::GetParticlePandInvMs(int32 Offset)
{
	return &Evolution->Particles().PAndInvM(Offset);
}

const Softs::FSolverVec3* FClothingSimulationSolver::GetParticleXs(int32 Offset) const
{
	return &Evolution->Particles().X(Offset);
}

Softs::FSolverVec3* FClothingSimulationSolver::GetParticleXs(int32 Offset)
{
	return &Evolution->Particles().X(Offset);
}

const Softs::FSolverVec3* FClothingSimulationSolver::GetParticleVs(int32 Offset) const
{
	return &Evolution->Particles().V(Offset);
}

Softs::FSolverVec3* FClothingSimulationSolver::GetParticleVs(int32 Offset)
{
	return &Evolution->Particles().V(Offset);
}

const Softs::FSolverReal* FClothingSimulationSolver::GetParticleInvMasses(int32 Offset) const
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
	Softs::FSolverRotation3* const Rs = GetCollisionParticleRs(Offset);
	Softs::FSolverVec3* const Xs = GetCollisionParticleXs(Offset);

	for (int32 Index = 0; Index < NumCollisionParticles; ++Index)
	{
		Xs[Index] = Softs::FSolverVec3(0.);
		Rs[Index] = Softs::FSolverRotation3::FromIdentity();
	}

	// Always starts with particles disabled
	EnableCollisionParticles(Offset, false);

	return Offset;
}

void FClothingSimulationSolver::EnableCollisionParticles(int32 Offset, bool bEnable)
{
#if !UE_BUILD_SHIPPING
	if (bClothSolverDisableCollision)
	{
		Evolution->ActivateCollisionParticleRange(Offset, false);
	}
	else
#endif  // #if !UE_BUILD_SHIPPING
	{
		Evolution->ActivateCollisionParticleRange(Offset, bEnable);
	}
}

void FClothingSimulationSolver::ResetCollisionStartPose(int32 Offset, int32 NumCollisionParticles)
{
	const Softs::FSolverRigidTransform3* const Transforms = GetCollisionTransforms(Offset);
	Softs::FSolverRigidTransform3* const OldTransforms = GetOldCollisionTransforms(Offset);
	Softs::FSolverRotation3* const Rs = GetCollisionParticleRs(Offset);
	Softs::FSolverVec3* const Xs = GetCollisionParticleXs(Offset);

	for (int32 Index = 0; Index < NumCollisionParticles; ++Index)
	{
		OldTransforms[Index] = Transforms[Index];
		Xs[Index] = Transforms[Index].GetTranslation();
		Rs[Index] = Transforms[Index].GetRotation();
	}
}

const Softs::FSolverVec3* FClothingSimulationSolver::GetCollisionParticleXs(int32 Offset) const
{
	return &Evolution->CollisionParticles().X(Offset);
}

Softs::FSolverVec3* FClothingSimulationSolver::GetCollisionParticleXs(int32 Offset)
{
	return &Evolution->CollisionParticles().X(Offset);
}

const Softs::FSolverRotation3* FClothingSimulationSolver::GetCollisionParticleRs(int32 Offset) const
{
	return &Evolution->CollisionParticles().R(Offset);
}

Softs::FSolverRotation3* FClothingSimulationSolver::GetCollisionParticleRs(int32 Offset)
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

const TArray<Softs::FSolverVec3>& FClothingSimulationSolver::GetCollisionContacts() const
{
	return Evolution->GetCollisionContacts();
}

const TArray<Softs::FSolverVec3>& FClothingSimulationSolver::GetCollisionNormals() const
{
	return Evolution->GetCollisionNormals();
}

void FClothingSimulationSolver::SetParticleMassUniform(int32 Offset, FRealSingle UniformMass, FRealSingle MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	// Retrieve the particle block size
	const int32 Size = Evolution->GetParticleRangeSize(Offset);

	// Set mass from uniform mass
	const TSet<int32> Vertices = Mesh.GetVertices();
	Softs::FSolverParticles& Particles = Evolution->Particles();
	for (int32 Index = Offset; Index < Offset + Size; ++Index)
	{
		Particles.M(Index) = Vertices.Contains(Index) ? (Softs::FSolverReal)UniformMass : (Softs::FSolverReal)0.;
	}

	// Clamp and enslave
	ParticleMassClampAndEnslave(Offset, Size, (Softs::FSolverReal)MinPerParticleMass, KinematicPredicate);
}

void FClothingSimulationSolver::SetParticleMassFromTotalMass(int32 Offset, FRealSingle TotalMass, FRealSingle MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	// Retrieve the particle block size
	const int32 Size = Evolution->GetParticleRangeSize(Offset);

	// Set mass per area
	const Softs::FSolverReal TotalArea = SetParticleMassPerArea(Offset, Size, Mesh);

	// Find density
	const Softs::FSolverReal Density = TotalArea > (Softs::FSolverReal)0. ? (Softs::FSolverReal)TotalMass / TotalArea : (Softs::FSolverReal)1.;

	// Update mass from mesh and density
	ParticleMassUpdateDensity(Mesh, Density);

	// Clamp and enslave
	ParticleMassClampAndEnslave(Offset, Size, (Softs::FSolverReal)MinPerParticleMass, KinematicPredicate);
}

void FClothingSimulationSolver::SetParticleMassFromDensity(int32 Offset, FRealSingle Density, FRealSingle MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	// Retrieve the particle block size
	const int32 Size = Evolution->GetParticleRangeSize(Offset);

	// Set mass per area
	const Softs::FSolverReal TotalArea = SetParticleMassPerArea(Offset, Size, Mesh);

	// Set density from cm2 to m2
	const Softs::FSolverReal DensityScaled = (Softs::FSolverReal)(Density / FMath::Square(ClothingSimulationSolverConstant::WorldScale));

	// Update mass from mesh and density
	ParticleMassUpdateDensity(Mesh, DensityScaled);

	// Clamp and enslave
	ParticleMassClampAndEnslave(Offset, Size, (Softs::FSolverReal)MinPerParticleMass, KinematicPredicate);
}

void FClothingSimulationSolver::SetReferenceVelocityScale(
	uint32 GroupId,
	const FRigidTransform3& OldReferenceSpaceTransform,  // Transforms are in world space so have to be FReal based for LWC
	const FRigidTransform3& ReferenceSpaceTransform,
	const TVec3<FRealSingle>& LinearVelocityScale,
	FRealSingle AngularVelocityScale,
	FRealSingle FictitiousAngularScale)
{
	FRigidTransform3 OldRootBoneLocalTransform = OldReferenceSpaceTransform;
	OldRootBoneLocalTransform.AddToTranslation(-OldLocalSpaceLocation);

	// Calculate deltas
	const FRigidTransform3 DeltaTransform = ReferenceSpaceTransform.GetRelativeTransform(OldReferenceSpaceTransform);

	// Apply linear velocity scale
	const FVec3 LinearRatio = FVec3(1.) - FVec3(LinearVelocityScale).BoundToBox(FVec3(0.), FVec3(1.));
	const FVec3 DeltaPosition = LinearRatio * DeltaTransform.GetTranslation();

	// Apply angular velocity scale
	FRotation3 DeltaRotation = DeltaTransform.GetRotation();
	FReal DeltaAngle = DeltaRotation.GetAngle();
	FVec3 Axis = DeltaRotation.GetRotationAxis();
	if (DeltaAngle > (FReal)PI)
	{
		DeltaAngle -= (FReal)2. * (FReal)PI;
	}

	const FReal PartialDeltaAngle = DeltaAngle * FMath::Clamp((FReal)1. - (FReal)AngularVelocityScale, (FReal)0., (FReal)1.);
	DeltaRotation = UE::Math::TQuat<FReal>(Axis, PartialDeltaAngle);

	// Transform points back into the previous frame of reference before applying the adjusted deltas
	const FRigidTransform3 PreSimulationTransform = OldRootBoneLocalTransform.Inverse() * FRigidTransform3(DeltaPosition, DeltaRotation) * OldRootBoneLocalTransform;
	PreSimulationTransforms[GroupId] = Softs::FSolverRigidTransform3(  // Store the delta in solver precision, no need for LWC here
		Softs::FSolverVec3(PreSimulationTransform.GetTranslation()),
		Softs::FSolverRotation3(PreSimulationTransform.GetRotation()));

	// Save the reference bone relative angular velocity for calculating the fictitious forces
	const FVec3 FictitiousAngularDisplacement = ReferenceSpaceTransform.TransformVector(Axis * PartialDeltaAngle * FMath::Min((FReal)2., (FReal)FictitiousAngularScale));  // Clamp to 2x the delta angle
	FictitiousAngularDisplacements[GroupId] = Softs::FSolverVec3(FictitiousAngularDisplacement);
}

Softs::FSolverReal FClothingSimulationSolver::SetParticleMassPerArea(int32 Offset, int32 Size, const FTriangleMesh& Mesh)
{
	// Zero out masses
	Softs::FSolverParticles& Particles = Evolution->Particles();
	for (int32 Index = Offset; Index < Offset + Size; ++Index)
	{
		Particles.M(Index) = (Softs::FSolverReal)0.;
	}

	// Assign per particle mass proportional to connected area.
	const TArray<TVec3<int32>>& SurfaceElements = Mesh.GetSurfaceElements();
	Softs::FSolverReal TotalArea = (Softs::FSolverReal)0.;
	for (const TVec3<int32>& Tri : SurfaceElements)
	{
		const Softs::FSolverReal TriArea = (Softs::FSolverReal)0.5 * Softs::FSolverVec3::CrossProduct(
			Particles.X(Tri[1]) - Particles.X(Tri[0]),
			Particles.X(Tri[2]) - Particles.X(Tri[0])).Size();
		TotalArea += TriArea;
		const Softs::FSolverReal ThirdTriArea = TriArea / (Softs::FSolverReal)3.;
		Particles.M(Tri[0]) += ThirdTriArea;
		Particles.M(Tri[1]) += ThirdTriArea;
		Particles.M(Tri[2]) += ThirdTriArea;
	}

	UE_LOG(LogChaosCloth, Verbose, TEXT("Total area: %f, SI total area: %f"), TotalArea, TotalArea / FMath::Square(ClothingSimulationSolverConstant::WorldScale));
	return TotalArea;
}

void FClothingSimulationSolver::ParticleMassUpdateDensity(const FTriangleMesh& Mesh, Softs::FSolverReal Density)
{
	const TSet<int32> Vertices = Mesh.GetVertices();
	Softs::FSolverParticles& Particles = Evolution->Particles();
	FReal TotalMass = 0.f;
	for (const int32 Vertex : Vertices)
	{
		Particles.M(Vertex) *= Density;
		TotalMass += Particles.M(Vertex);
	}

	UE_LOG(LogChaosCloth, Verbose, TEXT("Total mass: %f, "), TotalMass);
}

void FClothingSimulationSolver::ParticleMassClampAndEnslave(int32 Offset, int32 Size, Softs::FSolverReal MinPerParticleMass, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	Softs::FSolverParticles& Particles = Evolution->Particles();
	for (int32 Index = Offset; Index < Offset + Size; ++Index)
	{
		Particles.M(Index) = FMath::Max(Particles.M(Index), MinPerParticleMass);
		Particles.InvM(Index) = KinematicPredicate(Index - Offset) ? (Softs::FSolverReal)0. : (Softs::FSolverReal)1. / Particles.M(Index);
	}
}

void FClothingSimulationSolver::SetProperties(uint32 GroupId, FRealSingle DampingCoefficient, FRealSingle LocalDampingCoefficient, FRealSingle CollisionThickness, FRealSingle FrictionCoefficient)
{
	Evolution->SetDamping(DampingCoefficient, GroupId);
	Evolution->SetLocalDamping(LocalDampingCoefficient, GroupId);
	Evolution->SetCollisionThickness(CollisionThickness, GroupId);
	Evolution->SetCoefficientOfFriction(FrictionCoefficient, GroupId);
}

void FClothingSimulationSolver::SetUseCCD(uint32 GroupId, bool bUseCCD)
{
	Evolution->SetUseCCD(bUseCCD, GroupId);
}

void FClothingSimulationSolver::SetGravity(uint32 GroupId, const TVec3<FRealSingle>& InGravity)
{
	Evolution->SetGravity(Softs::FSolverVec3(InGravity), GroupId);
}

void FClothingSimulationSolver::SetWindVelocity(const TVec3<FRealSingle>& InWindVelocity, FRealSingle InLegacyWindAdaption)
{
	WindVelocity = InWindVelocity * ClothingSimulationSolverConstant::WorldScale;
	LegacyWindAdaption = InLegacyWindAdaption;
}

void FClothingSimulationSolver::SetWindVelocity(uint32 GroupId, const TVec3<FRealSingle>& InWindVelocity)
{
	Softs::FVelocityField& VelocityField = Evolution->GetVelocityField(GroupId);
	VelocityField.SetVelocity(Softs::FSolverVec3(InWindVelocity));
}

void FClothingSimulationSolver::SetWindGeometry(uint32 GroupId, const FTriangleMesh& TriangleMesh, const TConstArrayView<FRealSingle>& DragMultipliers, const TConstArrayView<FRealSingle>& LiftMultipliers)
{
	Softs::FVelocityField& VelocityField = Evolution->GetVelocityField(GroupId);
	VelocityField.SetGeometry(&TriangleMesh, DragMultipliers, LiftMultipliers);
}

void FClothingSimulationSolver::SetWindProperties(uint32 GroupId, const TVec2<FRealSingle>& Drag, const TVec2<FRealSingle>& Lift, FRealSingle AirDensity)
{
	Softs::FVelocityField& VelocityField = Evolution->GetVelocityField(GroupId);
	VelocityField.SetProperties(Drag, Lift, AirDensity);
}

const Softs::FVelocityField& FClothingSimulationSolver::GetWindVelocityField(uint32 GroupId)
{
	return Evolution->GetVelocityField(GroupId);
}

void FClothingSimulationSolver::AddExternalForces(uint32 GroupId, bool bUseLegacyWind)
{
	if (Evolution)
	{
		const bool bHasVelocityField = !PerSolverField.GetOutputResults(EFieldCommandOutputType::LinearVelocity).IsEmpty();
		const bool bHasForceField = !PerSolverField.GetOutputResults(EFieldCommandOutputType::LinearForce).IsEmpty();

		const FVec3& AngularDisplacement = FictitiousAngularDisplacements[GroupId];
		const bool bHasFictitiousForces = !AngularDisplacement.IsNearlyZero();

		static const FReal LegacyWindMultiplier = (FReal)25.;
		const FVec3 LegacyWindVelocity = WindVelocity * LegacyWindMultiplier;

		Evolution->GetForceFunction(GroupId) =
			[this, bHasVelocityField, bHasForceField, bHasFictitiousForces, bUseLegacyWind, LegacyWindVelocity, AngularDisplacement](Softs::FSolverParticles& Particles, const FReal Dt, const int32 Index)
			{
				FVec3 Forces((FReal)0.);

				if (bHasVelocityField)
				{
					const TArray<FVector>& LinearVelocities = PerSolverField.GetOutputResults(EFieldCommandOutputType::LinearVelocity);
					Forces += LinearVelocities[Index] * Particles.M(Index) / Dt;
				}

				if (bHasForceField)
				{
					const TArray<FVector>& LinearForces = PerSolverField.GetOutputResults(EFieldCommandOutputType::LinearForce);
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

				Particles.Acceleration(Index) += FVector3f(Forces) * Particles.InvM(Index);
			};
	}
}

void FClothingSimulationSolver::ApplyPreSimulationTransforms()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationSolver_ApplyPreSimulationTransforms);
	const Softs::FSolverVec3 DeltaLocalSpaceLocation(LocalSpaceLocation - OldLocalSpaceLocation);  // LWC, note that LocalSpaceLocation is FReal based. but the delta can be stored in FSolverReal

	const TPBDActiveView<Softs::FSolverParticles>& ParticlesActiveView = Evolution->ParticlesActiveView();
	const TArray<uint32>& ParticleGroupIds = Evolution->ParticleGroupIds();

	ParticlesActiveView.RangeFor(
		[this, &ParticleGroupIds, &DeltaLocalSpaceLocation](Softs::FSolverParticles& Particles, int32 Offset, int32 Range)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationSolver_ParticlePreSimulationTransforms);
			SCOPE_CYCLE_COUNTER(STAT_ChaosClothParticlePreSimulationTransforms);

			const int32 RangeSize = Range - Offset;

#if INTEL_ISPC
			if (bRealTypeCompatibleWithISPC && bChaos_PreSimulationTransforms_ISPC_Enabled)  // TODO: Make the ISPC works with both Single and Double depending on the FSolverReal type
			{
				ispc::ApplyPreSimulationTransforms(
					(ispc::FVector4f*)Particles.GetPAndInvM().GetData(),
					(ispc::FVector3f*)Particles.GetV().GetData(),
					(ispc::FVector3f*)Particles.XArray().GetData(),
					(ispc::FVector3f*)OldAnimationPositions.GetData(),
					Particles.GetInvM().GetData(),
					ParticleGroupIds.GetData(),
					(ispc::FTransform3f*)PreSimulationTransforms.GetData(),
					(ispc::FVector3f&)DeltaLocalSpaceLocation,
					Offset,
					Range);
			}
			else
#endif
			{
				PhysicsParallelFor(RangeSize,
					[this, &ParticleGroupIds, &DeltaLocalSpaceLocation, &Particles, Offset](int32 i)
					{
						const int32 Index = Offset + i;
						const Softs::FSolverRigidTransform3& GroupSpaceTransform = PreSimulationTransforms[ParticleGroupIds[Index]];

						// Update initial state for particles
						Particles.P(Index) = Particles.X(Index) = GroupSpaceTransform.TransformPositionNoScale(Particles.X(Index)) - DeltaLocalSpaceLocation;
						Particles.V(Index) = GroupSpaceTransform.TransformVector(Particles.V(Index));

						// Copy InvM over to PAndInvM
						Particles.PAndInvM(Index).InvM = Particles.InvM(Index);

						// Update anim initial state (target updated by skinning)
						OldAnimationPositions[Index] = GroupSpaceTransform.TransformPositionNoScale(OldAnimationPositions[Index]) - DeltaLocalSpaceLocation;
					}, RangeSize < ClothSolverMinParallelBatchSize);
			}
		}, /*bForceSingleThreaded =*/ !bClothSolverParallelClothPreUpdate);

#if FRAMEPRO_ENABLED
	FRAMEPRO_CUSTOM_STAT("ChaosClothSolverMinParallelBatchSize", ClothSolverMinParallelBatchSize, "ChaosClothSolver", "Particles", FRAMEPRO_COLOUR(128,0,255));
	FRAMEPRO_CUSTOM_STAT("ChaosClothSolverParallelClothPreUpdate", bClothSolverParallelClothPreUpdate, "ChaosClothSolver", "Enabled", FRAMEPRO_COLOUR(128, 128, 64));
#endif

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationSolver_CollisionPreSimulationTransforms);
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothCollisionPreSimulationTransforms);

		const TPBDActiveView<Softs::FSolverRigidParticles>& CollisionParticlesActiveView = Evolution->CollisionParticlesActiveView();
		const TArray<uint32>& CollisionParticleGroupIds = Evolution->CollisionParticleGroupIds();

		CollisionParticlesActiveView.SequentialFor(  // There's unlikely to ever have enough collision particles for a parallel for
			[this, &CollisionParticleGroupIds, &DeltaLocalSpaceLocation](Softs::FSolverRigidParticles& CollisionParticles, int32 Index)
			{
				const Softs::FSolverRigidTransform3& GroupSpaceTransform = PreSimulationTransforms[CollisionParticleGroupIds[Index]];

				// Update initial state for collisions
				OldCollisionTransforms[Index] = OldCollisionTransforms[Index] * GroupSpaceTransform;
				OldCollisionTransforms[Index].AddToTranslation(-DeltaLocalSpaceLocation);
				CollisionParticles.X(Index) = OldCollisionTransforms[Index].GetTranslation();
				CollisionParticles.R(Index) = OldCollisionTransforms[Index].GetRotation();
			});
	}
}

void FClothingSimulationSolver::UpdateSolverField()
{
	if (Evolution && !PerSolverField.IsEmpty())
	{
		TArray<FVector>& SamplePositions = PerSolverField.GetSamplePositions();
		TArray<FFieldContextIndex>& SampleIndices = PerSolverField.GetSampleIndices();

		const uint32 NumParticles = Evolution->Particles().Size();

		SamplePositions.SetNum(NumParticles, false);
		SampleIndices.SetNum(NumParticles, false);

		for (uint32 Index = 0; Index < NumParticles; ++Index)
		{
			SamplePositions[Index] = FVector(Evolution->Particles().X(Index)) + LocalSpaceLocation;
			SampleIndices[Index] = FFieldContextIndex(Index, Index);
		}
		PerSolverField.ComputeFieldLinearImpulse(GetTime());
	}
}

void FClothingSimulationSolver::Update(Softs::FSolverReal InDeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationSolver_Update);
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdate);

	if (!bClothSolverUseImprovedTimeStepSmoothing)
	{
		// Filter delta time to smoothen time variations and prevent unwanted vibrations
		// Note: This is now deprecated and replaced by in solver input force timestep smoothing
		constexpr Softs::FSolverReal DeltaTimeDecay = (Softs::FSolverReal)0.1;
		const Softs::FSolverReal PrevDeltaTime = DeltaTime;
		DeltaTime = DeltaTime + (InDeltaTime - DeltaTime) * DeltaTimeDecay;
	}
	else
	{
		// Update time step
		DeltaTime = InDeltaTime;
	}

#if !UE_BUILD_SHIPPING
	// Introduce artificial hitches for debugging any simulation jitter
	if (ClothSolverDebugHitchLength && ClothSolverDebugHitchInterval)
	{
		static int32 HitchCounter = 0;
		if (--HitchCounter < 0)
		{
			UE_LOG(LogChaosCloth, Warning, TEXT("Hitching for %dms"), ClothSolverDebugHitchLength);
			FPlatformProcess::Sleep((float)ClothSolverDebugHitchLength * 0.001f);
			HitchCounter = ClothSolverDebugHitchInterval;
		}
	}
#endif  // #if !UE_BUILD_SHIPPING

	// Update Cloths and cloth colliders
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationSolver_UpdateCloths);
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdateCloths);

		Swap(OldCollisionTransforms, CollisionTransforms);
		Swap(OldAnimationPositions, AnimationPositions);

		// Clear external collisions so that they can be re-added
		CollisionParticlesSize = 0;

		// Compute the solver field forces/velocities for future use in the AddExternalForces
		UpdateSolverField();

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
			Evolution->SetGravity(Gravity, GroupId);
			Evolution->GetVelocityField(GroupId).SetVelocity(WindVelocity);

			Cloth->Update(this);
		}, /*bForceSingleThreaded =*/ !bClothSolverParallelClothUpdate);
	}

	// Pre solver step, apply group space transforms for teleport and linear/delta ratios, ...etc
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationSolver_UpdatePreSolverStep);
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdatePreSolverStep);

		ApplyPreSimulationTransforms();
	}

	// Advance Sim
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationSolver_UpdateSolverStep);
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdateSolverStep);
		SCOPE_CYCLE_COUNTER(STAT_ClothInternalSolve);

		// Update solver time dependent parameters
		constexpr Softs::FSolverReal SolverFrequency = 60.f;  // TODO: This could become a solver property

		const int32 TimeDependentNumIterations = bClothSolverDisableTimeDependentNumIterations ?
			NumIterations :
			(int32)(SolverFrequency * DeltaTime * (Softs::FSolverReal)NumIterations);

		Evolution->SetIterations(FMath::Clamp(TimeDependentNumIterations, 1, MaxNumIterations));

		// Advance substeps
		const Softs::FSolverReal SubstepDeltaTime = DeltaTime / (Softs::FSolverReal)NumSubsteps;
	
		for (int32 i = 0; i < NumSubsteps; ++i)
		{
			Evolution->AdvanceOneTimeStep(SubstepDeltaTime, bClothSolverUseImprovedTimeStepSmoothing);
		}

		Time = Evolution->GetTime();
		UE_LOG(LogChaosCloth, VeryVerbose, TEXT("DeltaTime: %.6f, Time = %.6f"), DeltaTime, Time);
	}

	// Post solver step, update normals, ...etc
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationSolver_UpdatePostSolverStep);
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdatePostSolverStep);
		SCOPE_CYCLE_COUNTER(STAT_ClothComputeNormals);

		PhysicsParallelFor(Cloths.Num(), [this](int32 ClothIndex)
		{
			FClothingSimulationCloth* const Cloth = Cloths[ClothIndex];
			Cloth->PostUpdate(this);
		}, /*bForceSingleThreaded =*/ !bClothSolverParallelClothPostUpdate);
	}

	// Save old space location for next update
	OldLocalSpaceLocation = LocalSpaceLocation;
}

int32 FClothingSimulationSolver::GetNumUsedIterations() const
{
	return Evolution->GetIterations();
}

FBoxSphereBounds FClothingSimulationSolver::CalculateBounds() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationSolver_CalculateBounds);
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverCalculateBounds);

	const TPBDActiveView<Softs::FSolverParticles>& ParticlesActiveView = Evolution->ParticlesActiveView();

	if (ParticlesActiveView.HasActiveRange())
	{
		// Calculate bounding box
		Softs::FSolverAABB3 BoundingBox = Softs::FSolverAABB3::EmptyAABB();

#if INTEL_ISPC
		if (bRealTypeCompatibleWithISPC && bChaos_CalculateBounds_ISPC_Enabled)
		{
			ParticlesActiveView.RangeFor(
				[&BoundingBox](Softs::FSolverParticles& Particles, int32 Offset, int32 Range)
				{
					Softs::FSolverVec3 NewMin = BoundingBox.Min();
					Softs::FSolverVec3 NewMax = BoundingBox.Max();

					ispc::CalculateBounds(
						(ispc::FVector3f&)NewMin,
						(ispc::FVector3f&)NewMax,
						(const ispc::FVector3f*)Particles.XArray().GetData(),
						Offset,
						Range);

					BoundingBox.GrowToInclude(Softs::FSolverAABB3(NewMin, NewMax));
				});
		}
		else
#endif
		{
			ParticlesActiveView.SequentialFor(
				[&BoundingBox](Softs::FSolverParticles& Particles, int32 Index)
				{
					BoundingBox.GrowToInclude(Particles.X(Index));
				});
		}

		// Calculate (squared) radius
		const Softs::FSolverVec3 Center = BoundingBox.Center();
		Softs::FSolverReal SquaredRadius = (Softs::FSolverReal)0.;

#if INTEL_ISPC
		if (bRealTypeCompatibleWithISPC && bChaos_CalculateBounds_ISPC_Enabled)
		{
			ParticlesActiveView.RangeFor(
				[&SquaredRadius, &Center](Softs::FSolverParticles& Particles, int32 Offset, int32 Range)
				{
					ispc::CalculateSquaredRadius(
						SquaredRadius,
						(const ispc::FVector3f&)Center,
						(const ispc::FVector3f*)Particles.XArray().GetData(),
						Offset,
						Range);
				});
		}
		else
#endif
		{
			ParticlesActiveView.SequentialFor(
				[&SquaredRadius, &Center](Softs::FSolverParticles& Particles, int32 Index)
				{
					SquaredRadius = FMath::Max(SquaredRadius, (Particles.X(Index) - Center).SizeSquared());
				});
		}

		// Update bounds with this cloth
		return FBoxSphereBounds(LocalSpaceLocation + BoundingBox.Center(), FVector(BoundingBox.Extents() * 0.5f), FMath::Sqrt(SquaredRadius));
	}

	return FBoxSphereBounds(LocalSpaceLocation, FVector(0.f), 0.f);
}

} // End namespace Chaos
