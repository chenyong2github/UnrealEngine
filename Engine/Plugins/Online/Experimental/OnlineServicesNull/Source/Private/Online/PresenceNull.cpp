// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/PresenceNull.h"
#include "Online/OnlineAsyncOp.h"
#include "Online/OnlineServicesCommon.h"
#include "Online/OnlineServicesNull.h"

namespace UE::Online {

FPresenceNull::FPresenceNull(FOnlineServicesNull& InServices)
	: FPresenceCommon(InServices)
{

}

void FPresenceNull::Initialize()
{

}

void FPresenceNull::PreShutdown()
{

}

TOnlineAsyncOpHandle<FQueryPresence> FPresenceNull::QueryPresence(FQueryPresence::Params&& Params)
{

	TOnlineAsyncOpRef<FQueryPresence> Op = GetOp<FQueryPresence>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FQueryPresence>& InAsyncOp) mutable
	{
		const FQueryPresence::Params& Params = InAsyncOp.GetParams();

		if(Params.bListenToChanges)
		{
			PresenceListeners.FindOrAdd(Params.LocalUserId).Add(Params.TargetUserId);
		}

		if(Presences.Contains(Params.TargetUserId))
		{
			InAsyncOp.SetResult({Presences.FindChecked(Params.TargetUserId)});
		}
		else
		{
			InAsyncOp.SetError(Errors::NotFound());
		}
	})
    .Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineResult<FGetCachedPresence> FPresenceNull::GetCachedPresence(FGetCachedPresence::Params&& Params)
{
	if (Presences.Contains(Params.LocalUserId))
	{
		return TOnlineResult<FGetCachedPresence>({Presences.FindChecked(Params.LocalUserId)});
	}
	else
	{
		return TOnlineResult<FGetCachedPresence>(Errors::NotFound());
	}
}

TOnlineAsyncOpHandle<FUpdatePresence> FPresenceNull::UpdatePresence(FUpdatePresence::Params&& Params)
{

	TOnlineAsyncOpRef<FUpdatePresence> Op = GetOp<FUpdatePresence>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FUpdatePresence>& InAsyncOp) mutable
	{
		const FUpdatePresence::Params& Params = InAsyncOp.GetParams();
		Presences.Add(Params.LocalUserId, Params.Presence);

		for (const TPair<FOnlineAccountIdHandle, TSet<FOnlineAccountIdHandle>>& Pairs : PresenceListeners)
		{
			if (Pairs.Value.Contains(Params.LocalUserId))
			{
				OnPresenceUpdatedEvent.Broadcast({Pairs.Key, Params.Presence});
			}
		}

		InAsyncOp.SetResult({});
	})
    .Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FPartialUpdatePresence> FPresenceNull::PartialUpdatePresence(FPartialUpdatePresence::Params&& Params)
{

	TOnlineAsyncOpRef<FPartialUpdatePresence> Op = GetOp<FPartialUpdatePresence>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FPartialUpdatePresence>& InAsyncOp) mutable
	{
		TSharedRef<FUserPresence> NewPresence = MakeShared<FUserPresence>();
		const FPartialUpdatePresence::Params::FMutations& Mutations = InAsyncOp.GetParams().Mutations;
		
		if (Presences.Contains(InAsyncOp.GetParams().LocalUserId))
		{
			*NewPresence = *Presences.FindChecked(InAsyncOp.GetParams().LocalUserId);
		}

		TSharedRef<FUserPresence> MutatedPresence = ApplyPresenceMutations(*NewPresence, Mutations);
		Presences.Add(InAsyncOp.GetParams().LocalUserId, MutatedPresence);

		for (const TPair<FOnlineAccountIdHandle, TSet<FOnlineAccountIdHandle>>& Pairs : PresenceListeners)
		{
			if (Pairs.Value.Contains(InAsyncOp.GetParams().LocalUserId))
			{
				OnPresenceUpdatedEvent.Broadcast({ Pairs.Key, MutatedPresence });
			}
		}

		InAsyncOp.SetResult({});
	})
    .Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

} //namespace UE::Online