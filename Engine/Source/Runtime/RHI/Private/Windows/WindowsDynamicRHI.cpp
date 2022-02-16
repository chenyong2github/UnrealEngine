// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHI.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"

#if WINDOWS_USE_FEATURE_DYNAMIC_RHI

#include "Windows/WindowsPlatformApplicationMisc.h"

#if defined(NV_GEFORCENOW) && NV_GEFORCENOW
#include "GeForceNOWWrapper.h"
#endif

static const TCHAR* GLoadedRHIModuleName;

// Default to Performance Mode on low-end machines
static bool DefaultFeatureLevelES31()
{
	static TOptional<bool> ForceES31;
	if (ForceES31.IsSet())
	{
		return ForceES31.GetValue();
	}

	FConfigFile EngineSettings;
	FString PlatformNameString = FPlatformProperties::IniPlatformName();
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, *PlatformNameString);

	// Force Performance mode for machines with too few cores including hyperthreads
	int MinCoreCount = 0;
	if (EngineSettings.GetInt(TEXT("PerformanceMode"), TEXT("MinCoreCount"), MinCoreCount) && FPlatformMisc::NumberOfCoresIncludingHyperthreads() < MinCoreCount)
	{
		ForceES31 = true;
		return true;
	}

	FString MinMemorySizeBucketString;
	FString MinIntegratedMemorySizeBucketString;
	if (EngineSettings.GetString(TEXT("PerformanceMode"), TEXT("MinMemorySizeBucket"), MinMemorySizeBucketString) && EngineSettings.GetString(TEXT("PerformanceMode"), TEXT("MinIntegratedMemorySizeBucket"), MinIntegratedMemorySizeBucketString))
	{
		for (int EnumIndex = int(EPlatformMemorySizeBucket::Largest); EnumIndex <= int(EPlatformMemorySizeBucket::Tiniest); EnumIndex++)
		{
			const TCHAR* BucketString = LexToString(EPlatformMemorySizeBucket(EnumIndex));
			// Force Performance mode for machines with too little memory
			if (MinMemorySizeBucketString == BucketString)
			{
				if (FPlatformMemory::GetMemorySizeBucket() >= EPlatformMemorySizeBucket(EnumIndex))
				{
					ForceES31 = true;
					return true;
				}
			}

			// Force Performance mode for machines with too little memory when shared with the GPU
			if (MinIntegratedMemorySizeBucketString == BucketString)
			{
				if (FPlatformMemory::GetMemorySizeBucket() >= EPlatformMemorySizeBucket(EnumIndex) && FWindowsPlatformApplicationMisc::ProbablyHasIntegratedGPU())
				{
					ForceES31 = true;

					return true;
				}
			}
		}
	}

	ForceES31 = false;
	return false;
}

static bool PreferFeatureLevelES31()
{
	if (!GIsEditor)
	{
		bool bIsRunningInGFN = false;
#if defined(NV_GEFORCENOW) && NV_GEFORCENOW
		//Prevent ES31 from being forced since we have other ways of setting scalability issues on GFN.
		GeForceNOWWrapper::Get().Initialize();
		bIsRunningInGFN = GeForceNOWWrapper::Get().IsRunningInGFN();
#endif

		bool bPreferFeatureLevelES31 = false;
		bool bFoundPreference = GConfig->GetBool(TEXT("D3DRHIPreference"), TEXT("bPreferFeatureLevelES31"), bPreferFeatureLevelES31, GGameUserSettingsIni);

		// Force low-spec users into performance mode but respect their choice once they have set a preference
		bool bDefaultES31 = false;
		if (!bFoundPreference && !bIsRunningInGFN)
		{
			bDefaultES31 = DefaultFeatureLevelES31();
		}

		if (bPreferFeatureLevelES31 || bDefaultES31)
		{
			if (!bFoundPreference)
			{
				GConfig->SetBool(TEXT("D3DRHIPreference"), TEXT("bPreferFeatureLevelES31"), true, GGameUserSettingsIni);
			}
			return true;
		}
	}
	return false;
}

static bool AllowD3D12FeatureLevelES31()
{
	if (!GIsEditor)
	{
		bool bAllowD3D12FeatureLevelES31 = true;
		GConfig->GetBool(TEXT("SystemSettings"), TEXT("bAllowD3D12FeatureLevelES31"), bAllowD3D12FeatureLevelES31, GEngineIni);
		return bAllowD3D12FeatureLevelES31;
	}
	return true;
}

namespace
{
	enum class WindowsRHI
	{
		D3D11,
		D3D12,
		Vulkan,
		OpenGL,
	};
}

// Choose the default from DefaultGraphicsRHI or TargetedRHIs. DefaultGraphicsRHI has precedence.
static WindowsRHI ChooseDefaultRHI()
{
	WindowsRHI DefaultRHI = WindowsRHI::D3D11;

	// Default graphics RHI is the main project setting that governs the choice, so it takes the priority
	FConfigFile EngineSettings;
	FString PlatformNameString = FPlatformProperties::PlatformName();
	const TCHAR* PlatformName = *PlatformNameString;
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, PlatformName);
	FString DefaultGraphicsRHI;
	if (EngineSettings.GetString(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("DefaultGraphicsRHI"), DefaultGraphicsRHI))
	{
		FString NAME_DX11(TEXT("DefaultGraphicsRHI_DX11"));
		FString NAME_DX12(TEXT("DefaultGraphicsRHI_DX12"));
		FString NAME_VULKAN(TEXT("DefaultGraphicsRHI_Vulkan"));
		if (DefaultGraphicsRHI == NAME_DX11)
		{
			DefaultRHI = WindowsRHI::D3D11;
		}
		else if (DefaultGraphicsRHI == NAME_DX12)
		{
			DefaultRHI = WindowsRHI::D3D12;
		}
		else if (DefaultGraphicsRHI == NAME_VULKAN)
		{
			DefaultRHI = WindowsRHI::Vulkan;
		}
		else if (DefaultGraphicsRHI != TEXT("DefaultGraphicsRHI_Default"))
		{
			UE_LOG(LogRHI, Error, TEXT("Unrecognized setting '%s' for DefaultGraphicsRHI"), *DefaultGraphicsRHI);
		}
	}
	else 
	{
		// If we don't have DefaultGraphicsRHI set, try to deduce it from the list of targeted shader platforms
		TArray<FString> TargetedShaderFormats;
		GConfig->GetArray(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("TargetedRHIs"), TargetedShaderFormats, GEngineIni);
		if (TargetedShaderFormats.Num() > 0)
		{
			// Make sure the DDSPI is initialized before we try and read from it
			FGenericDataDrivenShaderPlatformInfo::Initialize();

			// Pick the first one
			FName ShaderFormatName(*TargetedShaderFormats[0]);
			EShaderPlatform TargetedPlatform = ShaderFormatToLegacyShaderPlatform(ShaderFormatName);

			// not checking D3D as DefaultRHI begins initialized as D3D12
			if (IsVulkanPlatform(TargetedPlatform))
			{
				DefaultRHI = WindowsRHI::Vulkan;
			}
			else if (IsOpenGLPlatform(TargetedPlatform))
			{
				DefaultRHI = WindowsRHI::OpenGL;
			}
		}
	}

	// If we are in game, there is a separate setting that can make it prefer D3D12 over D3D11 (but not over other RHIs).
	if (!GIsEditor && (DefaultRHI == WindowsRHI::D3D11 || DefaultRHI == WindowsRHI::D3D12))
	{
		bool bUseD3D12InGame = false;
		if (GConfig->GetBool(TEXT("D3DRHIPreference"), TEXT("bUseD3D12InGame"), bUseD3D12InGame, GGameUserSettingsIni))
		{
			DefaultRHI = bUseD3D12InGame ? WindowsRHI::D3D12 : WindowsRHI::D3D11;
		}
	}

	return DefaultRHI;
}

static TOptional<WindowsRHI> ChooseForcedRHI()
{
	TOptional<WindowsRHI> ForcedRHI = {};

	// Command line overrides
	uint32 Sum = 0;
	if (FParse::Param(FCommandLine::Get(), TEXT("vulkan")))
	{
		ForcedRHI = WindowsRHI::Vulkan;
		Sum++;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("opengl")))
	{
		ForcedRHI = WindowsRHI::OpenGL;
		Sum++;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("d3d11")) || FParse::Param(FCommandLine::Get(), TEXT("dx11")))
	{
		ForcedRHI = WindowsRHI::D3D11;
		Sum++;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("d3d12")) || FParse::Param(FCommandLine::Get(), TEXT("dx12")))
	{
		ForcedRHI = WindowsRHI::D3D12;
		Sum++;
	}

	if (Sum > 1)
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RHIOptionsError", "-d3d12/dx12, -d3d11/dx11, -vulkan, and -opengl are mutually exclusive options, but more than one was specified on the command-line."));
		UE_LOG(LogRHI, Fatal, TEXT("-d3d12, -d3d11, -vulkan, and -opengl are mutually exclusive options, but more than one was specified on the command-line."));
	}

#if	!WITH_EDITOR && UE_BUILD_SHIPPING
	// In Shipping builds we can limit ES31 on Windows to only DX11. All RHIs are allowed by default.
	bool bES31DXOnly = false;
	if (!GIsEditor)
	{
		GConfig->GetBool(TEXT("PerformanceMode"), TEXT("bES31DXOnly"), bES31DXOnly, GEngineIni);
	}

	// FeatureLevelES31 is also a command line override, so it will determine the underlying RHI unless one is specified
	if (bES31DXOnly && (FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES31")) || FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES3_1"))))
	{
		if (ForcedRHI == WindowsRHI::OpenGL)
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RHIPerformanceOpenGL", "OpenGL is not supported for Performance Mode."));
			UE_LOG(LogRHI, Fatal, TEXT("OpenGL is not supported for Performance Mode."));
		}
		else if (ForcedRHI == WindowsRHI::Vulkan)
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RHIPerformanceVulkan", "Vulkan is not supported for Performance Mode."));
			UE_LOG(LogRHI, Fatal, TEXT("Vulkan is not supported for Performance Mode."));
		}
		else if (ForcedRHI == WindowsRHI::D3D12)
		{
			if (!AllowD3D12FeatureLevelES31())
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RHIPerformanceDX12", "DirectX 12 is not supported for Performance Mode."));
				UE_LOG(LogRHI, Fatal, TEXT("DirectX 12 is not supported for Performance Mode."));
			}
		}
		else
		{
			ForcedRHI = WindowsRHI::D3D11;
		}
	}
#endif //!WITH_EDITOR && UE_BUILD_SHIPPING

	return ForcedRHI;
}

static ERHIFeatureLevel::Type ChooseFeatureLevel(TOptional<WindowsRHI> ChosenRHI, TOptional<WindowsRHI> ForcedRHI)
{
	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::SM5;
	if (FParse::Param(FCommandLine::Get(), TEXT("sm6")))
	{
		FeatureLevel = ERHIFeatureLevel::SM6;
	}

	bool bAllowD3D12FeatureLevelES31 = AllowD3D12FeatureLevelES31();

	if (!ForcedRHI && (ChosenRHI == WindowsRHI::D3D11 || (ChosenRHI == WindowsRHI::D3D12 && bAllowD3D12FeatureLevelES31)) && PreferFeatureLevelES31())
	{
		FeatureLevel = ERHIFeatureLevel::ES3_1;
	}

	bool bForceES31 = FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES31")) || FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES3_1"));
	if (bForceES31 && (ChosenRHI == WindowsRHI::D3D11 || (ChosenRHI == WindowsRHI::D3D12 && bAllowD3D12FeatureLevelES31)))
	{
		FeatureLevel = ERHIFeatureLevel::ES3_1;
	}

	if (ChosenRHI == WindowsRHI::OpenGL)
	{
		// OpenGL can only be used for mobile preview
		FeatureLevel = ERHIFeatureLevel::ES3_1;
	}

	bool bES31DXOnly = false;
#if !WITH_EDITOR
	if (!GIsEditor)
	{
		GConfig->GetBool(TEXT("PerformanceMode"), TEXT("bES31DXOnly"), bES31DXOnly, GEngineIni);
	}
#endif

	// Allow Vulkan to force ES31 unless we blocked it in config
	if (!bES31DXOnly && ChosenRHI == WindowsRHI::Vulkan && bForceES31)
	{
		FeatureLevel = ERHIFeatureLevel::ES3_1;
	}

	return FeatureLevel;
}

static IDynamicRHIModule* LoadDynamicRHIModule(ERHIFeatureLevel::Type& DesiredFeatureLevel, const TCHAR*& LoadedRHIModuleName)
{
	bool bUseGPUCrashDebugging = false;
	if (!GIsEditor && GConfig->GetBool(TEXT("D3DRHIPreference"), TEXT("bUseGPUCrashDebugging"), bUseGPUCrashDebugging, GGameUserSettingsIni))
	{
		auto GPUCrashDebuggingCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GPUCrashDebugging"));
		*GPUCrashDebuggingCVar = bUseGPUCrashDebugging;
	}

	// RHI is chosen by the project settings (first DefaultGraphicsRHI, then TargetedRHIs are consulted, "Default" maps to D3D12). 
	// After this, a separate game-only setting (does not affect editor) bPreferD3D12InGame selects between D3D12 or D3D11 (but will not have any effect if Vulkan or OpenGL are chosen).
	// Commandline switches apply after this and can force an arbitrary RHIs. If RHI isn't supported, the game will refuse to start.

	WindowsRHI DefaultRHI = ChooseDefaultRHI();
	TOptional<WindowsRHI> ForcedRHI = ChooseForcedRHI();

	WindowsRHI ChosenRHI = DefaultRHI;
	if (ForcedRHI)
	{
		ChosenRHI = ForcedRHI.GetValue();
	}

	DesiredFeatureLevel = ChooseFeatureLevel(ChosenRHI, ForcedRHI);

	// Load the dynamic RHI module.
	IDynamicRHIModule* DynamicRHIModule = NULL;

	if (ChosenRHI == WindowsRHI::OpenGL)
	{
		FApp::SetGraphicsRHI(TEXT("OpenGL"));
		const TCHAR* OpenGLRHIModuleName = TEXT("OpenGLDrv");
		DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(OpenGLRHIModuleName);

		if (!DynamicRHIModule->IsSupported())
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RequiredOpenGL", "OpenGL 4.3 is required to run the engine."));
			FPlatformMisc::RequestExit(1);
			DynamicRHIModule = NULL;
		}

		LoadedRHIModuleName = OpenGLRHIModuleName;
	}
	else if (ChosenRHI == WindowsRHI::Vulkan)
	{
		FApp::SetGraphicsRHI(TEXT("Vulkan"));
		const TCHAR* VulkanRHIModuleName = TEXT("VulkanRHI");
		DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(VulkanRHIModuleName);
		if (!DynamicRHIModule->IsSupported())
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RequiredVulkan", "Vulkan Driver is required to run the engine."));
			FPlatformMisc::RequestExit(1);
			DynamicRHIModule = NULL;
		}
		LoadedRHIModuleName = VulkanRHIModuleName;
	}
	else if (ChosenRHI == WindowsRHI::D3D12)
	{
		FApp::SetGraphicsRHI(TEXT("DirectX 12"));
		LoadedRHIModuleName = TEXT("D3D12RHI");
		DynamicRHIModule = FModuleManager::LoadModulePtr<IDynamicRHIModule>(LoadedRHIModuleName);

		if (!DynamicRHIModule || !DynamicRHIModule->IsSupported())
		{
			if (ForcedRHI == WindowsRHI::D3D12)
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RequiredDX12", "DX12 is not supported on your system. Try running without the -dx12 or -d3d12 command line argument."));
				FPlatformMisc::RequestExit(1);
			}
			if (DynamicRHIModule)
			{
				FModuleManager::Get().UnloadModule(LoadedRHIModuleName);
			}
			DynamicRHIModule = NULL;
			LoadedRHIModuleName = nullptr;
		}
	}

	// Fallback to D3D11RHI if nothing is selected
	if (!DynamicRHIModule)
	{
		FApp::SetGraphicsRHI(TEXT("DirectX 11"));
		const TCHAR* D3D11RHIModuleName = TEXT("D3D11RHI");
		DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(D3D11RHIModuleName);

		if (!DynamicRHIModule->IsSupported())
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RequiredDX11Feature_11_SM5", "A D3D11-compatible GPU (Feature Level 11.0, Shader Model 5.0) is required to run the engine."));
			FPlatformMisc::RequestExit(1);
			DynamicRHIModule = NULL;
		}

		LoadedRHIModuleName = D3D11RHIModuleName;
	}
	return DynamicRHIModule;
}

FDynamicRHI* PlatformCreateDynamicRHI()
{
	FDynamicRHI* DynamicRHI = nullptr;

	ERHIFeatureLevel::Type RequestedFeatureLevel;
	const TCHAR* LoadedRHIModuleName;
	IDynamicRHIModule* DynamicRHIModule = LoadDynamicRHIModule(RequestedFeatureLevel, LoadedRHIModuleName);

	if (DynamicRHIModule)
	{
		// Create the dynamic RHI.
		DynamicRHI = DynamicRHIModule->CreateRHI(RequestedFeatureLevel);
		GLoadedRHIModuleName = LoadedRHIModuleName;
	}

	return DynamicRHI;
}

const TCHAR* GetSelectedDynamicRHIModuleName(bool bCleanup)
{
	check(FApp::CanEverRender());

	if (GDynamicRHI)
	{
		check(!!GLoadedRHIModuleName);
		return GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1 ? TEXT("ES31") : GLoadedRHIModuleName;
	}
	else
	{
		ERHIFeatureLevel::Type DesiredFeatureLevel;
		const TCHAR* RHIModuleName;
		IDynamicRHIModule* DynamicRHIModule = LoadDynamicRHIModule(DesiredFeatureLevel, RHIModuleName);
		check(DynamicRHIModule);
		check(RHIModuleName);
		if (bCleanup)
		{
			FModuleManager::Get().UnloadModule(RHIModuleName);
		}

		return DesiredFeatureLevel == ERHIFeatureLevel::ES3_1 ? TEXT("ES31") : RHIModuleName;
	}
}

#endif //WINDOWS_USE_FEATURE_DYNAMIC_RHI
