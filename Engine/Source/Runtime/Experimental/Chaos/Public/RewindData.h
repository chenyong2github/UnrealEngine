// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Core.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
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

class FGeometryParticleStateBase
{
public:
	template <typename TParticle>
	const FVec3& X(const TParticle& Particle) const
	{
		return ParticlePositionRotation.IsSet() ? ParticlePositionRotation.Read().X() : Particle.X();
	}

	template <typename TParticle>
	const FRotation3& R(const TParticle& Particle) const
	{
		return ParticlePositionRotation.IsSet() ? ParticlePositionRotation.Read().R() : Particle.R();
	}
	
	template <typename TParticle>
	const FVec3& V(const TParticle& Particle) const
	{
		return Velocities.IsSet() ? Velocities.Read().V() : Particle.CastToKinematicParticle()->V();
	}

	template <typename TParticle>
	const FVec3& W(const TParticle& Particle) const
	{
		return Velocities.IsSet() ? Velocities.Read().W() : Particle.CastToKinematicParticle()->W();
	}

	template <typename TParticle>
	FReal LinearEtherDrag(const TParticle& Particle) const
	{
		return DynamicsMisc.IsSet() ? DynamicsMisc.Read().LinearEtherDrag() : Particle.CastToRigidParticle()->LinearEtherDrag();
	}

	template <typename TParticle>
	FReal AngularEtherDrag(const TParticle& Particle) const
	{
		return DynamicsMisc.IsSet() ? DynamicsMisc.Read().AngularEtherDrag() : Particle.CastToRigidParticle()->AngularEtherDrag();
	}

	template <typename TParticle>
	EObjectStateType ObjectState(const TParticle& Particle) const
	{
		return DynamicsMisc.IsSet() ? DynamicsMisc.Read().ObjectState() : Particle.CastToRigidParticle()->ObjectState();
	}

	template <typename TParticle>
	bool GravityEnabled(const TParticle& Particle) const
	{
		return DynamicsMisc.IsSet() ? DynamicsMisc.Read().GravityEnabled() : Particle.CastToRigidParticle()->GravityEnabled();
	}

	template <typename TParticle>
	bool CCDEnabled(const TParticle& Particle) const
	{
		return DynamicsMisc.IsSet() ? DynamicsMisc.Read().CCDEnabled() : Particle.CastToRigidParticle()->CCDEnabled();
	}

	template <typename TParticle>
	int32 CollisionGroup(const TParticle& Particle) const
	{
		return DynamicsMisc.IsSet() ? DynamicsMisc.Read().CollisionGroup() : Particle.CastToRigidParticle()->CollisionGroup();
	}

	template <typename TParticle>
	const FVec3& CenterOfMass(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().CenterOfMass() : Particle.CastToRigidParticle()->CenterOfMass();
	}

	template <typename TParticle>
	const FRotation3& RotationOfMass(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().RotationOfMass() : Particle.CastToRigidParticle()->RotationOfMass();
	}

	template <typename TParticle>
	const FMatrix33& I(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().I() : Particle.CastToRigidParticle()->I();
	}

	template <typename TParticle>
	const FMatrix33& InvI(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().InvI() : Particle.CastToRigidParticle()->InvI();
	}

	template <typename TParticle>
	FReal M(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().M() : Particle.CastToRigidParticle()->M();
	}

	template <typename TParticle>
	FReal InvM(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().InvM() : Particle.CastToRigidParticle()->InvM();
	}

	template <typename TParticle>
	TSerializablePtr<FImplicitObject> Geometry(const TParticle& Particle) const
	{
		return NonFrequentData.IsSet() ? MakeSerializable(NonFrequentData.Read().Geometry()) : Particle.Geometry();
	}

	template <typename TParticle>
	FUniqueIdx UniqueIdx(const TParticle& Particle) const
	{
		return NonFrequentData.IsSet() ? NonFrequentData.Read().UniqueIdx() : Particle.UniqueIdx();
	}
	
	template <typename TParticle>
	FSpatialAccelerationIdx SpatialIdx(const TParticle& Particle) const
	{
		return NonFrequentData.IsSet() ? NonFrequentData.Read().SpatialIdx() : Particle.SpatialIdx();
	}

#if CHAOS_CHECKED
	template <typename TParticle>
	FName DebugName(const TParticle& Particle) const
	{
		return NonFrequentData.IsSet() ? NonFrequentData.Read().DebugName() : Particle.DebugName();
	}
#endif

	template <typename TParticle>
	const FVec3& F(const TParticle& Particle) const
	{
		return Dynamics.IsSet() ? Dynamics.Read().F() : Particle.CastToRigidParticle()->F();
	}

	template <typename TParticle>
	const FVec3& Torque(const TParticle& Particle) const
	{
		return Dynamics.IsSet() ? Dynamics.Read().Torque() : Particle.CastToRigidParticle()->Torque();
	}

	template <typename TParticle>
	const FVec3& LinearImpulse(const TParticle& Particle) const
	{
		return Dynamics.IsSet() ? Dynamics.Read().LinearImpulse() : Particle.CastToRigidParticle()->LinearImpulse();
	}

	template <typename TParticle>
	const FVec3& AngularImpulse(const TParticle& Particle) const
	{
		return Dynamics.IsSet() ? Dynamics.Read().AngularImpulse() : Particle.CastToRigidParticle()->AngularImpulse();
	}

	void SyncSimWritablePropsFromSim(FDirtyPropData Manager,const TPBDRigidParticleHandle<FReal,3>& Rigid);
	void SyncDirtyDynamics(FDirtyPropData& DestManager,const FParticleDirtyData& Dirty,const FConstDirtyPropData& SrcManager);
	bool IsSimWritableDesynced(TPBDRigidParticleHandle<FReal,3>& Particle) const;
	
	template <typename TParticle>
	void SyncToParticle(TParticle& Particle) const;
	void SyncPrevFrame(FDirtyPropData& Manager,const FDirtyProxy& Dirty);
	void SyncIfDirty(const FDirtyPropData& Manager,const FGeometryParticleHandle& InParticle,const FGeometryParticleStateBase& RewindState);
	bool CoalesceState(const FGeometryParticleStateBase& LatestState);
	bool IsDesynced(const FConstDirtyPropData& SrcManager,const TGeometryParticleHandle<FReal,3>& Handle,const FParticleDirtyFlags Flags) const;

private:

	TParticleStateProperty<FParticlePositionRotation,EParticleProperty::XR> ParticlePositionRotation;
	TParticleStateProperty<FParticleNonFrequentData,EParticleProperty::NonFrequentData> NonFrequentData;
	TParticleStateProperty<FParticleVelocities,EParticleProperty::Velocities> Velocities;
	TParticleStateProperty<FParticleDynamics,EParticleProperty::Dynamics> Dynamics;
	TParticleStateProperty<FParticleDynamicMisc,EParticleProperty::DynamicMisc> DynamicsMisc;
	TParticleStateProperty<FParticleMassProps,EParticleProperty::MassProps> MassProps;
	TParticleStateProperty<FKinematicTarget, EParticleProperty::KinematicTarget> KinematicTarget;
};

class FGeometryParticleState
{
public:

	FGeometryParticleState(const FGeometryParticleHandle& InParticle)
	: Particle(InParticle)
	{
	}

	FGeometryParticleState(const FGeometryParticleStateBase& InState, const FGeometryParticleHandle& InParticle)
	: Particle(InParticle)
	, State(InState)
	{
	}

	const FVec3& X() const
	{
		return State.X(Particle);
	}

	const FRotation3& R() const
	{
		return State.R(Particle);
	}

	const FVec3& V() const
	{
		return State.V(Particle);
	}

	const FVec3& W() const
	{
		return State.W(Particle);
	}

	FReal LinearEtherDrag() const
	{
		return State.LinearEtherDrag(Particle);
	}

	FReal AngularEtherDrag() const
	{
		return State.AngularEtherDrag(Particle);
	}

	EObjectStateType ObjectState() const
	{
		return State.ObjectState(Particle);
	}

	bool GravityEnabled() const
	{
		return State.GravityEnabled(Particle);
	}

	bool CCDEnabled() const
	{
		return State.CCDEnabled(Particle);
	}

	int32 CollisionGroup() const
	{
		return State.CollisionGroup(Particle);
	}

	const FVec3& CenterOfMass() const
	{
		return State.CenterOfMass(Particle);
	}

	const FRotation3& RotationOfMass() const
	{
		return State.RotationOfMass(Particle);
	}

	const FMatrix33& I() const
	{
		return State.I(Particle);
	}

	const FMatrix33& InvI() const
	{
		return State.InvI(Particle);
	}

	FReal M() const
	{
		return State.M(Particle);
	}

	FReal InvM() const
	{
		return State.InvM(Particle);
	}

	TSerializablePtr<FImplicitObject> Geometry() const
	{
		return State.Geometry(Particle);
	}

	FUniqueIdx UniqueIdx() const
	{
		return State.UniqueIdx(Particle);
	}

	FSpatialAccelerationIdx SpatialIdx() const
	{
		return State.SpatialIdx(Particle);
	}

#if CHAOS_CHECKED
	FName DebugName() const
	{
		return State.DebugName(Particle);
	}
#endif

	const FVec3& F() const
	{
		return State.F(Particle);
	}

	const FVec3& Torque() const
	{
		return State.Torque(Particle);
	}

	const FVec3& LinearImpulse() const
	{
		return State.LinearImpulse(Particle);
	}

	const FVec3& AngularImpulse() const
	{
		return State.AngularImpulse(Particle);
	}

	const FGeometryParticleHandle& GetHandle() const
	{
		return Particle;
	}

	void SetState(const FGeometryParticleStateBase& InState)
	{
		State = InState;
	}

	bool IsDesynced(const FConstDirtyPropData& SrcManager, const TGeometryParticleHandle<FReal,3>& Handle, const FParticleDirtyFlags Flags) const
	{
		return State.IsDesynced(SrcManager,Handle,Flags);
	}

private:
	const FGeometryParticleHandle& Particle;
	FGeometryParticleStateBase State;
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

class FRewindData
{
public:
	FRewindData(int32 NumFrames, bool InResimOptimization, int32 InCurrentFrame)
	: Managers(NumFrames+1)	//give 1 extra for saving at head
	, CurFrame(InCurrentFrame)
	, LatestFrame(-1)
	, CurWave(1)
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

	void PrepareFrame(int32 NumDirtyParticles);

	void PrepareFrameForPTDirty(int32 NumActiveParticles);
	template <bool bResim>
	void PushGTDirtyData(const FDirtyPropertiesManager& SrcManager,const int32 SrcDataIdx,const FDirtyProxy& Dirty);

	template <bool bResim>
	void PushPTDirtyData(TPBDRigidParticleHandle<FReal,3>& Rigid,const int32 SrcDataIdx);

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

	class FFrameInfo
	{
	public:
		FFrameInfo()
		: bSet(false)
		{

		}

		FGeometryParticleStateBase* GetState(int32 Frame)
		{
			return (bSet && Frame == RecordedFrame) ? &State : nullptr;
		}

		const FGeometryParticleStateBase* GetState(int32 Frame) const
		{
			return (bSet && Frame == RecordedFrame) ? &State : nullptr;
		}

		FGeometryParticleStateBase& GetStateChecked(int32 Frame)
		{
			check(bSet && Frame == RecordedFrame);
			return State;
		}

		const FGeometryParticleStateBase& GetStateChecked(int32 Frame) const
		{
			check(bSet && Frame == RecordedFrame);
			return State;
		}

		FSimWritableState* GetSimWritableState(int32 Frame)
		{
			return (bSet && Frame == RecordedFrame) ? &SimWritableState : nullptr;
		}

		const FSimWritableState* GetSimWritableState(int32 Frame) const
		{
			return (bSet && Frame == RecordedFrame) ? &SimWritableState : nullptr;
		}

		FSimWritableState& GetSimWritableStateChecked(int32 Frame)
		{
			check(bSet && Frame == RecordedFrame);
			return SimWritableState;
		}

		const FSimWritableState& GetSimWritableStateChecked(int32 Frame) const
		{
			check(bSet && Frame == RecordedFrame);
			return SimWritableState;
		}

		FGeometryParticleStateBase& NewState(int32 Frame)
		{
			RecordedFrame = Frame;
			bSet = true;
			State = FGeometryParticleStateBase();
			return State;
		}

		void ClearState()
		{
			bSet = false;
		}

	private:
		FGeometryParticleStateBase State;
		FSimWritableState SimWritableState;
		int32 RecordedFrame;
		bool bSet;
	};

	void CHAOS_API AdvanceFrameImp(IResimCacheBase* ResimCache);

	void CoalesceBack(TCircularBuffer<FFrameInfo>& Frames,int32 LatestIdx);
	
	struct FFrameManagerInfo
	{
		TUniquePtr<FDirtyPropertiesManager> Manager;
		TUniquePtr<IResimCacheBase> ExternalResimCache;

		//Note that this is not exactly the same as which frame this manager represents. 
		//A manager can have data for two frames at once, the important part is just knowing which frame it was created on so we know whether the physics data can rely on it
		//Consider the case where nothing is dirty from GT and then an object moves from the simulation, in that case it needs a manager to record the data into
		int32 FrameCreatedFor;
		FReal DeltaTime;
	};

	struct FDirtyParticleInfo
	{
		TCircularBuffer<FFrameInfo> Frames;
		TCircularBuffer<FDirtyFrameInfo> GTDirtyOnFrame;
	private:
		TGeometryParticleHandle<FReal,3>* PTParticle;
	public:
		FUniqueIdx CachedUniqueIdx;	//Needed when manipulating on physics thread and Particle data cannot be read
		int32 LastDirtyFrame;	//Track how recently this was made dirty
		bool bDesync;
		ESyncState MostDesynced;	//Tracks the most desynced this has become (soft desync can go back to being clean, but we still want to know)

		FDirtyParticleInfo(FGeometryParticle& UnsafeGTParticle, TGeometryParticleHandle<FReal,3>& InPTParticle, const FUniqueIdx UniqueIdx,const int32 CurFrame,const int32 NumFrames)
		: Frames(NumFrames)
		, GTDirtyOnFrame(NumFrames)
		, PTParticle(&InPTParticle)
		, CachedUniqueIdx(UniqueIdx)
		, LastDirtyFrame(CurFrame)
		, bDesync(true)
		, MostDesynced(ESyncState::HardDesync)
		{

		}

		TGeometryParticleHandle<FReal,3>* GetPTParticle() const
		{
			return PTParticle;
		}

		FGeometryParticleStateBase& AddFrame(int32 FrameIdx);
		
		void Desync(int32 StartDesync,int32 LastFrame);
	};

	const FSimWritableState* GetSimWritableStateAtFrame(const FDirtyParticleInfo& Info, int32 Frame) const
	{
		const TCircularBuffer<FFrameInfo>& Frames = Info.Frames;
		return Frames[Frame].GetSimWritableState(Frame);
	}

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

	FDirtyParticleInfo& FindOrAddParticle(TGeometryParticleHandle<FReal,3>& PTParticle);
	
	TArrayAsMap<FUniqueIdx,int32> ParticleToAllDirtyIdx;
	TCircularBuffer<FFrameManagerInfo> Managers;
	TArray<FDirtyParticleInfo> AllDirtyParticles;
	int32 CurFrame;
	int32 LatestFrame;
	uint8 CurWave;
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
};
}
