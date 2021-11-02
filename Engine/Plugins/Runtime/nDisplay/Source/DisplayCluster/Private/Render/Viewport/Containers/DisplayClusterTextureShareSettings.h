// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDisplayClusterTextureShareSettings
{
public:
	// Allow texture sharing from nDisplay
	bool bIsEnabled = true;

	// Allow global frame sync. Requires the same sync from an external application. Used to synchronize multiviewports.
	bool bIsGlobalSyncEnabled = false;
};
