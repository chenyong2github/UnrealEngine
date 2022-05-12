// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphCoreModule.h"

#include "CoreMinimal.h"
#include "EvalGraph/EvalGraphNodeFactory.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "EvalGraphCore"

void IEvalGraphCoreModule::StartupModule()
{/*Never Called ?*/}

void IEvalGraphCoreModule::ShutdownModule()
{/*Never Called ?*/ }

IMPLEMENT_MODULE(IEvalGraphCoreModule, EvalGraphCore)

#undef LOCTEXT_NAMESPACE
