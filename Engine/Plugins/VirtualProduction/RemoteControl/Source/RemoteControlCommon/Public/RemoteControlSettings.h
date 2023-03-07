// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "RemoteControlSettings.generated.h"

/**
 * Utility struct to represent IPv4 Network addresses.
 */
USTRUCT(BlueprintType)
struct FRCNetworkAddress
{
	GENERATED_BODY()

	FRCNetworkAddress() = default;

	bool operator==(const FRCNetworkAddress& OtherNetworkAddress) const
	{
		return ClassA == OtherNetworkAddress.ClassA
			&& ClassB == OtherNetworkAddress.ClassB
			&& ClassC == OtherNetworkAddress.ClassC
			&& ClassD == OtherNetworkAddress.ClassD;
	}

	/**
	 * Calculates the hash for a Network Address.
	 *
	 * @param NetworkAddress The Network address to calculate the hash for.
	 * @return The hash.
	 */
	friend uint32 GetTypeHash(const FRCNetworkAddress& NetworkAddress)
	{
		const uint32 NetworkPartHash = HashCombine(GetTypeHash(NetworkAddress.ClassA), GetTypeHash(NetworkAddress.ClassB));
		const uint32 HostPartHash = HashCombine(GetTypeHash(NetworkAddress.ClassC), GetTypeHash(NetworkAddress.ClassD));

		return HashCombine(NetworkPartHash, HostPartHash);
	}

	/**
	 * Retrieves the network address as string e.g. 192.168.1.1
	 */
	const FString ToString() const
	{
		return FString::Printf(TEXT("%d.%d.%d.%d"), ClassA, ClassB, ClassC, ClassD);
	}

	/** Denotes the first octet of the IPv4 address (0-255.xxx.xxx.xxx) */
	UPROPERTY(EditAnywhere, Category="Network Address")
		uint8 ClassA = 192;

	/** Denotes the second octet of the IPv4 address (xxx.0-255.xxx.xxx) */
	UPROPERTY(EditAnywhere, Category = "Network Address")
		uint8 ClassB = 168;

	/** Denotes the third octet of the IPv4 address (xxx.xxx.0-255.xxx) */
	UPROPERTY(EditAnywhere, Category = "Network Address")
		uint8 ClassC = 1;

	/** Denotes the fourth octet of the IPv4 address (xxx.xxx.xxx.0-255) */
	UPROPERTY(EditAnywhere, Category = "Network Address")
		uint8 ClassD = 1;
};

/**
 * Utility struct to represent range of IPv4 Network addresses.
 */
USTRUCT(BlueprintType)
struct FRCNetworkAddressRange
{
	GENERATED_BODY()

	FRCNetworkAddressRange() = default;

	bool operator==(const FRCNetworkAddressRange& OtherNetworkAddressRange) const
	{
		return LowerBound == OtherNetworkAddressRange.LowerBound
			&& UpperBound == OtherNetworkAddressRange.UpperBound;
	}
	
	/**
	 * Calculates the hash for a Network Address Range.
	 *
	 * @param NetworkAddress The Network address range to calculate the hash for.
	 * @return The hash.
	 */
	friend uint32 GetTypeHash(const FRCNetworkAddressRange& NetworkAddressRange)
	{
		const uint32 LowerBoundHash = GetTypeHash(NetworkAddressRange.LowerBound);
		const uint32 UpperBoundHash = GetTypeHash(NetworkAddressRange.UpperBound);

		return HashCombine(LowerBoundHash, UpperBoundHash);
	}

	bool IsInRange(const FString& InNetworkAddress) const
	{
		TArray<FString> IndividualClasses;

		if (InNetworkAddress.ParseIntoArray(IndividualClasses, TEXT(".")) == 4)
		{
			return IsInRange_Internal(FCString::Atoi(*IndividualClasses[0])
				, FCString::Atoi(*IndividualClasses[1])
				, FCString::Atoi(*IndividualClasses[2])
				, FCString::Atoi(*IndividualClasses[3])
			);
		}

		return false;
	}
	
	bool IsInRange(const FRCNetworkAddress& InNetworkAddress) const
	{
		return IsInRange_Internal(InNetworkAddress.ClassA
			, InNetworkAddress.ClassB
			, InNetworkAddress.ClassC
			, InNetworkAddress.ClassD
		);
	}

private:

	bool IsInRange_Internal(uint8 InClassA, uint8 InClassB, uint8 InClassC, uint8 InClassD) const
	{
		bool bClassAIsInRange = InClassA >= LowerBound.ClassA && InClassA <= UpperBound.ClassA;
		bool bClassBIsInRange = InClassB >= LowerBound.ClassB && InClassB <= UpperBound.ClassB;
		bool bClassCIsInRange = InClassC >= LowerBound.ClassC && InClassC <= UpperBound.ClassC;
		bool bClassDIsInRange = InClassD >= LowerBound.ClassD && InClassD <= UpperBound.ClassD;
		
		return bClassAIsInRange && bClassBIsInRange && bClassCIsInRange && bClassDIsInRange;
	}

public:

	/** Denotes the lower bound IPv4 address. */
	UPROPERTY(EditAnywhere, Category="Network Address Range")
		FRCNetworkAddress LowerBound = { 192, 168, 1, 1 };

	/** Denotes the upper bound IPv4 address. */
	UPROPERTY(EditAnywhere, Category = "Network Address Range")
		FRCNetworkAddress UpperBound = { 192, 168, 255, 255 };
};

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

	virtual bool IsClientAllowed(const FString& InClientAddressStr) const
	{
		for (const FRCNetworkAddressRange& AllowlistedClient : AllowlistedClients)
		{
			if (AllowlistedClient.IsInRange(InClientAddressStr))
			{
				return true;
			}
		}

		return false;
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

	/** List of IP Addresses that are allowed to access the Web API. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control | Security", DisplayName = "Range of Allowlisted Clients", Meta = (EditCondition = "bRestrictServerAccess", EditConditionHides))
	TSet<FRCNetworkAddressRange> AllowlistedClients;

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
