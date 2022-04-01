// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

MLDEFORMERFRAMEWORK_API DECLARE_LOG_CATEGORY_EXTERN(LogMLDeformer, Log, All);

namespace UE::MLDeformer
{
	class MLDEFORMERFRAMEWORK_API FMLDeformerModule : public IModuleInterface
	{
	public:
		// IModuleInterface overrides.
		virtual void StartupModule() override;
		// ~END IModuleInterface overrides.
	};
}
