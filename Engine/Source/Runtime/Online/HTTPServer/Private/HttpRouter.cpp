// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpRouter.h"
#include "HttpRouteHandle.h"
#include "HttpPath.h"
#include "HttpConnection.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HttpRequestHandler.h"
#include "HttpRequestHandlerIterator.h"

bool FHttpRouter::Query(const TSharedPtr<FHttpServerRequest>& Request, const FHttpResultCallback& OnProcessingComplete)
{
	bool bRequestHandled = false;

	FHttpRequestHandlerIterator Iterator(Request, RequestHandlerRegistrar);
	while (const FHttpRequestHandler* RequestHandlerPtr = Iterator.Next())
	{
		(*RequestHandlerPtr).CheckCallable();
		bRequestHandled = (*RequestHandlerPtr)(*Request, OnProcessingComplete);
		if (bRequestHandled)
		{
			break;
		}
	}

	return bRequestHandled;
}

FHttpRouteHandle FHttpRouter::BindRoute(const FHttpPath& HttpPath,  const EHttpServerRequestVerbs& HttpVerbs,  const FHttpRequestHandler& Handler)
{
	check(HttpPath.IsValidPath());
	check(EHttpServerRequestVerbs::VERB_NONE != HttpVerbs);

	if (RequestHandlerRegistrar.ContainsRoute(HttpPath, HttpVerbs))
	{
		return nullptr;
	}

	auto RouteHandle = MakeShared<FHttpRouteHandleInternal>(HttpPath.GetPath(), HttpVerbs, Handler);
	RequestHandlerRegistrar.AddRoute(RouteHandle);

	return RouteHandle;
}

void FHttpRouter::UnbindRoute(const FHttpRouteHandle& RouteHandle)
{
	if (FRouteQueryResult QueryResult = RequestHandlerRegistrar.QueryRoute(RouteHandle->Path, RouteHandle->Verbs))
	{
		// Ensure caller is unbinding a route they actually own
		check(QueryResult.RouteHandle == RouteHandle);
		RequestHandlerRegistrar.RemoveRoute(RouteHandle);
	}
}

FHttpRequestHandlerIterator FHttpRouter::CreateRequestHandlerIterator(const TSharedPtr<FHttpServerRequest>& Request) const
{
	FHttpRequestHandlerIterator Iterator(Request, RequestHandlerRegistrar);
	return Iterator;
}

