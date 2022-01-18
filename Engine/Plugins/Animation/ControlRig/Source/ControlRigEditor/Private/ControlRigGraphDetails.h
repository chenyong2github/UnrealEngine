// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IDetailCustomNodeBuilder.h"
#include "UObject/WeakObjectPtr.h"
#include "Graph/ControlRigGraph.h"
#include "ControlRigBlueprint.h"
#include "ControlRigEditor.h"
#include "SGraphPin.h"
#include "Graph/SControlRigGraphNode.h"
#include "Widgets/Colors/SColorBlock.h"
#include "DetailsViewWrapperObject.h"
#include "Graph/ControlRigGraphSchema.h"

class IDetailLayoutBuilder;

class FControlRigArgumentGroupLayout : public IDetailCustomNodeBuilder, public TSharedFromThis<FControlRigArgumentGroupLayout>
{
public:
	FControlRigArgumentGroupLayout(
		URigVMGraph* InGraph, 
		UControlRigBlueprint* InBlueprint,
		TWeakPtr<IControlRigEditor> InEditor,
		bool bInputs);
	virtual ~FControlRigArgumentGroupLayout();

private:
	/** IDetailCustomNodeBuilder Interface*/
	virtual void SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren) override { OnRebuildChildren = InOnRegenerateChildren; }
	virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) override {}
	virtual void GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder) override;
	virtual void Tick(float DeltaTime) override {}
	virtual bool RequiresTick() const override { return false; }
	virtual FName GetName() const override { return NAME_None; }
	virtual bool InitiallyCollapsed() const override { return false; }

private:

	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	TWeakObjectPtr<URigVMGraph> GraphPtr;
	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprintPtr;
	TWeakPtr<IControlRigEditor> ControlRigEditorPtr;
	bool bIsInputGroup;
	FSimpleDelegate OnRebuildChildren;
};

class FControlRigArgumentLayout : public IDetailCustomNodeBuilder, public TSharedFromThis<FControlRigArgumentLayout>
{
public:

	FControlRigArgumentLayout(
		URigVMPin* InPin, 
		URigVMGraph* InGraph, 
		UControlRigBlueprint* InBlueprint,
		TWeakPtr<IControlRigEditor> InEditor)
		: PinPtr(InPin)
		, GraphPtr(InGraph)
		, ControlRigBlueprintPtr(InBlueprint)
		, ControlRigEditorPtr(InEditor)
		, NameValidator(InBlueprint, InGraph, InPin->GetFName())
	{}

private:

	/** IDetailCustomNodeBuilder Interface*/
	virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) override;
	virtual void GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder) override;
	virtual void Tick(float DeltaTime) override {}
	virtual bool RequiresTick() const override { return false; }
	virtual FName GetName() const override { return PinPtr.Get()->GetFName(); }
	virtual bool InitiallyCollapsed() const override { return true; }

private:

	/** Determines if this pin should not be editable */
	bool ShouldPinBeReadOnly(bool bIsEditingPinType = false) const;

	/** Determines if editing the pins on the node should be read only */
	bool IsPinEditingReadOnly(bool bIsEditingPinType = false) const;

	/** Determines if an argument can be moved up or down */
	bool CanArgumentBeMoved(bool bMoveUp) const;

	/** Callbacks for all the functionality for modifying arguments */
	void OnRemoveClicked();
	FReply OnArgMoveUp();
	FReply OnArgMoveDown();

	FText OnGetArgNameText() const;
	FText OnGetArgToolTipText() const;
	void OnArgNameTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit);

	FEdGraphPinType OnGetPinInfo() const;
	void PinInfoChanged(const FEdGraphPinType& PinType);
	void OnPrePinInfoChange(const FEdGraphPinType& PinType);

private:

	/** The argument pin that this layout reflects */
	TWeakObjectPtr<URigVMPin> PinPtr;
	
	/** The target graph that this argument is on */
	TWeakObjectPtr<URigVMGraph> GraphPtr;

	/** The blueprint we are editing */
	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprintPtr;

	/** The editor we are editing */
	TWeakPtr<IControlRigEditor> ControlRigEditorPtr;

	/** Holds a weak pointer to the argument name widget, used for error notifications */
	TWeakPtr<SEditableTextBox> ArgumentNameWidget;

	/** The validator to check if a name for an argument is valid */
	FControlRigLocalVariableNameValidator NameValidator;
};

class FControlRigArgumentDefaultNode : public IDetailCustomNodeBuilder, public TSharedFromThis<FControlRigArgumentDefaultNode>
{
public:
	FControlRigArgumentDefaultNode(
		URigVMGraph* InGraph,
		UControlRigBlueprint* InBlueprint
	);
	virtual ~FControlRigArgumentDefaultNode();

private:
	/** IDetailCustomNodeBuilder Interface*/
	virtual void SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren) override { OnRebuildChildren = InOnRegenerateChildren; }
	virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) override {}
	virtual void GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder) override;
	virtual void Tick(float DeltaTime) override {}
	virtual bool RequiresTick() const override { return false; }
	virtual FName GetName() const override { return NAME_None; }
	virtual bool InitiallyCollapsed() const override { return false; }

private:

	void OnGraphChanged(const FEdGraphEditAction& InAction);
	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	TWeakObjectPtr<URigVMGraph> GraphPtr;
	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprintPtr;
	FSimpleDelegate OnRebuildChildren;
	TSharedPtr<SControlRigGraphNode> OwnedNodeWidget;
	FDelegateHandle GraphChangedDelegateHandle;
};


/** Customization for editing Control Rig graphs */
class FControlRigGraphDetails : public IDetailCustomization
{
public:

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor);

	FControlRigGraphDetails(TSharedPtr<IControlRigEditor> InControlRigEditor, UControlRigBlueprint* ControlRigBlueprint)
		: ControlRigEditorPtr(InControlRigEditor)
		, ControlRigBlueprintPtr(ControlRigBlueprint)
	{}

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

	bool IsAddNewInputOutputEnabled() const;
	EVisibility GetAddNewInputOutputVisibility() const;
	FReply OnAddNewInputClicked();
	FReply OnAddNewOutputClicked();
	FText GetNodeCategory() const;
	void SetNodeCategory(const FText& InNewText, ETextCommit::Type InCommitType);
	FText GetNodeKeywords() const;
	void SetNodeKeywords(const FText& InNewText, ETextCommit::Type InCommitType);
	FText GetNodeDescription() const;
	void SetNodeDescription(const FText& InNewText, ETextCommit::Type InCommitType);
	FLinearColor GetNodeColor() const;
	void SetNodeColor(FLinearColor InColor, bool bSetupUndoRedo);
	void OnNodeColorBegin();
	void OnNodeColorEnd();
	void OnNodeColorCancelled(FLinearColor OriginalColor);
	FReply OnNodeColorClicked();
	FText GetCurrentAccessSpecifierName() const;
	void OnAccessSpecifierSelected( TSharedPtr<FString> SpecifierName, ESelectInfo::Type SelectInfo );
	TSharedRef<ITableRow> HandleGenerateRowAccessSpecifier( TSharedPtr<FString> SpecifierName, const TSharedRef<STableViewBase>& OwnerTable );

private:

	/** The Blueprint editor we are embedded in */
	TWeakPtr<IControlRigEditor> ControlRigEditorPtr;

	/** The graph we are editing */
	TWeakObjectPtr<UControlRigGraph> GraphPtr;

	/** The blueprint we are editing */
	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprintPtr;

	/** The color block widget */
	TSharedPtr<SColorBlock> ColorBlock;

	/** The color to change */
	FLinearColor TargetColor;

	/** The color array to change */
	TArray<FLinearColor*> TargetColors;

	/** Set to true if the UI is currently picking a color */
	bool bIsPickingColor;

	static TArray<TSharedPtr<FString>> AccessSpecifierStrings;
};

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

/** Customization for editing a Control Rig node */
class FControlRigWrappedNodeDetails : public IDetailCustomization
{
public:

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
};

#endif