// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Queue.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Collision/CollisionConstraintFlags.h"
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

enum class EResimType : uint8
{
	FullResim,	//fully re-run simulation and keep results (any forces must be applied again)
	//ResimWithPrevForces, //use previous forces and keep results (UNIMPLEMENTED)
	ResimAsSlave //use previous forces and snap to previous results regardless of variation - used to push other objects away
	//ResimAsKinematic //treat as kinematic (UNIMPLEMENTED)
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
		TArrayCollection::AddArray(&MVSmooth);
		TArrayCollection::AddArray(&MWSmooth);
		TArrayCollection::AddArray(&MF);
		TArrayCollection::AddArray(&MT);
		TArrayCollection::AddArray(&MLinearImpulse);
		TArrayCollection::AddArray(&MAngularImpulse);
		TArrayCollection::AddArray(&MI);
		TArrayCollection::AddArray(&MInvI);
		TArrayCollection::AddArray(&MM);
		TArrayCollection::AddArray(&MInvM);
		TArrayCollection::AddArray(&MCenterOfMass);
		TArrayCollection::AddArray(&MRotationOfMass);
		TArrayCollection::AddArray(&MLinearEtherDrag);
		TArrayCollection::AddArray(&MAngularEtherDrag);
		TArrayCollection::AddArray(&MCollisionParticles);
		TArrayCollection::AddArray(&MCollisionGroup);
		TArrayCollection::AddArray(&MCollisionConstraintFlags);
		TArrayCollection::AddArray(&MDisabled);
		TArrayCollection::AddArray(&MObjectState);
		TArrayCollection::AddArray(&MPreObjectState);
		TArrayCollection::AddArray(&MIsland);
		TArrayCollection::AddArray(&MToBeRemovedOnFracture);
		TArrayCollection::AddArray(&MGravityEnabled);
		TArrayCollection::AddArray(&MOneWayInteraction);
		TArrayCollection::AddArray(&MResimType);
		TArrayCollection::AddArray(&bCCDEnabled);
	}
	TRigidParticles(const TRigidParticles<T, d>& Other) = delete;
	CHAOS_API TRigidParticles(TRigidParticles<T, d>&& Other)
	    : TKinematicGeometryParticles<T, d>(MoveTemp(Other))
		, MVSmooth(MoveTemp(Other.MVSmooth))
		, MWSmooth(MoveTemp(Other.MWSmooth))
		, MF(MoveTemp(Other.MF))
		, MT(MoveTemp(Other.MT))
		, MLinearImpulse(MoveTemp(Other.MLinearImpulse))
		, MAngularImpulse(MoveTemp(Other.MAngularImpulse))
		, MI(MoveTemp(Other.MI)), MInvI(MoveTemp(Other.MInvI))
		, MM(MoveTemp(Other.MM))
		, MInvM(MoveTemp(Other.MInvM))
		, MCenterOfMass(MoveTemp(Other.MCenterOfMass))
		, MRotationOfMass(MoveTemp(Other.MRotationOfMass))
		, MCollisionParticles(MoveTemp(Other.MCollisionParticles))
		, MCollisionGroup(MoveTemp(Other.MCollisionGroup))
		, MCollisionConstraintFlags(MoveTemp(Other.MCollisionConstraintFlags))
		, MObjectState(MoveTemp(Other.MObjectState))
		, MPreObjectState(MoveTemp(Other.MPreObjectState))
		, MGravityEnabled(MoveTemp(Other.MGravityEnabled))
		, MOneWayInteraction(MoveTemp(Other.MOneWayInteraction))
		, MResimType(MoveTemp(Other.MResimType))
		, bCCDEnabled(MoveTemp(Other.bCCDEnabled))
	{
		TArrayCollection::AddArray(&MVSmooth);
		TArrayCollection::AddArray(&MWSmooth);
		TArrayCollection::AddArray(&MF);
		TArrayCollection::AddArray(&MT);
		TArrayCollection::AddArray(&MLinearImpulse);
		TArrayCollection::AddArray(&MAngularImpulse);
		TArrayCollection::AddArray(&MI);
		TArrayCollection::AddArray(&MInvI);
		TArrayCollection::AddArray(&MM);
		TArrayCollection::AddArray(&MInvM);
		TArrayCollection::AddArray(&MCenterOfMass);
		TArrayCollection::AddArray(&MRotationOfMass);
		TArrayCollection::AddArray(&MLinearEtherDrag);
		TArrayCollection::AddArray(&MAngularEtherDrag);
		TArrayCollection::AddArray(&MCollisionParticles);
		TArrayCollection::AddArray(&MCollisionGroup);
		TArrayCollection::AddArray(&MCollisionConstraintFlags);
		TArrayCollection::AddArray(&MDisabled);
		TArrayCollection::AddArray(&MObjectState);
		TArrayCollection::AddArray(&MPreObjectState);
		TArrayCollection::AddArray(&MIsland);
		TArrayCollection::AddArray(&MToBeRemovedOnFracture);
		TArrayCollection::AddArray(&MGravityEnabled);
		TArrayCollection::AddArray(&MOneWayInteraction);
		TArrayCollection::AddArray(&MResimType);
		TArrayCollection::AddArray(&bCCDEnabled);
	}

	CHAOS_API virtual ~TRigidParticles()
	{}

	FORCEINLINE const TVector<T, d>& VSmooth(const int32 Index) const { return MVSmooth[Index]; }
	FORCEINLINE TVector<T, d>& VSmooth(const int32 Index) { return MVSmooth[Index]; }

	FORCEINLINE const TVector<T, d>& WSmooth(const int32 Index) const { return MWSmooth[Index]; }
	FORCEINLINE TVector<T, d>& WSmooth(const int32 Index) { return MWSmooth[Index]; }

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

	FORCEINLINE const TVector<T,d>& CenterOfMass(const int32 Index) const { return MCenterOfMass[Index]; }
	FORCEINLINE TVector<T,d>& CenterOfMass(const int32 Index) { return MCenterOfMass[Index]; }

	FORCEINLINE const TRotation<T,d>& RotationOfMass(const int32 Index) const { return MRotationOfMass[Index]; }
	FORCEINLINE TRotation<T,d>& RotationOfMass(const int32 Index) { return MRotationOfMass[Index]; }

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

	FORCEINLINE bool HasCollisionConstraintFlag(const ECollisionConstraintFlags Flag, const int32 Index) const { return (MCollisionConstraintFlags[Index] & (uint32)Flag) != 0; }
	FORCEINLINE void AddCollisionConstraintFlag(const ECollisionConstraintFlags Flag, const int32 Index) { MCollisionConstraintFlags[Index] |= (uint32)Flag; }
	FORCEINLINE void RemoveCollisionConstraintFlag(const ECollisionConstraintFlags Flag, const int32 Index) { MCollisionConstraintFlags[Index] &= ~(uint32)Flag; }
	FORCEINLINE void ClearCollisionConstraintFlag(const int32 Index) { MCollisionConstraintFlags[Index] = (uint32)ECollisionConstraintFlags::CCF_None; }
	FORCEINLINE uint32 CollisionConstraintFlag(const int32 Index) const { return MCollisionConstraintFlags[Index]; }

	FORCEINLINE const bool Disabled(const int32 Index) const { return MDisabled[Index]; }

	FORCEINLINE bool& DisabledRef(const int32 Index) { return MDisabled[Index]; }

	// DisableParticle/EnableParticle on Evolution should be used. Don't disable particles with this.
    // Using this will break stuff. This is for solver's use only, and possibly some particle construction/copy code.
	FORCEINLINE void SetDisabledLowLevel(const int32 Index, bool disabled) { MDisabled[Index] = disabled; }

	FORCEINLINE const bool ToBeRemovedOnFracture(const int32 Index) const { return MToBeRemovedOnFracture[Index]; }
	FORCEINLINE bool& ToBeRemovedOnFracture(const int32 Index) { return MToBeRemovedOnFracture[Index]; }

	FORCEINLINE const bool& GravityEnabled(const int32 Index) const { return MGravityEnabled[Index]; }
	FORCEINLINE bool& GravityEnabled(const int32 Index) { return MGravityEnabled[Index]; }

	FORCEINLINE const bool& OneWayInteraction(const int32 Index) const { return MOneWayInteraction[Index]; }
	FORCEINLINE bool& OneWayInteraction(const int32 Index) { return MOneWayInteraction[Index]; }

	FORCEINLINE EResimType ResimType(const int32 Index) const { return MResimType[Index]; }
	FORCEINLINE EResimType& ResimType(const int32 Index) { return MResimType[Index]; }

	FORCEINLINE const bool& CCDEnabled(const int32 Index) const { return bCCDEnabled[Index]; }
	FORCEINLINE bool& CCDEnabled(const int32 Index) { return bCCDEnabled[Index]; }


	FORCEINLINE TArray<TSleepData<T, d>>& GetSleepData() { return MSleepData; }
	FORCEINLINE	void AddSleepData(TGeometryParticleHandle<T, d>* Particle, bool Sleeping)
	{ 
		TSleepData<T, d> SleepData;
		SleepData.Particle = Particle;
		SleepData.Sleeping = Sleeping;

		SleepDataLock.WriteLock();
		MSleepData.Add(SleepData);
		SleepDataLock.WriteUnlock();
	}
	CHAOS_API void ClearSleepData()
	{
		SleepDataLock.WriteLock();
		MSleepData.Empty();
		SleepDataLock.WriteUnlock();
	}
	FORCEINLINE FRWLock& GetSleepDataLock() { return SleepDataLock; }

	FORCEINLINE const EObjectStateType ObjectState(const int32 Index) const { return MObjectState[Index]; }
	FORCEINLINE EObjectStateType& ObjectState(const int32 Index) { return MObjectState[Index]; }

	FORCEINLINE const EObjectStateType PreObjectState(const int32 Index) const { return MPreObjectState[Index]; }
	FORCEINLINE EObjectStateType& PreObjectState(const int32 Index) { return MPreObjectState[Index]; }

	FORCEINLINE const bool Dynamic(const int32 Index) const { return ObjectState(Index) == EObjectStateType::Dynamic; }

	FORCEINLINE const bool Sleeping(const int32 Index) const { return ObjectState(Index) == EObjectStateType::Sleeping; }

	FORCEINLINE const bool HasInfiniteMass(const int32 Index) const { return MInvM[Index] == (T)0; }

	FORCEINLINE const int32 Island(const int32 Index) const { return MIsland[Index]; }
	FORCEINLINE int32& Island(const int32 Index) { return MIsland[Index]; }

	FORCEINLINE FString ToString(int32 index) const
	{
		FString BaseString = TKinematicGeometryParticles<T, d>::ToString(index);
		return FString::Printf(TEXT("%s, MF:%s, MT:%s, MLinearImpulse:%s, MAngularImpulse:%s, MI:%s, MInvI:%s, MM:%f, MInvM:%f, MCenterOfMass:%s, MRotationOfMass:%s, MCollisionParticles(num):%d, MCollisionGroup:%d, MDisabled:%d, MSleepring:%d, MIsland:%d"),
			*BaseString, *F(index).ToString(), *Torque(index).ToString(), *LinearImpulse(index).ToString(), *AngularImpulse(index).ToString(),
			*I(index).ToString(), *InvI(index).ToString(), M(index), InvM(index), *CenterOfMass(index).ToString(), *RotationOfMass(index).ToString(), CollisionParticlesSize(index),
			CollisionGroup(index), Disabled(index), Sleeping(index), Island(index));
	}

	CHAOS_API virtual void Serialize(FChaosArchive& Ar) override
	{
		TKinematicGeometryParticles<T,d>::Serialize(Ar);
		
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::KinematicCentersOfMass)
		{
			Ar << MCenterOfMass;
			Ar << MRotationOfMass;
		}

		Ar << MF << MT << MLinearImpulse << MAngularImpulse << MI << MInvI << MM << MInvM;

		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddDampingToRigids)
		{
			Ar << MLinearEtherDrag << MAngularEtherDrag;
		}

		Ar << MCollisionParticles << MCollisionGroup << MIsland << MDisabled << MObjectState;
		//todo: add gravity enabled when we decide how we want to handle serialization
	}

	FORCEINLINE TArray<TVector<T, d>>& AllF() { return MF; }
	FORCEINLINE TArray<TVector<T, d>>& AllT() { return MT; }
	FORCEINLINE TArray<TVector<T, d>>& AllLinearImpulse() { return MLinearImpulse; }
	FORCEINLINE TArray<TVector<T, d>>& AllAngularImpulse() { return MAngularImpulse; }
	FORCEINLINE TArray<PMatrix<T, d, d>>& AllI() { return MI; }
	FORCEINLINE TArray<PMatrix<T, d, d>>& AllInvI() { return MInvI; }
	FORCEINLINE TArray<FReal>& AllM() { return MM; }
	FORCEINLINE TArray<FReal>& AllInvM() { return MInvM; }
	FORCEINLINE TArray<TVector<T, d>>& AllCenterOfMass() { return MCenterOfMass; }
	FORCEINLINE TArray<TRotation<T, d>>& AllRotationOfMass() { return MRotationOfMass; }
	FORCEINLINE TArray<FReal>& AllLinearEtherDrag() { return MLinearEtherDrag; }
	FORCEINLINE TArray<FReal>& AllAngularEtherDrag() { return MAngularEtherDrag; }
	FORCEINLINE TArray<bool>& AllDisabled() { return MDisabled; }
	FORCEINLINE TArray<EObjectStateType>& AllObjectState() { return MObjectState; }
	FORCEINLINE TArray<bool>& AllGravityEnabled() { return MGravityEnabled; }
	FORCEINLINE TArray<bool>& AllCCDEnabled() { return bCCDEnabled; }

private:
	TArrayCollectionArray<TVector<T, d>> MVSmooth;
	TArrayCollectionArray<TVector<T, d>> MWSmooth;
	TArrayCollectionArray<TVector<T, d>> MF;
	TArrayCollectionArray<TVector<T, d>> MT;
	TArrayCollectionArray<TVector<T, d>> MLinearImpulse;
	TArrayCollectionArray<TVector<T, d>> MAngularImpulse;
	TArrayCollectionArray<PMatrix<T, d, d>> MI;
	TArrayCollectionArray<PMatrix<T, d, d>> MInvI;
	TArrayCollectionArray<T> MM;
	TArrayCollectionArray<T> MInvM;
	TArrayCollectionArray<TVector<T,d>> MCenterOfMass;
	TArrayCollectionArray<TRotation<T,d>> MRotationOfMass;
	TArrayCollectionArray<T> MLinearEtherDrag;
	TArrayCollectionArray<T> MAngularEtherDrag;
	TArrayCollectionArray<TUniquePtr<TBVHParticles<T, d>>> MCollisionParticles;
	TArrayCollectionArray<int32> MCollisionGroup;
	TArrayCollectionArray<uint32> MCollisionConstraintFlags;
	TArrayCollectionArray<int32> MIsland;
	TArrayCollectionArray<bool> MDisabled;
	TArrayCollectionArray<bool> MToBeRemovedOnFracture;
	TArrayCollectionArray<EObjectStateType> MObjectState;
	TArrayCollectionArray<EObjectStateType> MPreObjectState;
	TArrayCollectionArray<bool> MGravityEnabled;
	TArrayCollectionArray<bool> MOneWayInteraction;
	TArrayCollectionArray<EResimType> MResimType;
	TArrayCollectionArray<bool> bCCDEnabled;

	TArray<TSleepData<T, d>> MSleepData;
	FRWLock SleepDataLock;
};



template <typename T, int d>
FChaosArchive& operator<<(FChaosArchive& Ar, TRigidParticles<T, d>& Particles)
{
	Particles.Serialize(Ar);
	return Ar;
}

#ifdef __clang__
#if PLATFORM_WINDOWS
extern template class TRigidParticles<FReal, 3>;
#else
extern template class CHAOS_API TRigidParticles<FReal, 3>;
#endif
#else
extern template class TRigidParticles<FReal, 3>;
#endif

}
