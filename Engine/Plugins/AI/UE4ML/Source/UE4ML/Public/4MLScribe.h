// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include <vector>
#include <string>
#include <map>

/** Describes requested data in an rpclib-usable format */
namespace F4MLScribe
{
	std::vector<std::string> ToStringVector(const TArray<FString>& Array);
	std::vector<std::string> ToStringVector(const TArray<FName>& Array);
	std::vector<std::string> ListFunctions();
	std::map<std::string, uint32> ListSensorTypes();
	std::map<std::string, uint32> ListActuatorTypes();

	// will search for the given names first in function names, then sensors, 
	// actuators, agent classes. To be expanded
	std::string GetDescription(std::string const& ElementName);
};