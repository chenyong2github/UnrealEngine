// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "Engine/EngineTypes.h"
#include "NetworkedSimulationModelTime.h"
#include "Containers/SortedMap.h"

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
//	NetSimCue Traits
// ----------------------------------------------------------------------------------------------------------------------------------------------

struct TNetSimCueTraitsBase
{
	// Who can Invoke this Cue in their simulation
	static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::All };

	// Does the cue replicate? (from authority)
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::All };
};

// Preset: non replicated cue that only plays during "latest" simulate. Will not be played during rewind/resimulate.
struct TNetSimCueTraits_Weak
{
	static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Authority | (uint8)ESimulationTickContext::Predict };
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::None };
};

// Preset: Replicated, non predicted. Only invoked on authority and will replicate to everyone else.
struct TNetSimCueTraits_ReplicatedNonPredicted
{
	static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Authority };
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::All };
};

// Preset:  Replicated to simulated proxy, predicted by autonomous proxy
struct TNetSimCueTraits_ReplicatedXOrPredicted
{
	static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Authority | (uint8)ESimulationTickContext::Predict };
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::Interpolators };
};

// Preset: Invoked and replicated to all. Uniqueness testing to avoid double playing etc
struct TNetSimCueTraits_Strong
{
	static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::All };
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::All };
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
	virtual bool Unique(const FNetSimCueWrapperBase* Other) const = 0;
	virtual void* CueData() const = 0;
	virtual ENetSimCueReplicationTarget GetReplicationTarget() const = 0;
};

template<typename TCue>
struct TNetSimCueWrapper : FNetSimCueWrapperBase
{
	TNetSimCueWrapper() = default;
	TNetSimCueWrapper(const TCue& Source)
	{
		Instance = Source; // Fixme: can we use move semantics to avoid this copy?
	}

	void NetSerialize(FArchive& Ar) override final
	{
		Instance.NetSerialize(Ar);
	}

	bool Unique(const FNetSimCueWrapperBase* Other) const override final
	{
		return TCue::Unique(Instance, *((TCue*)Other->CueData()));
	}

	void* CueData() const override final
	{
		return (void*)&Instance;
	}

	ENetSimCueReplicationTarget GetReplicationTarget() const override final
	{
		return TCueHandlerTraits<TCue>::ReplicationTarget;
	}

	TCue Instance;
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

	template<typename TCue>
	FSavedCue(FNetworkSimTime InTime, const TCue& SourceCue) : Time(InTime)
	{
		ID = TCue::ID; //-V570
		CueInstance.Reset(new TNetSimCueWrapper<TCue>(SourceCue)); // Fixme: can the copy be avoided with move semantics?
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

	bool Unique(FSavedCue& OtherCue) const
	{
		if (ID != OtherCue.ID)
		{
			return false;
		}

		return CueInstance->Unique(OtherCue.CueInstance.Get());
	}

	FString GetTypeName() const
	{
		return FGlobalCueTypeTable::Get().GetTypeName(ID);
	}

	FNetSimCueTypeId ID = 0;
	FNetworkSimTime Time;
	TUniquePtr<FNetSimCueWrapperBase> CueInstance;
	bool bDispatched = false;
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
			CueTypeInfo.Dispatch = [](FNetSimCueWrapperBase* Cue, TCueHandler& Handler, const FNetworkSimTime& ElapsedTime)
			{
				Handler.HandleCue( *static_cast<TCue*>(Cue->CueData()), ElapsedTime );
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

	void Dispatch(FSavedCue& SavedCue, TCueHandler& Handler, const FNetworkSimTime& ElapsedTime)
	{
		if (FCueTypeInfo* TypeInfo = CueTypeInfoMap.Find(SavedCue.ID))
		{
			check(TypeInfo->Dispatch);
			TypeInfo->Dispatch(SavedCue.CueInstance.Get(), Handler, ElapsedTime);
		}
	}

private:

	static TCueDispatchTable<TCueHandler> Singleton;

	struct FCueTypeInfo
	{
		TFunction<void(FNetSimCueWrapperBase* Cue, TCueHandler& Handler, const FNetworkSimTime& ElapsedTime)> Dispatch;
	};

	TMap<FNetSimCueTypeId, FCueTypeInfo> CueTypeInfoMap;
};

template<typename TCueHandler>
TCueDispatchTable<TCueHandler> TCueDispatchTable<TCueHandler>::Singleton;

// ------------------------------------------------------------------------------
//	CueDispatcher
//	-Entry point for invoking cues during a SimulationTick
//	-Holds recorded cue state for replication
// ------------------------------------------------------------------------------

struct FCueDispatcher
{
	template<typename T>
	void Invoke(T&& Cue)
	{
		if (EnsureValidContext())
		{
			if ((TCueHandlerTraits<T>::InvokeMask & (FNetSimCueTypeId)Context.TickContext) > 0)
			{
				// There is an implicit contract that we will invoke events going forward OR will receive explicit rollback notification (TODO)
				ensure(SavedCues.Num() == 0 || SavedCues.Last().Time <= Context.CurrentSimTime);


				bool bTransient = false;
				if (Context.TickContext == ESimulationTickContext::Authority && TCueHandlerTraits<T>::ReplicationTarget == ENetSimCueReplicationTarget::None)
				{
					bTransient = true;
				}

				UE_LOG(LogNetSimCues, Log, TEXT("Invoking Cue %s. Transient: %d. Mask: %d. ReplicationTarget: %d"), *FGlobalCueTypeTable::Get().GetTypeName(T::ID), bTransient, TCueHandlerTraits<T>::InvokeMask, TCueHandlerTraits<T>::ReplicationTarget);

				if (bTransient)
				{
					TransientCues.Emplace(Context.CurrentSimTime, Cue);
				}
				else
				{
					SavedCues.Emplace(Context.CurrentSimTime, Cue);
				}
			}
			else
			{
				UE_LOG(LogNetSimCues, Log, TEXT("Suppressing Cue Invocation %s. Mask: %d. TickContext: %d"), *FGlobalCueTypeTable::Get().GetTypeName(T::ID), TCueHandlerTraits<T>::InvokeMask, (int32)Context.TickContext);
			}
		}
	}

	// Serializes the recorded cue record
	// Note: this is not NetSerializing the individual cues in the record, it is serializing the record which contains the "net bits" of each individual cue
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
					if (SerializedCue.Unique(SavedCues[ExistingIdx]) == false)
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

	template<typename T>
	void DispatchCueRecord(T& Handler, FNetworkSimTime CurrentSimTime)
	{
		// Prune
		FNetworkSimTime CutOffTime = CurrentSimTime - FNetworkSimTime::FromMSec(200); // fixme: hardcoded bad. But also, should take into consideration confirmed server frame in prediction case
		int32 FirstValidElement = 0;
		for (; FirstValidElement < SavedCues.Num(); ++FirstValidElement)
		{
			if ( SavedCues[FirstValidElement].Time > CutOffTime)
			{
				break;
			}
		}

		if (FirstValidElement > 0)
		{
			SavedCues.RemoveAt(0, FirstValidElement, false);
		}

		// Dispatch
		for (FSavedCue& SavedCue : SavedCues)
		{
			if (SavedCue.bDispatched == false)
			{
				UE_LOG(LogNetSimCues, Log, TEXT("Dispatching NetSimCue %s."), *SavedCue.GetTypeName());
				SavedCue.bDispatched = true;
				TCueDispatchTable<T>::Get().Dispatch(SavedCue, Handler, CurrentSimTime - SavedCue.Time);
			}
		}

		for (FSavedCue& TransientCue : TransientCues)
		{
			UE_LOG(LogNetSimCues, Log, TEXT("Dispatching transient NetSimCue %s."), *TransientCue.GetTypeName());
			TCueDispatchTable<T>::Get().Dispatch(TransientCue, Handler, CurrentSimTime - TransientCue.Time);
		}
		TransientCues.Reset();
	}

	// Sim Context: the Sim has to tell the dispatcher what its doing so that it can decide if it should supress Invocations or not
	struct FContext
	{
		FNetworkSimTime CurrentSimTime;
		ESimulationTickContext TickContext;
	};

	void PushContext(const FContext& InContext) { Context = InContext; }
	void PopContext() { Context = FContext(); }

private:

	TArray<FSavedCue> SavedCues;		// Cues that must be saved for some period of time, either for replication or for uniqueness testing
	TArray<FSavedCue> TransientCues;	// Cues that are dispatched on this frame and then forgotten about

	FContext Context;

	bool EnsureValidContext()
	{
		return ensure(Context.CurrentSimTime.IsPositive() && Context.TickContext != ESimulationTickContext::None);
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

