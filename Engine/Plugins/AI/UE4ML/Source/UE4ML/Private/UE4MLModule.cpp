// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE4MLModule.h"
#include "4MLManager.h"
#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebugger.h"
#include "Debug/GameplayDebuggerCategory_4ML.h"
#endif // WITH_GAMEPLAY_DEBUGGER


class FUE4MLModule : public IUE4MLModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FUE4MLModule, UE4ML )

void FUE4MLModule::StartupModule()
{
#if WITH_GAMEPLAY_DEBUGGER
	IGameplayDebugger& GameplayDebuggerModule = IGameplayDebugger::Get();
	GameplayDebuggerModule.RegisterCategory("UE4ML", IGameplayDebugger::FOnGetCategory::CreateStatic(&FGameplayDebuggerCategory_4ML::MakeInstance), EGameplayDebuggerCategoryState::EnabledInGameAndSimulate);
	GameplayDebuggerModule.NotifyCategoriesChanged();
#endif
}

void FUE4MLModule::ShutdownModule()
{
#if WITH_GAMEPLAY_DEBUGGER
	if (IGameplayDebugger::IsAvailable())
	{
		IGameplayDebugger& GameplayDebuggerModule = IGameplayDebugger::Get();
		GameplayDebuggerModule.UnregisterCategory("UE4ML");
		GameplayDebuggerModule.NotifyCategoriesChanged();
	}
#endif
}
