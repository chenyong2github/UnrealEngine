// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_CURL

#include "HttpThread.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif
#if WITH_CURL_XCURL
//We copied this template to include the windows file from WindowsHWrapper's way if including MinWindows.h, since including xcurl.h directly caused gnarly build errors
#include "CoreTypes.h"
#include "HAL/PlatformMemory.h"
#include "Windows/PreWindowsApi.h"
#ifndef STRICT
#define STRICT
#endif
#include "xcurl.h"
#include "Windows/PostWindowsApi.h"
#else
#include "curl/curl.h"
#endif
#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#endif //WITH_CURL

class IHttpThreadedRequest;

#if WITH_CURL

class FCurlHttpThread
	: public FHttpThread
{
public:
	
	FCurlHttpThread();

protected:
	//~ Begin FHttpThread Interface
	virtual void HttpThreadTick(float DeltaSeconds) override;
	virtual bool StartThreadedRequest(IHttpThreadedRequest* Request) override;
	virtual void CompleteThreadedRequest(IHttpThreadedRequest* Request) override;
	//~ End FHttpThread Interface
protected:

	/** Mapping of libcurl easy handles to HTTP requests */
	TMap<CURL*, IHttpThreadedRequest*> HandlesToRequests;
};


#endif //WITH_CURL
