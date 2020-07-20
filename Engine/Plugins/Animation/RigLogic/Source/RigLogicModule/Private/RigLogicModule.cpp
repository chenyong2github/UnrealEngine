// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigLogicModule.h"

#include "RigUnit_RigLogic.h"

#include "Interfaces/IPluginManager.h"
#include "Runtime/Core/Public/Misc/Paths.h"

#include "Runtime/Core/Public/Modules/ModuleInterface.h"
#include "Runtime/Core/Public/Modules/ModuleManager.h"

#include "Runtime/Core/Public/HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "FRigLogicModule"

DEFINE_LOG_CATEGORY(LogRigLogic);

void FRigLogicModule::StartupModule()
{

}

void FRigLogicModule::ShutdownModule()
{

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRigLogicModule, RigLogicModule)

