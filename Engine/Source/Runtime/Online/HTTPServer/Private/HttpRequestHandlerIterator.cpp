// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "HttpRequestHandlerIterator.h"
#include "HttpServerRequest.h"
#include "HttpRouteHandle.h"


FHttpRequestHandlerIterator::FHttpRequestHandlerIterator(
	const TSharedPtr<FHttpServerRequest>& InRequest, 
	const FHttpRequestHandlerRegistrar& InRequestHandlerRegistrar)
	: Request(InRequest)
	, RequestHandlerRegistrar(InRequestHandlerRegistrar)
	, HttpPathIterator(InRequest->RelativePath)
{
}

const FHttpRequestHandler * const FHttpRequestHandlerIterator::Next()
{
	while (HttpPathIterator.HasNext())
	{
		// Determine if we have a matching handler for the next route
		const auto& NextRoute = HttpPathIterator.Next();

		// Filter by http route
		const auto RouteHandlePtr = RequestHandlerRegistrar->Find(NextRoute);
		if (!RouteHandlePtr)
		{
			// Not a matching route
			continue;
		}
		const auto& RouteHandle = *RouteHandlePtr;

		// Filter by http verb
		const EHttpServerRequestVerbs VerbFilterResult = RouteHandle->Verbs & Request->Verb;
		if (EHttpServerRequestVerbs::VERB_NONE == VerbFilterResult)
		{
			// Not a matching verb
			continue;
		}

		// Make request path relative to the respective handler
		Request->RelativePath.MakeRelative(NextRoute);

		return &(RouteHandle->Handler);
	}
	return nullptr;
}

FHttpRequestHandlerIterator::FHttpPathIterator::FHttpPathIterator(const FHttpPath& HttpPath)
{
	NextPath = HttpPath.GetPath();
}

bool FHttpRequestHandlerIterator::FHttpPathIterator::HasNext() const
{
	return !bLastIteration;
}

const FString& FHttpRequestHandlerIterator::FHttpPathIterator::Next()
{
	// Callers  should always test HasNext() first!
	check(!bLastIteration); 

	if (!bFirstIteration)
	{
		int32 SlashIndex = 0;
		if (NextPath.FindLastChar(TCHAR('/'), SlashIndex))
		{
			const bool bAllowShrinking = false;
			NextPath.RemoveAt(SlashIndex, NextPath.Len() - SlashIndex, bAllowShrinking);

			if (0 == NextPath.Len())
			{
				NextPath.AppendChar(TCHAR('/'));
				bLastIteration = true;
			}
		}
	}
	bFirstIteration = false;
	return NextPath;
}

