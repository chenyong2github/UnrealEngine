// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#include "NetworkPredictionAsyncID.h"
#include "NetworkPredictionAsyncProxy.generated.h"

class APlayerController;

namespace UE_NP
{
	class FNetworkPredictionAsyncWorldManager;
};

USTRUCT()
struct NETWORKPREDICTION_API FNetworkPredictionAsyncProxy
{
	GENERATED_BODY()

	// acquires an ID but does not register with any sims (a proxy may participate in multiple sims at once)
	bool RegisterProxy(UWorld* World);

	// Unregisters proxy with NP system and all sims it was participating in
	void UnregisterProxy();

	void RegisterController(APlayerController* PC);

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
	bool Identical(const FNetworkPredictionAsyncProxy* Other, uint32 PortFlags) const;
	void OnPreReplication();

	// Include NetworkPredictionAsyncProxyImpl.h in your .cpp to use these.
	template<typename AsyncModelDef>
	void RegisterSim(typename AsyncModelDef::LocalStateType&& LocalState, typename AsyncModelDef::NetStateType&& NetState, typename AsyncModelDef::InputCmdType* PendingInputCmd, typename AsyncModelDef::NetStateType* OutNetState);

	// Unregisters with specific sim. Note that UnregisterProxy will do this automatically
	template<typename AsyncModelDef>
	void UnregisterSim();

	template<typename AsyncModelDef>
	void ModifyLocalState(TUniqueFunction<void(typename AsyncModelDef::LocalStateType&)> Func);

	template<typename AsyncModelDef>
	void ModifyNetState(TUniqueFunction<void(typename AsyncModelDef::NetStateType&)> Func);

private:

	UE_NP::FNetworkPredictionAsyncWorldManager* Manager = nullptr;
	UE_NP::FNetworkPredictionAsyncID ID;

	int32 CachedLatestFrame = INDEX_NONE;
};

template<>
struct TStructOpsTypeTraits<FNetworkPredictionAsyncProxy> : public TStructOpsTypeTraitsBase2<FNetworkPredictionAsyncProxy>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
		WithIdentical = true,
	};
 };