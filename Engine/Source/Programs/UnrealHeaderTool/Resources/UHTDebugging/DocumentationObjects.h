// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DocumentationObjects.generated.h"

/**
 * A class to test the documentation policy 
 * against. For now we'll focus on the main class
 * tooltip only (this comment).
 */
UCLASS(meta=(DocumentationPolicy = "Strict"))
class UClassToDocument : public UObject
{
	GENERATED_BODY()
};
