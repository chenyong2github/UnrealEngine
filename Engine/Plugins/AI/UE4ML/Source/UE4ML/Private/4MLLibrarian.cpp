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
	ensure(KnownSensorClasses.Find(CDO->GetElementID()) == nullptr);
	KnownSensorClasses.Add(CDO->GetElementID(), Class);
}

void F4MLLibrarian::RegisterActuatorClass(const TSubclassOf<U4MLActuator>& Class)
{
	if (FLibrarianHelper::IsValidClass(Class) == false)
	{
		return;
	}

	U4MLActuator* CDO = Class->GetDefaultObject<U4MLActuator>();
	check(CDO);
	ensure(KnownActuatorClasses.Find(CDO->GetElementID()) == nullptr);
	KnownActuatorClasses.Add(CDO->GetElementID(), Class);
}

void F4MLLibrarian::RegisterAgentClass(const TSubclassOf<U4MLAgent>& Class)
{
	KnownAgentClasses.AddUnique(Class);
}

bool F4MLLibrarian::GetFunctionDescription(const FName& FunctionName, FString& OutDescription) const
{
	auto* ClientFuncData = U4MLManager::Get().GetAvailableClientFunctions().Find(FunctionName);
	if (ClientFuncData)
	{
		OutDescription = ClientFuncData->Get<1>();
		return true;
	}
	auto* ServerFuncData = U4MLManager::Get().GetAvailableServerFunctions().Find(FunctionName);
	if (ServerFuncData)
	{
		OutDescription = ServerFuncData->Get<1>();
		return true; 
	}
	return false;
}

