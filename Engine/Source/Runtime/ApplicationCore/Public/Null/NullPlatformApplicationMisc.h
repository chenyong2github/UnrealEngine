// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformApplicationMisc.h"

struct APPLICATIONCORE_API FNullPlatformApplicationMisc : public FGenericPlatformApplicationMisc
{
	static class GenericApplication* CreateApplication();

	static bool IsUsingNullApplication();
};
