// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IRenderPagesEditorModule.h"


class IAssetTypeActions;
class IToolkitHost;
class URenderPageCollection;
class URenderPagesBlueprint;

namespace UE::RenderPages::Private
{
	class FRenderPageCollectionEditor;
}


namespace UE::RenderPages::Private
{
	class FRenderPagesEditorModule : public IRenderPagesEditorModule
	{
	public:
		//~ Begin IModuleInterface interface
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
		//~ End IModuleInterface interface

		//~ Begin IRenderPagesEditorModule interface
		virtual TSharedRef<IRenderPageCollectionEditor> CreateRenderPageCollectionEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, URenderPagesBlueprint* InBlueprint) override;

		virtual TSharedPtr<SRenderPagesPropsBase> CreatePropsSourceWidget(URenderPagePropsSourceBase* PropsSource, TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor) override;
		virtual const TMap<ERenderPagePropsSourceType, TSharedPtr<IRenderPagePropsSourceWidgetFactory>>& GetPropsSourceWidgetFactories() const override { return PropsSourceWidgetFactories; }
		//~ End IRenderPagesEditorModule interface

		/** Gets the extensibility managers for outside entities to extend gui page editor's menus. */
		virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override { return MenuExtensibilityManager; }

		/** Gets the extensibility managers for outside entities to extend gui page editor's and toolbars. */
		virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override { return ToolBarExtensibilityManager; }

	private:
		/** Handle a new render pages blueprint being created. */
		void HandleNewBlueprintCreated(UBlueprint* InBlueprint);

		void RegisterPropsSourceWidgetFactories();
		void UnregisterPropsSourceWidgetFactories();
		void RegisterPropsSourceWidgetFactory(const ERenderPagePropsSourceType PropsSourceType, const TSharedPtr<IRenderPagePropsSourceWidgetFactory>& InFactory);
		void UnregisterPropsSourceWidgetFactory(const ERenderPagePropsSourceType PropsSourceType);

	private:
		TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
		TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;

		TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;

		TMap<ERenderPagePropsSourceType, TSharedPtr<IRenderPagePropsSourceWidgetFactory>> PropsSourceWidgetFactories;
	};
}
