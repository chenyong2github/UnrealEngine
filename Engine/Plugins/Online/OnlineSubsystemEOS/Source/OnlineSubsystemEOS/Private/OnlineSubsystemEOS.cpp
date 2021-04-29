// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemEOS.h"
#include "UserManagerEOS.h"
#include "OnlineSessionEOS.h"
#include "OnlineStatsEOS.h"
#include "OnlineLeaderboardsEOS.h"
#include "OnlineAchievementsEOS.h"
#include "OnlineTitleFileEOS.h"
#include "OnlineUserCloudEOS.h"
#include "OnlineStoreEOS.h"
#include "EOSSettings.h"
#include "IEOSSDKManager.h"

#include "Features/IModularFeatures.h"
#include "Misc/App.h"
#include "Misc/NetworkVersion.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_EOS_SDK

// Missing defines
#define EOS_ENCRYPTION_KEY_MAX_LENGTH 64
#define EOS_ENCRYPTION_KEY_MAX_BUFFER_LEN (EOS_ENCRYPTION_KEY_MAX_LENGTH + 1)

namespace {
	IEOSSDKManager* GetEOSSDKManager()
	{
		const FName EOSSDKManagerFeatureName = TEXT("EOSSDKManager");
		if (IModularFeatures::Get().IsModularFeatureAvailable(EOSSDKManagerFeatureName))
		{
			return &IModularFeatures::Get().GetModularFeature<IEOSSDKManager>(EOSSDKManagerFeatureName);
		}
		return nullptr;
	}
}

/** Class that holds the strings for the call duration */
struct FEOSPlatformOptions :
	public EOS_Platform_Options
{
	FEOSPlatformOptions() :
		EOS_Platform_Options()
	{
		ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
		ProductId = ProductIdAnsi;
		SandboxId = SandboxIdAnsi;
		DeploymentId = DeploymentIdAnsi;
		ClientCredentials.ClientId = ClientIdAnsi;
		ClientCredentials.ClientSecret = ClientSecretAnsi;
		CacheDirectory = CacheDirectoryAnsi;
		EncryptionKey = EncryptionKeyAnsi;
	}

	char ClientIdAnsi[EOS_OSS_STRING_BUFFER_LENGTH];
	char ClientSecretAnsi[EOS_OSS_STRING_BUFFER_LENGTH];
	char ProductIdAnsi[EOS_OSS_STRING_BUFFER_LENGTH];
	char SandboxIdAnsi[EOS_OSS_STRING_BUFFER_LENGTH];
	char DeploymentIdAnsi[EOS_OSS_STRING_BUFFER_LENGTH];
	char CacheDirectoryAnsi[EOS_OSS_STRING_BUFFER_LENGTH];
	char EncryptionKeyAnsi[EOS_ENCRYPTION_KEY_MAX_BUFFER_LEN];
};

FPlatformEOSHelpersPtr FOnlineSubsystemEOS::EOSHelpersPtr;

void FOnlineSubsystemEOS::ModuleInit()
{
	EOSHelpersPtr = MakeShareable(new FPlatformEOSHelpers());

	const FName EOSSharedModuleName = TEXT("EOSShared");
	if (!FModuleManager::Get().IsModuleLoaded(EOSSharedModuleName))
	{
		FModuleManager::Get().LoadModule(EOSSharedModuleName);
	}
	IEOSSDKManager* EOSSDKManager = GetEOSSDKManager();
	if (!EOSSDKManager)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: Missing IEOSSDKManager modular feature."));
		return;
	}

	EOS_EResult InitResult = EOSSDKManager->Initialize();
	if (InitResult != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to initialize the EOS SDK with result code (%s)"), ANSI_TO_TCHAR(EOS_EResult_ToString(InitResult)));
		return;
	}
}

void FOnlineSubsystemEOS::ModuleShutdown()
{
	DESTRUCT_INTERFACE(EOSHelpersPtr);
}

/** Common method for creating the EOS platform */
bool FOnlineSubsystemEOS::PlatformCreate()
{
	FString ArtifactName;
	FParse::Value(FCommandLine::Get(), TEXT("EpicApp="), ArtifactName);
	// Find the settings for this artifact
	FEOSArtifactSettings ArtifactSettings;
	if (!UEOSSettings::GetSettingsForArtifact(ArtifactName, ArtifactSettings))
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS::PlatformCreate() failed to find artifact settings object for artifact (%s)"), *ArtifactName);
		return false;
	}

	EOSSDKManager = GetEOSSDKManager();
	if (!EOSSDKManager)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS::PlatformCreate() failed to init EOS platform"));
		return false;
	}

	// Create platform instance
	FEOSPlatformOptions PlatformOptions;
	FCStringAnsi::Strncpy(PlatformOptions.ClientIdAnsi, TCHAR_TO_UTF8(*ArtifactSettings.ClientId), EOS_OSS_STRING_BUFFER_LENGTH);
	FCStringAnsi::Strncpy(PlatformOptions.ClientSecretAnsi, TCHAR_TO_UTF8(*ArtifactSettings.ClientSecret), EOS_OSS_STRING_BUFFER_LENGTH);
	FCStringAnsi::Strncpy(PlatformOptions.ProductIdAnsi, TCHAR_TO_UTF8(*ArtifactSettings.ProductId), EOS_OSS_STRING_BUFFER_LENGTH);
	FCStringAnsi::Strncpy(PlatformOptions.SandboxIdAnsi, TCHAR_TO_UTF8(*ArtifactSettings.SandboxId), EOS_OSS_STRING_BUFFER_LENGTH);
	FCStringAnsi::Strncpy(PlatformOptions.DeploymentIdAnsi, TCHAR_TO_UTF8(*ArtifactSettings.DeploymentId), EOS_OSS_STRING_BUFFER_LENGTH);
	PlatformOptions.bIsServer = IsRunningDedicatedServer() ? EOS_TRUE : EOS_FALSE;
	PlatformOptions.Reserved = nullptr;
	FEOSSettings EOSSettings = UEOSSettings::GetSettings();
	uint64 OverlayFlags = 0;
	if (!EOSSettings.bEnableOverlay)
	{
		OverlayFlags |= EOS_PF_DISABLE_OVERLAY;
	}
	if (!EOSSettings.bEnableSocialOverlay)
	{
		OverlayFlags |= EOS_PF_DISABLE_SOCIAL_OVERLAY;
	}
	PlatformOptions.Flags = IsRunningGame() ? OverlayFlags : EOS_PF_DISABLE_OVERLAY;
	// Make the cache directory be in the user's writable area

	FString CacheDir = EOSHelpersPtr->PlatformCreateCacheDir(ArtifactName, EOSSettings.CacheDir);
	FCStringAnsi::Strncpy(PlatformOptions.CacheDirectoryAnsi, TCHAR_TO_UTF8(*CacheDir), EOS_OSS_STRING_BUFFER_LENGTH);
	FCStringAnsi::Strncpy(PlatformOptions.EncryptionKeyAnsi, TCHAR_TO_UTF8(*ArtifactSettings.EncryptionKey), EOS_ENCRYPTION_KEY_MAX_BUFFER_LEN);

	EOSPlatformHandle = EOSSDKManager->CreatePlatform(PlatformOptions);
	if (EOSPlatformHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS::PlatformCreate() failed to init EOS platform"));
		return false;
	}
	
	return true;
}

bool FOnlineSubsystemEOS::Init()
{
	// Determine if we are the default and if we're the platform OSS
	FString DefaultOSS;
	GConfig->GetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), DefaultOSS, GEngineIni);
	FString PlatformOSS;
	GConfig->GetString(TEXT("OnlineSubsystem"), TEXT("NativePlatformService"), PlatformOSS, GEngineIni);
	bIsDefaultOSS = DefaultOSS == TEXT("EOS");
	bIsPlatformOSS = DefaultOSS == TEXT("EOS");

	// Check for being launched by EGS
	bWasLaunchedByEGS = FParse::Param(FCommandLine::Get(), TEXT("EpicPortal"));
	FEOSSettings EOSSettings = UEOSSettings::GetSettings();
	if (!IsRunningDedicatedServer() && !bWasLaunchedByEGS && EOSSettings.bShouldEnforceBeingLaunchedByEGS)
	{
		FString ArtifactName;
		FParse::Value(FCommandLine::Get(), TEXT("EpicApp="), ArtifactName);
		UE_LOG_ONLINE(Warning, TEXT("FOnlineSubsystemEOS::Init() relaunching artifact (%s) via the store"), *ArtifactName);
		FPlatformProcess::LaunchURL(*FString::Printf(TEXT("com.epicgames.launcher://store/product/%s?action=launch&silent=true"), *ArtifactName), nullptr, nullptr);
		FPlatformMisc::RequestExit(false);
		return false;
	}

	if (!PlatformCreate())
	{
		return false;
	}

	// Get handles for later use
	AuthHandle = EOS_Platform_GetAuthInterface(EOSPlatformHandle);
	if (AuthHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get auth handle"));
		return false;
	}
	UserInfoHandle = EOS_Platform_GetUserInfoInterface(EOSPlatformHandle);
	if (UserInfoHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get user info handle"));
		return false;
	}
	UIHandle = EOS_Platform_GetUIInterface(EOSPlatformHandle);
	if (UIHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get UI handle"));
		return false;
	}
	FriendsHandle = EOS_Platform_GetFriendsInterface(EOSPlatformHandle);
	if (FriendsHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get friends handle"));
		return false;
	}
	PresenceHandle = EOS_Platform_GetPresenceInterface(EOSPlatformHandle);
	if (PresenceHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get presence handle"));
		return false;
	}
	ConnectHandle = EOS_Platform_GetConnectInterface(EOSPlatformHandle);
	if (ConnectHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get connect handle"));
		return false;
	}
	SessionsHandle = EOS_Platform_GetSessionsInterface(EOSPlatformHandle);
	if (SessionsHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get sessions handle"));
		return false;
	}
	StatsHandle = EOS_Platform_GetStatsInterface(EOSPlatformHandle);
	if (StatsHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get stats handle"));
		return false;
	}
	LeaderboardsHandle = EOS_Platform_GetLeaderboardsInterface(EOSPlatformHandle);
	if (LeaderboardsHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get leaderboards handle"));
		return false;
	}
	MetricsHandle = EOS_Platform_GetMetricsInterface(EOSPlatformHandle);
	if (MetricsHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get metrics handle"));
		return false;
	}
	AchievementsHandle = EOS_Platform_GetAchievementsInterface(EOSPlatformHandle);
	if (AchievementsHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get achievements handle"));
		return false;
	}
	P2PHandle = EOS_Platform_GetP2PInterface(EOSPlatformHandle);
	if (P2PHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get p2p handle"));
		return false;
	}
	// Disable ecom if not part of EGS
	if (bWasLaunchedByEGS)
	{
		EcomHandle = EOS_Platform_GetEcomInterface(EOSPlatformHandle);
		if (EcomHandle == nullptr)
		{
			UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get ecom handle"));
			return false;
		}
		StoreInterfacePtr = MakeShareable(new FOnlineStoreEOS(this));
	}
	TitleStorageHandle = EOS_Platform_GetTitleStorageInterface(EOSPlatformHandle);
	if (TitleStorageHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get title storage handle"));
		return false;
	}
	PlayerDataStorageHandle = EOS_Platform_GetPlayerDataStorageInterface(EOSPlatformHandle);
	if (PlayerDataStorageHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init EOS platform, couldn't get player data storage handle"));
		return false;
	}

	SocketSubsystem = MakeShareable(new FSocketSubsystemEOS(this));
	FString ErrorMessage;
	SocketSubsystem->Init(ErrorMessage);

	UserManager = MakeShareable(new FUserManagerEOS(this));
	SessionInterfacePtr = MakeShareable(new FOnlineSessionEOS(this));
	// Set the bucket id to use for all sessions based upon the name and version to avoid upgrade issues
	SessionInterfacePtr->Init(EOSSDKManager->GetProductName() + TEXT("_") + EOSSDKManager->GetProductVersion());
	StatsInterfacePtr = MakeShareable(new FOnlineStatsEOS(this));
	LeaderboardsInterfacePtr = MakeShareable(new FOnlineLeaderboardsEOS(this));
	AchievementsInterfacePtr = MakeShareable(new FOnlineAchievementsEOS(this));
	TitleFileInterfacePtr = MakeShareable(new FOnlineTitleFileEOS(this));
	UserCloudInterfacePtr = MakeShareable(new FOnlineUserCloudEOS(this));

	// We initialized ok so we can tick
	StartTicker();

	return true;
}

bool FOnlineSubsystemEOS::Shutdown()
{
	UE_LOG_ONLINE(VeryVerbose, TEXT("FOnlineSubsystemEOS::Shutdown()"));

	StopTicker();
	FOnlineSubsystemImpl::Shutdown();

#if !WITH_EDITOR
	EOS_EResult ShutdownResult = EOS_Shutdown();
	if (ShutdownResult != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to shutdown the EOS SDK with result code (%d)"), (int32)ShutdownResult);
	}
#endif

#define DESTRUCT_INTERFACE(Interface) \
	if (Interface.IsValid()) \
	{ \
		ensure(Interface.IsUnique()); \
		Interface = nullptr; \
	}

	// Destruct the interfaces
	DESTRUCT_INTERFACE(UserManager);
	DESTRUCT_INTERFACE(SessionInterfacePtr);
	DESTRUCT_INTERFACE(StatsInterfacePtr);
	DESTRUCT_INTERFACE(LeaderboardsInterfacePtr);
	DESTRUCT_INTERFACE(AchievementsInterfacePtr);
	DESTRUCT_INTERFACE(StoreInterfacePtr);
	DESTRUCT_INTERFACE(TitleFileInterfacePtr);
	DESTRUCT_INTERFACE(UserCloudInterfacePtr);

#undef DESTRUCT_INTERFACE

	return true;
}

bool FOnlineSubsystemEOS::Tick(float DeltaTime)
{
	if (!bTickerStarted)
	{
		return true;
	}

	SessionInterfacePtr->Tick(DeltaTime);
	FOnlineSubsystemImpl::Tick(DeltaTime);

	return true;
}

bool FOnlineSubsystemEOS::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FOnlineSubsystemImpl::Exec(InWorld, Cmd, Ar))
	{
		return true;
	}

	bool bWasHandled = false;
	if (FParse::Command(&Cmd, TEXT("EOS")))
	{
		if (StoreInterfacePtr != nullptr && FParse::Command(&Cmd, TEXT("ECOM")))
		{
			bWasHandled = StoreInterfacePtr->HandleEcomExec(InWorld, Cmd, Ar);
		}
		else if (TitleFileInterfacePtr != nullptr && FParse::Command(&Cmd, TEXT("TITLEFILE")))
		{
			bWasHandled = TitleFileInterfacePtr->HandleTitleFileExec(InWorld, Cmd, Ar);
		}
		else if (UserCloudInterfacePtr != nullptr && FParse::Command(&Cmd, TEXT("USERCLOUD")))
		{
			bWasHandled = UserCloudInterfacePtr->HandleUserCloudExec(InWorld, Cmd, Ar);
		}
	}
	return bWasHandled;
}

FString FOnlineSubsystemEOS::GetAppId() const
{
	return TEXT("");
}

FText FOnlineSubsystemEOS::GetOnlineServiceName() const
{
	return NSLOCTEXT("OnlineSubsystemEOS", "OnlineServiceName", "EOS");
}

IOnlineSessionPtr FOnlineSubsystemEOS::GetSessionInterface() const
{
	return SessionInterfacePtr;
}

IOnlineFriendsPtr FOnlineSubsystemEOS::GetFriendsInterface() const
{
	return UserManager;
}

IOnlineSharedCloudPtr FOnlineSubsystemEOS::GetSharedCloudInterface() const
{
	UE_LOG_ONLINE(Error, TEXT("Shared Cloud Interface Requested"));
	return nullptr;
}

IOnlineUserCloudPtr FOnlineSubsystemEOS::GetUserCloudInterface() const
{
	return UserCloudInterfacePtr;
}

IOnlineEntitlementsPtr FOnlineSubsystemEOS::GetEntitlementsInterface() const
{
	UE_LOG_ONLINE(Error, TEXT("Entitlements Interface Requested"));
	return nullptr;
};

IOnlineLeaderboardsPtr FOnlineSubsystemEOS::GetLeaderboardsInterface() const
{
	return LeaderboardsInterfacePtr;
}

IOnlineVoicePtr FOnlineSubsystemEOS::GetVoiceInterface() const
{
	return nullptr;
}

IOnlineExternalUIPtr FOnlineSubsystemEOS::GetExternalUIInterface() const
{
	return UserManager;
}

IOnlineIdentityPtr FOnlineSubsystemEOS::GetIdentityInterface() const
{
	return UserManager;
}

IOnlineTitleFilePtr FOnlineSubsystemEOS::GetTitleFileInterface() const
{
	return TitleFileInterfacePtr;
}

IOnlineStoreV2Ptr FOnlineSubsystemEOS::GetStoreV2Interface() const
{
	return StoreInterfacePtr;
}

IOnlinePurchasePtr FOnlineSubsystemEOS::GetPurchaseInterface() const
{
	return StoreInterfacePtr;
}

IOnlineAchievementsPtr FOnlineSubsystemEOS::GetAchievementsInterface() const
{
	return AchievementsInterfacePtr;
}

IOnlineUserPtr FOnlineSubsystemEOS::GetUserInterface() const
{
	return UserManager;
}

IOnlinePresencePtr FOnlineSubsystemEOS::GetPresenceInterface() const
{
	return UserManager;
}

IOnlineStatsPtr FOnlineSubsystemEOS::GetStatsInterface() const
{
	return StatsInterfacePtr;
}

#endif
