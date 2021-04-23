// Copyright Epic Games, Inc. All Rights Reserved.

#include "OculusHMDRuntimeSettings.h"

//////////////////////////////////////////////////////////////////////////
// UOculusHMDRuntimeSettings

#include "OculusHMD_Settings.h"

UOculusHMDRuntimeSettings::UOculusHMDRuntimeSettings(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, bAutoEnabled(true)
{
#if OCULUS_HMD_SUPPORTED_PLATFORMS
	// FSettings is the sole source of truth for Oculus default settings
	OculusHMD::FSettings DefaultSettings; 
	bSupportsDash = DefaultSettings.Flags.bSupportsDash;
	bCompositesDepth = DefaultSettings.Flags.bCompositeDepth;
	bHQDistortion = DefaultSettings.Flags.bHQDistortion;
	bChromaCorrection = DefaultSettings.Flags.bChromaAbCorrectionEnabled;
	FFRLevel = DefaultSettings.FFRLevel;
	FFRDynamic = DefaultSettings.FFRDynamic;
	CPULevel = DefaultSettings.CPULevel;
	GPULevel = DefaultSettings.GPULevel;
	PixelDensityMin = DefaultSettings.PixelDensityMin;
	PixelDensityMax = DefaultSettings.PixelDensityMax;
	bRecenterHMDWithController = DefaultSettings.Flags.bRecenterHMDWithController;
	bFocusAware = DefaultSettings.Flags.bFocusAware;
	bLateLatching = DefaultSettings.bLateLatching;
	bPhaseSync = DefaultSettings.bPhaseSync;
	bEnableSpecificColorGamut = DefaultSettings.bEnableSpecificColorGamut;
	ColorSpace = DefaultSettings.ColorSpace;
	bRequiresSystemKeyboard = DefaultSettings.Flags.bRequiresSystemKeyboard;
	HandTrackingSupport = DefaultSettings.HandTrackingSupport;

#else
	// Some set of reasonable defaults, since blueprints are still available on non-Oculus platforms.
	bSupportsDash = false;
	bCompositesDepth = false;
	bHQDistortion = false;
	bChromaCorrection = false;
	FFRLevel = EFixedFoveatedRenderingLevel::EFixedFoveatedRenderingLevel_Off;
	FFRDynamic = false;
	CPULevel = 2;
	GPULevel = 3;
	PixelDensityMin = 0.5f;
	PixelDensityMax = 1.0f;
	bRecenterHMDWithController = true;
	bFocusAware = true;
	bLateLatching = false;
	bPhaseSync = false;
	bEnableSpecificColorGamut = false;
	ColorSpace = EColorSpace::Unknown;
	bRequiresSystemKeyboard = false;
	HandTrackingSupport = EHandTrackingSupport::ControllersOnly;
#endif

	LoadFromIni();
}

void UOculusHMDRuntimeSettings::LoadFromIni()
{
	const TCHAR* OculusSettings = TEXT("Oculus.Settings");
	bool v;
	float f;
	FVector vec;

	if (GConfig->GetFloat(OculusSettings, TEXT("PixelDensityMax"), f, GEngineIni))
	{
		check(!FMath::IsNaN(f));
		PixelDensityMax = f;
	}
	if (GConfig->GetFloat(OculusSettings, TEXT("PixelDensityMin"), f, GEngineIni))
	{
		check(!FMath::IsNaN(f));
		PixelDensityMin = f;
	}
	if (GConfig->GetBool(OculusSettings, TEXT("bHQDistortion"), v, GEngineIni))
	{
		bHQDistortion = v;
	}
	if (GConfig->GetBool(OculusSettings, TEXT("bCompositeDepth"), v, GEngineIni))
	{
		bCompositesDepth = v;
	}
}