// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderPagesEditorModule.h"

#include "AssetTypeActions/RenderPagesBlueprintActions.h"
#include "Blueprints/RenderPagesBlueprint.h"
#include "Commands/RenderPagesEditorCommands.h"
#include "Factories/RenderPageCollectionFactory.h"
#include "Factories/RenderPagePropsSourceWidgetFactoryLocal.h"
#include "Factories/RenderPagePropsSourceWidgetFactoryRemoteControl.h"
#include "RenderPage/RenderPageCollection.h"
#include "Styles/RenderPagesEditorStyle.h"
#include "Toolkit/RenderPageCollectionEditor.h"

#include "IAssetTools.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "RenderPagesEditorModule"


void UE::RenderPages::Private::FRenderPagesEditorModule::StartupModule()
{
	FRenderPagesEditorStyle::Initialize();
	FRenderPagesEditorStyle::ReloadTextures();
	FRenderPagesEditorCommands::Register();

	MenuExtensibilityManager = MakeShared<FExtensibilityManager>();
	ToolBarExtensibilityManager = MakeShared<FExtensibilityManager>();

	RegisterPropsSourceWidgetFactories();

	// Register asset tools
	auto RegisterAssetTypeAction = [this](const TSharedRef<IAssetTypeActions>& InAssetTypeAction)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		RegisteredAssetTypeActions.Add(InAssetTypeAction);
		AssetTools.RegisterAssetTypeActions(InAssetTypeAction);
	};

	RegisterAssetTypeAction(MakeShared<FRenderPagesBlueprintActions>());

	// Register to fixup newly created BPs
	FKismetEditorUtilities::RegisterOnBlueprintCreatedCallback(this, URenderPageCollection::StaticClass(), FKismetEditorUtilities::FOnBlueprintCreated::CreateRaw(this, &FRenderPagesEditorModule::HandleNewBlueprintCreated));
}

void UE::RenderPages::Private::FRenderPagesEditorModule::ShutdownModule()
{
	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");

	if (AssetToolsModule)
	{
		for (TSharedRef<IAssetTypeActions> RegisteredAssetTypeAction : RegisteredAssetTypeActions)
		{
			AssetToolsModule->Get().UnregisterAssetTypeActions(RegisteredAssetTypeAction);
		}
	}

	UnregisterPropsSourceWidgetFactories();

	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();

	FRenderPagesEditorCommands::Unregister();
	FRenderPagesEditorStyle::Shutdown();
}

void UE::RenderPages::Private::FRenderPagesEditorModule::RegisterPropsSourceWidgetFactories()
{
	RegisterPropsSourceWidgetFactory(ERenderPagePropsSourceType::Local, MakeShared<FRenderPagePropsSourceWidgetFactoryLocal>());
	RegisterPropsSourceWidgetFactory(ERenderPagePropsSourceType::RemoteControl, MakeShared<FRenderPagePropsSourceWidgetFactoryRemoteControl>());
}

void UE::RenderPages::Private::FRenderPagesEditorModule::UnregisterPropsSourceWidgetFactories()
{
	UnregisterPropsSourceWidgetFactory(ERenderPagePropsSourceType::Local);
	UnregisterPropsSourceWidgetFactory(ERenderPagePropsSourceType::RemoteControl);
}

void UE::RenderPages::Private::FRenderPagesEditorModule::RegisterPropsSourceWidgetFactory(const ERenderPagePropsSourceType PropsSourceType, const TSharedPtr<IRenderPagePropsSourceWidgetFactory>& InFactory)
{
	PropsSourceWidgetFactories.Add(PropsSourceType, InFactory);
}

void UE::RenderPages::Private::FRenderPagesEditorModule::UnregisterPropsSourceWidgetFactory(const ERenderPagePropsSourceType PropsSourceType)
{
	PropsSourceWidgetFactories.Remove(PropsSourceType);
}

TSharedPtr<UE::RenderPages::Private::SRenderPagesPropsBase> UE::RenderPages::Private::FRenderPagesEditorModule::CreatePropsSourceWidget(URenderPagePropsSourceBase* PropsSource, TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor)
{
	if (!PropsSource)
	{
		return nullptr;
	}

	TSharedPtr<IRenderPagePropsSourceWidgetFactory>* FactoryPtr = PropsSourceWidgetFactories.Find(PropsSource->GetType());
	if (!FactoryPtr)
	{
		return nullptr;
	}

	TSharedPtr<IRenderPagePropsSourceWidgetFactory> Factory = *FactoryPtr;
	if (!Factory)
	{
		return nullptr;
	}

	return Factory->CreateInstance(PropsSource, BlueprintEditor);
}

TSharedRef<UE::RenderPages::IRenderPageCollectionEditor> UE::RenderPages::Private::FRenderPagesEditorModule::CreateRenderPageCollectionEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, URenderPagesBlueprint* InBlueprint)
{
	TSharedRef<FRenderPageCollectionEditor> NewRenderPagesEditor = MakeShared<FRenderPageCollectionEditor>();
	NewRenderPagesEditor->InitRenderPagesEditor(Mode, InitToolkitHost, InBlueprint);
	return NewRenderPagesEditor;
}

void UE::RenderPages::Private::FRenderPagesEditorModule::HandleNewBlueprintCreated(UBlueprint* InBlueprint)
{
	if (URenderPagesBlueprint* RenderPagesBlueprint = Cast<URenderPagesBlueprint>(InBlueprint))
	{
		RenderPagesBlueprint->PostLoad();
	}
}


#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(UE::RenderPages::Private::FRenderPagesEditorModule, RenderPagesEditor)
