// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Box.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/Vector.h"

class UPhysicalMaterial;

namespace Chaos
{

	template<typename T, int d>
	class TPBDCollisionConstraint;


	/*
	*
	*  Contact manifold stored in the local space of the
	*  Target object.
	*
	*/
	template<class T, int d>
	class TContactData
	{
	public:

		TContactData() : bDisabled(true), Normal(0), Phi(FLT_MAX) {};

		//void AddPoint(TVector<T, d> Point, TVector<T, d> Normal){}

		bool bDisabled;
		//int NumContacts;
		TVector<T, d> Normal;
		TVector<T, d> Location;
		T Phi;
	};

	/*
	*
	*/
	template<class T, int d>
	class TConvexManifold
	{
	public:
		using FContactData = TContactData<T, d>;

		TConvexManifold(int32 InTimestamp = -INT_MAX, const FImplicitObject* InImplicit0 = nullptr, const FImplicitObject* InImplicit1 = nullptr)
			: Timestamp(InTimestamp)
		{
			Implicit[0] = InImplicit0;
			Implicit[1] = InImplicit1;
		}

		int32 Timestamp;
		FContactData Manifold;
		const FImplicitObject* Implicit[2]; // {Of Particle[0], Of Particle[1]}
	};


	/*
	*
	*/
	template<class T, int d>
	class TRigidBodyContactConstraint
	{
	public:
		using FGeometryParticleHandle = TGeometryParticleHandle<T, d>;
		using FConvexManifold = TConvexManifold<T, d>;

		TRigidBodyContactConstraint() : AccumulatedImpulse(0) {}

		//API
		void ResetPhi(T InPhi) { SetPhi(InPhi); }
		bool ContainsManifold(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return (A==nullptr&&B==nullptr)?(ShapeManifold.Implicit[0] != nullptr && ShapeManifold.Implicit[1] != nullptr):(ShapeManifold.Implicit[0]==A && ShapeManifold.Implicit[1]==B); }

		void SetDisabled(bool bInDisabled, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) { ShapeManifold.Manifold.bDisabled = bInDisabled; }
		TVector<T, d> GetDisabled(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return ShapeManifold.Manifold.bDisabled; }


		void SetNormal(const TVector<T, d> & InNormal, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) { ShapeManifold.Manifold.Normal = InNormal; }
		TVector<T, d> GetNormal(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return ShapeManifold.Manifold.Normal; }

		void SetLocation(const TVector<T, d> & InLocation, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr, int32 Index=0) { ShapeManifold.Manifold.Location = InLocation; }
		TVector<T, d> GetLocation(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return ShapeManifold.Manifold.Location; }

		void SetPhi(T InPhi, const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr, int32 Index = 0) { ShapeManifold.Manifold.Phi = InPhi; }
		T GetPhi(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) const { return ShapeManifold.Manifold.Phi; }

		void AddManifold(const FImplicitObject* B = nullptr, const FImplicitObject* A = nullptr) { ShapeManifold.Implicit[0] = A; ShapeManifold.Implicit[1] = B; }

		FConvexManifold ShapeManifold;
		FGeometryParticleHandle* Particle[2]; // { Point, Volume } 
		TVector<T, d> AccumulatedImpulse;

		FString ToString() const
		{
			return FString::Printf(TEXT("Particle:%s, Levelset:%s, AccumulatedImpulse:%s"), *Particle[0]->ToString(), *Particle[1]->ToString(), *AccumulatedImpulse.ToString());
		}

	};


	template<class T, int d>
	struct TRigidBodyContactConstraintPGS
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

		TPBDCollisionConstraintHandle()
		{}

		TPBDCollisionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex)
			: TContainerConstraintHandle<TPBDCollisionConstraint<T, d>>(InConstraintContainer, InConstraintIndex)
		{
		}

		TRigidBodyContactConstraint<T, d>& GetContact()
		{
			return ConstraintContainer->Constraints[ConstraintIndex];
		}

		const TRigidBodyContactConstraint<T, d>& GetContact() const
		{
			return ConstraintContainer->Constraints[ConstraintIndex];
		}

		void SetConstraintIndex(int32 IndexIn)
		{
			ConstraintIndex = IndexIn;
		}

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;


	};

}
