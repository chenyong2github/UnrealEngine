// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Queue.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/BVHParticles.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/KinematicGeometryParticles.h"
#include "Chaos/Matrix.h"
#include "Chaos/Particles.h"
#include "Chaos/Rotation.h"
#include "HAL/LowLevelMemTracker.h"

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

template<class T, int d>
struct TSleepData
{
	TSleepData()
		: Particle(nullptr)
		, Sleeping(true)
	{}

	TSleepData(
		TGeometryParticleHandle<T, d>* InParticle, bool InSleeping)
		: Particle(InParticle)
		, Sleeping(InSleeping)
	{}

	TGeometryParticleHandle<T, d>* Particle;
	bool Sleeping; // if !Sleeping == Awake
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

	CHAOS_API TRigidParticles()
	    : TKinematicGeometryParticles<T, d>()
	{
		TArrayCollection::AddArray(&MF);
		TArrayCollection::AddArray(&MT);
		TArrayCollection::AddArray(&MLinearImpulse);
		TArrayCollection::AddArray(&MAngularImpulse);
		TArrayCollection::AddArray(&MI);
		TArrayCollection::AddArray(&MInvI);
		TArrayCollection::AddArray(&MM);
		TArrayCollection::AddArray(&MInvM);
		TArrayCollection::AddArray(&MLinearEtherDrag);
		TArrayCollection::AddArray(&MAngularEtherDrag);
		TArrayCollection::AddArray(&MCollisionParticles);
		TArrayCollection::AddArray(&MCollisionGroup);
		TArrayCollection::AddArray(&MDisabled);
		TArrayCollection::AddArray(&MObjectState);
		TArrayCollection::AddArray(&MIsland);
		TArrayCollection::AddArray(&MToBeRemovedOnFracture);
	}
	TRigidParticles(const TRigidParticles<T, d>& Other) = delete;
	CHAOS_API TRigidParticles(TRigidParticles<T, d>&& Other)
	    : TKinematicGeometryParticles<T, d>(MoveTemp(Other)), MF(MoveTemp(Other.MF)), MT(MoveTemp(Other.MT)), MLinearImpulse(MoveTemp(Other.MLinearImpulse)), MAngularImpulse(MoveTemp(Other.MAngularImpulse)), MI(MoveTemp(Other.MI)), MInvI(MoveTemp(Other.MInvI)), MM(MoveTemp(Other.MM)), MInvM(MoveTemp(Other.MInvM)), MCollisionParticles(MoveTemp(Other.MCollisionParticles)), MCollisionGroup(MoveTemp(Other.MCollisionGroup)), MObjectState(MoveTemp(Other.MObjectState))
	{
		TArrayCollection::AddArray(&MF);
		TArrayCollection::AddArray(&MT);
		TArrayCollection::AddArray(&MLinearImpulse);
		TArrayCollection::AddArray(&MAngularImpulse);
		TArrayCollection::AddArray(&MI);
		TArrayCollection::AddArray(&MInvI);
		TArrayCollection::AddArray(&MM);
		TArrayCollection::AddArray(&MInvM);
		TArrayCollection::AddArray(&MLinearEtherDrag);
		TArrayCollection::AddArray(&MAngularEtherDrag);
		TArrayCollection::AddArray(&MCollisionParticles);
		TArrayCollection::AddArray(&MCollisionGroup);
		TArrayCollection::AddArray(&MDisabled);
		TArrayCollection::AddArray(&MObjectState);
		TArrayCollection::AddArray(&MIsland);
		TArrayCollection::AddArray(&MToBeRemovedOnFracture);
	}

	CHAOS_API virtual ~TRigidParticles()
	{}

	FORCEINLINE const TVector<T, d>& Torque(const int32 Index) const { return MT[Index]; }
	FORCEINLINE TVector<T, d>& Torque(const int32 Index) { return MT[Index]; }

	FORCEINLINE const TVector<T, d>& F(const int32 Index) const { return MF[Index]; }
	FORCEINLINE TVector<T, d>& F(const int32 Index) { return MF[Index]; }

	FORCEINLINE const TVector<T, d>& LinearImpulse(const int32 Index) const { return MLinearImpulse[Index]; }
	FORCEINLINE TVector<T, d>& LinearImpulse(const int32 Index) { return MLinearImpulse[Index]; }

	FORCEINLINE const TVector<T, d>& AngularImpulse(const int32 Index) const { return MAngularImpulse[Index]; }
	FORCEINLINE TVector<T, d>& AngularImpulse(const int32 Index) { return MAngularImpulse[Index]; }

	FORCEINLINE const PMatrix<T, d, d>& I(const int32 Index) const { return MI[Index]; }
	FORCEINLINE PMatrix<T, d, d>& I(const int32 Index) { return MI[Index]; }

	FORCEINLINE const PMatrix<T, d, d>& InvI(const int32 Index) const { return MInvI[Index]; }
	FORCEINLINE PMatrix<T, d, d>& InvI(const int32 Index) { return MInvI[Index]; }

	FORCEINLINE const T M(const int32 Index) const { return MM[Index]; }
	FORCEINLINE T& M(const int32 Index) { return MM[Index]; }

	FORCEINLINE const T InvM(const int32 Index) const { return MInvM[Index]; }
	FORCEINLINE T& InvM(const int32 Index) { return MInvM[Index]; }

	FORCEINLINE const T& LinearEtherDrag(const int32 index) const { return MLinearEtherDrag[index]; }
	FORCEINLINE T& LinearEtherDrag(const int32 index) { return MLinearEtherDrag[index]; }

	FORCEINLINE const T& AngularEtherDrag(const int32 index) const { return MAngularEtherDrag[index]; }
	FORCEINLINE T& AngularEtherDrag(const int32 index) { return MAngularEtherDrag[index]; }

	FORCEINLINE int32 CollisionParticlesSize(int32 Index) const { return MCollisionParticles[Index] == nullptr ? 0 : MCollisionParticles[Index]->Size(); }

	void CHAOS_API CollisionParticlesInitIfNeeded(const int32 Index);
	void CHAOS_API SetCollisionParticles(const int32 Index, TParticles<T, d>&& Particles);
	
	FORCEINLINE const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles(const int32 Index) const { return MCollisionParticles[Index]; }
	FORCEINLINE TUniquePtr<TBVHParticles<T, d>>& CollisionParticles(const int32 Index) { return MCollisionParticles[Index]; }

	FORCEINLINE const int32 CollisionGroup(const int32 Index) const { return MCollisionGroup[Index]; }
	FORCEINLINE int32& CollisionGroup(const int32 Index) { return MCollisionGroup[Index]; }

	FORCEINLINE const bool Disabled(const int32 Index) const { return MDisabled[Index]; }

	FORCEINLINE bool& DisabledRef(const int32 Index) { return MDisabled[Index]; }

	// DisableParticle/EnableParticle on Evolution should be used. Don't disable particles with this.
    // Using this will break stuff. This is for solver's use only, and possibly some particle construction/copy code.
	FORCEINLINE void SetDisabledLowLevel(const int32 Index, bool disabled) { MDisabled[Index] = disabled; }

	FORCEINLINE const bool ToBeRemovedOnFracture(const int32 Index) const { return MToBeRemovedOnFracture[Index]; }
	FORCEINLINE bool& ToBeRemovedOnFracture(const int32 Index) { return MToBeRemovedOnFracture[Index]; }

	FORCEINLINE TQueue<TSleepData<T, d>, EQueueMode::Mpsc>& GetSleepData() { return MSleepData; }
	FORCEINLINE void AddSleepData(TGeometryParticleHandle<T, d>* Particle, bool Sleeping)
	{ 
		TSleepData<T, d> SleepData;
		SleepData.Particle = Particle;
		SleepData.Sleeping = Sleeping;
		MSleepData.Enqueue(SleepData); 
	}

	FORCEINLINE const EObjectStateType ObjectState(const int32 Index) const { return MObjectState[Index]; }
	FORCEINLINE EObjectStateType& ObjectState(const int32 Index) { return MObjectState[Index]; }

	FORCEINLINE const bool Dynamic(const int32 Index) const { return ObjectState(Index) == EObjectStateType::Dynamic; }

	FORCEINLINE const bool Sleeping(const int32 Index) const { return ObjectState(Index) == EObjectStateType::Sleeping; }

	FORCEINLINE const bool HasInfiniteMass(const int32 Index) const { return MInvM[Index] == (T)0; }

	FORCEINLINE const int32 Island(const int32 Index) const { return MIsland[Index]; }
	FORCEINLINE int32& Island(const int32 Index) { return MIsland[Index]; }

	FORCEINLINE FString ToString(int32 index) const
	{
		FString BaseString = TKinematicGeometryParticles<T, d>::ToString(index);
		return FString::Printf(TEXT("%s, MF:%s, MT:%s, MLinearImpulse:%s, MAngularImpulse:%s, MI:%s, MInvI:%s, MM:%f, MInvM:%f, MCollisionParticles(num):%d, MCollisionGroup:%d, MDisabled:%d, MSleepring:%d, MIsland:%d"), *BaseString, *F(index).ToString(), *Torque(index).ToString(), *LinearImpulse(index).ToString(), *AngularImpulse(index).ToString(), *I(index).ToString(), *InvI(index).ToString(), M(index), InvM(index), CollisionParticlesSize(index), CollisionGroup(index), Disabled(index), Sleeping(index), Island(index));
	}

	CHAOS_API virtual void Serialize(FChaosArchive& Ar) override
	{
		LLM_SCOPE(ELLMTag::ChaosParticles);
		TKinematicGeometryParticles<T,d>::Serialize(Ar);
		Ar << MF << MT << MLinearImpulse << MAngularImpulse << MI << MInvI << MM << MInvM;

		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddDampingToRigids)
		{
			Ar << MLinearEtherDrag << MAngularEtherDrag;
		}

		Ar << MCollisionParticles << MCollisionGroup << MIsland << MDisabled << MObjectState;
	}

  private:
	TArrayCollectionArray<TVector<T, d>> MF;
	TArrayCollectionArray<TVector<T, d>> MT;
	TArrayCollectionArray<TVector<T, d>> MLinearImpulse;
	TArrayCollectionArray<TVector<T, d>> MAngularImpulse;
	TArrayCollectionArray<PMatrix<T, d, d>> MI;
	TArrayCollectionArray<PMatrix<T, d, d>> MInvI;
	TArrayCollectionArray<T> MM;
	TArrayCollectionArray<T> MInvM;
	TArrayCollectionArray<T> MLinearEtherDrag;
	TArrayCollectionArray<T> MAngularEtherDrag;
	TArrayCollectionArray<TUniquePtr<TBVHParticles<T, d>>> MCollisionParticles;
	TArrayCollectionArray<int32> MCollisionGroup;
	TArrayCollectionArray<int32> MIsland;
	TArrayCollectionArray<bool> MDisabled;
	TArrayCollectionArray<bool> MToBeRemovedOnFracture;
	TArrayCollectionArray<EObjectStateType> MObjectState;
	TQueue<TSleepData<T, d>, EQueueMode::Mpsc> MSleepData;
};



template <typename T, int d>
FChaosArchive& operator<<(FChaosArchive& Ar, TRigidParticles<T, d>& Particles)
{
	Particles.Serialize(Ar);
	return Ar;
}

#ifdef __clang__
#if PLATFORM_WINDOWS
extern template class TRigidParticles<float, 3>;
#else
extern template class CHAOS_API TRigidParticles<float, 3>;
#endif
#else
extern template class TRigidParticles<float, 3>;
#endif

}
