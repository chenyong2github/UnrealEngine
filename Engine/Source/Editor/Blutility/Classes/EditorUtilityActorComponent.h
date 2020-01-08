// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Base class of any editor-only actor components
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/ScriptMacros.h"
#include "EditorUtilityActorComponent.generated.h"


UCLASS(Abstract, Blueprintable, meta = (ShowWorldContextPin))
class BLUTILITY_API UEditorUtilityActorComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

	
};
