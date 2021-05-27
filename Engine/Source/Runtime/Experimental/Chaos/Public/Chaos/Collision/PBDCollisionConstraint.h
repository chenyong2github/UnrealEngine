// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/ContactPoint.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/Vector.h"
#include "Chaos/BVHParticles.h"

namespace Chaos
{
	class FImplicitObject;
	class FPBDCollisionConstraintHandle;
	class FConstGenericParticleHandle;

	class CHAOS_API FManifoldPoint
	{
	public:
		FManifoldPoint() 
			: CoMContactPoints{ FVec3(0), FVec3(0) }
			, CoMContactNormal(0)
			, CoMContactTangent(0)
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
			{}

		FManifoldPoint(const FContactPoint& InContactPoint) 
			: ContactPoint(InContactPoint)
			, CoMContactPoints{ FVec3(0), FVec3(0) }
			, CoMContactNormal(0)
			, CoMContactTangent(0)
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
		{}

		// @todo(chaos): Normal and plane owner should be per manifold, not per manifold-point when we are not using incremental manifolds any more
		FContactPoint ContactPoint;			// Shape-space data from low-level collision detection
		FVec3 CoMContactPoints[2];			// CoM-space contact points on the two bodies core shapes (not including margin)
		FVec3 CoMContactNormal;				// CoM-space contact normal relative to ContactNormalOwner body	
		FVec3 CoMContactTangent;			// CoM-space contact tangent for friction
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
		const FBVHParticles* Simplicial[2]; // {Of Particle[0], Of Particle[1]}
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
			const FBVHParticles* Simplicial0,
			const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1,
			const FImplicitObject* Implicit1,
			const FBVHParticles* Simplicial1,
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

		bool ContainsManifold(const FImplicitObject* A, const FBVHParticles* AS, const FImplicitObject* B, const FBVHParticles* BS) const
		{
			return A == Manifold.Implicit[0] && B == Manifold.Implicit[1] && AS == Manifold.Simplicial[0] && BS == Manifold.Simplicial[1];
		}

		void SetManifold(const FImplicitObject* A, const FBVHParticles* AS, const FImplicitObject* B, const FBVHParticles* BS)
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
			, CullDistance(FLT_MAX)
			, bUseManifold(false)
		{}

		FRigidBodyPointContactConstraint(
			FGeometryParticleHandle* Particle0, 
			const FImplicitObject* Implicit0, 
			const FBVHParticles* Simplicial0, 
			const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, 
			const FImplicitObject* Implicit1, 
			const FBVHParticles* Simplicial1, 
			const FRigidTransform3& Transform1,
			const FReal InCullDistance,
			const EContactShapesType ShapesType,
			const bool bInUseManifold)
			: Base(Particle0, Implicit0, Simplicial0, Transform0, Particle1, Implicit1, Simplicial0, Transform1, Base::FType::SinglePoint, ShapesType)
			, CullDistance(InCullDistance)
			, bUseManifold(false)
		{
			bUseManifold = bInUseManifold && CanUseManifold(Particle0, Particle1);
		}

		static typename Base::FType StaticType() { return Base::FType::SinglePoint; };

		FReal GetCullDistance() const { return CullDistance; }
		void SetCullDistance(FReal InCullDistance) { CullDistance = InCullDistance; }

		bool GetUseManifold() const { return bUseManifold; }

		TArrayView<FManifoldPoint> GetManifoldPoints() { return MakeArrayView(ManifoldPoints); }
		TArrayView<const FManifoldPoint> GetManifoldPoints() const { return MakeArrayView(ManifoldPoints); }
		FManifoldPoint& SetActiveManifoldPoint(
			int32 ManifoldPointIndex,
			const FVec3& P0,
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1);
		void CalculatePrevCoMContactPoints(
			const FConstGenericParticleHandle Particle0,
			const FConstGenericParticleHandle Particle1,
			FManifoldPoint& ManifoldPoint,
			FReal Dt,
			FVec3& OutPrevCoMContactPoint0,
			FVec3& OutPrevCoMContactPoint1) const;

		void AddIncrementalManifoldContact(const FContactPoint& ContactPoint, const FReal Dt);
		void AddOneshotManifoldContact(const FContactPoint& ContactPoint, const FReal Dt);
		void UpdateManifoldContacts(FReal Dt);
		void ClearManifold();

	protected:
		// For use by derived types that can be used as point constraints in Update
		FRigidBodyPointContactConstraint(typename Base::FType InType) : Base(InType) {}

		FRigidBodyPointContactConstraint(
			FGeometryParticleHandle* Particle0, 
			const FImplicitObject* Implicit0, 
			const FBVHParticles* Simplicial0, 
			const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, 
			const FImplicitObject* Implicit1, 
			const FBVHParticles* Simplicial1,
			const FRigidTransform3& Transform1,
			const FReal InCullDistance,
			typename Base::FType InType,
			const EContactShapesType ShapesType)
			: Base(Particle0, Implicit0, Simplicial0, Transform0, Particle1, Implicit1, Simplicial1, Transform1, InType, ShapesType)
			, CullDistance(InCullDistance)
			, bUseManifold(false)
		{}

		// Whether we can use manifolds for the given partices. Manifolds do not work well with Joints and PBD
		// because the bodies may be moved (and especially rotated) a lot in the solver and this can make the manifold extremely inaccurate
		bool CanUseManifold(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1) const;

		bool AreMatchingContactPoints(const FContactPoint& A, const FContactPoint& B, FReal& OutScore) const;
		int32 FindManifoldPoint(const FContactPoint& ContactPoint) const;
		int32 AddManifoldPoint(const FContactPoint& ContactPoint, const FReal Dt);
		void InitManifoldPoint(FManifoldPoint& ManifoldPoint, FReal Dt);
		void UpdateManifoldPoint(int32 ManifoldPointIndex, const FContactPoint& ContactPoint, const FReal Dt);
		void UpdateManifoldPointFromContact(FManifoldPoint& ManifoldPoint);
		void SetActiveContactPoint(const FContactPoint& ContactPoint);
		void GetWorldSpaceManifoldPoint(const FManifoldPoint& ManifoldPoint, const FVec3& P0, const FRotation3& Q0, const FVec3& P1, const FRotation3& Q1, FVec3& OutContactLocation, FVec3& OutContactNormal, FReal& OutContactPhi);

		// @todo(chaos): Switching this to inline allocator does not help, but maybe a physics scratchpad would
		//TArray<FManifoldPoint, TInlineAllocator<4>> ManifoldPoints;
		TArray<FManifoldPoint> ManifoldPoints;
		FReal CullDistance;
		bool bUseManifold;
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
			const FBVHParticles* Simplicial0, 
			const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, 
			const FImplicitObject* Implicit1, 
			const FBVHParticles* Simplicial1, 
			const FRigidTransform3& Transform1,
			const FReal InCullDistance,
			EContactShapesType ShapesType)
			: Base(Particle0, Implicit0, Simplicial0, Transform0, Particle1, Implicit1, Simplicial1, Transform1, InCullDistance, Base::FType::SinglePointSwept, ShapesType)
			, TimeOfImpact(0)
		{}

		// Value in range [0,1] used to interpolate P between [X,P] that we will rollback to when solving at time of impact.
		FReal TimeOfImpact;
		static typename Base::FType StaticType() { return Base::FType::SinglePointSwept; };
	};


	struct CHAOS_API FCollisionConstraintsArray
	{
		// Previous arrays using TInlineAllocator<8> changed to normal arrays. Profiling showed faster without and
		// we are able to use a per particle async reciever cache and just MoveTemp all the constraints for a 
		// speed improvement to collision detection/gather.
		TArray<FRigidBodyPointContactConstraint> SinglePointConstraints;
		TArray<FRigidBodySweptPointContactConstraint> SinglePointSweptConstraints;

		int32 Num() const { return SinglePointConstraints.Num() + SinglePointSweptConstraints.Num(); }

		void Reset()
		{
			SinglePointConstraints.Reset();
			SinglePointSweptConstraints.Reset();
		}

		void Empty()
		{
			SinglePointConstraints.Empty();
			SinglePointSweptConstraints.Empty();
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
	};
}
