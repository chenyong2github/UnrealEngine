// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSSettings.h"
#include "OnlineSubsystemEOS.h"
#include "OnlineSubsystemEOSModule.h"

#include "Misc/CommandLine.h"

#if WITH_EDITOR
	#include "Misc/MessageDialog.h"
#endif

#define LOCTEXT_NAMESPACE "EOS"

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

#if WITH_EDITOR
void UEOSSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property == nullptr)
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);
		return;
	}

	// Turning off the overlay in general turns off the social overlay too
	if (PropertyChangedEvent.Property->GetFName() == FName(TEXT("bEnableOverlay")))
	{
		if (!bEnableOverlay)
		{
			bEnableSocialOverlay = false;
		}
	}

	// Turning on the social overlay requires the base overlay too
	if (PropertyChangedEvent.Property->GetFName() == FName(TEXT("bEnableSocialOverlay")))
	{
		if (bEnableSocialOverlay)
		{
			bEnableOverlay = true;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool IsAnsi(const FString& Source)
{
	for (const TCHAR& IterChar : Source)
	{
		if (!FChar::IsPrint(IterChar))
		{
			return false;
		}
	}
	return true;
}

bool IsHex(const FString& Source)
{
	for (const TCHAR& IterChar : Source)
	{
		if (!FChar::IsHexDigit(IterChar))
		{
			return false;
		}
	}
	return true;
}

void UEOSArtifactSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property == nullptr)
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);
		return;
	}

	if (PropertyChangedEvent.Property->GetFName() == FName(TEXT("ClientId")))
	{
		if (!ClientId.StartsWith(TEXT("xyz")))
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("ClientIdInvalidMsg", "Client ids created after SDK version 1.5 start with xyz. Double check that you did not use your BPT Client Id instead."));
		}
		if (!IsAnsi(ClientId))
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("ClientIdNotAnsiMsg", "Client ids must contain ANSI printable characters only"));
			ClientId.Empty();
		}
	}

	if (PropertyChangedEvent.Property->GetFName() == FName(TEXT("ClientSecret")))
	{
		if (!IsAnsi(ClientSecret))
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("ClientSecretNotAnsiMsg", "ClientSecret must contain ANSI printable characters only"));
			ClientSecret.Empty();
		}
	}

	if (PropertyChangedEvent.Property->GetFName() == FName(TEXT("EncryptionKey")))
	{
		if (!IsHex(EncryptionKey) || EncryptionKey.Len() != 64)
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("EncryptionKeyNotHexMsg", "EncryptionKey must contain 64 hex characters"));
			EncryptionKey.Empty();
		}
	}
	
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

#undef LOCTEXT_NAMESPACE
