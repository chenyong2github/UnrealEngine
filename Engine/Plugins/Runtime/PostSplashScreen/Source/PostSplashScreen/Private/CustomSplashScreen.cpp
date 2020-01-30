// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomSplashScreen.h"

#include "PreLoadScreen.h"
#include "PreLoadSettingsContainer.h"

#include "Misc/ConfigCacheIni.h"


#define LOCTEXT_NAMESPACE "CustomSplashScreen"

#define CONFIG_BASENAME TEXT("PostSplashScreen")
FString GPostSplashScreenIni;

void FCustomSplashScreen::Init()
{
	FPreLoadScreenBase::Init();
	
	SetPluginName("PostSplashScreen");
	InitSettingsFromConfig("PostSplashScreen");

	SAssignNew(SplashScreenWidget, SCustomSplashScreenWidget);

	FPreLoadSettingsContainerBase::Get().LoadGrouping(TEXT("PostSplashScreen"));

	TimeElapsed = 0.f;
	MaxTimeToDisplay = 0.f;
	FConfigCacheIni::LoadGlobalIniFile(GPostSplashScreenIni, CONFIG_BASENAME);
	GConfig->GetFloat(TEXT("PreLoadScreen.UISettings"), TEXT("PostSplashScreenLength"), MaxTimeToDisplay, GPostSplashScreenIni);
}

void FCustomSplashScreen::Tick(float DeltaTime)
{
	FPreLoadScreenBase::Tick(DeltaTime);

	TimeElapsed += DeltaTime;
}

bool FCustomSplashScreen::IsDone() const
{
	return TimeElapsed >= MaxTimeToDisplay;
}

float FCustomSplashScreen::GetAddedTickDelay()
{
	const float DefaultTickTime = 0.03f;
	return FMath::Min(MaxTimeToDisplay, DefaultTickTime);
}

#undef LOCTEXT_NAMESPACE
