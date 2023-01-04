// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "DMXControlConsolePreset.generated.h"

class UDMXControlConsole;


/** Preset to save Control Console's data */
UCLASS(BlueprintType)
class UDMXControlConsolePreset 
	: public UObject
{
	GENERATED_BODY()

public:
	UDMXControlConsole* GetControlConsole() const { return ControlConsole.IsValid() ? ControlConsole.Get() : nullptr; }

	void SetControlConsole(UDMXControlConsole* InControlConsole);

private:
	/** Control Console reference */
	UPROPERTY(VisibleAnywhere, Category = "DMX Control Console Preset")
	TWeakObjectPtr<UDMXControlConsole> ControlConsole;
};
