// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "RenderPagesBlueprintGeneratedClass.generated.h"


/**
 * A UBlueprintGeneratedClass child class for the RenderPages modules.
 *
 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
 */
UCLASS()
class RENDERPAGES_API URenderPagesBlueprintGeneratedClass : public UBlueprintGeneratedClass
{
	GENERATED_BODY()
public:
	//~ Begin UBlueprintGeneratedClass interface
	virtual uint8* GetPersistentUberGraphFrame(UObject* Obj, UFunction* FuncToCheck) const override;
	//~ End UBlueprintGeneratedClass interface
};
