// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "Engine/EngineTypes.h"
#include "NetworkedSimulationModelTime.h"
#include "NetworkedSimulationModelCueTraits.h"

/*=============================================================================
Networked Simulation Cues

Cues are events that are outputted by a Networked Simulation. They have the following properties:
-Cues are defined by a user struct (custom data payload). They are *invoked* during a SimulationTick and are *dispatched* to a *handler*.
-They are dispatched *after* the simulation is done ticking (via TNetSimCueDispatcher::DispatchCueRecord which is called during the owning component's tick).
-They should not affect the simulation. Or rather, if they do affect the simulation, it will be during the actor tick, effectively the same as any "out of band" modifications.
-They provide automatic replication and invocation settings (traits). They will not "double play" during resimulates.
-They are time aware. When dispatched, the receiver is given how much time has passed (relative to local "head" time) since the invocation.
-They are rollback aware. When dispatched, the receiver is given a callback that will be invoked if the cue needs to rollback (undo itself). 
-The callback is not given in contexts when a rollback is impossible: e.g, on the authority.

Notes on reliability:
-Cues are unreliable in nature. If you join a game in progress, an actor driving the sim suddenly becomes relevant: you will not get all past events.
-If network becomes saturated or drops, you may miss events too.
-Order of received cues is guaranteed, but we can't promise there won't be gaps/missing cues!
-In other words: do not use cues in stateful ways! Cues should be used for transient events ("NotifyExplosion") not state ("NotifySetDestroyed(true)")
-For state transitions, just use FinalizeFrame to detect these changes. "State transitions" are 100% reliable, but (obviously) cannot use data that is not in the Sync/Aux buffer.

Notes on "simulation affect events" (E.g, *not* NetSimCues)
-If you have an event that needs to affect the simulation during the simulation - that is seen as an extension of the simulation and is "up to the user" to implement.
-In other words, handle the event yourself inline with the simulation. That means directly broadcasting/calling other functions/into other classes/etc inside your simulation.
-If your event has state mutation on the handler side, that is a hazard (e.g, state that the network sim is not aware of, but is used in the event code which is an extension of the simulation)
-In these cases I would recommend: A) on the handler side, don't write to any non-networked-sim state if non authority or B) just don't handle the event on non authority. (Expect corrections)
-If the handler side doesn't have state hazards, say a teleporting volume that always does the same thing: there is no reason everyone can't run the event. Its an extension of the simulation.

See "Mock Cue Example" in NetworkedSimulationModelCues.cpp for minimal example of implementing the Cue types and Handler classes.

=============================================================================*/

NETWORKPREDICTION_API DECLARE_LOG_CATEGORY_EXTERN(LogNetSimCues, Log, All);

#ifndef NETSIMCUE_TYPEID_TYPE
#define NETSIMCUE_TYPEID_TYPE uint8;
#endif

using FNetSimCueTypeId = NETSIMCUE_TYPEID_TYPE;

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	Wrapper: wraps the actual user NetSimCue. We want to avoid virtualizing functions on the actual user types.
// ----------------------------------------------------------------------------------------------------------------------------------------------

struct FNetSimCueWrapperBase
{
	virtual ~FNetSimCueWrapperBase() { }
	virtual void NetSerialize(FArchive& Ar) = 0;
	virtual bool NetIdentical(const void* OtherCueData) const = 0;
	virtual void* CueData() const = 0;
	virtual ENetSimCueReplicationTarget GetReplicationTarget() const = 0;
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
		TNetCueNetSerializeHelper<TCue>::CallNetSerializeOrNot(Instance, Ar);
	}

	bool NetIdentical(const void* OtherCueData) const override final
	{
		return TNetCueNetIdenticalHelper<TCue>::CallNetIdenticalOrNot(Instance, *((const TCue*)OtherCueData));
	}

	void* CueData() const override final
	{
		return (void*)&Instance;
	}

	ENetSimCueReplicationTarget GetReplicationTarget() const override final
	{
		return TNetSimCueTraits<TCue>::ReplicationTarget;
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

	// It may make sense to add an "OnConfirmed" that will let the user know a rollback will no longer be possible
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
		
		static_assert(THasMemberFunction_NetSerialize<TCue>::Value || !TNetSimCueTypeRequirements<TCue>::RequiresNetSerialize, "TCue must implement void NetSerialize(FArchive&)");
		static_assert(THasNetIdenticalHelper<TCue>::Value || !TNetSimCueTypeRequirements<TCue>::RequiresNetIdentical, "TCue must implement bool NetIdentical(const TCue&) const");
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
		: ID(InId), Time(InTime), CueInstance(Cue), bAllowRollback(bInAllowRollback), bNetConfirmed(bInNetConfirmed), bResimulates(bInResimulates), ReplicationTarget(Cue->GetReplicationTarget())
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
			ReplicationTarget = CueInstance->GetReplicationTarget();
		}
	}

	// Test NetUniqueness against another saved cue
	bool NetIdentical(FSavedCue& OtherCue) const
	{
		return (ID == OtherCue.ID) && CueInstance->NetIdentical(OtherCue.CueInstance->CueData());
	}

	// Test NetUniqueness against an actual cue instance
	template<typename TCue>
	bool NetIdentical(TCue& OtherCue) const
	{
		return (ID == TCue::ID) && CueInstance->NetIdentical(&OtherCue);
	}


	FString GetDebugName() const
	{
		return FString::Printf(TEXT("[%s 0x%X]"), *FGlobalCueTypeTable::Get().GetTypeName(ID), (int64)this);
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

	ENetSimCueReplicationTarget ReplicationTarget;
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

// Traits for TNetSimCueDispatcher
struct NETWORKPREDICTION_API FCueDispatcherTraitsBase
{
	// Window for replicating a NetSimCue. That is, after a cue is invoked, it has ReplicationWindow time before it will be pruned.
	// If a client does not get a net update for the sim in this window, they will miss the event.
	static constexpr FNetworkSimTime ReplicationWindow = FNetworkSimTime::FromRealTimeMS(200);
};
template<typename T> struct TCueDispatcherTraits : public FCueDispatcherTraitsBase { };

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
			if (EnumHasAnyFlags(TNetSimCueTraits<T>::SimTickMask, Context.TickContext))
			{
				constexpr bool bSupportsResimulate = TNetSimCueTraits<T>::Resimulate;

				bool bAllowRollback = false;	// Whether this cue should be dispatched with rollback callbacks.
				bool bTransient = false;		// Whether we go in the transient list. Transient cues are dumped after dispatching (not saved over multiple frames for uniqueness comparisons during Invoke or NetSerialize)
				bool bNetConfirmed = false;		// Is this already confirmed? (we should not look to undo it if we don't get it confirmed from the server)
				if (Context.TickContext == ESimulationTickContext::Authority)
				{
					// Authority: never rolls back, is already confirmed, and can treat cue as transient if it doesn't have to replicate it
					bAllowRollback = false;
					bTransient = TNetSimCueTraits<T>::ReplicationTarget == ENetSimCueReplicationTarget::None;
					bNetConfirmed = true;
				}
				else
				{
					// Everyone else that is running the simulation (since this in ::Invoke which is called from within the simulation)
					// Rollback if it will replicate to us or if we plan to invoke this cue during resimulates. Transient and confirmed follow directly from this in the non authority case.
					bAllowRollback = (TNetSimCueTraits<T>::ReplicationTarget == ENetSimCueReplicationTarget::All) || bSupportsResimulate;
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
						if (RollbackTime <= ExistingCue.Time && ExistingCue.NetIdentical(NewCue))
						{
							// We've matched with an already predicted cue, so suppress this invocation and don't undo the predicted cue
							ExistingCue.bPendingResimulateRollback = false;
							UE_LOG(LogNetSimCues, Log, TEXT("%s. Resimulated Cue %s matched existing cue. Suppressing Invocation."), *GetDebugName(), *FGlobalCueTypeTable::Get().GetTypeName(T::ID));
							return;
						}
					}

					
					auto& SavedCue = GetBuffer(bTransient).Emplace_GetRef(T::ID, Context.CurrentSimTime, bAllowRollback, bNetConfirmed, bSupportsResimulate, new TNetSimCueWrapper<T>(MoveTempIfPossible(NewCue)));
					UE_LOG(LogNetSimCues, Log, TEXT("%s. Invoking Cue %s during resimulate. Context: %d. Transient: %d. bAllowRollback: %d. bNetConfirmed: %d."), *GetDebugName(), *SavedCue.GetDebugName(), Context.TickContext, bTransient, bAllowRollback, bNetConfirmed);
				}
				else
				{
					// Not resimulate case is simple: construct the new cue emplace in the appropriate list	
					auto& SavedCue = GetBuffer(bTransient).Emplace_GetRef(T::ID, Context.CurrentSimTime, bAllowRollback, bNetConfirmed, bSupportsResimulate, new TNetSimCueWrapper<T>(Forward<ArgsType>(Args)...));	
					UE_LOG(LogNetSimCues, Log, TEXT("%s. Invoking Cue %s. Context: %d. Transient: %d. bAllowRollback: %d. bNetConfirmed: %d."), *GetDebugName(), *SavedCue.GetDebugName(), Context.TickContext, bTransient, bAllowRollback, bNetConfirmed);
				}				
			}
			else
			{
				UE_LOG(LogNetSimCues, Log, TEXT("%s .Suppressing Cue Invocation %s. Mask: %d. TickContext: %d"), *GetDebugName(), *FGlobalCueTypeTable::Get().GetTypeName(T::ID), TNetSimCueTraits<T>::SimTickMask, (int32)Context.TickContext);
			}
		}
	}

	TFunction<FString()> GetDebugName = []() { return TEXT("Unset"); };

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
	void NetSerializeSavedCues(FArchive& Ar, ENetSimCueReplicationTarget ReplicationMask)
	{
		if (Ar.IsSaving())
		{
			// FIXME: requires two passes to count how many elements are valid for this replication mask.
			// We could count this as saved cues are added or possibly modify the bitstream after writing the elements (tricky and would require casting to FNetBitWriter which feels real bad)
			FNetSimCueTypeId NumCues = 0;
			for (FSavedCue& SavedCue : SavedCues)
			{
				if (EnumHasAnyFlags(SavedCue.ReplicationTarget, ReplicationMask))
				{
					NumCues++;
				}
			}

			Ar << NumCues;

			for (FSavedCue& SavedCue : SavedCues)
			{
				if (EnumHasAnyFlags(SavedCue.ReplicationTarget, ReplicationMask))
				{
					SavedCue.NetSerialize(Ar);
				}
			}
		}
		else
		{
			FNetSimCueTypeId NumCues;
			Ar << NumCues;

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
				if ( EnumHasAnyFlags(SerializedCue.ReplicationTarget, ReplicationMask) == false )
				{
					UE_LOG(LogNetSimCues, Log, TEXT("Discarding replicated NetSimCue %s that is not intended for us. CueMask: %d. Our Mask: %d"), *SerializedCue.GetDebugName(), SerializedCue.ReplicationTarget, ReplicationMask);
					continue;
				}
				
				// Uniqueness: have we already received/predicted it?
				// Note: we are basically ignoring invocation time when matching right now. This could potentially be a trait of the cue if needed.
				// This could create issues if a cue is invoked several times in quick succession, but that can be worked around with arbitrary counter parameters on the cue itself (to force NetUniqueness)
				bool bUniqueCue = true;
				for (int32 ExistingIdx=0; ExistingIdx < StartingNum; ++ExistingIdx)
				{
					FSavedCue& ExistingCue = SavedCues[ExistingIdx];
					if (SerializedCue.NetIdentical(ExistingCue))
					{
						// These cues are not unique ("close enough") so we are skipping receiving this one
						UE_LOG(LogNetSimCues, Log, TEXT("Discarding replicated NetSimCue %s because we've already processed it. (Matched %s)"), *SerializedCue.GetDebugName(), *ExistingCue.GetDebugName());
						bUniqueCue = false;
						ExistingCue.bNetConfirmed = true;
						break;
					}
				}

				if (bUniqueCue)
				{
					
					auto& SavedCue = SavedCues.Emplace_GetRef(MoveTemp(SerializedCue));
					UE_LOG(LogNetSimCues, Log, TEXT("%s. Received !NetIdentical Cue: %s. (Num replicated cue sent this bunch: %d)."), *GetDebugName(), *SerializedCue.GetDebugName(), NumCues);
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
					UE_LOG(LogNetSimCues, Log, TEXT("%s. Calling OnRollback for SavedCue NetSimCue %s. Cue has not been matched but it <= ConfirmedTime."), *GetDebugName(), *SavedCue.GetDebugName());
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
					UE_LOG(LogNetSimCues, Log, TEXT("%s. Calling OnRollback for SavedCue NetSimCue %s. Cue was not matched during a resimulate."), *GetDebugName(), *SavedCue.GetDebugName());
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
					UE_LOG(LogNetSimCues, Log, TEXT("%s Dispatching NetSimCue %s"), *GetDebugName(), *SavedCue.GetDebugName());
					SavedCue.bDispatched = true;
					TCueDispatchTable<T>::Get().Dispatch(SavedCue, Handler,DispatchTime);
				}
				else
				{
					UE_LOG(LogNetSimCues, Log, TEXT("Withholding Cue %s. %s > %s."), *SavedCue.GetDebugName(), *SavedCue.Time.ToString(), *DispatchTime.ToString());
				}
			}
		}

		for (FSavedCue& TransientCue : TransientCues)
		{
			UE_LOG(LogNetSimCues, Log, TEXT("Dispatching transient NetSimCue %s."), *TransientCue.GetDebugName());
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
				UE_CLOG(!SavedCues[i].bDispatched, LogNetSimCues, Warning, TEXT("Non-Dispatched Cue is about to be pruned! %s. %s"), *SavedCues[i].GetDebugName());
				UE_LOG(LogNetSimCues, Log, TEXT("%s. Pruning Cue %s. Invoke Time: %s. Current Time: %s."), *GetDebugName(), *SavedCues[i].GetDebugName(), *SavedCues[i].Time.ToString(), *DispatchTime.ToString());
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
				UE_LOG(LogNetSimCues, Log, TEXT("%s. Marking %s bPendingResimulateRollback."), *GetDebugName(), *SavedCue.GetDebugName());
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
template<typename TCue>
struct TNetSimCueTypeAutoRegisterHelper
{
	TNetSimCueTypeAutoRegisterHelper(const FString& Name)
	{
		FGlobalCueTypeTable::Get().RegisterType<TCue>(Name);
	}
};

// Note this also defines the internal static ID for the cue type
#define NETSIMCUE_REGISTER(X, STR) FNetSimCueTypeId X::ID=0; TNetSimCueTypeAutoRegisterHelper<X> NetSimCueAr_##X(STR);

// ------------------------------------------------------------------------------------------------
// Register a handler's cue types via a static "RegisterNetSimCueTypes" on the handler itself
// ------------------------------------------------------------------------------------------------
template<typename TCue>
struct TNetSimCueHandlerAutoRegisterHelper
{
	TNetSimCueHandlerAutoRegisterHelper()
	{
		TCue::RegisterNetSimCueTypes( TCueDispatchTable<TCue>::Get() );
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

