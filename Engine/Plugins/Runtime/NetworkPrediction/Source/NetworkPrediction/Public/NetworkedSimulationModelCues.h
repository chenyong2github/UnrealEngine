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
	Interpolators,	// Only replicate to /accept on clients that are interpolating (not running the simulation themselves)
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

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	Wrapper
// ----------------------------------------------------------------------------------------------------------------------------------------------

struct FNetSimCueWrapperBase
{
	virtual ~FNetSimCueWrapperBase() { }
	virtual void NetSerialize(FArchive& Ar) = 0;
	virtual bool NetUnique(const FNetSimCueWrapperBase* Other) const = 0;
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

	bool NetUnique(const FNetSimCueWrapperBase* Other) const override final
	{
		// Cue types must implement bool NetUnique(const TMyCueType& Other) const
		return Instance.NetUnique(*((TCue*)Other->CueData()));
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
	/** Rollback: the keyframe this cue was invoked on was rolled back (and will be resimulated) */
	DECLARE_MULTICAST_DELEGATE(FOnRollback)
	FOnRollback	OnRollback;
	
	/** Confirmed: the keyframe this was was predictively invoked on has been confirmed. The frame will not be rolled back now. */
	DECLARE_MULTICAST_DELEGATE(FOnConfirmed)
	FOnConfirmed OnConfirmed;

	void ClearAll() { OnRollback.Clear(); OnConfirmed.Clear(); }
	bool IsBound() const { return OnRollback.IsBound() || OnConfirmed.IsBound(); }
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
	FSavedCue() = default;

	FSavedCue(const FSavedCue&) = delete;
	FSavedCue& operator=(const FSavedCue&) = delete;

	FSavedCue(FSavedCue&&) = default;
	FSavedCue& operator=(FSavedCue&&) = default;
	
	FSavedCue(const FNetSimCueTypeId& InId, const FNetworkSimTime& InTime, FNetSimCueWrapperBase* Cue, const bool& bInAllowRollback)
		: ID(InId), Time(InTime), CueInstance(Cue), bAllowRollback(bInAllowRollback)
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

	bool NetUnique(FSavedCue& OtherCue) const
	{
		if (ID != OtherCue.ID)
		{
			return false;
		}

		return CueInstance->NetUnique(OtherCue.CueInstance.Get());
	}

	FString GetTypeName() const
	{
		return FGlobalCueTypeTable::Get().GetTypeName(ID);
	}

	FNetSimCueTypeId ID = 0;
	FNetworkSimTime Time;
	TUniquePtr<FNetSimCueWrapperBase> CueInstance;
	bool bDispatched = false;
	bool bAllowRollback = false;
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
				Handler.HandleCue( *static_cast<TCue*>(Cue->CueData()), SystemParameters );
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

	void Dispatch(FSavedCue& SavedCue, TCueHandler& Handler, const FNetSimCueSystemParamemters& SystemParameters)
	{
		if (FCueTypeInfo* TypeInfo = CueTypeInfoMap.Find(SavedCue.ID))
		{
			check(TypeInfo->Dispatch);
			TypeInfo->Dispatch(SavedCue.CueInstance.Get(), Handler, SystemParameters);
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
			if ((TCueHandlerTraits<T>::InvokeMask & (FNetSimCueTypeId)Context.TickContext) > 0)
			{
				// There is an implicit contract that we will invoke events going forward OR will receive explicit rollback notification (TODO)
				ensure(SavedCues.Num() == 0 || SavedCues.Last().Time <= Context.CurrentSimTime);

				// Whether this cue should be dispatched with rollback callbacks. This is a trait of the cue type + non authority (authority will never rollback)
				const bool bAllowRollback = TCueHandlerTraits<T>::Rollbackable && Context.TickContext != ESimulationTickContext::Authority; 

				// Whether we go in the transient list. Transient cues are dumped after dispatching (not saved over multiple frames for uniqueness comparisons)
				const bool bTransient = TCueHandlerTraits<T>::ReplicationTarget == ENetSimCueReplicationTarget::None;

				UE_LOG(LogNetSimCues, Log, TEXT("Invoking Cue %s. Transient: %d. Mask: %d. ReplicationTarget: %d"), *FGlobalCueTypeTable::Get().GetTypeName(T::ID), bTransient, TCueHandlerTraits<T>::InvokeMask, TCueHandlerTraits<T>::ReplicationTarget);
				if (bTransient)
				{
					TransientCues.Emplace(T::ID, Context.CurrentSimTime, new TNetSimCueWrapper<T>(Forward<ArgsType>(Args)...), bAllowRollback);
				}
				else
				{
					SavedCues.Emplace(T::ID, Context.CurrentSimTime, new TNetSimCueWrapper<T>(Forward<ArgsType>(Args)...), bAllowRollback);
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

	FContext Context;
};

// Traits for TNetSimCueDispatcher
struct NETWORKPREDICTION_API FCueDispatcherTraitsBase
{
	// Window for replicating a NetSimCue. That is, after a cue is invoked, it has ReplicationWindow time before it will be pruned.
	// If a client does not get a net update for the sim in this window, they will miss the event.
	static constexpr FNetworkSimTime ReplicationWindow = FNetworkSimTime::FromRealTimeMS(200);
};
template<typename T> struct TCueDispatcherTraits : public FCueDispatcherTraitsBase { };

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
				FSavedCue SerializedCue;
				SerializedCue.NetSerialize(Ar);

				// Decide if we should accept the cue:

				// ReplicationTarget: Cues can be set to only replicate to interpolators
				if (SerializedCue.CueInstance->GetReplicationTarget() == ENetSimCueReplicationTarget::Interpolators && !bIsInterpolatingSim)
				{
					UE_LOG(LogNetSimCues, Log, TEXT("Discarding replicated NetSimCue %s intended for interpolators."), *SerializedCue.GetTypeName());
					continue;
				}

				// Uniqueness: have we already received/predicted it?
				bool bUniqueCue = true;
				for (int32 ExistingIdx=0; ExistingIdx < StartingNum; ++ExistingIdx)
				{
					if (SerializedCue.NetUnique(SavedCues[ExistingIdx]) == false)
					{
						// These cues are not unique ("close enough") so we are skipping receiving this one
						UE_LOG(LogNetSimCues, Log, TEXT("Discarding replicated NetSimCue %s because we've already processed it."), *SerializedCue.GetTypeName());
						bUniqueCue = false;
						break;
					}
				}

				if (bUniqueCue)
				{
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
		// User callbacks
		// ------------------------------------------------------------------------
		
		ensure(UserConfirmedTime.IsPositive() || InvocationCallbacks.Num() == 0); // Should not have callbacks when PruneTime is zero

		for (FInvocationCallbackContainer& Callback : InvocationCallbacks)
		{
			if (RollbackTime.IsPositive() && Callback.InvocationTime > RollbackTime)
			{
				Callback.UserCallbacks.OnRollback.Broadcast();
				Callback.UserCallbacks.OnRollback.Clear();
			}

			if (Callback.UserCallbacks.OnConfirmed.IsBound() && Callback.InvocationTime <= ConfirmedTime)
			{
				Callback.UserCallbacks.OnConfirmed.Broadcast();
				Callback.UserCallbacks.ClearAll(); // Confirmed is the end of the road. Clear all and removal will happen at the bottom of this function
			}
		}
		RollbackTime.Reset();

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

			const bool bWithold = SavedCue.Time > DispatchTime;
			UE_CLOG(bWithold, LogNetSimCues, Log, TEXT("Withholding Cue %s. %s > %s"), *SavedCue.GetTypeName(), *SavedCue.Time.ToString(), *DispatchTime.ToString());

			if (SavedCue.bDispatched == false && !bWithold)
			{
				UE_LOG(LogNetSimCues, Log, TEXT("Dispatching NetSimCue %s."), *SavedCue.GetTypeName());
				SavedCue.bDispatched = true;
				TCueDispatchTable<T>::Get().Dispatch(SavedCue, Handler, {DispatchTime - SavedCue.Time, GetUserCallbackPtr(SavedCue)} );
			}
		}

		for (FSavedCue& TransientCue : TransientCues)
		{
			UE_LOG(LogNetSimCues, Log, TEXT("Dispatching transient NetSimCue %s."), *TransientCue.GetTypeName());
			TCueDispatchTable<T>::Get().Dispatch(TransientCue, Handler, {DispatchTime - TransientCue.Time, GetUserCallbackPtr(TransientCue)} );
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

		// Remove any unbounded callbacks
		for (int32 idx=InvocationCallbacks.Num()-1; idx >= 0; --idx)
		{
			FInvocationCallbackContainer& Callback = InvocationCallbacks[idx];
			if (!Callback.UserCallbacks.IsBound())
			{
				UE_LOG(LogNetSimCues, Log, TEXT("Pruned Callbacks at time=%s"), *Callback.InvocationTime.ToString());
				InvocationCallbacks.RemoveAt(idx, 1, false);
			}
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
			ensure(RollbackTime < InRollbackTime);
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
	
	struct FInvocationCallbackContainer
	{
		FInvocationCallbackContainer(const FNetworkSimTime& InTime) : InvocationTime(InTime) { }

		FNetworkSimTime InvocationTime;
		FNetSimCueCallbacks UserCallbacks;
	};

	TArray<FInvocationCallbackContainer> InvocationCallbacks;
	FNetworkSimTime RollbackTime;

	FNetSimCueCallbacks* GetUserCallbackPtr(const FSavedCue& SavedCue)
	{
		FNetSimCueCallbacks* CallbackPtr = nullptr;
		if (SavedCue.bAllowRollback)
		{
			for (int32 CallbacksIdx = InvocationCallbacks.Num()-1; CallbacksIdx >=0; --CallbacksIdx)
			{
				FInvocationCallbackContainer& CallbackContainer = InvocationCallbacks[CallbacksIdx];

				if (CallbackContainer.InvocationTime == SavedCue.Time)
				{
					CallbackPtr = &CallbackContainer.UserCallbacks;
					break;
				}
				if (CallbackContainer.InvocationTime < SavedCue.Time)
				{
					UE_LOG(LogNetSimCues, Log, TEXT("Added Callbacks for time=%s at idx=%d"), *SavedCue.Time.ToString(), CallbacksIdx);
					CallbackPtr = &InvocationCallbacks.EmplaceAt_GetRef( CallbacksIdx+1, SavedCue.Time ).UserCallbacks;
					break;
				}
			}

			if (!CallbackPtr)
			{
				UE_LOG(LogNetSimCues, Log, TEXT("Added Callbacks for time=%s"));
				CallbackPtr = &InvocationCallbacks.Emplace_GetRef( SavedCue.Time ).UserCallbacks;
			}

		}
		return CallbackPtr;
	}
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

