// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemEOS.h"
#include "UserManagerEOS.h"
#include "OnlineSessionEOS.h"
#include "OnlineStatsEOS.h"
#include "OnlineLeaderboardsEOS.h"
#include "OnlineAchievementsEOS.h"
#include "OnlineStoreEOS.h"
#include "EOSSettings.h"

#include "Misc/NetworkVersion.h"
#include "Misc/App.h"

DECLARE_CYCLE_STAT(TEXT("Tick"), STAT_EOS_Tick, STATGROUP_EOS);


#if WITH_EOS_SDK

#include "eos_logging.h"

typedef struct _InternalData
{
	int32_t ApiVersion;
	const char* BackendEnvironment;
} InternalData;

void EOS_CALL EOSFree(void* Ptr)
{
	FMemory::Free(Ptr);
}

void* EOS_CALL EOSMalloc(size_t Size, size_t Alignment)
{
	return FMemory::Malloc(Size, Alignment);
}

void* EOS_CALL EOSRealloc(void* Ptr, size_t Size, size_t Alignment)
{
	return FMemory::Realloc(Ptr, Size, Alignment);
}

void EOS_CALL EOSLog(const EOS_LogMessage* InMsg)
{
	if (GLog == nullptr)
	{
		return;
	}

	switch (InMsg->Level)
	{
		case EOS_ELogLevel::EOS_LOG_Fatal:
		{
			UE_LOG_ONLINE(Fatal, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
		case EOS_ELogLevel::EOS_LOG_Error:
		{
			UE_LOG_ONLINE(Error, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
		case EOS_ELogLevel::EOS_LOG_Warning:
		{
			UE_LOG_ONLINE(Warning, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
		case EOS_ELogLevel::EOS_LOG_Verbose:
		{
			UE_LOG_ONLINE(Verbose, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
		case EOS_ELogLevel::EOS_LOG_VeryVerbose:
		{
			UE_LOG_ONLINE(VeryVerbose, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
		case EOS_ELogLevel::EOS_LOG_Info:
		default:
		{
			UE_LOG_ONLINE(Log, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
	}
}

// Missing defines
#define EOS_ENCRYPTION_KEY_MAX_LENGTH 64
#define EOS_ENCRYPTION_KEY_MAX_BUFFER_LEN (EOS_ENCRYPTION_KEY_MAX_LENGTH + 1)

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

static char GProductNameAnsi[EOS_PRODUCTNAME_MAX_BUFFER_LEN];
static char GProductVersionAnsi[EOS_PRODUCTVERSION_MAX_BUFFER_LEN];
FString ProductName;
FString ProductVersion;

EOS_PlatformHandle* GEOSPlatformHandle = NULL;

void FOnlineSubsystemEOS::ModuleInit()
{
	// Init EOS SDK
	EOS_InitializeOptions SDKOptions = { };
	SDKOptions.ApiVersion = EOS_INITIALIZE_API_LATEST;
	ProductName = FApp::GetProjectName();
	FCStringAnsi::Strncpy(GProductNameAnsi, TCHAR_TO_UTF8(*ProductName), EOS_PRODUCTNAME_MAX_BUFFER_LEN);
	SDKOptions.ProductName = GProductNameAnsi;
	ProductVersion = FNetworkVersion::GetProjectVersion();
	if (ProductVersion.IsEmpty())
	{
		ProductVersion = TEXT("Unknown");
	}
	FCStringAnsi::Strncpy(GProductVersionAnsi, TCHAR_TO_UTF8(*ProductVersion), EOS_PRODUCTVERSION_MAX_BUFFER_LEN);
	SDKOptions.ProductVersion = GProductVersionAnsi;
	SDKOptions.AllocateMemoryFunction = &EOSMalloc;
	SDKOptions.ReallocateMemoryFunction = &EOSRealloc;
	SDKOptions.ReleaseMemoryFunction = &EOSFree;

	EOS_EResult InitResult = EOS_Initialize(&SDKOptions);
	if (InitResult != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to initialize the EOS SDK with result code (%s)"), ANSI_TO_TCHAR(EOS_EResult_ToString(InitResult)));
		return;
	}
#if !UE_BUILD_SHIPPING
	InitResult = EOS_Logging_SetCallback(&EOSLog);
	if (InitResult != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS: failed to init logging with result code %s"), ANSI_TO_TCHAR(EOS_EResult_ToString(InitResult)));
	}
	EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, UE_BUILD_DEBUG ? EOS_ELogLevel::EOS_LOG_Verbose : EOS_ELogLevel::EOS_LOG_Info);
#endif
	if (IsRunningGame() || IsRunningDedicatedServer())
	{
		GEOSPlatformHandle = FOnlineSubsystemEOS::PlatformCreate();
	}
}

/** Common method for creating the EOS platform */
EOS_PlatformHandle* FOnlineSubsystemEOS::PlatformCreate()
{
	FString ArtifactName;
	FParse::Value(FCommandLine::Get(), TEXT("EpicApp="), ArtifactName);
	// Find the settings for this artifact
	FEOSArtifactSettings ArtifactSettings;
	if (!UEOSSettings::GetSettingsForArtifact(ArtifactName, ArtifactSettings))
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS::DeferredInit() failed to find artifact settings object for artifact (%s)"), *ArtifactName);
		return nullptr;
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
	FString CacheDir = FPlatformProcess::UserDir() / ArtifactName / EOSSettings.CacheDir;
	FCStringAnsi::Strncpy(PlatformOptions.CacheDirectoryAnsi, TCHAR_TO_UTF8(*CacheDir), EOS_OSS_STRING_BUFFER_LENGTH);
	FCStringAnsi::Strncpy(PlatformOptions.EncryptionKeyAnsi, TCHAR_TO_UTF8(*ArtifactSettings.EncryptionKey), EOS_ENCRYPTION_KEY_MAX_BUFFER_LEN);

	EOS_PlatformHandle* EOSPlatformHandle = EOS_Platform_Create(&PlatformOptions);
	if (EOSPlatformHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOS::PlatformCreate() failed to init EOS platform"));
		return nullptr;
	}
	return EOSPlatformHandle;
}

bool FOnlineSubsystemEOS::Init()
{
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

	// The editor build will call this completely differently, so give it a chance to run it
	if (IsRunningGame() || IsRunningDedicatedServer())
	{
		EOSPlatformHandle = GEOSPlatformHandle;
	}
	else
	{
		EOSPlatformHandle = FOnlineSubsystemEOS::PlatformCreate();
	}
	if (EOSPlatformHandle == nullptr)
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

	SocketSubsystem = MakeShareable(new FSocketSubsystemEOS(this));
	FString ErrorMessage;
	SocketSubsystem->Init(ErrorMessage);

	FCStringAnsi::Strncpy(ProductNameAnsi, GProductNameAnsi, EOS_PRODUCTNAME_MAX_BUFFER_LEN);
	FCStringAnsi::Strncpy(ProductVersionAnsi, GProductVersionAnsi, EOS_PRODUCTVERSION_MAX_BUFFER_LEN);

	UserManager = MakeShareable(new FUserManagerEOS(this));
	SessionInterfacePtr = MakeShareable(new FOnlineSessionEOS(this));
	// Set the bucket id to use for all sessions based upon the name and version to avoid upgrade issues
	SessionInterfacePtr->Init(TCHAR_TO_UTF8(*(ProductName + TEXT("_") + ProductVersion)));
	StatsInterfacePtr = MakeShareable(new FOnlineStatsEOS(this));
	LeaderboardsInterfacePtr = MakeShareable(new FOnlineLeaderboardsEOS(this));
	AchievementsInterfacePtr = MakeShareable(new FOnlineAchievementsEOS(this));

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

#undef DESTRUCT_INTERFACE

	return true;
}

bool FOnlineSubsystemEOS::Tick(float DeltaTime)
{
	if (!bTickerStarted)
	{
		return true;
	}

	{
		FScopeCycleCounter Scope(GET_STATID(STAT_EOS_Tick), true);
		EOS_Platform_Tick(EOSPlatformHandle);
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
	UE_LOG_ONLINE(Error, TEXT("User Cloud Interface Requested"));
	return nullptr;
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
	return nullptr;
}

IOnlineIdentityPtr FOnlineSubsystemEOS::GetIdentityInterface() const
{
	return UserManager;
}

IOnlineTitleFilePtr FOnlineSubsystemEOS::GetTitleFileInterface() const
{
	UE_LOG_ONLINE(Error, TEXT("Title File Interface Requested"));
	return nullptr;
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
