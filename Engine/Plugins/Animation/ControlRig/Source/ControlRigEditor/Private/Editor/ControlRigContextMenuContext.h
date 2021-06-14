// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ControlRigContextMenuContext.generated.h"

class UControlRig;
class UControlRigBlueprint;
class FControlRigEditor;

UCLASS(BlueprintType)
class UControlRigContextMenuContext : public UObject
{
	GENERATED_BODY()
public:
	/** Get the control rig blueprint that we are editing */
	UFUNCTION(BlueprintCallable, Category = ControlRigEditorExtensions)
    UControlRigBlueprint* GetControlRigBlueprint() const;
	
	/** Our owning control rig editor */
	TWeakPtr<FControlRigEditor> ControlRigEditor;
};