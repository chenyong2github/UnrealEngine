// Copyright Epic Games, Inc. All Rights Reserved.

#include "IHoloLensDeviceDetectorModule.h"
#include "HttpModule.h"
#include "PlatformHttp.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ScopeLock.h"

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

#if USE_WINRT_DEVICE_WATCHER
#include <vccorlib.h>
#include <collection.h>
#endif

namespace HoloLensDeviceTypes
{
	const FName HoloLens = "HoloLens.Device";
	const FName HoloLensEmulation = "HoloLens.Emulation";
}

class FHoloLensDeviceDetectorModule : public IHoloLensDeviceDetectorModule
{
public:
	FHoloLensDeviceDetectorModule();
	~FHoloLensDeviceDetectorModule();

	virtual void StartDeviceDetection();
	virtual void StopDeviceDetection();

	virtual FOnDeviceDetected& OnDeviceDetected()			{ return DeviceDetected; }
	virtual const TArray<FHoloLensDeviceInfo> GetKnownDevices()	{ return KnownDevices; }

private:
#if USE_WINRT_DEVICE_WATCHER
	static void DeviceWatcherDeviceAdded(Platform::Object^ Sender, Windows::Devices::Enumeration::DeviceInformation^ Info);
	static void DeviceWatcherDeviceRemoved(Platform::Object^ Sender, Windows::Devices::Enumeration::DeviceInformationUpdate^ Info);
	static void DeviceWatcherDeviceUpdated(Platform::Object^ Sender, Windows::Devices::Enumeration::DeviceInformationUpdate^ Info);
	static void DeviceWatcherDeviceEnumerationCompleted(Platform::Object^ Sender, Platform::Object^);
#endif

	void AddDevice(const FHoloLensDeviceInfo& Info);

	//static void OnDevicePortalProbeRequestCompleted(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);

private:
	static FHoloLensDeviceDetectorModule* ThisModule;

#if USE_WINRT_DEVICE_WATCHER
	Windows::Devices::Enumeration::DeviceWatcher^ HoloLensDeviceWatcher;
#endif

	FOnDeviceDetected DeviceDetected;
	FCriticalSection DevicesLock;
	TArray<FHoloLensDeviceInfo> KnownDevices;
};

FHoloLensDeviceDetectorModule* FHoloLensDeviceDetectorModule::ThisModule = nullptr;

IMPLEMENT_MODULE(FHoloLensDeviceDetectorModule, HoloLensDeviceDetector);

FHoloLensDeviceDetectorModule::FHoloLensDeviceDetectorModule()
{
	// Hang on to the singleton instance of the loaded module
	// so that we can use it from an arbitrary thread context
	// (in response to Device Watcher events)
	check(!ThisModule);
	ThisModule = this;
}

FHoloLensDeviceDetectorModule::~FHoloLensDeviceDetectorModule()
{
	check(ThisModule == this);
	ThisModule = nullptr;
}

void FHoloLensDeviceDetectorModule::StartDeviceDetection()
{
	// Ensure local device is available even when device portal is off
	// or full device watcher is not enabled.
	if (KnownDevices.Num() == 0)
	{
		FHoloLensDeviceInfo LocalDevice;
		LocalDevice.HostName = FPlatformProcess::ComputerName();
		LocalDevice.bIs64Bit = true;
		LocalDevice.bRequiresCredentials = false;
		// @todo JoeG - Check for local emulation support
		LocalDevice.bCanDeployTo = false;
		LocalDevice.DeviceTypeName = HoloLensDeviceTypes::HoloLensEmulation;
		ThisModule->AddDevice(LocalDevice);

		LocalDevice.HostName = FPlatformProcess::ComputerName();
		LocalDevice.bIs64Bit = true;
		LocalDevice.bRequiresCredentials = false;
		LocalDevice.bCanDeployTo = false;
		LocalDevice.DeviceTypeName = HoloLensDeviceTypes::HoloLens;
		ThisModule->AddDevice(LocalDevice);
	}

#if USE_WINRT_DEVICE_WATCHER
	using namespace Platform;
	using namespace Platform::Collections;
	using namespace Windows::Foundation;
	using namespace Windows::Devices::Enumeration;

	if (HoloLensDeviceWatcher != nullptr)
	{
		return;
	}

	// Ensure the HTTP module is initialized so it can be used from device watcher events.
	FHttpModule::Get();

	RoInitialize(RO_INIT_MULTITHREADED);

	// Start up a watcher with a filter designed to pick up devices that have Windows Device Portal enabled
	String^ AqsFilter = "System.Devices.AepService.ProtocolId:={4526e8c1-8aac-4153-9b16-55e86ada0e54} AND System.Devices.Dnssd.Domain:=\"local\" AND System.Devices.Dnssd.ServiceName:=\"_wdp._tcp\"";
	Vector<String^>^ AdditionalProperties = ref new Vector<String^>();
	AdditionalProperties->Append("System.Devices.Dnssd.HostName");
	AdditionalProperties->Append("System.Devices.Dnssd.ServiceName");
	AdditionalProperties->Append("System.Devices.Dnssd.PortNumber");
	AdditionalProperties->Append("System.Devices.Dnssd.TextAttributes");
	AdditionalProperties->Append("System.Devices.IpAddress");

	HoloLensDeviceWatcher = DeviceInformation::CreateWatcher(AqsFilter, AdditionalProperties, DeviceInformationKind::AssociationEndpointService);
	HoloLensDeviceWatcher->Added += ref new TypedEventHandler<DeviceWatcher^, DeviceInformation^>(&FHoloLensDeviceDetectorModule::DeviceWatcherDeviceAdded);
	HoloLensDeviceWatcher->Removed += ref new TypedEventHandler<DeviceWatcher^, DeviceInformationUpdate^>(&FHoloLensDeviceDetectorModule::DeviceWatcherDeviceRemoved);
	HoloLensDeviceWatcher->Updated += ref new TypedEventHandler<DeviceWatcher^, DeviceInformationUpdate^>(&FHoloLensDeviceDetectorModule::DeviceWatcherDeviceUpdated);
	HoloLensDeviceWatcher->EnumerationCompleted += ref new TypedEventHandler<DeviceWatcher^, Object^>(&FHoloLensDeviceDetectorModule::DeviceWatcherDeviceEnumerationCompleted);

	HoloLensDeviceWatcher->Start();
#endif
}

void FHoloLensDeviceDetectorModule::StopDeviceDetection()
{
#if USE_WINRT_DEVICE_WATCHER
	if (HoloLensDeviceWatcher != nullptr)
	{
		HoloLensDeviceWatcher->Stop();
		HoloLensDeviceWatcher = nullptr;
	}
#endif
}


#if USE_WINRT_DEVICE_WATCHER
void FHoloLensDeviceDetectorModule::DeviceWatcherDeviceAdded(Platform::Object^ Sender, Windows::Devices::Enumeration::DeviceInformation^ Info)
{
	using namespace Platform;
	using namespace Platform::Collections;
	using namespace Windows::Foundation;
	using namespace Windows::Devices::Enumeration;

	FHoloLensDeviceInfo NewDevice;
	NewDevice.WindowsDeviceId = Info->Id->Data();

	NewDevice.HostName = safe_cast<IPropertyValue^>(Info->Properties->Lookup("System.Devices.Dnssd.HostName"))->GetString()->Data();
	NewDevice.HostName.RemoveFromEnd(TEXT(".local"));

	uint32 WdpPort = safe_cast<IPropertyValue^>(Info->Properties->Lookup("System.Devices.Dnssd.PortNumber"))->GetUInt32();
	Array<String^>^ TextAttributes;
	safe_cast<IPropertyValue^>(Info->Properties->Lookup("System.Devices.Dnssd.TextAttributes"))->GetStringArray(&TextAttributes);
	FString Protocol = TEXT("http");
	FName DeviceType = NAME_None;
	for (uint32 i = 0; i < TextAttributes->Length; ++i)
	{
		FString Architecture;
		uint32 SecurePort;
		if (FParse::Value(TextAttributes[i]->Data(), TEXT("S="), SecurePort))
		{
			Protocol = TEXT("https");
			WdpPort = SecurePort;
		}
		else if (FParse::Value(TextAttributes[i]->Data(), TEXT("D="), NewDevice.DeviceTypeName))
		{

		}
		else if (FParse::Value(TextAttributes[i]->Data(), TEXT("A="), Architecture))
		{
			NewDevice.Is64Bit = (Architecture == TEXT("AMD64"));
		}
	}

	Array<String^>^ AllIps;
	safe_cast<IPropertyValue^>(Info->Properties->Lookup("System.Devices.IpAddress"))->GetStringArray(&AllIps);
	if (AllIps->Length <= 0)
	{
		return;
	}
	FString DeviceIp = AllIps[0]->Data();

	NewDevice.WdpUrl = FString::Printf(TEXT("%s://%s:%d"), *Protocol, *DeviceIp, WdpPort);

	// Now make a test request against the device to determine whether or not the Device Portal requires authentication.
	// If it does, the user will have to add it manually so we can collect the username and password.
	TSharedRef<IHttpRequest> TestRequest = FHttpModule::Get().CreateRequest();
	TestRequest->SetVerb(TEXT("GET"));
	TestRequest->SetURL(NewDevice.WdpUrl);
	TestRequest->OnProcessRequestComplete().BindLambda(
		[NewDevice](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
	{
		bool RequiresCredentials = false;
		bool ReachedDevicePortal = false;
		if (bSucceeded && HttpResponse.IsValid())
		{
			int32 ResponseCode = HttpResponse->GetResponseCode();
			RequiresCredentials = ResponseCode == EHttpResponseCodes::Denied;
			ReachedDevicePortal = RequiresCredentials || EHttpResponseCodes::IsOk(ResponseCode);
		}

		if (ReachedDevicePortal || NewDevice.IsLocal())
		{
			FHoloLensDeviceInfo ValidatedDevice = NewDevice;
			ValidatedDevice.RequiresCredentials = RequiresCredentials;
			ThisModule->AddDevice(ValidatedDevice);
		}
	});
	TestRequest->ProcessRequest();
}

void FHoloLensDeviceDetectorModule::DeviceWatcherDeviceRemoved(Platform::Object^ Sender, Windows::Devices::Enumeration::DeviceInformationUpdate^ Info)
{
	// This doesn't seem to hit even when the remote device goes offline
}

void FHoloLensDeviceDetectorModule::DeviceWatcherDeviceUpdated(Platform::Object^ Sender, Windows::Devices::Enumeration::DeviceInformationUpdate^ Info)
{

}

void FHoloLensDeviceDetectorModule::DeviceWatcherDeviceEnumerationCompleted(Platform::Object^ Sender, Platform::Object^)
{

}
#endif

void FHoloLensDeviceDetectorModule::AddDevice(const FHoloLensDeviceInfo& Info)
{
	FScopeLock Lock(&DevicesLock);
	for (const FHoloLensDeviceInfo& Existing : KnownDevices)
	{
		if (Existing.HostName == Info.HostName)
		{
			return;
		}
	}

	KnownDevices.Add(Info);
	DeviceDetected.Broadcast(Info);
}

#include "Windows/HideWindowsPlatformTypes.h"
