#include "WindowsMixedRealityInteropLoader.h"
#include "Misc/MessageDialog.h"
#include "WindowsMixedRealityHMD.h"
#include "Interfaces/IPluginManager.h"

#if WITH_WINDOWS_MIXED_REALITY
#include "MixedRealityInterop.h"
#endif

// Holographic Remoting is only supported in Windows 10 version 1809 or better
// Originally we were supporting 1803, but there were rendering issues specific to that version so for now we only support 1809
#define MIN_WIN_10_VERSION_FOR_WMR 1809


namespace WindowsMixedReality
{

	MixedRealityInterop * LoadInteropLibrary()
	{
		MixedRealityInterop * HMD = nullptr;
#if WITH_WINDOWS_MIXED_REALITY
#if !PLATFORM_HOLOLENS
		FString OSVersionLabel;
		FString OSSubVersionLabel;
		FPlatformMisc::GetOSVersions(OSVersionLabel, OSSubVersionLabel);
		// GetOSVersion returns the Win10 release version in the OSVersion rather than the OSSubVersion, so parse it out ourselves
		OSSubVersionLabel = OSVersionLabel;
		bool bHasSupportedWindowsVersion = OSSubVersionLabel.StartsWith("Windows 10") || OSSubVersionLabel.StartsWith("Windows Server 2019");
		bool bHasSupportedWindowsBuild = false;

		// If we can't find Win10 version, check for Windows Server equivalent
		if (!bHasSupportedWindowsVersion)
		{
			OSSubVersionLabel = OSVersionLabel;
			bHasSupportedWindowsVersion = OSSubVersionLabel.RemoveFromStart("Windows Server Technical Preview (Release ") && OSSubVersionLabel.RemoveFromEnd(")") && (FCString::Atoi(*OSSubVersionLabel) >= MIN_WIN_10_VERSION_FOR_WMR);
		}

		if (bHasSupportedWindowsVersion)
		{
			FString BeginMarker = TEXT("(Release ");
			int Begin = OSSubVersionLabel.Find(BeginMarker);
			int End = OSSubVersionLabel.Find(")", ESearchCase::IgnoreCase, ESearchDir::FromStart, Begin);
			if (Begin >= 0 && End >= 0)
			{
				Begin += BeginMarker.Len();
				OSSubVersionLabel = OSSubVersionLabel.Mid(Begin, End - Begin);
				bHasSupportedWindowsBuild = FCString::Atoi(*OSSubVersionLabel) >= MIN_WIN_10_VERSION_FOR_WMR;
			}
		}

		if (bHasSupportedWindowsBuild && bHasSupportedWindowsVersion)
		{
			// Get the base directory of this plugin
			FString BaseDir = IPluginManager::Get().FindPlugin("WindowsMixedReality")->GetBaseDir();

			FString EngineDir = FPaths::EngineDir();
			FString BinariesSubDir = FPlatformProcess::GetBinariesSubdirectory();
#if WINDOWS_MIXED_REALITY_DEBUG_DLL
			FString DLLName(TEXT("MixedRealityInteropDebug.dll"));
#else // WINDOWS_MIXED_REALITY_DEBUG_DLL
			FString DLLName(TEXT("MixedRealityInterop.dll"));
#endif // WINDOWS_MIXED_REALITY_DEBUG_DLL
			FString MRInteropLibraryPath = EngineDir / "Binaries/ThirdParty/Windows/x64" / DLLName;

#if PLATFORM_64BITS
			// Load these dependencies first or MixedRealityInteropLibraryHandle fails to load since it doesn't look in the correct path for its dependencies automatically
			FString HoloLensLibraryDir = EngineDir / "Binaries/ThirdParty/Windows/x64";
			FPlatformProcess::PushDllDirectory(*HoloLensLibraryDir);
			FPlatformProcess::GetDllHandle(_TEXT("PerceptionDevice.dll"));
			FPlatformProcess::GetDllHandle(_TEXT("Microsoft.Holographic.AppRemoting.dll"));
			FPlatformProcess::GetDllHandle(_TEXT("Microsoft.MixedReality.QR.dll"));
			FPlatformProcess::GetDllHandle(_TEXT("Microsoft.MixedReality.SceneUnderstanding.dll"));
			FPlatformProcess::GetDllHandle(_TEXT("Microsoft.Azure.SpatialAnchors.dll"));
			FPlatformProcess::PopDllDirectory(*HoloLensLibraryDir);

			FPlatformProcess::GetDllHandle(*(EngineDir / "Binaries" / BinariesSubDir / "HolographicStreamerDesktop.dll"));
			FPlatformProcess::GetDllHandle(*(EngineDir / "Binaries" / BinariesSubDir / "Microsoft.Perception.Simulation.dll"));
#endif // PLATFORM_64BITS

			// Then finally try to load the WMR Interop Library
			void* MixedRealityInteropLibraryHandle = !MRInteropLibraryPath.IsEmpty() ? FPlatformProcess::GetDllHandle(*MRInteropLibraryPath) : nullptr;
			if (MixedRealityInteropLibraryHandle)
			{
				HMD = new MixedRealityInterop();
			}
			else
			{
				FText ErrorText = NSLOCTEXT("WindowsMixedRealityHMD", "MixedRealityInteropLibraryError", "Failed to load Windows Mixed Reality Interop Library!  Windows Mixed Reality cannot function.");
				FMessageDialog::Open(EAppMsgType::Ok, ErrorText);
				UE_LOG(LogWmrHmd, Error, TEXT("%s"), *ErrorText.ToString());
			}
		}
		else
		{
			FText ErrorText = FText::Format(FTextFormat(NSLOCTEXT("WindowsMixedRealityHMD", "MixedRealityInteropLibraryNotSupported", "Windows Mixed Reality is not supported on this Windows version. \nNote: UE4 only supports Windows Mixed Reality on Windows 10 Release {0} or higher. Current version: {1}")),
				FText::FromString(FString::FromInt(MIN_WIN_10_VERSION_FOR_WMR)), FText::FromString(OSVersionLabel));
			FMessageDialog::Open(EAppMsgType::Ok, ErrorText);
			if (IsRunningCommandlet())
			{
				UE_LOG(LogWmrHmd, Warning, TEXT("%s"), *ErrorText.ToString());
			}
			else
			{
				UE_LOG(LogWmrHmd, Error, TEXT("%s"), *ErrorText.ToString());
			}
		}

#else // !PLATFORM_HOLOLENS
		// ASA is unable to find the CoarseRelocUW.dll dependency of Microsoft.Azure.SpatialAnchors.dll on it's own because it doesn't look in the correct path automatically
		// Therefore we manually load that dll in advance.
		FString HoloLensLibraryDir = "Engine/Binaries/ThirdParty/Hololens/arm64";
		FPlatformProcess::PushDllDirectory(*HoloLensLibraryDir);
		FPlatformProcess::GetDllHandle(_TEXT("CoarseRelocUW.dll"));
		FPlatformProcess::PopDllDirectory(*HoloLensLibraryDir);

		HMD = new MixedRealityInterop();
#endif // !PLATFORM_HOLOLENS
#endif
		return HMD;
	}
}