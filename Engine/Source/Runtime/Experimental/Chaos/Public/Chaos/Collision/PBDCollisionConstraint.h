// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/Vector.h"

namespace Chaos
{
	class FImplicitObject;
	class FPBDCollisionConstraintHandle;

	/*
	*
	*/
	class CHAOS_API FCollisionContact
	{
	public:
		FCollisionContact(const FImplicitObject* InImplicit0 = nullptr, const FImplicitObject* InImplicit1 = nullptr)
			: bDisabled(false), Normal(0), Location(0), Phi(FLT_MAX), Friction(0), AngularFriction(0), Restitution(0), InvInertiaScale0(1.f), InvInertiaScale1(1.f), ShapesType(EContactShapesType::Unknown)
		{
			Implicit[0] = InImplicit0;
			Implicit[1] = InImplicit1;
		}

		bool bDisabled;
		FVec3 Normal;
		FVec3 Location;
		FReal Phi;

		FReal Friction;
		FReal AngularFriction;
		FReal Restitution;
		FReal InvInertiaScale0;
		FReal InvInertiaScale1;
		EContactShapesType ShapesType;


		FString ToString() const
		{
			return FString::Printf(TEXT("Location:%s, Normal:%s, Phi:%f"), *Location.ToString(), *Normal.ToString(), Phi);
		}

		const FImplicitObject* Implicit[2]; // {Of Particle[0], Of Particle[1]}
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
			Particle[0] = nullptr; Particle[1] = nullptr;
		}

		FCollisionConstraintBase(
			FGeometryParticleHandle* Particle0, const FImplicitObject* Implicit0, const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, const FImplicitObject* Implicit1, const FRigidTransform3& Transform1,
			FType InType, EContactShapesType ShapesType, int32 InTimestamp = -INT_MAX)
			: AccumulatedImpulse(0)
			, Timestamp(InTimestamp)
			, ConstraintHandle(nullptr)
			, Type(InType)
		{
			ImplicitTransform[0] = Transform0; ImplicitTransform[1] = Transform1;
			Manifold.Implicit[0] = Implicit0; Manifold.Implicit[1] = Implicit1;
			Manifold.ShapesType = ShapesType;
			Particle[0] = Particle0; Particle[1] = Particle1;
		}

		FType GetType() const { return Type; }

		template<class AS_T> AS_T* As() { return static_cast<AS_T*>(this); }
		template<class AS_T> const AS_T* As() const { return static_cast<const AS_T*>(this); }

		bool ContainsManifold(const FImplicitObject* A, const FImplicitObject* B) const { return A == Manifold.Implicit[0] && B == Manifold.Implicit[1]; }
		void SetManifold(const FImplicitObject* A, const FImplicitObject* B) { Manifold.Implicit[0] = A; Manifold.Implicit[1] = B; }
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

		FRigidTransform3 ImplicitTransform[2]; // { Point, Volume }
		FGeometryParticleHandle* Particle[2]; // { Point, Volume }
		FVec3 AccumulatedImpulse;
		FCollisionContact Manifold;// @todo(chaos): rename
		int32 Timestamp;
		FPBDCollisionConstraintHandle* ConstraintHandle;

	private:

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

		FRigidBodyPointContactConstraint() : Base(Base::FType::SinglePoint) {}
		FRigidBodyPointContactConstraint(
			FGeometryParticleHandle* Particle0, const FImplicitObject* Implicit0, const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, const FImplicitObject* Implicit1, const FRigidTransform3& Transform1,
			EContactShapesType ShapesType)
			: Base(Particle0, Implicit0, Transform0, Particle1, Implicit1, Transform1, Base::FType::SinglePoint, ShapesType) {}

		static typename Base::FType StaticType() { return Base::FType::SinglePoint; };

	protected:
		// For use by derived types that can be used as point constraints in Update
		FRigidBodyPointContactConstraint(typename Base::FType InType) : Base(InType) {}
		FRigidBodyPointContactConstraint(
			FGeometryParticleHandle* Particle0, const FImplicitObject* Implicit0, const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, const FImplicitObject* Implicit1, const FRigidTransform3& Transform1,
			typename Base::FType InType, EContactShapesType ShapesType)
			: Base(Particle0, Implicit0, Transform0, Particle1, Implicit1, Transform1, InType, ShapesType) {}
	};


	/*
	*
	*/
	class CHAOS_API FRigidBodyMultiPointContactConstraint : public FRigidBodyPointContactConstraint
	{
	public:
		using Base = FRigidBodyPointContactConstraint;
		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, 3>;

		FRigidBodyMultiPointContactConstraint() : Base(FCollisionConstraintBase::FType::MultiPoint), PlaneOwnerIndex(INDEX_NONE), PlaneFaceIndex(INDEX_NONE), PlaneNormal(0), PlanePosition(0) {}
		FRigidBodyMultiPointContactConstraint(
			FGeometryParticleHandle* Particle0, const FImplicitObject* Implicit0, const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, const FImplicitObject* Implicit1, const FRigidTransform3& Transform1,
			EContactShapesType ShapesType)
			: Base(Particle0, Implicit0, Transform0, Particle1, Implicit1, Transform1, Base::FType::MultiPoint, ShapesType)
			, PlaneOwnerIndex(INDEX_NONE), PlaneFaceIndex(INDEX_NONE), PlaneNormal(0), PlanePosition(0)
		{}

		static typename Base::FType StaticType() { return Base::FType::MultiPoint; };

		// Get the particle that owns the plane
		FGeometryParticleHandle* PlaneParticleHandle() const { check(PlaneOwnerIndex >= 0 && PlaneOwnerIndex < 2); return Particle[PlaneOwnerIndex]; }

		// Get the particle that owns the manifold sample points
		FGeometryParticleHandle* PointsParticleHandle() const { check(PlaneOwnerIndex >= 0 && PlaneOwnerIndex < 2); return Particle[1 - PlaneOwnerIndex]; }

		int32 GetManifoldPlaneOwnerIndex() const { return PlaneOwnerIndex; }
		int32 GetManifoldPlaneFaceIndex() const { return PlaneFaceIndex; }
		const FVec3& GetManifoldPlaneNormal() const { return PlaneNormal; }
		const FVec3& GetManifoldPlanePosition() const { return PlanePosition; }
		void SetManifoldPlane(int32 OwnerIndex, int32 FaceIndex, const FVec3& Normal, const FVec3& Pos)
		{
			PlaneOwnerIndex = OwnerIndex;
			PlaneFaceIndex = FaceIndex;
			PlaneNormal = Normal;
			PlanePosition = Pos;
		}

		int32               NumManifoldPoints() const { return Points.Num(); }
		const FVec3& GetManifoldPoint(int32 Index) const { check(0 <= Index && Index < NumManifoldPoints()); return Points[Index]; }
		void				SetManifoldPoint(int32 Index, const FVec3& Point) { check(0 <= Index && Index < NumManifoldPoints()); Points[Index] = Point; }
		void                AddManifoldPoint(const FVec3& Point) { Points.Add(Point); };
		void                ResetManifoldPoints(int32 NewSize = 0) { Points.Reset(NewSize); }

	private:
		// Manifold plane data
		int PlaneOwnerIndex; // index of particle which owns the plane. The other owns the sample positions
		int PlaneFaceIndex; // index of face used as the manifold plane on plane-owner body
		FVec3 PlaneNormal; // local space contact normal on plane-owner
		FVec3 PlanePosition; // local space surface position on plane-owner

		// Manifold point data
		static const int32 MaxPoints = 4;
		TArray<FVec3, TInlineAllocator<MaxPoints>> Points; // @todo(chaos): use TFixedAllocator when we handle building the best manifold properly
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
			FGeometryParticleHandle* Particle0, const FImplicitObject* Implicit0, const FRigidTransform3& Transform0,
			FGeometryParticleHandle* Particle1, const FImplicitObject* Implicit1, const FRigidTransform3& Transform1,
			EContactShapesType ShapesType)
			: Base(Particle0, Implicit0, Transform0, Particle1, Implicit1, Transform1, Base::FType::SinglePointSwept, ShapesType)
			, TimeOfImpact(0) {}

		// Value in range [0,1] used to interpolate P between [X,P] that we will rollback to when solving at time of impact.
		FReal TimeOfImpact;

		static typename Base::FType StaticType() { return Base::FType::SinglePointSwept; };
	};


	struct CHAOS_API FCollisionConstraintsArray
	{
		static const int32 InlineMaxConstraints = 8;

		TArray<FRigidBodyPointContactConstraint, TInlineAllocator<InlineMaxConstraints>> SinglePointConstraints;
		TArray<FRigidBodySweptPointContactConstraint, TInlineAllocator<InlineMaxConstraints>> SinglePointSweptConstraints;
		TArray<FRigidBodyMultiPointContactConstraint, TInlineAllocator<InlineMaxConstraints>> MultiPointConstraints;

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
