// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "HttpRequestHandler.h"
#include "HttpRouteHandle.h"

struct FHttpPath;

class IHttpRouter : public TSharedFromThis<IHttpRouter>
{
public:

	/**
	 * Binds the caller-supplied Uri to the caller-supplied handler
	 *  @param  HttpPath   The respective http path to bind
	 *  @param  HttpVerbs  The respective HTTP verbs to bind
	 *  @param  Handler    The caller-defined closure to execute when the binding is invoked
	 *  @return            An FHttpRouteHandle on success, nullptr otherwise. 
	 */
	virtual FHttpRouteHandle BindRoute(const FHttpPath& HttpPath, const EHttpServerRequestVerbs& HttpVerbs, const FHttpRequestHandler& Handler) = 0;

	/**
	 * Unbinds the caller-supplied Route 
	 *
	 *  @param  RouteHandle The handle to the route to unbind (callers must retain from BindRoute)
	 */
	virtual void UnbindRoute(const FHttpRouteHandle& RouteHandle) = 0;

protected:

	/**
	 * Destructor
	 */
	virtual ~IHttpRouter();
};


