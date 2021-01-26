// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace FEditorConfigTestHelpers
{
	// check that two serialized JSON objects are equivalent
	static bool AreJsonStringsEquivalent(const FString& Actual, const FString& Expected);
}