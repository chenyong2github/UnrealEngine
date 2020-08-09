// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/Docking/LayoutService.h"
#include "Misc/ConfigCacheIni.h"


const TCHAR* EditorLayoutsSectionName = TEXT("EditorLayouts");


const FString& FLayoutSaveRestore::GetAdditionalLayoutConfigIni()
{
	static const FString IniSectionAdditionalConfig = TEXT("SlateAdditionalLayoutConfig");
	return IniSectionAdditionalConfig;
}


void FLayoutSaveRestore::SaveToConfig( const FString& ConfigFileName, const TSharedRef<FTabManager::FLayout>& LayoutToSave )
{
	// Only save to config if it's not the FTabManager::FLayout::NullLayout
	if (LayoutToSave->GetLayoutName() != FTabManager::FLayout::NullLayout->GetLayoutName())
	{
		const FString LayoutAsString = FLayoutSaveRestore::PrepareLayoutStringForIni(LayoutToSave->ToString());
		GConfig->SetString(EditorLayoutsSectionName, *LayoutToSave->GetLayoutName().ToString(), *LayoutAsString, ConfigFileName);
	}
}


TSharedRef<FTabManager::FLayout> FLayoutSaveRestore::LoadFromConfig(const FString& ConfigFileName, const TSharedRef<FTabManager::FLayout>& DefaultLayout,
	const EOutputCanBeNullptr PrimaryAreaOutputCanBeNullptr)
{
	TArray<FString> DummyArray;
	return FLayoutSaveRestore::LoadFromConfig(ConfigFileName, DefaultLayout, PrimaryAreaOutputCanBeNullptr, false, DummyArray);
}


TSharedRef<FTabManager::FLayout> FLayoutSaveRestore::LoadFromConfig(const FString& ConfigFileName, const TSharedRef<FTabManager::FLayout>& DefaultLayout,
	const EOutputCanBeNullptr PrimaryAreaOutputCanBeNullptr, TArray<FString>& OutRemovedOlderLayoutVersions)
{
	return FLayoutSaveRestore::LoadFromConfig(ConfigFileName, DefaultLayout, PrimaryAreaOutputCanBeNullptr, true, OutRemovedOlderLayoutVersions);
}


TSharedRef<FTabManager::FLayout> FLayoutSaveRestore::LoadFromConfig(const FString& ConfigFileName, const TSharedRef<FTabManager::FLayout>& DefaultLayout,
	const EOutputCanBeNullptr PrimaryAreaOutputCanBeNullptr, const bool bRemoveOlderLayoutVersions, TArray<FString>& OutRemovedOlderLayoutVersions)
{
	const FName LayoutName = DefaultLayout->GetLayoutName();
	const FString LayoutNameString = LayoutName.ToString();
	FString UserLayoutString;
	// If the Key (LayoutName) already exists in the section EditorLayoutsSectionName of the file ConfigFileName, try to load the layout from that file
	if ( GConfig->GetString(EditorLayoutsSectionName, *LayoutNameString, UserLayoutString, ConfigFileName ) && !UserLayoutString.IsEmpty() )
	{
		TSharedPtr<FTabManager::FLayout> UserLayout = FTabManager::FLayout::NewFromString( FLayoutSaveRestore::GetLayoutStringFromIni( UserLayoutString ));
		if ( UserLayout.IsValid() && UserLayout->GetPrimaryArea().IsValid() )
		{
			// Return UserLayout in the following 2 cases:
			// - By default (PrimaryAreaOutputCanBeNullptr = Never or IfNoTabValid)
			// - For the case of PrimaryAreaOutputCanBeNullptr = IfNoOpenTabValid, only if the primary area has at least a valid open tab
			if (PrimaryAreaOutputCanBeNullptr != EOutputCanBeNullptr::IfNoOpenTabValid || FGlobalTabmanager::Get()->HasValidOpenTabs(UserLayout->GetPrimaryArea().Pin().ToSharedRef()))
			{
				return UserLayout.ToSharedRef();
			}
		}
	}
	// If the file layout could not be loaded and the caller wants to remove old fields
	else if (bRemoveOlderLayoutVersions)
	{
		// If File and Section exist
		if (FConfigSection* ConfigSection = GConfig->GetSectionPrivate(EditorLayoutsSectionName, /*Force*/false, /*Const*/true, ConfigFileName))
		{
			// If Key does not exist (i.e., Section does but not contain that Key)
			if (!ConfigSection->Find(*LayoutNameString))
			{
				// Create LayoutKeyToRemove
				FString LayoutKeyToRemove;
				for (int32 Index = LayoutNameString.Len() - 1; Index > 0; --Index)
				{
					if (LayoutNameString[Index] != TCHAR('.') && (LayoutNameString[Index] < TCHAR('0') || LayoutNameString[Index] > TCHAR('9')))
					{
						LayoutKeyToRemove = LayoutNameString.Left(Index+1);
						break;
					}
				}
				// Look for older versions of this Key
				OutRemovedOlderLayoutVersions.Empty();
				for (const auto& SectionPair : *ConfigSection/*->ArrayOfStructKeys*/)
				{
					FString CurrentKey = SectionPair.Key.ToString();
					if (CurrentKey.Len() > LayoutKeyToRemove.Len() && CurrentKey.Left(LayoutKeyToRemove.Len()) == LayoutKeyToRemove)
					{
						OutRemovedOlderLayoutVersions.Emplace(std::move(CurrentKey));
					}
				}
				// Remove older versions of this Key
				for (const FString& KeyToRemove : OutRemovedOlderLayoutVersions)
				{
					GConfig->RemoveKey(EditorLayoutsSectionName, *KeyToRemove, ConfigFileName);
					UE_LOG(LogTemp, Warning, TEXT("While key \"%s\" was not found, and older version exists (key \"%s\"). This means section \"%s\" was"
						" created with a previous version of UE and is no longer compatible. The old key has been removed and updated with the new one."),
						*LayoutNameString, *KeyToRemove, EditorLayoutsSectionName);
				}
			}
		}
	}

	return DefaultLayout;
}


void FLayoutSaveRestore::SaveSectionToConfig(const FString& InConfigFileName, const FString& InSectionName, const FText& InSectionValue)
{
	GConfig->SetText(EditorLayoutsSectionName, *InSectionName, InSectionValue, InConfigFileName);
}

FText FLayoutSaveRestore::LoadSectionFromConfig(const FString& InConfigFileName, const FString& InSectionName)
{
	FText LayoutString;
	GConfig->GetText(EditorLayoutsSectionName, *InSectionName, LayoutString, InConfigFileName);
	return LayoutString;
}


void FLayoutSaveRestore::MigrateConfig( const FString& OldConfigFileName, const FString& NewConfigFileName )
{
	TArray<FString> OldSectionStrings;

	// check whether any layout configuration needs to be migrated
	if (!GConfig->GetSection(EditorLayoutsSectionName, OldSectionStrings, OldConfigFileName) || (OldSectionStrings.Num() == 0))
	{
		return;
	}

	TArray<FString> NewSectionStrings;

	// migrate old configuration if a new layout configuration does not yet exist
	if (!GConfig->GetSection(EditorLayoutsSectionName, NewSectionStrings, NewConfigFileName) || (NewSectionStrings.Num() == 0))
	{
		FString Key, Value;

		for (auto SectionString : OldSectionStrings)
		{
			if (SectionString.Split(TEXT("="), &Key, &Value))
			{
				GConfig->SetString(EditorLayoutsSectionName, *Key, *Value, NewConfigFileName);
			}
		}
	}

	// remove old configuration
	GConfig->EmptySection(EditorLayoutsSectionName, OldConfigFileName);
	GConfig->Flush(false, OldConfigFileName);
	GConfig->Flush(false, NewConfigFileName);
}


bool FLayoutSaveRestore::IsValidConfig(const FString& InConfigFileName)
{
	return GConfig->DoesSectionExist(EditorLayoutsSectionName, *InConfigFileName);
}


FString FLayoutSaveRestore::PrepareLayoutStringForIni(const FString& LayoutString)
{
	// Have to store braces as parentheses due to braces causing ini issues
	return LayoutString.Replace(TEXT("{"), TEXT("(")).Replace(TEXT("}"), TEXT(")")).Replace(LINE_TERMINATOR, TEXT("\\") LINE_TERMINATOR);
}


FString FLayoutSaveRestore::GetLayoutStringFromIni(const FString& LayoutString)
{
	// Revert parenthesis to braces, from ini readable to Json readable
	return LayoutString.Replace(TEXT("("), TEXT("{")).Replace(TEXT(")"), TEXT("}")).Replace(TEXT("\\") LINE_TERMINATOR, LINE_TERMINATOR);
}
