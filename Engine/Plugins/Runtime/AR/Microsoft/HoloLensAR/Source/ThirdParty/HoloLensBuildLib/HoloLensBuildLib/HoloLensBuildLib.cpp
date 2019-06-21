// Copyright (c) Microsoft Corporation. All rights reserved.

#include "stdafx.h"
#include "HoloLensBuildLib.h"

#include "wrl/client.h"
#include "wrl/wrappers/corewrappers.h"

#include <shlwapi.h>
#include <shobjidl.h>
#include <AppxPackaging.h>

using namespace Microsoft::WRL;

namespace WindowsMixedReality
{
	bool HoloLensBuildLib::PackageProject(const wchar_t* StreamPath, bool PathIsActuallyPackage, const wchar_t* Params, UINT*& OutProcessId)
	{
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
				if (SUCCEEDED(ManifestPackageId->GetPackageFullName(&PackageFullName)))
				{
					PackageDebugSettings->EnableDebugging(PackageFullName, nullptr, nullptr);
					CoTaskMemFree(PackageFullName);
				}
			}
		}

		LPWSTR Aumid = nullptr;
		if (FAILED(ApplicationMetadata->GetAppUserModelId(&Aumid)))
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
	}
}
