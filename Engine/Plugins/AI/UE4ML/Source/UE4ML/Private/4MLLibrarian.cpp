// Copyright Epic Games, Inc. All Rights Reserved.

#include "4MLLibrarian.h"
#include "4MLTypes.h"
#include "Sensors/4MLSensor.h"
#include "Actuators/4MLActuator.h"
#include "Agents/4MLAgent.h"
#include "UObject/UObjectHash.h"
#include "4MLManager.h"


namespace FLibrarianHelper
{
	bool IsValidClass(const UClass* Class)
	{
		if (!Class)
		{
			return false;
		}

		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			UE_LOG(LogUE4ML, Log, TEXT("Librarian: skipping class %s registration due to it being %s%s%s")
				, *Class->GetName()
				, Class->HasAnyClassFlags(CLASS_Abstract) ? TEXT("Abstract, ") : TEXT("")
				, Class->HasAnyClassFlags(CLASS_Deprecated) ? TEXT("Deprecated, ") : TEXT("")
				, Class->HasAnyClassFlags(CLASS_NewerVersionExists) ? TEXT("NewerVesionExists, ") : TEXT("")
			);
			return false;
		}

		return true;
	}
}

const F4MLLibrarian& F4MLLibrarian::Get()
{
	return U4MLManager::Get().GetLibrarian();
}

void F4MLLibrarian::GatherClasses()
{
	{
		TArray<UClass*> Results;
		GetDerivedClasses(U4MLSensor::StaticClass(), Results, /*bRecursive=*/true);
		for (UClass* Class : Results)
		{
			RegisterSensorClass(Class);
		}
	}
	{
		TArray<UClass*> Results;
		GetDerivedClasses(U4MLActuator::StaticClass(), Results, /*bRecursive=*/true);
		for (UClass* Class : Results)
		{
			RegisterActuatorClass(Class);
		}
	}
	{
		RegisterAgentClass(U4MLAgent::StaticClass());

		TArray<UClass*> Results;
		GetDerivedClasses(U4MLAgent::StaticClass(), Results, /*bRecursive=*/true);
		for (UClass* Class : Results)
		{
			RegisterAgentClass(Class);
		}
	}
}

void F4MLLibrarian::RegisterSensorClass(const TSubclassOf<U4MLSensor>& Class)
{
	if (FLibrarianHelper::IsValidClass(Class) == false)
	{
		return;
	}

	U4MLSensor* CDO = Class->GetDefaultObject<U4MLSensor>();
	check(CDO);
	if (KnownSensorClasses.Find(CDO->GetElementID()) == nullptr)
	{
		KnownSensorClasses.Add(CDO->GetElementID(), Class);
	}
}

void F4MLLibrarian::RegisterActuatorClass(const TSubclassOf<U4MLActuator>& Class)
{
	if (FLibrarianHelper::IsValidClass(Class) == false)
	{
		return;
	}

	U4MLActuator* CDO = Class->GetDefaultObject<U4MLActuator>();
	check(CDO);
	if (KnownActuatorClasses.Find(CDO->GetElementID()) == nullptr)
	{
		KnownActuatorClasses.Add(CDO->GetElementID(), Class);
	}
}

void F4MLLibrarian::RegisterAgentClass(const TSubclassOf<U4MLAgent>& Class)
{
	KnownAgentClasses.AddUnique(Class);
}

void F4MLLibrarian::AddRPCFunctionDescription(const FName FunctionName, FString&& Description)
{
	RPCFunctionDescriptions.FindOrAdd(FunctionName) = Description;
}

bool F4MLLibrarian::GetFunctionDescription(const FName FunctionName, FString& OutDescription) const
{
	const FString* FoundDesc = RPCFunctionDescriptions.Find(FunctionName);
	if (FoundDesc)
	{
		OutDescription = *FoundDesc;
	}
	return FoundDesc != nullptr;
}

bool F4MLLibrarian::GetSensorDescription(const FName SensorName, FString& OutDescription) const
{
	UClass* ResultClass = FindSensorClass(SensorName);
	if (ResultClass)
	{
		U4MLAgentElement* CDO = ResultClass->GetDefaultObject<U4MLAgentElement>();
		if (CDO)
		{
			OutDescription = CDO->GetDescription();
			return true;
		}
	}
	return false;
}

bool F4MLLibrarian::GetActuatorDescription(const FName ActuatorName, FString& OutDescription) const
{
	UClass* ResultClass = FindActuatorClass(ActuatorName);
	if (ResultClass)
	{
		U4MLAgentElement* CDO = ResultClass->GetDefaultObject<U4MLAgentElement>();
		if (CDO)
		{
			OutDescription = CDO->GetDescription();
			return true;
		}
	}
	return false;
}

TSubclassOf<U4MLAgent> F4MLLibrarian::FindAgentClass(const FName ClassName) const
{
	UClass* AgentClass = nullptr;
	if (ClassName != NAME_None)
	{
		for (auto It = GetAgentsClassIterator(); It; ++It)
		{
			if (It->Get() && It->Get()->GetFName() == ClassName)
			{
				AgentClass = *It;
				break;
			}
		}

		if (AgentClass == nullptr)
		{
			const FName DecoratedName(FString::Printf(TEXT("4MLAgent_%s"), *ClassName.ToString()));
			for (auto It = GetAgentsClassIterator(); It; ++It)
			{
				if (It->Get() && It->Get()->GetFName() == DecoratedName)
				{
					AgentClass = *It;
					break;
				}
			}
		}
	}

	return (AgentClass != nullptr) ? AgentClass : U4MLAgent::StaticClass();
}

TSubclassOf<U4MLSensor> F4MLLibrarian::FindSensorClass(const FName ClassName) const
{
	UClass* ResultClass = nullptr;
	if (ClassName != NAME_None)
	{
		for (auto It = GetSensorsClassIterator(); It; ++It)
		{
			if (It->Value.Get() && It->Value.Get()->GetFName() == ClassName)
			{
				ResultClass = It->Value.Get();
				break;
			}
		}

		if (ResultClass == nullptr)
		{
			const FName DecoratedName(FString::Printf(TEXT("4MLSensor_%s"), *ClassName.ToString()));
			for (auto It = GetSensorsClassIterator(); It; ++It)
			{
				if (It->Value.Get() && It->Value.Get()->GetFName() == DecoratedName)
				{
					ResultClass = It->Value.Get();
					break;
				}
			}
		}
	}

	return ResultClass;
}

TSubclassOf<U4MLActuator> F4MLLibrarian::FindActuatorClass(const FName ClassName) const
{
	UClass* ResultClass = nullptr;
	if (ClassName != NAME_None)
	{
		for (auto It = GetActuatorsClassIterator(); It; ++It)
		{
			if (It->Value.Get() && It->Value.Get()->GetFName() == ClassName)
			{
				ResultClass = It->Value.Get();
				break;
			}
		}

		if (ResultClass == nullptr)
		{
			const FName DecoratedName(FString::Printf(TEXT("4MLActuator_%s"), *ClassName.ToString()));
			for (auto It = GetActuatorsClassIterator(); It; ++It)
			{
				if (It->Value.Get() && It->Value.Get()->GetFName() == DecoratedName)
				{
					ResultClass = It->Value.Get();
					break;
				}
			}
		}
	}

	return ResultClass;
}
