// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOp.h"
#include "Online/OnlineError.h"
#include "Async/Future.h"

#include "EOSShared.h"

namespace UE::Online {

inline FOnlineError FromEOSError(EOS_EResult ResultCode)
{
	// Todo: make this for real.
	return FOnlineError(Errors::Unknown());
}

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
decltype(auto) EOS_Async(TEOSFn EOSFn, TEOSHandle EOSHandle, TEOSParameters Parameters, TPromise<const TEOSResult*>&& Promise)
{
	TEOSCallback<TEOSResult>* Callback = new TEOSCallback<TEOSResult>(MoveTemp(Promise));
	return EOSFn(EOSHandle, &Parameters, Callback, *Callback);
}

class EOSEventRegistration
{
public:
	virtual ~EOSEventRegistration() = default;
};
typedef TUniquePtr<EOSEventRegistration> EOSEventRegistrationPtr;

/** 
* EOS event registration utility for binding an EOS notifier registration to a RAII object which handles
* unregistering when it exits scope. Intended to be used from a TOnlineComponent class.
* 
* Example:
*	EOSEventRegistrationPtr OnLobbyUpdatedEOSEventRegistration = EOS_RegisterComponentEventHandler(
*		this,
*		LobbyHandle,
*		EOS_LOBBY_ADDNOTIFYLOBBYUPDATERECEIVED_API_LATEST,
*		&EOS_Lobby_AddNotifyLobbyUpdateReceived,
*		&EOS_Lobby_RemoveNotifyLobbyUpdateReceived,
*		&FLobbiesEOS::HandleLobbyUpdated);
**/
template <
	typename ComponentHandlerClass,
	typename EOSHandle,
	typename EOSNotfyRegisterFunction,
	typename EOSNotfyUnregisterFunction,
	typename ComponentHandlerFunction>
EOSEventRegistrationPtr EOS_RegisterComponentEventHandler(
	ComponentHandlerClass* HandlerClass,
	EOSHandle ClientHandle,
	int32_t ApiVersion,
	EOSNotfyRegisterFunction NotfyRegisterFunction,
	EOSNotfyUnregisterFunction NotfyUnregisterFunction,
	ComponentHandlerFunction HandlerFunction);

namespace detail {

template<typename Function> struct TEOSCallbackTraitsBase;

template<typename Component, typename EventData>
struct TEOSCallbackTraitsBase<void (Component::*)(const EventData*)>
{
	typedef Component ComponentType;
	typedef EventData EventDataType;
};

template<typename Function>
struct TEOSCallbackTraits : public TEOSCallbackTraitsBase<typename std::remove_reference<Function>::type>
{
};

template<typename Function> struct TEOSNotifyRegisterTraits;

template<typename EOSHandle, typename Options, typename ClientData, typename NotificationFn, typename NotificationId>
struct TEOSNotifyRegisterTraits<NotificationId (*)(EOSHandle, const Options*, ClientData*, NotificationFn)>
{
	typedef Options OptionsType;
	typedef NotificationId NotificationIdType;
};

template <
	typename ComponentHandlerClass,
	typename EOSHandle,
	typename EOSNotfyRegisterFunction,
	typename EOSNotfyUnregisterFunction,
	typename ComponentHandlerFunction>
class EOSEventRegistrationImpl : public EOSEventRegistration
{
public:
	typedef EOSEventRegistrationImpl<
		ComponentHandlerClass,
		EOSHandle,
		EOSNotfyRegisterFunction,
		EOSNotfyUnregisterFunction,
		ComponentHandlerFunction> ThisClass;

	EOSEventRegistrationImpl(
		ComponentHandlerClass* HandlerClass,
		EOSHandle ClientHandle,
		int32_t ApiVersion,
		EOSNotfyRegisterFunction NotfyRegisterFunction,
		EOSNotfyUnregisterFunction NotfyUnregisterFunction,
		ComponentHandlerFunction HandlerFunction)
		: HandlerClass(HandlerClass)
		, ClientHandle(ClientHandle)
		, NotfyUnregisterFunction(NotfyUnregisterFunction)
		, HandlerFunction(HandlerFunction)
	{
		typename TEOSNotifyRegisterTraits<EOSNotfyRegisterFunction>::OptionsType Options = { };
		Options.ApiVersion = ApiVersion;
		NotificationId = NotfyRegisterFunction(ClientHandle, &Options, this,
		[](const typename TEOSCallbackTraits<ComponentHandlerFunction>::EventDataType* Data)
		{
			ThisClass* This = reinterpret_cast<ThisClass*>(Data->ClientData);
			(This->HandlerClass->*This->HandlerFunction)(Data);
		});
	}

	virtual ~EOSEventRegistrationImpl()
	{
		NotfyUnregisterFunction(ClientHandle, NotificationId);
	}

private:
	EOSEventRegistrationImpl() = delete;
	EOSEventRegistrationImpl(const EOSEventRegistrationImpl&) = delete;
	EOSEventRegistrationImpl& operator=(const EOSEventRegistrationImpl&) = delete;

	typedef typename TEOSNotifyRegisterTraits<EOSNotfyRegisterFunction>::NotificationIdType NotificationIdType;

	NotificationIdType NotificationId;
	ComponentHandlerClass* HandlerClass;
	EOSHandle ClientHandle;
	EOSNotfyUnregisterFunction NotfyUnregisterFunction;
	ComponentHandlerFunction HandlerFunction;
};

/* detail */ }

template <
	typename ComponentHandlerClass,
	typename EOSHandle,
	typename EOSNotfyRegisterFunction,
	typename EOSNotfyUnregisterFunction,
	typename ComponentHandlerFunction>
EOSEventRegistrationPtr EOS_RegisterComponentEventHandler(
	ComponentHandlerClass* HandlerClass,
	EOSHandle ClientHandle,
	int32_t ApiVersion,
	EOSNotfyRegisterFunction NotfyRegisterFunction,
	EOSNotfyUnregisterFunction NotfyUnregisterFunction,
	ComponentHandlerFunction HandlerFunction)
{
	return MakeUnique<
		detail::EOSEventRegistrationImpl<
			ComponentHandlerClass,
			EOSHandle,
			EOSNotfyRegisterFunction,
			EOSNotfyUnregisterFunction,
			ComponentHandlerFunction>>(
			HandlerClass,
			ClientHandle,
			ApiVersion,
			NotfyRegisterFunction,
			NotfyUnregisterFunction,
			HandlerFunction);
}

/* UE::Online */ }
