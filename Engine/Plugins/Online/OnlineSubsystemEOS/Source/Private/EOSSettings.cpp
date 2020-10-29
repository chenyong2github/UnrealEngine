// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSSettings.h"
#include "OnlineSubsystemEOS.h"
#include "OnlineSubsystemEOSModule.h"

#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_EDITOR
	#include "Misc/MessageDialog.h"
#endif

#define LOCTEXT_NAMESPACE "EOS"

#define INI_SECTION TEXT("/Script/OnlineSubsystemEOS.EOSSettings")

inline bool IsAnsi(const FString& Source)
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

inline bool IsHex(const FString& Source)
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

inline bool ContainsWhitespace(const FString& Source)
{
	for (const TCHAR& IterChar : Source)
	{
		if (FChar::IsWhitespace(IterChar))
		{
			return true;
		}
	}
	return false;
}

FEOSArtifactSettings FArtifactSettings::ToNative() const
{
	FEOSArtifactSettings Native;

	Native.ArtifactName = ArtifactName;
	Native.ClientId = ClientId;
	Native.ClientSecret = ClientSecret;
	Native.DeploymentId = DeploymentId;
	Native.EncryptionKey = EncryptionKey;
	Native.ProductId = ProductId;
	Native.SandboxId = SandboxId;

	return Native;
}

inline FString StripQuotes(FString& Source)
{
	if (Source.StartsWith(TEXT("\"")))
	{
		return Source.Mid(1, Source.Len() - 2);
	}
	return Source;
}

void FEOSArtifactSettings::ParseRawArrayEntry(FString& RawLine)
{
	TCHAR* Delims[4] = { TEXT("("), TEXT(")"), TEXT("="), TEXT(",") };
	TArray<FString> Values;
	RawLine.ParseIntoArray(Values, Delims, 4, false);
	for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ValueIndex++)
	{
		if (Values[ValueIndex].IsEmpty())
		{
			continue;
		}

		// Parse which struct field
		if (Values[ValueIndex] == TEXT("ArtifactName"))
		{
			ArtifactName = StripQuotes(Values[ValueIndex + 1]);
		}
		else if (Values[ValueIndex] == TEXT("ClientId"))
		{
			ClientId = StripQuotes(Values[ValueIndex + 1]);
		}
		else if (Values[ValueIndex] == TEXT("ClientSecret"))
		{
			ClientSecret = StripQuotes(Values[ValueIndex + 1]);
		}
		else if (Values[ValueIndex] == TEXT("ProductId"))
		{
			ProductId = StripQuotes(Values[ValueIndex + 1]);
		}
		else if (Values[ValueIndex] == TEXT("SandboxId"))
		{
			SandboxId = StripQuotes(Values[ValueIndex + 1]);
		}
		else if (Values[ValueIndex] == TEXT("DeploymentId"))
		{
			DeploymentId = StripQuotes(Values[ValueIndex + 1]);
		}
		else if (Values[ValueIndex] == TEXT("EncryptionKey"))
		{
			EncryptionKey = StripQuotes(Values[ValueIndex + 1]);
		}
		ValueIndex++;
	}
}

UEOSSettings::UEOSSettings()
	: CacheDir(TEXT("CacheDir"))
{

}

FEOSSettings UEOSSettings::GetSettings()
{
	if (UObjectInitialized())
	{
		return UEOSSettings::AutoGetSettings();
	}
	return UEOSSettings::ManualGetSettings();
}

FEOSSettings UEOSSettings::AutoGetSettings()
{
	return GetDefault<UEOSSettings>()->ToNative();
}

FEOSSettings UEOSSettings::ManualGetSettings()
{
	FEOSSettings Native;

	GConfig->GetBool(INI_SECTION, TEXT("bEnableOverlay"), Native.bEnableOverlay, GEngineIni);
	GConfig->GetBool(INI_SECTION, TEXT("bEnableSocialOverlay"), Native.bEnableSocialOverlay, GEngineIni);
	GConfig->GetString(INI_SECTION, TEXT("CacheDir"), Native.CacheDir, GEngineIni);
	GConfig->GetString(INI_SECTION, TEXT("DefaultArtifactName"), Native.DefaultArtifactName, GEngineIni);
	GConfig->GetInt(INI_SECTION, TEXT("TickBudgetInMilliseconds"), Native.TickBudgetInMilliseconds, GEngineIni);

	return Native;
}

FEOSSettings UEOSSettings::ToNative() const
{
	FEOSSettings Native;

	Native.bEnableOverlay = bEnableOverlay;
	Native.bEnableSocialOverlay = bEnableSocialOverlay;
	Native.CacheDir = CacheDir;
	Native.DefaultArtifactName = DefaultArtifactName;
	Native.TickBudgetInMilliseconds = TickBudgetInMilliseconds;

	for (const FArtifactSettings& ArtifactSettings : Artifacts)
	{
		Native.Artifacts.Add(ArtifactSettings.ToNative());
	}

	return Native;
}

bool UEOSSettings::GetSettingsForArtifact(const FString& ArtifactName, FEOSArtifactSettings& OutSettings)
{
	if (UObjectInitialized())
	{
		return UEOSSettings::AutoGetSettingsForArtifact(ArtifactName, OutSettings);
	}
	return UEOSSettings::ManualGetSettingsForArtifact(ArtifactName, OutSettings);
}

bool UEOSSettings::ManualGetSettingsForArtifact(const FString& ArtifactName, FEOSArtifactSettings& OutSettings)
{
	FString DefaultArtifactName;
	GConfig->GetString(INI_SECTION, TEXT("DefaultArtifactName"), DefaultArtifactName, GEngineIni);

	TArray<FEOSArtifactSettings> ArtifactSettings;

	TArray<FString> Artifacts;
	GConfig->GetArray(INI_SECTION, TEXT("Artifacts"), Artifacts, GEngineIni);
	for (FString& Line : Artifacts)
	{
		FEOSArtifactSettings Artifact;
		Artifact.ParseRawArrayEntry(Line);
		ArtifactSettings.Add(Artifact);
	}

	FString ArtifactNameOverride;
	// Figure out which config object we are loading
	FParse::Value(FCommandLine::Get(), TEXT("EOSArtifactNameOverride="), ArtifactNameOverride);
	if (ArtifactNameOverride.IsEmpty())
	{
		ArtifactNameOverride = ArtifactName;
	}
	// Search by name and then default if not found
	for (FEOSArtifactSettings& Artifact : ArtifactSettings)
	{
		if (Artifact.ArtifactName == ArtifactNameOverride)
		{
			OutSettings = Artifact;
			return true;
		}
	}
	for (FEOSArtifactSettings& Artifact : ArtifactSettings)
	{
		if (Artifact.ArtifactName == DefaultArtifactName)
		{
			OutSettings = Artifact;
			return true;
		}
	}
	return false;
}

bool UEOSSettings::AutoGetSettingsForArtifact(const FString& ArtifactName, FEOSArtifactSettings& OutSettings)
{
	const UEOSSettings* This = GetDefault<UEOSSettings>();
	FString ArtifactNameOverride;
	// Figure out which config object we are loading
	FParse::Value(FCommandLine::Get(), TEXT("EOSArtifactNameOverride="), ArtifactNameOverride);
	if (ArtifactNameOverride.IsEmpty())
	{
		ArtifactNameOverride = ArtifactName;
	}
	for (const FArtifactSettings& Artifact : This->Artifacts)
	{
		if (Artifact.ArtifactName == ArtifactNameOverride)
		{
			OutSettings = Artifact.ToNative();
			return true;
		}
	}
	for (const FArtifactSettings& Artifact : This->Artifacts)
	{
		if (Artifact.ArtifactName == This->DefaultArtifactName)
		{
			OutSettings = Artifact.ToNative();
			return true;
		}
	}
	UE_LOG_ONLINE(Error, TEXT("UEOSSettings::AutoGetSettingsForArtifact() failed due to missing config object specified. Check your project settings"));
	return false;
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

	if (PropertyChangedEvent.MemberProperty != nullptr &&
		PropertyChangedEvent.MemberProperty->GetFName() == FName(TEXT("Artifacts")) &&
		(PropertyChangedEvent.ChangeType & EPropertyChangeType::ValueSet))
	{
		// Loop through all entries validating them
		for (FArtifactSettings& Artifact : Artifacts)
		{
			if (!Artifact.ClientId.IsEmpty())
			{
				if (!Artifact.ClientId.StartsWith(TEXT("xyz")))
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						LOCTEXT("ClientIdInvalidMsg", "Client ids created after SDK version 1.5 start with xyz. Double check that you did not use your BPT Client Id instead."));
				}
				if (!IsAnsi(Artifact.ClientId) || ContainsWhitespace(Artifact.ClientId))
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						LOCTEXT("ClientIdNotAnsiMsg", "Client ids must contain ANSI printable characters only with no whitespace"));
					Artifact.ClientId.Empty();
				}
			}

			if (!Artifact.ClientSecret.IsEmpty())
			{
				if (!IsAnsi(Artifact.ClientSecret) || ContainsWhitespace(Artifact.ClientSecret))
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						LOCTEXT("ClientSecretNotAnsiMsg", "ClientSecret must contain ANSI printable characters only with no whitespace"));
					Artifact.ClientSecret.Empty();
				}
			}

			if (!Artifact.EncryptionKey.IsEmpty())
			{
				if (!IsHex(Artifact.EncryptionKey) || Artifact.EncryptionKey.Len() != 64)
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						LOCTEXT("EncryptionKeyNotHexMsg", "EncryptionKey must contain 64 hex characters"));
					Artifact.EncryptionKey.Empty();
				}
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

#undef LOCTEXT_NAMESPACE
