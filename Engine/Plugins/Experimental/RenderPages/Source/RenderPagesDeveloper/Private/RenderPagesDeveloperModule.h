// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IRenderPagesDeveloperModule.h"


namespace UE::RenderPages::Private
{
	/**
	 * The implementation of the IRenderPagesDeveloperModule interface.
	 */
	class FRenderPagesDeveloperModule : public IRenderPagesDeveloperModule
	{
	public:
		//~ Begin IModuleInterface
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
		//~ End IModuleInterface
	};
}
