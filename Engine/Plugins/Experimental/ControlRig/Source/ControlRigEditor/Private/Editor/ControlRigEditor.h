// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IControlRigEditor.h"
#include "ControlRigEditorEditMode.h"
#include "AssetEditorModeManager.h"
#include "DragAndDrop/GraphNodeDragDropOp.h"
#include "ControlRigDefines.h"
#include "ControlRigLog.h"
#include "ControlRigModel.h"
#include "IPersonaViewport.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

class UControlRigBlueprint;
class IPersonaToolkit;
class SWidget;
class SBorder;
class USkeletalMesh;
class FStructOnScope;

struct FControlRigEditorModes
{
	// Mode constants
	static const FName ControlRigEditorMode;
	static FText GetLocalizedMode(const FName InMode)
	{
		static TMap< FName, FText > LocModes;

		if (LocModes.Num() == 0)
		{
			LocModes.Add(ControlRigEditorMode, NSLOCTEXT("ControlRigEditorModes", "ControlRigEditorMode", "Rigging"));
		}

		check(InMode != NAME_None);
		const FText* OutDesc = LocModes.Find(InMode);
		check(OutDesc);
		return *OutDesc;
	}
private:
	FControlRigEditorModes() {}
};

class FControlRigEditor : public IControlRigEditor
{
public:
	/**
	 * Edits the specified character asset(s)
	 *
	 * @param	Mode					Mode that this editor should operate in
	 * @param	InitToolkitHost			When Mode is WorldCentric, this is the level editor instance to spawn this editor within
	 * @param	InControlRigBlueprint	The blueprint object to start editing.
	 */
	void InitControlRigEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, class UControlRigBlueprint* InControlRigBlueprint);

public:
	FControlRigEditor();
	virtual ~FControlRigEditor();

	// FBlueprintEditor interface
	virtual UBlueprint* GetBlueprintObj() const override;

public:
	// IToolkit Interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;	
	virtual FString GetDocumentationLink() const override
	{
		return FString(TEXT("Engine/Animation/ControlRig"));
	}

	// BlueprintEditor interface
	virtual bool CanAddNewLocalVariable() const override { return false; }
	virtual void DeleteSelectedNodes();
	virtual void PasteNodesHere(class UEdGraph* DestinationGraph, const FVector2D& GraphLocation) override;

	// IToolkitHost Interface
	virtual void OnToolkitHostingStarted(const TSharedRef<class IToolkit>& Toolkit) override;
	virtual void OnToolkitHostingFinished(const TSharedRef<class IToolkit>& Toolkit) override;

	//  FTickableEditorObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// Gets the Control Rig Blueprint being edited/viewed
	UControlRigBlueprint* GetControlRigBlueprint() const;

	void SetDetailObjects(const TArray<UObject*>& InObjects);

	void SetDetailObject(UObject* Obj);

	void SetDetailStruct(const FRigElementKey& InElement, TSharedPtr<FStructOnScope> StructToDisplay);

	void ClearDetailObject();

	/** Get the persona toolkit */
	TSharedRef<IPersonaToolkit> GetPersonaToolkit() const { return PersonaToolkit.ToSharedRef(); }

	/** Get the toolbox hosting widget */
	TSharedRef<SBorder> GetToolbox() { return Toolbox.ToSharedRef(); }

	/** Get the edit mode */
	FControlRigEditorEditMode* GetEditMode() const;

	// this changes everytime you compile, so don't cache it expecting it will last. 
	UControlRig* GetInstanceRig() const { return ControlRig;  }

	void OnCurveContainerChanged();

	void OnRigElementAdded(FRigHierarchyContainer* Container, const FRigElementKey& InKey);
	void OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey);
	void OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName);
	void OnRigElementReparented(FRigHierarchyContainer* Container, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName);
	void OnRigElementSelected(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bSelected);
	void OnControlUISettingChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey);

	void OnGraphNodeDropToPerform(TSharedPtr<FGraphNodeDragDropOp> DragDropOp, UEdGraph* Graph, const FVector2D& NodePosition, const FVector2D& ScreenPosition);

	FPersonaViewportKeyDownDelegate& GetKeyDownDelegate() { return OnKeyDownDelegate; }
	FNewMenuDelegate& OnViewportContextMenu() { return OnViewportContextMenuDelegate; }
	FNewMenuCommandsDelegate& OnViewportContextMenuCommands() { return OnViewportContextMenuCommandsDelegate; }

protected:

	void OnHierarchyChanged();
	void OnControlsSettingsChanged();

	// FBlueprintEditor Interface
	virtual void CreateDefaultCommands() override;
	virtual void OnCreateGraphEditorCommands(TSharedPtr<FUICommandList> GraphEditorCommandsList);
	virtual void Compile() override;
	virtual bool IsInAScriptingMode() const override { return true; }
	virtual void CreateDefaultTabContents(const TArray<UBlueprint*>& InBlueprints) override;
	virtual bool IsSectionVisible(NodeSectionID::Type InSectionID) const override;
	virtual FGraphAppearanceInfo GetGraphAppearance(class UEdGraph* InGraph) const override;
	virtual bool IsEditable(UEdGraph* InGraph) const override;
	virtual bool IsCompilingEnabled() const override;
	virtual FText GetGraphDecorationString(UEdGraph* InGraph) const override;
	virtual void OnActiveTabChanged( TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated ) override;
	virtual void OnSelectedNodesChangedImpl(const TSet<class UObject*>& NewSelection) override;
	virtual void OnBlueprintChangedImpl(UBlueprint* InBlueprint, bool bIsJustBeingCompiled) override;
	virtual void SetupGraphEditorEvents(UEdGraph* InGraph, SGraphEditor::FGraphEditorEvents& InEvents) override;

	// FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// FNotifyHook Interface
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

	void HandleModelModified(const UControlRigModel* InModel, EControlRigModelNotifType InType, const void* InPayload);

	// FGCObject Interface
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override;

	void BindCommands();

protected:
	/** Undo Action**/
	void UndoAction();

	/** Redo Action **/
	void RedoAction();

private:
	/** Extend menu */
	void ExtendMenu();

	/** Extend toolbar */
	void ExtendToolbar();

	/** Fill the toolbar with content */
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);

	virtual void GetCustomDebugObjects(TArray<FCustomDebugObject>& DebugList) const override;
	virtual bool OnlyShowCustomDebugObjects() const override { return true; }
	virtual void HandleSetObjectBeingDebugged(UObject* InObject) override;
	virtual FString GetCustomDebugObjectLabel(UObject* ObjectBeingDebugged) const override;

	/** Handle hiding items in the graph */
	void HandleHideItem();
	bool CanHideItem() const;

	/** Handle preview scene setup */
	void HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene);
	void HandleViewportCreated(const TSharedRef<class IPersonaViewport>& InViewport);
	TOptional<float> GetToolbarAxesScale() const;
	void OnToolbarAxesScaleChanged(float InValue);

		/** Handle switching skeletal meshes */
	void HandlePreviewMeshChanged(USkeletalMesh* InOldSkeletalMesh, USkeletalMesh* InNewSkeletalMesh);

	/** Push a newly compiled/opened control rig to the edit mode */
	void UpdateControlRig();

	/** Update the name lists for use in name combo boxes */
	void CacheNameLists();

	/** Rebind our anim instance to the preview's skeletal mesh component */
	void RebindToSkeletalMeshComponent();

	/** Wraps the normal blueprint editor's action menu creation callback */
	FActionMenuContent HandleCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed);
	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged);

	void ToggleExecuteGraph();
	bool IsExecuteGraphOn() const;

	enum ERigElementGetterSetterType
	{
		ERigElementGetterSetterType_Transform,
		ERigElementGetterSetterType_Rotation,
		ERigElementGetterSetterType_Translation,
		ERigElementGetterSetterType_Initial,
		ERigElementGetterSetterType_Relative,
		ERigElementGetterSetterType_Offset,
		ERigElementGetterSetterType_Name
	};

	 void HandleMakeElementGetterSetter(ERigElementGetterSetterType Type, bool bIsGetter, TArray<FRigElementKey> Keys, UEdGraph* Graph, FVector2D NodePosition);

protected:

	/** Toolbox hosting widget */
	TSharedPtr<SBorder> Toolbox;

	/** Persona toolkit used to support skeletal mesh preview */
	TSharedPtr<IPersonaToolkit> PersonaToolkit;

	/** The extender to pass to the level editor to extend it's window menu */
	TSharedPtr<FExtender> MenuExtender;

	/** Toolbar extender */
	TSharedPtr<FExtender> ToolbarExtender;

	/** Preview instance inspector widget */
	TSharedPtr<SWidget> PreviewEditor;

	/** Our currently running control rig instance */
	UControlRig* ControlRig;

	/** preview scene */
	TSharedPtr<IPersonaPreviewScene> PreviewScene;

	/** Delegate to deal with key down evens in the viewport / editor */
	FPersonaViewportKeyDownDelegate OnKeyDownDelegate;

	/** Delgate to build the context menu for the viewport */
	FNewMenuDelegate OnViewportContextMenuDelegate;
	void HandleOnViewportContextMenuDelegate(class FMenuBuilder& MenuBuilder);
	FNewMenuCommandsDelegate OnViewportContextMenuCommandsDelegate;
	TSharedPtr<FUICommandList> HandleOnViewportContextMenuCommandsDelegate();

	/** Bone Selection related */
	FTransform GetRigElementTransform(const FRigElementKey& InElement, bool bLocal) const;
	void SetRigElementTransform(const FRigElementKey& InElement, const FTransform& InTransform, bool bLocal);
	
	/** delegate for changing property */
	virtual void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent) override;

	bool bControlRigEditorInitialized;
	bool bIsSelecting;
	bool bIsSettingObjectBeingDebugged;
	FRigElementKey RigElementInDetailPanel;

	/** Currently executing ControlRig or not - later maybe this will change to enum for whatever different mode*/
	bool bExecutionControlRig;

	/** The log to use for errors resulting from the init phase of the units */
	FControlRigLog ControlRigLog;
	/** Once the log is collected update the graph */
	void UpdateGraphCompilerErrors();

	/** This can be used to enable dumping of a unit test */
	void DumpUnitTestCode();

	friend class FControlRigEditorMode;
	friend class SControlRigStackView;
	friend class SRigHierarchy;
};