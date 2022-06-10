// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "IRenderPageCollectionEditor.h"
#include "Modules/ModuleManager.h"


class URenderPagesBlueprint;
class URenderPagePropsSourceBase;
enum class ERenderPagePropsSourceType : uint8;

namespace UE::RenderPages
{
	class IRenderPagePropsSourceWidgetFactory;
}

namespace UE::RenderPages::Private
{
	class SRenderPagesPropsBase;
}


namespace UE::RenderPages
{
	/**
	 * RenderPagesEditor module interface.
	 */
	class IRenderPagesEditorModule : public IModuleInterface, public IHasMenuExtensibility, public IHasToolBarExtensibility
	{
	public:
		/**
		 * Singleton-like access to IRenderPagesEditorModule.
		 *
		 * @return Returns RenderPagesEditorModule singleton instance, loading the module on demand if needed.
		 */
		static FORCEINLINE IRenderPagesEditorModule& Get()
		{
			return FModuleManager::LoadModuleChecked<IRenderPagesEditorModule>(TEXT("RenderPagesEditor"));
		}

		/** Creates an instance of the render page collection editor. */
		virtual TSharedRef<IRenderPageCollectionEditor> CreateRenderPageCollectionEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, URenderPagesBlueprint* InBlueprint) = 0;

		/** Creates a props source widget for the given props source. */
		virtual TSharedPtr<Private::SRenderPagesPropsBase> CreatePropsSourceWidget(URenderPagePropsSourceBase* PropsSource, TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor) = 0;

		/** Returns all the factories for creating widgets for props sources. */
		virtual const TMap<ERenderPagePropsSourceType, TSharedPtr<IRenderPagePropsSourceWidgetFactory>>& GetPropsSourceWidgetFactories() const = 0;
	};
}
