// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreGlobals.h"
#include "Templates/SharedPointer.h"

#if WITH_EOS_SDK
#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif

#include "eos_common.h"
#endif

// Expect URLs to look like "EOS:PUID:SocketName:Channel"
#define EOS_CONNECTION_URL_PREFIX TEXT("EOS")
#define EOS_URL_SEPARATOR TEXT(":")

/** Used to store a pointer to the EOS callback object without knowing type */
class EOSSHARED_API FCallbackBase
{
public:
	virtual ~FCallbackBase() {}
};

#if WITH_EOS_SDK

/** Class to handle all callbacks generically using a lambda to process callback results */
template<typename CallbackFuncType, typename CallbackType, typename OwningType>
class TEOSGlobalCallback :
	public FCallbackBase
{
public:
	TFunction<void(const CallbackType*)> CallbackLambda;

	TEOSGlobalCallback(TWeakPtr<OwningType> InOwner)
		: FCallbackBase()
		, Owner(InOwner)
	{
	}
	virtual ~TEOSGlobalCallback() = default;


	CallbackFuncType GetCallbackPtr()
	{
		return &CallbackImpl;
	}

private:
	/** The object that needs to be checked for lifetime before calling the callback */
	TWeakPtr<OwningType> Owner;

	static void EOS_CALL CallbackImpl(const CallbackType* Data)
	{
		check(IsInGameThread());

		TEOSGlobalCallback* CallbackThis = (TEOSGlobalCallback*)Data->ClientData;
		check(CallbackThis);

		if (CallbackThis->Owner.IsValid())
		{
			check(CallbackThis->CallbackLambda);
			CallbackThis->CallbackLambda(Data);
		}
	}
};

#endif