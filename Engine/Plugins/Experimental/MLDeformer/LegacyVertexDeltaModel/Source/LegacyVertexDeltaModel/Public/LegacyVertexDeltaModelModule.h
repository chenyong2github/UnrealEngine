// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

LEGACYVERTEXDELTAMODEL_API DECLARE_LOG_CATEGORY_EXTERN(LogLegacyVertexDeltaModel, Log, All);

namespace UE::LegacyVertexDeltaModel
{
	class LEGACYVERTEXDELTAMODEL_API FLegacyVertexDeltaModelModule
		: public IModuleInterface
	{
	public:
		// IModuleInterface overrides.
		virtual void StartupModule() override;
		// ~END IModuleInterface overrides.
	};
}	// namespace UE::LegacyVertexDeltaModel
