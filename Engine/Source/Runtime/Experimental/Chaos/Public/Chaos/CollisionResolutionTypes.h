// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Box.h"
#include "Chaos/ExternalCollisionData.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/Vector.h"

class UPhysicalMaterial;

namespace Chaos
{

	template<typename T, int d>
	class TPBDCollisionConstraints;

	template<typename  T, int d>
	class TPBDCollisionConstraintHandle;

	template<typename T, int d>
	class TRigidBodyPointContactConstraint;



	/** Specifies the type of work we should do*/
	enum class CHAOS_API ECollisionUpdateType
	{
		Any,	//stop if we have at least one deep penetration. Does not compute location or normal
		Deepest	//find the deepest penetration. Compute location and normal
	};

	/** Return value of the collision modification callback */
	enum class CHAOS_API ECollisionModifierResult
	{
		Unchanged,	/** No change to the collision */
		Modified,	/** Modified the collision, but want it to remain enabled */
		Disabled,	/** Collision should be disabled */
	};



	/*
	*
	*/
	template<class T = float, int d = 3>
	class CHAOS_API TCollisionConstraintBase
	{
	public:
		using FGeometryParticleHandle = TGeometryParticleHandle<T, d>;

		enum class FType
		{
			None = 0,     // default value also indicates a invalid constraint
			SinglePoint,   // TRigidBodyPointContactConstraint
			Plane         // TRigidBodyPlaneContactConstraint
		};

		TCollisionConstraintBase() : Type(FType::None) {}
		TCollisionConstraintBase(FType InType) : Type(InType) {}
		FType GetType() const { return Type; }

		template<class AS_T> AS_T * As() { return static_cast<AS_T*>(this); }
		template<class AS_T> const AS_T * As() const { return static_cast<AS_T*>(this); }

		FGeometryParticleHandle* Particle[2]; // { Point, Volume } 

	private:
		FType Type;
	};
	typedef TCollisionConstraintBase<float, 3> FCollisionConstraintBase;
	typedef TArray<TCollisionConstraintBase<float, 3>*> FCollisionConstraintsArray;


	/*
	*
	*/
	template<class T, int d>
	class CHAOS_API TPointContactManifold
	{
	public:
		TPointContactManifold(int32 InTimestamp = -INT_MAX, const FImplicitObject* InImplicit0 = nullptr, const FImplicitObject* InImplicit1 = nullptr)
			: bDisabled(true), Timestamp(InTimestamp), Normal(0), Phi(FLT_MAX)
		{
			Implicit[0] = InImplicit0;
			Implicit[1] = InImplicit1;
		}

		bool bDisabled;
		int32 Timestamp;
		TVector<T, d> Normal;
		TVector<T, d> Location;
		T Phi;

		const FImplicitObject* Implicit[2]; // {Of Particle[0], Of Particle[1]}
	};

	/*
	*
	*/
	template<class T, int d>
	class CHAOS_API TRigidBodyPointContactConstraint : public TCollisionConstraintBase<T, d>
	{
	public:
		using Base = TCollisionConstraintBase<T, d>;
		using FGeometryParticleHandle = TGeometryParticleHandle<T, d>;
		using FManifold = TPointContactManifold<T, d>;
		using Base::Particle;

		TRigidBodyPointContactConstraint() : Base(Base::FType::SinglePoint), AccumulatedImpulse(0) {}
		static typename Base::FType StaticType() { return Base::FType::SinglePoint; };


		//
		// Manidfold management
		//
		bool ContainsManifold(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return (A == nullptr&&B == nullptr) ? (Manifold.Implicit[0] != nullptr && Manifold.Implicit[1] != nullptr) : (Manifold.Implicit[0] == A && Manifold.Implicit[1] == B); }
		void AddManifold(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) { Manifold.Implicit[0] = A; Manifold.Implicit[1] = B; }


		//
		// API
		//
		void ResetPhi(T InPhi) { SetPhi(InPhi); }

		void SetPhi(T InPhi, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr, int32 Index = 0) { Manifold.Phi = InPhi; }
		T GetPhi(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return Manifold.Phi; }

		void SetDisabled(bool bInDisabled, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) { Manifold.bDisabled = bInDisabled; }
		TVector<T, d> GetDisabled(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return Manifold.bDisabled; }

		void SetNormal(const TVector<T, d> & InNormal, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) { Manifold.Normal = InNormal; }
		TVector<T, d> GetNormal(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return Manifold.Normal; }

		void SetLocation(const TVector<T, d> & InLocation, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr, int32 Index = 0) { Manifold.Location = InLocation; }
		TVector<T, d> GetLocation(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return Manifold.Location; }

		FString ToString() const
		{
			return FString::Printf(TEXT("Particle:%s, Levelset:%s, AccumulatedImpulse:%s"), *Particle[0]->ToString(), *Particle[1]->ToString(), *AccumulatedImpulse.ToString());
		}


		FManifold Manifold;
		TVector<T, d> AccumulatedImpulse;

	};
	typedef TRigidBodyPointContactConstraint<float, 3> FRigidBodyPointContactConstraint;


	/*
	*
	*/
	template<class T, int d>
	class CHAOS_API TPlaneContactManifold
	{
	public:
		TPlaneContactManifold(int32 InTimestamp = -INT_MAX, const FImplicitObject* InImplicit0 = nullptr, const FImplicitObject* InImplicit1 = nullptr)
			: bDisabled(true), Timestamp(InTimestamp), Phi(FLT_MAX)
		{
			Implicit[0] = InImplicit0;
			Implicit[1] = InImplicit1;
		}

		bool bDisabled;
		int32 Timestamp;

		TVector<T, d> PlaneNormal;
		TVector<T, d> PlanePosition;
		TVector<T, d> ManifoldSamples[4]; // iterative plane samples

		TVector<T, d> Location;
		T Phi;

		const FImplicitObject* Implicit[2]; // {Of Particle[0], Of Particle[1]}
	};



	/*
	*
	*/
	template<class T, int d>
	class CHAOS_API TRigidBodyPlaneContactConstraint : public TCollisionConstraintBase<T, d>
	{
	public:
		using Base = TCollisionConstraintBase<T, d>;
		using FGeometryParticleHandle = TGeometryParticleHandle<T, d>;
		using FManifold = TPlaneContactManifold<T, d>;
		using Base::Particle;

		TRigidBodyPlaneContactConstraint() : Base(Base::FType::Plane), AccumulatedImpulse(0) {}
		static typename Base::FType StaticType() { return Base::FType::Plane; };


		bool ContainsManifold(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return (A == nullptr&&B == nullptr) ? (Manifold.Implicit[0] != nullptr && Manifold.Implicit[1] != nullptr) : (Manifold.Implicit[0] == A && Manifold.Implicit[1] == B); }
		void AddManifold(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) { Manifold.Implicit[0] = A; Manifold.Implicit[1] = B; }


		FString ToString() const
		{
			return FString::Printf(TEXT("Particle:%s, Levelset:%s, AccumulatedImpulse:%s"), *Particle[0]->ToString(), *Particle[1]->ToString(), *AccumulatedImpulse.ToString());
		}


		FManifold Manifold;
		TVector<T, d> AccumulatedImpulse;

	};
	typedef TRigidBodyPlaneContactConstraint<float, 3> FRigidBodyPlaneContactConstraint;


	template<class T, int d>
	struct CHAOS_API TRigidBodyContactConstraintPGS
	{
		TRigidBodyContactConstraintPGS() : AccumulatedImpulse(0.f) {}
		TGeometryParticleHandle<T, d>* Particle;
		TGeometryParticleHandle<T, d>* Levelset;
		TArray<TVector<T, d>> Normal;
		TArray<TVector<T, d>> Location;
		TArray<T> Phi;
		TVector<T, d> AccumulatedImpulse;
	};


	template<class T, int d>
	class CHAOS_API TPBDCollisionConstraintHandle : public TContainerConstraintHandle<TPBDCollisionConstraints<T, d>>
	{
	public:
		using Base = TContainerConstraintHandle<TPBDCollisionConstraints<T, d>>;
		using FConstraintContainer = TPBDCollisionConstraints<T, d>;
		using FConstraintBase = TCollisionConstraintBase<T, d>;


		TPBDCollisionConstraintHandle()
			: ConstraintType(FConstraintBase::FType::None)
		{}

		TPBDCollisionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex, typename FConstraintBase::FType InType)
			: TContainerConstraintHandle<TPBDCollisionConstraints<T, d>>(InConstraintContainer, InConstraintIndex)
			, ConstraintType(InType)
		{
		}

		// Handle API

		const TRigidBodyPointContactConstraint<T, d>& GetPointContact() const { check(GetType() == FConstraintBase::FType::SinglePoint); return ConstraintContainer->PointConstraints[ConstraintIndex]; }
		TRigidBodyPointContactConstraint<T, d>& GetPointContact() { check(GetType() == FConstraintBase::FType::SinglePoint); return ConstraintContainer->PointConstraints[ConstraintIndex]; }

		const TRigidBodyPlaneContactConstraint<T, d>& GetPlaneContact() const { check(GetType() == FConstraintBase::FType::Plane); return ConstraintContainer->PlaneConstraints[ConstraintIndex]; }
		TRigidBodyPlaneContactConstraint<T, d>& GetPlaneContact() { check(GetType() == FConstraintBase::FType::Plane); return ConstraintContainer->PlaneConstraints[ConstraintIndex]; }


		typename FConstraintBase::FType GetType() const { return ConstraintType; }

		void SetConstraintIndex(int32 IndexIn, typename FConstraintBase::FType InType)
		{
			ConstraintIndex = IndexIn;
			ConstraintType = InType;
		}

		// Contact API

		TVector<T, d> GetContactLocation() const
		{
			if (ConstraintType == FConstraintBase::FType::SinglePoint)
			{
				return GetPointContact().GetLocation();
			}
			ensure(false);
			return TVector<T, d>(0);
		}

		TVector<T, d> GetAccumulatedImpulse() const
		{
			if (ConstraintType == FConstraintBase::FType::SinglePoint)
			{
				return GetPointContact().AccumulatedImpulse;
			}
			ensure(false);

			return TVector<T, d>(0);
		}


		TVector<const TGeometryParticleHandle<T, d>*, 2> GetConstrainedParticles() const
		{
			if (ConstraintType == FConstraintBase::FType::SinglePoint)
			{
				const TRigidBodyPointContactConstraint<float, 3>& Contact = GetPointContact();
				return { Contact.Particle[0], Contact.Particle[1] };
			}
			ensure(false);
			return { nullptr,nullptr };
		}



		TVector<TGeometryParticleHandle<T, d>*, 2> GetConstrainedParticles()
		{
			if (ConstraintType == FConstraintBase::FType::SinglePoint)
			{
				return { GetPointContact().Particle[0], GetPointContact().Particle[1] };
			}
			ensure(false);
			return { nullptr,nullptr };
		}


	protected:
		typename FConstraintBase::FType ConstraintType;
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;


	};

}
