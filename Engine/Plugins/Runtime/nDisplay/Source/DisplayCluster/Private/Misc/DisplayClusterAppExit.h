// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/**
 * Auxiliary class. Responsible for terminating application.
 */
class FDisplayClusterAppExit
{
public:
	static void ExitApplication(const FString& Msg);
};
