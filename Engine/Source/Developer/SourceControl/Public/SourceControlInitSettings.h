// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Containers/StringFwd.h"
#include "Containers/UnrealString.h"

class FString;

class SOURCECONTROL_API FSourceControlInitSettings
{
public:
	enum class EBehavior
	{
		/** All existing settings will be overridden via the contents of FSourceControlInitSettings. Settings that are not found will be reset to default states */
		OverrideAll,
		/** Only the settings found in FSourceControlInitSettings will be overriden. Settings not found will be left with their current values. */
		OverrideExisting,
	};

	FSourceControlInitSettings(EBehavior Behavior);
	~FSourceControlInitSettings() = default;

	void AddSetting(FStringView SettingName, FStringView SettingValue);
	void OverrideSetting(FStringView SettingName, FString& InOutSettingValue);

	bool HasOverrides() const;
	bool IsOverridden(FStringView SettingName) const;
private:
	EBehavior Behavior;

	TMap<FString, FString> Settings;
};
