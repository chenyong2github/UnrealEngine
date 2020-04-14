// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Templates/SubclassOf.h"
#include "Sensors/4MLSensor.h"
#include "Actuators/4MLActuator.h"
#include "Agents/4MLAgent.h"
#include "4MLLibrarian.generated.h"

USTRUCT()
struct F4MLLibrarian
{
	GENERATED_BODY()
		
	void GatherClasses();
	void RegisterSensorClass(const TSubclassOf<U4MLSensor>& Class);
	void RegisterActuatorClass(const TSubclassOf<U4MLActuator>& Class);
	void RegisterAgentClass(const TSubclassOf<U4MLAgent>& Class);

	TMap<uint32, TSubclassOf<U4MLSensor> >::TConstIterator GetSensorsIterator() const { return KnownSensorClasses.CreateConstIterator(); }
	TMap<uint32, TSubclassOf<U4MLActuator> >::TConstIterator GetActuatorsIterator() const { return KnownActuatorClasses.CreateConstIterator(); }
	TArray<TSubclassOf<U4MLAgent> >::TConstIterator GetAgentsIterator() const { return KnownAgentClasses.CreateConstIterator(); }

	bool GetFunctionDescription(const FName& FunctionName, FString& OutDescription) const;
	inline bool GetFunctionDescription(const FString& FunctionName, FString& OutDescription) const;

protected:
	UPROPERTY()
	TMap<uint32, TSubclassOf<U4MLSensor> > KnownSensorClasses;

	UPROPERTY()
	TMap<uint32, TSubclassOf<U4MLActuator> > KnownActuatorClasses;

	UPROPERTY()
	TArray<TSubclassOf<U4MLAgent> > KnownAgentClasses;
};

//----------------------------------------------------------------------//
// inlines 
//----------------------------------------------------------------------//
bool F4MLLibrarian::GetFunctionDescription(const FString& FunctionName, FString& OutDescription) const
{
	return GetFunctionDescription(FName(*FunctionName), OutDescription);
}