// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOp.h"
#include "Async/Future.h"

#include "EOSShared.h"

namespace UE::Online {

/** Class to handle all callbacks generically using a future to forward callback results */
template<typename CallbackType>
class TEOSCallback
{
	using CallbackFuncType = void (EOS_CALL*)(const CallbackType*);
public:
	TEOSCallback() = default;
	TEOSCallback(TPromise<const CallbackType*>&& InPromise) : Promise(MoveTemp(InPromise)) {}
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

template<typename TEOSResult, typename TEOSHandle, typename TEOSParameters, typename TEOSFn, typename TAsyncOpType> 
TFuture<const TEOSResult*> EOS_Async(TOnlineAsyncOp<TAsyncOpType>& Op, TEOSFn EOSFn, TEOSHandle EOSHandle, TEOSParameters Parameters)
{
	TEOSCallback<TEOSResult>* Callback = new TEOSCallback<TEOSResult>();
	EOSFn(EOSHandle, &Parameters, Callback, *Callback);
	return Callback->GetFuture();
}

template<typename TEOSResult, typename TEOSHandle, typename TEOSParameters, typename TEOSFn, typename TAsyncOpType>
void EOS_Async(TOnlineAsyncOp<TAsyncOpType>& Op, TEOSFn EOSFn, TEOSHandle EOSHandle, TEOSParameters Parameters, TPromise<const TEOSResult*>&& Promise)
{
	TEOSCallback<TEOSResult>* Callback = new TEOSCallback<TEOSResult>(MoveTemp(Promise));
	EOSFn(EOSHandle, &Parameters, Callback, *Callback);
}

// TEMP until Net Id Registry is done
extern TMap<EOS_EpicAccountId, int32> EOSAccountIdMap;
inline FAccountId MakeEOSAccountId(EOS_EpicAccountId EpicAccountId)
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

inline TOptional<EOS_EpicAccountId> EOSAccountIdFromOnlineServiceAccountId(const FAccountId& InAccountId)
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
