// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/Core.h"

#include "Chaos/BVHParticles.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionKeys.h"
#include "Chaos/Collision/ContactPoint.h"
#include "Chaos/Collision/PBDCollisionConstraintHandle.h"
#include "Chaos/Framework/UncheckedArray.h"
#include "Chaos/GJK.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Vector.h"

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

	/**
	 * @brief A single point in a contact manifold.
	 * Each Collision Constraint will have up to 4 of these.
	*/
	class CHAOS_API FManifoldPoint
	{
	public:
		FManifoldPoint() 
			: ContactPoint()
			, ShapeAnchorPoints{ FVec3(0), FVec3(0) }
			, InitialShapeContactPoints{ FVec3(0), FVec3(0) }
			, WorldContactPoints{ FVec3(0), FVec3(0) }
			, NetPushOut(0)
			, NetImpulse(0)
			, Flags()
		{}

		FManifoldPoint(const FContactPoint& InContactPoint) 
			: ContactPoint(InContactPoint)
			, ShapeAnchorPoints{ FVec3(0), FVec3(0) }
			, InitialShapeContactPoints{ FVec3(0), FVec3(0) }
			, WorldContactPoints{ FVec3(0), FVec3(0) }
			, NetPushOut(0)
			, NetImpulse(0)
			, Flags()
		{}

		FContactPoint ContactPoint;			// Contact point results of low-level collision detection
		FVec3 ShapeAnchorPoints[2];
		FVec3 InitialShapeContactPoints[2];	// ShapeContactPoints when the constraint was first initialized. Used to track reusablility
		FVec3 WorldContactPoints[2];		// World-space contact points on the two bodies
		FVec3 NetPushOut;					// Total pushout applied at this contact point
		FVec3 NetImpulse;					// Total impulse applied by this contact point

		union FFlags
		{
			FFlags() : Bits(0) {}
			struct
			{
				uint32 bInsideStaticFrictionCone : 1;		// Whether we are inside the static friction cone (used in PushOut)
				uint32 bWasRestored : 1;
				uint32 bWasFrictionRestored : 1;
			};
			uint32 Bits;
		} Flags;
	};

	/**
	 * @brief The friction data for a manifold point
	 * This is the information that needs to be stored between ticks to implement static friction.
	*/
	class CHAOS_API FSavedManifoldPoint
	{
	public:
		FVec3 ShapeContactPoints[2];
	};

	/*
	*
	*/
	class CHAOS_API FCollisionContact
	{
	public:
		FCollisionContact(const FImplicitObject* InImplicit0 = nullptr, const FBVHParticles* InSimplicial0 = nullptr, const FImplicitObject* InImplicit1 = nullptr, const FBVHParticles* InSimplicial1 = nullptr)
			: bDisabled(false)
			, Normal(0)
			, Location(0)
			, Phi(TNumericLimits<FReal>::Max())
			, Friction(0)
			, AngularFriction(0)
			, Restitution(0)
			, RestitutionPadding(0)
			, RestitutionThreshold(0)
			, InvMassScale0(1.f)
			, InvMassScale1(1.f)
			, InvInertiaScale0(1.f)
			, InvInertiaScale1(1.f)
			, ShapesType(EContactShapesType::Unknown)
		{
			Implicit[0] = InImplicit0;
			Implicit[1] = InImplicit1;
			Simplicial[0] = InSimplicial0;
			Simplicial[1] = InSimplicial1;
		}

		bool bDisabled;

		FVec3 Normal;
		FVec3 Location;
		FReal Phi;

		FReal Friction;			// @todo(chaos): rename DynamicFriction
		FReal AngularFriction;	// @todo(chaos): rename StaticFriction
		FReal Restitution;
		FReal RestitutionPadding; // For PBD implementation of resitution, we pad constraints on initial contact to enforce outward velocity
		FReal RestitutionThreshold;
		FReal InvMassScale0;
		FReal InvMassScale1;
		FReal InvInertiaScale0;
		FReal InvInertiaScale1;
		EContactShapesType ShapesType;

		void Reset()
		{
			bDisabled = false;
			Phi = TNumericLimits<FReal>::Max();
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

		static const int32 MaxManifoldPoints = 4;

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
		 * @brief For use by the tri mesh and heighfield collision detection as a temporary measure
		 * @see FHeightField::ContactManifoldImp, FTriangleMeshImplicitObject::ContactManifoldImp
		*/
		static FPBDCollisionConstraint MakeTriangle(const FImplicitObject* Implicit0);

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
		const FReal GetCollisionMargin0() const { return CollisionMargins[0]; }
		const FReal GetCollisionMargin1() const { return CollisionMargins[1]; }
		const FReal GetCollisionTolerance() const { return CollisionTolerance; }

		const bool IsQuadratic0() const { return Flags.bIsQuadratic0; }
		const bool IsQuadratic1() const { return Flags.bIsQuadratic1; }
		const bool HasQuadraticShape() const { return (Flags.bIsQuadratic0 || Flags.bIsQuadratic1); }
		const FReal GetCollisionRadius0() const { return (Flags.bIsQuadratic0) ? CollisionMargins[0] : FReal(0); }
		const FReal GetCollisionRadius1() const { return (Flags.bIsQuadratic1) ? CollisionMargins[1] : FReal(0); }

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

		bool GetUseManifold() const { return Flags.bUseManifold; }
		
		bool GetUseIncrementalCollisionDetection() const { return !Flags.bUseManifold || Flags.bUseIncrementalManifold; }

		/**
		 * @brief Clear the current and previous manifolds
		*/
		void ResetManifold();

		// @todo(chaos): remove array view and provide per-point accessor
		TArrayView<FManifoldPoint> GetManifoldPoints() { return MakeArrayView(ManifoldPoints.begin(), ManifoldPoints.Num()); }
		TArrayView<const FManifoldPoint> GetManifoldPoints() const { return MakeArrayView(ManifoldPoints.begin(), ManifoldPoints.Num()); }

		void AddIncrementalManifoldContact(const FContactPoint& ContactPoint);
		void AddOneshotManifoldContact(const FContactPoint& ContactPoint);
		void UpdateManifoldContacts();

		const FRigidTransform3& GetShapeRelativeTransform0() const { return ImplicitTransform[0]; }
		const FRigidTransform3& GetShapeRelativeTransform1() const { return ImplicitTransform[1]; }

		const FRigidTransform3& GetShapeWorldTransform0() const { return ShapeWorldTransform0; }
		const FRigidTransform3& GetShapeWorldTransform1() const { return ShapeWorldTransform1; }

		void SetShapeWorldTransforms(const FRigidTransform3& InShapeWorldTransform0, const FRigidTransform3& InShapeWorldTransform1);
		void SetLastShapeWorldTransforms(const FRigidTransform3& InShapeWorldTransform0, const FRigidTransform3& InShapeWorldTransform1);

		bool UpdateAndTryRestoreManifold();
		void ResetActiveManifoldContacts();
		bool TryAddManifoldContact(const FContactPoint& ContactPoint);
		bool TryInsertManifoldContact(const FContactPoint& ContactPoint);

		//@ todo(chaos): These are for the collision forwarding system - this should use the collision modifier system (which should be extended to support adding collisions)
		void SetManifoldPoints(const TArray<FManifoldPoint>& InManifoldPoints)
		{ 
			ManifoldPoints.SetNum(FMath::Min(MaxManifoldPoints, InManifoldPoints.Num()));
			for (int32 ManifoldPointIndex = 0; ManifoldPoints.Num(); ++ManifoldPointIndex)
			{
				ManifoldPoints[ManifoldPointIndex] = InManifoldPoints[ManifoldPointIndex];
			}
		}
		void UpdateManifoldPointFromContact(const int32 ManifoldPointIndex);

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
		bool WasManifoldRestored() const { return Flags.bWasManifoldRestored; }

		/**
		 * @brief Restore the contact manifold (assumes relative motion of the two bodies is small)
		 * @see IsWithinManifoldRestorationThreshold
		*/
		void RestoreManifold(const bool bReproject);

		/**
		 * Determine the constraint direction based on Normal and Phi.
		 * This function assumes that the constraint is update-to-date.
		 */
		ECollisionConstraintDirection GetConstraintDirection(const FReal Dt) const;

		/**
		 * @brief Called before SetSolverResults() to reset accumulators
		*/
		void ResetSolverResults()
		{
			SavedManifoldPoints.Reset();
			AccumulatedImpulse = FVec3(0);
		}

		/**
		 * @brief Store the data from the solver that is retained between ticks for the specified manifold point or used by dependent systems (plasticity, breaking, etc.)
		*/
		void SetSolverResults(
			const int32 ManifoldPointIndex, 
			const FVec3& NetPushOut, 
			const FVec3& NetImpulse, 
			const FReal StaticFrictionRatio,
			const FReal Dt)
		{
			FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];

			ManifoldPoint.NetPushOut = NetPushOut;
			ManifoldPoint.NetImpulse = NetImpulse;
			ManifoldPoint.Flags.bInsideStaticFrictionCone = FMath::IsNearlyEqual(StaticFrictionRatio, FReal(1));

			AccumulatedImpulse += NetImpulse + (NetPushOut / Dt);

			// Save contact data for friction
			// NOTE: we do this even for points that did not apply PushOut or Impulse so that
			// we get previous contact data for initial contacts (sometimes). Otherwise we
			// end up having to estimate the previous contact from velocities
			if (!SavedManifoldPoints.IsFull())
			{
				const int32 SavedIndex = SavedManifoldPoints.Add();
				FSavedManifoldPoint& SavedManifoldPoint = SavedManifoldPoints[SavedIndex];

				if (FMath::IsNearlyEqual(StaticFrictionRatio, FReal(1)))
				{
					// Static friction held - we keep the same contacts points as-is for use next frame
					SavedManifoldPoint.ShapeContactPoints[0] = ManifoldPoint.ShapeAnchorPoints[0];
					SavedManifoldPoint.ShapeContactPoints[1] = ManifoldPoint.ShapeAnchorPoints[1];
				}
				else if (FMath::IsNearlyEqual(StaticFrictionRatio, FReal(0)))
				{
					// No friction - discard the friction anchors
					SavedManifoldPoint.ShapeContactPoints[0] = ManifoldPoint.ContactPoint.ShapeContactPoints[0];
					SavedManifoldPoint.ShapeContactPoints[1] = ManifoldPoint.ContactPoint.ShapeContactPoints[1];
				}
				else
				{
					// We exceeded the friction cone. Slide the friction anchor toward the last-detected contact position
					// so that it sits at the edge of the friction cone.
					const FReal Alpha = FMath::Clamp(StaticFrictionRatio, FReal(0), FReal(1));
					SavedManifoldPoint.ShapeContactPoints[0] = FVec3::Lerp(ManifoldPoint.ContactPoint.ShapeContactPoints[0], ManifoldPoint.ShapeAnchorPoints[0], Alpha);
					SavedManifoldPoint.ShapeContactPoints[1] = FVec3::Lerp(ManifoldPoint.ContactPoint.ShapeContactPoints[1], ManifoldPoint.ShapeAnchorPoints[1], Alpha);
				}
			}
		}

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
		int32 AddManifoldPoint(const FContactPoint& ContactPoint);
		void InitManifoldPoint(const int32 ManifoldPointIndex);
		void SetActiveContactPoint(const FContactPoint& ContactPoint);

		/**
		 * @brief Restore data used for static friction from the previous point(s) to the current
		*/
		void TryRestoreFrictionData(const int32 ManifoldPointIndex);

		void ReprojectManifoldContacts();
		void ReprojectManifoldPoint(const int32 ManifoldPointIndex);

		void InitMarginsAndTolerances(const EImplicitObjectType ImplicitType0, const EImplicitObjectType ImplicitType1, const FReal Margin0, const FReal Margin1);

		const FSavedManifoldPoint* FindSavedManifoldPoint(const FManifoldPoint& ManifoldPoint) const;
		FReal CalculateSavedManifoldPointScore(const FSavedManifoldPoint& SavedManifoldPoint, const FManifoldPoint& ManifoldPoint, const FReal DistanceToleranceSq) const;

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

		TCArray<FManifoldPoint, MaxManifoldPoints> ManifoldPoints;

		// The manifold points from the previous tick when we don't reuse the manifold. Used by static friction.
		TCArray<FSavedManifoldPoint, MaxManifoldPoints> SavedManifoldPoints;
		FReal CullDistance;

		// The margins to use during collision detection. We don't always use the margins on the shapes directly.
		// E.g., we use the smallest non-zero margin for 2 convex shapes. See InitMarginsAndTolerances
		FReal CollisionMargins[2];

		// The collision tolerance is used to determine whether a new contact matches an old on. It is derived from the
		// margins of the two shapes, as well as their types
		FReal CollisionTolerance;

		FReal FrictionPositionTolerance;

		union FFlags
		{
			FFlags() : Bits(0) {}
			struct
			{
				bool bUseManifold;
				bool bUseIncrementalManifold;
				bool bWasManifoldRestored;
				bool bIsQuadratic0;
				bool bIsQuadratic1;
			};
			uint32 Bits;
		} Flags;

		// These are only needed here while we still have the legacy solvers (not QuasiPBD)
		FSolverBody* SolverBodies[2];

		// Simplex data from the last call to GJK, used to warm-start GJK
		FGJKSimplexData GJKWarmStartData;

		FRigidTransform3 ShapeWorldTransform0;
		FRigidTransform3 ShapeWorldTransform1;
		FRigidTransform3 LastShapeWorldTransform0;
		FRigidTransform3 LastShapeWorldTransform1;
		int32 ExpectedNumManifoldPoints;
	};


	UE_DEPRECATED(4.27, "FCollisionConstraintBase has been removed and folded into FPBDCollisionConstraint. Use FPBDCollisionConstraint")
	typedef FPBDCollisionConstraint FCollisionConstraintBase;

	UE_DEPRECATED(4.27, "FRigidBodySweptPointContactConstraint has been removed and folded into FPBDCollisionConstraint. Use FPBDCollisionConstraint")
	typedef FPBDCollisionConstraint FRigidBodySweptPointContactConstraint;
}
