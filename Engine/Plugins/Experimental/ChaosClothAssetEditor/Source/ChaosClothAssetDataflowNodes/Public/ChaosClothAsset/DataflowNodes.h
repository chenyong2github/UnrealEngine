// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowObjectInterface.h"
#include "Logging/LogMacros.h"

namespace UE::Chaos::ClothAsset::DataflowNodes
{
	void Register();
	void LogAndToastWarning(const FText& Error);
}

DECLARE_LOG_CATEGORY_EXTERN(LogChaosClothAssetDataflowNodes, Log, All);
