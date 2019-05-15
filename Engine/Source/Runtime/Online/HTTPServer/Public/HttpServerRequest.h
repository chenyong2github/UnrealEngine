// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "HttpPath.h"

enum class EHttpServerRequestVerbs : uint16
{
	NONE   = 0,
	GET    = 1 << 0,
	POST   = 1 << 1,
	PUT    = 1 << 2,
	PATCH  = 1 << 3,
	DELETE = 1 << 4,
	ALL = GET | POST | PUT | PATCH | DELETE,
};

ENUM_CLASS_FLAGS(EHttpServerRequestVerbs);

struct FHttpServerRequest
{
public:

	/** Constructor */
	FHttpServerRequest() { };

	/** The handler-route-relative HTTP path */
	FHttpPath RelativePath;

	/** The HTTP-compliant verb  */
	EHttpServerRequestVerbs Verb;

	/** The HTTP headers */
	TMap<FString, TArray<FString>> Headers;

	/** The query parameters */
	TMap<FString, FString> QueryParams;

	/** The raw body contents */
	TArray<uint8> Body;

};

