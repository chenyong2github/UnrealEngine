// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "SwitchboardEditorSettings.generated.h"


UCLASS(config = EditorPerProjectUserSettings, MinimalAPI)
class USwitchboardEditorSettings : public UObject
{
	GENERATED_BODY()

	USwitchboardEditorSettings();

public:
	/** Switchboard installs its own python interpreter on its first launch. If you prefer
	 * to use your own, specify the path to the python executable.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Switchboard")
	FFilePath PythonInterpreterPath;

	/** Path to Switchboard itself. */
	UPROPERTY(Config, EditAnywhere, Category="Switchboard")
	FDirectoryPath SwitchboardPath;

	/** Arguments that should be passed to Switchboard. */
	UPROPERTY(Config, EditAnywhere, Category="Switchboard")
	FString CommandlineArguments;

	/** Path to Switchboard Listener executable. */
	UPROPERTY(Config, EditAnywhere, Category="Listener")
	FFilePath ListenerPath;

	/** Arguments that should be passed to the Switchboard Listener. */
	UPROPERTY(Config, EditAnywhere, Category="Listener")
	FString ListenerCommandlineArguments;
};
