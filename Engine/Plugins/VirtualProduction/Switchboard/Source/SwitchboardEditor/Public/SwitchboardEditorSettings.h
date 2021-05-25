// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Misc/Paths.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "SwitchboardEditorSettings.generated.h"


UCLASS(config=Engine, BlueprintType)
class SWITCHBOARDEDITOR_API USwitchboardEditorSettings : public UObject
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

	/** OSC Listener for Switchboard. An OSC server can be started on launch via VPUtilitiesEditor*/
	UPROPERTY(config, EditAnywhere, Category = "OSC", meta = (DisplayName = "Default Switchboard OSC Listener", Tooltip = "The OSC listener for Switchboard. An OSC server can be started on launch via the Virtual Production Editor section of the Project Settings. Switchboard uses port 8000 by default, but this can be configured in your Switchboard config settings. "))
	FSoftObjectPath SwitchboardOSCListener = FSoftObjectPath(TEXT("/Switchboard/OSCSwitchboard.OSCSwitchboard"));

	/** Get Settings object for Switchboard*/
	UFUNCTION(BlueprintPure, Category = "Switchboard")
	static USwitchboardEditorSettings* GetSwitchboardEditorSettings();

public:
	FString GetListenerPlatformPath() const
	{
		FString ListenerPlatformPath = ListenerPath.FilePath;
		FPaths::MakePlatformFilename(ListenerPlatformPath);
		return ListenerPlatformPath;
	}

	FString GetListenerInvocation() const
	{
		return FString::Printf(TEXT("\"%s\" %s"), *GetListenerPlatformPath(), *ListenerCommandlineArguments);
	}
};
