// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHI.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"

int32 GAppleMetalEnabled = 1;
static FAutoConsoleVariableRef CVarMacMetalEnabled(
	TEXT("rhi.Apple.UseMetal"),
	GAppleMetalEnabled,
	TEXT("If set to true uses Metal when available rather than OpenGL as the graphics API. (Default: True)"));

int32 GAppleOpenGLDisabled = 0;
static FAutoConsoleVariableRef CVarMacOpenGLDisabled(
	TEXT("rhi.Apple.OpenGLDisabled"),
	GAppleOpenGLDisabled,
	TEXT("If set, OpenGL RHI will not be used if Metal is not available. Instead, a dialog box explaining that the hardware requirements are not met will appear. (Default: False)"));

FDynamicRHI* PlatformCreateDynamicRHI()
{
	SCOPED_AUTORELEASE_POOL;

	FDynamicRHI* DynamicRHI = NULL;
	IDynamicRHIModule* DynamicRHIModule = NULL;

	bool const bIsMetalSupported = FPlatformMisc::HasPlatformFeature(TEXT("Metal"));
	
	// Must be Metal!
	if(!bIsMetalSupported)
	{
		FText Title = NSLOCTEXT("AppleDynamicRHI", "OpenGLNotSupportedTitle","Metal Not Supported");
#if PLATFORM_MAC
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("MacPlatformCreateDynamicRHI", "OpenGLNotSupported.", "You must have a Metal compatible graphics card and be running Mac OS X 10.11.6 or later to launch this process."), &Title);
#else
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("AppleDynamicRHI", "OpenGLNotSupported.", "You must have a Metal compatible iOS or tvOS device with iOS 8 or later to launch this app."), &Title);
#endif
		FPlatformMisc::RequestExit(true);
	}
	
	if (FParse::Param(FCommandLine::Get(),TEXT("opengl")))
	{
		UE_LOG(LogRHI, Log, TEXT("OpenGL command line option ignored; Apple platforms only support Metal."));
	}

	ERHIFeatureLevel::Type RequestedFeatureLevel = ERHIFeatureLevel::Num;
	{
		// Check the list of targeted shader platforms and decide an RHI based off them
		TArray<FString> TargetedShaderFormats;
#if PLATFORM_MAC
		GConfig->GetArray(TEXT("/Script/MacTargetPlatform.MacTargetSettings"), TEXT("TargetedRHIs"), TargetedShaderFormats, GEngineIni);
#else
		bool bSupportsMetalMRT = false;
		GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bSupportsMetalMRT"), bSupportsMetalMRT, GEngineIni);
		if (bSupportsMetalMRT)
		{
#if PLATFORM_TVOS
			TargetedShaderFormats.Add(LegacyShaderPlatformToShaderFormat(SP_METAL_MRT_TVOS).ToString());
#else
			TargetedShaderFormats.Add(LegacyShaderPlatformToShaderFormat(SP_METAL_MRT).ToString());
#endif
		}
		
#if PLATFORM_TVOS
		TargetedShaderFormats.Add(LegacyShaderPlatformToShaderFormat(SP_METAL_TVOS).ToString());
#else
		TargetedShaderFormats.Add(LegacyShaderPlatformToShaderFormat(SP_METAL).ToString());
#endif
		
#endif // else branch of PLATFORM_MAC
		
		// Metal is not always available, so don't assume that we can use the first platform
		for (FString Name : TargetedShaderFormats)
		{
			FName ShaderFormatName(*Name);
			EShaderPlatform TargetedPlatform = ShaderFormatToLegacyShaderPlatform(ShaderFormatName);
			
			// Instead use the first platform that *could* work
			if (IsMetalPlatform(TargetedPlatform))
			{
				RequestedFeatureLevel = GetMaxSupportedFeatureLevel(TargetedPlatform);
				break;
			}
		}
	}

	// Load the dynamic RHI module.
	{
		DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(TEXT("MetalRHI"));
		
		{
#if PLATFORM_MAC
			if (FParse::Param(FCommandLine::Get(),TEXT("metal")))
			{
				RequestedFeatureLevel = ERHIFeatureLevel::SM5;
			}
			else if (FParse::Param(FCommandLine::Get(),TEXT("metalsm5")) || FParse::Param(FCommandLine::Get(),TEXT("metalmrt")))
			{
				RequestedFeatureLevel = ERHIFeatureLevel::SM5;
			}
#else
			if (FParse::Param(FCommandLine::Get(),TEXT("metal")))
			{
				RequestedFeatureLevel = ERHIFeatureLevel::ES3_1;
			}
			else if (FParse::Param(FCommandLine::Get(),TEXT("metalmrt")))
			{
				RequestedFeatureLevel = ERHIFeatureLevel::SM5;
			}
#endif
		}
		FApp::SetGraphicsRHI(TEXT("Metal"));
	}
	
	// Create the dynamic RHI.
	DynamicRHI = DynamicRHIModule->CreateRHI(RequestedFeatureLevel);
	return DynamicRHI;
}
