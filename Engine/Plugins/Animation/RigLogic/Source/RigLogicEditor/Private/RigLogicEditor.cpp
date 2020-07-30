// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigLogicEditor.h"
#include "Features/IModularFeatures.h"
#include "Engine/Engine.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

#include "RigUnit_RigLogic.h"

IMPLEMENT_MODULE(FRigLogicEditor, RigLogicEditor)

DEFINE_LOG_CATEGORY_STATIC(LogRigLogicEditor, Log, All);

void FRigLogicEditor::StartupModule()
{
}

void FRigLogicEditor::ShutdownModule()
{
}

