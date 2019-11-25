// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "Engine/EngineTypes.h"
#include "NetworkedSimulationModelTime.h"

NETWORKPREDICTION_API DECLARE_LOG_CATEGORY_EXTERN(LogNetSimCues, Log, All);

#ifndef NETSIMCUE_TYPEID_TYPE
#define NETSIMCUE_TYPEID_TYPE uint8;
#endif

using FNetSimCueTypeId = NETSIMCUE_TYPEID_TYPE;

enum class ESimulationTickContext : uint8
{
	None			= 0,
	Authority		= 1 << 0,
	Predict			= 1 << 1,
	Resimulate		= 1 << 2,

	All				= (Authority | Predict | Resimulate),
};

enum class ENetSimCueReplicationTarget : uint8
{
	None,			// Do not replicate cue to anyone
	Interpolators,	// Only replicate to / accept on clients that are interpolating (not running the simulation themselves)
	All,			// Replicate to everyone
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	NetSimCue Traits: how to configure how a NetSimCue will dispatch within the NetworkedSimulationModel
// ----------------------------------------------------------------------------------------------------------------------------------------------

struct TNetSimCueTraitsBase
{
	// Who can Invoke this Cue in their simulation (if this test fails, the Invoke call is supressed locally)
	static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::All };

	// Does the cue replicate? (from authority). This will also determine if the cue needs to be saved locally for NetUnique tests (to avoid double playing)
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::All };

	// Does the cue support rollback callbacks (in contexts where applicable. e.g, when we might get rolled back (!Authority))
	static constexpr bool Rollbackable { true };
};

// Preset: non replicated cue that only plays during "latest" simulate. Will not be played during rewind/resimulate.
// Lightest weight cue. Best used for cosmetic, non critical events. Footsteps, impact effects, etc.
struct TNetSimCueTraits_Weak
{
	static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Authority | (uint8)ESimulationTickContext::Predict };
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::None };
	static constexpr bool Rollbackable { false };
};

// Preset: Replicated, non predicted. Only invoked on authority and will replicate to everyone else.
// Best for events that are critical that cannot be rolled back/undown and do not need to be predicted.
struct TNetSimCueTraits_ReplicatedNonPredicted
{
	static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Authority };
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::All };
	static constexpr bool Rollbackable { false };
};

// Preset: Replicated to interpolating proxies, predicted by autonomous proxy
// Best for events you want everyone to see but don't need to get perfect in the predicting cases: doesn't need to rollback and cheap on cpu (no unique tests on predicted path)
struct TNetSimCueTraits_ReplicatedXOrPredicted
{
	static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Authority | (uint8)ESimulationTickContext::Predict };
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::Interpolators };
	static constexpr bool Rollbackable { false };
};

// Preset: Invoked and replicated to all. Uniqueness testing to avoid double playing, rollbackable so that it can (re)play during resimulates
// Most expensive (bandwidth and CPU for uniqueness testing) and requires rollback callbacks to be implemented to be correct
struct TNetSimCueTraits_Strong
{
	static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::All };
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::All };
	static constexpr bool Rollbackable { true };
};

// Actual trait struct that we use to look up traits. User cues must specialize this
template<typename TCue>
struct TCueHandlerTraits : public TNetSimCueTraitsBase
{
};


// Traits for TNetSimCueDispatcher
struct NETWORKPREDICTION_API FCueDispatcherTraitsBase
{
	// Window for replicating a NetSimCue. That is, after a cue is invoked, it has ReplicationWindow time before it will be pruned.
	// If a client does not get a net update for the sim in this window, they will miss the event.
	static constexpr FNetworkSimTime ReplicationWindow = FNetworkSimTime::FromRealTimeMS(200);
};
template<typename T> struct TCueDispatcherTraits : public FCueDispatcherTraitsBase { };

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	Wrapper
// ----------------------------------------------------------------------------------------------------------------------------------------------

struct FNetSimCueWrapperBase
{
	virtual ~FNetSimCueWrapperBase() { }
	virtual void NetSerialize(FArchive& Ar) = 0;
	virtual bool NetUnique(const void* OtherCueData) const = 0;
	virtual void* CueData() const = 0;
	virtual ENetSimCueReplicationTarget GetReplicationTarget() const = 0;
	virtual bool Rollbackable() const = 0;
};

template<typename TCue>
struct TNetSimCueWrapper : FNetSimCueWrapperBase
{
	TNetSimCueWrapper() = default;

	template <typename... ArgsType>
	TNetSimCueWrapper(ArgsType&&... Args)
		: Instance( MoveTempIfPossible(Forward<ArgsType>(Args))... ) { }

	void NetSerialize(FArchive& Ar) override final
	{
		// Cue types must implement NetSerialize(FArchive& Ar)
		Instance.NetSerialize(Ar);
	}

	bool NetUnique(const void* OtherCueData) const override final
	{
		// Cue types must implement bool NetUnique(const TMyCueType& Other) const
		return Instance.NetUnique(*((const TCue*)OtherCueData));
	}

	void* CueData() const override final
	{
		return (void*)&Instance;
	}

	ENetSimCueReplicationTarget GetReplicationTarget() const override final
	{
		return TCueHandlerTraits<TCue>::ReplicationTarget;
	}

	bool Rollbackable() const override final
	{
		return TCueHandlerTraits<TCue>::Rollbackable;
	}

	TCue Instance;
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	Callbacks
// ----------------------------------------------------------------------------------------------------------------------------------------------

struct FNetSimCueCallbacks
{
	/** Callback to rollback any side effects of the cue. */
	DECLARE_MULTICAST_DELEGATE(FOnRollback)
	FOnRollback	OnRollback;
};

/** System parameters for NetSimCue events */
struct FNetSimCueSystemParamemters
{
	// How much simulation time has passed since this cue was invoked. This will be 0 in authority/predict contexts, but when invoked via replication this will tell you how long ago it happened, relative to local simulation time.
	const FNetworkSimTime& TimeSinceInvocation;

	// Callback structure if applicable. This will be null on non-rewindable cues as well as execution contexts where rollbacks wont happen (e.g, authority).
	FNetSimCueCallbacks* Callbacks;	
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
// GlobalCueTypeTable: Cue types register with this to get a Type ID assigned. That ID is used in net serialization to talk about types.
// ----------------------------------------------------------------------------------------------------------------------------------------------
class FGlobalCueTypeTable
{
public:

	NETWORKPREDICTION_API static FGlobalCueTypeTable& Get()
	{
		return Singleton;
	}

	template<typename TCue>
	void RegisterType(const FString& TypeName)
	{
		ensure(!bFinalized);

		FTypeInfo TypeInfo = 
		{
			&TCue::ID, 
			[](){ return new TNetSimCueWrapper<TCue>();},
			TypeName
		};
		PendingCueTypes.Emplace(TypeInfo);
	}

	void FinalizeTypes()
	{
		ensure(!bFinalized);
		bFinalized = true;

		PendingCueTypes.Sort([](const FTypeInfo& LHS, const FTypeInfo& RHS) { return LHS.TypeName < RHS.TypeName; });
		int32 ID=0;
		for (auto& TypeInfo: PendingCueTypes)
		{
			check(TypeInfo.IDPtr != nullptr);
			*TypeInfo.IDPtr = ++ID;
			TypeInfoMap.Add(*TypeInfo.IDPtr) = TypeInfo;
		}
		for (auto& Func : PostFinalizeCallbacks)
		{
			Func();
		}
	}
	
	bool HasBeenFinalized() const { return bFinalized; }
	void AddPostFinalizedCallback(TFunction<void()> Callback) { PostFinalizeCallbacks.Emplace(Callback); }

	// ---------------------------

	FNetSimCueWrapperBase* Allocate(FNetSimCueTypeId ID)
	{
		return TypeInfoMap.FindChecked(ID).AllocateFunc();
	}

	FString GetTypeName(FNetSimCueTypeId ID) const
	{
		return TypeInfoMap.FindChecked(ID).TypeName;
	}

private:

	struct FTypeInfo
	{
		FNetSimCueTypeId* IDPtr = nullptr;
		TFunction<FNetSimCueWrapperBase*()> AllocateFunc;
		FString TypeName;
	};

	NETWORKPREDICTION_API static FGlobalCueTypeTable Singleton;

	TArray<TFunction<void()>> PostFinalizeCallbacks;
	TArray<FTypeInfo> PendingCueTypes;
	bool bFinalized = false;

	TMap<FNetSimCueTypeId, FTypeInfo> TypeInfoMap;
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
// SavedCue: a recorded Invocation of a NetSimCue
// ----------------------------------------------------------------------------------------------------------------------------------------------

struct FSavedCue
{
	FSavedCue(bool bInNetConfirmed) : bNetConfirmed(bInNetConfirmed) {}

	FSavedCue(const FSavedCue&) = delete;
	FSavedCue& operator=(const FSavedCue&) = delete;

	FSavedCue(FSavedCue&&) = default;
	FSavedCue& operator=(FSavedCue&&) = default;
	
	FSavedCue(const FNetSimCueTypeId& InId, const FNetworkSimTime& InTime, const bool& bInAllowRollback, const bool& bInNetConfirmed, const bool& bInResimulates, FNetSimCueWrapperBase* Cue)
		: ID(InId), Time(InTime), CueInstance(Cue), bAllowRollback(bInAllowRollback), bNetConfirmed(bInNetConfirmed), bResimulates(bInResimulates)
	{
		
	}
	
	void NetSerialize(FArchive& Ar)
	{
		if (Ar.IsSaving())
		{
			check(CueInstance.IsValid());
			Ar << ID;
			CueInstance->NetSerialize(Ar);

			Time.NetSerialize(Ar);
		}
		else
		{
			Ar << ID;
			CueInstance.Reset(FGlobalCueTypeTable::Get().Allocate(ID));
			CueInstance->NetSerialize(Ar);

			Time.NetSerialize(Ar);
		}
	}

	// Test NetUniqueness against another saved cue
	bool NetUnique(FSavedCue& OtherCue) const
	{
		if (ID != OtherCue.ID)
		{
			return false;
		}

		return CueInstance->NetUnique(OtherCue.CueInstance->CueData());
	}

	// Test NetUniqueness against an actual cue instance
	template<typename TCue>
	bool NetUnique(TCue& OtherCue) const
	{
		if (ID != TCue::ID)
		{
			return false;
		}
		return CueInstance->NetUnique(&OtherCue);
	}


	FString GetTypeName() const
	{
		return FGlobalCueTypeTable::Get().GetTypeName(ID);
	}

	FNetSimCueTypeId ID = 0;
	FNetworkSimTime Time;
	TUniquePtr<FNetSimCueWrapperBase> CueInstance;
	FNetSimCueCallbacks Callbacks;

	bool bDispatched = false;		// Cue has been dispatched to the local handler. Never dispatch twice.
	bool bAllowRollback = false;	// Cue supports rolling back. ie., we should pass the user valid FNetSimCueCallbacks rollback callback (note this is "this saved cue" specifically, not "this cue type". E.g, on authority, bAllowRollback is always false)
	bool bNetConfirmed = false;		// This cue has been net confirmed, meaning we received it directly via replication or we received a replicated cue that matched this one that was locally predicted.
	
	bool bResimulates = false;				 // Whether this cue supports invocation during resimulation. Needed to set bResimulatePendingRollback
	bool bPendingResimulateRollback = false; // Rollback is pending due to resimulation (unless CUe is matched with an Invocation during the resimulate)
};


// ----------------------------------------------------------------------------------------------------------------------------------------------
// Per-Receiver dispatch table. This is how we go from a serialized ID to a function call
// ----------------------------------------------------------------------------------------------------------------------------------------------
template<typename TCueHandler>
class TCueDispatchTable
{
public:
	static TCueDispatchTable<TCueHandler>& Get()
	{
		return Singleton;
	}

	// All types that the receiver can handle must be registered here. This is where we create the TFunction to call ::HandleCue
	template<typename TCue>
	void RegisterType()
	{
		// Registeration has to be deferred until the FGlobalCueTypeTable is ready
		auto Register = [this]() {
			check(TCue::ID != 0);

			FCueTypeInfo& CueTypeInfo = CueTypeInfoMap.Add(TCue::ID);

			// The actual Dispatch func that gets invoked
			CueTypeInfo.Dispatch = [](FNetSimCueWrapperBase* Cue, TCueHandler& Handler, const FNetSimCueSystemParamemters& SystemParameters)
			{
				// If you are finding compile errors here, you may be missing a ::HandleCue implementation for a specific cue type that your handler has registered with
				Handler.HandleCue( *static_cast<const TCue*>(Cue->CueData()), SystemParameters );
			};
		};
		
		if (!FGlobalCueTypeTable::Get().HasBeenFinalized())
		{
			FGlobalCueTypeTable::Get().AddPostFinalizedCallback(Register);
		}
		else
		{
			Register();
		}
	}

	void Dispatch(FSavedCue& SavedCue, TCueHandler& Handler, const FNetworkSimTime& DispatchTime)
	{
		if (FCueTypeInfo* TypeInfo = CueTypeInfoMap.Find(SavedCue.ID))
		{
			check(TypeInfo->Dispatch);
			TypeInfo->Dispatch(SavedCue.CueInstance.Get(), Handler, {DispatchTime - SavedCue.Time, SavedCue.bAllowRollback ? &SavedCue.Callbacks : nullptr });
		}
	}

private:

	static TCueDispatchTable<TCueHandler> Singleton;

	struct FCueTypeInfo
	{
		TFunction<void(FNetSimCueWrapperBase* Cue, TCueHandler& Handler, const FNetSimCueSystemParamemters& SystemParameters)> Dispatch;
	};

	TMap<FNetSimCueTypeId, FCueTypeInfo> CueTypeInfoMap;
};

template<typename TCueHandler>
TCueDispatchTable<TCueHandler> TCueDispatchTable<TCueHandler>::Singleton;

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//	CueDispatcher
//	-Entry point for invoking cues during a SimulationTick
//	-Holds recorded cue state for replication
// ------------------------------------------------------------------------------------------------------------------------------------------------------------

// Non-templated, "networking model independent" base: this is what the pure simulation code gets to invoke cues. 
struct FNetSimCueDispatcher
{
	// Invoke - this is how to invoke a cue from simulation code. This will construct the CueType T emplace in the saved cue record.
	// 
	// Best way to call:
	//	Invoke<FMyCue>(a, b, c); // a, b, c are constructor parameters
	//
	// This works too, but will cause a move (if possible) or copy
	//	FMyCue MyCue(a,b,c);
	//	Invoke<FMyCue>(MyCue);	

	template<typename T, typename... ArgsType>
	void Invoke(ArgsType&&... Args)
	{
		if (EnsureValidContext())
		{
			if ((TCueHandlerTraits<T>::InvokeMask & (uint8)Context.TickContext) > 0)
			{
				/*
				{
					// Whether we go in the transient list. Transient cues are dumped after dispatching (not saved over multiple frames for uniqueness comparisons during Invoke or NetSerialize)
					const bool bTransient = (Context.TickContext == ESimulationTickContext::Authority && TCueHandlerTraits<T>::ReplicationTarget == ENetSimCueReplicationTarget::None)	  // Transient on authority if it doesn't replicate
						|| ((TCueHandlerTraits<T>::InvokeMask & (uint8)ESimulationTickContext::Resimulate) == 0 && TCueHandlerTraits<T>::ReplicationTarget != ENetSimCueReplicationTarget::All); // Transient elsewhere if it won't replicate to us and it won't be invoked during resimulates

					// Whether this cue should be dispatched with rollback callbacks. This is a trait of the cue type + context (e.g, authority will never rollback)
					const bool bAllowRollback = TCueHandlerTraits<T>::Rollbackable && !bTransient && Context.TickContext != ESimulationTickContext::Authority;

					// Is this already confirmed? (it is if we can't roll it back, or if it won't be replicated to us)
					const bool bNetConfirmed = !bAllowRollback || TCueHandlerTraits<T>::ReplicationTarget != ENetSimCueReplicationTarget::All;
				}


				{
					// Whether this cue should be dispatched with rollback callbacks. Authority never rolls back, clients rollback if they plan to invoke during resims or if the cue is replicated to everyone
					const bool bRequiresRollbackForReplication = Context.TickContext != ESimulationTickContext::Authority && (TCueHandlerTraits<T>::ReplicationTarget == ENetSimCueReplicationTarget::All);
					const bool bRequiresRollbackForResimulate = Context.TickContext != ESimulationTickContext::Authority && (TCueHandlerTraits<T>::InvokeMask & (uint8)ESimulationTickContext::Resimulate);
					const bool bAllowRollback = bRequiresRollbackForReplication || bRequiresRollbackForResimulate;

					// Whether we go in the transient list. Transient cues are dumped after dispatching (not saved over multiple frames for uniqueness comparisons during Invoke or NetSerialize)
					const bool bTransient = !bAllowRollback &&
						(Context.TickContext == ESimulationTickContext::Authority && (TCueHandlerTraits<T>::ReplicationTarget == ENetSimCueReplicationTarget::None)) ||
						(Context.TickContext != ESimulationTickContext::Authority && (TCueHandlerTraits<T>::ReplicationTarget != ENetSimCueReplicationTarget::All));

					const bool bNetConfirmed = !bAllowRollback || TCueHandlerTraits<T>::ReplicationTarget != ENetSimCueReplicationTarget::All;
				}
				*/

				const bool bSupportsResimulate = (TCueHandlerTraits<T>::InvokeMask & (uint8)ESimulationTickContext::Resimulate) > 0;

				bool bAllowRollback = false;	// Whether this cue should be dispatched with rollback callbacks.
				bool bTransient = false;		// Whether we go in the transient list. Transient cues are dumped after dispatching (not saved over multiple frames for uniqueness comparisons during Invoke or NetSerialize)
				bool bNetConfirmed = false;		// Is this already confirmed? (we should not look to undo it if we don't get it confirmed from the server)
				if (Context.TickContext == ESimulationTickContext::Authority)
				{
					// Authority: never rolls back, is already confirmed, and can treat cue as transient if it doesn't have to replicate it
					bAllowRollback = false;
					bTransient = TCueHandlerTraits<T>::ReplicationTarget == ENetSimCueReplicationTarget::None;
					bNetConfirmed = true;
				}
				else
				{
					// Everyone else that is running the simulation (since this in ::Invoke which is called from within the simulation)
					// Rollback if it will replicate to us or if we plan to invoke this cue during resimulates. Transient and confirmed follow directly from this in the non authority case.
					bAllowRollback = (TCueHandlerTraits<T>::ReplicationTarget == ENetSimCueReplicationTarget::All) || bSupportsResimulate;
					bTransient = !bAllowRollback;
					bNetConfirmed = !bAllowRollback;
				}

				ensure(!(bTransient && bAllowRollback)); // this combination cannot happen: we can't be transient and support rollback (but we can be transient without supporting rollback)
				ensure(!(bNetConfirmed && bAllowRollback)); // a confirmed cue shouldn't be rolled back.

				// In resimulate case, we have to see if we already predicted it
				if (Context.TickContext == ESimulationTickContext::Resimulate)
				{
					ensure(RollbackTime.IsPositive() && RollbackTime <= Context.CurrentSimTime);
					
					// Since we haven't constructed the cue yet, we can't test for uniqueness!
					// So, create one on the stack. If we let it through we can move it to the appropriate buffer
					// (not as nice as the non resimulate path, but better than allocating a new FSavedCue+TNetSimCueWrapper and then removing them)
					T NewCue(MoveTempIfPossible(Forward<ArgsType>(Args))...);

					for (FSavedCue& ExistingCue : SavedCues)
					{
						if (RollbackTime <= ExistingCue.Time && ExistingCue.NetUnique(NewCue) == false)
						{
							// We've matched with an already predicted cue, so suppress this invocation and don't undo the predicted cue
							ExistingCue.bPendingResimulateRollback = false;
							UE_LOG(LogNetSimCues, Log, TEXT("Resimulated Cue %s matched existing cue. Suppressing Invocation."), *FGlobalCueTypeTable::Get().GetTypeName(T::ID));
							return;
						}
					}

					UE_LOG(LogNetSimCues, Log, TEXT("Invoking Cue %s during resimulate. Context: %d. Transient: %d. bAllowRollback: %d. bNetConfirmed: %d."), *FGlobalCueTypeTable::Get().GetTypeName(T::ID), Context.TickContext, bTransient, bAllowRollback, bNetConfirmed);
					GetBuffer(bTransient).Emplace(T::ID, Context.CurrentSimTime, bAllowRollback, bNetConfirmed, bSupportsResimulate, new TNetSimCueWrapper<T>(MoveTempIfPossible(NewCue)));
				}
				else
				{
					// Not resimulate case is simple: construct the new cue emplace in the appropriate list
					UE_LOG(LogNetSimCues, Log, TEXT("Invoking Cue %s. Context: %d. Transient: %d. bAllowRollback: %d. bNetConfirmed: %d."), *FGlobalCueTypeTable::Get().GetTypeName(T::ID), Context.TickContext, bTransient, bAllowRollback, bNetConfirmed);
					GetBuffer(bTransient).Emplace(T::ID, Context.CurrentSimTime, bAllowRollback, bNetConfirmed, bSupportsResimulate, new TNetSimCueWrapper<T>(Forward<ArgsType>(Args)...));	
				}				
			}
			else
			{
				UE_LOG(LogNetSimCues, Log, TEXT("Suppressing Cue Invocation %s. Mask: %d. TickContext: %d"), *FGlobalCueTypeTable::Get().GetTypeName(T::ID), TCueHandlerTraits<T>::InvokeMask, (int32)Context.TickContext);
			}
		}
	}

protected:

	bool EnsureValidContext()
	{
		return ensure(Context.CurrentSimTime.IsPositive() && Context.TickContext != ESimulationTickContext::None);
	}

	// Sim Context: the Sim has to tell the dispatcher what its doing so that it can decide if it should supress Invocations or not
	struct FContext
	{
		FNetworkSimTime CurrentSimTime;
		ESimulationTickContext TickContext;
	};
	
	TArray<FSavedCue> SavedCues;		// Cues that must be saved for some period of time, either for replication or for uniqueness testing
	TArray<FSavedCue> TransientCues;	// Cues that are dispatched on this frame and then forgotten about
	TArray<FSavedCue>& GetBuffer(const bool& bTransient) { return bTransient ? TransientCues : SavedCues; }

	FContext Context;
	FNetworkSimTime RollbackTime;		// Time of last rollback, reset after dispatching
};

// Templated cue dispatcher that can be specialized per networking model definition. This is what the system actually uses internally, but is not exposed to user code.
template<typename Model=void>
struct TNetSimCueDispatcher : public FNetSimCueDispatcher
{
	// Serializes all saved cues
	void NetSerializeSavedCues(FArchive& Ar, bool bIsInterpolatingSim)
	{
		FNetSimCueTypeId NumCues = SavedCues.Num();
		Ar << NumCues;
		
		if (Ar.IsSaving())
		{
			for (FSavedCue& SavedCue : SavedCues)
			{
				SavedCue.NetSerialize(Ar);
			}
		}
		else
		{
			// This is quite inefficient right now. 
			//	-We are replicating cues in the last X seconds (ReplicationWindow) redundantly
			//	-Client has to deserialize them (+ heap allocation) and check for uniqueness (have they already processed)
			//	-If already processed (quite common), they are thrown out.
			//	-Would be better if we maybe serialized "net hash" and could skip ahead in the bunch of already processed

			int32 StartingNum = SavedCues.Num();

			for (int32 CueIdx=0; CueIdx < NumCues; ++CueIdx)
			{
				FSavedCue SerializedCue(true);
				SerializedCue.NetSerialize(Ar);

				// Decide if we should accept the cue:

				// ReplicationTarget: Cues can be set to only replicate to interpolators
				if (SerializedCue.CueInstance->GetReplicationTarget() == ENetSimCueReplicationTarget::Interpolators && !bIsInterpolatingSim)
				{
					UE_LOG(LogNetSimCues, Log, TEXT("Discarding replicated NetSimCue %s intended for interpolators."), *SerializedCue.GetTypeName());
					continue;
				}
				
				// Uniqueness: have we already received/predicted it?
				// Note: we are basically ignoring invocation time when matching right now. This could potentially be a trait of the cue if needed.
				// This could create issues if a cue is invoked several times in quick succession, but that can be worked around with arbitrary counter parameters on the cue itself (to force NetUniqueness)
				bool bUniqueCue = true;
				for (int32 ExistingIdx=0; ExistingIdx < StartingNum; ++ExistingIdx)
				{
					FSavedCue& ExistingCue = SavedCues[ExistingIdx];
					if (SerializedCue.NetUnique(ExistingCue) == false)
					{
						// These cues are not unique ("close enough") so we are skipping receiving this one
						UE_LOG(LogNetSimCues, Log, TEXT("Discarding replicated NetSimCue %s because we've already processed it."), *SerializedCue.GetTypeName());
						bUniqueCue = false;
						ExistingCue.bNetConfirmed = true;
						break;
					}
				}

				if (bUniqueCue)
				{
					UE_LOG(LogNetSimCues, Log, TEXT("Received NetUnique Cue: %s. (Num replicated cue sent this bunch: %d)"), *SerializedCue.GetTypeName(), NumCues);
					SavedCues.Emplace(MoveTemp(SerializedCue));
				}
			}
		}
	}

	// Dispatches and prunes saved/transient cues
	template<typename T>
	void DispatchCueRecord(T& Handler, FNetworkSimTime CurrentSimTime)
	{
		const FNetworkSimTime& ConfirmedTime = UserConfirmedTime.IsPositive() ? UserConfirmedTime : CurrentSimTime;
		const FNetworkSimTime SavedCuePruneTime = ConfirmedTime - TCueDispatcherTraits<Model>::ReplicationWindow;

		const FNetworkSimTime& DispatchTime = UserMaxDispatchTime.IsPositive() ? UserMaxDispatchTime : CurrentSimTime;
		
		int32 SavedCuePruneIdx = -1;

		// ------------------------------------------------------------------------
		// Rollback events if necessary
		//	Fixme - this code was written for clarity, it could be sped up considerably by taking advantage of sorting by time, or keeping acceleration lists for this type of pruning
		// ------------------------------------------------------------------------

		if (UserConfirmedTime.IsPositive())
		{
			// Look for cues that should have been matched by now, but were not
			for (auto It = SavedCues.CreateIterator(); It; ++It)
			{
				FSavedCue& SavedCue = *It;
				if (!SavedCue.bNetConfirmed && SavedCue.Time <= ConfirmedTime)
				{
					UE_LOG(LogNetSimCues, Log, TEXT("Calling OnRollback for SavedCue NetSimCue %s. Cue has not been matched but it <= ConfirmedTime."), *SavedCue.GetTypeName());
					SavedCue.Callbacks.OnRollback.Broadcast();
					SavedCues.RemoveAt(It.GetIndex(), 1, false);
				}
			}
		}

		if (RollbackTime.IsPositive())
		{
			for (auto It = SavedCues.CreateIterator(); It; ++It)
			{
				FSavedCue& SavedCue = *It;
				if (SavedCue.bPendingResimulateRollback)
				{
					// Unmatched cue whose time has passed, time to rollback
					UE_LOG(LogNetSimCues, Log, TEXT("Calling OnRollback for SavedCue NetSimCue %s. Cue was not matched during a resimulate. "), *SavedCue.GetTypeName());
					SavedCue.Callbacks.OnRollback.Broadcast();
					SavedCues.RemoveAt(It.GetIndex(), 1, false);
				}
			}

			RollbackTime.Reset();
		}

		// ------------------------------------------------------------------------
		// Dispatch (call ::HandleCue)
		// ------------------------------------------------------------------------
		
		for (int32 SavedCueIdx = 0; SavedCueIdx < SavedCues.Num(); ++ SavedCueIdx)
		{
			FSavedCue& SavedCue = SavedCues[SavedCueIdx];
			if ( SavedCue.Time <= SavedCuePruneTime)
			{
				SavedCuePruneIdx = SavedCueIdx;
			}			

			if (!SavedCue.bDispatched)
			{
				if (SavedCue.Time <= DispatchTime)
				{
					UE_LOG(LogNetSimCues, Log, TEXT("Dispatching NetSimCue %s."), *SavedCue.GetTypeName());
					SavedCue.bDispatched = true;
					TCueDispatchTable<T>::Get().Dispatch(SavedCue, Handler,DispatchTime);
				}
				else
				{
					UE_LOG(LogNetSimCues, Log, TEXT("Withholding Cue %s. %s > %s"), *SavedCue.GetTypeName(), *SavedCue.Time.ToString(), *DispatchTime.ToString());
				}
			}
		}

		for (FSavedCue& TransientCue : TransientCues)
		{
			UE_LOG(LogNetSimCues, Log, TEXT("Dispatching transient NetSimCue %s."), *TransientCue.GetTypeName());
			TCueDispatchTable<T>::Get().Dispatch(TransientCue, Handler, DispatchTime);
		}
		TransientCues.Reset();

		// ------------------------------------------------------------------------
		// Prune
		// ------------------------------------------------------------------------
		
		// Remove Cues we know longer need to keep around
		if (SavedCuePruneIdx >= 0)
		{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			for (int32 i=0; i <= SavedCuePruneIdx; ++i)
			{
				UE_CLOG(!SavedCues[i].bDispatched, LogNetSimCues, Warning, TEXT("Non-Dispatched Cue is about to be pruned! %s. %s"), *SavedCues[i].GetTypeName());
				UE_LOG(LogNetSimCues, Log, TEXT("Pruning Cue %s. Invoke Time: %s. Current Time: %s"), *SavedCues[i].GetTypeName(), *SavedCues[i].Time.ToString(), *DispatchTime.ToString());
			}
#endif

			SavedCues.RemoveAt(0, SavedCuePruneIdx+1, false);
		}
	}

	// Tell dispatcher that we've rolled back to a new simulation time (resimulate steps to follow, most likely)
	void NotifyRollback(const FNetworkSimTime& InRollbackTime)
	{
		// Just cache off the notify. We want to invoke the callbacks in DispatchCueRecord, not right now (in the middle of simulation tick/reconcile)
		if (RollbackTime.IsPositive() == false)
		{
			RollbackTime = InRollbackTime;
		}
		else
		{
			// Two rollbacks could happen in between DispatchCueRecord calls. That is ok as long as the subsequent rollbacks are further ahead in simulation time
			ensure(RollbackTime < InRollbackTime);
		}

		// Mark all cues that support invocation during resimulation as pending rollback (unless they match in an Invoke)
		for (FSavedCue& SavedCue : SavedCues)
		{
			if (SavedCue.bResimulates && SavedCue.Time >= InRollbackTime)
			{
				SavedCue.bPendingResimulateRollback = true;
			}
		}
	}

	// Push/pop simulation context.
	void PushContext(const FContext& InContext) { Context = InContext; }
	void PopContext() { Context = FContext(); }

	// Set max simulation time to invoke cues for. Clearing = always process latest. This is used for interpolation or client side buffering delaying cues until "its time"
	void SetMaxDispatchTime(const FNetworkSimTime& NewMaxTime) const { UserMaxDispatchTime = NewMaxTime; }
	void ClearMaxDispatchTime() const { UserMaxDispatchTime = FNetworkSimTime(); }

	// Set Confirmed time. If this is never called, we assume we are always confirmed/authority.
	void SetConfirmedTime(const FNetworkSimTime& NewConfirmedTime) const { UserConfirmedTime = NewConfirmedTime; }

private:

	mutable FNetworkSimTime UserMaxDispatchTime;	// (If set) Max time of a saved cue that can be dispatched. Needed for delaying interpolation / client side buffering.
	mutable FNetworkSimTime UserConfirmedTime;		// (If set) latest confirmed simulation time. If not set, we assume we are authority and will never rollback.
};

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	Registration Helpers and Macros
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// Body of NetSimCue header. Gives type a static ID to be identified with
#define NETSIMCUE_BODY() static FNetSimCueTypeId ID

// ------------------------------------------------------------------------------------------------
// Register a cue type with the global table.
// ------------------------------------------------------------------------------------------------
template<typename T>
struct TNetSimCueTypeAutoRegisterHelper
{
	TNetSimCueTypeAutoRegisterHelper(const FString& Name)
	{
		FGlobalCueTypeTable::Get().RegisterType<T>(Name);
	}
};

// Note this also defines the internal static ID for the cue type
#define NETSIMCUE_REGISTER(X, STR) FNetSimCueTypeId X::ID=0; TNetSimCueTypeAutoRegisterHelper<X> NetSimCueAr_##X(STR);

// ------------------------------------------------------------------------------------------------
// Register a handler's cue types via a static "RegisterNetSimCueTypes" on the handler itself
// ------------------------------------------------------------------------------------------------
template<typename T>
struct TNetSimCueHandlerAutoRegisterHelper
{
	TNetSimCueHandlerAutoRegisterHelper()
	{
		T::RegisterNetSimCueTypes( TCueDispatchTable<T>::Get() );
	}
};

#define NETSIMCUEHANDLER_REGISTER(X) TNetSimCueHandlerAutoRegisterHelper<X> NetSimCueHandlerAr_##X;

// ------------------------------------------------------------------------------------------------
// Register a handler's cue types via an intermediate "set" class with a static "RegisterNetSimCueTypes" function
// ------------------------------------------------------------------------------------------------
template<typename THandler, typename TCueSet>
struct TNetSimCueSetHandlerAutoRegisterHelper
{
	TNetSimCueSetHandlerAutoRegisterHelper()
	{
		TCueSet::RegisterNetSimCueTypes( TCueDispatchTable<THandler>::Get() );
	}
};

#define NETSIMCUESET_REGISTER(THandler, TCueSet) TNetSimCueSetHandlerAutoRegisterHelper<THandler,TCueSet> NetSimCueSetHandlerAr_##THandler_##TCueSet;

