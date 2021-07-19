// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHI.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"

#if WINDOWS_USE_FEATURE_DYNAMIC_RHI

#include "Windows/WindowsPlatformApplicationMisc.h"

static const TCHAR* GLoadedRHIModuleName;

static bool ShouldPreferD3D12()
{
	if (!GIsEditor)
	{
		bool bPreferD3D12 = false;
		if (GConfig->GetBool(TEXT("D3DRHIPreference"), TEXT("bUseD3D12InGame"), bPreferD3D12, GGameUserSettingsIni))
		{
			return bPreferD3D12;
		}
	}
	
	return false;
}

static bool ShouldForceFeatureLevelES31()
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

static bool ShouldPreferFeatureLevelES31()
{
	if (!GIsEditor)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES31")) || FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES3_1")))
		{
			return true;
		}

		bool bPreferFeatureLevelES31 = false;
		bool bFoundPreference = GConfig->GetBool(TEXT("D3DRHIPreference"), TEXT("bPreferFeatureLevelES31"), bPreferFeatureLevelES31, GGameUserSettingsIni);

		// Force low-spec users into performance mode but respect their choice once they have set a preference
		bool bForceES31 = false;
		if (!bFoundPreference)
		{
			bForceES31 = ShouldForceFeatureLevelES31();
		}

		if (bPreferFeatureLevelES31 || bForceES31)
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

static bool ShouldAllowD3D12FeatureLevelES31()
{
	if (!GIsEditor)
	{
		bool bAllowD3D12FeatureLevelES31 = true;
		GConfig->GetBool(TEXT("SystemSettings"), TEXT("bAllowD3D12FeatureLevelES31"), bAllowD3D12FeatureLevelES31, GEngineIni);
		return bAllowD3D12FeatureLevelES31;
	}
	return true;
}

static IDynamicRHIModule* LoadDynamicRHIModule(ERHIFeatureLevel::Type& DesiredFeatureLevel, const TCHAR*& LoadedRHIModuleName)
{
	bool bUseGPUCrashDebugging = false;
	if (!GIsEditor && GConfig->GetBool(TEXT("D3DRHIPreference"), TEXT("bUseGPUCrashDebugging"), bUseGPUCrashDebugging, GGameUserSettingsIni))
	{
		auto GPUCrashDebuggingCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GPUCrashDebugging"));
		*GPUCrashDebuggingCVar = bUseGPUCrashDebugging;
	}

	bool bPreferD3D12 = ShouldPreferD3D12();
	
	// command line overrides
	bool bForceOpenGL = FParse::Param(FCommandLine::Get(), TEXT("opengl"));
	if (bForceOpenGL)
	{
		// OpenGL can only be used for mobile preview.
		ERHIFeatureLevel::Type PreviewFeatureLevel;
		bool bUsePreviewFeatureLevel = RHIGetPreviewFeatureLevel(PreviewFeatureLevel);
		if (!bUsePreviewFeatureLevel)
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "OpenGLRemoved", "Warning: OpenGL is no longer supported for desktop platforms. The default RHI will be used."));
			bForceOpenGL = false;
		}
	}

	bool bForceSM5 = FParse::Param(FCommandLine::Get(), TEXT("sm5"));
	bool bPreferES31 = ShouldPreferFeatureLevelES31() && !bForceSM5;
	bool bAllowD3D12FeatureLevelES31 = ShouldAllowD3D12FeatureLevelES31();
	bool bForceVulkan = FParse::Param(FCommandLine::Get(), TEXT("vulkan"));
	bool bForceD3D11 = FParse::Param(FCommandLine::Get(), TEXT("d3d11")) || FParse::Param(FCommandLine::Get(), TEXT("dx11")) || ((bForceSM5 || (bPreferES31 && !bAllowD3D12FeatureLevelES31)) && !bForceVulkan && !bForceOpenGL);
	bool bForceD3D12 = (FParse::Param(FCommandLine::Get(), TEXT("d3d12")) || FParse::Param(FCommandLine::Get(), TEXT("dx12"))) && (!bPreferES31 || bAllowD3D12FeatureLevelES31);
	DesiredFeatureLevel = ERHIFeatureLevel::Num;
	
	if(!(bForceVulkan||bForceOpenGL||bForceD3D11||bForceD3D12))
	{
		//Default graphics RHI is only used if no command line option is specified
		FConfigFile EngineSettings;
		FString PlatformNameString = FPlatformProperties::PlatformName();
		const TCHAR* PlatformName = *PlatformNameString;
		FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, PlatformName);
		FString DefaultGraphicsRHI;
		if(EngineSettings.GetString(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("DefaultGraphicsRHI"), DefaultGraphicsRHI))
		{
			FString NAME_DX11(TEXT("DefaultGraphicsRHI_DX11"));
			FString NAME_DX12(TEXT("DefaultGraphicsRHI_DX12"));
			FString NAME_VULKAN(TEXT("DefaultGraphicsRHI_Vulkan"));
			if(DefaultGraphicsRHI == NAME_DX11)
			{
				bForceD3D11 = true;
			}
			else if (DefaultGraphicsRHI == NAME_DX12)
			{
				bForceD3D12 = true;
			}
			else if (DefaultGraphicsRHI == NAME_VULKAN)
			{
				bForceVulkan = true;
			}
		}
	}



	int32 Sum = ((bForceD3D12 ? 1 : 0) + (bForceD3D11 ? 1 : 0) + (bForceOpenGL ? 1 : 0) + (bForceVulkan ? 1 : 0));

	if (Sum > 1)
	{
		UE_LOG(LogRHI, Fatal, TEXT("-d3d12, -d3d11, -vulkan, and -opengl are mutually exclusive options, but more than one was specified on the command-line."));
	}
	else if (Sum == 0)
	{
		// Check the list of targeted shader platforms and decide an RHI based off them
		TArray<FString> TargetedShaderFormats;
		GConfig->GetArray(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("TargetedRHIs"), TargetedShaderFormats, GEngineIni);
		if (TargetedShaderFormats.Num() > 0)
		{
			// Pick the first one
			FName ShaderFormatName(*TargetedShaderFormats[0]);
			EShaderPlatform TargetedPlatform = ShaderFormatToLegacyShaderPlatform(ShaderFormatName);
			bForceVulkan = IsVulkanPlatform(TargetedPlatform);
			bForceD3D11 = !bPreferD3D12 && IsD3DPlatform(TargetedPlatform);
			bForceOpenGL = IsOpenGLPlatform(TargetedPlatform);
			if (bPreferES31)
			{
				DesiredFeatureLevel = ERHIFeatureLevel::ES3_1;
			}
			else
			{
				DesiredFeatureLevel = GetMaxSupportedFeatureLevel(TargetedPlatform);
			}
		}
	}
	else
	{
		if (bForceSM5)
		{
			DesiredFeatureLevel = ERHIFeatureLevel::SM5;
		}
		else if (bPreferES31)
		{
			DesiredFeatureLevel = ERHIFeatureLevel::ES3_1;
		}
	}

	// Load the dynamic RHI module.
	IDynamicRHIModule* DynamicRHIModule = NULL;

#if defined(SWITCHRHI)
	const bool bForceSwitch = FParse::Param(FCommandLine::Get(), TEXT("switch"));
	// Load the dynamic RHI module.
	if (bForceSwitch)
	{
#define A(x) #x
#define B(x) A(x)
#define SWITCH_RHI_STR B(SWITCHRHI)
		FApp::SetGraphicsRHI(TEXT("Switch"));
		const TCHAR* SwitchRHIModuleName = TEXT(SWITCH_RHI_STR);
		DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(SwitchRHIModuleName);
		if (!DynamicRHIModule->IsSupported())
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("SwitchDynamicRHI", "UnsupportedRHI", "The chosen RHI is not supported"));
			FPlatformMisc::RequestExit(1);
			DynamicRHIModule = NULL;
		}
		LoadedRHIModuleName = SwitchRHIModuleName;
	}
	else
#endif

	if (bForceOpenGL)
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
	else if (bForceVulkan)
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
	else if (bForceD3D12 || (bPreferD3D12 && !bForceD3D11))
	{
		FApp::SetGraphicsRHI(TEXT("DirectX 12"));
		LoadedRHIModuleName = TEXT("D3D12RHI");
		DynamicRHIModule = FModuleManager::LoadModulePtr<IDynamicRHIModule>(LoadedRHIModuleName);

		if (!DynamicRHIModule || !DynamicRHIModule->IsSupported())
		{
			if (bForceD3D12)
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
		else
		{
#if !WITH_EDITOR
			// Enable -psocache by default on DX12. Since RHI is selected at runtime we can't set this at compile time with PIPELINE_CACHE_DEFAULT_ENABLED.
			auto PSOFileCacheEnabledCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ShaderPipelineCache.Enabled"));
			*PSOFileCacheEnabledCVar = 1;

			auto PSOFileCacheReportCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ShaderPipelineCache.ReportPSO"));
			*PSOFileCacheReportCVar = 1;

			auto PSOFileCacheUserCacheCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ShaderPipelineCache.SaveUserCache"));
			*PSOFileCacheUserCacheCVar = UE_BUILD_SHIPPING;
#endif

			if (FPlatformProcess::IsApplicationRunning(TEXT("fraps.exe")))
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "UseExpressionEncoder", "Fraps has been known to crash D3D12. Please use Microsoft Expression Encoder instead for capturing."));
			}
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
		else if (FPlatformProcess::IsApplicationRunning(TEXT("fraps.exe")))
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "UseExpressionEncoderDX11", "Fraps has been known to crash D3D11. Please use Microsoft Expression Encoder instead for capturing."));
		}
		LoadedRHIModuleName = D3D11RHIModuleName;
	}
	return DynamicRHIModule;
}

FDynamicRHI* PlatformCreateDynamicRHI()
{
	FDynamicRHI* DynamicRHI = nullptr;

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	if (!FPlatformMisc::IsDebuggerPresent())
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("AttachDebugger")))
		{
			// Wait to attach debugger
			do
			{
				FPlatformProcess::Sleep(0);
			}
			while (!FPlatformMisc::IsDebuggerPresent());
		}
	}
#endif

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
	if (ShouldPreferFeatureLevelES31())
	{
		return TEXT("ES31");
	}
	else if (GDynamicRHI)
	{
		check(!!GLoadedRHIModuleName);
		return GLoadedRHIModuleName;
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
		return RHIModuleName;
	}
}

#endif //WINDOWS_USE_FEATURE_DYNAMIC_RHI
