// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
struct FHttpServerResponse;

/**
* FHttpResultCallback
* This callback is intended to be invoked exclusively by FHttpRequestHandler delegates
* 
* @param Response The response to write
*/
typedef TFunction<void(TUniquePtr<FHttpServerResponse>&& Response)> FHttpResultCallback;


