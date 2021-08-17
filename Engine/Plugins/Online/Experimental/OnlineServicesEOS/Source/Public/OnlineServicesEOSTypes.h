// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOp.h"
#include "Async/Future.h"

#if WITH_EOS_SDK

#include "eos_common.h"

namespace UE::Online {

/** Class to handle all callbacks generically using a future to forward callback results */
template<typename CallbackType>
class TEOSCallback
{
	using CallbackFuncType = void (EOS_CALL*)(const CallbackType*);
public:
	TEOSCallback() = default;
	virtual ~TEOSCallback() = default;

	operator CallbackFuncType()
	{
		return &CallbackImpl;
	}

	TFuture<const CallbackType*> GetFuture() { return Promise.GetFuture(); }

private:
	TPromise<const CallbackType*> Promise;

	static void EOS_CALL CallbackImpl(const CallbackType* Data)
	{
		if (EOS_EResult_IsOperationComplete(Data->ResultCode) == EOS_FALSE)
		{
			// Ignore
			return;
		}
		check(IsInGameThread());

		TEOSCallback* CallbackThis = reinterpret_cast<TEOSCallback*>(Data->ClientData);
		check(CallbackThis);

		CallbackThis->Promise.EmplaceValue(Data);

		delete CallbackThis;
	}
};

template<typename TEosResult, typename TEosHandle, typename TEosParameters, typename TEosFn, typename TAsyncOpType> 
TFuture<const TEosResult*> EOS_Async(TOnlineAsyncOp<TAsyncOpType>& Op, TEosFn EosFn, TEosHandle EosHandle, TEosParameters Parameters)
{
	TEOSCallback<TEosResult>* Callback = new TEOSCallback<TEosResult>();
	EosFn(EosHandle, &Parameters, Callback, *Callback);
	return Callback->GetFuture();
}

// TEMP until Net Id Registry is done
extern TMap<EOS_EpicAccountId, int32> EOSAccountIdMap;
FAccountId MakeEOSAccountId(EOS_EpicAccountId EpicAccountId)
{
	static int32 EpicAccountIdCounter = 0;

	FAccountId Result;
	//Result.Type = EOnlineServices::Epic;
	if (int32* ExistingId = EOSAccountIdMap.Find(EpicAccountId))
	{
		Result.Handle = *ExistingId;
	}
	else
	{
		Result.Handle = EOSAccountIdMap.Emplace(EpicAccountId, ++EpicAccountIdCounter);
	}
	return Result;
}

TOptional<EOS_EpicAccountId> EOSAccountIdFromOnlineServiceAccountId(const FAccountId& InAccountId)
{
	TOptional<EOS_EpicAccountId> Result;
	for (const TPair<EOS_EpicAccountId, int32>& EOSAccountIdPair : EOSAccountIdMap)
	{
		if (EOSAccountIdPair.Value == InAccountId.Handle)
		{
			Result = EOSAccountIdPair.Key;
			break;
		}
	}
	return Result;
}

/* UE::Online */ }

#endif // WITH_EOS_SDK
