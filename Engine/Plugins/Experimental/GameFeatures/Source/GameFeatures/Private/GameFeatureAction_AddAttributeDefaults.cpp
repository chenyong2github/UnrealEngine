// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureAction_AddAttributeDefaults.h"
#include "AbilitySystemGlobals.h"

#define LOCTEXT_NAMESPACE "GameFeatures"

//////////////////////////////////////////////////////////////////////
// UGameFeatureAction_AddAttributeDefaults

void UGameFeatureAction_AddAttributeDefaults::OnGameFeatureRegistering()
{
	UAbilitySystemGlobals& AbilitySystemGlobals = UAbilitySystemGlobals::Get();
	AbilitySystemGlobals.AddAttributeDefaultTables(AttribDefaultTableNames);
}

//////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
