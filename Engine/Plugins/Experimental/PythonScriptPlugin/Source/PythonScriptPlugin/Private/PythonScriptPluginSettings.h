// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/DeveloperSettings.h"
#include "PythonScriptPluginSettings.generated.h"

/**
 * Configure the Python plug-in.
 */
UCLASS(config=Engine, defaultconfig)
class UPythonScriptPluginSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPythonScriptPluginSettings();

#if WITH_EDITOR
	//~ UObject interface
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	//~ UDeveloperSettings interface
	virtual FText GetSectionText() const override;
#endif

	/** Array of Python scripts to run at start-up (run before the first Tick after the Engine has initialized). */
	UPROPERTY(config, EditAnywhere, Category=Python, meta=(ConfigRestartRequired=true, MultiLine=true))
	TArray<FString> StartupScripts;

	/** Array of additional paths to add to the Python system paths. */
	UPROPERTY(config, EditAnywhere, Category=Python, meta=(ConfigRestartRequired=true, RelativePath))
	TArray<FDirectoryPath> AdditionalPaths;

	/**
	 * Should Developer Mode be enabled on the Python interpreter *for all users of the project*
	 * Note: Most of the time you want to enable bDeveloperMode in the Editor Preferences instead!
	 *
	 * (will also enable extra warnings (e.g., for deprecated code), and enable stub code generation for
	 * use with external IDEs).
	 */
	UPROPERTY(config, EditAnywhere, Category=Python, meta=(ConfigRestartRequired=true, DisplayName="Developer Mode (all users)"), AdvancedDisplay)
	bool bDeveloperMode;

	/** Should remote Python execution be enabled? */
	UPROPERTY(config, EditAnywhere, Category=PythonRemoteExecution, meta=(DisplayName="Enable Remote Execution?"))
	bool bRemoteExecution;

	/** The multicast group endpoint (in the form of IP_ADDRESS:PORT_NUMBER) that the UDP multicast socket should join */
	UPROPERTY(config, EditAnywhere, Category=PythonRemoteExecution, AdvancedDisplay, meta=(DisplayName="Multicast Group Endpoint"))
	FString RemoteExecutionMulticastGroupEndpoint;

	/** The adapter address that the UDP multicast socket should bind to, or 0.0.0.0 to bind to all adapters */
	UPROPERTY(config, EditAnywhere, Category=PythonRemoteExecution, AdvancedDisplay, meta=(DisplayName="Multicast Bind Address"))
	FString RemoteExecutionMulticastBindAddress;

	/** Size of the send buffer for the remote endpoint connection */
	UPROPERTY(config, EditAnywhere, Category=PythonRemoteExecution, AdvancedDisplay, meta=(DisplayName="Send Buffer Size", Units="Bytes"))
	int32 RemoteExecutionSendBufferSizeBytes;

	/** Size of the receive buffer for the remote endpoint connection */
	UPROPERTY(config, EditAnywhere, Category=PythonRemoteExecution, AdvancedDisplay, meta=(DisplayName="Receive Buffer Size", Units="Bytes"))
	int32 RemoteExecutionReceiveBufferSizeBytes;

	/** The TTL that the UDP multicast socket should use (0 is limited to the local host, 1 is limited to the local subnet) */
	UPROPERTY(config, EditAnywhere, Category=PythonRemoteExecution, AdvancedDisplay, meta=(DisplayName="Multicast Time-To-Live"))
	uint8 RemoteExecutionMulticastTtl;
};


UCLASS(config=EditorPerProjectUserSettings)
class UPythonScriptPluginUserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPythonScriptPluginUserSettings();

#if WITH_EDITOR
	//~ UDeveloperSettings interface
	virtual FText GetSectionText() const override;
#endif

	/**
	 * Should Developer Mode be enabled on the Python interpreter?
	 *
	 * (will also enable extra warnings (e.g., for deprecated code), and enable stub code generation for
	 * use with external IDEs).
	 */
	UPROPERTY(config, EditAnywhere, Category=Python, meta=(ConfigRestartRequired=true))
	bool bDeveloperMode;

	/** Should Python scripts be available in the Content Browser? */
	UPROPERTY(config, EditAnywhere, Category=Python, meta=(ConfigRestartRequired=true))
	bool bEnableContentBrowserIntegration;
};
