// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoloLensTargetDevice.h"

#include "Misc/Paths.h"

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/AllowWindowsPlatformTypes.h"

#endif
#include "HttpModule.h"
#include "PlatformHttp.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ScopeLock.h"
#include "Misc/Base64.h"
#include "Containers/UnrealString.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ScopeLock.h"

#pragma warning(push)
#pragma warning(disable:4265 4459)

#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#pragma warning(pop)

#include <shlwapi.h>

#if (NTDDI_VERSION < NTDDI_WIN8)
#pragma push_macro("NTDDI_VERSION")
#undef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN8
//this header cannot be directly imported because of current _WIN32_WINNT less then 0x0602 (the value assigned in UEBuildWindows.cs:139)
//the macro code added from couple interface declarations, it doesn't affect to any imported function
#include <shobjidl.h> 
#pragma pop_macro("NTDDI_VERSION")
#else
#include <shobjidl.h> 
#endif

#if APPXPACKAGING_ENABLE 
#include <AppxPackaging.h>
#endif

#include "Microsoft/COMPointer.h"

#if WITH_ENGINE
#include "Engine/World.h"
#include "TimerManager.h"
#endif
#include <functional>




namespace WindowsMixedReality
{
	bool PackageProject(const wchar_t* StreamPath, bool PathIsActuallyPackage, const wchar_t* Params, UINT*& OutProcessId)
	{
#if APPXPACKAGING_ENABLE
		using namespace Microsoft::WRL;
		ComPtr<IAppxFactory> AppxFactory;
		if (FAILED(CoCreateInstance(CLSID_AppxFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&AppxFactory))))
		{
			return false;
		}

		ComPtr<IStream> ReaderStream;
		if (FAILED(SHCreateStreamOnFileEx(StreamPath, STGM_READ, 0, FALSE, nullptr, &ReaderStream)))
		{
			return false;
		}

		ComPtr<IAppxManifestReader> ManifestReader;
		if (PathIsActuallyPackage)
		{
			ComPtr<IAppxPackageReader> PackageReader;
			if (FAILED(AppxFactory->CreatePackageReader(ReaderStream.Get(), &PackageReader)))
			{
				return false;
			}

			if (FAILED(PackageReader->GetManifest(&ManifestReader)))
			{
				return false;
			}
		}
		else
		{
			if (FAILED(AppxFactory->CreateManifestReader(ReaderStream.Get(), &ManifestReader)))
			{
				return false;
			}
		}

		ComPtr<IAppxManifestApplicationsEnumerator> AppEnumerator;
		if (FAILED(ManifestReader->GetApplications(&AppEnumerator)))
		{
			return false;
		}

		ComPtr<IAppxManifestApplication> ApplicationMetadata;
		if (FAILED(AppEnumerator->GetCurrent(&ApplicationMetadata)))
		{
			return false;
		}

		ComPtr<IPackageDebugSettings> PackageDebugSettings;
		if (SUCCEEDED(CoCreateInstance(CLSID_PackageDebugSettings, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&PackageDebugSettings))))
		{
			ComPtr<IAppxManifestPackageId> ManifestPackageId;
			if (SUCCEEDED(ManifestReader->GetPackageId(&ManifestPackageId)))
			{
				LPWSTR PackageFullName = nullptr;
				// Avoid warning C6387 (static analysis assumes PackageFullName could be null even if the GetPackageFullName call succeeds.
				if (SUCCEEDED(ManifestPackageId->GetPackageFullName(&PackageFullName)) && PackageFullName != nullptr)
				{
					PackageDebugSettings->EnableDebugging(PackageFullName, nullptr, nullptr);
					CoTaskMemFree(PackageFullName);
				}
			}
		}

		LPWSTR Aumid = nullptr;
		// Avoid warning C6387 (static analysis assumes Aumid could be null even if the GetAppUserModelId call succeeds.
		if (FAILED(ApplicationMetadata->GetAppUserModelId(&Aumid)) || Aumid == nullptr)
		{
			return false;
		}

		bool ActivationSuccess = false;
		ComPtr<IApplicationActivationManager> ActivationManager;
		if (SUCCEEDED(CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ActivationManager))))
		{
			DWORD NativeProcessId;
			if (SUCCEEDED(ActivationManager->ActivateApplication(Aumid, Params, AO_NONE, &NativeProcessId)))
			{
				if (OutProcessId != nullptr)
				{
					*OutProcessId = NativeProcessId;
				}
				ActivationSuccess = true;
			}
		}

		CoTaskMemFree(Aumid);
		return ActivationSuccess;
#else
		return false;
#endif
	}
}

FHoloLensTargetDevice::FHoloLensTargetDevice(const ITargetPlatform& InTargetPlatform, const FHoloLensDeviceInfo& InInfo)
	: HeartBeatRequest(nullptr)
	, TargetPlatform(InTargetPlatform)
	, Info(InInfo)
	, IsDeviceConnected(true)
{
	if (!Info.IsLocal)
	{
#if WITH_ENGINE
		GWorld->GetTimerManager().SetTimer(TimerHandle, std::bind(&FHoloLensTargetDevice::StartHeartBeat, this), 10.f, true, 10.f);
#endif
	}
}

FHoloLensTargetDevice::~FHoloLensTargetDevice()
{
	FScopeLock lock(&CriticalSection);
	if (!Info.IsLocal)
	{
#if WITH_ENGINE
		if (TimerHandle.IsValid())
		{
			GWorld->GetTimerManager().ClearTimer(TimerHandle);
		}
#endif
		if (HeartBeatRequest)
		{
			HeartBeatRequest->OnProcessRequestComplete().Unbind();
			HeartBeatRequest->CancelRequest();
			HeartBeatRequest = nullptr;
		}
	}
}


bool FHoloLensTargetDevice::Connect() 
{
	return true;
}

void FHoloLensTargetDevice::Disconnect() 
{ }

ETargetDeviceTypes FHoloLensTargetDevice::GetDeviceType() const 
{
	return ETargetDeviceTypes::HMD;
}

FTargetDeviceId FHoloLensTargetDevice::GetId() const 
{
	if (Info.IsLocal)
	{
		return FTargetDeviceId(TargetPlatform.PlatformName(), Info.HostName);
	}
	else
	{
		// This is what gets handed off to UAT, so we need to supply the
		// actual Device Portal url instead of just the host name
		return FTargetDeviceId(TargetPlatform.PlatformName(), Info.WdpUrl);
	}
}

FString FHoloLensTargetDevice::GetName() const 
{
	return Info.HostName + TEXT(" (HoloLens)");
}

FString FHoloLensTargetDevice::GetOperatingSystemName() 
{
	return FString::Printf(TEXT("HoloLens (%s)"), *Info.DeviceTypeName.ToString());
}

bool FHoloLensTargetDevice::GetProcessSnapshotAsync(TFunction<void(const TArray<FTargetDeviceProcessInfo>&)> CompleteHandler)
{
	if (Info.IsLocal)
	{
		return false;
	}

	auto HttpRequest = GenerateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(Info.WdpUrl + TEXT("api/resourcemanager/processes"));
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[&, CompleteHandler](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
	{
		TArray<FTargetDeviceProcessInfo> OutProcessInfos;
		do
		{
			if (!bSucceeded || !HttpResponse.IsValid())
			{
				break;
			}
			FString Json = HttpResponse->GetContentAsString();
			auto RootObject = TSharedPtr<FJsonObject>();
			auto Reader = TJsonReaderFactory<TCHAR>::Create(Json);

			if (!FJsonSerializer::Deserialize(Reader, RootObject))
			{
				break;
			}

			const TArray< TSharedPtr<FJsonValue> > * OutArray;
			if (!RootObject->TryGetArrayField(TEXT("Processes"), OutArray))
			{
				break;
			}

			for (TSharedPtr<FJsonValue> process : *OutArray)
			{
				FTargetDeviceProcessInfo ProcessInfo;
				ProcessInfo.ParentId = 0;
				FString Publisher; //present for HoloLens processes only
				FString ImageName, AppName;
				TSharedPtr<FJsonObject> obj = process->AsObject();
				if (!GetJsonField(Publisher, obj, TEXT("Publisher")))
				{
					continue;
				}
				if (!GetJsonField(ImageName, obj, TEXT("ImageName")))
				{
					continue;
				}
				if (!GetJsonField(AppName, obj, TEXT("AppName")))
				{
					continue;
				}
				if (!GetJsonField(ProcessInfo.UserName, obj, TEXT("UserName")))
				{
					continue;
				}
				if (!GetJsonField(ProcessInfo.Id, obj, TEXT("ProcessId")))
				{
					continue;
				}
				ProcessInfo.Name = FString::Format(TEXT("{0} ({1})"), { ImageName , AppName } );
				OutProcessInfos.Add(ProcessInfo);
			}
		} while (false);

		CompleteHandler(OutProcessInfos);
	});

	HttpRequest->ProcessRequest();
	return true;
}

const class ITargetPlatform& FHoloLensTargetDevice::GetTargetPlatform() const
{
	return TargetPlatform;
}

bool FHoloLensTargetDevice::GetUserCredentials(FString& OutUserName, FString& OutUserPassword) 
{
	if (!Info.RequiresCredentials)
	{
		return false;
	}

	OutUserName = Info.Username;
	OutUserPassword = Info.Password;
	return true;
}

bool FHoloLensTargetDevice::IsConnected()
{
	if (Info.IsLocal)
	{
		return true;
	}

	return IsDeviceConnected;
}

bool FHoloLensTargetDevice::IsDefault() const 
{
	return true;
}

TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> FHoloLensTargetDevice::GenerateRequest() const
{
	if (Info.IsLocal)
	{
		return nullptr;
	}

	SslCertDisabler disabler;
	
	auto HttpRequest = TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>(FPlatformHttp::ConstructRequest());

	if (Info.RequiresCredentials)
	{
		FString AuthentificationString = FString::Format(TEXT("auto-{0}:{1}"), { Info.Username, Info.Password });
		FString AuthHeaderString = FString::Format(TEXT("Basic {0}"), { FBase64::Encode(AuthentificationString) });
		HttpRequest->AppendToHeader(TEXT("Authorization"), AuthHeaderString);
	}

	return HttpRequest;
}


bool FHoloLensTargetDevice::PowerOff(bool Force) 
{
	if (Info.IsLocal)
	{
		return false;
	}

	auto HttpRequest = GenerateRequest();
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetURL(Info.WdpUrl + TEXT("api/control/shutdown"));
	HttpRequest->ProcessRequest();
	return true;
}

bool FHoloLensTargetDevice::PowerOn() 
{
	return false;
}

bool FHoloLensTargetDevice::Reboot(bool bReconnect) 
{
	if (Info.IsLocal)
	{
		return false;
	}

	auto HttpRequest = GenerateRequest();
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetURL(Info.WdpUrl + TEXT("api/control/restart"));
	HttpRequest->ProcessRequest();

	return true;
}

void FHoloLensTargetDevice::SetUserCredentials(const FString& UserName, const FString& UserPassword) 
{ 
	Info.Username = UserName;
	Info.Password = UserPassword;
}

bool FHoloLensTargetDevice::SupportsFeature(ETargetDeviceFeatures Feature) const 
{
	if (Info.IsLocal)
	{
		return false;
	}

	switch (Feature)
	{
	case ETargetDeviceFeatures::PowerOff:
	case ETargetDeviceFeatures::ProcessSnapshot:
	case ETargetDeviceFeatures::Reboot:
		return true;
	default:
		return false;
	}
}

bool FHoloLensTargetDevice::SupportsSdkVersion(const FString& VersionString) const 
{
	return false;
}

bool FHoloLensTargetDevice::TerminateProcess(const int64 ProcessId) 
{
	if (Info.IsLocal)
	{
		return false;
	}

	auto HttpRequest = GenerateRequest();
	HttpRequest->SetVerb(TEXT("DELETE"));
	HttpRequest->SetURL(Info.WdpUrl + TEXT("api/taskmanager/process?pid=") + FString::FromInt(ProcessId));
	HttpRequest->ProcessRequest();

	return true;
}

bool FHoloLensTargetDevice::TerminateLaunchedProcess(const FString & ProcessIdentifier)
{
	if (Info.IsLocal)
	{
		return false;
	}

	auto HttpRequest = GenerateRequest();
	HttpRequest->SetVerb(TEXT("DELETE"));
	HttpRequest->SetURL(Info.WdpUrl + TEXT("api/taskmanager/app?package=") + FBase64::Encode(ProcessIdentifier));
	HttpRequest->ProcessRequest();

	return true;
}


bool FHoloLensTargetDevice::Deploy(const FString& SourceFolder, FString& OutAppId)
{
	return true;
}

bool FHoloLensTargetDevice::Launch(const FString& AppId, EBuildConfiguration BuildConfiguration, EBuildTargetType TargetType, const FString& Params, uint32* OutProcessId)
{
	return false;
}

bool FHoloLensTargetDevice::Run(const FString& ExecutablePath, const FString& Params, uint32* OutProcessId)
{
	HRESULT hr = CoInitialize(nullptr);
	if (FAILED(hr))
	{
		UE_LOG(LogTemp, Warning, TEXT("FHoloLensTargetDevice::Run - CoInitialize() failed with hr = 0x(%x)"), hr);
	}

	// Currently even packaged builds get an exe name in here which kind of works because we 
	// don't yet support remote deployment and so the loose structure the package was created 
	// from is probably in place on this machine.  So the code will read the manifest from the
	// loose version, but actually launch the package (since that's what's registered).
	auto Extension = FPaths::GetExtension(ExecutablePath);
	bool PathIsActuallyPackage = Extension.StartsWith(TEXT("appx")) || Extension.StartsWith(TEXT("msix"));
	FString StreamPath;
	if (PathIsActuallyPackage)
	{
		StreamPath = ExecutablePath;
	}
	else
	{
		StreamPath = FPaths::Combine(*FPaths::GetPath(ExecutablePath), TEXT("../../.."));
		StreamPath = FPaths::Combine(*StreamPath, TEXT("AppxManifest.xml"));
	}

	return WindowsMixedReality::PackageProject(*StreamPath, PathIsActuallyPackage, *Params, OutProcessId);
}

void FHoloLensTargetDevice::StartHeartBeat()
{
	FScopeLock lock(&CriticalSection);
	if (HeartBeatRequest)
	{
		return;
	}
#if WITH_EDITOR
	if (!TimerHandle.IsValid()) // the app is quiting
	{
		return;
	}
#endif
	HeartBeatRequest = GenerateRequest();
	HeartBeatRequest->SetVerb(TEXT("GET"));
	HeartBeatRequest->SetURL(Info.WdpUrl + TEXT("api/os/info"));

	HeartBeatRequest->OnProcessRequestComplete().BindLambda(
		[=](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
	{
		FString HostName;
		bool Success = false;
		do
		{
			if (!bSucceeded || !HttpResponse.IsValid())
			{
				break;
			}
			FString Json = HttpResponse->GetContentAsString();
			auto RootObject = TSharedPtr<FJsonObject>();
			auto Reader = TJsonReaderFactory<TCHAR>::Create(Json);

			if (!FJsonSerializer::Deserialize(Reader, RootObject))
			{
				break;
			}

			if (GetJsonField(HostName, RootObject, TEXT("ComputerName")))
			{
				Success = (HostName == Info.HostName);
			}
		} while (false);

		{
			FScopeLock lock(&CriticalSection);
			IsDeviceConnected = Success;
			HeartBeatRequest = nullptr;
		}
	});

	HeartBeatRequest->ProcessRequest();
}


#include "Windows/HideWindowsPlatformTypes.h"
