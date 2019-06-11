// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ChaosCoreModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#if INCLUDE_CHAOS

IMPLEMENT_MODULE(FChaosCoreEngineModule, ChaosCore);

#else

IMPLEMENT_MODULE(FDefaultModuleImpl, ChaosCore);

#endif
