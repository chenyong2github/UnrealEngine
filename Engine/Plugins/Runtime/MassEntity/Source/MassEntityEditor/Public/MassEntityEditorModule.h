// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "AssetTypeCategories.h"
#include "Toolkits/IToolkitHost.h"
#include "Toolkits/AssetEditorToolkit.h"


class IMassEntityEditor;
class FAssetTypeActions_Base;
struct FGraphPanelNodeFactory;
class UMassSchematic;

/**
* The public interface to this module
*/
class MASSENTITYEDITOR_API FMassEntityEditorModule : public IModuleInterface, public IHasMenuExtensibility, public IHasToolBarExtensibility
{
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAssetPropertiesChanged, class UMassSchematic* /*MassSchematic*/, const FPropertyChangedEvent& /*PropertyChangedEvent*/);

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Creates an instance of Mass editor. Only virtual so that it can be called across the DLL boundary. */
	virtual TSharedRef<IMassEntityEditor> CreateMassEntityEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UMassSchematic* MassSchematic);

	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override { return MenuExtensibilityManager; }
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override { return ToolBarExtensibilityManager; }

	TSharedPtr<struct FGraphNodeClassHelper> GetProcassorClassCache() { return ProcessorClassCache; }
	
	FOnAssetPropertiesChanged& GetOnAssetPropertiesChanged() { return OnAssetPropertiesChanged; }

protected:
	TSharedPtr<struct FGraphNodeClassHelper> ProcessorClassCache;

	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;

	TArray<TSharedPtr<FAssetTypeActions_Base>> ItemDataAssetTypeActions;

	FOnAssetPropertiesChanged OnAssetPropertiesChanged;
};
