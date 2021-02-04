// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace FEditorConfigTestHelpers
{
	// check that two serialized JSON objects are equivalent
	bool AreJsonStringsEquivalent(const FString& Actual, const FString& Expected);
}