// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "Engine/Engine.h"

// Forward Declare
class UClass;
class UMoviePipelineAntiAliasingSetting;

namespace MoviePipeline
{
static UWorld* FindCurrentWorld()
{
	UWorld* World = nullptr;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::Game)
		{
			World = WorldContext.World();
		}
#if WITH_EDITOR
		else if (GIsEditor && WorldContext.WorldType == EWorldType::PIE)
		{
			World = WorldContext.World();
			if (World)
			{
				return World;
			}
		}
#endif
	}

	return World;
}
}

#define MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(InOutVariable, CVarName, OverrideValue, bUseOverride) \
{ \
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName); \
	if(ensureMsgf(CVar, TEXT("Failed to find CVar " #CVarName " to override."))) \
	{ \
		if(bUseOverride) \
		{ \
			InOutVariable = CVar->GetInt(); \
			CVar->Set(OverrideValue, EConsoleVariableFlags::ECVF_SetByConsole); \
		} \
		else \
		{ \
			CVar->Set(InOutVariable, EConsoleVariableFlags::ECVF_SetByConsole); \
		} \
	} \
}

#define MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_FLOAT(InOutVariable, CVarName, OverrideValue, bUseOverride) \
{ \
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName); \
	if(ensureMsgf(CVar, TEXT("Failed to find CVar " #CVarName " to override."))) \
	{ \
		if(bUseOverride) \
		{ \
			InOutVariable = CVar->GetFloat(); \
			CVar->Set(OverrideValue, EConsoleVariableFlags::ECVF_SetByConsole); \
		} \
		else \
		{ \
			CVar->Set(InOutVariable, EConsoleVariableFlags::ECVF_SetByConsole); \
		} \
	} \
}

#define MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT_IF_EXIST(InOutVariable, CVarName, OverrideValue, bUseOverride) \
{ \
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName); \
	if(CVar) \
	{ \
		if(bUseOverride) \
		{ \
			InOutVariable = CVar->GetInt(); \
			CVar->Set(OverrideValue, EConsoleVariableFlags::ECVF_SetByConsole); \
		} \
		else \
		{ \
			CVar->Set(InOutVariable, EConsoleVariableFlags::ECVF_SetByConsole); \
		} \
	} \
}

#define MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_FLOAT_IF_EXIST(InOutVariable, CVarName, OverrideValue, bUseOverride) \
{ \
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName); \
	if(CVar) \
	{ \
		if(bUseOverride) \
		{ \
			InOutVariable = CVar->GetFloat(); \
			CVar->Set(OverrideValue, EConsoleVariableFlags::ECVF_SetByConsole); \
		} \
		else \
		{ \
			CVar->Set(InOutVariable, EConsoleVariableFlags::ECVF_SetByConsole); \
		} \
	} \
}

namespace UE
{
	namespace MovieRenderPipeline
	{
		MOVIERENDERPIPELINECORE_API TArray<UClass*> FindMoviePipelineSettingClasses();
		MOVIERENDERPIPELINECORE_API EAntiAliasingMethod GetEffectiveAntiAliasingMethod(const UMoviePipelineAntiAliasingSetting* InSetting);
	}
}