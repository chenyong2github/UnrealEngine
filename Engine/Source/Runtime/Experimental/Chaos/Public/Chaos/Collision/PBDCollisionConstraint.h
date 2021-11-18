// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionKeys.h"
#include "Chaos/Collision/ContactPoint.h"
#include "Chaos/Collision/PBDCollisionConstraintHandle.h"
#include "Chaos/GJK.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Vector.h"
#include "Chaos/BVHParticles.h"

namespace Chaos
{
	class FCollisionConstraintAllocator;
	class FConstGenericParticleHandle;
	class FImplicitObject;
	class FParticlePairMidPhase;
	class FPBDCollisionConstraint;
	class FPBDCollisionConstraints;
	class FPBDCollisionSolver;
	class FSingleShapePairCollisionDetector;
	class FSolverBody;
	class FSolverBodyContainer;
	class FPBDCollisionSolverContainer;

	UE_DEPRECATED(4.27, "Use FPBDCollisionConstraint instead")
	typedef FPBDCollisionConstraint FRigidBodyPointContactConstraint;

	CHAOS_API bool ContactConstraintSortPredicate(const FPBDCollisionConstraint& L, const FPBDCollisionConstraint& R);

	class CHAOS_API FManifoldPoint
	{
	public:
		FManifoldPoint() 
			: CoMContactPoints{ FVec3(0), FVec3(0) }
			, NetPushOut(0)
			, NetImpulse(0)
			, StaticFrictionMax(0)
			, bInsideStaticFrictionCone(false)
		{}

		FManifoldPoint(const FContactPoint& InContactPoint) 
			: ContactPoint(InContactPoint)
			, CoMContactPoints{ FVec3(0), FVec3(0) }
			, NetPushOut(0)
			, NetImpulse(0)
			, StaticFrictionMax(0)
			, bInsideStaticFrictionCone(false)
		{}

		FContactPoint ContactPoint;			// Contact point results of low-level collision detection
		FVec3 CoMContactPoints[2];			// CoM-space contact points on the two bodies
		FVec3 NetPushOut;					// Total pushout applied at this contact point
		FVec3 NetImpulse;					// Total impulse applied by this contact point
		FReal StaticFrictionMax;			// A proxy for the normal impulse used to limit static friction correction. Used for smoothing static friction limits
		bool bInsideStaticFrictionCone;		// Whether we are inside the static friction cone (used in PushOut)
	};

	/*
	*
	*/
	class CHAOS_API FCollisionContact
	{
	public:
		FCollisionContact(const FImplicitObject* InImplicit0 = nullptr, const FBVHParticles* InSimplicial0 = nullptr, const FImplicitObject* InImplicit1 = nullptr, const FBVHParticles* InSimplicial1 = nullptr)
			: bDisabled(false)
			, bUseAccumalatedImpulseInSolve(true)
			, Normal(0)
			, Location(0)
			, Phi(FLT_MAX)
			, Friction(0)
			, AngularFriction(0)
			, Restitution(0)
			, RestitutionPadding(0)
			, RestitutionThreshold(0)
			, InvMassScale0(1.f)
			, InvMassScale1(1.f)
			, InvInertiaScale0(1.f)
			, InvInertiaScale1(1.f)
			, ContactMoveSQRDistance(0)
			, ShapesType(EContactShapesType::Unknown)
		{
			Implicit[0] = InImplicit0;
			Implicit[1] = InImplicit1;
			Simplicial[0] = InSimplicial0;
			Simplicial[1] = InSimplicial1;
		}

		bool bDisabled;
		bool bUseAccumalatedImpulseInSolve; // Accumulated Impulse will only be used when the contact did not move significantly during iterations (This will reduce jitter)

		FVec3 Normal;
		FVec3 Location;
		FReal Phi;

		FReal Friction;
		FReal AngularFriction;
		FReal Restitution;
		FReal RestitutionPadding; // For PBD implementation of resitution, we pad constraints on initial contact to enforce outward velocity
		FReal RestitutionThreshold;
		FReal InvMassScale0;
		FReal InvMassScale1;
		FReal InvInertiaScale0;
		FReal InvInertiaScale1;
		FReal ContactMoveSQRDistance; // How much the contact position moved after the last update
		EContactShapesType ShapesType;

		void Reset()
		{
			bDisabled = false;
			Phi = FLT_MAX;
			RestitutionPadding = 0;
		}

		FString ToString() const
		{
			return FString::Printf(TEXT("Location:%s, Normal:%s, Phi:%f"), *Location.ToString(), *Normal.ToString(), Phi);
		}

		const FImplicitObject* Implicit[2]; // {Of Particle[0], Of Particle[1]}
		const FBVHParticles* Simplicial[2]; // {Of Particle[0], Of Particle[1]}
	};


	/**
	 * @brief Information used by the constraint allocator
	 * This includes any information used for optimizations like array indexes etc
	 * as well as data for managing lifetime and pruning.
	*/
	class FPBDCollisionConstraintContainerCookie
	{
	public:
		FPBDCollisionConstraintContainerCookie()
			: MidPhase(nullptr)
			, bIsMultiShapePair(false)
			, CreationEpoch(INDEX_NONE)
			, LastUsedEpoch(INDEX_NONE)
			, ConstraintIndex(INDEX_NONE)
			, SweptConstraintIndex(INDEX_NONE)
		{
		}

		/**
		 * @brief Used to clear the container data when copying constraints out of the container (see resim cache)
		 * The constraint index will not be valid when the constraint is copied out of the container, but everything
		 * else is ok and should be restorable.
		*/
		void ClearContainerData()
		{
			MidPhase = nullptr;
			ConstraintIndex = INDEX_NONE;
			SweptConstraintIndex = INDEX_NONE;
		}

		// The constraint owner - set when the constraint is created
		FParticlePairMidPhase* MidPhase;

		// Used by the MidPhase when a constraint is reactivated from a Resim cache
		// If true, indicates that the constraint was created from the recursive collision detection
		// path rather than the prefiltered shape-pair loop
		bool bIsMultiShapePair;

		// The Epoch when then constraint was initially created
		int32 CreationEpoch;

		// The Epoch when the constraint was last used
		int32 LastUsedEpoch;

		// The index in the container - this changes every tick
		int32 ConstraintIndex;

		// The index in swept constraints - this changes every tick
		int32 SweptConstraintIndex;
	};


	/**
	 * @brief A contact constraint
	 * 
	 * A contact constraint represents the non-penetration, friction, and restitution constraints for a single
	 * shape pair on a particle pair. I.e., a specific particle-pair may have multiple contact constraints
	 * between them if one or boht has multuple collision shapes that overlap the shape(s) of the other body.
	 * 
	 * Each contact constraint contains a Manifold, which is a set of contact points that approximate the
	 * contact patch between the two shapes.
	 * 
	 * Contact constraints are allocated on the heap and have permanent addresses. They use intrusive handles
	 * to reduce unnecessary indirection.
	 * 
	*/
	class CHAOS_API FPBDCollisionConstraint : public FPBDCollisionConstraintHandle
	{
		friend class FCollisionConstraintAllocator;
		friend class FMultiShapePairCollisionDetector;
		friend class FParticlePairMidPhase;
		friend class FPBDCollisionConstraints;
		friend class FSingleShapePairCollisionDetector;

		friend CHAOS_API bool ContactConstraintSortPredicate(const FPBDCollisionConstraint& L, const FPBDCollisionConstraint& R);

	public:
		using FConstraintContainerHandle = TIntrusiveConstraintHandle<FPBDCollisionConstraint>;

		/**
		 * @brief Create a contact constraint
		 * Allocates a constraint on the heap, with a permanent address.
		 * May return null if we hit the contact limit for the scene.
		*/
		static TUniquePtr<FPBDCollisionConstraint> Make(
			FGeometryParticleHandle* Particle0,
			const FImplicitObject* Implicit0,
			const FBVHParticles* Simplicial0,
			const FRigidTransform3& ImplicitLocalTransform0,
			FGeometryParticleHandle* Particle1,
			const FImplicitObject* Implicit1,
			const FBVHParticles* Simplicial1,
			const FRigidTransform3& ImplicitLocalTransform1,
			const FReal InCullDistance,
			const bool bInUseManifold,
			const EContactShapesType ShapesType);

		/**
		 * @brief Return a constraint copied from the Source constraint, for use in the Resim Cache or other system
		 * @note Unlike the other factory method, this version returns a constraint by value for emplacing into an array (ther others are by pointer)
		*/
		static FPBDCollisionConstraint MakeCopy(const FPBDCollisionConstraint& Source);

		FPBDCollisionConstraint();

		/**
		 * @brief The current CCD state of this constraint
		 * This may change from tick to tick as an object's velocity changes.
		*/
		ECollisionCCDType GetCCDType() const { return CCDType; }

		/**
		 * @brief Enable or disable CCD for this constraint
		*/
		void SetCCDEnabled(const bool bCCDEnabled) { CCDType = bCCDEnabled ? ECollisionCCDType::Enabled : ECollisionCCDType::Disabled; }

		bool ContainsManifold(const FImplicitObject* A, const FBVHParticles* AS, const FImplicitObject* B, const FBVHParticles* BS) const
		{
			return A == Manifold.Implicit[0] && B == Manifold.Implicit[1] && AS == Manifold.Simplicial[0] && BS == Manifold.Simplicial[1];
		}

		void SetManifold(const FImplicitObject* A, const FBVHParticles* AS, const FImplicitObject* B, const FBVHParticles* BS)
		{
			Manifold.Implicit[0] = A; Manifold.Implicit[1] = B;
			Manifold.Simplicial[0] = AS; Manifold.Simplicial[1] = BS;
		}

		//
		// API
		//

		FGeometryParticleHandle* GetParticle0() { return Particle[0]; }
		const FGeometryParticleHandle* GetParticle0() const { return Particle[0]; }
		FGeometryParticleHandle* GetParticle1() { return Particle[1]; }
		const FGeometryParticleHandle* GetParticle1() const { return Particle[1]; }
		const FImplicitObject* GetImplicit0() const { return Manifold.Implicit[0]; }
		const FImplicitObject* GetImplicit1() const { return Manifold.Implicit[1]; }
		const FBVHParticles* GetCollisionParticles0() const { return Manifold.Simplicial[0]; }
		const FBVHParticles* GetCollisionParticles1() const { return Manifold.Simplicial[1]; }

		// @todo(chaos): half of this API is wrong for the new multi-point manifold constraints. Remove it

		const FCollisionContact& GetManifold() const { return Manifold; }

		void ResetPhi(FReal InPhi) { SetPhi(InPhi); }

		void SetPhi(FReal InPhi) { Manifold.Phi = InPhi; }
		FReal GetPhi() const { return Manifold.Phi; }

		void SetDisabled(bool bInDisabled) { Manifold.bDisabled = bInDisabled; }
		bool GetDisabled() const { return Manifold.bDisabled; }

		virtual void SetIsSleeping(const bool bInIsSleeping) override;
		//virtual bool IsSleeping() const override { return ContainerCookie.bIsSleeping; }

		void SetNormal(const FVec3& InNormal) { Manifold.Normal = InNormal; }
		FVec3 GetNormal() const { return Manifold.Normal; }

		void SetLocation(const FVec3& InLocation) { Manifold.Location = InLocation; }
		FVec3 GetLocation() const { return Manifold.Location; }

		void SetInvMassScale0(const FReal InInvMassScale) { Manifold.InvMassScale0 = InInvMassScale; }
		FReal GetInvMassScale0() const { return Manifold.InvMassScale0; }

		void SetInvMassScale1(const FReal InInvMassScale) { Manifold.InvMassScale1 = InInvMassScale; }
		FReal GetInvMassScale1() const { return Manifold.InvMassScale1; }

		void SetInvInertiaScale0(const FReal InInvInertiaScale) { Manifold.InvInertiaScale0 = InInvInertiaScale; }
		FReal GetInvInertiaScale0() const { return Manifold.InvInertiaScale0; }

		void SetInvInertiaScale1(const FReal InInvInertiaScale) { Manifold.InvInertiaScale1 = InInvInertiaScale; }
		FReal GetInvInertiaScale1() const { return Manifold.InvInertiaScale1; }

		void SetStiffness(FReal InStiffness) { Stiffness = InStiffness; }
		FReal GetStiffness() const { return Stiffness; }

		FString ToString() const;

		FReal GetCullDistance() const { return CullDistance; }
		void SetCullDistance(FReal InCullDistance) { CullDistance = InCullDistance; }

		bool GetUseManifold() const { return bUseManifold; }
		
		bool GetUseIncrementalCollisionDetection() const { return !bUseManifold || bUseIncrementalManifold; }

		void SetNumActivePositionIterations(const int32 InNumActivePositionIterations) { NumActivePositionIterations = InNumActivePositionIterations; }
		int32 GetNumActivePositionIterations() const { return NumActivePositionIterations; }

		/**
		 * @brief Clear the current and previous manifolds
		*/
		void ResetManifold();

		TArrayView<FManifoldPoint> GetManifoldPoints() { return MakeArrayView(ManifoldPoints); }
		TArrayView<const FManifoldPoint> GetManifoldPoints() const { return MakeArrayView(ManifoldPoints); }
		FManifoldPoint& SetActiveManifoldPoint(
			int32 ManifoldPointIndex,
			const FVec3& P0,
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1);

		void AddIncrementalManifoldContact(const FContactPoint& ContactPoint, const FReal Dt);
		void AddOneshotManifoldContact(const FContactPoint& ContactPoint, const FReal Dt);
		void UpdateManifoldContacts();
		void ClearManifold();

		//@ todo(chaos): These are for the collision forwarding system - this should use the collision modifier system (which should be extended to support adding collisions)
		void SetManifoldPoints(const TArray<FManifoldPoint>& InManifoldPoints) { ManifoldPoints = InManifoldPoints; }
		void UpdateManifoldPointFromContact(FManifoldPoint& ManifoldPoint);

		// Helpers for interacting with constraint from world space
		static void GetWorldSpaceContactPositions(const FManifoldPoint& ManifoldPoint, const FVec3& PCoM0, const FRotation3& QCoM0, const FVec3& PCoM1, const FRotation3& QCoM1, FVec3& OutWorldPosition0, FVec3& OutWorldPosition1);
		static void GetCoMContactPositionsFromWorld(const FManifoldPoint& ManifoldPoint, const FVec3& PCoM0, const FRotation3& QCoM0, const FVec3& PCoM1, const FRotation3& QCoM1, const FVec3& WorldPoint0, const FVec3& WorldPoint1, FVec3& OutCoMPoint0, FVec3& OutCoMPoint1);


		// The GJK warm-start data. This is updated directly in the narrow phase
		FGJKSimplexData& GetGJKWarmStartData() { return GJKWarmStartData; }

		FSolverBody* GetSolverBody0() { return SolverBodies[0]; }
		const FSolverBody* GetSolverBody0() const { return SolverBodies[0]; }
		FSolverBody* GetSolverBody1() { return SolverBodies[1]; }
		const FSolverBody* GetSolverBody1() const { return SolverBodies[1]; }

		void SetSolverBodies(FSolverBody* InSolverBody0, FSolverBody* InSolverBody1)
		{
			SolverBodies[0] = InSolverBody0;
			SolverBodies[1] = InSolverBody1;
		}

		/**
		 * @brief Whether this constraint was newly created this tick (as opposed to restored from a previous tick)
		 * @see IsRestored()
		*/
		//bool IsNew() const { return ContainerCookie.CreationEpoch == ContainerCookie.LastUsedEpoch; }

		/**
		 * @brief Whether this constraint was fully restored from a previous tick, and the manifold should be reused as-is
		*/
		bool WasManifoldRestored() const { return bWasManifoldRestored; }

		/**
		 * @brief Restore the contact manifold (assumes relative motion of the two bodies is small)
		 * @see IsWithinManifoldRestorationThreshold
		*/
		void RestoreManifold();

		/**
		 * Determine the constraint direction based on Normal and Phi.
		 * This function assumes that the constraint is update-to-date.
		 */
		ECollisionConstraintDirection GetConstraintDirection(const FReal Dt) const;

		/**
		 * @brief The container key for this contact (for internal use only)
		*/
		//const FRigidBodyContactKey& GetKey() const { return GetContainerCookie().Key; }

	public:
		const FPBDCollisionConstraintHandle* GetConstraintHandle() const { return this; }
		FPBDCollisionConstraintHandle* GetConstraintHandle() { return this; }

	protected:

		FPBDCollisionConstraint(
			FGeometryParticleHandle* Particle0,
			const FImplicitObject* Implicit0,
			const FBVHParticles* Simplicial0,
			FGeometryParticleHandle* Particle1,
			const FImplicitObject* Implicit1,
			const FBVHParticles* Simplicial1);

		// Set all the data not initialized in the constructor
		void Setup(
			const ECollisionCCDType InCCDType,
			const EContactShapesType InShapesType,
			const FRigidTransform3& InImplicitTransform0,
			const FRigidTransform3& InImplicitTransform1,
			const FReal InCullDistance,
			const bool bInUseManifold);

		// Access to the data used by the container
		const FPBDCollisionConstraintContainerCookie& GetContainerCookie() const { return ContainerCookie; }
		FPBDCollisionConstraintContainerCookie& GetContainerCookie() { return ContainerCookie; }

		// Whether we can use manifolds for the given partices. Manifolds do not work well with Joints and PBD
		// because the bodies may be moved (and especially rotated) a lot in the solver and this can make the manifold extremely inaccurate
		bool CanUseManifold(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1) const;

		bool AreMatchingContactPoints(const FContactPoint& A, const FContactPoint& B, FReal& OutScore) const;
		int32 FindManifoldPoint(const FContactPoint& ContactPoint) const;
		int32 AddManifoldPoint(const FContactPoint& ContactPoint, const FReal Dt);
		void InitManifoldPoint(FManifoldPoint& ManifoldPoint, FReal Dt);
		void UpdateManifoldPoint(int32 ManifoldPointIndex, const FContactPoint& ContactPoint, const FReal Dt);
		void SetActiveContactPoint(const FContactPoint& ContactPoint);
		void GetWorldSpaceManifoldPoint(const FManifoldPoint& ManifoldPoint, const FVec3& P0, const FRotation3& Q0, const FVec3& P1, const FRotation3& Q1, FVec3& OutContactLocation, FReal& OutContactPhi);

		/**
		 * @brief Move the current manifold to the previous manifold, and reset the current manifold ready for it to be rebuilt
		*/
		void SaveManifold();

		/**
		 * @brief Restore data used for static friction from the previous point(s) to the current
		*/
		bool TryRestoreFrictionData(FManifoldPoint& ManifoldPoint);

	public:
		//@todo(chaos): make this stuff private
		FRigidTransform3 ImplicitTransform[2];		// Local-space transforms of the shape (relative to particle)
		FGeometryParticleHandle* Particle[2];
		FVec3 AccumulatedImpulse;					// @todo(chaos): we need to accumulate angular impulse separately
		FCollisionContact Manifold;// @todo(chaos): rename

		// Value in range [0,1] used to interpolate P between [X,P] that we will rollback to when solving at time of impact.
		FReal TimeOfImpact;

	private:
		FPBDCollisionConstraintContainerCookie ContainerCookie;
		ECollisionCCDType CCDType;
		FReal Stiffness;

		// @todo(chaos): Switching this to inline allocator does not help, but maybe a physics scratchpad would
		//TArray<FManifoldPoint, TInlineAllocator<4>> ManifoldPoints;
		TArray<FManifoldPoint> ManifoldPoints;
		FReal CullDistance;
		bool bUseManifold;
		bool bUseIncrementalManifold;

		// These are only needed here while we still have thge lgacy solvers (not QuasiPBD)
		FSolverBody* SolverBodies[2];

		// Simplex data from the last call to GJK, used to warm-start GJK
		FGJKSimplexData GJKWarmStartData;

		// The manifold points from the previous tick when we don't reuse the manifold. Used by static friction.
		// @todo(chaos): this could be a subset of the manifold data
		TArray<FManifoldPoint> PrevManifoldPoints;

		bool bWasManifoldRestored;

		int32 NumActivePositionIterations;
	};


	UE_DEPRECATED(4.27, "FCollisionConstraintBase has been removed and folded into FPBDCollisionConstraint. Use FPBDCollisionConstraint")
	typedef FPBDCollisionConstraint FCollisionConstraintBase;

	UE_DEPRECATED(4.27, "FRigidBodySweptPointContactConstraint has been removed and folded into FPBDCollisionConstraint. Use FPBDCollisionConstraint")
	typedef FPBDCollisionConstraint FRigidBodySweptPointContactConstraint;
}
