// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Templates/SubclassOf.h"
#include "Sensors/MLAdapterSensor.h"
#include "Actuators/MLAdapterActuator.h"
#include "Agents/MLAdapterAgent.h"
#include "MLAdapterLibrarian.generated.h"

USTRUCT()
struct FMLAdapterLibrarian
{
	GENERATED_BODY()
		
	void GatherClasses();
	void RegisterSensorClass(const TSubclassOf<UMLAdapterSensor>& Class);
	void RegisterActuatorClass(const TSubclassOf<UMLAdapterActuator>& Class);
	void RegisterAgentClass(const TSubclassOf<UMLAdapterAgent>& Class);

	void AddRPCFunctionDescription(const FName FunctionName, FString&& Description);

	TMap<uint32, TSubclassOf<UMLAdapterSensor> >::TConstIterator GetSensorsClassIterator() const { return KnownSensorClasses.CreateConstIterator(); }
	TMap<uint32, TSubclassOf<UMLAdapterActuator> >::TConstIterator GetActuatorsClassIterator() const { return KnownActuatorClasses.CreateConstIterator(); }
	TArray<TSubclassOf<UMLAdapterAgent> >::TConstIterator GetAgentsClassIterator() const { return KnownAgentClasses.CreateConstIterator(); }

	bool GetFunctionDescription(const FName FunctionName, FString& OutDescription) const;
	inline bool GetFunctionDescription(const FString& FunctionName, FString& OutDescription) const;
	TMap<FName, FString>::TConstIterator GetFunctionDescriptionsIterator() const { return RPCFunctionDescriptions.CreateConstIterator(); }

	bool GetSensorDescription(const FName SensorName, FString& OutDescription) const;
	bool GetActuatorDescription(const FName ActuatorName, FString& OutDescription) const;

	TSubclassOf<UMLAdapterAgent> FindAgentClass(const FName ClassName) const;
	TSubclassOf<UMLAdapterSensor> FindSensorClass(const FName ClassName) const;
	TSubclassOf<UMLAdapterActuator> FindActuatorClass(const FName ClassName) const;

	static const FMLAdapterLibrarian& Get();

protected:
	UPROPERTY()
	TMap<uint32, TSubclassOf<UMLAdapterSensor> > KnownSensorClasses;

	UPROPERTY()
	TMap<uint32, TSubclassOf<UMLAdapterActuator> > KnownActuatorClasses;

	UPROPERTY()
	TArray<TSubclassOf<UMLAdapterAgent> > KnownAgentClasses;

	TMap<FName, FString> RPCFunctionDescriptions;
};

//----------------------------------------------------------------------//
// inlines 
//----------------------------------------------------------------------//
bool FMLAdapterLibrarian::GetFunctionDescription(const FString& FunctionName, FString& OutDescription) const
{
	return GetFunctionDescription(FName(*FunctionName), OutDescription);
}