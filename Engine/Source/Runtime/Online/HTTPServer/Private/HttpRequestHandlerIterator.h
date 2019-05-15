// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "HttpRequestHandler.h"
#include "HttpRequestHandlerRegistrar.h"

struct FHttpPath;
struct FHttpServerRequest;

/**
 * Utility to iterate qualified FHttpRequestHandlers
 */
struct FHttpRequestHandlerIterator final
{

	/**
	 *  Utility (internal) to iterate FHttpPaths in-place
	 */
	struct FHttpPathIterator final
	{
	public:
		/**
		 * Constructor
		 * @param HttpPath The iterator starting path
		 */
		FHttpPathIterator(const FHttpPath& HttpPath);

		/**
		 * Gets the next route 
		 * @return The base path of the current path
		 */
		const FString& Next();

		/**
		 * Determines whether there is a Next() path to get
		 * @return true if there is a next path, false otherwise
		 */
		FORCEINLINE bool HasNext() const;

	private:
		FString NextPath;
		bool bFirstIteration = true;
		bool bLastIteration = false;
	};


public:

	/**
	 * Constructor
	 */
	FHttpRequestHandlerIterator(const TSharedPtr<FHttpServerRequest>& InRequest, const FHttpRequestHandlerRegistrar& InRequestHandlerRegistrar);

	/** 
	* Determines the next registered request handler
	* @return The next registered request handler if found, nullptr otherwise
	*/
	const FHttpRequestHandler* const Next();

private:

	/** The basis request */
	const TSharedPtr<FHttpServerRequest> Request;

	/** The associative route/handler registration  */
	const FHttpRequestHandlerRegistrar RequestHandlerRegistrar;

	/** Utility to iterate FHttpRoutes in-place */
	FHttpPathIterator HttpPathIterator;

};
