// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnWebServerStarted, uint32 /*Port*/);

/**
 * A Remote Control module that allows exposing objects and properties from the editor.
 */
class IWebRemoteControlModule : public IModuleInterface
{
public:
	
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
