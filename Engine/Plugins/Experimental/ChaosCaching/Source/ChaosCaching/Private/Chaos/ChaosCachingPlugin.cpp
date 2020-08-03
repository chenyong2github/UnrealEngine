// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosCachingPlugin.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(IChaosCachingPlugin, ChaosCaching)

DEFINE_LOG_CATEGORY(LogChaosCache)

void IChaosCachingPlugin::StartupModule() {}

void IChaosCachingPlugin::ShutdownModule() {}
