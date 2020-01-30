// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpRouter.h"
#include "HttpRouteHandle.h"
#include "HttpPath.h"
#include "HttpConnection.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HttpRequestHandler.h"
#include "HttpRequestHandlerIterator.h"

FHttpRouter::FHttpRouter()
{
	RequestHandlerRegistrar = MakeShared<TMap<const FString, const FHttpRouteHandle>>();
}

FHttpRouteHandle FHttpRouter::BindRoute(const FHttpPath& HttpPath,  const EHttpServerRequestVerbs& HttpVerbs,  const FHttpRequestHandler& Handler)
{
	check(HttpPath.IsValidPath());
	check(EHttpServerRequestVerbs::VERB_NONE != HttpVerbs);

	if (RequestHandlerRegistrar->Contains(HttpPath.GetPath()))
	{
		return nullptr;
	}

	auto RouteHandle = MakeShared<FHttpRouteHandleInternal>(HttpPath.GetPath(), HttpVerbs, Handler);
	RequestHandlerRegistrar->Add(HttpPath.GetPath(), RouteHandle);

	return RouteHandle;
}

void FHttpRouter::UnbindRoute(const FHttpRouteHandle& RouteHandle)
{
	auto ExistingRouteHandle = RequestHandlerRegistrar->Find(RouteHandle->Path);

	if (ExistingRouteHandle)
	{
		// Ensure caller is unbinding a route they actually own
		check(*ExistingRouteHandle == RouteHandle);
		RequestHandlerRegistrar->Remove(RouteHandle->Path);
	}
}

FHttpRequestHandlerIterator FHttpRouter::CreateRequestHandlerIterator(const TSharedPtr<FHttpServerRequest>& Request) const
{
	FHttpRequestHandlerIterator Iterator(Request, RequestHandlerRegistrar);
	return Iterator;
}
