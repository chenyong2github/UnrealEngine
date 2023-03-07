// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "RemoteControlSettings.generated.h"

/**
 * Passphrase Struct
 */
USTRUCT(BlueprintType)
struct FRCPassphrase
{
	GENERATED_BODY()
	
	FRCPassphrase(){}

	UPROPERTY(EditAnywhere, Category="Passphrase")
	FString Identifier;
	
	UPROPERTY(EditAnywhere, Category="Passphrase")
	FString Passphrase;
};

/**
 * Global remote control settings
 */
UCLASS(config = RemoteControl)
class REMOTECONTROLCOMMON_API URemoteControlSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Gets the settings container name for the settings, either Project or Editor */
	virtual FName GetContainerName() const
	{
		return "Project";
	}
	
	/** Gets the category for the settings, some high level grouping like, Editor, Engine, Game...etc. */
	virtual FName GetCategoryName() const
	{
		return "Plugins";
	}
	
	/** The unique name for your section of settings, uses the class's FName. */
	virtual FName GetSectionName() const
	{
		return "Remote Control";
	}

	virtual FText GetSectionText() const
	{
		return NSLOCTEXT("RemoteControlSettings", "RemoteControlSettingsSection", "Remote Control");
	}

	virtual TArray<FString> GetHashedPassphrases() const
	{
		TArray<FString> OutArray;

		for (const FRCPassphrase& Passphrase : Passphrases)
		{
			OutArray.Add(Passphrase.Passphrase);
		}
		
		return OutArray;
	}

	/**
	 * Should transactions be generated for events received through protocols (ie. MIDI, DMX etc.)
	 * Disabling transactions improves performance but will prevent events from being transacted to Multi-User
	 * unless using the Remote Control Interception feature.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control")
	bool bProtocolsGenerateTransactions = true;

	/** The remote control web app http port. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Interface", DisplayName = "Remote Control Web Interface http Port")
	uint32 RemoteControlWebInterfacePort = 30000;

	/** Should force a build of the WebApp at startup. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Interface", DisplayName = "Force WebApp build at startup")
	bool bForceWebAppBuildAtStartup = false;

	/** Should WebApp log timing. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Interface", DisplayName = "Log WebApp requests handle duration")
	bool bWebAppLogRequestDuration = false;

	/** Whether web server is started automatically. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Server")
	bool bAutoStartWebServer = true;

	/** Whether web socket server is started automatically. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Server")
	bool bAutoStartWebSocketServer = true;

	/** The web remote control HTTP server's port. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Server", DisplayName = "Remote Control HTTP Server Port")
	uint32 RemoteControlHttpServerPort = 30010;

	/** The web remote control WebSocket server's port. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Server", DisplayName = "Remote Control WebSocket Server Port")
	uint32 RemoteControlWebSocketServerPort = 30020;

	/** Show a warning icon for exposed editor-only fields. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Preset", DisplayName = "Show a warning when exposing editor-only entities.")
	bool bDisplayInEditorOnlyWarnings = false;

	/** The split widget control ratio between entity tree and details/protocol binding list. */
	UPROPERTY(config)
	float TreeBindingSplitRatio = 0.7;

	UPROPERTY(config)
	bool bUseRebindingContext = true;

	UPROPERTY(config, EditAnywhere, Category = "Remote Control Preset", DisplayName = "Ignore Remote Control Protected Check")
	bool bIgnoreProtectedCheck = false;

	UPROPERTY(Config, EditAnywhere, Category = "Remote Control Preset", DisplayName = "Ignore Remote Control Getter/Setter Check")
	bool bIgnoreGetterSetterCheck = false;

	UPROPERTY(Config, EditAnywhere, Category = "Remote Control Preset")
	bool bIgnoreWarnings = false;

	/** Whether to restrict access to a list of hostname/IPs in the AllowedOrigins setting. */
	UPROPERTY(Config, EditAnywhere, Category = "Remote Control | Security")
	bool bRestrictServerAccess = false;
	
	/** Whether communication with the Web Interface should only be allowed with an Passphrase */
	UPROPERTY(Config, EditAnywhere, Category = "Remote Control | Security", DisplayName = "Use Passphrase to block Access")
	bool bUseRemoteControlPassphrase = false;

	/** Whether the User should be warned that Passphrase usage is disabled or now. Initially activated */
	UPROPERTY(Config, EditAnywhere, Category = "Remote Control | Security", DisplayName = "Warn that Passphrase might be disabled ")
	bool bShowPassphraseDisabledWarning = true;

	/** Enable remote python execution, enabling this could open you open to vulnerabilities if an outside actor has access to your server. */
	UPROPERTY(Config, EditAnywhere, Category = "Remote Control | Security")
	bool bEnableRemotePythonExecution = false;
	
	/** 
	 * Origin that can make requests to the remote control server. Should contain the hostname or IP of the server making requests to remote control. ie. "http://yourdomain.com", or "*" to allow all origins. 
	 * @Note: This is used to block requests coming from a browser (ie. Coming from a script running on a website), ideally you should use both this setting and AllowedIPs, as a request coming from a websocket client can have an empty Origin.
	 * @Note Supports wildcards (ie. *.yourdomain.com)
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Remote Control | Security", meta=(EditCondition = bRestrictServerAccess))
	FString AllowedOrigin = TEXT("*");
	
	/** 
	 * What IP is allowed to make request to the RemoteControl and RemoteControl Websocket servers.
	 * @Note If empty or *.*.*.*,  all IP addresses will be able to make requests to your servers if they are on your network, so consider limiting this to a range of IPs that you own.
	 * @Note Using this setting without AllowedOrigin can potentially open you up to malicous requests coming from a website, as the request would come from localhost.
	 * @Note Supports wildcards (ie. 202.120.*.*)
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Remote Control | Security", meta = (EditCondition = bRestrictServerAccess))
	FString AllowedIP = TEXT("127.0.0.1");

	/**
	 * Controls whether a passphrase should be required when remote control is accessed by a client outside of localhost.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control | Security")
	bool bEnforcePassphraseForRemoteClients = false;
	
	/**
     * List of IPs that are explicitly *not* required to have a passphrase when accessing remote control.
     */
    UPROPERTY(config, EditAnywhere, Category = "Remote Control | Security", meta= (EditCondition = bEnforcePassphraseForRemoteClients))
    TArray<FString> AllowedIPsForRemotePassphrases;
	
	UPROPERTY(config, EditAnywhere, Category = "Remote Control | Security", DisplayName = "Remote Control Passphrase")
	TArray<FRCPassphrase> Passphrases = {};

private:
	UPROPERTY(config)
    bool bSecuritySettingsReviewed = false;
};
