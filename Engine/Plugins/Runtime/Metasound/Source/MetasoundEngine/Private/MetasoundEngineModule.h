// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleInterface.h"

// Forward Declarations
class UMetasound;

DECLARE_LOG_CATEGORY_EXTERN(LogMetasoundEngine, Log, All);


class IMetasoundEngineModule : public IModuleInterface
{
	// Deserializes Metasound from the graph at the provided path.
	// @param InPath Content directory path to load metasound from.
	// @returns New Metasound object on success, nullptr on failure.
	// @todo decide if this means that we remove the graph in our current asset.
	//       This seems dangerous since we can fail to save the new asset and scratch the old one.
	virtual UMetasound* DeserializeMetasound(const FString& InPath) = 0;

	// Serializes Metasound to the provided path.
	// @param InMetasound Metasound to serialize.
	// @param InPath Path to serialize to.
	virtual void SerializeMetasound(const UMetasound& InMetasound, const FString& InPath) = 0;
};
