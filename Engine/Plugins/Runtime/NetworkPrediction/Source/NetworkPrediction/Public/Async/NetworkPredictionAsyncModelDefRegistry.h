// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkPredictionLog.h"
#include "NetworkPredictionAsyncModelDef.h"
#include "NetworkPredictionCheck.h"

namespace UE_NP {

// Basic ModelDef type registry. ModelDefs are registered and assigned an ID (their ModelDef::ID) based on ModelDef::SortPriority.
// This ID is used as indices into the various NP service arrays
class FNetworkPredictionAsyncModelDefRegistry
{
public:

	NETWORKPREDICTION_API static FNetworkPredictionAsyncModelDefRegistry& Get()
	{
		return Singleton;
	}

	template<typename AsyncModelDef>
	void RegisterType()
	{
		bFinalized = false; // New type must re-finalize

		if (!npEnsure(ModelDefList.Contains(&AsyncModelDef::ID) == false))
		{
			return;
		}

		FTypeInfo TypeInfo =
		{
			&AsyncModelDef::ID,					// Must include NP_MODEL_BODY()
			AsyncModelDef::GetSortPriority(),	// Must implement ::GetSortPriorty()
			AsyncModelDef::GetName()			// Must implement ::GetName()
		};

		ModelDefList.Emplace(TypeInfo);
	}

	void FinalizeTypes()
	{
		if (bFinalized)
		{
			return;
		}

		bFinalized = true;
		ModelDefList.Sort([](const FTypeInfo& LHS, const FTypeInfo& RHS) -> bool
			{
				if (LHS.SortPriority == RHS.SortPriority)
				{
					UE_LOG(LogNetworkPrediction, Log, TEXT("AsyncModelDefs %s and %s have same sort priority. Using lexical sort as backup"), LHS.Name, RHS.Name);
					int32 StrCmpResult = FCString::Strcmp(LHS.Name, RHS.Name);
					npEnsureMsgf(StrCmpResult != 0, TEXT("Duplicate AsyncModelDefs appear to have been registered."));
					return StrCmpResult > 0;
				}

				return LHS.SortPriority < RHS.SortPriority; 
			});

		int32 Count = 0;
		for (FTypeInfo& TypeInfo : ModelDefList)
		{
			*TypeInfo.IDPtr = Count++;
		}
	}

private:

	NETWORKPREDICTION_API static FNetworkPredictionAsyncModelDefRegistry Singleton;

	struct FTypeInfo
	{
		bool operator==(const FAsyncModelDefID* OtherIDPtr) const
		{
			return IDPtr == OtherIDPtr;
		}

		FAsyncModelDefID* IDPtr = nullptr;
		int32 SortPriority;
		const TCHAR* Name;
	};

	TArray<FTypeInfo> ModelDefList;

	bool bFinalized = false;
};

template<typename AsyncModelDef>
struct TNetworkPredictionAsyncModelDefRegisterHelper
{
	TNetworkPredictionAsyncModelDefRegisterHelper()
	{
		FNetworkPredictionAsyncModelDefRegistry::Get().RegisterType<AsyncModelDef>();
	}
};

} // namespace UE_NP

// Helper to register ModelDef type.
// Sets static ID to -1 (invalid) and calls global registration function
#define NP_ASYNC_MODEL_REGISTER(X) \
	UE_NP::FAsyncModelDefID X::ID=-1; \
	static UE_NP::TNetworkPredictionAsyncModelDefRegisterHelper<X> NetModelAr_##X = UE_NP::TNetworkPredictionAsyncModelDefRegisterHelper<X>();