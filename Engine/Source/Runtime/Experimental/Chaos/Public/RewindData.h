// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Core.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Containers/CircularBuffer.h"
#include "Chaos/ResimCacheBase.h"

#ifndef VALIDATE_REWIND_DATA
#define VALIDATE_REWIND_DATA 0
#endif

namespace Chaos
{

struct FFrameAndPhase
{
	enum EParticleHistoryPhase : uint8
	{
		//The particle state before PushData, server state update, or any sim callbacks are processed 
		//This is the results of the previous frame before any GT modifications are made in this frame
		PrePushData = 0,

		//The particle state after PushData is applied, but before any server state is applied
		//This is what the server state should be compared against
		//This is what we rewind to before a resim
		PostPushData,

		//The particle state after sim callbacks are applied.
		//This is used to detect desync of particles before simulation itself is run (these desyncs can come from server state or the sim callback itself)
		PostCallbacks,

		NumPhases
};

	int32 Frame : 30;
	uint32 Phase : 2;

	bool operator<(const FFrameAndPhase& Other) const
{
		return Frame < Other.Frame || (Frame == Other.Frame && Phase < Other.Phase);
	}

	bool operator<=(const FFrameAndPhase& Other) const
	{
		return Frame < Other.Frame || (Frame == Other.Frame && Phase <= Other.Phase);
	}

	bool operator==(const FFrameAndPhase& Other) const
	{
		return Frame == Other.Frame && Phase == Other.Phase;
	}
};

template <typename THandle, typename T, bool bNoEntryIsHead>
struct NoEntryInSync
{
	static bool Helper(const THandle& Handle)
	{
		//nothing written so we're pointing to the particle which means it's in sync
		return true;
	}
};

template <typename THandle, typename T>
struct NoEntryInSync<THandle, T, false>
{
	static bool Helper(const THandle& Handle)
	{
		//nothing written so compare to zero
		T HeadVal;
		HeadVal.CopyFrom(Handle);
		return HeadVal == T::ZeroValue();
	}
};

struct FPropertyInterval
{
	FPropertyIdx Ref;
	FFrameAndPhase FrameAndPhase;
};

template <typename T, EParticleProperty PropName, bool bNoEntryIsHead = true>
class TParticlePropertyBuffer
{
public:
	explicit TParticlePropertyBuffer(int32 InCapacity)
	: Next(0)
	, NumValid(0)
	, Capacity(InCapacity)
	{
	}

	TParticlePropertyBuffer(TParticlePropertyBuffer<T, PropName>&& Other)
	: Next(Other.Next)
	, NumValid(Other.NumValid)
	, Capacity(Other.Capacity)
	, Buffer(MoveTemp(Other.Buffer))
	{
		Other.NumValid = 0;
		Other.Next = 0;
	}
	
	TParticlePropertyBuffer(const TParticlePropertyBuffer<T, PropName>& Other) = delete;

	~TParticlePropertyBuffer()
	{
		//Need to explicitly cleanup before destruction using Release (release back into the pool)
		ensure(Buffer.Num() == 0);
	}

	//Gets access into buffer in monotonically increasing FrameAndPhase order: x_{n+1} > x_n
	T& WriteAccessMonotonic(const FFrameAndPhase FrameAndPhase, FDirtyPropertiesPool& Manager)
	{
		return *WriteAccessImp<true>(FrameAndPhase, Manager);
	}

	//Gets access into buffer in non-decreasing FrameAndPhase order: x_{n+1} >= x_n
	//If x_{n+1} == x_n we return null to inform the user (usefull when a single phase can have multiple writes)
	T* WriteAccessNonDecreasing(const FFrameAndPhase FrameAndPhase, FDirtyPropertiesPool& Manager)
	{
		return WriteAccessImp<false>(FrameAndPhase, Manager);
	}

	//Searches in reverse order for interval that contains FrameAndPhase
	const T* Read(const FFrameAndPhase FrameAndPhase, const FDirtyPropertiesPool& Manager) const
	{
		const int32 Idx = FindIdx(FrameAndPhase);
		return Idx != INDEX_NONE ? &GetPool(Manager).GetElement(Buffer[Idx].Ref) : nullptr;
	}

	//Releases data back into the pool
	void Release(FDirtyPropertiesPool& Manager)
	{
		TPropertyPool<T>& Pool = GetPool(Manager);
		for(FPropertyInterval& Interval : Buffer)
	{
			Pool.RemoveElement(Interval.Ref);
	}
	
		Buffer.Empty();
		NumValid = 0;
	}
	
	void Reset()
	{
		NumValid = 0;
	}
	
	void ClearEntryAndFuture(const FFrameAndPhase FrameAndPhase)
	{
		//Move next backwards until FrameAndPhase and anything more future than it is gone
		while(NumValid)
		{
			const int32 PotentialNext = Next - 1 >= 0 ? Next - 1 : Buffer.Num() - 1;
	
			if(Buffer[PotentialNext].FrameAndPhase < FrameAndPhase)
	{
				break;
	}

			Next = PotentialNext;
			--NumValid;
		}
	}

	bool IsClean(const FFrameAndPhase FrameAndPhase) const
	{
		return FindIdx(FrameAndPhase) == INDEX_NONE;
	}
	
	template <typename THandle>
	bool IsInSync(const THandle& Handle, const FFrameAndPhase FrameAndPhase, const FDirtyPropertiesPool& Pool) const
	{
		if (const T* Val = Read(FrameAndPhase, Pool))
	{
			T HeadVal;
			HeadVal.CopyFrom(Handle);
			return *Val == HeadVal;
	}

		return NoEntryInSync<THandle, T, bNoEntryIsHead>::Helper(Handle);
	}

private:

	const int32 FindIdx(const FFrameAndPhase FrameAndPhase) const
	{
		int32 Cur = Next;	//go in reverse order because hopefully we don't rewind too far back
		int32 Result = INDEX_NONE;
		for(int32 Count = 0; Count < NumValid; ++Count)
		{
			--Cur;
			if (Cur < 0) { Cur = Buffer.Num() - 1; }

			const FPropertyInterval& Interval = Buffer[Cur];
			if(Interval.FrameAndPhase < FrameAndPhase)
{
				//no reason to keep searching, frame is bigger than everything before this
				break;
			}
			else
	{
				Result = Cur;
			}
	}

		if(bNoEntryIsHead || Result == INDEX_NONE)
	{
			//in this mode we consider the entire interval as one entry
			return Result;
	}
		else
	{
			//in this mode each interval just represents the frame the property was dirtied on
			//so in that case we have to check for equality
			return Buffer[Result].FrameAndPhase == FrameAndPhase ? Result : INDEX_NONE;
		}
	}

	TPropertyPool<T>& GetPool(FDirtyPropertiesPool& Manager) { return Manager.GetPool<T, PropName>(); }
	const TPropertyPool<T>& GetPool(const FDirtyPropertiesPool& Manager) const { return Manager.GetPool<T, PropName>(); }

	//Gets access into buffer in FrameAndPhase order.
	//It's assumed FrameAndPhase is monotonically increasing: x_{n+1} > x_n
	//If bEnsureMonotonic is true we will always return a valid access (unless assert fires)
	//If bEnsureMonotonic is false we will ensure x_{n+1} >= x_n. If x_{n+1} == x_n we return null to inform the user (can be useful when multiple writes happen in same phase)
	template <bool bEnsureMonotonic>
	T* WriteAccessImp(const FFrameAndPhase FrameAndPhase, FDirtyPropertiesPool& Manager)
	{
		if (NumValid)
		{
			const int32 Prev = Next == 0 ? Buffer.Num() - 1 : Next - 1;
			const FFrameAndPhase& LatestFrameAndPhase = Buffer[Prev].FrameAndPhase;
			if (bEnsureMonotonic)
			{
				ensure(LatestFrameAndPhase < FrameAndPhase);	//Must write in monotonic growing order so that x_{n+1} > x_n
		}
		else
		{
				ensure(LatestFrameAndPhase <= FrameAndPhase);	//Must write in growing order so that x_{n+1} >= x_n
				if (LatestFrameAndPhase == FrameAndPhase)
	{
					//Already wrote once for this FrameAndPhase so skip
					return nullptr;
		}
	}

			ValidateOrder();
	}

		T* Result;

		if (Next < Buffer.Num())
	{
			//reuse
			FPropertyInterval& Interval = Buffer[Next];
			Interval.FrameAndPhase = FrameAndPhase;
			Result = &GetPool(Manager).GetElement(Interval.Ref);
		}
		else
		{
			//no reuse yet so can just push
			FPropertyIdx NewIdx;
			Result = &GetPool(Manager).AddElement(NewIdx);
			Buffer.Add({NewIdx, FrameAndPhase });
		}

		++Next;
		if (Next == Capacity) { Next = 0; }

		NumValid = FMath::Min(++NumValid, Capacity);

		return Result;
	}

	void ValidateOrder()
	{
#if VALIDATE_REWIND_DATA
		int32 Val = Next;
		FFrameAndPhase PrevVal;
		for(int32 Count = 0; Count < NumValid; ++Count)
		{
			--Val;
			if (Val < 0) { Val = Buffer.Num() - 1; }
			if (Count == 0)
		{
				PrevVal = Buffer[Val].FrameAndPhase;
		}
		else
		{
				ensure(Buffer[Val].FrameAndPhase < PrevVal);
				PrevVal = Buffer[Val].FrameAndPhase;
			}
		}
#endif
	}

private:
	int32 Next;
	int32 NumValid;
	int32 Capacity;
	TArray<FPropertyInterval> Buffer;
};


enum EDesyncResult
{
	InSync, //both have entries and are identical, or both have no entries
	Desync, //both have entries but they are different
	NeedInfo //one of the entries is missing. Need more context to determine whether desynced
};

// Wraps FDirtyPropertiesManager and its DataIdx to avoid confusion between Source and offset Dest indices
struct FDirtyPropData
{
	FDirtyPropData(FDirtyPropertiesManager* InManager, int32 InDataIdx)
		: Ptr(InManager), DataIdx(InDataIdx) { }

	FDirtyPropertiesManager* Ptr;
	int32 DataIdx;
};

struct FConstDirtyPropData
{
	FConstDirtyPropData(const FDirtyPropertiesManager* InManager, int32 InDataIdx)
		: Ptr(InManager), DataIdx(InDataIdx) { }

	const FDirtyPropertiesManager* Ptr;
	int32 DataIdx;
};

template <typename T, EShapeProperty PropName>
class TPerShapeDataStateProperty
{
public:
	const T& Read() const
	{
		check(bSet);
		return Val;
	}

	void Write(const T& InVal)
	{
		bSet = true;
		Val = InVal;
	}

	bool IsSet() const
	{
		return bSet;
	}

private:
	T Val;
	bool bSet = false;
};

struct FPerShapeDataStateBase
{
	TPerShapeDataStateProperty<FCollisionData, EShapeProperty::CollisionData> CollisionData;
	TPerShapeDataStateProperty<FMaterialData, EShapeProperty::Materials> MaterialData;

	//helper functions for shape API
	template <typename TParticle>
	static const FCollisionFilterData& GetQueryData(const FPerShapeDataStateBase* State, const TParticle& Particle, int32 ShapeIdx) { return State && State->CollisionData.IsSet() ? State->CollisionData.Read().QueryData : Particle.ShapesArray()[ShapeIdx]->GetQueryData(); }
	/*const FCollisionFilterData& GetSimData() const { return CollisionData.Read().SimData; }
	
	TSerializablePtr<FImplicitObject> GetGeometry() const { return Geometry; }
	const TArray<FMaterialHandle>& GetMaterials() const { return Materials.Read().Materials; }
	const TArray<FMaterialMaskHandle>& GetMaterialMasks() const { return Materials.Read().MaterialMasks; }
	const TArray<uint32>& GetMaterialMaskMaps() const { return Materials.Read().MaterialMaskMaps; }
	const TArray<FMaterialHandle>& GetMaterialMaskMapMaterials() const { return Materials.Read().MaterialMaskMapMaterials; }*/
};

class FPerShapeDataState
{
public:
	FPerShapeDataState(const FPerShapeDataStateBase* InState, const FGeometryParticleHandle& InParticle, const int32 InShapeIdx)
	: State(InState)
	, Particle(InParticle)
	, ShapeIdx(InShapeIdx)
	{
	}

	const FCollisionFilterData& GetQueryData() const { return FPerShapeDataStateBase::GetQueryData(State, Particle, ShapeIdx); }
private:
	const FPerShapeDataStateBase* State;
	const FGeometryParticleHandle& Particle;
	const int32 ShapeIdx;

};

struct FShapesArrayStateBase
{
	TArray<FPerShapeDataStateBase> PerShapeData;

	FPerShapeDataStateBase& FindOrAdd(const int32 ShapeIdx)
	{
		if(ShapeIdx >= PerShapeData.Num())
		{
			const int32 NumNeededToAdd = ShapeIdx + 1 - PerShapeData.Num();
			PerShapeData.AddDefaulted(NumNeededToAdd);
		}
		return PerShapeData[ShapeIdx];

	}
};

template <typename TParticle>
class TShapesArrayState
{
public:
	TShapesArrayState(const TParticle& InParticle, const FShapesArrayStateBase* InState)
		: Particle(InParticle)
		, State(InState)
	{}

	FPerShapeDataState operator[](const int32 ShapeIdx) const { return FPerShapeDataState{ State && ShapeIdx < State->PerShapeData.Num() ? &State->PerShapeData[ShapeIdx] : nullptr, Particle, ShapeIdx }; }
private:
	const TParticle& Particle;
	const FShapesArrayStateBase* State;
};

class FGeometryParticleStateBase
{
public:
	explicit FGeometryParticleStateBase(int32 NumFrames)
	: ParticlePositionRotation(NumFrames * FFrameAndPhase::NumPhases)
	, NonFrequentData(NumFrames* FFrameAndPhase::NumPhases)
	, Velocities(NumFrames* FFrameAndPhase::NumPhases)
	, Dynamics(NumFrames * FFrameAndPhase::NumPhases)
	, DynamicsMisc(NumFrames* FFrameAndPhase::NumPhases)
	, MassProps(NumFrames* FFrameAndPhase::NumPhases)
	, KinematicTarget(NumFrames* FFrameAndPhase::NumPhases)
	{

	}

	FGeometryParticleStateBase(const FGeometryParticleStateBase& Other) = delete;
	FGeometryParticleStateBase(FGeometryParticleStateBase&& Other) = default;
	~FGeometryParticleStateBase() = default;

	void Release(FDirtyPropertiesPool& Manager)
	{
		ParticlePositionRotation.Release(Manager);
		NonFrequentData.Release(Manager);
		Velocities.Release(Manager);
		Dynamics.Release(Manager);
		DynamicsMisc.Release(Manager);
		MassProps.Release(Manager);
		KinematicTarget.Release(Manager);
	}

	void Reset()
	{
		ParticlePositionRotation.Reset();
		NonFrequentData.Reset();
		Velocities.Reset();
		Dynamics.Reset();
		DynamicsMisc.Reset();
		MassProps.Reset();
		KinematicTarget.Reset();
	}

	void ClearEntryAndFuture(const FFrameAndPhase FrameAndPhase)
	{
		ParticlePositionRotation.ClearEntryAndFuture(FrameAndPhase);
		NonFrequentData.ClearEntryAndFuture(FrameAndPhase);
		Velocities.ClearEntryAndFuture(FrameAndPhase);
		Dynamics.ClearEntryAndFuture(FrameAndPhase);
		DynamicsMisc.ClearEntryAndFuture(FrameAndPhase);
		MassProps.ClearEntryAndFuture(FrameAndPhase);
		KinematicTarget.ClearEntryAndFuture(FrameAndPhase);
	}

	bool IsClean(const FFrameAndPhase FrameAndPhase) const
	{
		return IsCleanExcludingDynamics(FrameAndPhase) && Dynamics.IsClean(FrameAndPhase);
	}

	bool IsCleanExcludingDynamics(const FFrameAndPhase FrameAndPhase) const
	{
		return ParticlePositionRotation.IsClean(FrameAndPhase) &&
			NonFrequentData.IsClean(FrameAndPhase) &&
			Velocities.IsClean(FrameAndPhase) &&
			DynamicsMisc.IsClean(FrameAndPhase) &&
			MassProps.IsClean(FrameAndPhase) &&
			KinematicTarget.IsClean(FrameAndPhase);
	}

	template <bool bSkipDynamics = false>
	bool IsInSync(const FGeometryParticleHandle& Handle, const FFrameAndPhase FrameAndPhase, const FDirtyPropertiesPool& Pool) const;

	template <typename TParticle>
	static const FVec3& X(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->ParticlePositionRotation.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->X() : Particle.X();
	}

	template <typename TParticle>
	static const FRotation3& R(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->ParticlePositionRotation.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->R() : Particle.R();
	}
	
	template <typename TParticle>
	static const FVec3& V(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->Velocities.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->V() : Particle.CastToKinematicParticle()->V();
	}

	template <typename TParticle>
	static const FVec3& W(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->Velocities.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->W() : Particle.CastToKinematicParticle()->W();
	}

	template <typename TParticle>
	static FReal LinearEtherDrag(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->DynamicsMisc.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->LinearEtherDrag() : Particle.CastToRigidParticle()->LinearEtherDrag();
	}

	template <typename TParticle>
	static FReal AngularEtherDrag(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->DynamicsMisc.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->AngularEtherDrag() : Particle.CastToRigidParticle()->AngularEtherDrag();
	}

	template <typename TParticle>
	static FReal MaxLinearSpeedSq(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->DynamicsMisc.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->MaxLinearSpeedSq() : Particle.CastToRigidParticle()->MaxLinearSpeedSq();
	}

	template <typename TParticle>
	static FReal MaxAngularSpeedSq(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->DynamicsMisc.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->MaxAngularSpeedSq() : Particle.CastToRigidParticle()->MaxAngularSpeedSq();
	}

	template <typename TParticle>
	static EObjectStateType ObjectState(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->DynamicsMisc.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->ObjectState() : Particle.CastToRigidParticle()->ObjectState();
	}

	template <typename TParticle>
	static bool GravityEnabled(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->DynamicsMisc.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->GravityEnabled() : Particle.CastToRigidParticle()->GravityEnabled();
	}

	template <typename TParticle>
	static bool CCDEnabled(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->DynamicsMisc.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->CCDEnabled() : Particle.CastToRigidParticle()->CCDEnabled();
	}

	template <typename TParticle>
	static int32 CollisionGroup(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->DynamicsMisc.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->CollisionGroup() : Particle.CastToRigidParticle()->CollisionGroup();
	}

	template <typename TParticle>
	static const FVec3& CenterOfMass(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->MassProps.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->CenterOfMass() : Particle.CastToRigidParticle()->CenterOfMass();
	}

	template <typename TParticle>
	static const FRotation3& RotationOfMass(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->MassProps.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->RotationOfMass() : Particle.CastToRigidParticle()->RotationOfMass();
	}

	template <typename TParticle>
	static const FMatrix33& I(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->MassProps.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->I() : Particle.CastToRigidParticle()->I();
	}

	template <typename TParticle>
	static const FMatrix33& InvI(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->MassProps.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->InvI() : Particle.CastToRigidParticle()->InvI();
	}

	template <typename TParticle>
	static FReal M(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->MassProps.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->M() : Particle.CastToRigidParticle()->M();
	}

	template <typename TParticle>
	static FReal InvM(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->MassProps.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->InvM() : Particle.CastToRigidParticle()->InvM();
	}

	template <typename TParticle>
	static TSerializablePtr<FImplicitObject> Geometry(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->NonFrequentData.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->Geometry() : Particle.Geometry();
	}

	template <typename TParticle>
	static FUniqueIdx UniqueIdx(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->NonFrequentData.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->UniqueIdx() : Particle.UniqueIdx();
	}
	
	template <typename TParticle>
	static FSpatialAccelerationIdx SpatialIdx(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->NonFrequentData.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->SpatialIdx() : Particle.SpatialIdx();
	}

#if CHAOS_DEBUG_NAME
	template <typename TParticle>
	static const TSharedPtr<FString, ESPMode::ThreadSafe>& DebugName(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->NonFrequentData.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->DebugName() : Particle.DebugName();
	}
#endif

	template <typename TParticle>
	static const FVec3& F(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->Dynamics.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->F() : ZeroVector; //dynamics do not use delta writes, they always write their value immediately or it's 0
	}

	template <typename TParticle>
	static const FVec3& Torque(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->Dynamics.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->Torque() : ZeroVector; //dynamics do not use delta writes, they always write their value immediately or it's 0
	}

	template <typename TParticle>
	static const FVec3& LinearImpulse(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->Dynamics.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->LinearImpulse() : ZeroVector; //dynamics do not use delta writes, they always write their value immediately or it's 0
	}

	template <typename TParticle>
	static const FVec3& AngularImpulse(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager, const FFrameAndPhase FrameAndPhase)
	{
		const auto Data = State ? State->Dynamics.Read(FrameAndPhase, Manager) : nullptr;
		return Data ? Data->AngularImpulse() : ZeroVector; //dynamics do not use delta writes, they always write their value immediately or it's 0
	}

	template <typename TParticle>
	static TShapesArrayState<TParticle> ShapesArray(const FGeometryParticleStateBase* State, const TParticle& Particle)
	{
		return TShapesArrayState<TParticle>{ Particle, State ? &State->ShapesArrayState : nullptr };
	}

	void SyncSimWritablePropsFromSim(FDirtyPropData Manager,const TPBDRigidParticleHandle<FReal,3>& Rigid);
	void SyncDirtyDynamics(FDirtyPropData& DestManager,const FParticleDirtyData& Dirty,const FConstDirtyPropData& SrcManager);
	
private:

	TParticlePropertyBuffer<FParticlePositionRotation,EParticleProperty::XR> ParticlePositionRotation;
	TParticlePropertyBuffer<FParticleNonFrequentData,EParticleProperty::NonFrequentData> NonFrequentData;
	TParticlePropertyBuffer<FParticleVelocities,EParticleProperty::Velocities> Velocities;
	TParticlePropertyBuffer<FParticleDynamics,EParticleProperty::Dynamics, /*bNoEntryIsHead=*/false> Dynamics;
	TParticlePropertyBuffer<FParticleDynamicMisc,EParticleProperty::DynamicMisc> DynamicsMisc;
	TParticlePropertyBuffer<FParticleMassProps,EParticleProperty::MassProps> MassProps;
	TParticlePropertyBuffer<FKinematicTarget, EParticleProperty::KinematicTarget> KinematicTarget;

	FShapesArrayStateBase ShapesArrayState;

	CHAOS_API static FVec3 ZeroVector;

	friend class FRewindData;
};

class FGeometryParticleState
{
public:

	FGeometryParticleState(const FGeometryParticleHandle& InParticle, const FDirtyPropertiesPool& InPool)
	: Particle(InParticle)
	, Pool(InPool)
	, FrameAndPhase{0,0}
	{
	}

	FGeometryParticleState(const FGeometryParticleStateBase* InState, const FGeometryParticleHandle& InParticle, const FDirtyPropertiesPool& InPool, const FFrameAndPhase InFrameAndPhase)
	: Particle(InParticle)
	, Pool(InPool)
	, State(InState)
	, FrameAndPhase(InFrameAndPhase)
	{
	}

	const FVec3& X() const
	{
		return FGeometryParticleStateBase::X(State, Particle, Pool, FrameAndPhase);
	}

	const FRotation3& R() const
	{
		return FGeometryParticleStateBase::R(State, Particle, Pool, FrameAndPhase);
	}

	const FVec3& V() const
	{
		return FGeometryParticleStateBase::V(State, Particle, Pool, FrameAndPhase);
	}

	const FVec3& W() const
	{
		return FGeometryParticleStateBase::W(State, Particle, Pool, FrameAndPhase);
	}

	FReal LinearEtherDrag() const
	{
		return FGeometryParticleStateBase::LinearEtherDrag(State, Particle, Pool, FrameAndPhase);
	}

	FReal AngularEtherDrag() const
	{
		return FGeometryParticleStateBase::AngularEtherDrag(State, Particle, Pool, FrameAndPhase);
	}

	FReal MaxLinearSpeedSq() const
	{
		return FGeometryParticleStateBase::MaxLinearSpeedSq(State, Particle, Pool, FrameAndPhase);
	}

	FReal MaxAngularSpeedSq() const
	{
		return FGeometryParticleStateBase::MaxAngularSpeedSq(State, Particle, Pool, FrameAndPhase);
	}

	EObjectStateType ObjectState() const
	{
		return FGeometryParticleStateBase::ObjectState(State, Particle, Pool, FrameAndPhase);
	}

	bool GravityEnabled() const
	{
		return FGeometryParticleStateBase::GravityEnabled(State, Particle, Pool, FrameAndPhase);
	}

	bool CCDEnabled() const
	{
		return FGeometryParticleStateBase::CCDEnabled(State, Particle, Pool, FrameAndPhase);
	}

	int32 CollisionGroup() const
	{
		return FGeometryParticleStateBase::CollisionGroup(State, Particle, Pool, FrameAndPhase);
	}

	const FVec3& CenterOfMass() const
	{
		return FGeometryParticleStateBase::CenterOfMass(State, Particle, Pool, FrameAndPhase);
	}

	const FRotation3& RotationOfMass() const
	{
		return FGeometryParticleStateBase::RotationOfMass(State, Particle, Pool, FrameAndPhase);
	}

	const FMatrix33& I() const
	{
		return FGeometryParticleStateBase::I(State, Particle, Pool, FrameAndPhase);
	}

	const FMatrix33& InvI() const
	{
		return FGeometryParticleStateBase::InvI(State, Particle, Pool, FrameAndPhase);
	}

	FReal M() const
	{
		return FGeometryParticleStateBase::M(State, Particle, Pool, FrameAndPhase);
	}

	FReal InvM() const
	{
		return FGeometryParticleStateBase::InvM(State, Particle, Pool, FrameAndPhase);
	}

	TSerializablePtr<FImplicitObject> Geometry() const
	{
		return FGeometryParticleStateBase::Geometry(State, Particle, Pool, FrameAndPhase);
	}

	FUniqueIdx UniqueIdx() const
	{
		return FGeometryParticleStateBase::UniqueIdx(State, Particle, Pool, FrameAndPhase);
	}

	FSpatialAccelerationIdx SpatialIdx() const
	{
		return FGeometryParticleStateBase::SpatialIdx(State, Particle, Pool, FrameAndPhase);
	}

#if CHAOS_DEBUG_NAME
	const TSharedPtr<FString, ESPMode::ThreadSafe>& DebugName() const
	{
		return FGeometryParticleStateBase::DebugName(State, Particle, Pool, FrameAndPhase);
	}
#endif

	const FVec3& F() const
	{
		return FGeometryParticleStateBase::F(State, Particle, Pool, FrameAndPhase);
	}

	const FVec3& Torque() const
	{
		return FGeometryParticleStateBase::Torque(State, Particle, Pool, FrameAndPhase);
	}

	const FVec3& LinearImpulse() const
	{
		return FGeometryParticleStateBase::LinearImpulse(State, Particle, Pool, FrameAndPhase);
	}

	const FVec3& AngularImpulse() const
	{
		return FGeometryParticleStateBase::AngularImpulse(State, Particle, Pool, FrameAndPhase);
	}

	TShapesArrayState<FGeometryParticleHandle> ShapesArray() const
	{
		return FGeometryParticleStateBase::ShapesArray(State, Particle);
	}

	const FGeometryParticleHandle& GetHandle() const
	{
		return Particle;
	}

	void SetState(const FGeometryParticleStateBase* InState)
	{
		State = InState;
	}

private:
	const FGeometryParticleHandle& Particle;
	const FDirtyPropertiesPool& Pool;
	const FGeometryParticleStateBase* State = nullptr;
	const FFrameAndPhase FrameAndPhase;
	};

extern CHAOS_API int32 EnableResimCache;

class FPBDRigidsSolver;

class FRewindData
{
public:
	FRewindData(FPBDRigidsSolver* InSolver, int32 NumFrames, bool InResimOptimization, int32 InCurrentFrame)
	: Managers(NumFrames+1)	//give 1 extra for saving at head
	, Solver(InSolver)
	, CurFrame(InCurrentFrame)
	, LatestFrame(-1)
	, Buffer(0)
	, FramesSaved(0)
	, DataIdxOffset(0)
	, bNeedsSave(false)
	, bResimOptimization(InResimOptimization)
	{
	}

	int32 Capacity() const { return Managers.Capacity(); }
	int32 CurrentFrame() const { return CurFrame; }
	int32 GetFramesSaved() const { return FramesSaved; }

	FReal GetDeltaTimeForFrame(int32 Frame) const
	{
		ensure(Managers[Frame].FrameCreatedFor == Frame);
		return Managers[Frame].DeltaTime;
	}

	void CHAOS_API RemoveParticle(const FUniqueIdx UniqueIdx);

	int32 CHAOS_API GetEarliestFrame_Internal() const { return CurFrame - FramesSaved; }

	/* Query the state of particles from the past. Once a rewind happens state captured must be queried using GetFutureStateAtFrame */
	FGeometryParticleState CHAOS_API GetPastStateAtFrame(const FGeometryParticleHandle& Handle,int32 Frame, FFrameAndPhase::EParticleHistoryPhase Phase = FFrameAndPhase::EParticleHistoryPhase::PostPushData) const;

	IResimCacheBase* GetCurrentStepResimCache() const
	{
		return !!EnableResimCache && bResimOptimization ? Managers[CurFrame].ExternalResimCache.Get() : nullptr;
	}

	template <typename CreateCache>
	void AdvanceFrame(FReal DeltaTime, const CreateCache& CreateCacheFunc)
	{
		QUICK_SCOPE_CYCLE_COUNTER(RewindDataAdvance);
		Managers[CurFrame].DeltaTime = DeltaTime;
		Managers[CurFrame].FrameCreatedFor = CurFrame;
		TUniquePtr<IResimCacheBase>& ResimCache = Managers[CurFrame].ExternalResimCache;

		if(bResimOptimization)
		{
			if(IsResim())
			{
				if(ResimCache)
				{
					ResimCache->SetResimming(true);
				}
			}
			else
			{
				if(ResimCache)
				{
					ResimCache->ResetCache();
				} else
				{
					ResimCache = CreateCacheFunc();
				}
				ResimCache->SetResimming(false);
			}
		}
		else
		{
			ResimCache.Reset();
		}

		AdvanceFrameImp(ResimCache.Get());
	}

	void FinishFrame();

	bool IsResim() const
	{
		return CurFrame < LatestFrame;
	}

	bool IsFinalResim() const
	{
		return (CurFrame + 1) == LatestFrame;
	}

	//Number of particles that we're currently storing history for
	int32 GetNumDirtyParticles() const { return AllDirtyParticles.Num(); }

	template <bool bResim>
	void PushGTDirtyData(const FDirtyPropertiesManager& SrcManager,const int32 SrcDataIdx,const FDirtyProxy& Dirty, const FShapeDirtyData* ShapeDirtyData);

	template <bool bResim>
	void PushPTDirtyData(TPBDRigidParticleHandle<FReal,3>& Rigid,const int32 SrcDataIdx);

	void CHAOS_API MarkDirtyFromPT(FGeometryParticleHandle& Handle);

	void CHAOS_API SpawnProxyIfNeeded(FSingleParticlePhysicsProxy& Proxy);

private:

	friend class FPBDRigidsSolver;

	struct FSimWritableState
	{
		template <bool bResim>
		bool SyncSimWritablePropsFromSim(const TPBDRigidParticleHandle<FReal,3>& Rigid, const int32 Frame);
		void SyncToParticle(TPBDRigidParticleHandle<FReal,3>& Rigid) const;

		bool IsSimWritableDesynced(const TPBDRigidParticleHandle<FReal,3>& Rigid) const;
		
		const FVec3& X() const { return MX; }
		const FQuat& R() const { return MR; }
		const FVec3& V() const { return MV; }
		const FVec3& W() const { return MW; }

		int32 FrameRecordedHack = INDEX_NONE;

	private:
		FVec3 MX;
		FQuat MR;
		FVec3 MV;
		FVec3 MW;
	};

	struct FDirtyFrameInfo
	{
		int32 Frame;	//needed to protect against stale entries in circular buffer
		uint8 Wave;

		void SetWave(int32 InFrame, uint8 InWave)
		{
			Frame = InFrame;
			Wave = InWave;
		}

		bool MissingWrite(int32 InFrame, uint8 InWave) const
		{
			//If this is not a stale entry and it was written to, but not during this latest sim
			return (Wave != 0 && Frame == InFrame) && Wave != InWave;
		}
	};

	void CHAOS_API AdvanceFrameImp(IResimCacheBase* ResimCache);

	struct FFrameManagerInfo
	{
		TUniquePtr<IResimCacheBase> ExternalResimCache;

		//Note that this is not exactly the same as which frame this manager represents. 
		//A manager can have data for two frames at once, the important part is just knowing which frame it was created on so we know whether the physics data can rely on it
		//Consider the case where nothing is dirty from GT and then an object moves from the simulation, in that case it needs a manager to record the data into
		int32 FrameCreatedFor = INDEX_NONE;
		FReal DeltaTime;
	};

	struct FDirtyParticleInfo
	{
	private:
		FGeometryParticleStateBase History;
		TGeometryParticleHandle<FReal,3>* PTParticle;
		FDirtyPropertiesPool* PropertiesPool;
	public:
		FUniqueIdx CachedUniqueIdx;	//Needed when manipulating on physics thread and Particle data cannot be read
		int32 LastDirtyFrame;	//Track how recently this was made dirty
		int32 InitializedOnStep = INDEX_NONE;	//if not INDEX_NONE, it indicates we saw initialization during rewind history window
		int32 DirtyDynamics = INDEX_NONE;
		bool bResimAsSlave = false;	//Indicates the particle will always resim in the exact same way from game thread data

		FDirtyParticleInfo(FDirtyPropertiesPool& InPropertiesPool, TGeometryParticleHandle<FReal,3>& InPTParticle, const FUniqueIdx UniqueIdx,const int32 CurFrame,const int32 NumFrames)
		: History(NumFrames)
		, PTParticle(&InPTParticle)
		, PropertiesPool(&InPropertiesPool)
		, CachedUniqueIdx(UniqueIdx)
		, LastDirtyFrame(CurFrame)
		{
		}

		FDirtyParticleInfo(FDirtyParticleInfo&& Other)
		: History(MoveTemp(Other.History))
			, PTParticle(Other.PTParticle)
			, PropertiesPool(Other.PropertiesPool)
			, CachedUniqueIdx(MoveTemp(Other.CachedUniqueIdx))
			, LastDirtyFrame(Other.LastDirtyFrame)
			, InitializedOnStep(Other.InitializedOnStep)
		, bResimAsSlave(Other.bResimAsSlave)
		{
			Other.PropertiesPool = nullptr;
		}

		~FDirtyParticleInfo();
		
		TGeometryParticleHandle<FReal,3>* GetPTParticle() const
		{
			return PTParticle;
		}

		FDirtyParticleInfo(const FDirtyParticleInfo& Other) = delete;

		FGeometryParticleStateBase& AddFrame(const int32 Frame)
		{
			LastDirtyFrame = Frame;
			return History;
		}
		
		void ClearPhaseAndFuture(const FFrameAndPhase FrameAndPhase)
		{
			History.ClearEntryAndFuture(FrameAndPhase);
		}

		const FGeometryParticleStateBase& GetHistory() const	//For non-const access use AddFrame
		{
			return History;
		}
	};

	const FDirtyParticleInfo& FindParticleChecked(const FUniqueIdx UniqueIdx) const
	{
		const int32 Idx = ParticleToAllDirtyIdx.FindChecked(UniqueIdx);
		return AllDirtyParticles[Idx];
	}

	FDirtyParticleInfo& FindParticleChecked(const FUniqueIdx UniqueIdx)
	{
		const int32 Idx = ParticleToAllDirtyIdx.FindChecked(UniqueIdx);
		return AllDirtyParticles[Idx];
	}

	const FDirtyParticleInfo* FindParticle(const FUniqueIdx UniqueIdx) const
	{
		if(const int32* Idx = ParticleToAllDirtyIdx.Find(UniqueIdx))
		{
			return &AllDirtyParticles[*Idx];
		}

		return nullptr;
	}

	FDirtyParticleInfo* FindParticle(const FUniqueIdx UniqueIdx)
	{
		if(const int32* Idx = ParticleToAllDirtyIdx.Find(UniqueIdx))
		{
			return &AllDirtyParticles[*Idx];
		}

		return nullptr;
	}

	FDirtyParticleInfo& FindOrAddParticle(TGeometryParticleHandle<FReal,3>& PTParticle, const int32 InitializedOnFrame = INDEX_NONE);
	bool RewindToFrame(int32 Frame);


	TArrayAsMap<FUniqueIdx,int32> ParticleToAllDirtyIdx;
	TCircularBuffer<FFrameManagerInfo> Managers;
	FDirtyPropertiesPool PropertiesPool;	//must come before AllDirtyParticles since it relies on it (and used in destruction)
	TArray<FDirtyParticleInfo> AllDirtyParticles;

	FPBDRigidsSolver* Solver;
	int32 CurFrame;
	int32 LatestFrame;
	uint8 Buffer;	//Used to flip flop between resim buffer and current buffer. Since resimmed becomes current we just use a double buffer
	int32 FramesSaved;
	int32 DataIdxOffset;
	bool bNeedsSave;	//Indicates that some data is pointing at head and requires saving before a rewind
	bool bResimOptimization;

	uint8 CurrentBuffer() const { return Buffer; }
	uint8 OriginalBufferIdx() const { return 1 - Buffer; }

	bool IsResimAndInSync(const FGeometryParticleHandle& Handle) const { return IsResim() && Handle.SyncState() == ESyncState::InSync; }

	template <bool bSkipDynamics = false>
	void DesyncIfNecessary(FDirtyParticleInfo& Info, const FFrameAndPhase FrameAndPhase);

};

/** Used by user code to determine when rewind should occur and gives it the opportunity to record any additional data */
class IRewindCallback
{
public:
	virtual ~IRewindCallback() = default;
	/** Called before any sim callbacks are triggered but after physics data has marshalled over
	*   This means brand new physics particles are already created for example, and any pending game thread modifications have happened
	*   See ISimCallbackObject for recording inputs to callbacks associated with this PhysicsStep */
	virtual void ProcessInputs_Internal(int32 PhysicsStep, const TArray<FSimCallbackInputAndObject>& SimCallbackInputs){}

	/** Called before any inputs are marshalled over to the physics thread.
	*	The physics state has not been applied yet, and cannot be inspected anyway because this is triggered from the external thread (game thread)
	*	Gives user the ability to modify inputs or record them - this can help with reducing latency if you want to act on inputs immediately
	*/
	virtual void ProcessInputs_External(int32 PhysicsStep, const TArray<FSimCallbackInputAndObject>& SimCallbackInputs) {}

	/** Called after sim step to give the option to rewind. Any pending inputs for the next frame will remain in the queue
	*   Return the PhysicsStep to start resimulating from. Resim will run up until latest step passed into RecordInputs (i.e. latest physics sim simulated so far)
	*   Return INDEX_NONE to indicate no rewind
	*/
	virtual int32 TriggerRewindIfNeeded_Internal(int32 LatestStepCompleted) { return INDEX_NONE; }

	/** Called before each rewind step. This is to give user code the opportunity to trigger other code before each rewind step
	*   Usually to simulate external systems that ran in lock step with the physics sim
	*/
	virtual void PreResimStep_Internal(int32 PhysicsStep, bool bFirstStep){}

	/** Called after each rewind step. This is to give user code the opportunity to trigger other code after each rewind step
	*   Usually to simulate external systems that ran in lock step with the physics sim
	*/
	virtual void PostResimStep_Internal(int32 PhysicsStep){}

	virtual void RegisterRewindableSimCallback_Internal(ISimCallbackObject* Callback) { ensure(false); }
};
}
