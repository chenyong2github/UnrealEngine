// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/ParticlePairMidPhase.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/CollisionFilter.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDCollisionConstraints.h"

#include "ChaosStats.h"

extern bool Chaos_Collision_NarrowPhase_AABBBoundsCheck;

namespace Chaos
{
	namespace CVars
	{
		bool bChaos_Collision_EnableManifoldRestore = true;
		Chaos::FRealSingle Chaos_Collision_RestoreTolerance_NoContact_Position = 0.005f;	// About 0.5cm for a meter cube
		Chaos::FRealSingle Chaos_Collision_RestoreTolerance_NoContact_Rotation = 0.1f;		// About 10deg
		Chaos::FRealSingle Chaos_Collision_RestoreTolerance_Contact_Position = 0.02f;		// About 2cm for a meter cube
		Chaos::FRealSingle Chaos_Collision_RestoreTolerance_Contact_Rotation = 0.1f;		// About 10deg
		FAutoConsoleVariableRef CVarChaos_Collision_EnableManifoldRestore(TEXT("p.Chaos.Collision.EnableManifoldRestore"), bChaos_Collision_EnableManifoldRestore, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Collision_RestoreTolerance_NoContact_Position(TEXT("p.Chaos.Collision.RestoreTolerance.NoContact.Position"), Chaos_Collision_RestoreTolerance_NoContact_Position, TEXT("Fraction of Size. Particle pairs that move less than this may have their contacts reinstated"));
		FAutoConsoleVariableRef CVarChaos_Collision_RestoreTolerance_NoContact_Rotation(TEXT("p.Chaos.Collision.RestoreTolerance.NoContact.Rotation"), Chaos_Collision_RestoreTolerance_NoContact_Rotation, TEXT("Quaternion Dot Product Limit. Particle pairs that move less than this may have their contacts reinstated"));
		FAutoConsoleVariableRef CVarChaos_Collision_RestoreTolerance_Contact_Position(TEXT("p.Chaos.Collision.RestoreTolerance.WithContact.Position"), Chaos_Collision_RestoreTolerance_Contact_Position, TEXT("Fraction of Size. Particle pairs that move less than this may have their contacts reinstated"));
		FAutoConsoleVariableRef CVarChaos_Collision_RestoreTolerance_Contact_Rotation(TEXT("p.Chaos.Collision.RestoreTolerance.WithContact.Rotation"), Chaos_Collision_RestoreTolerance_Contact_Rotation, TEXT("Quaternion Dot Product Limit. Particle pairs that move less than this may have their contacts reinstated"));

		bool bChaos_Collision_EnableManifoldUpdate = true;
		FAutoConsoleVariableRef CVarChaos_Collision_EnableManifoldUpdate(TEXT("p.Chaos.Collision.EnableManifoldUpdate"), bChaos_Collision_EnableManifoldUpdate, TEXT(""));

	}
	using namespace CVars;

	inline bool ImplicitOverlapOBBToAABB(
		const FImplicitObject* Implicit0, 
		const FImplicitObject* Implicit1, 
		const FRigidTransform3& ShapeWorldTransform0, 
		const FRigidTransform3& ShapeWorldTransform1, 
		const FReal CullDistance)
	{
		if (Implicit0->HasBoundingBox() && Implicit1->HasBoundingBox())
		{
			const FRigidTransform3 Box1ToBox0TM = ShapeWorldTransform1.GetRelativeTransform(ShapeWorldTransform0);
			const FAABB3 Box1In0 = Implicit1->CalculateTransformedBounds(Box1ToBox0TM).Thicken(CullDistance);
			const FAABB3 Box0 = Implicit0->BoundingBox();
			return Box0.Intersects(Box1In0);
		}
		return true;
	}

	TUniquePtr<FPBDCollisionConstraint> CreateShapePairConstraint(
		FGeometryParticleHandle* Particle0,
		const FPerShapeData* InShape0,
		FGeometryParticleHandle* Particle1,
		const FPerShapeData* InShape1,
		const FReal CullDistance,
		const EContactShapesType ShapePairType)
	{
		const FImplicitObject* Implicit0 = InShape0->GetLeafGeometry();
		const FBVHParticles* BVHParticles0 = FConstGenericParticleHandle(Particle0)->CollisionParticles().Get();
		const FRigidTransform3& ShapeRelativeTransform0 = InShape0->GetLeafRelativeTransform();
		const FImplicitObject* Implicit1 = InShape1->GetLeafGeometry();
		const FBVHParticles* BVHParticles1 = FConstGenericParticleHandle(Particle1)->CollisionParticles().Get();
		const FRigidTransform3& ShapeRelativeTransform1 = InShape1->GetLeafRelativeTransform();
		const bool bUseManifolds = true;

		return FPBDCollisionConstraint::Make(Particle0, Implicit0, BVHParticles0, ShapeRelativeTransform0, Particle1, Implicit1, BVHParticles1, ShapeRelativeTransform1, CullDistance, bUseManifolds, ShapePairType);
	}

	TUniquePtr<FPBDCollisionConstraint> CreateImplicitPairConstraint(
		FGeometryParticleHandle* Particle0,
		const FImplicitObject* Implicit0,
		const FBVHParticles* BVHParticles0,
		const FRigidTransform3& ShapeRelativeTransform0,
		FGeometryParticleHandle* Particle1,
		const FImplicitObject* Implicit1,
		const FBVHParticles* BVHParticles1,
		const FRigidTransform3& ShapeRelativeTransform1,
		const FReal CullDistance,
		const EContactShapesType ShapePairType,
		const bool bInUseManifold)
	{
		const bool bUseManifolds = true;
		return FPBDCollisionConstraint::Make(Particle0, Implicit0, BVHParticles0, ShapeRelativeTransform0, Particle1, Implicit1, BVHParticles1, ShapeRelativeTransform1, CullDistance, bUseManifolds, ShapePairType);
	}


	////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////


	FSingleShapePairCollisionDetector::FSingleShapePairCollisionDetector(
		FGeometryParticleHandle* InParticle0,
		const FPerShapeData* InShape0,
		FGeometryParticleHandle* InParticle1,
		const FPerShapeData* InShape1,
		const EContactShapesType InShapePairType, 
		FParticlePairMidPhase& InMidPhase)
		: MidPhase(InMidPhase)
		, Constraint(nullptr)
		, Particle0(InParticle0)
		, Particle1(InParticle1)
		, Shape0(InShape0)
		, Shape1(InShape1)
		, ShapePairType(InShapePairType)
		, bEnableOBBCheck0(false)
		, bEnableOBBCheck1(false)
		, bEnableManifoldCheck(false)
	{
		const EImplicitObjectType ImplicitType0 = (Shape0->GetLeafGeometry() != nullptr) ? GetInnerType(Shape0->GetLeafGeometry()->GetCollisionType()) : ImplicitObjectType::Unknown;
		const EImplicitObjectType ImplicitType1 = (Shape1->GetLeafGeometry() != nullptr) ? GetInnerType(Shape1->GetLeafGeometry()->GetCollisionType()) : ImplicitObjectType::Unknown;
		const bool bIsSphere0 = (ImplicitType0 == ImplicitObjectType::Sphere);
		const bool bIsSphere1 = (ImplicitType1 == ImplicitObjectType::Sphere);

		bEnableOBBCheck0 = Chaos_Collision_NarrowPhase_AABBBoundsCheck && !bIsSphere0;
		bEnableOBBCheck1 = Chaos_Collision_NarrowPhase_AABBBoundsCheck && !bIsSphere1;
		bEnableManifoldCheck = bChaos_Collision_EnableManifoldUpdate && !bIsSphere0 && !bIsSphere1;
	}

	FSingleShapePairCollisionDetector::~FSingleShapePairCollisionDetector()
	{
	}

	FSingleShapePairCollisionDetector::FSingleShapePairCollisionDetector(FSingleShapePairCollisionDetector&& R)
		: MidPhase(R.MidPhase)
		, Constraint(MoveTemp(R.Constraint))
		, Particle0(R.Particle0)
		, Particle1(R.Particle1)
		, Shape0(R.Shape0)
		, Shape1(R.Shape1)
		, ShapePairType(R.ShapePairType)
		, bEnableOBBCheck0(R.bEnableOBBCheck0)
		, bEnableOBBCheck1(R.bEnableOBBCheck1)
		, bEnableManifoldCheck(R.bEnableManifoldCheck)
	{
	}

	bool FSingleShapePairCollisionDetector::IsUsedSince(const int32 Epoch) const
	{
		// If we have no constraint it was never used, so this check is always false
		return (Constraint != nullptr) && (Constraint->GetContainerCookie().LastUsedEpoch >= Epoch);
	}

	int32 FSingleShapePairCollisionDetector::GenerateCollision(
		const FReal CullDistance, 
		const bool bUseCCD,
		const FReal Dt)
	{

		// Shape-pair Bounds check (not for CCD)
		const FImplicitObject* Implicit0 = Shape0->GetLeafGeometry();
		const FImplicitObject* Implicit1 = Shape1->GetLeafGeometry();
		if (Implicit0->HasBoundingBox() && Implicit1->HasBoundingBox() && !bUseCCD)
		{
			PHYSICS_CSV_SCOPED_EXPENSIVE(PhysicsVerbose, NarrowPhase_ShapeBounds);

			// World-space expanded bounds check
			const FAABB3& ShapeWorldBounds0 = Shape0->GetWorldSpaceInflatedShapeBounds();
			const FAABB3& ShapeWorldBounds1 = Shape1->GetWorldSpaceInflatedShapeBounds();
			if (!ShapeWorldBounds0.Intersects(ShapeWorldBounds1))
			{
				return 0;
			}

			const int32 LastEpoch = MidPhase.GetCollisionAllocator().GetCurrentEpoch() - 1;
			const bool bCollidedLastTick = IsUsedSince(LastEpoch);
			if ((bEnableOBBCheck0 || bEnableOBBCheck1) && !bCollidedLastTick)
			{
				// OBB-AABB test on both directions. This is beneficial for shapes which do not fit their AABBs very well,
				// which includes boxes and other shapes that are not roughly spherical. It is especially beneficial when
				// one shape is long and thin (i.e., it does not fit an AABB well when the shape is rotated).
				// However, it is quite expensive to do this all the time so we only do this test when we did not 
				// collide last frame. This is ok if we assume not much changes from frame to frame, but it means
				// we might call the narrow phase one time too many when shapes become separated.
				const FRigidTransform3& ShapeWorldTransform0 = Shape0->GetLeafWorldTransform();
				const FRigidTransform3& ShapeWorldTransform1 = Shape1->GetLeafWorldTransform();
			
				if (bEnableOBBCheck0)
				{
					if (!ImplicitOverlapOBBToAABB(Implicit0, Implicit1, ShapeWorldTransform0, ShapeWorldTransform1, CullDistance))
					{
						return 0;
					}
				}

				if (bEnableOBBCheck1)
				{
					if (!ImplicitOverlapOBBToAABB(Implicit1, Implicit0, ShapeWorldTransform1, ShapeWorldTransform0, CullDistance))
					{
						return 0;
					}
				}
			}
		}

		return GenerateCollisionImpl(CullDistance, bUseCCD, Dt);
	}

	void FSingleShapePairCollisionDetector::CreateConstraint(const FReal CullDistance)
	{
		PHYSICS_CSV_SCOPED_EXPENSIVE(PhysicsVerbose, NarrowPhase_CreateConstraint);
		check(Constraint == nullptr);

		Constraint = CreateShapePairConstraint(Particle0, Shape0, Particle1, Shape1, CullDistance, ShapePairType);

		Constraint->GetContainerCookie().MidPhase = &MidPhase;
		Constraint->GetContainerCookie().bIsMultiShapePair = false;
		Constraint->GetContainerCookie().CreationEpoch = MidPhase.GetCollisionAllocator().GetCurrentEpoch();
	}

	int32 FSingleShapePairCollisionDetector::GenerateCollisionImpl(const FReal CullDistance, const bool bUseCCD, const FReal Dt)
	{
		if (Constraint == nullptr)
		{
			// Lazy creation of the constraint. If a shape pair never gets within CullDistance of each
			// other, we never allocate a constraint for them. Once they overlap, we reuse the constraint
			// until the owing particles are not overlapping. i.e., we keep the constraint even if
			// the shape pairs stop overlapping, reusing it if they start overlapping again.
			CreateConstraint(CullDistance);
		}

		if (Constraint != nullptr)
		{
			PHYSICS_CSV_SCOPED_EXPENSIVE(PhysicsVerbose, NarrowPhase_UpdateConstraint);

			const FImplicitObject* Implicit0 = Shape0->GetLeafGeometry();
			const FImplicitObject* Implicit1 = Shape1->GetLeafGeometry();
			const FRigidTransform3& ShapeWorldTransform0 = Shape0->GetLeafWorldTransform();
			const FRigidTransform3& ShapeWorldTransform1 = Shape1->GetLeafWorldTransform();
			const int32 LastEpoch = MidPhase.GetCollisionAllocator().GetCurrentEpoch() - 1;
			const bool bWasUpdatedLastTick = IsUsedSince(LastEpoch);

			// If the constraint was not used last frame, it needs to be reset. 
			// If it was used last frame, its data can be used for static friction etc. (unless CCD is enabled)
			Constraint->SetCCDEnabled(bUseCCD);
			if (bWasUpdatedLastTick && !bUseCCD)
			{
				// Copy manifold data used for static friction - we are about to overwrite the manifold points
				// NOTE: does not clear the current manifold points
				Constraint->SaveManifold();
			}
			else
			{
				// Clear all manifold data
				Constraint->ResetManifold();
			}

			bool bWasManifoldRestored = false;
			if (bEnableManifoldCheck)
			{
				// Update the existing manifold. We can re-use as-is if none of the points have moved much and the bodies have not moved much
				// NOTE: this can succeed in "restoring" even if we have no manifold points
				bWasManifoldRestored = Constraint->UpdateAndTryRestoreManifold(ShapeWorldTransform0, ShapeWorldTransform1);
			}
			else
			{
				// We are not trying to reuse manifold points, so reset them but leave stored data intact (for friction)
				Constraint->ResetActiveManifoldContacts();
			}

			if (!bWasManifoldRestored)
			{
				// We will be updating the manifold, if only partially, so update the restore comparison transforms
				Constraint->UpdateLastShapeWorldTransforms(ShapeWorldTransform0, ShapeWorldTransform1);

				// Run the narrow phase
				if (!bUseCCD)
				{
					Collisions::UpdateConstraint(*Constraint.Get(), Shape0->GetLeafWorldTransform(), Shape1->GetLeafWorldTransform(), Dt);
				}
				else
				{
					// Note: This is unusual but we are using a mix of the previous and current transform
					// This is due to how CCD rewinds the position (not rotation) and then sweeps to find the first contact at the current orientation
					// NOTE: These are actor transforms, not CoM transforms
					// @todo(chaos): see if we can easily switch to CoM transforms now in collision loop (shapes are held in actor space)
					// @todo(chaos): this is broken if both objects are CCD
					FConstGenericParticleHandle P0 = Particle0;
					FConstGenericParticleHandle P1 = Particle1;
					const FRigidTransform3 CCDParticleWorldTransform0 = FRigidTransform3(P0->CCDEnabled() ? P0->X() : P0->P(), P0->Q());
					const FRigidTransform3 CCDParticleWorldTransform1 = FRigidTransform3(P1->CCDEnabled() ? P1->X() : P1->P(), P1->Q());
					const FRigidTransform3 CCDShapeWorldTransform0 = Constraint->ImplicitTransform[0] * CCDParticleWorldTransform0;
					const FRigidTransform3 CCDShapeWorldTransform1 = Constraint->ImplicitTransform[1] * CCDParticleWorldTransform1;
					Collisions::UpdateConstraintSwept(*Constraint.Get(), CCDShapeWorldTransform0, CCDShapeWorldTransform1, Dt);
				}
			}

			// If we have a valid contact, add it to the active list
			if (Constraint->GetPhi() <= CullDistance)
			{
				if (MidPhase.GetCollisionAllocator().ActivateConstraint(Constraint.Get()))
				{
					return 1;
				}
			}
		}

		return 0;
	}

	int32 FSingleShapePairCollisionDetector::RestoreCollision(const FReal CullDistance)
	{
		// Only restore constraints if active last tick. Any older than that and the shapes were separated for a bit
		const int32 LastEpoch = MidPhase.GetCollisionAllocator().GetCurrentEpoch() - 1;
		if ((Constraint != nullptr) && IsUsedSince(LastEpoch))
		{
			Constraint->RestoreManifold();
			if (Constraint->GetPhi() <= CullDistance)
			{
				if (MidPhase.GetCollisionAllocator().ActivateConstraint(Constraint.Get()))
				{
					return 1;
				}
			}
		}
		return 0;
	}

	void FSingleShapePairCollisionDetector::WakeCollision(const int32 SleepEpoch)
	{
		if ((Constraint != nullptr) && IsUsedSince(SleepEpoch))
		{
			// We just need to refresh the epoch so that the constraint state will be used as the previous
			// state iof the pair is still colliding in the next tick
			Constraint->GetContainerCookie().LastUsedEpoch = MidPhase.GetCollisionAllocator().GetCurrentEpoch();
		}
	}

	void FSingleShapePairCollisionDetector::SetCollision(const FPBDCollisionConstraint& SourceConstraint)
	{
		if (Constraint == nullptr)
		{
			Constraint = MakeUnique<FPBDCollisionConstraint>();
			Constraint->GetContainerCookie().MidPhase = &MidPhase;
			Constraint->GetContainerCookie().bIsMultiShapePair = false;
			Constraint->GetContainerCookie().CreationEpoch = MidPhase.GetCollisionAllocator().GetCurrentEpoch();
		}

		// Copy the constraint over the existing one, taking care to leave the cookie intact
		const FPBDCollisionConstraintContainerCookie Cookie = Constraint->GetContainerCookie();
		*(Constraint.Get()) = SourceConstraint;
		Constraint->GetContainerCookie() = Cookie;

		// Add the constraint to the active list
		// If the constraint already existed and was already active, this will do nothing
		MidPhase.GetCollisionAllocator().ActivateConstraint(Constraint.Get());
	}


	////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////


	FMultiShapePairCollisionDetector::FMultiShapePairCollisionDetector(
		FGeometryParticleHandle* InParticle0,
		const FPerShapeData* InShape0,
		FGeometryParticleHandle* InParticle1,
		const FPerShapeData* InShape1,
		FParticlePairMidPhase& InMidPhase)
		: MidPhase(InMidPhase)
		, Constraints()
		, Particle0(InParticle0)
		, Particle1(InParticle1)
		, Shape0(InShape0)
		, Shape1(InShape1)
	{
	}

	FMultiShapePairCollisionDetector::FMultiShapePairCollisionDetector(FMultiShapePairCollisionDetector&& R)
		: MidPhase(R.MidPhase)
		, Constraints(MoveTemp(R.Constraints))
		, Particle0(R.Particle0)
		, Particle1(R.Particle1)
		, Shape0(R.Shape0)
		, Shape1(R.Shape1)
	{
		check(R.NewConstraints.IsEmpty());
	}

	FMultiShapePairCollisionDetector::~FMultiShapePairCollisionDetector()
	{
	}

	int32 FMultiShapePairCollisionDetector::GenerateCollisions(
		const FReal CullDistance,
		const bool bUseCCD,
		const FReal Dt,
		FCollisionContext& Context)
	{
		FConstGenericParticleHandle P0 = Particle0;
		FConstGenericParticleHandle P1 = Particle1;

		const FImplicitObject* Implicit0 = Shape0->GetLeafGeometry();
		const FBVHParticles* BVHParticles0 = P0->CollisionParticles().Get();
		const FRigidTransform3& ShapeRelativeTransform0 = Shape0->GetLeafRelativeTransform();
		const FRigidTransform3 ParticleWorldTransform0 = FParticleUtilities::GetActorWorldTransform(P0);
		const FImplicitObject* Implicit1 = Shape1->GetLeafGeometry();
		const FBVHParticles* BVHParticles1 = P1->CollisionParticles().Get();
		const FRigidTransform3& ShapeRelativeTransform1 = Shape1->GetLeafRelativeTransform();
		const FRigidTransform3 ParticleWorldTransform1 = FParticleUtilities::GetActorWorldTransform(P1);

		FCollisionContext LocalContext = Context;
		LocalContext.CollisionAllocator = this;

		Collisions::ConstructConstraints(
			Particle0, 
			Particle1, 
			Implicit0, 
			BVHParticles0, 
			Implicit1, 
			BVHParticles1, 
			ParticleWorldTransform0,
			ShapeRelativeTransform0,
			ParticleWorldTransform1,
			ShapeRelativeTransform1,
			CullDistance,
			Dt,
			LocalContext);

		int32 NumActiveConstraints = ProcessNewConstraints();

		// @todo(chaos): we could clean up unused collisions between this pair, but probably not worth it
		// and we would have to prevent cleanup for sleeping particles because the collisions will still
		// be referenced in the IslandManager's constraint graph for the sleeping island.
		//PruneConstraints();

		return NumActiveConstraints;
	}

	FPBDCollisionConstraint* FMultiShapePairCollisionDetector::FindOrCreateConstraint(
		FGeometryParticleHandle* InParticle0,
		const FImplicitObject* Implicit0,
		const FBVHParticles* BVHParticles0,
		const FRigidTransform3& ShapeRelativeTransform0,
		FGeometryParticleHandle* InParticle1,
		const FImplicitObject* Implicit1,
		const FBVHParticles* BVHParticles1,
		const FRigidTransform3& ShapeRelativeTransform1,
		const FReal CullDistance,
		const EContactShapesType ShapePairType,
		const bool bUseManifold)
	{
		// This is a callback from the low-level collision function. It should always be the same two particles, though the
		// shapes may be some children in the implicit hierarchy. The particles could be in the opposite order though, and
		// this will depend on the shape types involved. E.g., with two particles each with a sphere and a box in a union
		// would require up to two Sphere-Box contacts, with the particles in opposite orders.
		if (!ensure(((InParticle0 == Particle0) && (InParticle1 == Particle1)) || ((InParticle0 == Particle1) && (InParticle1 == Particle0))))
		{
			// We somehow received a callback for the wrong particle pair...this should not happen
			return nullptr;
		}

		const FCollisionParticlePairConstraintKey Key = FCollisionParticlePairConstraintKey(Implicit0, BVHParticles0, Implicit1, BVHParticles1);
		FPBDCollisionConstraint* Constraint = FindConstraint(Key);

		// @todo(chaos): fix key uniqueness guarantee.  We need a truly unique key gen function
		const bool bIsKeyCollision = (Constraint != nullptr) && ((Constraint->GetImplicit0() != Implicit0) || (Constraint->GetImplicit1() != Implicit1) || (Constraint->GetCollisionParticles0() != BVHParticles0) || (Constraint->GetCollisionParticles1() != BVHParticles1));
		if (bIsKeyCollision)
		{
			// If we get here, we have a key collision. The key uses a hash of pointers which is very likely to be unique for different implicit pairs, 
			// especially since it only needs to be unique for this particle pair, but it is not guaranteed.
			// Creating a new constraint with the same key could cause fatal problems (the original constraint will be deleted when we add the new one 
			// to the map, but if it is asleep it will be referenced in the contact graph) so we just abort and accept we will miss collisions. 
			// It is extremely unlikely to happen but we should fix it at some point.
			ensure(false);
			return nullptr;
		}

		if (Constraint == nullptr)
		{
			// NOTE: Using InParticle0 and InParticle1 here because the order may be different to what we have stored
			Constraint = CreateConstraint(InParticle0, Implicit0, BVHParticles0, ShapeRelativeTransform0, InParticle1, Implicit1, BVHParticles1, ShapeRelativeTransform1, CullDistance, ShapePairType, bUseManifold, Key);
		}
		NewConstraints.Add(Constraint);
		return Constraint;
	}


	FPBDCollisionConstraint* FMultiShapePairCollisionDetector::FindOrCreateSweptConstraint(
		FGeometryParticleHandle* InParticle0,
		const FImplicitObject* Implicit0,
		const FBVHParticles* BVHParticles0,
		const FRigidTransform3& ShapeRelativeTransform0,
		FGeometryParticleHandle* InParticle1,
		const FImplicitObject* Implicit1,
		const FBVHParticles* BVHParticles1,
		const FRigidTransform3& ShapeRelativeTransform1,
		const FReal CullDistance,
		const EContactShapesType ShapePairType)
	{
		const bool bUseManifold = true;
		FPBDCollisionConstraint* Constraint = FindOrCreateConstraint(InParticle0, Implicit0, BVHParticles0, ShapeRelativeTransform0, InParticle1, Implicit1, BVHParticles1, ShapeRelativeTransform1, CullDistance, ShapePairType, bUseManifold);
		if (Constraint != nullptr)
		{
			Constraint->SetCCDEnabled(true);
		}
		return Constraint;
	}

	FPBDCollisionConstraint* FMultiShapePairCollisionDetector::FindConstraint(const FCollisionParticlePairConstraintKey& Key)
	{
		TUniquePtr<FPBDCollisionConstraint>* Constraint = Constraints.Find(Key.GetKey());
		if (Constraint != nullptr)
		{
			return (*Constraint).Get();
		}
		return nullptr;
	}

	FPBDCollisionConstraint* FMultiShapePairCollisionDetector::CreateConstraint(
		FGeometryParticleHandle* InParticle0,
		const FImplicitObject* Implicit0,
		const FBVHParticles* BVHParticles0,
		const FRigidTransform3& ShapeRelativeTransform0,
		FGeometryParticleHandle* InParticle1,
		const FImplicitObject* Implicit1,
		const FBVHParticles* BVHParticles1,
		const FRigidTransform3& ShapeRelativeTransform1,
		const FReal CullDistance,
		const EContactShapesType ShapePairType,
		const bool bInUseManifold,
		const FCollisionParticlePairConstraintKey& Key)
	{
		PHYSICS_CSV_SCOPED_EXPENSIVE(PhysicsVerbose, NarrowPhase_CreateConstraint);
		TUniquePtr<FPBDCollisionConstraint> Constraint = CreateImplicitPairConstraint(InParticle0, Implicit0, BVHParticles0, ShapeRelativeTransform0, InParticle1, Implicit1, BVHParticles1, ShapeRelativeTransform1, CullDistance, ShapePairType, bInUseManifold);
		
		Constraint->GetContainerCookie().MidPhase = &MidPhase;
		Constraint->GetContainerCookie().bIsMultiShapePair = true;
		Constraint->GetContainerCookie().CreationEpoch = MidPhase.GetCollisionAllocator().GetCurrentEpoch();

		return Constraints.Add(Key.GetKey(), MoveTemp(Constraint)).Get();
	}

	int32 FMultiShapePairCollisionDetector::RestoreCollisions(const FReal CullDistance)
	{
		int32 NumRestored = 0;
		const int32 LastEpoch = MidPhase.GetCollisionAllocator().GetCurrentEpoch() - 1;
		for (auto& KVP : Constraints)
		{
			TUniquePtr<FPBDCollisionConstraint>& Constraint = KVP.Value;
			if ((Constraint != nullptr) && (Constraint->GetContainerCookie().LastUsedEpoch >= LastEpoch))
			{
				Constraint->RestoreManifold();
				if (Constraint->GetPhi() < CullDistance)
				{
					if (MidPhase.GetCollisionAllocator().ActivateConstraint(Constraint.Get()))
					{
						++NumRestored;
					}
				}
			}
		}
		return NumRestored;
	}

	void FMultiShapePairCollisionDetector::WakeCollisions(const int32 SleepEpoch)
	{
		const int32 CurrentEpoch = MidPhase.GetCollisionAllocator().GetCurrentEpoch();
		for (auto& KVP : Constraints)
		{
			TUniquePtr<FPBDCollisionConstraint>& Constraint = KVP.Value;
			if (Constraint->GetContainerCookie().LastUsedEpoch >= SleepEpoch)
			{
				Constraint->GetContainerCookie().LastUsedEpoch = CurrentEpoch;
			}
		}
	}

	int32 FMultiShapePairCollisionDetector::ProcessNewConstraints()
	{
		int32 NumActiveConstraints = 0;
		for (FPBDCollisionConstraint* Constraint : NewConstraints)
		{
			if (Constraint->GetPhi() < Constraint->GetCullDistance())
			{
				MidPhase.GetCollisionAllocator().ActivateConstraint(Constraint);
				++NumActiveConstraints;
			}
		}
		NewConstraints.Reset();
		return NumActiveConstraints;
	}

	void FMultiShapePairCollisionDetector::PruneConstraints()
	{
		// We don't prune from NewCollisions - must call ProcessNewCollisions before Prune
		check(NewConstraints.Num() == 0);

		const int32 CurrentEpoch = MidPhase.GetCollisionAllocator().GetCurrentEpoch();

		// Find all the expired collisions
		TArray<uint32> Pruned;
		for (auto& KVP : Constraints)
		{
			const uint32 Key = KVP.Key;
			TUniquePtr<FPBDCollisionConstraint>& Constraint = KVP.Value;
			if (Constraint->GetContainerCookie().LastUsedEpoch < CurrentEpoch)
			{
				Pruned.Add(Key);
			}
		}

		// Destroy expired collisions
		for (uint32 Key : Pruned)
		{
			Constraints.Remove(Key);
		}
	}

	void FMultiShapePairCollisionDetector::VisitCollisions(const int32 LastEpoch, const FPBDCollisionVisitor& Visitor) const
	{
		for (auto& KVP : Constraints)
		{
			const TUniquePtr<FPBDCollisionConstraint>& Constraint = KVP.Value;
			if (Constraint->GetContainerCookie().LastUsedEpoch >= LastEpoch)
			{
				Visitor(Constraint.Get());
			}
		}
	}


	////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////


	FParticlePairMidPhase::FParticlePairMidPhase(
		FGeometryParticleHandle* InParticle0, 
		FGeometryParticleHandle* InParticle1, 
		const FCollisionParticlePairKey& InKey,
		FCollisionConstraintAllocator& InCollisionAllocator)
		: Particle0(InParticle0)
		, Particle1(InParticle1)
		, Key(InKey)
		, ShapePairDetectors()
		, MultiShapePairDetectors()
		, CollisionAllocator(&InCollisionAllocator)
		, bIsCCD(false)
		, bIsInitialized(false)
		, bRestorable(false)
		, bIsSleeping(false)
		, LastUsedEpoch(INDEX_NONE)
		, NumActiveConstraints(0)
		, RestoreThresholdZeroContacts()
		, RestoreThreshold()
		, RestoreParticleP0(FVec3(0))
		, RestoreParticleP1(FVec3(0))
		, RestoreParticleQ0(FRotation3::FromIdentity())
		, RestoreParticleQ1(FRotation3::FromIdentity())
	{
		if (ensure(Particle0 != Particle1))
		{
			Init();
		}
	}

	FParticlePairMidPhase::~FParticlePairMidPhase()
	{
		Reset();
	}

	void FParticlePairMidPhase::DetachParticle(FGeometryParticleHandle* Particle)
	{
		Reset();

		if (Particle == Particle0)
		{
			Particle0 = nullptr;
		}
		else if (Particle == Particle1)
		{
			Particle1 = nullptr;
		}
	}

	void FParticlePairMidPhase::Reset()
	{
		ShapePairDetectors.Reset();
		MultiShapePairDetectors.Reset();

		bIsCCD = false;
		bIsInitialized = false;
		bIsSleeping = false;
	}

	void FParticlePairMidPhase::Init()
	{
		PHYSICS_CSV_SCOPED_EXPENSIVE(PhysicsVerbose, NarrowPhase_Filter);

		bIsCCD = FConstGenericParticleHandle(Particle0)->CCDEnabled() || FConstGenericParticleHandle(Particle1)->CCDEnabled();

		bRestorable = true;

		BuildDetectors();

		InitRestoreThresholds();

		bIsInitialized = true;
	}

	void FParticlePairMidPhase::BuildDetectors()
	{
		if (IsValid() && (Particle0 != Particle1))
		{
			const FShapesArray& Shapes0 = Particle0->ShapesArray();
			const FShapesArray& Shapes1 = Particle1->ShapesArray();
			for (int32 ShapeIndex0 = 0; ShapeIndex0 < Shapes0.Num(); ++ShapeIndex0)
			{
				const FPerShapeData* Shape0 = Shapes0[ShapeIndex0].Get();
				for (int32 ShapeIndex1 = 0; ShapeIndex1 < Shapes1.Num(); ++ShapeIndex1)
				{
					const FPerShapeData* Shape1 = Shapes1[ShapeIndex1].Get();
					TryAddShapePair(Shape0, Shape1);
				}
			}
		}
	}

	void FParticlePairMidPhase::TryAddShapePair(const FPerShapeData* Shape0, const FPerShapeData* Shape1)
	{
		const FImplicitObject* Implicit0 = Shape0->GetLeafGeometry();
		const FBVHParticles* BVHParticles0 = FConstGenericParticleHandle(Particle0)->CollisionParticles().Get();
		const EImplicitObjectType ImplicitType0 = (Implicit0 != nullptr) ? GetInnerType(Implicit0->GetCollisionType()) : ImplicitObjectType::Unknown;

		const FImplicitObject* Implicit1 = Shape1->GetLeafGeometry();
		const FBVHParticles* BVHParticles1 = FConstGenericParticleHandle(Particle1)->CollisionParticles().Get();
		const EImplicitObjectType ImplicitType1 = (Implicit1 != nullptr) ? GetInnerType(Implicit1->GetCollisionType()) : ImplicitObjectType::Unknown;

		const bool bDoPassFilter = DoCollide(ImplicitType0, Shape0, ImplicitType1, Shape1);
		if (bDoPassFilter)
		{
			bool bSwap = false;
			const EContactShapesType ShapePairType = Collisions::CalculateShapePairType(Implicit0, BVHParticles0, Implicit1, BVHParticles1, bSwap);

			if (ShapePairType != EContactShapesType::Unknown)
			{
				if (!bSwap)
				{
					ShapePairDetectors.Emplace(FSingleShapePairCollisionDetector(Particle0, Shape0, Particle1, Shape1, ShapePairType, *this));
				}
				else
				{
					ShapePairDetectors.Emplace(FSingleShapePairCollisionDetector(Particle1, Shape1, Particle0, Shape0, ShapePairType, *this));
				}
			}
			else
			{
				if (ensure(!bSwap))
				{
					MultiShapePairDetectors.Emplace(FMultiShapePairCollisionDetector(Particle0, Shape0, Particle1, Shape1, *this));
				}
			}

			// We don't allow full constraint restoration for LevelSets or Unions because small changes in 
			// transform can change what conatct points are generated.
			// @todo(chaos): LevelSets require one-shot manifolds to suport full restore
			// @todo(chaos): Unions may need to reactivate constraints that were not used last frame to support full restore
			if ((ShapePairType == EContactShapesType::LevelSetLevelSet) || (ShapePairType == EContactShapesType::Unknown))
			{
				bRestorable = false;
			}
		}
	}

	bool FParticlePairMidPhase::ShouldEnableCCD(const FReal Dt)
	{
		if (bIsCCD)
		{
			FConstGenericParticleHandle ConstParticle0 = FConstGenericParticleHandle(Particle0);
			FConstGenericParticleHandle ConstParticle1 = FConstGenericParticleHandle(Particle1);

			FReal LengthCCD = 0;
			FVec3 DirCCD = FVec3(0);
			const FVec3 StartX0 = (ConstParticle0->ObjectState() == EObjectStateType::Kinematic) ? ConstParticle0->P() - ConstParticle0->V() * Dt : ConstParticle0->X();
			const FVec3 StartX1 = (ConstParticle1->ObjectState() == EObjectStateType::Kinematic) ? ConstParticle1->P() - ConstParticle1->V() * Dt : ConstParticle1->X();
			const bool bUseCCD = Collisions::ShouldUseCCD(Particle0, StartX0, Particle1, StartX1, DirCCD, LengthCCD, false);

			return bUseCCD;
		}
		return false;
	}

	void FParticlePairMidPhase::InitRestoreThresholds()
	{
		// @todo(chaos): improve this threshold calculation for thin objects? Dynamic thin objects have bigger problems so maybe we don't care
		// @todo(chaos): Spheres and capsules need smaller position tolerance - the restore test doesn't work well with rolling
		bool bIsDynamic0 = FConstGenericParticleHandle(Particle0)->IsDynamic();
		bool bIsDynamic1 = FConstGenericParticleHandle(Particle1)->IsDynamic();
		const FReal BoundsSize0 = bIsDynamic0 ? Particle0->LocalBounds().Extents().GetMin() : TNumericLimits<FReal>::Max();
		const FReal BoundsSize1 = bIsDynamic1 ? Particle1->LocalBounds().Extents().GetMin() : TNumericLimits<FReal>::Max();
		const FReal ThresholdSize = FMath::Min(BoundsSize0, BoundsSize1);

		RestoreThresholdZeroContacts.PositionThreshold = Chaos_Collision_RestoreTolerance_NoContact_Position * ThresholdSize;
		RestoreThresholdZeroContacts.RotationThreshold = Chaos_Collision_RestoreTolerance_NoContact_Rotation;

		RestoreThreshold.PositionThreshold = Chaos_Collision_RestoreTolerance_Contact_Position * ThresholdSize;
		RestoreThreshold.RotationThreshold = Chaos_Collision_RestoreTolerance_Contact_Rotation;
	}

	bool FParticlePairMidPhase::ShouldRestoreConstraints(const FReal Dt)
	{
		const FVec3& ParticleP0 = FConstGenericParticleHandle(Particle0)->P();
		const FRotation3& ParticleQ0 = FConstGenericParticleHandle(Particle0)->Q();
		const FVec3& ParticleP1 = FConstGenericParticleHandle(Particle1)->P();
		const FRotation3& ParticleQ1 = FConstGenericParticleHandle(Particle1)->Q();

		// We can only restore collisions if they were created or updated last tick
		if (IsUsedSince(CollisionAllocator->GetCurrentEpoch() - 1))
		{
			const FReal PositionThreshold = (NumActiveConstraints == 0) ? RestoreThresholdZeroContacts.PositionThreshold : RestoreThreshold.PositionThreshold;
			const FReal RotationThreshold = (NumActiveConstraints == 0) ? RestoreThresholdZeroContacts.RotationThreshold : RestoreThreshold.RotationThreshold;

			// If either particle has moved or rotated in world space we cannot reuse the constraint
			if ((ParticleP0 - RestoreParticleP0).IsNearlyZero(PositionThreshold) && (ParticleP1 - RestoreParticleP1).IsNearlyZero(PositionThreshold))
			{
				if (FRotation3::IsNearlyEqual(ParticleQ0, RestoreParticleQ0, RotationThreshold) && FRotation3::IsNearlyEqual(ParticleQ1, RestoreParticleQ1, RotationThreshold))
				{
					// We passed the gauntlet - reuse the constraint
					return true;
				}
			}
		}

		// We have moved and should rebuild the manifold. Update the current manifold transforms for future restore checks
		RestoreParticleP0 = ParticleP0;
		RestoreParticleP1 = ParticleP1;
		RestoreParticleQ0 = ParticleQ0;
		RestoreParticleQ1 = ParticleQ1;
		return false;
	}

	bool FParticlePairMidPhase::TryRestoreConstraints(const FReal Dt, const FReal CullDistance)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Restore);
		PHYSICS_CSV_SCOPED_EXPENSIVE(PhysicsVerbose, DetectCollisions_RestoreCollision);

		// If the particles haven't moved relative to each other, we can just reuse the constraint as-is
		if (ShouldRestoreConstraints(Dt))
		{
			int32 NumRestored = 0;
			for (FSingleShapePairCollisionDetector& ShapePair : ShapePairDetectors)
			{
				NumRestored += ShapePair.RestoreCollision(CullDistance);
			}
			for (FMultiShapePairCollisionDetector& MultiShapePair : MultiShapePairDetectors)
			{
				NumRestored += MultiShapePair.RestoreCollisions(CullDistance);
			}

			NumActiveConstraints = NumRestored;

			PHYSICS_CSV_CUSTOM_EXPENSIVE(PhysicsCounters, NumRestoredContacts, NumRestored, ECsvCustomStatOp::Accumulate);

			// NOTE: We return restored as true, even if we didn't have any constraints to restore. This is for Bodies 
			// that were separated by more than CullDistance last tick and have not moved more than the tolerances.
			return true;
		}
		return false;
	}

	void FParticlePairMidPhase::GenerateCollisions(
		const FReal CullDistance,
		const FReal Dt,
		FCollisionContext& Context)
	{
		if (!IsValid())
		{
			return;
		}

		// Enable CCD?
		const bool bUseCCD = bIsCCD && ShouldEnableCCD(Dt);

		// If the bodies have not moved, we will reuse the constraints as-is
		const bool bCanRestore = bChaos_Collision_EnableManifoldRestore && bRestorable && !bUseCCD;
		const bool bWasRestored = bCanRestore && TryRestoreConstraints(Dt, CullDistance);

		// If the bodies have moved we need to create or update the constraints
		if (!bWasRestored)
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_GenerateCollisions);
			PHYSICS_CSV_SCOPED_EXPENSIVE(PhysicsVerbose, DetectCollisions_NarrowPhase);

			NumActiveConstraints = 0;

			// Run collision detection on all potentially colliding shape pairs
			for (FSingleShapePairCollisionDetector& ShapePair : ShapePairDetectors)
			{
				NumActiveConstraints += ShapePair.GenerateCollision(CullDistance, bUseCCD, Dt);
			}

			for (FMultiShapePairCollisionDetector& MultiShapePair : MultiShapePairDetectors)
			{
				NumActiveConstraints += MultiShapePair.GenerateCollisions(CullDistance, bUseCCD, Dt, Context);
			}
		}

		LastUsedEpoch = CollisionAllocator->GetCurrentEpoch();
	}

	void FParticlePairMidPhase::InjectCollision(const FPBDCollisionConstraint& Constraint)
	{
		if (!Constraint.GetContainerCookie().bIsMultiShapePair)
		{
			// @todo(chaos): remove GetImplicitShape - we should store the shape in the constraint
			const FPerShapeData* Shape0 = Constraint.GetParticle0()->GetImplicitShape(Constraint.GetImplicit0());
			const FPerShapeData* Shape1 = Constraint.GetParticle1()->GetImplicitShape(Constraint.GetImplicit1());

			// @todo(chaos): fix O(N) search for shape pair - store the index in the cookie (it will be the same
			// as long as the ShapesArray on each particle has not changed)
			for (FSingleShapePairCollisionDetector& ShapePair : ShapePairDetectors)
			{
				if (((Shape0 == ShapePair.GetShape0()) && (Shape1 == ShapePair.GetShape1())) || ((Shape0 == ShapePair.GetShape1()) && (Shape1 == ShapePair.GetShape0())))
				{
					ShapePair.SetCollision(Constraint);
				}
			}
		}
		else
		{
			// @todo(chaos): implement cluster Resim restore
			ensure(false);
		}
	}


	bool FParticlePairMidPhase::IsUsedSince(const int32 Epoch) const
	{
		return (LastUsedEpoch >= Epoch);
	}

	void FParticlePairMidPhase::SetIsSleeping(const bool bInIsSleeping)
	{
		// This can be called from two locations:
		// 1)	At the start of the tick as a results of some state change from the game thread such as an explicit wake event,
		//		applying an impulse, or moving a particle.
		// 2)	After the constraint solver phase when we put non-moving islands to sleep.
		// 
		// Note that in both cases there is a collision detection phase before the next constraint solving phase.
		//
		// When awakening we re-activate collisions so that we have a "previous" collision to use for static friction etc.
		// We don't need to do anything when going to sleep because sleeping particles pairs are ignored in collision detection 
		// so the next set of active collisions generated will not contain these collisions.

		if (bIsSleeping != bInIsSleeping)
		{
			// If we are waking particles, reactivate all collisions that were
			// active when we were put to sleep, i.e., all collisions whose LastUsedEpoch
			// is equal to our LastUsedEpoch.
			const bool bWakingUp = !bInIsSleeping;
			if (bWakingUp)
			{
				if (LastUsedEpoch < CollisionAllocator->GetCurrentEpoch())
				{
					// Restore all constraints that were active when we were put to sleep
					for (FSingleShapePairCollisionDetector& ShapePair : ShapePairDetectors)
					{
						ShapePair.WakeCollision(LastUsedEpoch);
					}
					for (FMultiShapePairCollisionDetector& MultiShapePair : MultiShapePairDetectors)
					{
						MultiShapePair.WakeCollisions(LastUsedEpoch);
					}
					LastUsedEpoch = CollisionAllocator->GetCurrentEpoch();
				}
			}
			// If we are going to sleep, there is nothing to do (see comments above)

			bIsSleeping = bInIsSleeping;
		}
	}

	void FParticlePairMidPhase::VisitCollisions(const FPBDCollisionVisitor& Visitor) const
	{
		const int32 LastEpoch = IsSleeping() ? LastUsedEpoch : CollisionAllocator->GetCurrentEpoch();
		for (const FSingleShapePairCollisionDetector& ShapePair : ShapePairDetectors)
		{
			if (ShapePair.IsUsedSince(LastEpoch))
			{
				Visitor(ShapePair.GetConstraint());
			}
		}

		for (const FMultiShapePairCollisionDetector& MultiShapePair : MultiShapePairDetectors)
		{
			MultiShapePair.VisitCollisions(LastEpoch, Visitor);
		}

	}

}


