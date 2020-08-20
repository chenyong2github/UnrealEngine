// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/Docking/LayoutService.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogLayoutService, Log, All);

const TCHAR* EditorLayoutsSectionName = TEXT("EditorLayouts");


const FString& FLayoutSaveRestore::GetAdditionalLayoutConfigIni()
{
	static const FString IniSectionAdditionalConfig = TEXT("SlateAdditionalLayoutConfig");
	return IniSectionAdditionalConfig;
}


void FLayoutSaveRestore::SaveToConfig( const FString& InConfigFileName, const TSharedRef<FTabManager::FLayout>& InLayoutToSave )
{
	// Only save to config if it's not the FTabManager::FLayout::NullLayout
	if (InLayoutToSave->GetLayoutName() != FTabManager::FLayout::NullLayout->GetLayoutName())
	{
		const FString LayoutAsString = FLayoutSaveRestore::PrepareLayoutStringForIni(InLayoutToSave->ToString());
		GConfig->SetString(EditorLayoutsSectionName, *InLayoutToSave->GetLayoutName().ToString(), *LayoutAsString, InConfigFileName);
	}
}


TSharedRef<FTabManager::FLayout> FLayoutSaveRestore::LoadFromConfig(const FString& InConfigFileName, const TSharedRef<FTabManager::FLayout>& InDefaultLayout,
	const EOutputCanBeNullptr InPrimaryAreaOutputCanBeNullptr)
{
	TArray<FString> DummyArray;
	return FLayoutSaveRestore::LoadFromConfigPrivate(InConfigFileName, InDefaultLayout, InPrimaryAreaOutputCanBeNullptr, false, DummyArray);
}


TSharedRef<FTabManager::FLayout> FLayoutSaveRestore::LoadFromConfig(const FString& InConfigFileName,
	const TSharedRef<FTabManager::FLayout>& InDefaultLayout, const EOutputCanBeNullptr InPrimaryAreaOutputCanBeNullptr, TArray<FString>& OutRemovedOlderLayoutVersions)
{
	return FLayoutSaveRestore::LoadFromConfigPrivate(InConfigFileName, InDefaultLayout, InPrimaryAreaOutputCanBeNullptr, true, OutRemovedOlderLayoutVersions);
}


TSharedRef<FTabManager::FLayout> FLayoutSaveRestore::LoadFromConfigPrivate(const FString& InConfigFileName, const TSharedRef<FTabManager::FLayout>& InDefaultLayout,
	const EOutputCanBeNullptr InPrimaryAreaOutputCanBeNullptr, const bool bInRemoveOlderLayoutVersions, TArray<FString>& OutRemovedOlderLayoutVersions)
{
	const FString LayoutNameString = InDefaultLayout->GetLayoutName().ToString();
	// If the Key (InDefaultLayout->GetLayoutName()) already exists in the section EditorLayoutsSectionName of the file InConfigFileName, try to load the layout from that file
	FString UserLayoutString;
	if (GConfig->GetString(EditorLayoutsSectionName, *LayoutNameString, UserLayoutString, InConfigFileName) && !UserLayoutString.IsEmpty())
	{
		TSharedPtr<FTabManager::FLayout> UserLayout = FTabManager::FLayout::NewFromString( FLayoutSaveRestore::GetLayoutStringFromIni( UserLayoutString ));
		if ( UserLayout.IsValid() && UserLayout->GetPrimaryArea().IsValid() )
		{
			// Return UserLayout in the following 2 cases:
			// - By default (PrimaryAreaOutputCanBeNullptr = Never or IfNoTabValid)
			// - For the case of PrimaryAreaOutputCanBeNullptr = IfNoOpenTabValid, only if the primary area has at least a valid open tab
			if (InPrimaryAreaOutputCanBeNullptr != EOutputCanBeNullptr::IfNoOpenTabValid
				|| FGlobalTabmanager::Get()->HasValidOpenTabs(UserLayout->GetPrimaryArea().Pin().ToSharedRef()))
			{
				return UserLayout.ToSharedRef();
			}
		}
	}
	// If the file layout could not be loaded and the caller wants to remove old fields
	else if (bInRemoveOlderLayoutVersions)
	{
		// If File and Section exist
		if (FConfigSection* ConfigSection = GConfig->GetSectionPrivate(EditorLayoutsSectionName, /*Force*/false, /*Const*/true, InConfigFileName))
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
					GConfig->RemoveKey(EditorLayoutsSectionName, *KeyToRemove, InConfigFileName);
					UE_LOG(LogLayoutService, Warning, TEXT("While key \"%s\" was not found, and older version exists (key \"%s\"). This means section \"%s\" was"
						" created with a previous version of UE and is no longer compatible. The old key has been removed and updated with the new one."),
						*LayoutNameString, *KeyToRemove, EditorLayoutsSectionName);
				}
			}
		}
	}

	return InDefaultLayout;
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
