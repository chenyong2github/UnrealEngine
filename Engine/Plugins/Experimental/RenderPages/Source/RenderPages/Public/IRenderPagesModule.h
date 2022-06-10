// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"


class URenderPagePropsSourceBase;
enum class ERenderPagePropsSourceType : uint8;

namespace UE::RenderPages
{
	class FRenderPageManager;
	class IRenderPagePropsSourceFactory;
}


namespace UE::RenderPages
{
	/**
	* RenderPages module interface.
	*/
	class IRenderPagesModule : public IModuleInterface
	{
	public:
		/**
		 * Singleton-like access to IRenderPagesModule.
		 *
		 * @return Returns RenderPagesModule singleton instance, loading the module on demand if needed.
		 */
		static IRenderPagesModule& Get()
		{
			static const FName ModuleName = "RenderPages";
			return FModuleManager::LoadModuleChecked<IRenderPagesModule>(ModuleName);
		}

		/**
		 * Singleton-like access to FRenderPageManager. Will error if this module hasn't started (or has stopped).
		 *
		 * @return Returns RenderPageManager singleton instance.
		 */
		virtual FRenderPageManager& GetManager() const = 0;

		/** Creates a URenderPagePropsSourceBase instance, based on the given ERenderPagePropsSourceType. */
		virtual URenderPagePropsSourceBase* CreatePropsSource(UObject* Outer, ERenderPagePropsSourceType PropsSourceType, UObject* PropsSourceOrigin) = 0;
		
		/** Returns all set IRenderPagePropsSourceFactory instances, these are used to create URenderPagePropsSourceBase instances based on a given ERenderPagePropsSourceType. */
		virtual const TMap<ERenderPagePropsSourceType, TSharedPtr<IRenderPagePropsSourceFactory>>& GetPropsSourceFactories() const = 0;
	};
}
