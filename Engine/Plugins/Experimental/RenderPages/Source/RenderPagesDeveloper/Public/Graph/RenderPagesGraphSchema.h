// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2_Actions.h"
#include "GraphEditorDragDropAction.h"
#include "RenderPagesGraphSchema.generated.h"


class URenderPagesBlueprint;
class URenderPagesGraph;


namespace UE::RenderPages
{
	/**
	 * A FStringSetNameValidator child class for the RenderPages modules, for validating local variable names.
	 *
	 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
	 */
	class RENDERPAGESDEVELOPER_API FRenderPagesLocalVariableNameValidator : public FStringSetNameValidator
	{
	public:
		FRenderPagesLocalVariableNameValidator(const UBlueprint* Blueprint, const URenderPagesGraph* Graph, FName InExistingName = NAME_None);
	};

	/**
	 * A FStringSetNameValidator child class for the RenderPages modules, for validating names.
	 *
	 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
	 */
	class RENDERPAGESDEVELOPER_API FRenderPagesNameValidator : public FStringSetNameValidator
	{
	public:
		FRenderPagesNameValidator(const UBlueprint* Blueprint, const UStruct* ValidationScope, FName InExistingName = NAME_None);
	};
}


/**
 * A FEdGraphSchemaAction_BlueprintVariableBase child class for the RenderPages modules, for local variables.
 *
 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
 */
USTRUCT()
struct RENDERPAGESDEVELOPER_API FRenderPagesGraphSchemaAction_LocalVar : public FEdGraphSchemaAction_BlueprintVariableBase
{
	GENERATED_BODY()

public:
	/* Returns the type ID. Returns the same string every time. */
	static FName StaticGetTypeId()
	{
		static FName Type("FRenderPagesGraphSchemaAction_LocalVar");
		return Type;
	}

	FRenderPagesGraphSchemaAction_LocalVar() = default;
	FRenderPagesGraphSchemaAction_LocalVar(const FText& InNodeCategory, const FText& InMenuDesc, const FText& InToolTip, const int32 InGrouping, const int32 InSectionID)
		: FEdGraphSchemaAction_BlueprintVariableBase(InNodeCategory, InMenuDesc, InToolTip, InGrouping, InSectionID)
	{}

	//~ Begin FEdGraphSchemaAction interface
	virtual FName GetTypeId() const override { return StaticGetTypeId(); }
	virtual bool IsA(const FName& InType) const override { return (InType == GetTypeId()) || (InType == FEdGraphSchemaAction_BlueprintVariableBase::StaticGetTypeId()); }
	virtual bool IsValidName(const FName& NewName, FText& OutErrorMessage) const override;
	//~ End FEdGraphSchemaAction interface
};


/**
 * A FEdGraphSchemaAction child class for the RenderPages modules, for the promote-to-variable action.
 *
 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
 */
USTRUCT()
struct RENDERPAGESDEVELOPER_API FRenderPagesGraphSchemaAction_PromoteToVariable : public FEdGraphSchemaAction
{
	GENERATED_BODY()

public:
	/* Returns the type ID. Returns the same string every time. */
	static FName StaticGetTypeId()
	{
		static FName Type("FRenderPagesGraphSchemaAction_PromoteToVariable");
		return Type;
	}

	FRenderPagesGraphSchemaAction_PromoteToVariable() = default;
	FRenderPagesGraphSchemaAction_PromoteToVariable(UEdGraphPin* InEdGraphPin, bool InLocalVariable);

	//~ Begin FEdGraphSchemaAction interface
	virtual FName GetTypeId() const override { return StaticGetTypeId(); }
	virtual bool IsA(const FName& InType) const override { return (InType == GetTypeId()); }
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode) override;
	//~ End FEdGraphSchemaAction interface

private:
	TObjectPtr<UEdGraphPin> EdGraphPin = nullptr;
	bool bLocalVariable = false;
};


namespace UE::RenderPages
{
	/**
	 * A FGraphSchemaActionDragDropAction child class for the RenderPages modules, for drag and dropping an item from the blueprints tree (like a variable or a function).
	 *
	 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
	 */
	class RENDERPAGESDEVELOPER_API FRenderPagesFunctionDragDropAction : public FGraphSchemaActionDragDropAction
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FRenderPagesFunctionDragDropAction, FGraphSchemaActionDragDropAction)

	protected:
		FRenderPagesFunctionDragDropAction();

	public:
		/** Set if operation is modified by alt. */
		void SetAltDrag(const bool bInIsAltDrag) { bAltDrag = bInIsAltDrag; }

		/** Set if operation is modified by the ctrl key. */
		void SetCtrlDrag(const bool bInIsCtrlDrag) { bControlDrag = bInIsCtrlDrag; }

	public:
		/** Creates and returns a new instance of this class. */
		static TSharedRef<FRenderPagesFunctionDragDropAction> New(TSharedPtr<FEdGraphSchemaAction> InAction, URenderPagesBlueprint* InRenderPagesBlueprint, URenderPagesGraph* InRenderPagesGraph);

	protected:
		TObjectPtr<URenderPagesBlueprint> SourceRenderPagesBlueprint;
		TObjectPtr<URenderPagesGraph> SourceRenderPagesGraph;
		bool bControlDrag;
		bool bAltDrag;

		friend class URenderPagesGraphSchema;
	};
}


/**
 * A UEdGraphSchema child class for the RenderPages modules.
 *
 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
 */
UCLASS()
class RENDERPAGESDEVELOPER_API URenderPagesGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	URenderPagesGraphSchema();

	//~ Begin UEdGraphSchema Interface
	virtual void GetGraphDisplayInformation(const UEdGraph& Graph, /*out*/ FGraphDisplayInfo& DisplayInfo) const override;

	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;

	virtual bool CanDuplicateGraph(UEdGraph* InSourceGraph) const override { return false; }

	virtual bool CanGraphBeDropped(TSharedPtr<FEdGraphSchemaAction> InAction) const override;
	virtual FReply BeginGraphDragAction(TSharedPtr<FEdGraphSchemaAction> InAction, const FPointerEvent& MouseEvent = FPointerEvent()) const override;

	virtual bool CanVariableBeDropped(UEdGraph* InGraph, FProperty* InVariableToDrop) const override;
	virtual bool RequestVariableDropOnPanel(UEdGraph* InGraph, FProperty* InVariableToDrop, const FVector2D& InDropPosition, const FVector2D& InScreenPosition) override;
	virtual bool RequestVariableDropOnPin(UEdGraph* InGraph, FProperty* InVariableToDrop, UEdGraphPin* InPin, const FVector2D& InDropPosition, const FVector2D& InScreenPosition) override;

	virtual void InsertAdditionalActions(TArray<UBlueprint*> InBlueprints, TArray<UEdGraph*> InGraphs, TArray<UEdGraphPin*> InPins, FGraphActionListBuilderBase& OutAllActions) const override;

	virtual TSharedPtr<INameValidatorInterface> GetNameValidator(const UBlueprint* BlueprintObj, const FName& OriginalName, const UStruct* ValidationScope, const FName& ActionTypeId) const override;
	//~ End UEdGraphSchema Interface

public:
	/** Name constant. */
	static const FName GraphName_RenderPages;
};
