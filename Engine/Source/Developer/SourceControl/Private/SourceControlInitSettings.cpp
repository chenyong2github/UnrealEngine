// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceControlInitSettings.h"

#include "Containers/StringView.h"

FSourceControlInitSettings::FSourceControlInitSettings(EBehavior InBehavior)
	: Behavior(InBehavior)
{

}

void FSourceControlInitSettings::AddSetting(FStringView SettingName, FStringView SettingValue)
{
//	const int32 Hash = GetTypeHash(SettingName);
//	Settings.AddByHash(Hash, SettingName, SettingValue)

	Settings.Add(FString(SettingName), FString(SettingValue));
}

void FSourceControlInitSettings::OverrideSetting(FStringView SettingName, FString& InOutSettingValue)
{
	const int32 Hash = GetTypeHash(SettingName);
	FString* InitialValue = Settings.FindByHash(Hash, SettingName);

	if (InitialValue != nullptr)
	{
		InOutSettingValue = *InitialValue;
	}
	else if (Behavior == EBehavior::OverrideAll)
	{
		InOutSettingValue.Empty();
	}
}

bool FSourceControlInitSettings::HasOverrides() const
{
	return !Settings.IsEmpty();
}

bool FSourceControlInitSettings::IsOverridden(FStringView SettingName) const
{
	const int32 Hash = GetTypeHash(SettingName);
	return Settings.FindByHash(Hash, SettingName) != nullptr;
}