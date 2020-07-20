// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#ifndef RIGLOGIC_MODULE_DISCARD

#include "RigLogicLib.h"

#include "Runtime/Core/Public/Modules/ModuleInterface.h"
#include "Runtime/Core/Public/Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FRigLogicLib"

DEFINE_LOG_CATEGORY(LogRigLogicLib);

void FRigLogicLib::StartupModule()
{
}

void FRigLogicLib::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRigLogicLib, RigLogicLib)

#endif  // RIGLOGIC_MODULE_DISCARD
