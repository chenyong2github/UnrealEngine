// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Core.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Containers/CircularBuffer.h"
#include "Chaos/ResimCacheBase.h"

namespace Chaos
{

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

template <typename T,EParticleProperty PropName>
class TParticleStateProperty
{
public:
	
	TParticleStateProperty()
		: Manager(nullptr, INDEX_NONE)
	{
	}

	TParticleStateProperty(const FDirtyPropData& InManager)
		: Manager(InManager)
	{
	}

	const T& Read() const
	{
		const TDirtyElementPool<T>& Pool = Manager.Ptr->GetParticlePool<T,PropName>();
		return Pool.GetElement(Manager.DataIdx);
	}
	
	template <typename LambdaWrite>
	void SyncToParticle(const LambdaWrite& WriteFunc) const;
	
	template <typename LambdaSet>
	void SyncRemoteDataForced(const FDirtyPropData& InManager,const LambdaSet& SetFunc);
	
	template <typename LambdaSet>
	void SyncRemoteData(const FDirtyPropData& InManager,const FParticleDirtyData& DirtyData,const LambdaSet& SetFunc);
	
	bool IsSet() const
	{
		return Manager.Ptr != nullptr;
	}

	template <typename TParticleHandle>
	bool IsInSync(const FConstDirtyPropData& SrcManager,const FParticleDirtyFlags Flags,const TParticleHandle& Handle) const;

private:
	FDirtyPropData Manager;
	
	static const T& GetValue(const FDirtyPropertiesManager* Ptr, int32 DataIdx)
	{
		return Ptr->GetParticlePool<T,PropName>().GetElement(DataIdx);
	}
};


template <typename T, EParticleProperty PropName>
class TParticleStateProperty2
{
public:
	void SetRefFrom(const TParticleStateProperty2& Other, FDirtyPropertiesPool& Manager)
	{
		Ref.SetRefFrom(Other.Ref, GetPool(Manager));
	}

	const T& ReadChecked(const FDirtyPropertiesPool& Manager) const
	{
		return GetPool(Manager).GetElement(Ref);
	}

	const T* Read(const FDirtyPropertiesPool& Manager) const
	{
		return Ref.IsSet() ? &GetPool(Manager).GetElement(Ref) : nullptr;
	}

	void Write(FDirtyPropertiesPool& Manager, const T& InVal)
	{
		TPropertyPool<T>& Pool = GetPool(Manager);
		if(Ref.IsSet())
		{
			Pool.GetElement(Ref) = InVal;
		}
		else
		{
			Pool.AddElement(InVal, Ref);
		}
	}

	bool IsSet() const
	{
		return Ref.IsSet();
	}

private:
	TPropertyRef<T> Ref;

	TPropertyPool<T>& GetPool(FDirtyPropertiesPool& Manager) { return Manager.GetPool<T, PropName>(); }
	const TPropertyPool<T>& GetPool(const FDirtyPropertiesPool& Manager) const { return Manager.GetPool<T, PropName>(); }
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

	bool IsClean() const
	{
		return !ParticlePositionRotation.IsSet() &&
			!NonFrequentData.IsSet() &&
			!Velocities.IsSet() &&
			!Dynamics.IsSet() &&
			!DynamicsMisc.IsSet() &&
			!MassProps.IsSet() &&
			!KinematicTarget.IsSet();
	}

	void SetRefFrom(const FGeometryParticleStateBase& Other, FDirtyPropertiesPool& Manager)
	{
		ParticlePositionRotation.SetRefFrom(Other.ParticlePositionRotation, Manager);
		NonFrequentData.SetRefFrom(Other.NonFrequentData, Manager);
		Velocities.SetRefFrom(Other.Velocities, Manager);
		Dynamics.SetRefFrom(Other.Dynamics, Manager);
		DynamicsMisc.SetRefFrom(Other.DynamicsMisc, Manager);
		MassProps.SetRefFrom(Other.MassProps, Manager);
		KinematicTarget.SetRefFrom(Other.KinematicTarget, Manager);
	}

	void RecordPreDirtyData(const FDirtyProxy& Dirty, const FShapeDirtyData* ShapeDirtyData, FDirtyPropertiesPool& Manager);
	void RecordAnyDirty(const FGeometryParticleHandle& Handle, FDirtyPropertiesPool& Manager, const FGeometryParticleStateBase& OldState);
	void CopyToParticle(FGeometryParticleHandle& Handle, FDirtyPropertiesPool& Manager);
	void MarkAllDirty(const FGeometryParticleHandle& Handle, FDirtyPropertiesPool& Manager);
	void RecordDynamics(const FParticleDynamics& Dynamics, FDirtyPropertiesPool& Manager);
	void RecordSimResults(const FPBDRigidParticleHandle& Handle, FDirtyPropertiesPool& Manager);

	template <typename TParticle>
	static const FVec3& X(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->ParticlePositionRotation.IsSet() ? State->ParticlePositionRotation.ReadChecked(Manager).X() : Particle.X();
	}

	template <typename TParticle>
	static const FRotation3& R(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->ParticlePositionRotation.IsSet() ? State->ParticlePositionRotation.ReadChecked(Manager).R() : Particle.R();
	}
	
	template <typename TParticle>
	static const FVec3& V(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->Velocities.IsSet() ? State->Velocities.ReadChecked(Manager).V() : Particle.CastToKinematicParticle()->V();
	}

	template <typename TParticle>
	static const FVec3& W(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->Velocities.IsSet() ? State->Velocities.ReadChecked(Manager).W() : Particle.CastToKinematicParticle()->W();
	}

	template <typename TParticle>
	static FReal LinearEtherDrag(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->DynamicsMisc.IsSet() ? State->DynamicsMisc.ReadChecked(Manager).LinearEtherDrag() : Particle.CastToRigidParticle()->LinearEtherDrag();
	}

	template <typename TParticle>
	static FReal AngularEtherDrag(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->DynamicsMisc.IsSet() ? State->DynamicsMisc.ReadChecked(Manager).AngularEtherDrag() : Particle.CastToRigidParticle()->AngularEtherDrag();
	}

	template <typename TParticle>
	static FReal MaxLinearSpeedSq(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->DynamicsMisc.IsSet() ? State->DynamicsMisc.ReadChecked(Manager).MaxLinearSpeedSq() : Particle.CastToRigidParticle()->MaxLinearSpeedSq();
	}

	template <typename TParticle>
	static FReal MaxAngularSpeedSq(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->DynamicsMisc.IsSet() ? State->DynamicsMisc.ReadChecked(Manager).MaxAngularSpeedSq() : Particle.CastToRigidParticle()->MaxAngularSpeedSq();
	}
	template <typename TParticle>
	static EObjectStateType ObjectState(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->DynamicsMisc.IsSet() ? State->DynamicsMisc.ReadChecked(Manager).ObjectState() : Particle.CastToRigidParticle()->ObjectState();
	}

	template <typename TParticle>
	static bool GravityEnabled(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->DynamicsMisc.IsSet() ? State->DynamicsMisc.ReadChecked(Manager).GravityEnabled() : Particle.CastToRigidParticle()->GravityEnabled();
	}

	template <typename TParticle>
	static bool CCDEnabled(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->DynamicsMisc.IsSet() ? State->DynamicsMisc.ReadChecked(Manager).CCDEnabled() : Particle.CastToRigidParticle()->CCDEnabled();
	}

	template <typename TParticle>
	static int32 CollisionGroup(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->DynamicsMisc.IsSet() ? State->DynamicsMisc.ReadChecked(Manager).CollisionGroup() : Particle.CastToRigidParticle()->CollisionGroup();
	}

	template <typename TParticle>
	static const FVec3& CenterOfMass(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->MassProps.IsSet() ? State->MassProps.ReadChecked(Manager).CenterOfMass() : Particle.CastToRigidParticle()->CenterOfMass();
	}

	template <typename TParticle>
	static const FRotation3& RotationOfMass(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->MassProps.IsSet() ? State->MassProps.ReadChecked(Manager).RotationOfMass() : Particle.CastToRigidParticle()->RotationOfMass();
	}

	template <typename TParticle>
	static const FMatrix33& I(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->MassProps.IsSet() ? State->MassProps.ReadChecked(Manager).I() : Particle.CastToRigidParticle()->I();
	}

	template <typename TParticle>
	static const FMatrix33& InvI(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->MassProps.IsSet() ? State->MassProps.ReadChecked(Manager).InvI() : Particle.CastToRigidParticle()->InvI();
	}

	template <typename TParticle>
	static FReal M(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->MassProps.IsSet() ? State->MassProps.ReadChecked(Manager).M() : Particle.CastToRigidParticle()->M();
	}

	template <typename TParticle>
	static FReal InvM(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->MassProps.IsSet() ? State->MassProps.ReadChecked(Manager).InvM() : Particle.CastToRigidParticle()->InvM();
	}

	template <typename TParticle>
	static TSerializablePtr<FImplicitObject> Geometry(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->NonFrequentData.IsSet() ? State->NonFrequentData.ReadChecked(Manager).Geometry() : Particle.Geometry();
	}

	template <typename TParticle>
	static FUniqueIdx UniqueIdx(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->NonFrequentData.IsSet() ? State->NonFrequentData.ReadChecked(Manager).UniqueIdx() : Particle.UniqueIdx();
	}
	
	template <typename TParticle>
	static FSpatialAccelerationIdx SpatialIdx(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->NonFrequentData.IsSet() ? State->NonFrequentData.ReadChecked(Manager).SpatialIdx() : Particle.SpatialIdx();
	}

#if CHAOS_CHECKED
	template <typename TParticle>
	static FName DebugName(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->NonFrequentData.IsSet() ? State->NonFrequentData.ReadChecked(Manager).DebugName() : Particle.DebugName();
	}
#endif

	template <typename TParticle>
	static const FVec3& F(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->Dynamics.IsSet() ? State->Dynamics.ReadChecked(Manager).F() : Particle.CastToRigidParticle()->F();
	}

	template <typename TParticle>
	static const FVec3& Torque(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->Dynamics.IsSet() ? State->Dynamics.ReadChecked(Manager).Torque() : Particle.CastToRigidParticle()->Torque();
	}

	template <typename TParticle>
	static const FVec3& LinearImpulse(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->Dynamics.IsSet() ? State->Dynamics.ReadChecked(Manager).LinearImpulse() : Particle.CastToRigidParticle()->LinearImpulse();
	}

	template <typename TParticle>
	static const FVec3& AngularImpulse(const FGeometryParticleStateBase* State, const TParticle& Particle, const FDirtyPropertiesPool& Manager)
	{
		return State && State->Dynamics.IsSet() ? State->Dynamics.ReadChecked(Manager).AngularImpulse() : Particle.CastToRigidParticle()->AngularImpulse();
	}

	template <typename TParticle>
	static TShapesArrayState<TParticle> ShapesArray(const FGeometryParticleStateBase* State, const TParticle& Particle)
	{
		return TShapesArrayState<TParticle>{ Particle, State ? &State->ShapesArrayState : nullptr };
	}

	void SyncSimWritablePropsFromSim(FDirtyPropData Manager,const TPBDRigidParticleHandle<FReal,3>& Rigid);
	void SyncDirtyDynamics(FDirtyPropData& DestManager,const FParticleDirtyData& Dirty,const FConstDirtyPropData& SrcManager);
	bool IsSimWritableDesynced(TPBDRigidParticleHandle<FReal,3>& Particle) const;
	
	template <typename TParticle>
	void SyncToParticle(TParticle& Particle, const FDirtyPropertiesPool& Manager) const;
	void SyncPrevFrame(FDirtyPropData& Manager,const FDirtyProxy& Dirty, const FShapeDirtyData* ShapeData);
	void SyncIfDirty(const FDirtyPropData& Manager,const FGeometryParticleHandle& InParticle,const FGeometryParticleStateBase& RewindState);
	bool CoalesceState(const FGeometryParticleStateBase& LatestState, FDirtyPropertiesPool& Manager);
	bool IsDesynced(const FConstDirtyPropData& SrcManager,const TGeometryParticleHandle<FReal,3>& Handle,const FParticleDirtyFlags Flags) const;

private:

	TParticleStateProperty2<FParticlePositionRotation,EParticleProperty::XR> ParticlePositionRotation;
	TParticleStateProperty2<FParticleNonFrequentData,EParticleProperty::NonFrequentData> NonFrequentData;
	TParticleStateProperty2<FParticleVelocities,EParticleProperty::Velocities> Velocities;
	TParticleStateProperty2<FParticleDynamics,EParticleProperty::Dynamics> Dynamics;
	TParticleStateProperty2<FParticleDynamicMisc,EParticleProperty::DynamicMisc> DynamicsMisc;
	TParticleStateProperty2<FParticleMassProps,EParticleProperty::MassProps> MassProps;
	TParticleStateProperty2<FKinematicTarget, EParticleProperty::KinematicTarget> KinematicTarget;

	FShapesArrayStateBase ShapesArrayState;
};

class FGeometryParticleState
{
public:

	FGeometryParticleState(const FGeometryParticleHandle& InParticle, const FDirtyPropertiesPool& InPool)
	: Particle(InParticle)
	, Pool(InPool)
	{
	}

	FGeometryParticleState(const FGeometryParticleStateBase* InState, const FGeometryParticleHandle& InParticle, const FDirtyPropertiesPool& InPool)
	: Particle(InParticle)
	, Pool(InPool)
	, State(InState)
	{
	}

	const FVec3& X() const
	{
		return FGeometryParticleStateBase::X(State, Particle, Pool);
	}

	const FRotation3& R() const
	{
		return FGeometryParticleStateBase::R(State, Particle, Pool);
	}

	const FVec3& V() const
	{
		return FGeometryParticleStateBase::V(State, Particle, Pool);
	}

	const FVec3& W() const
	{
		return FGeometryParticleStateBase::W(State, Particle, Pool);
	}

	FReal LinearEtherDrag() const
	{
		return FGeometryParticleStateBase::LinearEtherDrag(State, Particle, Pool);
	}

	FReal AngularEtherDrag() const
	{
		return FGeometryParticleStateBase::AngularEtherDrag(State, Particle, Pool);
	}

	FReal MaxLinearSpeedSq() const
	{
		return FGeometryParticleStateBase::MaxLinearSpeedSq(State, Particle, Pool);
	}

	FReal MaxAngularSpeedSq() const
	{
		return FGeometryParticleStateBase::MaxAngularSpeedSq(State, Particle, Pool);
	}

	EObjectStateType ObjectState() const
	{
		return FGeometryParticleStateBase::ObjectState(State, Particle, Pool);
	}

	bool GravityEnabled() const
	{
		return FGeometryParticleStateBase::GravityEnabled(State, Particle, Pool);
	}

	bool CCDEnabled() const
	{
		return FGeometryParticleStateBase::CCDEnabled(State, Particle, Pool);
	}

	int32 CollisionGroup() const
	{
		return FGeometryParticleStateBase::CollisionGroup(State, Particle, Pool);
	}

	const FVec3& CenterOfMass() const
	{
		return FGeometryParticleStateBase::CenterOfMass(State, Particle, Pool);
	}

	const FRotation3& RotationOfMass() const
	{
		return FGeometryParticleStateBase::RotationOfMass(State, Particle, Pool);
	}

	const FMatrix33& I() const
	{
		return FGeometryParticleStateBase::I(State, Particle, Pool);
	}

	const FMatrix33& InvI() const
	{
		return FGeometryParticleStateBase::InvI(State, Particle, Pool);
	}

	FReal M() const
	{
		return FGeometryParticleStateBase::M(State, Particle, Pool);
	}

	FReal InvM() const
	{
		return FGeometryParticleStateBase::InvM(State, Particle, Pool);
	}

	TSerializablePtr<FImplicitObject> Geometry() const
	{
		return FGeometryParticleStateBase::Geometry(State, Particle, Pool);
	}

	FUniqueIdx UniqueIdx() const
	{
		return FGeometryParticleStateBase::UniqueIdx(State, Particle, Pool);
	}

	FSpatialAccelerationIdx SpatialIdx() const
	{
		return FGeometryParticleStateBase::SpatialIdx(State, Particle, Pool);
	}

#if CHAOS_CHECKED
	FName DebugName() const
	{
		return FGeometryParticleStateBase::DebugName(State, Particle, Pool);
	}
#endif

	const FVec3& F() const
	{
		return FGeometryParticleStateBase::F(State, Particle, Pool);
	}

	const FVec3& Torque() const
	{
		return FGeometryParticleStateBase::Torque(State, Particle, Pool);
	}

	const FVec3& LinearImpulse() const
	{
		return FGeometryParticleStateBase::LinearImpulse(State, Particle, Pool);
	}

	const FVec3& AngularImpulse() const
	{
		return FGeometryParticleStateBase::AngularImpulse(State, Particle, Pool);
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

	bool IsDesynced(const FConstDirtyPropData& SrcManager, const TGeometryParticleHandle<FReal,3>& Handle, const FParticleDirtyFlags Flags) const
	{
		return State->IsDesynced(SrcManager,Handle,Flags);
	}

private:
	const FGeometryParticleHandle& Particle;
	const FDirtyPropertiesPool& Pool;
	const FGeometryParticleStateBase* State = nullptr;
};

struct FParticleHistoryEntry
{
	enum EParticleHistoryPhase
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

	//Indicates which frame is associated with this entry (Needed for circular buffer storage)
	const int32 GetRecordedFrame(int32 Buffer) const { return RecordedFramePlusOne[Buffer] - 1; }

	FGeometryParticleStateBase* GetState(const EParticleHistoryPhase Phase, int32 Frame, int32 Buffer)
	{
		return Frame == GetRecordedFrame(Buffer) ? &Phases[Buffer][Phase] : nullptr;
	}

	const FGeometryParticleStateBase* GetState(const EParticleHistoryPhase Phase, int32 Frame, int32 Buffer) const
	{
		return Frame == GetRecordedFrame(Buffer) ? &Phases[Buffer][Phase] : nullptr;
	}

	FGeometryParticleStateBase& GetStateChecked(const EParticleHistoryPhase Phase, int32 Frame, int32 Buffer)
	{
		check(Frame == GetRecordedFrame(Buffer));
		return Phases[Buffer][Phase];
	}

	const FGeometryParticleStateBase& GetStateChecked(const EParticleHistoryPhase Phase, int32 Frame, int32 Buffer) const
	{
		check(Frame == GetRecordedFrame(Buffer));
		return Phases[Buffer][Phase];
	}

	void NewFrame(const int32 Frame, const int32 Buffer, FDirtyPropertiesPool& Manager)
	{
		Reset(Manager, Buffer);
		RecordedFramePlusOne[Buffer] = Frame + 1;
	}

	void NewFrameIfNeeded(const int32 Frame, const int32 Buffer, FDirtyPropertiesPool& Manager)
	{
		if(GetRecordedFrame(Buffer) != Frame)
		{
			NewFrame(Frame, Buffer, Manager);
		}
	}

	bool CoalesceState(const FGeometryParticleStateBase& LatestState, FDirtyPropertiesPool& Manager, const EParticleHistoryPhase LatestPhase, const int32 Buffer)
	{
		//go from latest state to first
		bool bContinueToCoalesce = true;
		for(int32 Phase = LatestPhase; Phase >= 0 && bContinueToCoalesce; --Phase)
		{
			bContinueToCoalesce = Phases[Buffer][Phase].CoalesceState(LatestState, Manager);
		}

		return bContinueToCoalesce;
	}

	void Reset(FDirtyPropertiesPool& Manager, const int32 Buffer)
	{
		for(FGeometryParticleStateBase& State : Phases[Buffer])
		{
			State.SetRefFrom(FGeometryParticleStateBase(), Manager);
		}
		RecordedFramePlusOne[Buffer] = 0;
	}

	void ResetAll(FDirtyPropertiesPool& Manager)
	{
		Reset(Manager, 0);
		Reset(Manager, 1);
	}

private:

	//Stores the different state of the particle in different phases of the step
	FGeometryParticleStateBase Phases[2][EParticleHistoryPhase::NumPhases];

	//Storing as RecordedFramePlusOne because storage is 0 initialized so we don't want to imply frame 0 was recorded
	int32 RecordedFramePlusOne[2] = { 0,0 };
};

enum class EFutureQueryResult
{
	Ok,	//There is reliable data for this particle
	Untracked, //The particle is untracked. This could mean it's new, or that it was unchanged in prior simulations
	Desync //The particle's state has diverged from the previous recordings
};

struct FDesyncedParticleInfo
{
	FGeometryParticleHandle* Particle;
	ESyncState MostDesynced;	//Indicates the most desynced this particle got during resim (could be that it was soft desync and then went back to normal)
};

class FPBDRigidsSolver;

class FRewindData
{
public:
	FRewindData(FPBDRigidsSolver* InSolver, int32 NumFrames, bool InResimOptimization, int32 InCurrentFrame)
	: Managers(NumFrames+1)	//give 1 extra for saving at head
	, Solver(InSolver)
	, CurFrame(InCurrentFrame)
	, LatestFrame(-1)
	, CurWave(1)
	, Buffer(0)
	, FramesSaved(0)
	, DataIdxOffset(0)
	, bNeedsSave(false)
	, bResimOptimization(false)
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

	bool CHAOS_API RewindToFrame(int32 Frame);

	void CHAOS_API RemoveParticle(const FUniqueIdx UniqueIdx);

	TArray<FDesyncedParticleInfo> CHAOS_API ComputeDesyncInfo() const;

	/* Query the state of particles from the past. Once a rewind happens state captured must be queried using GetFutureStateAtFrame */
	FGeometryParticleState CHAOS_API GetPastStateAtFrame(const FGeometryParticleHandle& Handle,int32 Frame) const;

	/* Query the state of particles in the future. This operation can fail for particles that are desynced or that we have not been tracking */
	EFutureQueryResult CHAOS_API GetFutureStateAtFrame(FGeometryParticleState& OutState,int32 Frame) const;

	IResimCacheBase* GetCurrentStepResimCache() const
	{
		return bResimOptimization ? Managers[CurFrame].ExternalResimCache.Get() : nullptr;
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

	void FlipBufferIfNeeded();

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

private:

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

	void CoalesceBack(TCircularBuffer<FParticleHistoryEntry>& Frames, const FParticleHistoryEntry::EParticleHistoryPhase LatestPhase);
	
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
		TCircularBuffer<FParticleHistoryEntry> Frames;
		TCircularBuffer<FDirtyFrameInfo> GTDirtyOnFrame;
	private:
		TGeometryParticleHandle<FReal,3>* PTParticle;
		FDirtyPropertiesPool* PropertiesPool;
	public:
		FUniqueIdx CachedUniqueIdx;	//Needed when manipulating on physics thread and Particle data cannot be read
		int32 LastDirtyFrame;	//Track how recently this was made dirty
		int32 InitializedOnStep = INDEX_NONE;	//if not INDEX_NONE, it indicates we saw initialization during rewind history window
		bool bDesync;
		ESyncState MostDesynced;	//Tracks the most desynced this has become (soft desync can go back to being clean, but we still want to know)

		FDirtyParticleInfo(FDirtyPropertiesPool& InPropertiesPool, TGeometryParticleHandle<FReal,3>& InPTParticle, const FUniqueIdx UniqueIdx,const int32 CurFrame,const int32 NumFrames)
		: Frames(NumFrames)
		, GTDirtyOnFrame(NumFrames)
		, PTParticle(&InPTParticle)
		, PropertiesPool(&InPropertiesPool)
		, CachedUniqueIdx(UniqueIdx)
		, LastDirtyFrame(CurFrame)
		, bDesync(true)
		, MostDesynced(ESyncState::HardDesync)
		{

		}

		FDirtyParticleInfo(FDirtyParticleInfo&& Other)
			: Frames(MoveTemp(Other.Frames))
			, GTDirtyOnFrame(MoveTemp(Other.GTDirtyOnFrame))
			, PTParticle(Other.PTParticle)
			, PropertiesPool(Other.PropertiesPool)
			, CachedUniqueIdx(MoveTemp(Other.CachedUniqueIdx))
			, LastDirtyFrame(Other.LastDirtyFrame)
			, InitializedOnStep(Other.InitializedOnStep)
			, bDesync(Other.bDesync)
			, MostDesynced(Other.MostDesynced)
		{
			Other.PropertiesPool = nullptr;
		}

		~FDirtyParticleInfo();
		
		TGeometryParticleHandle<FReal,3>* GetPTParticle() const
		{
			return PTParticle;
		}

		FParticleHistoryEntry& AddFrame(int32 FrameIdx, int32 InBuffer, FDirtyPropertiesPool& Manager);
		
		void Desync(int32 StartDesync,int32 LastFrame);

		FDirtyParticleInfo(const FDirtyParticleInfo& Other) = delete;
	};


	const FGeometryParticleStateBase* GetStateAtFrameImp(const FDirtyParticleInfo& Info,int32 Frame) const;
	
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

	TArrayAsMap<FUniqueIdx,int32> ParticleToAllDirtyIdx;
	TCircularBuffer<FFrameManagerInfo> Managers;
	FDirtyPropertiesPool PropertiesPool;	//must come before AllDirtyParticles since it relies on it (and used in destruction)
	TArray<FDirtyParticleInfo> AllDirtyParticles;
	FPBDRigidsSolver* Solver;
	int32 CurFrame;
	int32 LatestFrame;
	uint8 CurWave;
	uint8 Buffer;	//Used to flip flop between resim buffer and current buffer. Since resimmed becomes current we just use a double buffer
	bool bNeedBufferFlip = false;
	int32 FramesSaved;
	int32 DataIdxOffset;
	bool bNeedsSave;	//Indicates that some data is pointing at head and requires saving before a rewind
	bool bResimOptimization;
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
