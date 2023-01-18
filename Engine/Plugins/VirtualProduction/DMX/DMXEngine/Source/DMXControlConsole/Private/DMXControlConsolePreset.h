// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "DMXControlConsolePreset.generated.h"

class UDMXControlConsole;


/** Preset to save Control Console's data */
UCLASS(BlueprintType)
class DMXCONTROLCONSOLE_API UDMXControlConsolePreset
	: public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	DECLARE_EVENT_OneParam(UDMXControlConsolePreset, FDMXControlConsolePresetEvent, const UDMXControlConsolePreset*)
#endif // WITH_EDITOR

	/** Gets the Preset's Control Console reference */
	UDMXControlConsole* GetControlConsole() const { return ControlConsole.IsValid() ? ControlConsole.Get() : nullptr; }

	/** Sets the Preset's Control Console reference */
	void SetControlConsole(UDMXControlConsole* InControlConsole);

	/** Gets a reference to OnControlConsolePresetSaved delegate */
#if WITH_EDITOR
	FDMXControlConsolePresetEvent& GetOnControlConsolePresetSaved() { return OnControlConsolePresetSaved; }
#endif // WITH_EDITOR

protected:
	//~ Begin UObject interface
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject interface

private:
	/** Control Console reference */
	UPROPERTY(VisibleAnywhere, Category = "DMX Control Console Preset")
	TWeakObjectPtr<UDMXControlConsole> ControlConsole;

	/** Called when the preset asset is saved */
#if WITH_EDITOR
	FDMXControlConsolePresetEvent OnControlConsolePresetSaved;
#endif // WITH_EDITOR
};
