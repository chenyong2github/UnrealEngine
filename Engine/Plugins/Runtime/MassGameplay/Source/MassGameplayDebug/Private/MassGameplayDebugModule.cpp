// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IMassGameplayDebugModule.h"
#include "MassCommonTypes.h"
#include "MassGameplayDebugTypes.h"
#include "DebugEntityTemplateBuilder.h"

DEFINE_LOG_CATEGORY(LogMassDebug);

class FMassGameplayDebug : public IMassGameplayDebugModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FMassGameplayDebug, MassGameplayDebug)

void FMassGameplayDebug::StartupModule()
{
	UE::Mass::DebugEntityTemplateBuilder::Register();
}

void FMassGameplayDebug::ShutdownModule()
{
}



