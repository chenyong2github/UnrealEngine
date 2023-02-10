// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"

#if WITH_EDITOR

#define UE_API HORDE_API

struct FHorde
{
	UE_API static FString GetTemplateName();
	UE_API static FString GetTemplateId();
	UE_API static FString GetJobId();
	UE_API static FString GetJobURL();
	UE_API static FString GetStepId();
	UE_API static FString GetStepURL();
	UE_API static FString GetStepName();
	UE_API static FString GetBatchId();
};

#endif
