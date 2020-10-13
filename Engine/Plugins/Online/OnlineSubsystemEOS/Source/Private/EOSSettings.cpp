// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSSettings.h"
#include "OnlineSubsystemEOS.h"
#include "OnlineSubsystemEOSModule.h"

UEOSSettings::UEOSSettings()
	: CacheDir(TEXT("CacheDir"))
{

}

void UEOSSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Works around the fact that PostEngineInit core delegate is too late
	// and you can't rely on OnInit core delegate because the order of firing
	// is not something you can predict that CoreUObjectModule was handled
	// before you, so rely on the default object init to trigger this
	// This dirtiness would not be needed if there was a PreEngineInit core delegate
	FOnlineSubsystemEOSModule::PossiblyDeferredInit();
}

const UEOSArtifactSettings* UEOSSettings::GetSettingsForArtifact(const FString& ArtifactName) const
{
	FString ObjectToLoad;
	// Figure out which config object we are loading
	FParse::Value(FCommandLine::Get(), TEXT("EOSSettingsOverride="), ObjectToLoad);
	if (ObjectToLoad.IsEmpty())
	{
		for (const FArtifactLink& ArtifactLink : ArtifactObjects)
		{
			if (ArtifactLink.ArtifactName == ArtifactName)
			{
				ObjectToLoad = ArtifactLink.SettingsObjectName;
				break;
			}
		}
		if (ObjectToLoad.IsEmpty())
		{
			// If we are here, try the default name
			for (const FArtifactLink& ArtifactLink : ArtifactObjects)
			{
				if (ArtifactLink.ArtifactName == DefaultArtifactName)
				{
					ObjectToLoad = ArtifactLink.SettingsObjectName;
					break;
				}
			}
		}
	}
	if (ObjectToLoad.IsEmpty())
	{
		UE_LOG_ONLINE(Error, TEXT("UEOSSettings::GetSettingsForArtifact() failed due to missing config object specified. Check your project settings"));
		return nullptr;
	}
	// Load the object so we can get the artifact specific settings out of it
	UEOSArtifactSettings* SettingsObj = LoadObject<UEOSArtifactSettings>(nullptr, *(TEXT("/Game/") + ObjectToLoad), nullptr, LOAD_None, nullptr);
	if (SettingsObj == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("UEOSSettings::GetSettingsForArtifact() failed to load settings object (%s)"), *ObjectToLoad);
		return nullptr;
	}
	return SettingsObj;
}

