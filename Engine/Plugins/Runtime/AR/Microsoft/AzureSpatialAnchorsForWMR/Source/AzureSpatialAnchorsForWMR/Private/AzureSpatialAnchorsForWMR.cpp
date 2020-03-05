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
static_assert((uint8)EAzureSpatialAnchorsResult::NotStarted == (uint8)AzureSpatialAnchorsInterop::AsyncResult::NotStarted, "EAzureSpatialAnchorsResult interop enum match failed!");
static_assert((uint8)EAzureSpatialAnchorsResult::Started == (uint8)AzureSpatialAnchorsInterop::AsyncResult::Started, "EAzureSpatialAnchorsResult interop enum match failed!");
static_assert((uint8)EAzureSpatialAnchorsResult::Canceled == (uint8)AzureSpatialAnchorsInterop::AsyncResult::Canceled, "EAzureSpatialAnchorsResult interop enum match failed!");
static_assert((uint8)EAzureSpatialAnchorsResult::Success == (uint8)AzureSpatialAnchorsInterop::AsyncResult::Success, "EAzureSpatialAnchorsResult interop enum match failed!");

namespace ASAForWMR_Time
{
	// make an FTimeSpan object that represents the "epoch" for time_t (from a _stat struct)
	const FDateTime HoloLensEpoch(1970, 1, 1);

	FORCEINLINE int32 UEDayOfWeekToWindowsSystemTimeDayOfWeek(const EDayOfWeek InDayOfWeek)
	{
		switch (InDayOfWeek)
		{
		case EDayOfWeek::Monday:
			return 1;
		case EDayOfWeek::Tuesday:
			return 2;
		case EDayOfWeek::Wednesday:
			return 3;
		case EDayOfWeek::Thursday:
			return 4;
		case EDayOfWeek::Friday:
			return 5;
		case EDayOfWeek::Saturday:
			return 6;
		case EDayOfWeek::Sunday:
			return 0;
		default:
			break;
		}

		return 0;
	}

	FORCEINLINE FDateTime WindowsFileTimeToUEDateTime(const FILETIME& InFileTime)
	{
		// This roundabout conversion clamps the precision of the returned time value to match that of time_t (1 second precision)
		// This avoids issues when sending files over the network via cook-on-the-fly
		SYSTEMTIME SysTime;
		if (FileTimeToSystemTime(&InFileTime, &SysTime))
		{
			return FDateTime(SysTime.wYear, SysTime.wMonth, SysTime.wDay, SysTime.wHour, SysTime.wMinute, SysTime.wSecond);
		}

		// Failed to convert
		return FDateTime::MinValue();
	}

	FORCEINLINE FILETIME UEDateTimeToWindowsFileTime(const FDateTime& InDateTime)
	{
		// This roundabout conversion clamps the precision of the returned time value to match that of time_t (1 second precision)
		// This avoids issues when sending files over the network via cook-on-the-fly
		SYSTEMTIME SysTime;
		SysTime.wYear = InDateTime.GetYear();
		SysTime.wMonth = InDateTime.GetMonth();
		SysTime.wDay = InDateTime.GetDay();
		SysTime.wDayOfWeek = UEDayOfWeekToWindowsSystemTimeDayOfWeek(InDateTime.GetDayOfWeek());
		SysTime.wHour = InDateTime.GetHour();
		SysTime.wMinute = InDateTime.GetMinute();
		SysTime.wSecond = InDateTime.GetSecond();
		SysTime.wMilliseconds = 0;

		FILETIME FileTime;
		SystemTimeToFileTime(&SysTime, &FileTime);

		return FileTime;
	}
}

void FAzureSpatialAnchorsForWMR::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(IAzureSpatialAnchors::GetModularFeatureName(), this);

#if !PLATFORM_HOLOLENS
#  if PLATFORM_64BITS
	{

		FString EngineDir = FPaths::EngineDir();
		FString HoloLensLibraryDir = EngineDir / "Binaries/ThirdParty/Windows/x64";
		FPlatformProcess::PushDllDirectory(*HoloLensLibraryDir);
		FPlatformProcess::GetDllHandle(_TEXT("MSVCP140_APP.dll"));
		FPlatformProcess::GetDllHandle(_TEXT("VCRUNTIME140_1_APP.dll"));
		FPlatformProcess::GetDllHandle(_TEXT("VCRUNTIME140_APP.dll"));
		FPlatformProcess::GetDllHandle(_TEXT("Microsoft.Azure.SpatialAnchors.dll"));
		FPlatformProcess::PopDllDirectory(*HoloLensLibraryDir);
	}
#  endif
#else // !PLATFORM_HOLOLENS			
	{
		// Load these explicitly.  The microsoft code doesn't find them automatically because of their paths.
		FString HoloLensLibraryDir = "Engine/Binaries/ThirdParty/Hololens/ARM64";
		FPlatformProcess::PushDllDirectory(*HoloLensLibraryDir);
		void* MASAHandle = FPlatformProcess::GetDllHandle(TEXT("Microsoft.Azure.SpatialAnchors.dll"));
		if (MASAHandle == nullptr) UE_LOG(LogAzureSpatialAnchors, Log, TEXT("Microsoft.Azure.SpatialAnchors.dll load failed! AzureSpatialAnchors will not work.  Exceptions may result."));
		FPlatformProcess::PopDllDirectory(*HoloLensLibraryDir);
	}
#endif

	WindowsMixedReality::MixedRealityInterop* MixedRealityInterop = IWindowsMixedRealityHMDPlugin::Get().GetMixedRealityInterop();
	check(MixedRealityInterop);
	AzureSpatialAnchorsInterop::Create(*MixedRealityInterop, &OnLog);
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
}

void FAzureSpatialAnchorsForWMR::ShutdownModule()
{
	AzureSpatialAnchorsInterop::Release();
}


bool FAzureSpatialAnchorsForWMR::CreateSession()
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return Interop.CreateSession();
}

bool FAzureSpatialAnchorsForWMR::ConfigSession(const FString& AccountId, const FString& AccountKey, const FCoarseLocalizationSettings& CoarseLocalizationSettings, EAzureSpatialAnchorsLogVerbosity LogVerbosity)
{
	AzureSpatialAnchorsInterop::ConfigData Data;
	Data.accountId = *AccountId;
	Data.accountKey = *AccountKey;
	Data.bCoarseLocalizationEnabled = CoarseLocalizationSettings.bEnable;
	Data.bEnableGPS = CoarseLocalizationSettings.bEnableGPS;
	Data.bEnableWifi = CoarseLocalizationSettings.bEnableWifi;
	for (const auto& UUID : CoarseLocalizationSettings.BLEBeaconUUIDs)
	{
		// skip empty identifiers, which the microsoft api throws exceptions about.
		if (!UUID.IsEmpty())
		{
			Data.BLEBeaconUUIDs.push_back(*UUID);
		}
		else
		{
			UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("ConfigSession called with an empty UUID in its BLEBeaconUUIDs.  Ignoring the empty UUID."));
		}
	}
	Data.logVerbosity = static_cast<int>(LogVerbosity);

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return Interop.ConfigSession(Data);
}

bool FAzureSpatialAnchorsForWMR::StartSession()
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return Interop.StartSession();
}

void FAzureSpatialAnchorsForWMR::StopSession()
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.StopSession();
}

void FAzureSpatialAnchorsForWMR::DestroySession()
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	Interop.DestroySession();
	AzureSpatialAnchorsInterop::Release();
}

bool FAzureSpatialAnchorsForWMR::GetCloudAnchor(UARPin*& InARPin, UAzureCloudSpatialAnchor*& OutCloudAnchor)
{
	const UWMRARPin* WMRPin = Cast<UWMRARPin>(InARPin);
	if (!WMRPin)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("CreateCloudAnchor called with null ARPin.  Ignoring."));
		OutCloudAnchor = nullptr;
		return false;
	}

	const FString& AnchorId = WMRPin->GetAnchorId();
	for (UAzureCloudSpatialAnchor* Anchor : CloudAnchors)
	{
		if (Anchor->ARPin == InARPin)
		{
			OutCloudAnchor = Anchor;
			return true;
		}
	}

	OutCloudAnchor = nullptr;
	return false;
}

void FAzureSpatialAnchorsForWMR::GetCloudAnchors(TArray<UAzureCloudSpatialAnchor*>& OutCloudAnchors)
{
	OutCloudAnchors = CloudAnchors;
}

void FAzureSpatialAnchorsForWMR::GetUnpinnedCloudAnchors(TArray<UAzureCloudSpatialAnchor*>& OutCloudAnchors)
{
	OutCloudAnchors.Empty();
	for (UAzureCloudSpatialAnchor* Anchor : CloudAnchors)
	{
		if (Anchor->ARPin == nullptr)
		{
			OutCloudAnchors.Add(Anchor);
		}
	}
}

FString FAzureSpatialAnchorsForWMR::GetCloudSpatialAnchorIdentifier(UAzureCloudSpatialAnchor::AzureCloudAnchorID CloudAnchorID)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return FString(Interop.GetCloudSpatialAnchorIdentifier(CloudAnchorID));
}

bool FAzureSpatialAnchorsForWMR::CreateCloudAnchor(UARPin*& InARPin, UAzureCloudSpatialAnchor*& OutCloudAnchor)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	UWMRARPin* WMRPin = Cast<UWMRARPin>(InARPin);
	if (!WMRPin)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("CreateCloudAnchor called with null ARPin.  Ignoring."));
		return false;
	}
	const FString& AnchorId = WMRPin->GetAnchorId();
	AzureSpatialAnchorsInterop::CloudAnchorID CloudAnchorID = -1;
	bool bSuccess =  Interop.CreateCloudAnchor(*AnchorId, CloudAnchorID);

	if (bSuccess)
	{
		check(CloudAnchorID != -1);
		UAzureCloudSpatialAnchor* NewCloudAnchor = NewObject<UAzureCloudSpatialAnchor>();
		NewCloudAnchor->ARPin = InARPin;
		NewCloudAnchor->CloudAnchorID = CloudAnchorID;
		CloudAnchors.Add(NewCloudAnchor);
		OutCloudAnchor = NewCloudAnchor;
	}

	return bSuccess;
}

bool FAzureSpatialAnchorsForWMR::SetCloudAnchorExpiration(const UAzureCloudSpatialAnchor* const & InCloudAnchor, FDateTime InExpirationTime)
{
	if (InCloudAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("SetCloudAnchorExpiration called with null CloudAnchor.  Ignoring."));
		return false;
	}
 
	const FILETIME ExpirationTime = ASAForWMR_Time::UEDateTimeToWindowsFileTime(InExpirationTime);

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return Interop.SetCloudAnchorExpiration(InCloudAnchor->CloudAnchorID, ExpirationTime);
}

FDateTime FAzureSpatialAnchorsForWMR::GetCloudAnchorExpiration(const class UAzureCloudSpatialAnchor* const& InCloudAnchor)
{
	if (InCloudAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("GetCloudAnchorExpiration called with null CloudAnchor.  Returing an empty FDateTime."));
		return FDateTime();
	}

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	FILETIME ExpirationTime;
	bool bSuccess = Interop.GetCloudAnchorExpiration(InCloudAnchor->CloudAnchorID, ExpirationTime);

	if (bSuccess)
	{
		return ASAForWMR_Time::WindowsFileTimeToUEDateTime(ExpirationTime);
	}
	else
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("SetCloudAnchorExpiration could not get the Expiration time for CloudAnchorID %i, perhaps the anchorid is invalid?  Returing an empty FDateTime."), InCloudAnchor->CloudAnchorID);
		return FDateTime();
	}
}

bool FAzureSpatialAnchorsForWMR::SetCloudAnchorAppProperties(const class UAzureCloudSpatialAnchor* const& InCloudAnchor, const TMap<FString, FString>& InAppProperties)
{
	if (InCloudAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("SetCloudAnchorAppProperties called with null CloudAnchor.  Ignoring."));
		return false;
	}

	std::vector<std::pair<std::wstring, std::wstring>> AppProperties;
	AppProperties.reserve(InAppProperties.Num());
	for (auto pair : InAppProperties)
	{
		AppProperties.push_back(std::make_pair<std::wstring, std::wstring>(*pair.Key, *pair.Value));
	}

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return Interop.SetCloudAnchorAppProperties(InCloudAnchor->CloudAnchorID, AppProperties);
}

TMap<FString, FString> FAzureSpatialAnchorsForWMR::GetCloudAnchorAppProperties(const class UAzureCloudSpatialAnchor* const& InCloudAnchor)
{
	if (InCloudAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("GetCloudAnchorExpiration called with null CloudAnchor.  Returning an empty map."));
		return TMap<FString, FString>();
	}

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	std::vector<std::pair<std::wstring, std::wstring>> AppProperties;
	bool bSuccess = Interop.GetCloudAnchorAppProperties(InCloudAnchor->CloudAnchorID, AppProperties);

	if (bSuccess)
	{
		TMap<FString, FString> Return;
		for (const auto itr : AppProperties)
		{
			Return.Add(itr.first.c_str(), itr.second.c_str());
		}
		return Return;
	}
	else
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("GetCloudAnchorAppProperties could not get the AppPropeties for CloudAnchorID %i, perhaps the anchorid is invalid?  Returning an empty map."), InCloudAnchor->CloudAnchorID);
		return TMap<FString, FString>();
	}
}

bool FAzureSpatialAnchorsForWMR::SaveCloudAnchorAsync_Start(FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (InCloudAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("SaveCloudAnchor called with null CloudAnchor.  Ignoring."));
		return false;
	}

	AzureSpatialAnchorsInterop::SaveAsyncDataPtr Data = std::make_shared<AzureSpatialAnchorsInterop::SaveAsyncData>();
	Data->CloudAnchorID = InCloudAnchor->CloudAnchorID;

	SaveAsyncDataMap.Add(LatentAction, Data);

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	bool bStarted = Interop.SaveCloudAnchor(Data);
	
	OutResult = (EAzureSpatialAnchorsResult)Data->Result;
	OutErrorString = Data->OutError.c_str();

	return bStarted;
}
bool FAzureSpatialAnchorsForWMR::SaveCloudAnchorAsync_Update(FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	AzureSpatialAnchorsInterop::SaveAsyncDataPtr& Data = SaveAsyncDataMap.FindChecked(LatentAction);
	if (Data->Completed)
	{
		OutResult = (EAzureSpatialAnchorsResult)Data->Result;
		OutErrorString = Data->OutError.c_str();
		SaveAsyncDataMap.Remove(LatentAction);

		return true;
	}
	else
	{
		return false;
	}
}
void FAzureSpatialAnchorsForWMR::SaveCloudAnchorAsync_Orphan(FPendingLatentAction* LatentAction)
{
	SaveAsyncDataMap.Remove(LatentAction);
}


bool FAzureSpatialAnchorsForWMR::DeleteCloudAnchorAsync_Start(FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (InCloudAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("DeleteCloudAnchor called with null CloudAnchor.  Ignoring."));
		return false;
	}

	AzureSpatialAnchorsInterop::DeleteAsyncDataPtr Data = std::make_shared<AzureSpatialAnchorsInterop::DeleteAsyncData>();
	Data->CloudAnchorID = InCloudAnchor->CloudAnchorID;

	DeleteAsyncDataMap.Add(LatentAction, Data);

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	bool bStarted = Interop.DeleteCloudAnchor(Data);

	OutResult = (EAzureSpatialAnchorsResult)Data->Result;
	OutErrorString = Data->OutError.c_str();

	return bStarted;
}
bool FAzureSpatialAnchorsForWMR::DeleteCloudAnchorAsync_Update(FPendingLatentAction* LatentAction, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	AzureSpatialAnchorsInterop::DeleteAsyncDataPtr& Data = DeleteAsyncDataMap.FindChecked(LatentAction);
	if (Data->Completed)
	{
		OutResult = (EAzureSpatialAnchorsResult)Data->Result;
		OutErrorString = Data->OutError.c_str();
		DeleteAsyncDataMap.Remove(LatentAction);

		return true;
	}
	else
	{
		return false;
	}
}
void FAzureSpatialAnchorsForWMR::DeleteCloudAnchorAsync_Orphan(FPendingLatentAction* LatentAction)
{
	DeleteAsyncDataMap.Remove(LatentAction);
}


bool FAzureSpatialAnchorsForWMR::LoadCloudAnchorByIDAsync_Start(FPendingLatentAction* LatentAction, const FString& InCloudAnchorIdentifier, const FString& InLocalAnchorId, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (InCloudAnchorIdentifier.IsEmpty())
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("SaveCloudAnchor called with empty InCloudAnchorId.  Ignoring."));
		return false;
	}

	AzureSpatialAnchorsInterop::LoadByIDAsyncDataPtr Data = std::make_shared<AzureSpatialAnchorsInterop::LoadByIDAsyncData>();
	Data->CloudAnchorIdentifier = *InCloudAnchorIdentifier;
	Data->LocalAnchorId = *InLocalAnchorId;
	LoadByIDAsyncDataMap.Add(LatentAction, Data);

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	bool bStarted = Interop.LoadCloudAnchorByID(Data);

	OutResult = (EAzureSpatialAnchorsResult)Data->Result;
	OutErrorString = Data->OutError.c_str();

	return bStarted;
}
bool FAzureSpatialAnchorsForWMR::LoadCloudAnchorByIDAsync_Update(FPendingLatentAction* LatentAction, UARPin*& OutARPin, UAzureCloudSpatialAnchor*& OutCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	AzureSpatialAnchorsInterop::LoadByIDAsyncDataPtr& Data = LoadByIDAsyncDataMap.FindChecked(LatentAction);
	if (Data->Completed)
	{
		OutResult = (EAzureSpatialAnchorsResult)Data->Result;
		OutErrorString = Data->OutError.c_str();

		if (OutResult == EAzureSpatialAnchorsResult::Success)
		{
			// Create an ARPin and CloudAnchor so we can return them.
			OutARPin = UHoloLensARFunctionLibrary::CreateNamedARPinAroundAnchor(FName(Data->LocalAnchorId.c_str()), Data->LocalAnchorId.c_str());
			if (OutARPin == nullptr)
			{
				OutResult = EAzureSpatialAnchorsResult::FailSeeErrorString;
				OutErrorString = TEXT("LoadCloudAnchorByIDAsync_Update failed to create ARPin after loading anchor.  Bug.");
				OutCloudAnchor = nullptr;
				check(false);
				return true;
			}

			UAzureCloudSpatialAnchor* NewCloudAnchor = NewObject<UAzureCloudSpatialAnchor>();
			NewCloudAnchor->ARPin = OutARPin;
			NewCloudAnchor->CloudAnchorID = Data->CloudAnchorID;

			CloudAnchors.Add(NewCloudAnchor);
			OutCloudAnchor = NewCloudAnchor;
			return true;
		}

		LoadByIDAsyncDataMap.Remove(LatentAction);

		return true;
	}
	else
	{
		return false;
	}
}
void FAzureSpatialAnchorsForWMR::LoadCloudAnchorByIDAsync_Orphan(FPendingLatentAction* LatentAction)
{
	LoadByIDAsyncDataMap.Remove(LatentAction);
}


bool FAzureSpatialAnchorsForWMR::UpdateCloudAnchorPropertiesAsync_Start(FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (InAzureCloudSpatialAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("UpdateCloudAnchorProperties called with null CloudAnchor.  Ignoring."));
		return false;
	}

	AzureSpatialAnchorsInterop::UpdateCloudAnchorPropertiesAsyncDataPtr Data = std::make_shared<AzureSpatialAnchorsInterop::UpdateCloudAnchorPropertiesAsyncData>();
	Data->CloudAnchorID = InAzureCloudSpatialAnchor->CloudAnchorID;

	UpdateCloudAnchorPropertiesAsyncDataMap.Add(LatentAction, Data);

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	bool bStarted = Interop.UpdateCloudAnchorProperties(Data);

	OutResult = (EAzureSpatialAnchorsResult)Data->Result;
	OutErrorString = Data->OutError.c_str();

	return bStarted;
}
bool FAzureSpatialAnchorsForWMR::UpdateCloudAnchorPropertiesAsync_Update(FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	AzureSpatialAnchorsInterop::UpdateCloudAnchorPropertiesAsyncDataPtr& Data = UpdateCloudAnchorPropertiesAsyncDataMap.FindChecked(LatentAction);
	if (Data->Completed)
	{
		OutResult = (EAzureSpatialAnchorsResult)Data->Result;
		OutErrorString = Data->OutError.c_str();
		UpdateCloudAnchorPropertiesAsyncDataMap.Remove(LatentAction);

		return true;
	}
	else
	{
		return false;
	}
}
void FAzureSpatialAnchorsForWMR::UpdateCloudAnchorPropertiesAsync_Orphan(FPendingLatentAction* LatentAction)
{
	UpdateCloudAnchorPropertiesAsyncDataMap.Remove(LatentAction);
}


bool FAzureSpatialAnchorsForWMR::RefreshCloudAnchorPropertiesAsync_Start(FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (InAzureCloudSpatialAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("RefreshCloudAnchorProperties called with null CloudAnchor.  Ignoring."));
		return false;
	}

	AzureSpatialAnchorsInterop::RefreshCloudAnchorPropertiesAsyncDataPtr Data = std::make_shared<AzureSpatialAnchorsInterop::RefreshCloudAnchorPropertiesAsyncData>();
	Data->CloudAnchorID = InAzureCloudSpatialAnchor->CloudAnchorID;

	RefreshCloudAnchorPropertiesAsyncDataMap.Add(LatentAction, Data);

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	bool bStarted = Interop.RefreshCloudAnchorProperties(Data);

	OutResult = (EAzureSpatialAnchorsResult)Data->Result;
	OutErrorString = Data->OutError.c_str();

	return bStarted;
}
bool FAzureSpatialAnchorsForWMR::RefreshCloudAnchorPropertiesAsync_Update(FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor* InAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	AzureSpatialAnchorsInterop::RefreshCloudAnchorPropertiesAsyncDataPtr& Data = RefreshCloudAnchorPropertiesAsyncDataMap.FindChecked(LatentAction);
	if (Data->Completed)
	{
		OutResult = (EAzureSpatialAnchorsResult)Data->Result;
		OutErrorString = Data->OutError.c_str();
		RefreshCloudAnchorPropertiesAsyncDataMap.Remove(LatentAction);

		return true;
	}
	else
	{
		return false;
	}
}
void FAzureSpatialAnchorsForWMR::RefreshCloudAnchorPropertiesAsync_Orphan(FPendingLatentAction* LatentAction)
{
	RefreshCloudAnchorPropertiesAsyncDataMap.Remove(LatentAction);
}


bool FAzureSpatialAnchorsForWMR::GetCloudAnchorPropertiesAsync_Start(FPendingLatentAction* LatentAction, FString CloudIdentifier, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	//TODO
	return false;
}
bool FAzureSpatialAnchorsForWMR::GetCloudAnchorPropertiesAsync_Update(FPendingLatentAction* LatentAction, FString CloudIdentifier, UAzureCloudSpatialAnchor*& OutAzureCloudSpatialAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	//TODO
	return false;
}
void FAzureSpatialAnchorsForWMR::GetCloudAnchorPropertiesAsync_Orphan(FPendingLatentAction* LatentAction)
{
	GetCloudAnchorPropertiesAsyncDataMap.Remove(LatentAction);
}


bool FAzureSpatialAnchorsForWMR::CreateWatcherAsync_Start(FPendingLatentAction* LatentAction, const FAzureSpatialAnchorsLocateCriteria& InLocateCriteria, float InWorldToMetersScale, int32& OutWatcherIdentifier, TArray<UAzureCloudSpatialAnchor*>& OutAzureCloudSpatialAnchors, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	AzureSpatialAnchorsInterop::CreateWatcherAsyncDataPtr Data = std::make_shared<AzureSpatialAnchorsInterop::CreateWatcherAsyncData>();
	Data->bBypassCache = InLocateCriteria.bBypassCache;
	Data->Identifiers.reserve(InLocateCriteria.Identifiers.Num());
	for (auto& itr : InLocateCriteria.Identifiers)
	{
		// skip empty identifiers, which the microsoft api throws exceptions about.
		if (!itr.IsEmpty())
		{
			Data->Identifiers.push_back(*itr);
		}
		else
		{
			UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("CreateWatcherAsync called with an empty identifier in its LocateCritera.  Ignoring the empty identifier."));
		}
	}
	Data->NearCloudAnchorID = InLocateCriteria.NearAnchor ? InLocateCriteria.NearAnchor->CloudAnchorID : UAzureCloudSpatialAnchor::AzureCloudAnchorID_Invalid;
	Data->NearCloudAnchorDistance = InLocateCriteria.NearAnchorDistance / InWorldToMetersScale;
	Data->NearCloudAnchorMaxResultCount = InLocateCriteria.NearAnchorMaxResultCount;

	Data->SearchNearDevice = InLocateCriteria.bSearchNearDevice;
	Data->NearDeviceDistance = InLocateCriteria.NearDeviceDistance / InWorldToMetersScale;
	Data->NearDeviceMaxResultCount = InLocateCriteria.NearDeviceMaxResultCount;

	Data->AzureSpatialAnchorDataCategory = static_cast<int>(InLocateCriteria.RequestedCategories);
	Data->AzureSptialAnchorsLocateStrategy = static_cast<int>(InLocateCriteria.Strategy);

	CreateWatcherAsyncDataMap.Add(LatentAction, Data);

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	bool bStarted = Interop.CreateWatcher(Data);

	OutAzureCloudSpatialAnchors.Empty();
	OutResult = (EAzureSpatialAnchorsResult)Data->Result;
	OutErrorString = Data->OutError.c_str();

	return bStarted;
}
bool FAzureSpatialAnchorsForWMR::CreateWatcherAsync_Update(FPendingLatentAction* LatentAction, TArray<UAzureCloudSpatialAnchor*>& OutAzureCloudSpatialAnchors, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	AzureSpatialAnchorsInterop::CreateWatcherAsyncDataPtr& Data = CreateWatcherAsyncDataMap.FindChecked(LatentAction);
	if (Data->Completed)
	{
		OutResult = (EAzureSpatialAnchorsResult)Data->Result;
		OutErrorString = Data->OutError.c_str();
		for (auto newCloudAnchorID : Data->OutCloudAnchorIDs)
		{
			check(newCloudAnchorID != UAzureCloudSpatialAnchor::AzureCloudAnchorID_Invalid);
			UAzureCloudSpatialAnchor* NewCloudAnchor = NewObject<UAzureCloudSpatialAnchor>();
			NewCloudAnchor->CloudAnchorID = newCloudAnchorID;

			CloudAnchors.Add(NewCloudAnchor);
			OutAzureCloudSpatialAnchors.Add(NewCloudAnchor);
		}
		CreateWatcherAsyncDataMap.Remove(LatentAction);

		return true;
	}
	else
	{
		return false;
	}
}
void FAzureSpatialAnchorsForWMR::CreateWatcherAsync_Orphan(FPendingLatentAction* LatentAction)
{
	CreateWatcherAsyncDataMap.Remove(LatentAction);
}


bool FAzureSpatialAnchorsForWMR::StopWatcher(const int32 InWatcherIdentifier)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return Interop.StopWatcher(InWatcherIdentifier);
}

bool FAzureSpatialAnchorsForWMR::CreateARPinAroundAzureCloudSpatialAnchor(const FString& PinId, UAzureCloudSpatialAnchor*& InAzureCloudSpatialAnchor, UARPin*& OutARPin)
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

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	if (Interop.CreateARPinAroundAzureCloudSpatialAnchor(*PinId, InAzureCloudSpatialAnchor->CloudAnchorID) == false)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("WrapARPinAroundAzureCloudSpatialAnchor interop failed to store the local anchor from the cloud anchor.  Ignoring."));
		return false;
	}

	// create a pin around the local anchor
	OutARPin = UHoloLensARFunctionLibrary::CreateNamedARPinAroundAnchor(FName(PinId), PinId);
	return (OutARPin != nullptr);
	{
		InAzureCloudSpatialAnchor->ARPin = OutARPin;
		return true;
	}
}



IMPLEMENT_MODULE(FAzureSpatialAnchorsForWMR, AzureSpatialAnchorsForWMR)

