// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

NEURALMORPHMODEL_API DECLARE_LOG_CATEGORY_EXTERN(LogNeuralMorphModel, Log, All);

namespace UE::NeuralMorphModel
{
	class NEURALMORPHMODEL_API FNeuralMorphModelModule
		: public IModuleInterface
	{
	};
}	// namespace NeuralMorphModel
