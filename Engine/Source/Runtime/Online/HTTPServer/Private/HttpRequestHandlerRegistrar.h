// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "HttpRouteHandle.h"

/**
 * FHttpRequestHandlerRegistrar
 *  Represents the associative relationship between Http request paths and respective route handles
 */
typedef TSharedPtr<TMap<const FString, const FHttpRouteHandle>> FHttpRequestHandlerRegistrar;
