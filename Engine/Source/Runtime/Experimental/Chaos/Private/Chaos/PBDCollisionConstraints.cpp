// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraints.h"

#include "Chaos/Capsule.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/ContactModification.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/SolverCollisionContainer.h"
#include "Chaos/Defines.h"
#include "Chaos/Evolution/SolverBodyContainer.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/SpatialAccelerationCollection.h"
#include "Chaos/Levelset.h"
#include "Chaos/Pair.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/Sphere.h"
#include "Chaos/Transform.h"
#include "Chaos/CastingUtilities.h"
#include "ChaosLog.h"
#include "ChaosStats.h"
#include "Chaos/Evolution/SolverDatas.h"
#include "Containers/Queue.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Algo/Sort.h"
#include "Algo/StableSort.h"

// Private includes
#include "Collision/PBDCollisionSolver.h"

#if INTEL_ISPC
#include "PBDCollisionConstraints.ispc.generated.h"
#endif

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	extern FRealSingle Chaos_Collision_EdgePrunePlaneDistance;

	int32 CollisionParticlesBVHDepth = 4;
	FAutoConsoleVariableRef CVarCollisionParticlesBVHDepth(TEXT("p.CollisionParticlesBVHDepth"), CollisionParticlesBVHDepth, TEXT("The maximum depth for collision particles bvh"));

	int32 ConstraintBPBVHDepth = 2;
	FAutoConsoleVariableRef CVarConstraintBPBVHDepth(TEXT("p.ConstraintBPBVHDepth"), ConstraintBPBVHDepth, TEXT("The maximum depth for constraint bvh"));

	int32 BPTreeOfGrids = 1;
	FAutoConsoleVariableRef CVarBPTreeOfGrids(TEXT("p.BPTreeOfGrids"), BPTreeOfGrids, TEXT("Whether to use a seperate tree of grids for bp"));

	FRealSingle CollisionFrictionOverride = -1.0f;
	FAutoConsoleVariableRef CVarCollisionFrictionOverride(TEXT("p.CollisionFriction"), CollisionFrictionOverride, TEXT("Collision friction for all contacts if >= 0"));

	FRealSingle CollisionRestitutionOverride = -1.0f;
	FAutoConsoleVariableRef CVarCollisionRestitutionOverride(TEXT("p.CollisionRestitution"), CollisionRestitutionOverride, TEXT("Collision restitution for all contacts if >= 0"));
	
	FRealSingle CollisionAngularFrictionOverride = -1.0f;
	FAutoConsoleVariableRef CVarCollisionAngularFrictionOverride(TEXT("p.CollisionAngularFriction"), CollisionAngularFrictionOverride, TEXT("Collision angular friction for all contacts if >= 0"));

	CHAOS_API int32 EnableCollisions = 1;
	FAutoConsoleVariableRef CVarEnableCollisions(TEXT("p.EnableCollisions"), EnableCollisions, TEXT("Enable/Disable collisions on the Chaos solver."));
	
	FRealSingle DefaultCollisionFriction = 0;
	FAutoConsoleVariableRef CVarDefaultCollisionFriction(TEXT("p.DefaultCollisionFriction"), DefaultCollisionFriction, TEXT("Collision friction default value if no materials are found."));

	FRealSingle DefaultCollisionRestitution = 0;
	FAutoConsoleVariableRef CVarDefaultCollisionRestitution(TEXT("p.DefaultCollisionRestitution"), DefaultCollisionRestitution, TEXT("Collision restitution default value if no materials are found."));

	FRealSingle CollisionRestitutionThresholdOverride = -1.0f;
	FAutoConsoleVariableRef CVarDefaultCollisionRestitutionThreshold(TEXT("p.CollisionRestitutionThreshold"), CollisionRestitutionThresholdOverride, TEXT("Collision restitution threshold override if >= 0 (units of acceleration)"));

	int32 CollisionCanAlwaysDisableContacts = 0;
	FAutoConsoleVariableRef CVarCollisionCanAlwaysDisableContacts(TEXT("p.CollisionCanAlwaysDisableContacts"), CollisionCanAlwaysDisableContacts, TEXT("Collision culling will always be able to permanently disable contacts"));

	int32 CollisionCanNeverDisableContacts = 0;
	FAutoConsoleVariableRef CVarCollisionCanNeverDisableContacts(TEXT("p.CollisionCanNeverDisableContacts"), CollisionCanNeverDisableContacts, TEXT("Collision culling will never be able to permanently disable contacts"));

	bool CollisionsAllowParticleTracking = true;
	FAutoConsoleVariableRef CVarCollisionsAllowParticleTracking(TEXT("p.Chaos.Collision.AllowParticleTracking"), CollisionsAllowParticleTracking, TEXT("Allow particles to track their collisions constraints when their DoBufferCollisions flag is enable [def:true]"));

	DECLARE_CYCLE_STAT(TEXT("Collisions::Reset"), STAT_Collisions_Reset, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::UpdatePointConstraints"), STAT_Collisions_UpdatePointConstraints, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::BeginDetect"), STAT_Collisions_BeginDetect, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::EndDetect"), STAT_Collisions_EndDetect, STATGROUP_ChaosCollision);

	//
	// Collision Constraint Container
	//

	FPBDCollisionConstraints::FPBDCollisionConstraints(
		const FPBDRigidsSOAs& InParticles,
		TArrayCollectionArray<bool>& Collided,
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& InPhysicsMaterials,
		const TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>>& InPerParticlePhysicsMaterials,
		const THandleArray<FChaosPhysicsMaterial>* const InSimMaterials,
		const int32 InApplyPairIterations /*= 1*/,
		const int32 InApplyPushOutPairIterations /*= 1*/,
		const FReal InRestitutionThreshold /*= (FReal)0*/)
		: FPBDConstraintContainer(FConstraintContainerHandle::StaticType())
		, Particles(InParticles)
		, NumActivePointConstraints(0)
		, MCollided(Collided)
		, MPhysicsMaterials(InPhysicsMaterials)
		, MPerParticlePhysicsMaterials(InPerParticlePhysicsMaterials)
		, SimMaterials(InSimMaterials)
		, MApplyPairIterations(InApplyPairIterations)
		, MApplyPushOutPairIterations(InApplyPushOutPairIterations)
		, RestitutionThreshold(InRestitutionThreshold)	// @todo(chaos): expose as property
		, bEnableCollisions(true)
		, bEnableRestitution(true)
		, bHandlesEnabled(true)
		, bEnableEdgePruning(true)
		, bIsDeterministic(false)
		, bCanDisableContacts(true)
		, GravityDirection(FVec3(0,0,-1))
		, GravitySize(980)
		, MaxPushOutVelocity(0)
		, SolverType(EConstraintSolverType::QuasiPbd)
	{
	}

	FPBDCollisionConstraints::~FPBDCollisionConstraints()
	{
	}

	void FPBDCollisionConstraints::DisableHandles()
	{
		check(NumConstraints() == 0);
		bHandlesEnabled = false;
	}

	FPBDCollisionConstraints::FHandles FPBDCollisionConstraints::GetConstraintHandles() const
	{
		return ConstraintAllocator.GetConstraints();
	}

	FPBDCollisionConstraints::FConstHandles FPBDCollisionConstraints::GetConstConstraintHandles() const
	{
		return ConstraintAllocator.GetConstConstraints();
	}

	const FChaosPhysicsMaterial* GetPhysicsMaterial(const TGeometryParticleHandle<FReal, 3>* Particle, const FImplicitObject* Geom, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PhysicsMaterials, const TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>>& PerParticlePhysicsMaterials, const THandleArray<FChaosPhysicsMaterial>* const SimMaterials)
	{
		// Use the per-particle material if it exists
		const FChaosPhysicsMaterial* UniquePhysicsMaterial = Particle->AuxilaryValue(PerParticlePhysicsMaterials).Get();
		if (UniquePhysicsMaterial != nullptr)
		{
			return UniquePhysicsMaterial;
		}
		const FChaosPhysicsMaterial* PhysicsMaterial = Particle->AuxilaryValue(PhysicsMaterials).Get();
		if (PhysicsMaterial != nullptr)
		{
			return PhysicsMaterial;
		}

		// If no particle material, see if the shape has one
		// @todo(chaos): handle materials for meshes etc
		for (const TUniquePtr<FPerShapeData>& ShapeData : Particle->ShapesArray())
		{
			const FImplicitObject* OuterShapeGeom = ShapeData->GetGeometry().Get();
			const FImplicitObject* InnerShapeGeom = Utilities::ImplicitChildHelper(OuterShapeGeom);
			if (Geom == OuterShapeGeom || Geom == InnerShapeGeom)
			{
				if (ShapeData->GetMaterials().Num() > 0)
				{
					if(SimMaterials)
					{
						return SimMaterials->Get(ShapeData->GetMaterials()[0].InnerHandle);
					}
					else
					{
						UE_LOG(LogChaos, Warning, TEXT("Attempted to resolve a material for a constraint but we do not have a sim material container."));
					}
				}
				else
				{
					// This shape doesn't have a material assigned
					return nullptr;
				}
			}
		}

		// The geometry used for this particle does not belong to the particle.
		// This can happen in the case of fracture.
		return nullptr;
	}

	void FPBDCollisionConstraints::UpdateConstraintMaterialProperties(FPBDCollisionConstraint& Constraint)
	{
		const FChaosPhysicsMaterial* PhysicsMaterial0 = GetPhysicsMaterial(Constraint.Particle[0], Constraint.Implicit[0], MPhysicsMaterials, MPerParticlePhysicsMaterials, SimMaterials);
		const FChaosPhysicsMaterial* PhysicsMaterial1 = GetPhysicsMaterial(Constraint.Particle[1], Constraint.Implicit[1], MPhysicsMaterials, MPerParticlePhysicsMaterials, SimMaterials);

		FCollisionContact& Contact = Constraint.Manifold;
		if (PhysicsMaterial0 && PhysicsMaterial1)
		{
			const FChaosPhysicsMaterial::ECombineMode RestitutionCombineMode = FChaosPhysicsMaterial::ChooseCombineMode(PhysicsMaterial0->RestitutionCombineMode,PhysicsMaterial1->RestitutionCombineMode);
			Contact.Restitution = FChaosPhysicsMaterial::CombineHelper(PhysicsMaterial0->Restitution, PhysicsMaterial1->Restitution, RestitutionCombineMode);

			const FChaosPhysicsMaterial::ECombineMode FrictionCombineMode = FChaosPhysicsMaterial::ChooseCombineMode(PhysicsMaterial0->FrictionCombineMode,PhysicsMaterial1->FrictionCombineMode);
			Contact.Friction = FChaosPhysicsMaterial::CombineHelper(PhysicsMaterial0->Friction,PhysicsMaterial1->Friction, FrictionCombineMode);
			const FReal StaticFriction0 = FMath::Max(PhysicsMaterial0->Friction, PhysicsMaterial0->StaticFriction);
			const FReal StaticFriction1 = FMath::Max(PhysicsMaterial1->Friction, PhysicsMaterial1->StaticFriction);
			Contact.AngularFriction = FChaosPhysicsMaterial::CombineHelper(StaticFriction0, StaticFriction1, FrictionCombineMode);
		}
		else if (PhysicsMaterial0)
		{
			const FReal StaticFriction0 = FMath::Max(PhysicsMaterial0->Friction, PhysicsMaterial0->StaticFriction);
			Contact.Restitution = PhysicsMaterial0->Restitution;
			Contact.Friction = PhysicsMaterial0->Friction;
			Contact.AngularFriction = StaticFriction0;
		}
		else if (PhysicsMaterial1)
		{
			const FReal StaticFriction1 = FMath::Max(PhysicsMaterial1->Friction, PhysicsMaterial1->StaticFriction);
			Contact.Restitution = PhysicsMaterial1->Restitution;
			Contact.Friction = PhysicsMaterial1->Friction;
			Contact.AngularFriction = StaticFriction1;
		}
		else
		{
			Contact.Friction = DefaultCollisionFriction;
			Contact.AngularFriction = DefaultCollisionFriction;
			Contact.Restitution = DefaultCollisionRestitution;
		}

		Contact.RestitutionThreshold = (CollisionRestitutionThresholdOverride >= 0.0f) ? CollisionRestitutionThresholdOverride : RestitutionThreshold;

		if (!bEnableRestitution)
		{
			Contact.Restitution = 0.0f;
		}

		// Overrides for testing
		if (CollisionFrictionOverride >= 0)
		{
			Contact.Friction = CollisionFrictionOverride;
			Contact.AngularFriction = CollisionFrictionOverride;
		}
		if (CollisionRestitutionOverride >= 0)
		{
			Contact.Restitution = CollisionRestitutionOverride;
		}
		if (CollisionAngularFrictionOverride >= 0)
		{
			Contact.AngularFriction = CollisionAngularFrictionOverride;
		}
	}

	void FPBDCollisionConstraints::UpdatePositionBasedState(const FReal Dt)
	{
	}

	void FPBDCollisionConstraints::BeginFrame()
	{
		ConstraintAllocator.BeginFrame();
	}

	void FPBDCollisionConstraints::Reset()
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Reset);

		ConstraintAllocator.Reset();
	}

	void FPBDCollisionConstraints::BeginDetectCollisions()
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_BeginDetect);

		ConstraintAllocator.BeginDetectCollisions();
	}

	void FPBDCollisionConstraints::EndDetectCollisions()
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_EndDetect);

		// Prune the unused contacts
		ConstraintAllocator.EndDetectCollisions();

		// Disable any edge collisions that are hidden by face collisions
		PruneEdgeCollisions();

		if (bIsDeterministic)
		{
			ConstraintAllocator.SortConstraintsHandles();
		}

		// Bind the constraints to this container and initialize other properties
		// @todo(chaos): this could be set on creation if the midphase knew about the container
		for (FPBDCollisionConstraint* Contact : GetConstraints())
		{
			if (Contact->GetContainer() == nullptr)
			{
				Contact->SetContainer(this);
				UpdateConstraintMaterialProperties(*Contact);
			}
		}
	}

	void FPBDCollisionConstraints::ApplyCollisionModifier(const TArray<ISimCallbackObject*>& CollisionModifiers, FReal Dt)
	{
		if (GetConstraints().Num() > 0)
		{
			TArrayView<FPBDCollisionConstraint* const> ConstraintHandles = GetConstraintHandles();
			FCollisionContactModifier Modifier(ConstraintHandles, Dt);

			for(ISimCallbackObject* ModifierCallback : CollisionModifiers)
			{
				ModifierCallback->ContactModification_Internal(Modifier);
			}

			Modifier.UpdateConstraintManifolds();
		}
	}

	void FPBDCollisionConstraints::DisconnectConstraints(const TSet<FGeometryParticleHandle*>& ParticleHandles)
	{
		RemoveConstraints(ParticleHandles);
	}

	void FPBDCollisionConstraints::RemoveConstraints(const TSet<FGeometryParticleHandle*>& ParticleHandles)
	{
		for (FGeometryParticleHandle* ParticleHandle : ParticleHandles)
		{
			ConstraintAllocator.RemoveParticle(ParticleHandle);
		}
	}

	Collisions::FContactParticleParameters FPBDCollisionConstraints::GetContactParticleParameters(const FReal Dt)
	{
		return { 
			(CollisionRestitutionThresholdOverride >= 0.0f) ? CollisionRestitutionThresholdOverride * Dt : RestitutionThreshold * Dt,
			CollisionCanAlwaysDisableContacts ? true : (CollisionCanNeverDisableContacts ? false : bCanDisableContacts),
			&MCollided,

		};
	}

	Collisions::FContactIterationParameters FPBDCollisionConstraints::GetContactIterationParameters(const FReal Dt, const int32 Iteration, const int32 NumIterations, const int32 NumPairIterations, bool& bNeedsAnotherIteration)
	{
		return {
			Dt, 
			Iteration, 
			NumIterations, 
			NumPairIterations, 
			SolverType, 
			&bNeedsAnotherIteration
		};
	}

	void FPBDCollisionConstraints::SetNumIslandConstraints(const int32 NumIslandConstraints, FPBDIslandSolverData& SolverData)
	{
		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			FPBDCollisionSolverContainer& SolverContainer = GetConstraintSolverContainer(SolverData);
			SolverContainer.SetNum(NumIslandConstraints);

			// @todo(chaos): we shouldn't have to pass settings down to the solvers every frame. Ideally the solver containers would be created by the constraint containers and
			// the settings would be passed on then. Currently the solver containers are created here FPBDIslandSolverData::AddConstraintDatas
			SolverContainer.SetMaxPushOutVelocity(MaxPushOutVelocity);
		}
		else
		{
			SolverData.GetConstraintHandles(ContainerId).Reset(NumIslandConstraints);
		}
	}

	FPBDCollisionSolverContainer& FPBDCollisionConstraints::GetConstraintSolverContainer(FPBDIslandSolverData& SolverData)
	{
		check(SolverType == EConstraintSolverType::QuasiPbd);
		return SolverData.GetConstraintContainer<FPBDCollisionSolverContainer>(ContainerId);
	}

	void FPBDCollisionConstraints::GatherInput(const FReal Dt, FPBDCollisionConstraint& Constraint, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData)
	{
		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			// We shouldn't be adding disabled constraints to the solver list. The check needs to be at caller site or we should return success/fail - see TPBDConstraintColorRule::GatherSolverInput
			check(Constraint.IsEnabled());

			FPBDCollisionSolverContainer& SolverContainer = GetConstraintSolverContainer(SolverData);
			SolverContainer.AddConstraintSolver(Dt, Constraint, Particle0Level, Particle1Level, SolverData.GetBodyContainer(), SolverData.GetConstraintIndex(ContainerId));
		}
		else
		{
			LegacyGatherInput(Dt, Constraint, Particle0Level, Particle1Level, SolverData);
		}
	}

	void FPBDCollisionConstraints::GatherInput(const FReal Dt, FPBDIslandSolverData& SolverData)
	{
		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			for (FPBDCollisionConstraint* Constraint : GetConstraints())
			{
				if (Constraint->IsEnabled())
				{
					GatherInput(Dt, *Constraint, INDEX_NONE, INDEX_NONE, SolverData);
				}
			}
		}
		else
		{
			for (FPBDCollisionConstraint* Constraint : GetConstraints())
			{
				if (Constraint->IsEnabled())
				{
					LegacyGatherInput(Dt, *Constraint, INDEX_NONE, INDEX_NONE, SolverData);
				}
			}
		}
	}

	void FPBDCollisionConstraints::ScatterOutput(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData)
	{
		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			GetConstraintSolverContainer(SolverData).ScatterOutput(Dt, BeginIndex, EndIndex);
		}
		else
		{
			LegacyScatterOutput(Dt, BeginIndex, EndIndex, SolverData);
		}
	}

	void FPBDCollisionConstraints::ScatterOutput(const FReal Dt, FPBDIslandSolverData& SolverData)
	{
		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			FPBDCollisionSolverContainer& SolverContainer = GetConstraintSolverContainer(SolverData);
			SolverContainer.ScatterOutput(Dt, 0, SolverContainer.NumSolvers());
		}
		else
		{
			LegacyScatterOutput(Dt, 0, SolverData.GetConstraintHandles(ContainerId).Num(), SolverData);
		}
	}

	// Simple Rule version
	bool FPBDCollisionConstraints::ApplyPhase1(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);

		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			FPBDCollisionSolverContainer& SolverContainer = GetConstraintSolverContainer(SolverData);
			return SolverContainer.SolvePositionSerial(Dt, It, NumIts, 0, SolverContainer.NumSolvers());
		}
		else
		{
			return LegacyApplyPhase1Serial(Dt, It, NumIts, 0, SolverData.GetConstraintHandles(ContainerId).Num(), SolverData);
		}
	}

	// Island Rule version
	bool FPBDCollisionConstraints::ApplyPhase1Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);

		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			FPBDCollisionSolverContainer& SolverContainer = GetConstraintSolverContainer(SolverData);
			return SolverContainer.SolvePositionSerial(Dt, It, NumIts, 0, SolverContainer.NumSolvers());
		}
		else
		{
			return LegacyApplyPhase1Serial(Dt, It, NumIts, 0, SolverData.GetConstraintHandles(ContainerId).Num(), SolverData);
		}
	}

	// Color Rule version
	bool FPBDCollisionConstraints::ApplyPhase1Serial(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);

		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			FPBDCollisionSolverContainer& SolverContainer = GetConstraintSolverContainer(SolverData);
			return SolverContainer.SolvePositionSerial(Dt, It, NumIts, BeginIndex, EndIndex);
		}
		else
		{
			return LegacyApplyPhase1Serial(Dt, It, NumIts, BeginIndex, EndIndex, SolverData);
		}
	}

	// Color Rule version
	bool FPBDCollisionConstraints::ApplyPhase1Parallel(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);

		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			FPBDCollisionSolverContainer& SolverContainer = GetConstraintSolverContainer(SolverData);
			return SolverContainer.SolvePositionParallel(Dt, It, NumIts, BeginIndex, EndIndex);
		}
		else
		{
			return LegacyApplyPhase1Parallel(Dt, It, NumIts, BeginIndex, EndIndex, SolverData);
		}
	}

	// Simple Rule version
	bool FPBDCollisionConstraints::ApplyPhase2(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);

		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			FPBDCollisionSolverContainer& SolverContainer = GetConstraintSolverContainer(SolverData);
			return SolverContainer.SolveVelocitySerial(Dt, It, NumIts, 0, SolverContainer.NumSolvers());
		}
		else
		{
			return LegacyApplyPhase2Serial(Dt, It, NumIts, 0, SolverData.GetConstraintHandles(ContainerId).Num(), SolverData);
		}
	}

	// Island Rule version
	bool FPBDCollisionConstraints::ApplyPhase2Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);

		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			FPBDCollisionSolverContainer& SolverContainer = GetConstraintSolverContainer(SolverData);
			return SolverContainer.SolveVelocitySerial(Dt, It, NumIts, 0, SolverContainer.NumSolvers());
		}
		else
		{
			return LegacyApplyPhase2Serial(Dt, It, NumIts, 0, SolverData.GetConstraintHandles(ContainerId).Num(), SolverData);
		}
	}

	// Color Rule version
	bool FPBDCollisionConstraints::ApplyPhase2Serial(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);

		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			FPBDCollisionSolverContainer& SolverContainer = GetConstraintSolverContainer(SolverData);
			return SolverContainer.SolveVelocitySerial(Dt, It, NumIts, BeginIndex, EndIndex);
		}
		else
		{
			return LegacyApplyPhase2Serial(Dt, It, NumIts, BeginIndex, EndIndex, SolverData);
		}
	}

	// Color Rule version
	bool FPBDCollisionConstraints::ApplyPhase2Parallel(const FReal Dt,  const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);

		if (SolverType == EConstraintSolverType::QuasiPbd)
		{
			FPBDCollisionSolverContainer& SolverContainer = GetConstraintSolverContainer(SolverData);
			return SolverContainer.SolveVelocityParallel(Dt, It, NumIts, BeginIndex, EndIndex);
		}
		else
		{
			return LegacyApplyPhase2Parallel(Dt, It, NumIts, BeginIndex, EndIndex, SolverData);
		}
	}

	void FPBDCollisionConstraints::LegacyGatherInput(const FReal Dt, FPBDCollisionConstraint& Constraint, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData)
	{
		SolverData.GetConstraintHandles(ContainerId).Add(&Constraint);

		FSolverBody* SolverBody0 = SolverData.GetBodyContainer().FindOrAdd(Constraint.Particle[0]);
		FSolverBody* SolverBody1 = SolverData.GetBodyContainer().FindOrAdd(Constraint.Particle[1]);

		SolverBody0->SetLevel(Particle0Level);
		SolverBody1->SetLevel(Particle1Level);

		Constraint.SetSolverBodies(SolverBody0, SolverBody1);

		Constraint.AccumulatedImpulse = FVec3(0);
	}

	void FPBDCollisionConstraints::LegacyScatterOutput(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData)
	{
		for (int32 Index = BeginIndex; Index < EndIndex; ++Index)
		{
			FPBDCollisionConstraint* Constraint = SolverData.GetConstraintHandle<FPBDCollisionConstraint>(ContainerId,Index);
			Constraint->SetSolverBodies(nullptr, nullptr);
		}
	}

	bool FPBDCollisionConstraints::LegacyApplyPhase1Serial(const FReal Dt, const int32 Iterations, const int32 NumIterations, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData)
	{
		bool bNeedsAnotherIteration = false;
		if (MApplyPairIterations > 0)
		{
			NumActivePointConstraints = 0;
			const Collisions::FContactParticleParameters ParticleParameters = GetContactParticleParameters(Dt);
			const Collisions::FContactIterationParameters IterationParameters = GetContactIterationParameters(Dt, Iterations, NumIterations, MApplyPairIterations, bNeedsAnotherIteration);

			for (int32 Index = BeginIndex; Index < EndIndex; ++Index)
			{
				FPBDCollisionConstraint* Constraint = SolverData.GetConstraintHandle<FPBDCollisionConstraint>(ContainerId,Index);
				if (!Constraint->GetDisabled())
				{
					Collisions::Apply(*Constraint, IterationParameters, ParticleParameters);
					++NumActivePointConstraints;
				}
			}
		}
		return bNeedsAnotherIteration;
	}

	bool FPBDCollisionConstraints::LegacyApplyPhase1Parallel(const FReal Dt, const int32 Iterations, const int32 NumIterations, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData)
	{
		return LegacyApplyPhase1Serial(Dt, Iterations, NumIterations, BeginIndex, EndIndex, SolverData);
	}

	bool FPBDCollisionConstraints::LegacyApplyPhase2Serial(const FReal Dt, const int32 Iterations, const int32 NumIterations, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData)
	{
		bool bNeedsAnotherIteration = false;
		if (MApplyPairIterations > 0)
		{
			const Collisions::FContactParticleParameters ParticleParameters = GetContactParticleParameters(Dt);
			const Collisions::FContactIterationParameters IterationParameters = GetContactIterationParameters(Dt, Iterations, NumIterations, MApplyPushOutPairIterations, bNeedsAnotherIteration);

			for (int32 Index = BeginIndex; Index < EndIndex; ++Index)
			{
				FPBDCollisionConstraint* Constraint = SolverData.GetConstraintHandle<FPBDCollisionConstraint>(ContainerId,Index);
				if (!Constraint->GetDisabled())
				{
					Collisions::ApplyPushOut(*Constraint, IterationParameters, ParticleParameters);
				}
			}
		}
		return bNeedsAnotherIteration;
	}

	bool FPBDCollisionConstraints::LegacyApplyPhase2Parallel(const FReal Dt, const int32 Iterations, const int32 NumIterations, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData)
	{
		return LegacyApplyPhase2Serial(Dt,  Iterations, NumIterations, BeginIndex, EndIndex, SolverData);
	}

	const FPBDCollisionConstraint& FPBDCollisionConstraints::GetConstraint(int32 Index) const
	{
		check(Index < NumConstraints());
		
		return *GetConstraints()[Index];
	}

	FPBDCollisionConstraint& FPBDCollisionConstraints::GetConstraint(int32 Index)
	{
		check(Index < NumConstraints());

		return *GetConstraints()[Index];
	}

	void FPBDCollisionConstraints::PruneEdgeCollisions()
	{
		if (bEnableEdgePruning)
		{
			for (auto& ParticleHandle : Particles.GetNonDisabledDynamicView())
			{
				if ((ParticleHandle.CollisionConstraintFlags() & (uint32)ECollisionConstraintFlags::CCF_SmoothEdgeCollisions) != 0)
				{
					PruneParticleEdgeCollisions(ParticleHandle.Handle());
				}
			}
		}
	}

	void FPBDCollisionConstraints::PruneParticleEdgeCollisions(FGeometryParticleHandle* Particle)
	{
		FParticleCollisions& ParticleCollision = Particle->ParticleCollisions();

		const FReal EdgePlaneTolerance = Chaos_Collision_EdgePrunePlaneDistance;

		// Loop over edge collisions, then all plane collisions and remove the edge collision if it is hidden by a plane collision
		// NOTE: We only look at plane collisions where the other shape owns the plane.
		// @todo(chaos): this should probably only disable individual manifold points
		// @todo(chaos): we should probably only reject edges if the plane contact is also close to the edge contact
		// @todo(chaos): we should also try to eliminate face contacts from sub-surface faces
		// @todo(chaos): perf issue: this processes contacts in world space, but we don't calculated that data until Gather. Fix this.
		ParticleCollision.VisitCollisions(
			[Particle, &ParticleCollision, EdgePlaneTolerance](FPBDCollisionConstraint& EdgeCollision)
			{
				if (EdgeCollision.IsEnabled())
				{
					const int32 EdgeOtherShapeIndex = (EdgeCollision.GetParticle0() == Particle) ? 1 : 0;
					const EContactPointType VertexContactType = (EdgeOtherShapeIndex == 0) ? EContactPointType::VertexPlane : EContactPointType::PlaneVertex;

					for (const FManifoldPoint& EdgeManifoldPoint : EdgeCollision.GetManifoldPoints())
					{
						const bool bIsEdgeContact = (EdgeManifoldPoint.ContactPoint.ContactType == EContactPointType::EdgeEdge);

						if (bIsEdgeContact)
						{
							const FRigidTransform3& EdgeTransform = (EdgeOtherShapeIndex == 0) ? EdgeCollision.GetShapeWorldTransform0() : EdgeCollision.GetShapeWorldTransform1();
							const FVec3 EdgePos = EdgeTransform.TransformPositionNoScale(EdgeManifoldPoint.ContactPoint.ShapeContactPoints[EdgeOtherShapeIndex]);

							// Loop over plane collisions
							ECollisionVisitorResult PlaneResult = ParticleCollision.VisitConstCollisions(
								[Particle, &EdgeCollision, &EdgePos, EdgePlaneTolerance](const FPBDCollisionConstraint& PlaneCollision)
								{
									if ((&PlaneCollision != &EdgeCollision) && PlaneCollision.IsEnabled())
									{
										const int32 PlaneOtherShapeIndex = (PlaneCollision.GetParticle0() == Particle) ? 1 : 0;
										const EContactPointType PlaneContactType = (PlaneOtherShapeIndex == 0) ? EContactPointType::PlaneVertex : EContactPointType::VertexPlane;
										const FRigidTransform3& PlaneTransform = (PlaneOtherShapeIndex == 0) ? PlaneCollision.GetShapeWorldTransform0() : PlaneCollision.GetShapeWorldTransform1();

										for (const FManifoldPoint& PlaneManifoldPoint : PlaneCollision.GetManifoldPoints())
										{
											if (PlaneManifoldPoint.ContactPoint.ContactType == PlaneContactType)
											{
												// If the edge position is in the plane, disable it
												const FVec3 PlanePos = PlaneTransform.TransformPositionNoScale(PlaneManifoldPoint.ContactPoint.ShapeContactPoints[PlaneOtherShapeIndex]);
												const FVec3 PlaneNormal = PlaneCollision.GetShapeWorldTransform1().TransformVectorNoScale(PlaneManifoldPoint.ContactPoint.ShapeContactNormal);

												const FVec3 EdgePlaneDelta = EdgePos - PlanePos;
												const FReal EdgePlaneDistance = FVec3::DotProduct(EdgePlaneDelta, PlaneNormal);
												if (FMath::Abs(EdgePlaneDistance) < EdgePlaneTolerance)
												{
													// The edge contact is hidden by a plane contact so disable it and stop the inner loop
													EdgeCollision.SetDisabled(true);
													return ECollisionVisitorResult::Stop;
												}
											}
										}
									}
									return ECollisionVisitorResult::Continue;
								});

							// If we disabled this constraint, move to the next one and ignore remaining manifold points
							if (PlaneResult == ECollisionVisitorResult::Stop)
							{
								break;
							}
						}
					}
				}
				return ECollisionVisitorResult::Continue;
			});
	}

}
