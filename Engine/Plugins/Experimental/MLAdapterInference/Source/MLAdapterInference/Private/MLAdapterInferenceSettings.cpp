// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLAdapterInferenceSettings.h"
#include "GameFramework/PlayerController.h"
#include "MLAdapterInferenceAgent.h"

#define GET_CONFIG_VALUE(a) (GetDefault<UMLAdapterInferenceSettings>()->a)
TConstArrayView<FInferenceAgentConfig> UMLAdapterInferenceSettings::GetAgentConfigs()
{
	const TArray<FInferenceAgentConfig>& Configs = GET_CONFIG_VALUE(AgentConfigs);
	TConstArrayView<FInferenceAgentConfig> View((const FInferenceAgentConfig*)Configs.GetData(), Configs.Num());
	return View;
}
#undef GET_CONFIG_VALUE

FMLAdapterAgentConfig FInferenceAgentConfig::AsMLAdapterAgentConfig() const
{
	FMLAdapterAgentConfig ResultConfig;

	if (AgentClass != nullptr)
	{
		ResultConfig.AgentClassName = AgentClass->GetFName();
	}
	else
	{
		ResultConfig.AgentClassName = UMLAdapterInferenceAgent::StaticClass()->GetFName();
	}

	if (AvatarClass != nullptr)
	{
		ResultConfig.AvatarClassName = AvatarClass->GetFName();
		ResultConfig.AvatarClass = AvatarClass;
	}
	else
	{
		ResultConfig.AvatarClassName = APlayerController::StaticClass()->GetFName();
		ResultConfig.AvatarClass = APlayerController::StaticClass();
	}	

	for (FInferenceSensorConfig SensorConfig : Sensors)
	{
		FMLAdapterParameterMap ParamMap;
		ParamMap.Params = SensorConfig.Params;
		ResultConfig.Sensors.Add(FName(SensorConfig.SensorClass.GetAssetName()), ParamMap);
	}

	for (FInferenceActuatorConfig ActuatorConfig : Actuators)
	{
		FMLAdapterParameterMap ParamMap;
		ParamMap.Params = ActuatorConfig.Params;
		ResultConfig.Actuators.Add(FName(ActuatorConfig.ActuatorClass.GetAssetName()), ParamMap);
	}

	return ResultConfig;
}
