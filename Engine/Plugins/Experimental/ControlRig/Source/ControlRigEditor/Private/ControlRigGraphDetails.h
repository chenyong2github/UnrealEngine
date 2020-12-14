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

class FMobilityCustomization;
class IDetailLayoutBuilder;

class FControlRigArgumentGroupLayout : public IDetailCustomNodeBuilder, public TSharedFromThis<FControlRigArgumentGroupLayout>
{
public:
	FControlRigArgumentGroupLayout(URigVMGraph* InGraph, UControlRigBlueprint* InBlueprint, bool bInputs);
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
	bool bIsInputGroup;
	FSimpleDelegate OnRebuildChildren;

};

class FControlRigArgumentLayout : public IDetailCustomNodeBuilder, public TSharedFromThis<FControlRigArgumentLayout>
{
public:

	FControlRigArgumentLayout(URigVMPin* InPin, URigVMGraph* InGraph, UControlRigBlueprint* InBlueprint)
		: PinPtr(InPin)
		, GraphPtr(InGraph)
		, ControlRigBlueprintPtr(InBlueprint)
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

	/** Callbacks for all the functionality for modifying arguments */
	void OnRemoveClicked();
	FReply OnArgMoveUp();
	FReply OnArgMoveDown();

	FText OnGetArgNameText() const;
	FText OnGetArgToolTipText() const;
	void OnArgNameChange(const FText& InNewText);
	void OnArgNameTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit);

	FEdGraphPinType OnGetPinInfo() const;
	void PinInfoChanged(const FEdGraphPinType& PinType);
	void OnPrePinInfoChange(const FEdGraphPinType& PinType);

	/** Returns the graph pin representing this variable */
	UEdGraphPin* GetPin() const;

	/** Returns whether the "Pass-by-Reference" checkbox is checked or not */
	ECheckBoxState IsRefChecked() const;

	/** Handles toggling the "Pass-by-Reference" checkbox */
	void OnRefCheckStateChanged(ECheckBoxState InState);

private:

	/** The argument pin that this layout reflects */
	TWeakObjectPtr<URigVMPin> PinPtr;
	
	/** The target graph that this argument is on */
	TWeakObjectPtr<URigVMGraph> GraphPtr;

	/** The blueprint we are editing */
	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprintPtr;

	/** Holds a weak pointer to the argument name widget, used for error notifications */
	TWeakPtr<SEditableTextBox> ArgumentNameWidget;

	/** The SGraphPin widget created to show/edit default value */
	TSharedPtr<SGraphPin> DefaultValuePinWidget;
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

private:
	/** The Blueprint editor we are embedded in */
	TWeakPtr<IControlRigEditor> ControlRigEditorPtr;

	/** The blueprint we are editing */
	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprintPtr;

	/** The graph we are editing */
	TWeakObjectPtr<UControlRigGraph> GraphPtr;
};