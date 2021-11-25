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

template<typename TEOSResult, typename TEOSHandle, typename TEOSParameters, typename TEOSFn> 
TFuture<const TEOSResult*> EOS_Async(TEOSFn EOSFn, TEOSHandle EOSHandle, TEOSParameters Parameters)
{
	TEOSCallback<TEOSResult>* Callback = new TEOSCallback<TEOSResult>();
	EOSFn(EOSHandle, &Parameters, Callback, *Callback);
	return Callback->GetFuture();
}

template<typename TEOSResult, typename TEOSHandle, typename TEOSParameters, typename TEOSFn>
void EOS_Async(TEOSFn EOSFn, TEOSHandle EOSHandle, TEOSParameters Parameters, TPromise<const TEOSResult*>&& Promise)
{
	TEOSCallback<TEOSResult>* Callback = new TEOSCallback<TEOSResult>(MoveTemp(Promise));
	EOSFn(EOSHandle, &Parameters, Callback, *Callback);
}

/* UE::Online */ }
