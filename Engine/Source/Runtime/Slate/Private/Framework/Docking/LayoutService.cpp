// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/Docking/LayoutService.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogLayoutService, Log, All);

const TCHAR* EditorLayoutsSectionName = TEXT("EditorLayouts");

static FString PrepareLayoutStringForIni(const FString& LayoutString)
{
	// Have to store braces as parentheses due to braces causing ini issues
	return LayoutString
		.Replace(TEXT("{"), TEXT("("))
		.Replace(TEXT("}"), TEXT(")"))
		.Replace(TEXT("\r"), TEXT(""))
		.Replace(TEXT("\n"), TEXT(""))
		.Replace(TEXT("\t"), TEXT(""));
}

static FString GetLayoutStringFromIni(const FString& LayoutString)
{
	// Revert parenthesis to braces, from ini readable to Json readable
	return LayoutString
		.Replace(TEXT("("), TEXT("{"))
		.Replace(TEXT(")"), TEXT("}"))
		.Replace(TEXT("\\") LINE_TERMINATOR, LINE_TERMINATOR);
}


const FString& FLayoutSaveRestore::GetAdditionalLayoutConfigIni()
{
	static const FString IniSectionAdditionalConfig = TEXT("SlateAdditionalLayoutConfig");
	return IniSectionAdditionalConfig;
}

static TSharedPtr<FJsonObject> ConvertSectionToJson(const TArray<FString>& SectionStrings)
{
	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();

	for (const FString& SectionPair : SectionStrings)
	{
		FString Key, Value;
		if (SectionPair.Split(TEXT("="), &Key, &Value))
		{
			Value = GetLayoutStringFromIni(Value);

			TSharedPtr<FJsonObject> ChildObject = MakeShared<FJsonObject>();
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Value);
			if (FJsonSerializer::Deserialize(Reader, ChildObject))
			{
				RootObject->SetObjectField(Key, ChildObject);
			}
			else
			{
				RootObject->SetStringField(Key, Value);
			}
		}
	}

	return RootObject;
}

static FString GetLayoutJsonFileName(const FString& InConfigFileName)
{
	const FString JsonFileName = FPaths::GetBaseFilename(InConfigFileName) + TEXT(".json");
	const FString UserSettingsPath = FPaths::Combine(FPlatformProcess::UserSettingsDir(), FApp::GetEpicProductIdentifier(), TEXT("Editor"), JsonFileName);
	return UserSettingsPath;
}

static TSharedPtr<FJsonObject> LoadJsonFile(const FString& InFileName)
{
	TSharedPtr<FJsonObject> JsonObject;

	FString JsonContents;
	if (FFileHelper::LoadFileToString(JsonContents, *InFileName))
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContents);
		FJsonSerializer::Deserialize(Reader, JsonObject);
	}

	return JsonObject;
}

static bool SaveJsonFile(const FString& InFileName, TSharedPtr<FJsonObject> JsonObject)
{
	FString NewJsonContents;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&NewJsonContents);
	if (FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
	{
		return FFileHelper::SaveStringToFile(NewJsonContents, *InFileName);
	}

	return false;
}

static void SaveLayoutToJson(const FString& InConfigFileName, const TSharedRef<FTabManager::FLayout>& InLayoutToSave)
{
	const FString UserSettingsPath = GetLayoutJsonFileName(InConfigFileName);

	TSharedPtr<FJsonObject> AllLayoutsObject = LoadJsonFile(UserSettingsPath);
	if (!AllLayoutsObject.IsValid())
	{
		// doesn't exist
		AllLayoutsObject = MakeShared<FJsonObject>();
	}

	AllLayoutsObject->SetObjectField(InLayoutToSave->GetLayoutName().ToString(), InLayoutToSave->ToJson());

	SaveJsonFile(UserSettingsPath, AllLayoutsObject);
}

static bool LoadLayoutFromJson(const FString& InConfigFileName, const FString& InLayoutName, TSharedPtr<FTabManager::FLayout>& OutLayout)
{
	const FString UserSettingsPath = GetLayoutJsonFileName(InConfigFileName);

	TSharedPtr<FJsonObject> JsonObject = LoadJsonFile(InConfigFileName);
	if (!JsonObject.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* LayoutJson = nullptr;
	if (JsonObject->TryGetObjectField(InLayoutName, LayoutJson))
	{
		OutLayout = FTabManager::FLayout::NewFromJson(*LayoutJson);
		return true;
	}

	return false;
}

void FLayoutSaveRestore::SaveToConfig( const FString& InConfigFileName, const TSharedRef<FTabManager::FLayout>& InLayoutToSave )
{
	// Only save to config if it's not the FTabManager::FLayout::NullLayout
	if (InLayoutToSave->GetLayoutName() != FTabManager::FLayout::NullLayout->GetLayoutName())
	{
		const FString LayoutAsString = PrepareLayoutStringForIni(InLayoutToSave->ToString());
		GConfig->SetString(EditorLayoutsSectionName, *InLayoutToSave->GetLayoutName().ToString(), *LayoutAsString, InConfigFileName);

		SaveLayoutToJson(InConfigFileName, InLayoutToSave);
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

	TSharedPtr<FTabManager::FLayout> UserLayout;

	// First try to load from JSON, then INI if that does not exist
	if (!LoadLayoutFromJson(InConfigFileName, LayoutNameString, UserLayout))
	{
		FString IniLayoutString;
		// If the Key (InDefaultLayout->GetLayoutName()) already exists in the section EditorLayoutsSectionName of the file InConfigFileName, try to load the layout from that file
		GConfig->GetString(EditorLayoutsSectionName, *LayoutNameString, IniLayoutString, InConfigFileName);
		UserLayout = FTabManager::FLayout::NewFromString( GetLayoutStringFromIni( IniLayoutString ) );
	}

	if (UserLayout.IsValid())
	{
		// Return UserLayout in the following 2 cases:
		// - By default (PrimaryAreaOutputCanBeNullptr = Never or IfNoTabValid)
		// - For the case of PrimaryAreaOutputCanBeNullptr = IfNoOpenTabValid, only if the primary area has at least a valid open tab
		TSharedPtr<FTabManager::FArea> PrimaryArea = UserLayout->GetPrimaryArea().Pin();
		if (PrimaryArea.IsValid() && (InPrimaryAreaOutputCanBeNullptr != EOutputCanBeNullptr::IfNoOpenTabValid || FGlobalTabmanager::Get()->HasValidOpenTabs(PrimaryArea.ToSharedRef())))
		{
			return UserLayout.ToSharedRef();
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
	FString StrValue;
	FTextStringHelper::WriteToBuffer(StrValue, InSectionValue);

	GConfig->SetString(EditorLayoutsSectionName, *InSectionName, *StrValue, InConfigFileName);

	const FString JsonFileName = GetLayoutJsonFileName(InConfigFileName);
	TSharedPtr<FJsonObject> JsonObject = LoadJsonFile(JsonFileName);
	if (JsonObject.IsValid())
	{
		JsonObject->SetStringField(InSectionName, *StrValue);
		SaveJsonFile(JsonFileName, JsonObject);
	}
}

FText FLayoutSaveRestore::LoadSectionFromConfig(const FString& InConfigFileName, const FString& InSectionName)
{
	FString ValueString;
	const FString JsonFileName = GetLayoutJsonFileName(InConfigFileName);
	TSharedPtr<FJsonObject> JsonObject = LoadJsonFile(JsonFileName);
	if (JsonObject.IsValid())
	{
		ValueString = JsonObject->GetStringField(InSectionName);
	}

	if (ValueString.IsEmpty())
	{
		GConfig->GetString(EditorLayoutsSectionName, *InSectionName, ValueString, InConfigFileName);
	}

	FText ValueText;
	FTextStringHelper::ReadFromBuffer(*ValueString, ValueText, EditorLayoutsSectionName);
	
	return ValueText;
}

bool FLayoutSaveRestore::DuplicateConfig(const FString& SourceConfigFileName, const FString& TargetConfigFileName)
{
	const bool bShouldReplace = true;
	const bool bCopyEvenIfReadOnly = true;
	const bool bCopyAttributes = false; // If true, we could copy the read-only flag of DefaultLayout.ini and cause save/load to stop working

	if (IFileManager::Get().Copy(*TargetConfigFileName, *SourceConfigFileName, bShouldReplace, bCopyEvenIfReadOnly, bCopyAttributes) == COPY_Fail)
	{
		return false;
	}

	// convert this layout to a JSON file
	TArray<FString> SectionPairs;
	GConfig->GetSection(EditorLayoutsSectionName, SectionPairs, TargetConfigFileName);

	TSharedPtr<FJsonObject> RootObject = ConvertSectionToJson(SectionPairs);

	const FString TargetJsonFilename = GetLayoutJsonFileName(TargetConfigFileName);
	SaveJsonFile(TargetJsonFilename, RootObject);

	return true;
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

		for (const FString& SectionString : OldSectionStrings)
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

	// migrate layout to JSON as well
	const FString NewLayoutJsonFileName = GetLayoutJsonFileName(NewConfigFileName);
	TSharedPtr<FJsonObject> JsonObject = LoadJsonFile(NewLayoutJsonFileName);
	if (!JsonObject.IsValid() || JsonObject->Values.Num() == 0)
	{
		TSharedPtr<FJsonObject> RootObject = ConvertSectionToJson(OldSectionStrings);
		SaveJsonFile(NewLayoutJsonFileName, RootObject);
	}
}


bool FLayoutSaveRestore::IsValidConfig(const FString& InConfigFileName)
{
	if (GConfig->DoesSectionExist(EditorLayoutsSectionName, *InConfigFileName))
	{
		return true;
	}

	const FString JsonFileName = GetLayoutJsonFileName(InConfigFileName);
	return IFileManager::Get().FileExists(*JsonFileName);
}
