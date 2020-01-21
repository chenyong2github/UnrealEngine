// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

namespace DisasterRecoveryUtil
{
	/** Returns the container name for the settings. */
	inline static FName GetSettingsContainerName() { return "Project"; }

	/** Returns the category name for the settings. */
	inline static FName GetSettingsCategoryName() { return "Plugins"; }

	/** Returns the section name for the settings. */
	inline static FName GetSettingsSectionName() { return "Disaster Recovery"; }
}
