// Copyright Epic Games, Inc. All Rights Reserved.


#include "RHI.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include COMPILED_PLATFORM_HEADER_WITH_PREFIX(Apple/Platform, PlatformDynamicRHI.h)


//------------------------------------------------------------------------------
// MARK: - FAppleDynamicRHIOptions Union
//

union FAppleDynamicRHIOptions
{
	struct
	{
		uint16 PreferAGX                : 1;
		uint16 ForceSM5                 : 1;
		uint16 ForceSM6                 : 1;
		uint16 PreferES31               : 1;
		uint16 AllowAGXFeatureLevelES31 : 1;
		uint16 ForceMTL                 : 1;
		uint16 ForceAGX                 : 1;
		uint16 UnusedReservedBits       : 9;
	};

	uint16 All;
};


//------------------------------------------------------------------------------
// MARK: - Apple Dynamic RHI Support Routines
//

static bool ShouldPreferAGX()
{
	// TODO: Add logic here to detect Apple GPUs and set this preference.
	return false;
}

static bool ShouldAllowAGXFeatureLevelES31()
{
	return true;
}

static inline bool ValidateAppleDynamicRHIOptions(FAppleDynamicRHIOptions* Options)
{
	if (0 != (Options->ForceMTL & Options->ForceAGX))
	{
		UE_LOG(LogRHI, Fatal, TEXT("-mtl and -agx are mutually exclusive options but more than one was specified on the command line."));
		return false;
	}
	if (0 != (Options->ForceSM5 & Options->ForceSM6))
	{
		UE_LOG(LogRHI, Fatal, TEXT("-sm5 and -sm6 are mutually exclusive options but more than one was specified on the command line."));
		return false;
	}
	if (0 != (Options->ForceMTL & Options->ForceSM6))
	{
		UE_LOG(LogRHI, Warning, TEXT("-mtl and -sm6 are incompatible options, using MetalRHI with SM5."));
		Options->ForceSM5 = 1;
		Options->ForceSM6 = 0;
		Options->ForceMTL = 1;
		Options->ForceAGX = 0;
	}
	if (0 != Options->ForceSM6)
	{
		Options->ForceMTL = 0;
		Options->ForceAGX = 1;
	}
	return true;
}

static bool InitAppleDynamicRHIOptions(FAppleDynamicRHIOptions* Options)
{
	Options->PreferAGX                = ShouldPreferAGX();
	Options->ForceSM5                 = FParse::Param(FCommandLine::Get(), TEXT("sm5"));
	Options->ForceSM6                 = FParse::Param(FCommandLine::Get(), TEXT("sm6"));
	Options->PreferES31               = FPlatformDynamicRHI::ShouldPreferFeatureLevelES31() && !(Options->ForceSM5 || Options->ForceSM6);
	Options->AllowAGXFeatureLevelES31 = ShouldAllowAGXFeatureLevelES31();
	Options->ForceMTL                 = FParse::Param(FCommandLine::Get(), TEXT("mtl"));
	Options->ForceAGX                 = FParse::Param(FCommandLine::Get(), TEXT("agx")) && (!Options->PreferES31 || Options->AllowAGXFeatureLevelES31);

	return ValidateAppleDynamicRHIOptions(Options);
}

static inline bool ShouldUseShaderModelPreference(const FAppleDynamicRHIOptions& Options)
{
	return (0 != (Options.ForceSM5 | Options.ForceSM6 | Options.PreferES31));
}

static void ComputeRequestedFeatureLevel(const FAppleDynamicRHIOptions& Options, ERHIFeatureLevel::Type& RequestedFeatureLevel)
{
	if (!ShouldUseShaderModelPreference(Options))
	{
		TArray<FString> TargetedShaderFormats;

		FPlatformDynamicRHI::AddTargetedShaderFormats(TargetedShaderFormats);

		if (TargetedShaderFormats.Num() > 0)
		{
			// Pick the first one
			FName ShaderFormatName(*TargetedShaderFormats[0]);
			EShaderPlatform TargetedPlatform = ShaderFormatToLegacyShaderPlatform(ShaderFormatName);
			RequestedFeatureLevel = GetMaxSupportedFeatureLevel(TargetedPlatform);
		}
	}
	else
	{
		if (Options.ForceSM6)
		{
			RequestedFeatureLevel = ERHIFeatureLevel::SM6;
		}
		else if (Options.ForceSM5)
		{
			RequestedFeatureLevel = ERHIFeatureLevel::SM5;
		}
		else
		{
			check(Options.PreferES31 != 0);
			RequestedFeatureLevel = ERHIFeatureLevel::ES3_1;
		}
	}

	check(RequestedFeatureLevel != ERHIFeatureLevel::Num);
}

static inline bool ShouldUseAGX(const FAppleDynamicRHIOptions& Options)
{
	return (0 != (Options.ForceAGX | (Options.PreferAGX & ~Options.ForceMTL)));
}

static IDynamicRHIModule* LoadDynamicRHIModule(ERHIFeatureLevel::Type& RequestedFeatureLevel)
{
	IDynamicRHIModule*      DynamicRHIModule = nullptr;
	FAppleDynamicRHIOptions Options          = { .All = 0 };

	if (!InitAppleDynamicRHIOptions(&Options))
	{
		return nullptr;
	}

	ComputeRequestedFeatureLevel(Options, RequestedFeatureLevel);

	if (ShouldUseAGX(Options))
	{
		FApp::SetGraphicsRHI(TEXT("AGX"));
		const TCHAR* AGXRHIModuleName = TEXT("AGXRHI");
		DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(AGXRHIModuleName);
		if (!DynamicRHIModule || !DynamicRHIModule->IsSupported())
		{
			if (Options.ForceAGX)
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("AppleDynamicRHI", "RequiredAGX", "The AGX RHI is not supported on your system. Try running without the -agx command line argument."));
				FPlatformMisc::RequestExit(1);
			}
			if (DynamicRHIModule)
			{
				FModuleManager::Get().UnloadModule(AGXRHIModuleName);
			}
			DynamicRHIModule = nullptr;
		}
	}

	if (DynamicRHIModule == nullptr)
	{
		FApp::SetGraphicsRHI(TEXT("Metal"));
		DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(TEXT("MetalRHI"));
	}

	return DynamicRHIModule;
}


//------------------------------------------------------------------------------
// MARK: - Dynamic RHI API
//

FDynamicRHI* PlatformCreateDynamicRHI()
{
	FDynamicRHI*           DynamicRHI            = nullptr;
	IDynamicRHIModule*     DynamicRHIModule      = nullptr;
	ERHIFeatureLevel::Type RequestedFeatureLevel = ERHIFeatureLevel::Num;

	if (nullptr != (DynamicRHIModule = LoadDynamicRHIModule(RequestedFeatureLevel)))
	{
		DynamicRHI = DynamicRHIModule->CreateRHI(RequestedFeatureLevel);
	}

	return DynamicRHI;
}
