// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StaticMeshEditorExtensionModule.h"

#include "PolygonEditingToolbar.h"
#include "PolygonSelectionTool.h"
#include "UVTools/UVGenerationFlattenMappingTool.h"
#include "UVTools/UVGenerationToolbar.h"
#include "UVTools/UVGenerationTool.h"
#include "UVTools/UVGenerationSettings.h"

#include "ContentBrowserModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "StaticMeshEditorModule.h"

#define LOCTEXT_NAMESPACE "StaticMeshEditorExtensionModule"

class FStaticMeshEditorExtensionModule : public IModuleInterface
{
private:
	static FDelegateHandle StaticMeshEditorExtenderHandle;
	static FDelegateHandle StaticMeshEditorSecondaryExtenderHandle;
	static FDelegateHandle StaticMeshEditorOpenedHandle;
	static FDelegateHandle ContentBrowserExtenderDelegateHandle;

public:
	FStaticMeshEditorExtensionModule()
	{
	}

	// IModuleInterface overrides
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

private:
	static void SetupContentBrowserContextMenuExtender();
	static void RemoveContentBrowserContextMenuExtender();
	static TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);

	static void OnStaticMeshEditorOpened(TWeakPtr<IStaticMeshEditor> StaticMeshEditorPtr);
	static TSharedRef<FExtender> ExtendStaticMeshEditorToolbar(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> Objects);
	static TSharedRef<FExtender> ExtendStaticMeshEditorSecondaryToolbar(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> Objects);
};

FDelegateHandle FStaticMeshEditorExtensionModule::StaticMeshEditorExtenderHandle;
FDelegateHandle FStaticMeshEditorExtensionModule::StaticMeshEditorSecondaryExtenderHandle;
FDelegateHandle FStaticMeshEditorExtensionModule::StaticMeshEditorOpenedHandle;
FDelegateHandle FStaticMeshEditorExtensionModule::ContentBrowserExtenderDelegateHandle;

void FStaticMeshEditorExtensionModule::StartupModule()
{
	// IMPORTANT: The call to FModuleManager:;LoadModuleChecked on "MeshProcessingLibrary" is mandatory to expose "MeshProcessingLibrary" thru scripting.
	IModuleInterface& ModuleInterface = FModuleManager::Get().LoadModuleChecked<IModuleInterface>("MeshProcessingLibrary");

	if (!IsRunningCommandlet())
	{
		FEditorModeRegistry::Get().RegisterMode<FPolygonSelectionTool>(
			FPolygonSelectionTool::EM_PolygonSelection,
			NSLOCTEXT("StaticMeshEditorExtension", "StaticMeshEditorExtensionEditMode", "Edit Mode in StaticMeshEditor"),
			FSlateIcon(),
			false);

		FEditorModeRegistry::Get().RegisterMode<FUVGenerationTool>(
			FUVGenerationTool::EM_UVGeneration,
			NSLOCTEXT("StaticMeshEditorExtension", "StaticMeshEditorExtensionGenerateUVMode", "Generate UV Mode in StaticMeshEditor"),
			FSlateIcon(),
			false);

		IStaticMeshEditorModule& StaticMeshEditorModule = FModuleManager::Get().LoadModuleChecked<IStaticMeshEditorModule>("StaticMeshEditor");
		TArray<FAssetEditorExtender>& ToolbarExtenders = StaticMeshEditorModule.GetToolBarExtensibilityManager()->GetExtenderDelegates();
		TArray<FAssetEditorExtender>& SecondaryToolbarExtenders = StaticMeshEditorModule.GetSecondaryToolBarExtensibilityManager()->GetExtenderDelegates();

		ToolbarExtenders.Add(FAssetEditorExtender::CreateStatic(&FStaticMeshEditorExtensionModule::ExtendStaticMeshEditorToolbar));
		SecondaryToolbarExtenders.Add(FAssetEditorExtender::CreateStatic(&FStaticMeshEditorExtensionModule::ExtendStaticMeshEditorSecondaryToolbar));
		StaticMeshEditorOpenedHandle = StaticMeshEditorModule.OnStaticMeshEditorOpened().AddStatic(OnStaticMeshEditorOpened);
		
		StaticMeshEditorExtenderHandle = ToolbarExtenders.Last().GetHandle();
		StaticMeshEditorSecondaryExtenderHandle = SecondaryToolbarExtenders.Last().GetHandle();

		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomPropertyTypeLayout("UVGenerationSettings", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FUVGenerationSettingsCustomization::MakeInstance));

		SetupContentBrowserContextMenuExtender();
	}
}

void FStaticMeshEditorExtensionModule::ShutdownModule()
{
	FEditorModeRegistry::Get().UnregisterMode(FPolygonSelectionTool::EM_PolygonSelection);
	FEditorModeRegistry::Get().UnregisterMode(FUVGenerationTool::EM_UVGeneration);

	IStaticMeshEditorModule* StaticMeshEditorModule = FModuleManager::Get().GetModulePtr<IStaticMeshEditorModule>("StaticMeshEditor");
	if (StaticMeshEditorModule)
	{
		if (StaticMeshEditorExtenderHandle.IsValid())
		{
			StaticMeshEditorModule->GetToolBarExtensibilityManager()->GetExtenderDelegates().RemoveAll([=](const auto& In) { return In.GetHandle() == StaticMeshEditorExtenderHandle; });
		}
		if (StaticMeshEditorSecondaryExtenderHandle.IsValid())
		{
			StaticMeshEditorModule->GetSecondaryToolBarExtensibilityManager()->GetExtenderDelegates().RemoveAll([=](const auto& In) { return In.GetHandle() == StaticMeshEditorSecondaryExtenderHandle; });
		}
		if (StaticMeshEditorOpenedHandle.IsValid())
		{
			StaticMeshEditorModule->OnStaticMeshEditorOpened().Remove(StaticMeshEditorOpenedHandle);
		}
	}

	FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropertyModule)
	{
		PropertyModule->UnregisterCustomPropertyTypeLayout("UVGenerationSettings");
	}

	RemoveContentBrowserContextMenuExtender();
}

void FStaticMeshEditorExtensionModule::OnStaticMeshEditorOpened(TWeakPtr<IStaticMeshEditor> StaticMeshEditorPtr)
{
	FUVGenerationToolbar::CreateTool(StaticMeshEditorPtr);
}

void FStaticMeshEditorExtensionModule::SetupContentBrowserContextMenuExtender()
{
	if (!IsRunningCommandlet())
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray< FContentBrowserMenuExtender_SelectedAssets >& CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();

		CBMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateStatic(&FStaticMeshEditorExtensionModule::OnExtendContentBrowserAssetSelectionMenu));
		ContentBrowserExtenderDelegateHandle = CBMenuExtenderDelegates.Last().GetHandle();
	}
}

void FStaticMeshEditorExtensionModule::RemoveContentBrowserContextMenuExtender()
{
	if (ContentBrowserExtenderDelegateHandle.IsValid() && FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
		CBMenuExtenderDelegates.RemoveAll(
			[](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
		{
			return Delegate.GetHandle() == ContentBrowserExtenderDelegateHandle;
		}
		);
	}
}

TSharedRef<FExtender> FStaticMeshEditorExtensionModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	return FUVGenerationFlattenMappingTool::OnExtendContentBrowserAssetSelectionMenu(SelectedAssets);
}

TSharedRef<FExtender> FStaticMeshEditorExtensionModule::ExtendStaticMeshEditorToolbar(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> Objects)
{
	checkf(Objects.Num() && Objects[0]->IsA<UStaticMesh>(), TEXT("Invalid object for static mesh editor toolbar extender"));

	TSharedRef<FExtender> Extender = MakeShared<FExtender>();
	// Add extension to StaticMesh Editor's toolbar
	Extender->AddMenuExtension(
		"UVActionOptions",
		EExtensionHook::First,
		CommandList,
		FMenuExtensionDelegate::CreateStatic(&FUVGenerationToolbar::CreateUVMenu, CommandList, Cast<UStaticMesh>(Objects[0]))
	);

	Extender->AddMenuExtension(
		"UVActionOptions",
		EExtensionHook::First,
		CommandList,
		FMenuExtensionDelegate::CreateStatic(&FUVGenerationFlattenMappingToolbar::CreateMenu, CommandList, Cast<UStaticMesh>(Objects[0]))
	);

	return Extender;
}

TSharedRef<FExtender> FStaticMeshEditorExtensionModule::ExtendStaticMeshEditorSecondaryToolbar(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> Objects)
{
	checkf(Objects.Num() && Objects[0]->IsA<UStaticMesh>(), TEXT("Invalid object for static mesh editor toolbar extender"));

	TSharedRef<FExtender> Extender = MakeShared<FExtender>();
		MakeShareable(new FExtender);

	// Add extension to StaticMesh Editor's secondary toolbar
	Extender->AddToolBarExtension(
		"Extensions",
		EExtensionHook::After,
		CommandList,
		FToolBarExtensionDelegate::CreateStatic(&FPolygonEditingToolbar::CreateToolbar, CommandList, Cast<UStaticMesh>(Objects[0]) )
	);

	return Extender;
}

IMPLEMENT_MODULE( FStaticMeshEditorExtensionModule, StaticMeshEditorExtension )

#undef LOCTEXT_NAMESPACE