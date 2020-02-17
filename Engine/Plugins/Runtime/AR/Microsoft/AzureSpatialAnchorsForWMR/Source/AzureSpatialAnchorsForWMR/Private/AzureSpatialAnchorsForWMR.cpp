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

void FAzureSpatialAnchorsForWMR::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(IAzureSpatialAnchors::GetModularFeatureName(), this);

#if !PLATFORM_HOLOLENS
#  if PLATFORM_64BITS
	{
		FString EngineDir = FPaths::EngineDir();
		FString HoloLensLibraryDir = EngineDir / "Binaries/ThirdParty/Windows/x64";
		FPlatformProcess::PushDllDirectory(*HoloLensLibraryDir);
		FPlatformProcess::GetDllHandle(TEXT("Microsoft.Azure.SpatialAnchors.dll"));
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

bool FAzureSpatialAnchorsForWMR::ConfigSession(const FString& AccountId, const FString& AccountKey, EAzureSpatialAnchorsLogVerbosity LogVerbosity)
{
	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return Interop.ConfigSession(*AccountId, *AccountKey, (int)LogVerbosity);
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
		return false;
	}

	const FString& AnchorId = WMRPin->GetAnchorId();
	if (UAzureCloudSpatialAnchor** Itr = CloudAnchorMap.Find(AnchorId))
	{
		OutCloudAnchor = *Itr;
		return true;
	}
	else
	{
		OutCloudAnchor = nullptr;
		return false;
	}
}

void FAzureSpatialAnchorsForWMR::GetCloudAnchors(TArray<UAzureCloudSpatialAnchor*>& OutCloudAnchors)
{
	CloudAnchorMap.GenerateValueArray(OutCloudAnchors);
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
	bool bSuccess =  Interop.CreateCloudAnchor(*AnchorId);

	if (bSuccess)
	{
		UAzureCloudSpatialAnchor* NewCloudAnchor = NewObject<UAzureCloudSpatialAnchor>();
		NewCloudAnchor->ARPin = InARPin;
		CloudAnchorMap.Add(AnchorId, NewCloudAnchor);
		OutCloudAnchor = NewCloudAnchor;
	}

	return bSuccess;
}

bool FAzureSpatialAnchorsForWMR::SetCloudAnchorExpiration(UAzureCloudSpatialAnchor*& InCloudAnchor, int MinutesFromNow)
{
	if (InCloudAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("SetCloudAnchorExpiration called with null CloudAnchor.  Ignoring."));
		return false;
	}

	if (MinutesFromNow <= 0)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("SetCloudAnchorExpiration called with <= 0 MinutesFromNow.  Ignoring."));
		return false;
	}

	const UWMRARPin* WMRPin = Cast<UWMRARPin>(InCloudAnchor->ARPin);
	check(WMRPin);
	const FString& AnchorId = WMRPin->GetAnchorId();

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	return Interop.SetCloudAnchorExpiration(*AnchorId, MinutesFromNow);
}

bool FAzureSpatialAnchorsForWMR::SaveCloudAnchorAsync_Start(FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor*& InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (InCloudAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("SaveCloudAnchor called with null CloudAnchor.  Ignoring."));
		return false;
	}

	const UWMRARPin* WMRPin = Cast<UWMRARPin>(InCloudAnchor->ARPin);
	check(WMRPin);
	const FString& LocalAnchorId = WMRPin->GetAnchorId();

	AzureSpatialAnchorsInterop::SaveAsyncDataPtr Data = std::make_shared<AzureSpatialAnchorsInterop::SaveAsyncData>();
	Data->LocalAnchorId = *LocalAnchorId;

	SaveAsyncDataMap.Add(LatentAction, Data);

	AzureSpatialAnchorsInterop& Interop = AzureSpatialAnchorsInterop::Get();
	bool bStarted = Interop.SaveCloudAnchor(Data);
	
	OutResult = (EAzureSpatialAnchorsResult)Data->Result;
	OutErrorString = Data->OutError.c_str();

	return bStarted;
}
bool FAzureSpatialAnchorsForWMR::SaveCloudAnchorAsync_Update(FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor*& InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	AzureSpatialAnchorsInterop::SaveAsyncDataPtr& Data = SaveAsyncDataMap.FindChecked(LatentAction);
	if (Data->Completed)
	{
		InCloudAnchor->CloudIdentifier = Data->CloudAnchorIdentifier.c_str();
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

bool FAzureSpatialAnchorsForWMR::DeleteCloudAnchorAsync_Start(FPendingLatentAction* LatentAction, UAzureCloudSpatialAnchor*& InCloudAnchor, EAzureSpatialAnchorsResult& OutResult, FString& OutErrorString)
{
	if (InCloudAnchor == nullptr)
	{
		UE_LOG(LogAzureSpatialAnchors, Warning, TEXT("DeleteCloudAnchor called with null CloudAnchor.  Ignoring."));
		return false;
	}

	const UWMRARPin* WMRPin = Cast<UWMRARPin>(InCloudAnchor->ARPin);
	check(WMRPin);
	const FString& LocalAnchorId = WMRPin->GetAnchorId();

	AzureSpatialAnchorsInterop::DeleteAsyncDataPtr Data = std::make_shared<AzureSpatialAnchorsInterop::DeleteAsyncData>();
	Data->LocalAnchorId = *LocalAnchorId;

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
			NewCloudAnchor->CloudIdentifier = Data->CloudAnchorIdentifier.c_str();
			CloudAnchorMap.Add(Data->LocalAnchorId.c_str(), NewCloudAnchor);
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
IMPLEMENT_MODULE(FAzureSpatialAnchorsForWMR, AzureSpatialAnchorsForWMR)

