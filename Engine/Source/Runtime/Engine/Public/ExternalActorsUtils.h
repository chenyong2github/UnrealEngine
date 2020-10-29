// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace ExternalActorsUtils
{
	/**
	 * Gather direct references to external actors from the root object.
	 */
	TArray<AActor*> GetExternalActorReferences(UObject* Root);
}