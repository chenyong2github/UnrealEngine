// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/BVHParticles.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/KinematicGeometryParticles.h"
#include "Chaos/Matrix.h"
#include "Chaos/Particles.h"
#include "Chaos/Rotation.h"
#include "Chaos/TriangleMesh.h"

namespace Chaos
{
enum class EObjectStateType : int8
{
	Uninitialized = 0,
	Sleeping = 1,
	Kinematic = 2,
	Static = 3,
	Dynamic = 4,

	Count
};

// Counts the number of bits needed to represent an int with a max
constexpr int8 NumBitsNeeded(const int8 MaxValue)
{
	return MaxValue == 0 ? 0 : 1 + NumBitsNeeded(MaxValue >> 1);
}

// Make a bitmask which covers the lowest NumBits bits with 1's.
constexpr int8 LowBitsMask(const int8 NumBits)
{
	return NumBits == 0 ? 0 : (1 << (NumBits - 1)) | LowBitsMask(NumBits - 1);
}

// Count N, the number of bits needed to store an object state
static constexpr int8 ObjectStateBitCount = NumBitsNeeded((int8)EObjectStateType::Count - 1);

template<class T, int d>
class TRigidParticles : public TKinematicGeometryParticles<T, d>
{
  public:
	using TArrayCollection::Size;
    using TParticles<T, d>::X;
    using TGeometryParticles<T, d>::R;

	TRigidParticles()
	    : TKinematicGeometryParticles<T, d>()
	{
		TArrayCollection::AddArray(&MF);
		TArrayCollection::AddArray(&MT);
		TArrayCollection::AddArray(&MI);
		TArrayCollection::AddArray(&MInvI);
		TArrayCollection::AddArray(&MM);
		TArrayCollection::AddArray(&MInvM);
		TArrayCollection::AddArray(&MCollisionParticles);
		TArrayCollection::AddArray(&MCollisionGroup);
		TArrayCollection::AddArray(&MDisabled);
		TArrayCollection::AddArray(&MObjectState);
		TArrayCollection::AddArray(&MIsland);
		TArrayCollection::AddArray(&MToBeRemovedOnFracture);
	}
	TRigidParticles(const TRigidParticles<T, d>& Other) = delete;
	TRigidParticles(TRigidParticles<T, d>&& Other)
	    : TKinematicGeometryParticles<T, d>(MoveTemp(Other)), MF(MoveTemp(Other.MF)), MT(MoveTemp(Other.MT)), MI(MoveTemp(Other.MI)), MInvI(MoveTemp(Other.MInvI)), MM(MoveTemp(Other.MM)), MInvM(MoveTemp(Other.MInvM)), MCollisionParticles(MoveTemp(Other.MCollisionParticles)), MCollisionGroup(MoveTemp(Other.MCollisionGroup)), MObjectState(MoveTemp(Other.MObjectState))
	{
		TArrayCollection::AddArray(&MF);
		TArrayCollection::AddArray(&MT);
		TArrayCollection::AddArray(&MI);
		TArrayCollection::AddArray(&MInvI);
		TArrayCollection::AddArray(&MM);
		TArrayCollection::AddArray(&MInvM);
		TArrayCollection::AddArray(&MCollisionParticles);
		TArrayCollection::AddArray(&MCollisionGroup);
		TArrayCollection::AddArray(&MDisabled);
		TArrayCollection::AddArray(&MObjectState);
		TArrayCollection::AddArray(&MIsland);
		TArrayCollection::AddArray(&MToBeRemovedOnFracture);
	}

	const TVector<T, d>& Torque(const int32 Index) const { return MT[Index]; }
	TVector<T, d>& Torque(const int32 Index) { return MT[Index]; }

	const TVector<T, d>& F(const int32 Index) const { return MF[Index]; }
	TVector<T, d>& F(const int32 Index) { return MF[Index]; }

	const PMatrix<T, d, d>& I(const int32 Index) const { return MI[Index]; }
	PMatrix<T, d, d>& I(const int32 Index) { return MI[Index]; }

	const PMatrix<T, d, d>& InvI(const int32 Index) const { return MInvI[Index]; }
	PMatrix<T, d, d>& InvI(const int32 Index) { return MInvI[Index]; }

	const T M(const int32 Index) const { return MM[Index]; }
	T& M(const int32 Index) { return MM[Index]; }

	const T InvM(const int32 Index) const { return MInvM[Index]; }
	T& InvM(const int32 Index) { return MInvM[Index]; }

	int32 CollisionParticlesSize(int32 Index) const { return MCollisionParticles[Index] == nullptr ? 0 : MCollisionParticles[Index]->Size(); }
	void CHAOS_API CollisionParticlesInitIfNeeded(const int32 Index);
	void CHAOS_API SetCollisionParticles(const int32 Index, TParticles<T, d>&& Particles);
	
	const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles(const int32 Index) const { return MCollisionParticles[Index]; }
	TUniquePtr<TBVHParticles<T, d>>& CollisionParticles(const int32 Index) { return MCollisionParticles[Index]; }

	const int32 CollisionGroup(const int32 Index) const { return MCollisionGroup[Index]; }
	int32& CollisionGroup(const int32 Index) { return MCollisionGroup[Index]; }

	const bool Disabled(const int32 Index) const { return MDisabled[Index]; }

	bool& DisabledRef(const int32 Index) { return MDisabled[Index]; }

	// DisableParticle/EnableParticle on Evolution should be used. Don't disable particles with this.
    // Using this will break stuff. This is for solver's use only, and possibly some particle construction/copy code.
	void SetDisabledLowLevel(const int32 Index, bool disabled) { MDisabled[Index] = disabled; }

	const bool ToBeRemovedOnFracture(const int32 Index) const { return MToBeRemovedOnFracture[Index]; }
	bool& ToBeRemovedOnFracture(const int32 Index) { return MToBeRemovedOnFracture[Index]; }

	const EObjectStateType ObjectState(const int32 Index) const { return MObjectState[Index]; }
	EObjectStateType& ObjectState(const int32 Index) { return MObjectState[Index]; }

	const bool Dynamic(const int32 Index) const { return ObjectState(Index) == EObjectStateType::Dynamic; }

	const bool Sleeping(const int32 Index) const { return ObjectState(Index) == EObjectStateType::Sleeping; }

	const bool HasInfiniteMass(const int32 Index) const { return MInvM[Index] == (T)0; }

	const int32 Island(const int32 Index) const { return MIsland[Index]; }
	int32& Island(const int32 Index) { return MIsland[Index]; }

	FString ToString(int32 index) const
	{
		FString BaseString = TKinematicGeometryParticles<T, d>::ToString(index);
		return FString::Printf(TEXT("%s, MF:%s, MT:%s, MI:%s, MInvI:%s, MM:%f, MInvM:%f, MCollisionParticles(num):%d, MCollisionGroup:%d, MDisabled:%d, MSleepring:%d, MIsland:%d"), *BaseString, *F(index).ToString(), *Torque(index).ToString(), *I(index).ToString(), *InvI(index).ToString(), M(index), InvM(index), CollisionParticlesSize(index), CollisionGroup(index), Disabled(index), Sleeping(index), Island(index));
	}

	void Serialize(FChaosArchive& Ar)
	{
		TKinematicGeometryParticles<T,d>::Serialize(Ar);
		Ar << MF << MT << MI << MInvI << MM << MInvM;
		Ar << MCollisionParticles << MCollisionGroup << MIsland << MDisabled << MObjectState;
	}

  private:
	TArrayCollectionArray<TVector<T, d>> MF;
	TArrayCollectionArray<TVector<T, d>> MT;
	TArrayCollectionArray<PMatrix<T, d, d>> MI;
	TArrayCollectionArray<PMatrix<T, d, d>> MInvI;
	TArrayCollectionArray<T> MM;
	TArrayCollectionArray<T> MInvM;
	TArrayCollectionArray<TUniquePtr<TBVHParticles<T, d>>> MCollisionParticles;
	TArrayCollectionArray<int32> MCollisionGroup;
	TArrayCollectionArray<int32> MIsland;
	TArrayCollectionArray<bool> MDisabled;
	TArrayCollectionArray<bool> MToBeRemovedOnFracture;
	TArrayCollectionArray<EObjectStateType> MObjectState;
};



template <typename T, int d>
FChaosArchive& operator<<(FChaosArchive& Ar, TRigidParticles<T, d>& Particles)
{
	Particles.Serialize(Ar);
	return Ar;
}

extern template class TRigidParticles<float, 3>;

}
