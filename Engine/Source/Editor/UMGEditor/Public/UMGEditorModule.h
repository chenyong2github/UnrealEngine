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
