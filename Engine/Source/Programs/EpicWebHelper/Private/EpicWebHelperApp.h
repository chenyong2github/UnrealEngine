// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_CEF3

#include "EpicWebHelperLibCEFIncludes.h"


#include "EpicWebHelperRemoteScripting.h"

/**
 * Implements CEF App and other Process level interfaces
 */
class FEpicWebHelperApp
	: public CefApp
	, public CefRenderProcessHandler
{
public:
    
	/**
	 * Default Constructor
	 */
	FEpicWebHelperApp();

private:
	// CefApp methods:
	virtual CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }

	// CefRenderProcessHandler methods:
	virtual void OnContextCreated( CefRefPtr<CefBrowser> Browser, CefRefPtr<CefFrame> Frame, CefRefPtr<CefV8Context> Context ) override;

	virtual void OnContextReleased( CefRefPtr<CefBrowser> Browser, CefRefPtr<CefFrame> Frame, CefRefPtr<CefV8Context> Context ) override;

	virtual bool OnProcessMessageReceived( CefRefPtr<CefBrowser> Browser, CefRefPtr<CefFrame> frame, CefProcessId SourceProcess, CefRefPtr<CefProcessMessage> Message ) override;

	virtual void OnRenderThreadCreated( CefRefPtr<CefListValue> ExtraInfo ) override;

#if !PLATFORM_LINUX
	virtual void OnFocusedNodeChanged(CefRefPtr<CefBrowser> Browser, CefRefPtr<CefFrame> Frame, CefRefPtr<CefDOMNode> Node) override;
#endif

	// Handles remote scripting messages from the frontend process
	FEpicWebHelperRemoteScripting RemoteScripting;

	// Include the default reference counting implementation.
	IMPLEMENT_REFCOUNTING(FEpicWebHelperApp);
};

#endif // WITH_CEF3
