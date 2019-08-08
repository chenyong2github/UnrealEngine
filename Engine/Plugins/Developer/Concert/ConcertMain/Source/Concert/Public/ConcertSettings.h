// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "ConcertTransportSettings.h"
#include "ConcertSettings.generated.h"

namespace ConcertSettingsUtils
{

/** Returns an error messages if the user display name is invalid, otherwise, returns an empty text. */
FText CONCERT_API ValidateDisplayName(const FString& Name);

/** Returns an error messages if the specified session name is invalid, otherwise, returns an empty text. */
FText CONCERT_API ValidateSessionName(const FString& Name);

}

USTRUCT()
struct FConcertSessionSettings
{
	GENERATED_BODY()

	void Initialize()
	{
		ProjectName = FApp::GetProjectName();
		// TODO: BaseRevision should have a robust way to know which content version a project is on, as we currently check this using the current build version (see EngineVersion in FConcertSessionVersionInfo), which works for UGS but isn't reliable for public binary builds
	}

	bool ValidateRequirements(const FConcertSessionSettings& Other, FText* OutFailureReason = nullptr) const
	{
		if (ProjectName != Other.ProjectName)
		{
			if (OutFailureReason)
			{
				*OutFailureReason = FText::Format(NSLOCTEXT("ConcertMain", "Error_InvalidProjectNameFmt", "Invalid project name (expected '{0}', got '{1}')"), FText::AsCultureInvariant(ProjectName), FText::AsCultureInvariant(Other.ProjectName));
			}
			return false;
		}

		if (BaseRevision != Other.BaseRevision)
		{
			if (OutFailureReason)
			{
				*OutFailureReason = FText::Format(NSLOCTEXT("ConcertMain", "Error_InvalidBaseRevisionFmt", "Invalid base revision (expected '{0}', got '{1}')"), BaseRevision, Other.BaseRevision);
			}
			return false;
		}

		return true;
	}

	/**
	 * Name of the project of the session.
	 * Can be specified on the server cmd with `-CONCERTPROJECT=`
	 */
	UPROPERTY(config, VisibleAnywhere, Category="Session Settings")
	FString ProjectName;

	/**
	 * Base Revision the session was created at.
	 * Can be specified on the server cmd with `-CONCERTREVISION=`
	 */
	UPROPERTY(config, VisibleAnywhere, Category="Session Settings")
	uint32 BaseRevision = 0;

	/**
	 * Override the default name chosen when archiving this session.
	 * Can be specified on the server cmd with `-CONCERTSAVESESSIONAS=`
	 */
	UPROPERTY(config, VisibleAnywhere, Category="Session Settings")
	FString ArchiveNameOverride;

	// TODO: private session, password, etc etc,
};

USTRUCT()
struct FConcertServerSettings
{
	GENERATED_BODY()

	/** The server will allow client to join potentially incompatible sessions */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="Server Settings")
	bool bIgnoreSessionSettingsRestriction = false;

	/** The timespan at which session updates are processed. */
	UPROPERTY(config, EditAnywhere, DisplayName="Session Tick Frequency", AdvancedDisplay, Category="Server Settings", meta=(ForceUnits=s))
	int32 SessionTickFrequencySeconds = 1;
};

UCLASS(config=Engine)
class CONCERT_API UConcertServerConfig : public UObject
{
	GENERATED_BODY()
public:
	UConcertServerConfig();

	/**
	 * If true, instruct the server to auto-archive sessions that were left in the working directory because the server did not exit properly rather than
	 * restoring them as 'live' (the default).
	 */
	UPROPERTY(config)
	bool bAutoArchiveOnReboot = false;

	/**
	 * Clean server sessions working directory when booting
	 * Can be specified on the server cmd with `-CONCERTCLEAN`
	 */
	UPROPERTY(config, EditAnywhere, Category="Server Settings")
	bool bCleanWorkingDir;

	/**
	 * Number of archived sessions to keep when booting, or <0 to keep all archived sessions
	 */
	UPROPERTY(config, EditAnywhere, Category="Server Settings")
	int32 NumSessionsToKeep;

	/** 
	 * Name of the server, or empty to use the default name.
	 * Can be specified on the server cmd with `-CONCERTSERVER=`
	 */
	UPROPERTY(config, EditAnywhere, Category="Server Settings")
	FString ServerName;

	/** 
	 * Name of the default session created on the server.
	 * Can be specified on the server cmd with `-CONCERTSESSION=`
	 */
	UPROPERTY(config, EditAnywhere, Category="Session Settings")
	FString DefaultSessionName;

	/**
	 * Name of the default session to restore on the server.
	 * Set the name of the desired save to restore its content in your session.
	 * Leave this blank if you want to create an empty session.
	 * Can be specified on the editor cmd with `-CONCERTSESSIONTORESTORE=`.
	 */
	UPROPERTY(config, EditAnywhere, Category="Session Settings")
	FString DefaultSessionToRestore;

	/** Default server session settings */
	UPROPERTY(config, EditAnywhere, Category="Session Settings")
	FConcertSessionSettings DefaultSessionSettings;

	/** Server & server session settings */
	UPROPERTY(config, EditAnywhere, Category="Server Settings", meta=(ShowOnlyInnerProperties))
	FConcertServerSettings ServerSettings;

	/** Endpoint settings passed down to endpoints on creation */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="Endpoint Settings", meta=(ShowOnlyInnerProperties))
	FConcertEndpointSettings EndpointSettings;

	/** The directory where the server keeps the live session files. Can be specified on the server command line with `-CONCERTWORKINGDIR=`*/
	UPROPERTY(config)
	FString WorkingDir;

	/** The directory where the server keeps the archived session files. Can be specified on the server command line with `-CONCERTSAVEDDIR=`*/
	UPROPERTY(config)
	FString ArchiveDir;
};

USTRUCT()
struct FConcertClientSettings
{
	GENERATED_BODY()

	FConcertClientSettings()
		: DisplayName()
		, AvatarColor(1.0f, 1.0f, 1.0f, 1.0f)
		, DesktopAvatarActorClass(TEXT("/ConcertSyncClient/DesktopPresence.DesktopPresence_C"))
		, VRAvatarActorClass(TEXT("/ConcertSyncClient/VRPresence.VRPresence_C"))
		, DiscoveryTimeoutSeconds(5)
		, SessionTickFrequencySeconds(1)
		, LatencyCompensationMs(0)
	{}

	/** 
	 * The display name to use when in a session. 
	 * Can be specified on the editor cmd with `-CONCERTDISPLAYNAME=`.
	 */
	UPROPERTY(config, EditAnywhere, Category="Client Settings")
	FString DisplayName;

	/** The color used for the presence avatar in a session. */
	UPROPERTY(config, EditAnywhere, Category="Client Settings")
	FLinearColor AvatarColor;

	/** The desktop representation of this editor's user to other connected users */
	UPROPERTY(config, EditAnywhere, NoClear, Category = "Client Settings", meta = (MetaClass = "ConcertClientDesktopPresenceActor"))
	FSoftClassPath DesktopAvatarActorClass;

	/** The VR representation of this editor's user to other connected users */
	UPROPERTY(config, EditAnywhere, NoClear, Category = "Client Settings", meta = (MetaClass = "ConcertClientVRPresenceActor", DisplayName = "VR Avatar Actor Class"))
	FSoftClassPath VRAvatarActorClass;

	/** The timespan at which discovered Concert server are considered stale if they haven't answered back */
	UPROPERTY(config, EditAnywhere, DisplayName="Discovery Timeout", AdvancedDisplay, Category="Client Settings", meta=(ForceUnits=s))
	int32 DiscoveryTimeoutSeconds;

	/** The timespan at which session updates are processed. */
	UPROPERTY(config, EditAnywhere, DisplayName="Session Tick Frequency", AdvancedDisplay, Category="Client Settings", meta=(ForceUnits=s))
	int32 SessionTickFrequencySeconds;

	/** Amount of latency compensation to apply to time-synchronization sensitive interactions */
	UPROPERTY(config, EditAnywhere, DisplayName="Latency Compensation", AdvancedDisplay, Category="Client Settings", meta=(ForceUnits=ms))
	float LatencyCompensationMs;

	/** Array of tags that can be used for grouping and categorizing. */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Client Settings")
	TArray<FName> Tags;
};

UCLASS(config=Engine)
class CONCERT_API UConcertClientConfig : public UObject
{
	GENERATED_BODY()
public:
	UConcertClientConfig();

	/*
	 * Mark this setting object as editor only.
	 * This so soft object path reference made by this setting object won't be automatically grabbed by the cooker.
	 * @see UPackage::Save, FSoftObjectPathThreadContext::GetSerializationOptions, FSoftObjectPath::ImportTextItem
	 * @todo: cooker should have a better way to filter editor only objects for 'unsollicited' references.
	 */
	virtual bool IsEditorOnly() const
	{
		return true;
	}

	/**
	 * True if this client should be "headless"? (ie, not display any UI).
	 */
	UPROPERTY(config)
	bool bIsHeadless;

	/**
	 * True if the Multi-User module should install shortcut button and its drop-down menu in the level editor toolbar.
	 */
	UPROPERTY(config, EditAnywhere, Category="Client Settings", Meta=(ConfigRestartRequired=true, DisplayName="Enable Multi-User Toolbar Button"))
	bool bInstallEditorToolbarButton;

	/** 
	 * Automatically connect or create default session on default server. 
	 * Can be specified on the editor cmd with `-CONCERTAUTOCONNECT` or `-CONCERTAUTOCONNECT=<true/false>`.
	 */
	UPROPERTY(config, EditAnywhere, Category="Client Settings")
	bool bAutoConnect;

	/** 
	 * Default server url (just a name for now) to look for on auto or default connect. 
 	 * Can be specified on the editor cmd with `-CONCERTSERVER=`.
	 */
	UPROPERTY(config, EditAnywhere, Category="Client Settings")
	FString DefaultServerURL;

	/** 
	 * Default session name to look for on auto connect or default connect.
	 * Can be specified on the editor cmd with `-CONCERTSESSION=`.
	 */
	UPROPERTY(config, EditAnywhere, Category="Client Settings")
	FString DefaultSessionName;

	/**
	 * If this client create the default session, should the session restore a saved session.
	 * Set the name of the desired save to restore its content in your session.
	 * Leave this blank if you want to create an empty session.
	 * Can be specified on the editor cmd with `-CONCERTSESSIONTORESTORE=`.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Client Settings")
	FString DefaultSessionToRestore;

	/**
	 * If this client create the default session, should the session data be saved when it's deleted.
	 * Set the name desired for the save and the session data will be moved in that save when the session is deleted
	 * Leave this blank if you don't want to save the session data.
	 * Can be specified on the editor cmd with `-CONCERTSAVESESSIONAS=`.
	*/
	UPROPERTY(config, EditAnywhere, Category = "Client Settings")
	FString DefaultSaveSessionAs;

	/** Client & client session settings */
	UPROPERTY(config, EditAnywhere, Category="Client Settings", meta=(ShowOnlyInnerProperties))
	FConcertClientSettings ClientSettings;

	/** Endpoint settings passed down to endpoints on creation */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="Endpoint Settings", meta=(ShowOnlyInnerProperties))
	FConcertEndpointSettings EndpointSettings;
};
