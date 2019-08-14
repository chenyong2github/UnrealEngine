// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CoreMinimal.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "OSCLog.h"

DEFINE_LOG_CATEGORY(LogOSC);


class FOSCModule : public IModuleInterface
{
};

IMPLEMENT_MODULE(FOSCModule, OSC)
