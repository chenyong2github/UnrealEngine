// Copyright Epic Games, Inc. All Rights Reserved.
#include "Exporters/AnimSeqExportOption.h"

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "UObject/UObjectBaseUtility.h"
#include "UObject/UObjectHash.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

UAnimSeqExportOption::UAnimSeqExportOption(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ResetToDefault();
}

