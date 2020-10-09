// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IControlRigEditor.h"
#include "ControlRigEditorEditMode.h"
#include "AssetEditorModeManager.h"
#include "DragAndDrop/GraphNodeDragDropOp.h"
#include "ControlRigDefines.h"
#include "Units/RigUnitContext.h"
#include "ControlRigLog.h"
#include "IPersonaViewport.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMCore/RigVM.h"
#include "Graph/SControlRigGraphPinNameListValueWidget.h"
#include "Styling/SlateTypes.h"
#include "AnimPreviewInstance.h"
#include "ScopedTransaction.h"
#include "Graph/ControlRigGraphNode.h"

class UControlRigBlueprint;
class IPersonaToolkit;
class SWidget;
class SBorder;
class USkeletalMesh;
class FStructOnScope;

UENUM()
enum class EControlRigEditorEventQueue : uint8
{
	/** Setup Event */
	Setup,

	/** Update Event */
	Update,

	/** Inverse Event */
	Inverse,

	/** Inverse -> Update */
	InverseAndUpdate,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

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
	virtual bool TransactionObjectAffectsBlueprint(UObject* InTransactedObject) override;
	virtual bool CanAddNewLocalVariable() const override { return false; }
	virtual void DeleteSelectedNodes() override;
	virtual bool CanDeleteNodes() const override;

	virtual void CopySelectedNodes() override;
	virtual bool CanCopyNodes() const override;

	virtual void PasteNodes() override;
	virtual bool CanPasteNodes() const override;

	// FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	void EnsureValidRigElementInDetailPanel();

	virtual void OnStartWatchingPin();
	virtual bool CanStartWatchingPin() const;

	virtual void OnStopWatchingPin();
	virtual bool CanStopWatchingPin() const;

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

	void SetDetailStruct(const FRigElementKey& InElement);

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
	void OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bForce = false);
	void OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName);
	void OnRigElementReparented(FRigHierarchyContainer* Container, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName);
	void OnRigElementSelected(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bSelected);
	void OnControlUISettingChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey);

	void OnGraphNodeDropToPerform(TSharedPtr<FGraphNodeDragDropOp> DragDropOp, UEdGraph* Graph, const FVector2D& NodePosition, const FVector2D& ScreenPosition);

	FPersonaViewportKeyDownDelegate& GetKeyDownDelegate() { return OnKeyDownDelegate; }
	FNewMenuDelegate& OnViewportContextMenu() { return OnViewportContextMenuDelegate; }
	FNewMenuCommandsDelegate& OnViewportContextMenuCommands() { return OnViewportContextMenuCommandsDelegate; }

	DECLARE_EVENT_OneParam(FControlRigEditor, FPreviewControlRigUpdated, FControlRigEditor*);
	FPreviewControlRigUpdated& OnPreviewControlRigUpdated() { return PreviewControlRigUpdated;  }

private:

	virtual void OnCreateComment() override;

protected:

	void OnHierarchyChanged();
	void OnControlsSettingsChanged();

	void SynchronizeViewportBoneSelection();

	// FBlueprintEditor Interface
	virtual void CreateDefaultCommands() override;
	virtual void OnCreateGraphEditorCommands(TSharedPtr<FUICommandList> GraphEditorCommandsList);
	virtual void Compile() override;
	virtual void SaveAsset_Execute() override;
	virtual void SaveAssetAs_Execute() override;
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

	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);
	void HandleVMCompiledEvent(UBlueprint* InBlueprint, URigVM* InVM);
	void HandleControlRigExecutedEvent(UControlRig* InControlRig, const EControlRigState InState, const FName& InEventName);

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

	EControlRigEditorEventQueue GetEventQueue() const;
	void SetEventQueue(EControlRigEditorEventQueue InEventQueue);
	int32 GetEventQueueComboValue() const;
	FText GetEventQueueLabel() const;
	static FSlateIcon GetEventQueueIcon(EControlRigEditorEventQueue InEventQueue);
	FSlateIcon GetEventQueueIcon() const;
	void OnEventQueueComboChanged(int32 InValue, ESelectInfo::Type InSelectInfo);
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
	bool IsToolbarDrawSpacesEnabled() const;
	ECheckBoxState GetToolbarDrawSpaces() const;
	void OnToolbarDrawSpacesChanged(ECheckBoxState InNewValue);
	ECheckBoxState GetToolbarDrawAxesOnSelection() const;
	void OnToolbarDrawAxesOnSelectionChanged(ECheckBoxState InNewValue);
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

	/** Update stale watch pins */
	void UpdateStaleWatchedPins();

	/** Wraps the normal blueprint editor's action menu creation callback */
	FActionMenuContent HandleCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed);
	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged);

	void ToggleExecuteGraph();
	bool IsExecuteGraphOn() const;
	void ToggleAutoCompileGraph();
	bool IsAutoCompileGraphOn() const;
	bool CanAutoCompileGraph() const { return true; }
	void ToggleEventQueue();
	TSharedRef<SWidget> GenerateEventQueueMenuContent();

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

	 void HandleOnControlModified(UControlRig* Subject, const FRigControl& Control, const FRigControlModifiedContext& Context);

	 void HandleRefreshEditorFromBlueprint(UControlRigBlueprint* InBlueprint);

	 void HandleVariableDroppedFromBlueprint(UObject* InSubject, FProperty* InVariableToDrop, const FVector2D& InDropPosition, const FVector2D& InScreenPosition);

	 void OnGraphNodeClicked(UControlRigGraphNode* InNode);

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

	/** preview animation instance */
	UAnimPreviewInstance* PreviewInstance;

	/** Delegate to deal with key down evens in the viewport / editor */
	FPersonaViewportKeyDownDelegate OnKeyDownDelegate;

	/** Delgate to build the context menu for the viewport */
	FNewMenuDelegate OnViewportContextMenuDelegate;
	void HandleOnViewportContextMenuDelegate(class FMenuBuilder& MenuBuilder);
	FNewMenuCommandsDelegate OnViewportContextMenuCommandsDelegate;
	TSharedPtr<FUICommandList> HandleOnViewportContextMenuCommandsDelegate();

	/** Bone Selection related */
	FTransform GetRigElementTransform(const FRigElementKey& InElement, bool bLocal, bool bOnDebugInstance) const;
	void SetRigElementTransform(const FRigElementKey& InElement, const FTransform& InTransform, bool bLocal);
	
	// FNotifyHook Interface
	virtual void NotifyPreChange(FProperty* PropertyAboutToChange) override;
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
	/** delegate for changing property */
	virtual void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent) override;

	bool bControlRigEditorInitialized;
	bool bIsSettingObjectBeingDebugged;
	FRigElementKey RigElementInDetailPanel;
	TSharedPtr<FStructOnScope> StructToDisplay;

	TArray<uint8, TAlignedHeapAllocator<16>> NodeDetailBuffer;
	UScriptStruct* NodeDetailStruct;
	FName NodeDetailName;

	/** Currently executing ControlRig or not - later maybe this will change to enum for whatever different mode*/
	bool bExecutionControlRig;

	/** The log to use for errors resulting from the init phase of the units */
	FControlRigLog ControlRigLog;
	/** Once the log is collected update the graph */
	void UpdateGraphCompilerErrors();

	/** This can be used to enable dumping of a unit test */
	void DumpUnitTestCode();

	void OnAnimInitialized();

	/** Are we currently in setup mode */
	bool bSetupModeEnabled;

	FPreviewControlRigUpdated PreviewControlRigUpdated;

	TSharedPtr<SControlRigGraphPinNameListValueWidget> PinControlNameList;
	bool IsPinControlNameListEnabled() const;
	TSharedRef<SWidget> MakePinControlNameListItemWidget(TSharedPtr<FString> InItem);
	FText GetPinControlNameListText() const;
	TSharedPtr<FString> GetPinControlCurrentlySelectedItem(const TArray<TSharedPtr<FString>>* InNameList) const;
	void SetPinControlNameListText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/);
	void OnPinControlNameListChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnPinControlNameListComboBox(const TArray<TSharedPtr<FString>>* InNameList);
	void ToggleSetupMode();

	bool bFirstTimeSelecting;
	bool bAnyErrorsLeft;

	EControlRigEditorEventQueue LastEventQueue;
	FString LastDebuggedRig;

	friend class FControlRigEditorMode;
	friend class SControlRigStackView;
	friend class SRigHierarchy;
};