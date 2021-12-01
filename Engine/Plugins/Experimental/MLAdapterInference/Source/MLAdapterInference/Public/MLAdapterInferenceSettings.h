// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Engine/DeveloperSettings.h"
#include "GameFramework/Controller.h"
#include "Agents/MLAdapterAgent.h"
#include "Sensors/MLAdapterSensor.h"
#include "Actuators/MLAdapterActuator.h"
#include "NeuralNetwork.h"
#include "MLAdapterInferenceSettings.generated.h"


USTRUCT()
struct MLADAPTERINFERENCE_API FInferenceSensorConfig : public FMLAdapterParameterMap
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = MLAdapterInference, meta = (MetaClass = "MLAdapterSensor"))
	FSoftClassPath SensorClass;
};

USTRUCT()
struct MLADAPTERINFERENCE_API FInferenceActuatorConfig : public FMLAdapterParameterMap
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = MLAdapterInference, meta = (MetaClass = "MLAdapterActuator"))
	FSoftClassPath ActuatorClass;
};

USTRUCT()
struct MLADAPTERINFERENCE_API FInferenceAgentConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, config, Category = MLAdapterInference)
	TSubclassOf<UMLAdapterAgent> AgentClass;

	UPROPERTY(EditAnywhere, config, Category = MLAdapterInference)
	TSubclassOf<AActor> AvatarClass;

	UPROPERTY(EditAnywhere, config, Category = MLAdapterInference, meta = (AllowedClasses = "NeuralNetwork"))
	FSoftObjectPath NeuralNetworkPath;

	UPROPERTY(EditAnywhere, config, Category = MLAdapterInference)
	TArray<FInferenceSensorConfig> Sensors;

	UPROPERTY(EditAnywhere, config, Category = MLAdapterInference)
	TArray<FInferenceActuatorConfig> Actuators;

	FMLAdapterAgentConfig AsMLAdapterAgentConfig() const;
};

#define GET_CONFIG_VALUE(a) (GetDefault<UMLAdapterInferenceSettings>()->a)

/**
 * Implements the settings for the MLAdapterInference plugin.
 */
UCLASS(config = Plugins, defaultconfig, DisplayName = "MLAdapterInference")
class MLADAPTERINFERENCE_API UMLAdapterInferenceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static TConstArrayView<FInferenceAgentConfig> GetAgentConfigs();

protected:
	UPROPERTY(EditAnywhere, config, Category = MLAdapterInference)
	TArray<FInferenceAgentConfig> AgentConfigs;
};

#undef GET_CONFIG_VALUE