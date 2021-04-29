// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "HttpRequestHandler.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnWebServerStarted, uint32 /*Port*/);

/**
 * A Remote Control module that allows exposing objects and properties from the editor.
 */
class WEBREMOTECONTROL_API IWebRemoteControlModule : public IModuleInterface
{
public:

	/**
	 * Register a request preprocessor.
	 * Useful for cases where you want to drop or handle incoming requests before they are handled the the web remote control module.
	 * @param RequestPreprocessor The function called to process the incoming request.
	 * @return FDelegateHandle The handle to the delegate, used for unregistering preprocessors. 
	 */
	virtual FDelegateHandle RegisterRequestPreprocessor(FHttpRequestHandler RequestPreprocessor) = 0;

	/**
	 * Unregister a request preprocessor.
	 * @param RequestPreprocessorHandle The handle to the preprocessor delegate.
	 */
	virtual void UnregisterRequestPreprocessor(const FDelegateHandle& RequestPreprocessorHandle) = 0;
	
	/** 
	 * Event triggered when the http server starts.
	 */
	virtual FOnWebServerStarted& OnHttpServerStarted() = 0;
	
	/** 
	 * Event triggered when the http server stops.
	 */
	virtual FSimpleMulticastDelegate& OnHttpServerStopped() = 0;
	
	
	/** 
	 * Event triggered when the websocket server starts.
	 */
	virtual FOnWebServerStarted& OnWebSocketServerStarted() = 0;
	
	/** 
	 * Event triggered when the websocket server stops.
	 */
	virtual FSimpleMulticastDelegate& OnWebSocketServerStopped() = 0;
};
