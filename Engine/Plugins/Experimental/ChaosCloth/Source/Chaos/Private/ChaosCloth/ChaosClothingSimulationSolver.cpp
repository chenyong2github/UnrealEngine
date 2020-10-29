// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosCloth/ChaosClothingSimulationSolver.h"
#include "ChaosCloth/ChaosClothingSimulationCloth.h"
#include "ChaosCloth/ChaosClothingSimulationCollider.h"
#include "ChaosCloth/ChaosClothingSimulationMesh.h"
#include "ChaosCloth/ChaosClothingSimulation.h"
#include "ChaosCloth/ChaosClothPrivate.h"

#include "Chaos/PBDEvolution.h"

using namespace Chaos;

DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update"), STAT_ChaosClothSolverUpdate, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Cloths"), STAT_ChaosClothSolverUpdateCloths, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Pre Solver Step"), STAT_ChaosClothSolverUpdatePreSolverStep, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Solver Step"), STAT_ChaosClothSolverUpdateSolverStep, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Update Post Solver Step"), STAT_ChaosClothSolverUpdatePostSolverStep, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Solver Calculate Bounds"), STAT_ChaosClothSolverCalculateBounds, STATGROUP_ChaosCloth);

namespace ChaosClothingSimulationSolverDefault
{
	static const TVector<float, 3> Gravity(0.f, 0.f, -980.665f);  // cm/s^2
	static const TVector<float, 3> WindVelocity(0.f);
	static const float WindFluidDensity = 1.225e-6f;  // kg/cm^3
	static const int32 NumIterations = 1;
	static const int32 NumSubsteps = 1;
	static const float SelfCollisionThickness = 2.f;
	static const float CollisionThickness = 1.2f;
	static const float FrictionCoefficient = 0.2f;
	static const float DampingCoefficient = 0.01f;
}

namespace ChaosClothingSimulationSolverConstant
{
	static const float WorldScale = 100.f;  // World is in cm, but values like wind speed and density are in SI unit and relates to m.
	static const float StartDeltaTime = 1.f / 30.f;  // Initialize filtered timestep at 30fps
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
	TPBDParticles<float, 3> LocalParticles;
	TKinematicGeometryClothParticles<float, 3> TRigidParticles;
	Evolution.Reset(
		new TPBDEvolution<float, 3>(
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

	Evolution->Particles().AddArray(&Normals);
	Evolution->Particles().AddArray(&OldAnimationPositions);
	Evolution->Particles().AddArray(&AnimationPositions);
	Evolution->Particles().AddArray(&AnimationNormals);

	Evolution->CollisionParticles().AddArray(&CollisionBoneIndices);
	Evolution->CollisionParticles().AddArray(&CollisionBaseTransforms);
	Evolution->CollisionParticles().AddArray(&OldCollisionTransforms);
	Evolution->CollisionParticles().AddArray(&CollisionTransforms);

	Evolution->SetKinematicUpdateFunction(
		[this](TPBDParticles<float, 3>& ParticlesInput, const float Dt, const float LocalTime, const int32 Index)
		{
			if (!OldAnimationPositions.IsValidIndex(Index) || ParticlesInput.InvM(Index) > 0)
			{
				return;
			}
			const float Alpha = (LocalTime - Time) / DeltaTime;
			ParticlesInput.X(Index) = Alpha * AnimationPositions[Index] + (1.f - Alpha) * OldAnimationPositions[Index];
		});

	Evolution->SetCollisionKinematicUpdateFunction(
		[this](TKinematicGeometryClothParticles<float, 3>& ParticlesInput, const float Dt, const float LocalTime, const int32 Index)
		{
			checkSlow(Dt > SMALL_NUMBER && DeltaTime > SMALL_NUMBER);
			const float Alpha = (LocalTime - Time) / DeltaTime;
			const TVector<float, 3> NewX =
				Alpha * CollisionTransforms[Index].GetTranslation() + (1.f - Alpha) * OldCollisionTransforms[Index].GetTranslation();
			ParticlesInput.V(Index) = (NewX - ParticlesInput.X(Index)) / Dt;
			ParticlesInput.X(Index) = NewX;
			TRotation<float, 3> NewR = FQuat::Slerp(OldCollisionTransforms[Index].GetRotation(), CollisionTransforms[Index].GetRotation(), Alpha);
			TRotation<float, 3> Delta = NewR * ParticlesInput.R(Index).Inverse();
			TVector<float, 3> Axis;
			float Angle;
			Delta.ToAxisAndAngle(Axis, Angle);
			ParticlesInput.W(Index) = Axis * Angle / Dt;
			ParticlesInput.R(Index) = NewR;
		});
}

FClothingSimulationSolver::~FClothingSimulationSolver()
{
}

void FClothingSimulationSolver::SetLocalSpaceLocation(const TVector<float, 3>& InLocalSpaceLocation, bool bReset)
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
		->Initialize(Evolution.Get(), AnimationPositions, AnimationNormals, Offset, NumParticles);

	// Always starts with particles disabled
	EnableParticles(Offset, false);

	return Offset;
}

void FClothingSimulationSolver::EnableParticles(int32 Offset, bool bEnable)
{
	Evolution->ActivateParticleRange(Offset, bEnable);
	GetClothConstraints(Offset).Enable(bEnable);
}

const TVector<float, 3>* FClothingSimulationSolver::GetParticlePs(int32 Offset) const
{
	return &Evolution->Particles().P(Offset);
}

TVector<float, 3>* FClothingSimulationSolver::GetParticlePs(int32 Offset)
{
	return &Evolution->Particles().P(Offset);
}

const TVector<float, 3>* FClothingSimulationSolver::GetParticleXs(int32 Offset) const
{
	return &Evolution->Particles().X(Offset);
}

TVector<float, 3>* FClothingSimulationSolver::GetParticleXs(int32 Offset)
{
	return &Evolution->Particles().X(Offset);
}

const TVector<float, 3>* FClothingSimulationSolver::GetParticleVs(int32 Offset) const
{
	return &Evolution->Particles().V(Offset);
}

TVector<float, 3>* FClothingSimulationSolver::GetParticleVs(int32 Offset)
{
	return &Evolution->Particles().V(Offset);
}

const float* FClothingSimulationSolver::GetParticleInvMasses(int32 Offset) const
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
	TRotation<float, 3>* const Rs = GetCollisionParticleRs(Offset);
	TVector<float, 3>* const Xs = GetCollisionParticleXs(Offset);

	for (int32 Index = 0; Index < NumCollisionParticles; ++Index)
	{
		Xs[Index] = TVector<float, 3>(0.f);
		Rs[Index] = TRotation<float, 3>::FromIdentity();
	}

	// Always starts with particles disabled
	EnableCollisionParticles(Offset, false);

	return Offset;
}

void FClothingSimulationSolver::EnableCollisionParticles(int32 Offset, bool bEnable)
{
	Evolution->ActivateCollisionParticleRange(Offset, bEnable);
}

const TVector<float, 3>* FClothingSimulationSolver::GetCollisionParticleXs(int32 Offset) const
{
	return &Evolution->CollisionParticles().X(Offset);
}

TVector<float, 3>* FClothingSimulationSolver::GetCollisionParticleXs(int32 Offset)
{
	return &Evolution->CollisionParticles().X(Offset);
}

const TRotation<float, 3>* FClothingSimulationSolver::GetCollisionParticleRs(int32 Offset) const
{
	return &Evolution->CollisionParticles().R(Offset);
}

TRotation<float, 3>* FClothingSimulationSolver::GetCollisionParticleRs(int32 Offset)
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

void FClothingSimulationSolver::SetParticleMassUniform(int32 Offset, float UniformMass, float MinPerParticleMass, const TTriangleMesh<float>& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	// Retrieve the particle block size
	const int32 Size = Evolution->GetParticleRangeSize(Offset);

	// Set mass from uniform mass
	const TSet<int32> Vertices = Mesh.GetVertices();
	TPBDParticles<float, 3>& Particles = Evolution->Particles();
	for (int32 Index = Offset; Index < Offset + Size; ++Index)
	{
		Particles.M(Index) = Vertices.Contains(Index) ? UniformMass : 0.f;
	}

	// Clamp and enslave
	ParticleMassClampAndEnslave(Offset, Size, MinPerParticleMass, KinematicPredicate);
}

void FClothingSimulationSolver::SetParticleMassFromTotalMass(int32 Offset, float TotalMass, float MinPerParticleMass, const TTriangleMesh<float>& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	// Retrieve the particle block size
	const int32 Size = Evolution->GetParticleRangeSize(Offset);

	// Set mass per area
	const float TotalArea = SetParticleMassPerArea(Offset, Size, Mesh);

	// Find density
	const float Density = TotalArea > 0.f ? TotalMass / TotalArea : 1.f;

	// Update mass from mesh and density
	ParticleMassUpdateDensity(Mesh, Density);

	// Clamp and enslave
	ParticleMassClampAndEnslave(Offset, Size, MinPerParticleMass, KinematicPredicate);
}

void FClothingSimulationSolver::SetParticleMassFromDensity(int32 Offset, float Density, float MinPerParticleMass, const TTriangleMesh<float>& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	// Retrieve the particle block size
	const int32 Size = Evolution->GetParticleRangeSize(Offset);

	// Set mass per area
	const float TotalArea = SetParticleMassPerArea(Offset, Size, Mesh);

	// Set density from cm2 to m2
	Density /= FMath::Square(ChaosClothingSimulationSolverConstant::WorldScale);

	// Update mass from mesh and density
	ParticleMassUpdateDensity(Mesh, Density);

	// Clamp and enslave
	ParticleMassClampAndEnslave(Offset, Size, MinPerParticleMass, KinematicPredicate);
}

void FClothingSimulationSolver::SetReferenceVelocityScale(
	uint32 GroupId,
	const TRigidTransform<float, 3>& OldReferenceSpaceTransform,
	const TRigidTransform<float, 3>& ReferenceSpaceTransform,
	const TVector<float, 3>& LinearVelocityScale,
	float AngularVelocityScale)
{
	TRigidTransform<float, 3> OldRootBoneLocalTransform = OldReferenceSpaceTransform;
	OldRootBoneLocalTransform.AddToTranslation(-OldLocalSpaceLocation);

	// Calculate deltas
	const FTransform DeltaTransform = ReferenceSpaceTransform.GetRelativeTransform(OldReferenceSpaceTransform);

	// Apply linear velocity scale
	const TVector<float, 3> LinearRatio = TVector<float, 3>(1.f) - LinearVelocityScale.BoundToBox(TVector<float, 3>(0.f), TVector<float, 3>(1.f));
	const TVector<float, 3> DeltaPosition = LinearRatio * DeltaTransform.GetTranslation();

	// Apply angular velocity scale
	TRotation<float, 3> DeltaRotation = DeltaTransform.GetRotation();
	TVector<float, 3> Axis;
	float DeltaAngle;
	DeltaRotation.ToAxisAndAngle(Axis, DeltaAngle);
	if (DeltaAngle > PI)
	{
		DeltaAngle -= 2.f * PI;
	}

	const float AngularRatio = FMath::Clamp(1.f - AngularVelocityScale, 0.f, 1.f);
	DeltaRotation = FQuat(Axis, DeltaAngle * AngularRatio);

	// Transform points back into the previous frame of reference before applying the adjusted deltas 
	PreSimulationTransforms[GroupId] = OldRootBoneLocalTransform.Inverse() * FTransform(DeltaRotation, DeltaPosition) * OldRootBoneLocalTransform;
}

float FClothingSimulationSolver::SetParticleMassPerArea(int32 Offset, int32 Size, const TTriangleMesh<float>& Mesh)
{
	// Zero out masses
	TPBDParticles<float, 3>& Particles = Evolution->Particles();
	for (int32 Index = Offset; Index < Offset + Size; ++Index)
	{
		Particles.M(Index) = 0.f;
	}

	// Assign per particle mass proportional to connected area.
	const TArray<TVector<int32, 3>>& SurfaceElements = Mesh.GetSurfaceElements();
	float TotalArea = 0.f;
	for (const TVector<int32, 3>& Tri : SurfaceElements)
	{
		const float TriArea = 0.5f * TVector<float, 3>::CrossProduct(
			Particles.X(Tri[1]) - Particles.X(Tri[0]),
			Particles.X(Tri[2]) - Particles.X(Tri[0])).Size();
		TotalArea += TriArea;
		const float ThirdTriArea = TriArea / 3.f;
		Particles.M(Tri[0]) += ThirdTriArea;
		Particles.M(Tri[1]) += ThirdTriArea;
		Particles.M(Tri[2]) += ThirdTriArea;
	}

	UE_LOG(LogChaosCloth, Verbose, TEXT("Total area: %f, SI total area: %f"), TotalArea, TotalArea / FMath::Square(ChaosClothingSimulationSolverConstant::WorldScale));
	return TotalArea;
}

void FClothingSimulationSolver::ParticleMassUpdateDensity(const TTriangleMesh<float>& Mesh, float Density)
{
	const TSet<int32> Vertices = Mesh.GetVertices();
	TPBDParticles<float, 3>& Particles = Evolution->Particles();
	float TotalMass = 0.f;
	for (const int32 Vertex : Vertices)
	{
		Particles.M(Vertex) *= Density;
		TotalMass += Particles.M(Vertex);
	}

	UE_LOG(LogChaosCloth, Verbose, TEXT("Total mass: %f, "), TotalMass);
}

void FClothingSimulationSolver::ParticleMassClampAndEnslave(int32 Offset, int32 Size, float MinPerParticleMass, const TFunctionRef<bool(int32)>& KinematicPredicate)
{
	TPBDParticles<float, 3>& Particles = Evolution->Particles();
	for (int32 Index = Offset; Index < Offset + Size; ++Index)
	{
		Particles.M(Index) = FMath::Max(Particles.M(Index), MinPerParticleMass);
		Particles.InvM(Index) = KinematicPredicate(Index - Offset) ? 0.f : 1.f / Particles.M(Index);
	}
}

void FClothingSimulationSolver::SetProperties(uint32 GroupId, float DampingCoefficient, float CollisionThickness, float FrictionCoefficient)
{
	Evolution->SetDamping(DampingCoefficient, GroupId);
	Evolution->SetCollisionThickness(CollisionThickness, GroupId);
	Evolution->SetCoefficientOfFriction(FrictionCoefficient, GroupId);
}

void FClothingSimulationSolver::SetGravity(uint32 GroupId, const TVector<float, 3>& InGravity)
{
	Evolution->GetGravityForces(GroupId).SetAcceleration(InGravity);
}

void FClothingSimulationSolver::SetWindVelocity(const TVector<float, 3>& InWindVelocity, float InLegacyWindAdaption)
{
	WindVelocity = InWindVelocity * ChaosClothingSimulationSolverConstant::WorldScale;
	LegacyWindAdaption = InLegacyWindAdaption;
}

void FClothingSimulationSolver::SetWindVelocityField(uint32 GroupId, float DragCoefficient, float LiftCoefficient, const TTriangleMesh<float>* TriangleMesh)
{
	TVelocityField<float, 3>& VelocityField = Evolution->GetVelocityField(GroupId);
	VelocityField.SetGeometry(TriangleMesh);
	VelocityField.SetCoefficients(DragCoefficient, LiftCoefficient);
}

const TVelocityField<float, 3>& FClothingSimulationSolver::GetWindVelocityField(uint32 GroupId)
{
	return Evolution->GetVelocityField(GroupId);
}

void FClothingSimulationSolver::SetLegacyWind(uint32 GroupId, bool bUseLegacyWind)
{
	if (!bUseLegacyWind)
	{
		// Clear force function
		// NOTE: This assumes that the force function is only used for the legacy wind effect
		Evolution->GetForceFunction(GroupId) = TFunction<void(TPBDParticles<float, 3>&, const float, const int32)>();
	}
	else
	{
		// Add legacy wind function
		Evolution->GetForceFunction(GroupId) = 
			[this](TPBDParticles<float, 3>& Particles, const float /*Dt*/, const int32 Index)
			{
				if (Particles.InvM(Index) != 0.f)
				{
					// Calculate wind velocity delta
					static const float LegacyWindMultiplier = 25.f;
					const TVector<float, 3> VelocityDelta = WindVelocity * LegacyWindMultiplier - Particles.V(Index);

					TVector<float, 3> Direction = VelocityDelta;
					if (Direction.Normalize())
					{
						// Scale by angle
						const float DirectionDot = TVector<float, 3>::DotProduct(Direction, Normals[Index]);
						const float ScaleFactor = FMath::Min(1.f, FMath::Abs(DirectionDot) * LegacyWindAdaption);

						Particles.F(Index) += VelocityDelta * ScaleFactor * Particles.M(Index);
					}
				}
			};
	}
}

void FClothingSimulationSolver::SetSelfCollisions(uint32 GroupId, float SelfCollisionThickness, const TTriangleMesh<float>* TriangleMesh)
{
#ifdef CHAOS_CLOTH_MUST_IMPROVE_SELF_COLLISION  // TODO: Improve self-collision until it can run in engine tests without crashing the simulation.
	if (TriangleMesh)
	{
		// TODO(mlentine): Parallelize these for multiple meshes
		// TODO(Kriss.Gossart): Check/fix potential issue with CollisionTriangles/mesh indices
		Evolution->CollisionTriangles().Append(TriangleMesh->GetSurfaceElements());

		const TVector<int32, 2> VertexRange = TriangleMesh->GetVertexRange();
		for (int32 Index = VertexRange[0]; Index <= VertexRange[1]; ++Index)
		{
			const TSet<int32> Neighbors = TriangleMesh->GetNRing(Index, 5);
			for (int32 Element : Neighbors)
			{
				check(Index != Element);
				Evolution->DisabledCollisionElements().Add(TVector<int32, 2>(Index, Element));
				Evolution->DisabledCollisionElements().Add(TVector<int32, 2>(Element, Index));
			}
		}
	}
#endif  // #ifdef CHAOS_CLOTH_MUST_IMPROVE_SELF_COLLISION

	// TODO: Note that enabling self collision also enable collisions between all cloths running within the same solver.
	//       Therefore this function should be called nevertheless.
	Evolution->SetSelfCollisionThickness(SelfCollisionThickness, GroupId);
}

void FClothingSimulationSolver::ApplyPreSimulationTransforms()
{
	const TVector<float, 3> DeltaLocalSpaceLocation = LocalSpaceLocation - OldLocalSpaceLocation;

	const TPBDActiveView<TPBDParticles<float, 3>>& ParticlesActiveView = Evolution->ParticlesActiveView();
	const TArray<uint32>& ParticleGroupIds = Evolution->ParticleGroupIds();

	ParticlesActiveView.ParallelFor(
		[this, &ParticleGroupIds, &DeltaLocalSpaceLocation](TPBDParticles<float, 3>& Particles, int32 Index)
		{
			const TRigidTransform<float, 3>& GroupSpaceTransform = PreSimulationTransforms[ParticleGroupIds[Index]];

			// Update initial state for particles
			Particles.P(Index) = Particles.X(Index) = GroupSpaceTransform.TransformPosition(Particles.X(Index)) - DeltaLocalSpaceLocation;
			Particles.V(Index) = GroupSpaceTransform.TransformVector(Particles.V(Index));

			// Update anim initial state (target updated by skinning)
			OldAnimationPositions[Index] = GroupSpaceTransform.TransformPosition(OldAnimationPositions[Index]) - DeltaLocalSpaceLocation;
		}, /*MinParallelBatchSize =*/ 1000);  // TODO: Profile this value

	const TPBDActiveView<TKinematicGeometryClothParticles<float, 3>>& CollisionParticlesActiveView = Evolution->CollisionParticlesActiveView();
	const TArray<uint32>& CollisionParticleGroupIds = Evolution->CollisionParticleGroupIds();

	CollisionParticlesActiveView.SequentialFor(  // There's unlikely to ever have enough collision particles for a parallel for
		[this, &CollisionParticleGroupIds, &DeltaLocalSpaceLocation](TKinematicGeometryClothParticles<float, 3>& CollisionParticles, int32 Index)
		{
			const TRigidTransform<float, 3>& GroupSpaceTransform = PreSimulationTransforms[CollisionParticleGroupIds[Index]];

			// Update initial state for collisions
			OldCollisionTransforms[Index] = GroupSpaceTransform * OldCollisionTransforms[Index];
			OldCollisionTransforms[Index].AddToTranslation(-DeltaLocalSpaceLocation);
			CollisionParticles.X(Index) = OldCollisionTransforms[Index].GetTranslation();
			CollisionParticles.R(Index) = OldCollisionTransforms[Index].GetRotation();
		});
}

void FClothingSimulationSolver::Update(float InDeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdate);

	// Filter delta time to smoothen time variations and prevent unwanted vibrations
	static const float DeltaTimeDecay = 0.1f;
	const float PrevDeltaTime = DeltaTime;
	DeltaTime = DeltaTime + (InDeltaTime - DeltaTime) * DeltaTimeDecay;

	// Update Cloths and cloth colliders
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverUpdateCloths);

		Swap(OldCollisionTransforms, CollisionTransforms);
		Swap(OldAnimationPositions, AnimationPositions);

		// Clear external collisions so that they can be re-added
		CollisionParticlesSize = 0;

		for (FClothingSimulationCloth* Cloth : Cloths)
		{
			const uint32 GroupId = Cloth->GetGroupId();

			// Pre-update overridable solver properties first
			Evolution->GetGravityForces(GroupId).SetAcceleration(Gravity);
			Evolution->GetVelocityField(GroupId).SetVelocity(WindVelocity);
			Evolution->GetVelocityField(GroupId).SetFluidDensity(WindFluidDensity);

			Cloth->Update(this);
		}
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

		const float SubstepDeltaTime = DeltaTime / (float)NumSubsteps;
	
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

		for (FClothingSimulationCloth* Cloth : Cloths)
		{
			Cloth->PostUpdate(this);
		}
	}

	// Save old space location for next update
	OldLocalSpaceLocation = LocalSpaceLocation;
}

FBoxSphereBounds FClothingSimulationSolver::CalculateBounds() const
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothSolverCalculateBounds);

	const TPBDActiveView<TPBDParticles<float, 3>>& ParticlesActiveView = Evolution->ParticlesActiveView();

	if (ParticlesActiveView.HasActiveRange())
	{
		// Calculate bounding box
		TAABB<float, 3> BoundingBox = TAABB<float, 3>::EmptyAABB();

		ParticlesActiveView.SequentialFor(
			[&BoundingBox](TPBDParticles<float, 3>& Particles, int32 Index)
			{
				BoundingBox.GrowToInclude(Particles.X(Index));
			});

		// Calculate (squared) radius
		const TVector<float, 3> Center = BoundingBox.Center();
		float SquaredRadius = 0.f;

		ParticlesActiveView.SequentialFor(
			[&SquaredRadius, &Center](TPBDParticles<float, 3>& Particles, int32 Index)
			{
				SquaredRadius = FMath::Max(SquaredRadius, (Particles.X(Index) - Center).SizeSquared());
			});

		// Update bounds with this cloth
		return FBoxSphereBounds(LocalSpaceLocation + BoundingBox.Center(), BoundingBox.Extents() * 0.5f, FMath::Sqrt(SquaredRadius));
	}

	return FBoxSphereBounds(LocalSpaceLocation, FVector(0.f), 0.f);
}
