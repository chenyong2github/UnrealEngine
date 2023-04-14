// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Blueprint.h"

#include "MVVMViewModelBlueprint.generated.h"

class FKismetCompilerContext;

class FCompilerResultsLog;
class UEdGraph;
class UMVVMViewModelBlueprintGeneratedClass;


/**
 * 
 */
UCLASS(Deprecated)
class UE_DEPRECATED(5.3, "The prototype viewmodel editor is deprecated. Use the regular Blueprint editor.")
MODELVIEWVIEWMODELBLUEPRINT_API UDEPRECATED_MVVMViewModelBlueprint : public UBlueprint
{
	GENERATED_BODY()
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
