// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

VERTEXDELTAMODEL_API DECLARE_LOG_CATEGORY_EXTERN(LogVertexDeltaModel, Log, All);

namespace UE::VertexDeltaModel
{
	class VERTEXDELTAMODEL_API FVertexDeltaModelModule
		: public IModuleInterface
	{
	public:
		// IModuleInterface overrides.
		virtual void StartupModule() override;
		// ~END IModuleInterface overrides.
	};
}	// namespace VertexDeltaModel
