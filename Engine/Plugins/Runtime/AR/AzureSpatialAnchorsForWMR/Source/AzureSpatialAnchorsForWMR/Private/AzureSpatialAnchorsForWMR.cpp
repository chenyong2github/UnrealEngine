// Copyright Epic Games, Inc. All Rights Reserved.

#include "AzureSpatialAnchorsForWMR.h"

#include "MixedRealityInterop.h"
#include "Misc/Paths.h"
#include "IWindowsMixedRealityHMDPlugin.h"
#include "HoloLensARSystem.h"
#include "HoloLensARFunctionLibrary.h"
#include "Modules/ModuleManager.h"
#include "LatentActions.h"

// Some sanity checks for the return enums
static_assert((uint8)EAzureSpatialAnchorsResult::NotStarted == (uint8)AzureSpatialAnchorsInterop::ASAResult::NotStarted, "EAzureSpatialAnchorsResult interop enum match failed!");
static_assert((uint8)EAzureSpatialAnchorsResult::Started == (uint8)AzureSpatialAnchorsInterop::ASAResult::Started, "EAzureSpatialAnchorsResult interop enum match failed!");
static_assert((uint8)EAzureSpatialAnchorsResult::Canceled == (uint8)AzureSpatialAnchorsInterop::ASAResult::Canceled, "EAzureSpatialAnchorsResult interop enum match failed!");
static_assert((uint8)EAzureSpatialAnchorsResult::Success == (uint8)AzureSpatialAnchorsInterop::ASAResult::Success, "EAzureSpatialAnchorsResult interop enum match failed!");

void FAzureSpatialAnchorsForWMR::StartupModule()
{
	FAzureSpatialAnchorsBase::Startup();

	IModularFeatures::Get().RegisterModularFeature(IAzureSpatialAnchors::GetModularFeatureName(), this);

#if !PLATFORM_HOLOLENS
#  if PLATFORM_64BITS
	{

		FString EngineDir = FPaths::EngineDir();
		FString HoloLensLibraryDir = EngineDir / "Binaries/ThirdParty/Windows/x64";
		FPlatformProcess::PushDllDirectory(*HoloLensLibraryDir);

		FPlatformProcess::GetDllHandle(_TEXT("concrt140_app.dll"));
		FPlatformProcess::GetDllHandle(_TEXT("msvcp140_1_app.dll"));
		FPlatformProcess::GetDllHandle(_TEXT("msvcp140_2_app.dll"));
		FPlatformProcess::GetDllHandle(_TEXT("msvcp140_app.dll"));
		FPlatformProcess::GetDllHandle(_TEXT("vcamp140_app.dll"));
		FPlatformProcess::GetDllHandle(_TEXT("vccorlib140_app.dll"));
		FPlatformProcess::GetDllHandle(_TEXT("vcomp140_app.dll"));
		FPlatformProcess::GetDllHandle(_TEXT("vcruntime140_1_app.dll"));
		FPlatformProcess::GetDllHandle(_TEXT("vcruntime140_app.dll"));

		FPlatformProcess::GetDllHandle(_TEXT("Microsoft.Azure.SpatialAnchors.dll"));
		FPlatformProcess::PopDllDirectory(*HoloLensLibraryDir);
	}
#  endif
#endif

	CreateInterop();
}

void FAzureSpatialAnchorsForWMR::CreateInterop()
{
	WindowsMixedReality::MixedRealityInterop* MixedRealityInterop = IWindowsMixedRealityHMDPlugin::Get().GetMixedRealityInterop();
	check(MixedRealityInterop);

	auto AnchorLocatedLambda = std::bind(&FAzureSpatialAnchorsForWMR::AnchorLocatedCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	auto LocateAnchorsCompletedLambda = std::bind(&FAzureSpatialAnchorsForWMR::LocateAnchorsCompletedCallback, this, std::placeholders::_1, std::placeholders::_2);
	auto SessionUpdatedLambda = std::bind(&FAzureSpatialAnchorsForWMR::SessionUpdatedCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);

	AzureSpatialAnchorsInterop::Create(
		*MixedRealityInterop,
		&OnLog,
		AnchorLocatedLambda,
		LocateAnchorsCompletedLambda,
		SessionUpdatedLambda);
}

void FAzureSpatialAnchorsForWMR::ShutdownModule()
{
	AzureSpatialAnchorsInterop::Release();

	FAzureSpatialAnchorsBase::Shutdown();
}


bool FAzureSpatialAnchorsForWMR::CreateSession()
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return Interop.CreateSession();
}

void FAzureSpatialAnchorsForWMR::DestroySession()
{
	FAzureSpatialAnchorsBase::DestroySession();
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.DestroySession();

	// Get a fresh interop to ensure all it's state is gone.
	AzureSpatialAnchorsInterop::Release();
	CreateInterop();
}


//////////////////

void FAzureSpatialAnchorsForWMR::GetAccessTokenWithAccountKeyAsync(const FString& AccountKey, Callback_Result_String Callback)
{
	AzureSpatialAnchorsInterop::Callback_Result_String Callback2 = [Callback](AzureSpatialAnchorsInterop::ASAResult Result, const wchar_t* ErrorString, const wchar_t* AccessToken) mutable
	{
		Callback(static_cast<EAzureSpatialAnchorsResult>(Result), ErrorString, AccessToken);
	};
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.GetAccessTokenWithAccountKeyAsync(*AccountKey, Callback2);
}

void FAzureSpatialAnchorsForWMR::GetAccessTokenWithAuthenticationTokenAsync(const FString& AuthenticationToken, Callback_Result_String Callback)
{
	AzureSpatialAnchorsInterop::Callback_Result_String Callback2 = [Callback](AzureSpatialAnchorsInterop::ASAResult Result, const wchar_t* ErrorString, const wchar_t* AccessToken) mutable
	{
		Callback(static_cast<EAzureSpatialAnchorsResult>(Result), ErrorString, AccessToken);
	};
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.GetAccessTokenWithAuthenticationTokenAsync(*AuthenticationToken, Callback2);
}

EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::StartSession()
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return static_cast<EAzureSpatialAnchorsResult>(Interop.StartSession());
}

void FAzureSpatialAnchorsForWMR::StopSession()
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.StopSession();
}
EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::ResetSession()
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return static_cast<EAzureSpatialAnchorsResult>(Interop.ResetSession());
}
void FAzureSpatialAnchorsForWMR::DisposeSession()
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.DisposeSession();
}

void FAzureSpatialAnchorsForWMR::GetSessionStatusAsync(Callback_Result_SessionStatus Callback)
{
	AzureSpatialAnchorsInterop::Callback_Result_SessionStatus Callback2 = [Callback](AzureSpatialAnchorsInterop::ASAResult Result, const wchar_t* ErrorString, AzureSpatialAnchorsInterop::SessionStatus InSessionStatus) mutable
	{ 
		FAzureSpatialAnchorsSessionStatus SessionStatus;
		SessionStatus.ReadyForCreateProgress = InSessionStatus.ReadyForCreateProgress;
		SessionStatus.RecommendedForCreateProgress = InSessionStatus.RecommendedForCreateProgress;
		SessionStatus.SessionCreateHash = InSessionStatus.SessionCreateHash;
		SessionStatus.SessionLocateHash = InSessionStatus.SessionLocateHash;
		SessionStatus.feedback = static_cast<EAzureSpatialAnchorsSessionUserFeedback>(InSessionStatus.UserFeedback);
		Callback(static_cast<EAzureSpatialAnchorsResult>(Result), ErrorString, SessionStatus); 
	};
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.GetSessionStatusAsync(Callback2);
}
EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::ConstructAnchor(UARPin* InARPin, CloudAnchorID& OutCloudAnchorID)
{
	UWMRARPin* WMRPin = Cast<UWMRARPin>(InARPin);
	if (!WMRPin)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("ConstrucAnchor called with null ARPin.  Ignoring."));
		return EAzureSpatialAnchorsResult::FailNoARPin;
	}
	const FString LocalAnchorId = WMRPin->GetAnchorId();
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return static_cast<EAzureSpatialAnchorsResult>(Interop.ConstructAnchor(*LocalAnchorId, OutCloudAnchorID));
}

void FAzureSpatialAnchorsForWMR::CreateAnchorAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback)
{
	AzureSpatialAnchorsInterop::Callback_Result Callback2 = [Callback](AzureSpatialAnchorsInterop::ASAResult Result, const wchar_t* ErrorString) mutable { Callback(static_cast<EAzureSpatialAnchorsResult>(Result), ErrorString); UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("c_CreateAnchorAsync callback2")); };
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.CreateAnchorAsync(static_cast<AzureSpatialAnchorsInterop::CloudAnchorID>(InCloudAnchorID), Callback2);
}
void FAzureSpatialAnchorsForWMR::DeleteAnchorAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback)
{
	AzureSpatialAnchorsInterop::Callback_Result Callback2 = [Callback](AzureSpatialAnchorsInterop::ASAResult Result, const wchar_t* ErrorString) mutable { Callback(static_cast<EAzureSpatialAnchorsResult>(Result), ErrorString); };
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.DeleteAnchorAsync(static_cast<AzureSpatialAnchorsInterop::CloudAnchorID>(InCloudAnchorID), Callback2);
}

EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::CreateWatcher(const FAzureSpatialAnchorsLocateCriteria& InLocateCriteria, float InWorldToMetersScale, WatcherID& OutWatcherID, FString& OutErrorString)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	AzureSpatialAnchorsInterop::LocateCriteria Criteria;
	Criteria.bBypassCache = InLocateCriteria.bBypassCache;
	TArray<const wchar_t*> Identifiers;
	Identifiers.Reserve(InLocateCriteria.Identifiers.Num());
	for (auto& Identifier : InLocateCriteria.Identifiers)
	{
		Identifiers.Add(*Identifier);
	}
	Criteria.NumIdentifiers = Identifiers.Num();
	Criteria.Identifiers = Identifiers.GetData();
	Criteria.NearCloudAnchorID = InLocateCriteria.NearAnchor ? InLocateCriteria.NearAnchor->CloudAnchorID : IAzureSpatialAnchors::CloudAnchorID_Invalid;
	Criteria.NearCloudAnchorDistance = InLocateCriteria.NearAnchorDistance * InWorldToMetersScale;
	Criteria.NearCloudAnchorMaxResultCount = InLocateCriteria.NearAnchorMaxResultCount;
	Criteria.SearchNearDevice = InLocateCriteria.bSearchNearDevice;
	Criteria.NearDeviceDistance = InLocateCriteria.NearDeviceDistance;
	Criteria.NearDeviceMaxResultCount = InLocateCriteria.NearDeviceMaxResultCount;
	Criteria.AzureSpatialAnchorDataCategory = static_cast<int>(InLocateCriteria.RequestedCategories);
	Criteria.AzureSptialAnchorsLocateStrategy = static_cast<int>(InLocateCriteria.Strategy);
	AzureSpatialAnchorsInterop::StringOutParam ErrorString;
	EAzureSpatialAnchorsResult Result = static_cast<EAzureSpatialAnchorsResult>(Interop.CreateWatcher(Criteria, OutWatcherID, ErrorString));
	OutErrorString = ErrorString.String;
	return Result;
}
EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::GetActiveWatchers(TArray<WatcherID>& OutWatcherIDs)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	AzureSpatialAnchorsInterop::IntArrayOutParam IDs;
	EAzureSpatialAnchorsResult Result = static_cast<EAzureSpatialAnchorsResult>(Interop.GetActiveWatchers(IDs));
	OutWatcherIDs.Reset(IDs.ArraySize);
	for (uint32_t i = 0; i < IDs.ArraySize; ++i)
	{
		OutWatcherIDs.Add(IDs.Array[i]);
	}
	return Result;
}

void FAzureSpatialAnchorsForWMR::GetAnchorPropertiesAsync(const FString& InCloudAnchorIdentifier, Callback_Result_CloudAnchorID Callback)
{
	AzureSpatialAnchorsInterop::Callback_Result_CloudAnchorID Callback2 = [Callback](AzureSpatialAnchorsInterop::ASAResult Result, const wchar_t* ErrorString, AzureSpatialAnchorsInterop::CloudAnchorID InCloudAnchorID) mutable { Callback(static_cast<EAzureSpatialAnchorsResult>(Result), ErrorString, static_cast<CloudAnchorID>(InCloudAnchorID)); };
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.GetAnchorPropertiesAsync(*InCloudAnchorIdentifier, Callback2);
}

void FAzureSpatialAnchorsForWMR::RefreshAnchorPropertiesAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback)
{
	AzureSpatialAnchorsInterop::Callback_Result Callback2 = [Callback](AzureSpatialAnchorsInterop::ASAResult Result, const wchar_t* ErrorString) mutable { Callback(static_cast<EAzureSpatialAnchorsResult>(Result), ErrorString); };
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.RefreshAnchorPropertiesAsync(static_cast<AzureSpatialAnchorsInterop::CloudAnchorID>(InCloudAnchorID), Callback2);
}
void FAzureSpatialAnchorsForWMR::UpdateAnchorPropertiesAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback)
{
	AzureSpatialAnchorsInterop::Callback_Result Callback2 = [Callback](AzureSpatialAnchorsInterop::ASAResult Result, const wchar_t* ErrorString) mutable { Callback(static_cast<EAzureSpatialAnchorsResult>(Result), ErrorString); };
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.UpdateAnchorPropertiesAsync(static_cast<AzureSpatialAnchorsInterop::CloudAnchorID>(InCloudAnchorID), Callback2);
}

EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::GetConfiguration(FAzureSpatialAnchorsSessionConfiguration& OutConfig)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	AzureSpatialAnchorsInterop::SessionConfig Config;
	EAzureSpatialAnchorsResult Result = static_cast<EAzureSpatialAnchorsResult>(Interop.GetConfiguration(Config));
	OutConfig.AccessToken = Config.AccessToken;
	OutConfig.AccountDomain = Config.AccountDomain;
	OutConfig.AccountId = Config.AccountId;
	OutConfig.AccountKey = Config.AccountKey;
	OutConfig.AuthenticationToken = Config.AuthenticationToken;
	return Result;
}
EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::SetConfiguration(const FAzureSpatialAnchorsSessionConfiguration& InConfig)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	AzureSpatialAnchorsInterop::SessionConfig Config;
	Config.AccessToken = *InConfig.AccessToken;
	Config.AccountDomain = *InConfig.AccountDomain;
	Config.AccountId = *InConfig.AccountId;
	Config.AccountKey = *InConfig.AccountKey;
	Config.AuthenticationToken = *InConfig.AuthenticationToken;
	return static_cast<EAzureSpatialAnchorsResult>(Interop.SetConfiguration(Config));
}

EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::SetLocationProvider(const FCoarseLocalizationSettings& InConfig)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	AzureSpatialAnchorsInterop::LocationProviderConfig Config;
	Config.bCoarseLocalizationEnabled = InConfig.bEnable;
	Config.bEnableGPS = InConfig.bEnableGPS;
	Config.bEnableWifi = InConfig.bEnableWifi;
	Config.NumBLEBeaconUUIDs = InConfig.BLEBeaconUUIDs.Num();
	TUniquePtr<const wchar_t*[]> UUIDs = Config.NumBLEBeaconUUIDs > 0 ? MakeUnique<const wchar_t*[]>(Config.NumBLEBeaconUUIDs) : nullptr;
	for (int32_t i = 0; i < Config.NumBLEBeaconUUIDs; i++)
	{
		UUIDs.Get()[i] = *(InConfig.BLEBeaconUUIDs[i]);
	}
	return static_cast<EAzureSpatialAnchorsResult>(Interop.SetLocationProvider(Config));
}

EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::GetLogLevel(EAzureSpatialAnchorsLogVerbosity& OutLogVerbosity)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	int32_t Out = 0;
	EAzureSpatialAnchorsResult Result = static_cast<EAzureSpatialAnchorsResult>(Interop.GetLogLevel(Out));
	OutLogVerbosity = static_cast<EAzureSpatialAnchorsLogVerbosity>(Out);
	return Result;
}
EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::SetLogLevel(EAzureSpatialAnchorsLogVerbosity InLogVerbosity)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return static_cast<EAzureSpatialAnchorsResult>(Interop.SetLogLevel(static_cast<int32_t>(InLogVerbosity)));
}
// EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::GetSession();
// EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::SetSession();
EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::GetSessionId(FString& OutSessionID)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	std::wstring SessionID;
	EAzureSpatialAnchorsResult Result = static_cast<EAzureSpatialAnchorsResult>(Interop.GetSessionId(SessionID));
	OutSessionID = SessionID.c_str();
	return Result;
}

EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::StopWatcher(WatcherID WatcherID)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return static_cast<EAzureSpatialAnchorsResult>(Interop.StopWatcher(WatcherID));
}


EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::GetCloudSpatialAnchorIdentifier(CloudAnchorID InCloudAnchorID, FString& OutCloudAnchorIdentifier)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	AzureSpatialAnchorsInterop::StringOutParam Identifier;
	EAzureSpatialAnchorsResult Result = static_cast<EAzureSpatialAnchorsResult>(Interop.GetCloudSpatialAnchorIdentifier(InCloudAnchorID, Identifier));
	OutCloudAnchorIdentifier = Identifier.String;
	return Result;
}
EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::SetCloudAnchorExpiration(CloudAnchorID InCloudAnchorID, float InLifetimeInSeconds)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return static_cast<EAzureSpatialAnchorsResult>(Interop.SetCloudAnchorExpiration(InCloudAnchorID, InLifetimeInSeconds));
}
EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::GetCloudAnchorExpiration(CloudAnchorID InCloudAnchorID, float& OutLifetimeInSeconds)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return static_cast<EAzureSpatialAnchorsResult>(Interop.GetCloudAnchorExpiration(InCloudAnchorID, OutLifetimeInSeconds));
}
EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::SetCloudAnchorAppProperties(CloudAnchorID InCloudAnchorID, const TMap<FString, FString>& InAppProperties)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	int32_t NumAppProperties = InAppProperties.Num();
	TUniquePtr<const wchar_t* []> AppProperties_KeyValueInterleaved = NumAppProperties > 0 ? MakeUnique<const wchar_t* []>(NumAppProperties * 2) : nullptr;

	int32_t Index = 0;
	for (const auto& Pair : InAppProperties)
	{
		AppProperties_KeyValueInterleaved[Index] = *Pair.Key;
		AppProperties_KeyValueInterleaved[Index+1] = *Pair.Value;
		Index += 2;
	}
	return static_cast<EAzureSpatialAnchorsResult>(Interop.SetCloudAnchorAppProperties(InCloudAnchorID, NumAppProperties, AppProperties_KeyValueInterleaved.Get()));
}
EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::GetCloudAnchorAppProperties(CloudAnchorID InCloudAnchorID, TMap<FString, FString>& OutAppProperties)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	AzureSpatialAnchorsInterop::StringArrayOutParam AppProperties_KeyValueInterleaved;
	EAzureSpatialAnchorsResult Result = static_cast<EAzureSpatialAnchorsResult>(Interop.GetCloudAnchorAppProperties(InCloudAnchorID, AppProperties_KeyValueInterleaved));
	OutAppProperties.Reset();
	for (uint32_t i = 0; i < AppProperties_KeyValueInterleaved.ArraySize; i+=2)
	{
		OutAppProperties.Add(AppProperties_KeyValueInterleaved.Array[i], AppProperties_KeyValueInterleaved.Array[i+1]);
	}
	return Result;
}
EAzureSpatialAnchorsResult FAzureSpatialAnchorsForWMR::SetDiagnosticsConfig(FAzureSpatialAnchorsDiagnosticsConfig& InConfig)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	AzureSpatialAnchorsInterop::DiagnosticsConfig Config;
	Config.bImagesEnabled = InConfig.bImagesEnabled;
	Config.LogDirectory = *InConfig.LogDirectory;
	Config.LogLevel = static_cast<int32_t>(InConfig.LogLevel);
	Config.MaxDiskSizeInMB = InConfig.MaxDiskSizeInMB;
	return static_cast<EAzureSpatialAnchorsResult>(Interop.SetDiagnosticsConfig(Config));
}
void FAzureSpatialAnchorsForWMR::CreateDiagnosticsManifestAsync(const FString& Description, Callback_Result_String Callback)
{
	AzureSpatialAnchorsInterop::Callback_Result_String Callback2 = [Callback](AzureSpatialAnchorsInterop::ASAResult Result, const wchar_t* ErrorString, const wchar_t* ReturnString) mutable
	{
		Callback(static_cast<EAzureSpatialAnchorsResult>(Result), ErrorString, ReturnString);
	};
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.CreateDiagnosticsManifestAsync(*Description, Callback2);
}
void FAzureSpatialAnchorsForWMR::SubmitDiagnosticsManifestAsync(const FString& ManifestPath, Callback_Result Callback)
{
	AzureSpatialAnchorsInterop::Callback_Result Callback2 = [Callback](AzureSpatialAnchorsInterop::ASAResult Result, const wchar_t* ErrorString) mutable
	{
		Callback(static_cast<EAzureSpatialAnchorsResult>(Result), ErrorString);
	};
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.SubmitDiagnosticsManifestAsync(*ManifestPath, Callback2);
}


void FAzureSpatialAnchorsForWMR::CreateNamedARPinAroundAnchor(const FString& InLocalAnchorId, UARPin*& OutARPin)
{
	OutARPin = UHoloLensARFunctionLibrary::CreateNamedARPinAroundAnchor(FName(InLocalAnchorId), InLocalAnchorId);
}

bool FAzureSpatialAnchorsForWMR::CreateARPinAroundAzureCloudSpatialAnchor(const FString& InPinId, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, UARPin*& OutARPin)
{
	if (InAzureCloudSpatialAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("WrapARPinAroundAzureCloudSpatialAnchor called with null InAzureCloudSpatialAnchor.  Ignoring."));
		return false;
	}

	if (InAzureCloudSpatialAnchor->ARPin != nullptr)
	{
		OutARPin = InAzureCloudSpatialAnchor->ARPin;
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("WrapARPinAroundAzureCloudSpatialAnchor called with an InAzureCloudSpatialAnchor that already has an ARPin.  Ignoring."));
		return false;
	}

	if (InPinId.IsEmpty())
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("WrapARPinAroundAzureCloudSpatialAnchor called with an empty PinId.  Ignoring."));
		return false;
	}

	FName PinIdName(InPinId);

	if (PinIdName == NAME_None)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("WrapARPinAroundAzureCloudSpatialAnchor called with illegal PinId of 'None'.  This is dangerous because empty strings cast to Name 'None'."));
	}

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	if (Interop.CreateARPinAroundAzureCloudSpatialAnchor(*InPinId, InAzureCloudSpatialAnchor->CloudAnchorID) == false)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("WrapARPinAroundAzureCloudSpatialAnchor interop failed to store the local anchor from the cloud anchor.  Ignoring."));
		return false;
	}

	// create a pin around the local anchor
	OutARPin = UHoloLensARFunctionLibrary::CreateNamedARPinAroundAnchor(PinIdName, InPinId);
	return (OutARPin != nullptr);
	{
		InAzureCloudSpatialAnchor->ARPin = OutARPin;
		return true;
	}
}

IMPLEMENT_MODULE(FAzureSpatialAnchorsForWMR, AzureSpatialAnchorsForWMR)

