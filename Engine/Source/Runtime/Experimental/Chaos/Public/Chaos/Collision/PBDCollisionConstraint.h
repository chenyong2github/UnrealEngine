// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/Vector.h"
#include "Chaos/BVHParticles.h"

namespace Chaos
{
	class FImplicitObject;
	class FPBDCollisionConstraintHandle;

	// Data returned by the low-level collision functions
	class CHAOS_API FContactPoint
	{
	public:
		FVec3 ShapeContactPoints[2];	// Shape-space contact points on the two bodies
		FVec3 ShapeContactNormal;		// Shape-space contact normal relative to second body

		// @todo(chaos): these do not need to be stored here (they can be derived from above)
		FVec3 Location;					// World-space contact location
		FVec3 Normal;					// World-space contact normal
		FReal Phi;						// Contact separation (negative for overlap)

		FContactPoint()
			: Normal(1, 0, 0)
			, Phi(TNumericLimits<FReal>::Max()) {}

		bool IsSet() const { return (Phi != TNumericLimits<FReal>::Max()); }
	};

	class CHAOS_API FManifoldPoint
	{
	public:
		FManifoldPoint() 
			: CoMContactPoints{ FVec3(0), FVec3(0) }
			, CoMContactNormal(0)
			, PrevCoMContactPoint1(0)
			, NetImpulse(0)
			, NetPushOut(0)
			, NetPushOutImpulse(0)
			, InitialContactVelocity(0)
			, InitialPhi(0)
			, bPotentialRestingContact(false)
			, bInsideStaticFrictionCone(false)
			, bRestitutionEnabled(false)
			, bActive(false)
			{}

		FManifoldPoint(const FContactPoint& InContactPoint) 
			: ContactPoint(InContactPoint)
			, CoMContactPoints{ FVec3(0), FVec3(0) }
			, CoMContactNormal(0)
			, PrevCoMContactPoint1(0)
			, NetImpulse(0)
			, NetPushOut(0)
			, NetPushOutImpulse(0)
			, InitialContactVelocity(0)
			, InitialPhi(0)
			, bPotentialRestingContact(false)
			, bInsideStaticFrictionCone(false)
			, bRestitutionEnabled(false)
			, bActive(false)
		{}

		FContactPoint ContactPoint;			// Shape-space data from low-level collision detection
		FVec3 CoMContactPoints[2];			// CoM-space contact points on the two bodies
		FVec3 CoMContactNormal;				// CoM-space contact normal relative to second body
		FVec3 PrevCoMContactPoint1;			// CoM-space contact point on second body at previous transforms (used for static friction)
		FVec3 NetImpulse;					// Total impulse applied by this contact point
		FVec3 NetPushOut;					// Total pushout applied at this contact point
		FReal NetPushOutImpulse;			// Total pushout impulse along normal (for final velocity correction) applied at this contact point
		FReal InitialContactVelocity;		// Contact velocity at start of frame (used for restitution)
		FReal InitialPhi;					// Contact separation at first contact (used for pushout restitution)
		bool bPotentialRestingContact;		// Whether this may be a resting contact (used for static fricton)
		bool bInsideStaticFrictionCone;		// Whether we are inside the static friction cone (used in PushOut)
		bool bRestitutionEnabled;			// Whether restitution was added in the apply step (used in PushOut)
		bool bActive;						// Whether this contact applied an impulse
	};

	/*
	*
	*/
	class CHAOS_API FCollisionContact
	{
	public:
		FCollisionContact(const FImplicitObject* InImplicit0 = nullptr, const TBVHParticles<FReal, 3>* InSimplicial0 = nullptr, const FImplicitObject* InImplicit1 = nullptr, const TBVHParticles<FReal, 3>* InSimplicial1 = nullptr)
			: bDisabled(false)
			, bUseAccumalatedImpulseInSolve(true)
			, Normal(0)
			, Location(0)
			, Phi(FLT_MAX)
			, Friction(0)
			, AngularFriction(0)
			, Restitution(0)
			, RestitutionPadding(0)
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
		FReal InvInertiaScale0;
		FReal InvInertiaScale1;
		FReal ContactMoveSQRDistance; // How much the contact position moved after the last update
		EContactShapesType ShapesType;


		FString ToString() const
		{
			return FString::Printf(TEXT("Location:%s, Normal:%s, Phi:%f"), *Location.ToString(), *Normal.ToString(), Phi);
		}

		const FImplicitObject* Implicit[2]; // {Of Particle[0], Of Particle[1]}
		const TBVHParticles<FReal, 3>* Simplicial[2]; // {Of Particle[0], Of Particle[1]}
	};

	/*
	*
	*/
	class CHAOS_API FCollisionConstraintBase
	{
	public:
		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, 3>;

		enum class FType
		{
			None = 0,			// default value also indicates a invalid constraint
			SinglePoint,		// TRigidBodyPointContactConstraint
			SinglePointSwept,	// TRigidBodySweptPointContactConstraint
			MultiPoint			// TRigidBodyMultiPointContactConstraint
		};

		FCollisionConstraintBase(FType InType = FType::None)
			: AccumulatedImpulse(0)
			, Timestamp(-INT_MAX)
			, ConstraintHandle(nullptr)
			, Type(InType)
		{
			ImplicitTransform[0] = FRigidTransform3::Identity; ImplicitTransform[1] = FRigidTransform3::Identity;
			Manifold.Implicit[0] = nullptr; Manifold.Implicit[1] = nullptr;
			Manifold.Simplicial[0] = nullptr; Manifold.Simplicial[1] = nullptr;
			Particle[0] = nullptr; Particle[1] = nullptr;
		}

		FCollisionConstraintBase(
			FGeometryParticleHandle* Particle0,
			const FImplicitObject* Implicit0,
			const TBVHParticles<FReal, 3>* Simplicial0,
			const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1,
			const FImplicitObject* Implicit1,
			const TBVHParticles<FReal, 3>* Simplicial1,
			const FRigidTransform3& Transform1,
			const FType InType, 
			const EContactShapesType ShapesType, 
			const int32 InTimestamp = -INT_MAX)
			: AccumulatedImpulse(0)
			, Timestamp(InTimestamp)
			, ConstraintHandle(nullptr)
			, Type(InType)
		{
			ImplicitTransform[0] = Transform0; 
			ImplicitTransform[1] = Transform1;

			Manifold.Implicit[0] = Implicit0; 
			Manifold.Implicit[1] = Implicit1;

			Manifold.Simplicial[0] = Simplicial0; 
			Manifold.Simplicial[1] = Simplicial1;

			Manifold.ShapesType = ShapesType;

			Particle[0] = Particle0; 
			Particle[1] = Particle1;
		}

		FType GetType() const { return Type; }

		template<class AS_T> AS_T* As() { return static_cast<AS_T*>(this); }
		template<class AS_T> const AS_T* As() const { return static_cast<const AS_T*>(this); }

		bool ContainsManifold(const FImplicitObject* A, const TBVHParticles<FReal, 3>* AS, const FImplicitObject* B, const TBVHParticles<FReal, 3>* BS) const
		{
			return A == Manifold.Implicit[0] && B == Manifold.Implicit[1] && AS == Manifold.Simplicial[0] && BS == Manifold.Simplicial[1];
		}

		void SetManifold(const FImplicitObject* A, const TBVHParticles<FReal, 3>* AS, const FImplicitObject* B, const TBVHParticles<FReal, 3>* BS)
		{
			Manifold.Implicit[0] = A; Manifold.Implicit[1] = B;
			Manifold.Simplicial[0] = AS; Manifold.Simplicial[1] = BS;
		}

		const FCollisionContact& GetManifold() const { return Manifold; }

		//
		// API
		//

		void ResetPhi(FReal InPhi) { SetPhi(InPhi); }

		void SetPhi(FReal InPhi) { Manifold.Phi = InPhi; }
		FReal GetPhi() const { return Manifold.Phi; }

		void SetDisabled(bool bInDisabled) { Manifold.bDisabled = bInDisabled; }
		bool GetDisabled() const { return Manifold.bDisabled; }

		void SetNormal(const FVec3& InNormal) { Manifold.Normal = InNormal; }
		FVec3 GetNormal() const { return Manifold.Normal; }

		void SetLocation(const FVec3& InLocation) { Manifold.Location = InLocation; }
		FVec3 GetLocation() const { return Manifold.Location; }

		void SetInvInertiaScale0(const FReal InInvInertiaScale) { Manifold.InvInertiaScale0 = InInvInertiaScale; }
		FReal GetInvInertiaScale0() const { return Manifold.InvInertiaScale0; }

		void SetInvInertiaScale1(const FReal InInvInertiaScale) { Manifold.InvInertiaScale1 = InInvInertiaScale; }
		FReal GetInvInertiaScale1() const { return Manifold.InvInertiaScale1; }


		FString ToString() const;

		FRigidTransform3 ImplicitTransform[2];		// Local-space transforms of the shape (relative to particle)
		FGeometryParticleHandle* Particle[2];
		FVec3 AccumulatedImpulse;					// @todo(chaos): we need to accumulate angular impulse separately
		FCollisionContact Manifold					;// @todo(chaos): rename
		int32 Timestamp;
		
		void SetConstraintHandle(FPBDCollisionConstraintHandle* InHandle) { ConstraintHandle = InHandle; }
		FPBDCollisionConstraintHandle* GetConstraintHandle() const { return ConstraintHandle; }

		bool operator<(const FCollisionConstraintBase& Other) const;

	private:
		FPBDCollisionConstraintHandle* ConstraintHandle;
		FType Type;
	};


	/*
	*
	*/
	class CHAOS_API FRigidBodyPointContactConstraint : public FCollisionConstraintBase
	{
	public:
		using Base = FCollisionConstraintBase;
		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, 3>;

		FRigidBodyPointContactConstraint() 
			: Base(Base::FType::SinglePoint)
			, bUseIncrementalManifold(false)
			, bUseOneShotManifold(false)
		{}
		FRigidBodyPointContactConstraint(
			FGeometryParticleHandle* Particle0, 
			const FImplicitObject* Implicit0, 
			const TBVHParticles<FReal, 3>* Simplicial0, 
			const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, 
			const FImplicitObject* Implicit1, 
			const TBVHParticles<FReal, 3>* Simplicial1, 
			const FRigidTransform3& Transform1,
			const EContactShapesType ShapesType,
			const bool bInUseIncrementalManifold,
			const bool bInUseOneshotManifolds)
			: Base(Particle0, Implicit0, Simplicial0, Transform0, Particle1, Implicit1, Simplicial0, Transform1, Base::FType::SinglePoint, ShapesType)
			, bUseIncrementalManifold(bInUseIncrementalManifold)
			, bUseOneShotManifold(bInUseOneshotManifolds)
		{}

		static typename Base::FType StaticType() { return Base::FType::SinglePoint; };

		bool UseIncrementalManifold() const { return bUseIncrementalManifold; }
		bool UseOneShotManifold() const { return bUseOneShotManifold; }		
		void SetUseOneShotManifold(bool bInUseOneShotManifold) { bUseOneShotManifold = bInUseOneShotManifold; };

		TArrayView<FManifoldPoint> GetManifoldPoints() { return MakeArrayView(ManifoldPoints); }
		TArrayView<const FManifoldPoint> GetManifoldPoints() const { return MakeArrayView(ManifoldPoints); }
		FManifoldPoint& SetActiveManifoldPoint(
			int32 ManifoldPointIndex,
			const FVec3& P0,
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1);

		void AddOneshotManifoldContact(const FContactPoint& ContactPoint, bool bInInitialize = true);
		void UpdateOneShotManifoldContacts();
		void UpdateManifold(const FContactPoint& ContactPoint);
		void ClearManifold();

	protected:
		// For use by derived types that can be used as point constraints in Update
		FRigidBodyPointContactConstraint(typename Base::FType InType) : Base(InType) {}

		FRigidBodyPointContactConstraint(
			FGeometryParticleHandle* Particle0, 
			const FImplicitObject* Implicit0, 
			const TBVHParticles<FReal, 3>* Simplicial0, 
			const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, 
			const FImplicitObject* Implicit1, 
			const TBVHParticles<FReal, 3>* Simplicial1,
			const FRigidTransform3& Transform1,
			typename Base::FType InType, 
			const EContactShapesType ShapesType)
			: Base(Particle0, Implicit0, Simplicial0, Transform0, Particle1, Implicit1, Simplicial1, Transform1, InType, ShapesType)
			, bUseIncrementalManifold(false)
			, bUseOneShotManifold(false)
		{}

		bool AreMatchingContactPoints(const FContactPoint& A, const FContactPoint& B, FReal& OutScore) const;
		int32 FindManifoldPoint(const FContactPoint& ContactPoint) const;
		int32 AddManifoldPoint(const FContactPoint& ContactPoint, bool bInInitialize = true);
		void InitManifoldPoint(FManifoldPoint& ManifoldPoint);
		void UpdateManifoldPoint(int32 ManifoldPointIndex, const FContactPoint& ContactPoint);
		void SetActiveContactPoint(const FContactPoint& ContactPoint);

		// @todo(chaos): inline array
		TArray<FManifoldPoint> ManifoldPoints;
		bool bUseIncrementalManifold;
		bool bUseOneShotManifold;
	};


	/*
	 * Holds information about a one-shot contact manifold. The Contact Manifold consists of a plane
	 * attached to one particle, and a set of points on the other. Once the manifold is created, collision
	 * detection between the two particles is reduced to finding the point that most deeply penetrates
	 * the plane.
	 *
	 * Note that Particles are moved as part of constraint solving (collision, joints, etc.) so a manifold
	 * can become increasingly inaccurate as  adjustments are made. How tolerant the manifold is to
	 * particle motion depends on the nature of the shapes it represents. I.e., how much would we have to
	 * rotate or translate the shapes such that the feature-selection algorithm in the manifold creation 
	 * would have selected different features.
	 */
	class CHAOS_API FRigidBodyMultiPointContactConstraint : public FRigidBodyPointContactConstraint
	{
	public:
		using Base = FRigidBodyPointContactConstraint;
		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, 3>;

		FRigidBodyMultiPointContactConstraint() : Base(FCollisionConstraintBase::FType::MultiPoint), PlaneOwnerIndex(INDEX_NONE), PlaneFaceIndex(INDEX_NONE), PlaneNormal(0), PlanePosition(0) {}
		FRigidBodyMultiPointContactConstraint(
			FGeometryParticleHandle* Particle0, 
			const FImplicitObject* Implicit0, 
			const TBVHParticles<FReal, 3>* Simplicial0, 
			const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, 
			const FImplicitObject* Implicit1, 
			const TBVHParticles<FReal, 3>* Simplicial1, 
			const FRigidTransform3& Transform1,
			const EContactShapesType ShapesType)
			: Base(Particle0, Implicit0, Simplicial0, Transform0, Particle1, Implicit1, Simplicial1, Transform1, Base::FType::MultiPoint, ShapesType)
			, PlaneOwnerIndex(INDEX_NONE), PlaneFaceIndex(INDEX_NONE), PlaneNormal(0), PlanePosition(0)
			, bUseManifoldTolerance(false)
		{
		}

		static typename Base::FType StaticType() { return Base::FType::MultiPoint; };

		// Put the manifold into the "created" state, although it still has no plane or points so IsValidManifold will be false.
		// We require this state to support just-in-time manifold creation during the first call to Apply collisions, as opposed 
		// to up-front manifold creation in the collision detection phase.
		// After this, assuming a valid manifold can be found, there should be calls to SetManifoldPlane and AddManifoldPoint(s).
		FORCEINLINE void InitManifold()
		{
			PlaneOwnerIndex = 0;
			PlaneFaceIndex = INDEX_NONE;
			bUseManifoldTolerance = false;
			ResetManifoldPoints();
		}

		// Calling this enables the tolerance system to test whether a manifold has been invalidated when the particles that
		// own the manifold plane and points move. If not called, the IsManifoldWithinTolerance() always returns true. Once called,
		// the manifold is invalidated if the relative position and rotation of the particles changes by an amount that exceeds the tolerance.
		void InitManifoldTolerance(const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1, const FReal InPositionTolerance, const FReal InRotationTolerance);

		// Whether we attempted to create the Manifold (it might not be usable though - see IsValidManifold())
		FORCEINLINE bool IsManifoldCreated() const
		{
			return PlaneOwnerIndex != INDEX_NONE;
		}

		// Whether the manifold has a usable plane and set of points
		// If we were unable to select a decent manifold, we should fall back to geometry-based collision detection
		FORCEINLINE bool IsManifoldValid() const
		{
			return IsManifoldCreated() && (Points.Num() > 0);
		}

		// Whether the current relative particle positions have changed by an amount that is within acceptable tolerance for this manifold
		FORCEINLINE bool IsManifoldWithinTolerance(const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1)
		{
			if (IsManifoldCreated() && IsManifoldValid() && bUseManifoldTolerance)
			{
				return IsManifoldWithinToleranceImpl(ParticleTransform0, ParticleTransform1);
			}
			return true;
		}

		// Get the particle that owns the plane
		FORCEINLINE FGeometryParticleHandle* PlaneParticleHandle() const { check(PlaneOwnerIndex >= 0 && PlaneOwnerIndex < 2); return Particle[PlaneOwnerIndex]; }

		// Get the particle that owns the manifold sample points
		FORCEINLINE FGeometryParticleHandle* PointsParticleHandle() const { check(PlaneOwnerIndex >= 0 && PlaneOwnerIndex < 2); return Particle[1 - PlaneOwnerIndex]; }

		FORCEINLINE int32 GetManifoldPlaneOwnerIndex() const { return PlaneOwnerIndex; }
		FORCEINLINE int32 GetManifoldPlaneFaceIndex() const { return PlaneFaceIndex; }
		FORCEINLINE const FVec3& GetManifoldPlaneNormal() const { return PlaneNormal; }
		FORCEINLINE const FVec3& GetManifoldPlanePosition() const { return PlanePosition; }
		FORCEINLINE void SetManifoldPlane(int32 OwnerIndex, int32 FaceIndex, const FVec3& Normal, const FVec3& Pos)
		{
			PlaneOwnerIndex = OwnerIndex;
			PlaneFaceIndex = FaceIndex;
			PlaneNormal = Normal;
			PlanePosition = Pos;
		}

		FORCEINLINE int32 NumManifoldPoints() const { return Points.Num(); }
		FORCEINLINE const FVec3& GetManifoldPoint(int32 Index) const { check(0 <= Index && Index < NumManifoldPoints()); return Points[Index]; }
		FORCEINLINE void SetManifoldPoint(int32 Index, const FVec3& Point) { check(0 <= Index && Index < NumManifoldPoints()); Points[Index] = Point; }
		FORCEINLINE void AddManifoldPoint(const FVec3& Point) { Points.Add(Point); };
		FORCEINLINE void ResetManifoldPoints(int32 NewSize = 0) { Points.Reset(NewSize); }

	private:
		bool IsManifoldWithinToleranceImpl(const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1);

		// Manifold plane data
		int PlaneOwnerIndex; // index of particle which owns the plane. The other owns the sample positions
		int PlaneFaceIndex; // index of face used as the manifold plane on plane-owner body
		FVec3 PlaneNormal; // local space contact normal on plane-owner
		FVec3 PlanePosition; // local space surface position on plane-owner

		// Manifold point data
		static const int32 MaxPoints = 4;
		TArray<FVec3, TInlineAllocator<MaxPoints>> Points; // @todo(chaos): use TFixedAllocator when we handle building the best manifold properly

		// Manifold tolerance data
		FVec3 InitialPositionSeparation;
		FVec3 InitialRotationSeparation;
		FReal PositionToleranceSq;
		FReal RotationToleranceSq;
		bool bUseManifoldTolerance;
	};

	/*
	*
	*/
	class CHAOS_API FRigidBodySweptPointContactConstraint : public FRigidBodyPointContactConstraint
	{
	public:
		using Base = FRigidBodyPointContactConstraint;
		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, 3>;

		FRigidBodySweptPointContactConstraint() : Base(Base::FType::SinglePointSwept) {}
		FRigidBodySweptPointContactConstraint(
			FGeometryParticleHandle* Particle0, 
			const FImplicitObject* Implicit0, 
			const TBVHParticles<FReal, 3>* Simplicial0, 
			const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, 
			const FImplicitObject* Implicit1, 
			const TBVHParticles<FReal, 3>* Simplicial1, 
			const FRigidTransform3& Transform1,
			EContactShapesType ShapesType)
			: Base(Particle0, Implicit0, Simplicial0, Transform0, Particle1, Implicit1, Simplicial1, Transform1, Base::FType::SinglePointSwept, ShapesType)
			, TimeOfImpact(0)
		    , bShouldTreatAsSinglePoint(false)
		{}

		// Value in range [0,1] used to interpolate P between [X,P] that we will rollback to when solving at time of impact.
		FReal TimeOfImpact;
		bool bShouldTreatAsSinglePoint;
		static typename Base::FType StaticType() { return Base::FType::SinglePointSwept; };
	};


	struct CHAOS_API FCollisionConstraintsArray
	{
		// Previous arrays using TInlineAllocator<8> changed to normal arrays. Profiling showed faster without and
		// we are able to use a per particle async reciever cache and just MoveTemp all the constraints for a 
		// speed improvement to collision detection/gather.
		TArray<FRigidBodyPointContactConstraint> SinglePointConstraints;
		TArray<FRigidBodySweptPointContactConstraint> SinglePointSweptConstraints;
		TArray<FRigidBodyMultiPointContactConstraint> MultiPointConstraints;

		int32 Num() const { return SinglePointConstraints.Num() + SinglePointSweptConstraints.Num() + MultiPointConstraints.Num(); }

		void Reset()
		{
			SinglePointConstraints.Reset();
			SinglePointSweptConstraints.Reset();
			MultiPointConstraints.Reset();
		}

		void Empty()
		{
			SinglePointConstraints.Empty();
			SinglePointSweptConstraints.Empty();
			MultiPointConstraints.Empty();
		}


		FRigidBodyPointContactConstraint* Add(const FRigidBodyPointContactConstraint& C)
		{
			int32 ConstraintIndex = SinglePointConstraints.Add(C);
			return &SinglePointConstraints[ConstraintIndex];
		}

		FRigidBodySweptPointContactConstraint* Add(const FRigidBodySweptPointContactConstraint& C)
		{
			int32 ConstraintIndex = SinglePointSweptConstraints.Add(C);
			return &SinglePointSweptConstraints[ConstraintIndex];
		}

		FRigidBodyMultiPointContactConstraint* Add(const FRigidBodyMultiPointContactConstraint& C)
		{
			int32 ConstraintIndex = MultiPointConstraints.Add(C);
			return &MultiPointConstraints[ConstraintIndex];
		}


		FRigidBodyPointContactConstraint* TryAdd(FReal MaxPhi, const FRigidBodyPointContactConstraint& C)
		{
			if (C.GetPhi() < MaxPhi)
			{
				int32 ConstraintIndex = SinglePointConstraints.Add(C);
				return &SinglePointConstraints[ConstraintIndex];
			}
			return nullptr;
		}

		FRigidBodySweptPointContactConstraint* TryAdd(FReal MaxPhi, const FRigidBodySweptPointContactConstraint& C)
		{
			if (C.GetPhi() < MaxPhi)
			{
				int32 ConstraintIndex = SinglePointSweptConstraints.Add(C);
				return &SinglePointSweptConstraints[ConstraintIndex];
			}
			return nullptr;
		}

		FRigidBodyMultiPointContactConstraint* TryAdd(FReal MaxPhi, const FRigidBodyMultiPointContactConstraint& C)
		{
			if (C.GetPhi() < MaxPhi)
			{
				int32 ConstraintIndex = MultiPointConstraints.Add(C);
				return &MultiPointConstraints[ConstraintIndex];
			}
			return nullptr;
		}
	};
}
