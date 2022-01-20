// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "IHasDesignerExtensibility.h"
#include "IHasPropertyBindingExtensibility.h"

extern const FName UMGEditorAppIdentifier;

class FUMGEditor;
class FWidgetBlueprintApplicationMode;
class FWorkflowAllowedTabSet;

DECLARE_DELEGATE_RetVal_ThreeParams(TSharedPtr<FExtender>, FOnExtendBindingsMenuForProperty, const class UWidgetBlueprint*, const class UWidget*, const class FProperty*);

/** The public interface of the UMG editor module. */
class IUMGEditorModule : 
	public IModuleInterface, 
	public IHasMenuExtensibility, 
	public IHasToolBarExtensibility, 
	public IHasDesignerExtensibility,
	public IHasPropertyBindingExtensibility
{
public:
	virtual class FWidgetBlueprintCompiler* GetRegisteredCompiler() = 0;

	DECLARE_EVENT_TwoParams(IUMGEditorModule, FOnRegisterTabs, const FWidgetBlueprintApplicationMode&, FWorkflowAllowedTabSet&);
	virtual FOnRegisterTabs& OnRegisterTabsForEditor() = 0;
};
