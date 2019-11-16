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
	class TPBDCollisionConstraint;

	template<typename  T, int d>
	class TPBDCollisionConstraintHandle;

	template<typename T, int d>
	class TRigidBodyContactConstraint;

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
	template<class T, int d>
	class CHAOS_API TConvexManifold
	{
	public:
		TConvexManifold(int32 InTimestamp = -INT_MAX, const FImplicitObject* InImplicit0 = nullptr, const FImplicitObject* InImplicit1 = nullptr)
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


	template<class T, int d>
	class CHAOS_API TCollisionConstraintBase
	{
	public:

		enum class FType
		{
			None = 0,     // default value also indicates a invalid constraint
			SinglePoint   // TRigidBodyContactConstraint
		};

		TCollisionConstraintBase() : Type(FType::None) {}
		TCollisionConstraintBase(FType InType) : Type(InType) {}
		FType GetType() const { return Type; }

		template<class AS_T> AS_T * As() {return static_cast<AS_T>(this); }
		template<class AS_T> const AS_T * As() const { return static_cast<AS_T>(this); }

	private:
		FType Type;
	};

	/*
	*
	*/
	template<class T, int d>
	class CHAOS_API TRigidBodyContactConstraint : public TCollisionConstraintBase<T,d>
	{
	public:
		using Base = TCollisionConstraintBase<T, d>;
		using FGeometryParticleHandle = TGeometryParticleHandle<T, d>;
		using FConvexManifold = TConvexManifold<T, d>;

		TRigidBodyContactConstraint() : AccumulatedImpulse(0) {}

		//API
		void ResetPhi(T InPhi) { SetPhi(InPhi); }
		bool ContainsManifold(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return (A==nullptr&&B==nullptr)?(ShapeManifold.Implicit[0] != nullptr && ShapeManifold.Implicit[1] != nullptr):(ShapeManifold.Implicit[0]==A && ShapeManifold.Implicit[1]==B); }

		void SetDisabled(bool bInDisabled, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) { ShapeManifold.bDisabled = bInDisabled; }
		TVector<T, d> GetDisabled(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return ShapeManifold.bDisabled; }


		void SetNormal(const TVector<T, d> & InNormal, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) { ShapeManifold.Normal = InNormal; }
		TVector<T, d> GetNormal(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return ShapeManifold.Normal; }

		void SetLocation(const TVector<T, d> & InLocation, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr, int32 Index=0) { ShapeManifold.Location = InLocation; }
		TVector<T, d> GetLocation(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return ShapeManifold.Location; }

		void SetPhi(T InPhi, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr, int32 Index = 0) { ShapeManifold.Phi = InPhi; }
		T GetPhi(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return ShapeManifold.Phi; }

		void AddManifold(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) { ShapeManifold.Implicit[0] = A; ShapeManifold.Implicit[1] = B; }

		FConvexManifold ShapeManifold;
		FGeometryParticleHandle* Particle[2]; // { Point, Volume } 
		TVector<T, d> AccumulatedImpulse;

		FString ToString() const
		{
			return FString::Printf(TEXT("Particle:%s, Levelset:%s, AccumulatedImpulse:%s"), *Particle[0]->ToString(), *Particle[1]->ToString(), *AccumulatedImpulse.ToString());
		}

		static typename Base::FType StaticType() { return Base::FType::SinglePoint; };

	};


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
	class CHAOS_API TPBDCollisionConstraintHandle : public TContainerConstraintHandle<TPBDCollisionConstraint<T, d>>
	{
	public:
		using Base = TContainerConstraintHandle<TPBDCollisionConstraint<T, d>>;
		using FConstraintContainer = TPBDCollisionConstraint<T, d>;
		using FConstraintBase = TCollisionConstraintBase<T, d>;


		TPBDCollisionConstraintHandle() 
			: ConstraintType(FConstraintBase::FType::None)
		{}

		TPBDCollisionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex, typename FConstraintBase::FType InType)
			: TContainerConstraintHandle<TPBDCollisionConstraint<T, d>>(InConstraintContainer, InConstraintIndex)
			, ConstraintType(InType)
		{
		}

		// Handle API

		template<class AS_T> CHAOS_API AS_T& GetContact();
		template<class AS_T> CHAOS_API const AS_T& GetContact() const;

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
				return GetContact<TRigidBodyContactConstraint<float, 3> >().GetLocation();
			}
			ensure(false);
			return TVector<T, d>(0);
		}

		TVector<T, d> GetAccumulatedImpulse() const
		{
			if (ConstraintType == FConstraintBase::FType::SinglePoint)
			{
				return GetContact<TRigidBodyContactConstraint<float, 3> >().AccumulatedImpulse;
			}
			ensure(false);

			return TVector<T, d>(0);
		}


		TVector<const TGeometryParticleHandle<T, d>*, 2> GetConstrainedParticles() const
		{
			if (ConstraintType == FConstraintBase::FType::SinglePoint)
			{
				const TRigidBodyContactConstraint<float, 3>& Contact = GetContact< TRigidBodyContactConstraint<float, 3> >();
				return { Contact.Particle[0], Contact.Particle[1] };
			}
			ensure(false);
			return { nullptr,nullptr };
		}



		TVector<TGeometryParticleHandle<T, d>*, 2> GetConstrainedParticles()
		{
			if (ConstraintType == FConstraintBase::FType::SinglePoint)
			{
				return { GetContact< TRigidBodyContactConstraint<float, 3> >().Particle[0], GetContact< TRigidBodyContactConstraint<float, 3> >().Particle[1] };
			}
			ensure(false);
			return { nullptr,nullptr };
		}


	protected:
		typename FConstraintBase::FType ConstraintType;
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;


	};

	extern template TRigidBodyContactConstraint<float, 3>& TPBDCollisionConstraintHandle<float, 3>::GetContact<TRigidBodyContactConstraint<float, 3>>();
	extern template const TRigidBodyContactConstraint<float, 3>& TPBDCollisionConstraintHandle<float, 3>::GetContact<TRigidBodyContactConstraint<float, 3>>() const;

}
