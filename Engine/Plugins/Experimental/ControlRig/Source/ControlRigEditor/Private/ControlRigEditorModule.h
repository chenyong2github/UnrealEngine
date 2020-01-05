// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "UObject/WeakObjectPtr.h"
#include "IControlRigEditorModule.h"
#include "IControlRigModule.h"

class UBlueprint;
class IAssetTypeActions;
class UMaterial;
class UAnimSequence;
class USkeletalMesh;
class FToolBarBuilder;
class FExtender;
class FUICommandList;
class UMovieSceneTrack;
class FControlRigGraphPanelNodeFactory;
class FControlRigGraphPanelPinFactory;

class FControlRigEditorModule : public IControlRigEditorModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** IControlRigEditorModule interface */
	virtual TSharedRef<IControlRigEditor> CreateControlRigEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UControlRigBlueprint* Blueprint) override;
	DECLARE_DELEGATE_RetVal_TwoParams(TSharedRef<FExtender>, FControlRigEditorToolbarExtender, const TSharedRef<FUICommandList> /*InCommandList*/, TSharedRef<IControlRigEditor> /*InControlRigEditor*/);
	virtual TArray<FControlRigEditorToolbarExtender>& GetAllControlRigEditorToolbarExtenders() override { return ControlRigEditorToolbarExtenders; }
	/** IHasMenuExtensibility interface */
	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override { return MenuExtensibilityManager; }

	/** IHasToolBarExtensibility interface */
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override { return ToolBarExtensibilityManager; }


	
	virtual void RegisterRigUnitEditorClass(FName RigUnitClassName, TSubclassOf<URigUnitEditor_Base> Class) override;
	virtual void UnregisterRigUnitEditorClass(FName RigUnitClassName) override;
	
	virtual void GetTypeActions(const UControlRigBlueprint* CRB, FBlueprintActionDatabaseRegistrar& ActionRegistrar) override;
	virtual void GetInstanceActions(const UControlRigBlueprint* CRB, FBlueprintActionDatabaseRegistrar& ActionRegistrar) override;
	virtual FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) override;
	virtual void GetNodeContextMenuActions(const UControlRigGraphNode* Node, class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual void GetContextMenuActions(const UControlRigGraphSchema* Schema, class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;

	static TSubclassOf<URigUnitEditor_Base> GetEditorObjectByRigUnit(const FName& RigUnitClassName);

private:
	/** Handle a new animation controller blueprint being created */
	void HandleNewBlueprintCreated(UBlueprint* InBlueprint);

	/** Handle for our sequencer control rig parameter track editor */
	FDelegateHandle ControlRigParameterTrackCreateEditorHandle;

	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;

	/** StaticClass is not safe on shutdown, so we cache the name, and use this to unregister on shut down */
	TArray<FName> ClassesToUnregisterOnShutdown;
	TArray<FName> PropertiesToUnregisterOnShutdown;

	/** Extensibility managers */
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;
	TArray<FControlRigEditorToolbarExtender> ControlRigEditorToolbarExtenders;

	/** Node factory for the control rig graph */
	TSharedPtr<FControlRigGraphPanelNodeFactory> ControlRigGraphPanelNodeFactory;

	/** Pin factory for the control rig graph */
	TSharedPtr<FControlRigGraphPanelPinFactory> ControlRigGraphPanelPinFactory;

	/** Delegate handles for blueprint utils */
	FDelegateHandle RefreshAllNodesDelegateHandle;
	FDelegateHandle ReconstructAllNodesDelegateHandle;
	FDelegateHandle RenameVariableReferencesDelegateHandle;

	/** Rig Unit Editor Classes Handler */
	static TMap<FName, TSubclassOf<URigUnitEditor_Base>> RigUnitEditorClasses;
};