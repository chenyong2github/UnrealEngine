// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
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
	class FParticlePairCollisionConstraints;
	class FPBDCollisionConstraint;
	class FPBDCollisionConstraints;
	class FPBDCollisionSolver;
	class FSolverBody;
	class FSolverBodyContainer;
	class FPBDCollisionSolverContainer;

	UE_DEPRECATED(4.27, "Use FPBDCollisionConstraint instead")
	typedef FPBDCollisionConstraint FRigidBodyPointContactConstraint;

	CHAOS_API bool ContactConstraintSortPredicate(const FPBDCollisionConstraint& L, const FPBDCollisionConstraint& R);

	class FRigidBodyContactKey
	{
	public:
		FRigidBodyContactKey()
			: Key(0)
		{
		}

		FRigidBodyContactKey(const FGeometryParticleHandle* Particle0, const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, const FGeometryParticleHandle* Particle1, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1)
			: Key(0)
		{
			GenerateHash(Particle0, Implicit0, Simplicial0, Particle1, Implicit1, Simplicial1);
		}

		uint32 GetKey() const
		{
			return Key;
		}

		friend bool operator==(const FRigidBodyContactKey& L, const FRigidBodyContactKey& R)
		{
			return L.Key == R.Key;
		}

		friend bool operator!=(const FRigidBodyContactKey& L, const FRigidBodyContactKey& R)
		{
			return !(L == R);
		}

		friend bool operator<(const FRigidBodyContactKey& L, const FRigidBodyContactKey& R)
		{
			return L.Key < R.Key;
		}

	private:
		void GenerateHash(const FGeometryParticleHandle* Particle0, const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, const FGeometryParticleHandle* Particle1, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1)
		{
			// @todo(chaos): We should use ShapeIndex rather than Implicit/Simplicial pointers
			const uint32 Particle0Hash = HashCombine(::GetTypeHash(Particle0->ParticleID().GlobalID), ::GetTypeHash(Particle0->ParticleID().LocalID));
			const uint32 Particle1Hash = HashCombine(::GetTypeHash(Particle1->ParticleID().GlobalID), ::GetTypeHash(Particle1->ParticleID().LocalID));
			const uint32 ParticlesHash = HashCombine(::GetTypeHash(Particle0Hash), ::GetTypeHash(Particle1Hash));
			const uint32 ImplicitHash = HashCombine(::GetTypeHash(Implicit0), ::GetTypeHash(Implicit1));
			const uint32 SimplicialHash = HashCombine(::GetTypeHash(Simplicial0), ::GetTypeHash(Simplicial1));
			const uint32 ShapesHash = HashCombine(ImplicitHash, SimplicialHash);
			Key = HashCombine(ParticlesHash, ShapesHash);
		}

		uint32 Key;
	};


	class CHAOS_API FManifoldPoint
	{
	public:
		FManifoldPoint() 
			: CoMContactPoints{ FVec3(0), FVec3(0) }
			, ManifoldContactTangents{ FVec3(0), FVec3(0) }
			, ManifoldContactNormal(0)
			, NetImpulse(0)
			, NetPushOut(0)
			, NetPushOutImpulseNormal(0)
			, NetPushOutImpulseTangent(0)
			, NetPushOutNormal(0)
			, NetPushOutTangent(0)
			, NetImpulseNormal(0)
			, NetImpulseTangent(0)
			, InitialContactVelocity(0)
			, InitialPhi(0)
			, bPotentialRestingContact(false)
			, bInsideStaticFrictionCone(false)
			, bRestitutionEnabled(false)
			, bActive(false)
			, StaticFrictionMax(0)
		{}

		FManifoldPoint(const FContactPoint& InContactPoint) 
			: ContactPoint(InContactPoint)
			, CoMContactPoints{ FVec3(0), FVec3(0) }
			, ManifoldContactTangents{ FVec3(0), FVec3(0) }
			, ManifoldContactNormal(0)
			, NetImpulse(0)
			, NetPushOut(0)
			, NetPushOutImpulseNormal(0)
			, NetPushOutImpulseTangent(0)
			, NetPushOutNormal(0)
			, NetPushOutTangent(0)
			, NetImpulseNormal(0)
			, NetImpulseTangent(0)
			, InitialContactVelocity(0)
			, InitialPhi(0)
			, bPotentialRestingContact(false)
			, bInsideStaticFrictionCone(false)
			, bRestitutionEnabled(false)
			, bActive(false)
			, StaticFrictionMax(0)
		{}

		// @todo(chaos): Normal and plane owner should be per manifold, not per manifold-point when we are not using incremental manifolds any more
		FContactPoint ContactPoint;			// Shape-space data from low-level collision detection
		FVec3 CoMContactPoints[2];			// CoM-space contact points on the two bodies core shapes (not including margin)
		FVec3 ManifoldContactTangents[2];		// CoM-space or world space (depending on CVAR bChaos_Collision_Manifold_FixNormalsInWorldSpace) contact tangents for friction
		FVec3 ManifoldContactNormal;				// CoM-space or  world space  (depending on CVAR bChaos_Collision_Manifold_FixNormalsInWorldSpace) contact normal relative to ContactNormalOwner body
		FVec3 CoMAnchorPoints[2];			// CoM-space contact points on the two bodies core shapes used for static friction
		FVec3 NetImpulse;					// Total impulse applied by this contact point
		FVec3 NetPushOut;					// Total pushout applied at this contact point
		FReal NetPushOutImpulseNormal;		// Total pushout impulse along normal (for final velocity correction) applied at this contact point
		FReal NetPushOutImpulseTangent;		// Total pushout impulse along tangent (for final velocity correction) applied at this contact point
		FReal NetPushOutNormal;
		FReal NetPushOutTangent;
		FReal NetImpulseNormal;
		FReal NetImpulseTangent;
		FReal InitialContactVelocity;		// Contact velocity at start of frame (used for restitution)
		FReal InitialPhi;					// Contact separation at first contact (used for pushout restitution)
		bool bPotentialRestingContact;		// Whether this may be a resting contact (used for static friction)
		bool bInsideStaticFrictionCone;		// Whether we are inside the static friction cone (used in PushOut)
		bool bRestitutionEnabled;			// Whether restitution was added in the apply step (used in PushOut)
		bool bActive;						// Whether this contact applied an impulse

		FReal StaticFrictionMax;			// A proxy for the normal impulse used to limit static friction correction. Used for smoothing static friction limits
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
			: Key()
			, CreationEpoch(INDEX_NONE)
			, LastUsedEpoch(INDEX_NONE)
			, ConstraintIndex(INDEX_NONE)
			, SweptConstraintIndex(INDEX_NONE)
			, bIsInShapePairMap(false)
			, bIsSleeping(false)
		{
		}

		/**
		 * @brief Used to clear the container data when copying constraints out of the container (see resim cache)
		 * The constraint index will no be valid when the constraint is copied out of the container, but everything
		 * else is ok and should be restorable.
		*/
		void ClearContainerData()
		{
			ConstraintIndex = INDEX_NONE;
			SweptConstraintIndex = INDEX_NONE;
		}

		/**
		 * @brief Called once on creation by the container to initialize the lifetime contants
		*/
		void Init(const FRigidBodyContactKey& InKey, const int32 InEpoch)
		{
			Key = InKey;
			CreationEpoch = InEpoch;
			LastUsedEpoch = INDEX_NONE;
			bIsSleeping = false;
		}

		/**
		 * @brief Called each tick by the container
		*/
		void Update(const int32 InIndex, const int32 InEpoch)
		{
			ConstraintIndex = InIndex;
			LastUsedEpoch = InEpoch;
			bIsSleeping = false;
		}

		/**
		 * @brief Called each tick by the container for swept constraints
		*/
		void UpdateSweptIndex(const int32 InIndex)
		{
			SweptConstraintIndex = InIndex;
		}

		/**
		 * @brief Change the sleeping state (used by the Island Manager)
		*/
		void SetIsSleeping(const bool bInIsSleeping)
		{
			bIsSleeping = bInIsSleeping;
		}

		// A hash of the colliding object pair to uniquely identify a contact and allow recovery next tick
		FRigidBodyContactKey Key;

		// The conatiner Epoch when then constraint was initially created
		int32 CreationEpoch;

		// The container Epoch when the constraint was last used
		int32 LastUsedEpoch;

		// The index in the container - this changes every tick
		int32 ConstraintIndex;

		// The index in swept constraints - this changes every tick
		int32 SweptConstraintIndex;

		// Whether the Constraint is in the container's maps
		bool bIsInShapePairMap;

		// Whether the constraint is in a sleeping island (this is used to prevent culling because Epoch is not updated for sleepers)
		bool bIsSleeping;
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
		friend class FPBDCollisionConstraints;
		friend CHAOS_API bool ContactConstraintSortPredicate(const FPBDCollisionConstraint& L, const FPBDCollisionConstraint& R);

	public:
		using FConstraintContainerHandle = TIntrusiveConstraintHandle<FPBDCollisionConstraint>;

		/**
		 * @brief Create a standard (non-swept) contact
		 * Allocates a constraint on the heap, with a permanent address.
		 * May return null if we hit the contact limit for the scene.
		*/
		static FPBDCollisionConstraint* Make(
			FGeometryParticleHandle* Particle0,
			const FImplicitObject* Implicit0,
			const FBVHParticles* Simplicial0,
			const FRigidTransform3& ParticleWorldTransform0,
			const FRigidTransform3& ImplicitLocalTransform0,
			FGeometryParticleHandle* Particle1,
			const FImplicitObject* Implicit1,
			const FBVHParticles* Simplicial1,
			const FRigidTransform3& ParticleWorldTransform1,
			const FRigidTransform3& ImplicitLocalTransform1,
			const FReal InCullDistance,
			const EContactShapesType ShapesType,
			const bool bInUseManifold,
			FCollisionConstraintAllocator& Allocator);

		/**
		 * @brief Create a swept contact
		 * Allocates a constraint on the heap, with a permanent address.
		 * May return null if we hit the contact limit for the scene.
		*/
		static FPBDCollisionConstraint* MakeSwept(
			FGeometryParticleHandle* Particle0,
			const FImplicitObject* Implicit0,
			const FBVHParticles* Simplicial0,
			const FRigidTransform3& ParticleWorldTransform0,
			const FRigidTransform3& ImplicitLocalTransform0,
			FGeometryParticleHandle* Particle1,
			const FImplicitObject* Implicit1,
			const FBVHParticles* Simplicial1,
			const FRigidTransform3& ParticleWorldTransform1,
			const FRigidTransform3& ImplicitLocalTransform1,
			const FReal InCullDistance,
			EContactShapesType ShapesType,
			FCollisionConstraintAllocator& Allocator);

		/**
		 * @brief Return a constraint copied from the Source constraint, for use in the Resim Cache
		 * This copies everything needed to rehydrate the contact after a rewind
		 * @note Unlike the other factory methods, this version returns a constraint by value for emplacing into an array (ther others are by pointer)
		*/
		static FPBDCollisionConstraint MakeResimCache(
			const FPBDCollisionConstraint& Source);

		static void Destroy(
			FPBDCollisionConstraint* Constraint,
			FCollisionConstraintAllocator& Allocator);

		FPBDCollisionConstraint();

		ECollisionConstraintType GetType() const { return Type; }

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

		// @todo(chaos): half of this API is wrong for the new multi-point manifold constraints. Remove it

		const FCollisionContact& GetManifold() const { return Manifold; }

		void ResetPhi(FReal InPhi) { SetPhi(InPhi); }

		void SetPhi(FReal InPhi) { Manifold.Phi = InPhi; }
		FReal GetPhi() const { return Manifold.Phi; }

		void SetDisabled(bool bInDisabled) { Manifold.bDisabled = bInDisabled; }
		bool GetDisabled() const { return Manifold.bDisabled; }

		virtual void SetIsSleeping(const bool bInIsSleeping) override;
		virtual bool IsSleeping() const override { return ContainerCookie.bIsSleeping; }

		void SetNormal(const FVec3& InNormal) { Manifold.Normal = InNormal; }
		FVec3 GetNormal() const { return Manifold.Normal; }

		void SetLocation(const FVec3& InLocation) { Manifold.Location = InLocation; }
		FVec3 GetLocation() const { return Manifold.Location; }

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
		void CalculatePrevCoMContactPoints(
			const FSolverBody& Body0,
			const FSolverBody& Body1,
			FManifoldPoint& ManifoldPoint,
			FReal Dt,
			FVec3& OutPrevCoMContactPoint0,
			FVec3& OutPrevCoMContactPoint1) const;

		void AddIncrementalManifoldContact(const FContactPoint& ContactPoint, const FReal Dt);
		void AddOneshotManifoldContact(const FContactPoint& ContactPoint, const FReal Dt);
		void UpdateManifoldContacts();
		void ClearManifold();

		//@ todo(chaos): These are for the collision forwarding system - this should use the collision modifier system (which should be extended to support adding collisions)
		void SetManifoldPoints(const TArray<FManifoldPoint>& InManifoldPoints) { ManifoldPoints = InManifoldPoints; }
		void UpdateManifoldPointFromContact(FManifoldPoint& ManifoldPoint);

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
		bool IsNew() const { return ContainerCookie.CreationEpoch == ContainerCookie.LastUsedEpoch; }

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
		const FRigidBodyContactKey& GetKey() const { return GetContainerCookie().Key; }

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
			const ECollisionConstraintType InType,
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
		void GetWorldSpaceManifoldPoint(const FManifoldPoint& ManifoldPoint, const FVec3& P0, const FRotation3& Q0, const FVec3& P1, const FRotation3& Q1, FVec3& OutContactLocation, FVec3& OutContactNormal, FReal& OutContactPhi);

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
		ECollisionConstraintType Type;
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
