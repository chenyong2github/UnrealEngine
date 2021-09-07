// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/NetworkPredictionAsyncProxy.h"
#include "Async/NetworkPredictionAsyncWorldManager.h"

template<typename AsyncModelDef>
void FNetworkPredictionAsyncProxy::RegisterSim(typename AsyncModelDef::LocalStateType&& LocalState, typename AsyncModelDef::NetStateType&& NetState, typename AsyncModelDef::InputCmdType* PendingInputCmd, typename AsyncModelDef::NetStateType* OutNetState)
{
	if (!(ensureMsgf(Manager, TEXT("Proxy not registered prior to call to RegisterSim. Please call RegisterProxy first"))))
	{
		return;
	}

	Manager->RegisterSim<AsyncModelDef>(ID, MoveTemp(LocalState), MoveTemp(NetState), PendingInputCmd, OutNetState);
}

template<typename AsyncModelDef>
void FNetworkPredictionAsyncProxy::UnregisterSim()
{
	if (npEnsureSlow(Manager) && npEnsureSlow(ID.IsValid()))
	{
		Manager->UnregisterSim<AsyncModelDef>(ID);
	}
}

template<typename AsyncModelDef>
void FNetworkPredictionAsyncProxy::ModifyLocalState(TUniqueFunction<void(typename AsyncModelDef::LocalStateType&)> Func)
{
	if (npEnsureSlow(Manager) && npEnsureSlow(ID.IsValid()))
	{
		Manager->ModifyLocalState<AsyncModelDef>(ID, MoveTemp(Func));
	}
}

template<typename AsyncModelDef>
void FNetworkPredictionAsyncProxy::ModifyNetState(TUniqueFunction<void(typename AsyncModelDef::NetStateType&)> Func)
{
	if (npEnsureSlow(Manager) && npEnsureSlow(ID.IsValid()))
	{
		Manager->ModifyNetState<AsyncModelDef>(ID, MoveTemp(Func));
	}
}