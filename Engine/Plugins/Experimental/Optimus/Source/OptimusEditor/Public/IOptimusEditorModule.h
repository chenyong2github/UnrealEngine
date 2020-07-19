// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Modules/ModuleManager.h"


DECLARE_LOG_CATEGORY_EXTERN(LogOptimusEditor, Log, All);

class IOptimusEditor;
class UOptimusDeformer;

class IOptimusEditorModule
	: public IModuleInterface
//	, public IHasMenuExtensibility
//	, public IHasToolBarExtensibility
{
public:
	static FORCEINLINE IOptimusEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked< IOptimusEditorModule >(TEXT("OptimusEditor"));
	}

	/// Creates an instance of a Control Rig editor.
	/// @param Mode				Mode that this editor should operate in
	/// @param InitToolkitHost	When Mode is WorldCentric, this is the level editor instance to spawn this editor within
	/// @param DeformerObject	The deformer object to start editing.
	///	@return Interface to the new Optimus Deformer editor
	virtual TSharedRef<IOptimusEditor> CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UOptimusDeformer* DeformerObject) = 0;

	/** Get all toolbar extenders */
	// DECLARE_DELEGATE_RetVal_TwoParams(TSharedRef<FExtender>, FControlRigEditorToolbarExtender, const TSharedRef<FUICommandList> /*InCommandList*/, TSharedRef<IControlRigEditor> /*InControlRigEditor*/);
	// virtual TArray<FControlRigEditorToolbarExtender>& GetAllControlRigEditorToolbarExtenders() = 0;

	// virtual void GetTypeActions(const UControlRigBlueprint* CRB, FBlueprintActionDatabaseRegistrar& ActionRegistrar) = 0;
	// virtual FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) = 0;
	// virtual void GetNodeContextMenuActions(const UControlRigGraphNode* Node, class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const = 0;
	// virtual void GetContextMenuActions(const UControlRigGraphSchema* Schema, class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const = 0;
};
