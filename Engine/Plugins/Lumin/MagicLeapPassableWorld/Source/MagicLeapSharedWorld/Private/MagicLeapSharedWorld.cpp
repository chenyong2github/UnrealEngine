
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "MagicLeapSharedWorldTypes.h"

DEFINE_LOG_CATEGORY(LogMagicLeapSharedWorld);

class FMagicLeapSharedWorldModule : public IModuleInterface
{};

IMPLEMENT_MODULE(FMagicLeapSharedWorldModule, MagicLeapSharedWorld);
