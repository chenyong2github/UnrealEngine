// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSwitchboardPlugin, Log, All);


class FSwitchboardMenuEntry
{
public:
	static void Register();
	static void Unregister();
};
