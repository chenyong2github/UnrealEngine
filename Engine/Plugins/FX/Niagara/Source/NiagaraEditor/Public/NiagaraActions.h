// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "GraphEditorDragDropAction.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "NiagaraEditorCommon.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraActions.generated.h"

USTRUCT()
struct NIAGARAEDITOR_API FNiagaraMenuAction : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	DECLARE_DELEGATE(FOnExecuteStackAction);
	DECLARE_DELEGATE_RetVal(bool, FCanExecuteStackAction);

	FNiagaraMenuAction() {}
	FNiagaraMenuAction(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords, FOnExecuteStackAction InAction, int32 InSectionID = 0);
	FNiagaraMenuAction(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords, FOnExecuteStackAction InAction, FCanExecuteStackAction InCanPerformAction, int32 InSectionID = 0);

	void ExecuteAction()
	{
		if (CanExecute())
		{
			Action.ExecuteIfBound();
		}
	}

	bool CanExecute() const
	{
		// Fire the 'can execute' delegate if we have one, otherwise always return true
		return CanPerformAction.IsBound() ? CanPerformAction.Execute() : true;
	}

	bool IsExperimental = false;

	TOptional<FNiagaraVariable> GetParameterVariable() const;
	void SetParamterVariable(const FNiagaraVariable& InParameterVariable);

private:
	TOptional<FNiagaraVariable> ParameterVariable;
	FOnExecuteStackAction Action;
	FCanExecuteStackAction CanPerformAction;
};

struct NIAGARAEDITOR_API FNiagaraScriptVarAndViewInfoAction : public FEdGraphSchemaAction
{
	FNiagaraScriptVarAndViewInfoAction(const FNiagaraScriptVariableAndViewInfo& InScriptVariableAndViewInfo,
		FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords, int32 InSectionID = 0);

	const FNiagaraTypeDefinition GetScriptVarType() const { return ScriptVariableAndViewInfo.ScriptVariable.GetType(); };

	FNiagaraScriptVariableAndViewInfo ScriptVariableAndViewInfo;
};

struct NIAGARAEDITOR_API FNiagaraParameterAction : public FEdGraphSchemaAction
{
	FNiagaraParameterAction()
		: bIsExternallyReferenced(false)
	{
	}

	FNiagaraParameterAction(const FNiagaraVariable& InParameter,
		const TArray<FNiagaraGraphParameterReferenceCollection>& InReferenceCollection,
		FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords,
		TSharedPtr<TArray<FName>> ParameterWithNamespaceModifierRenamePending,
		int32 InSectionID = 0);

	FNiagaraParameterAction(const FNiagaraVariable& InParameter, 
		FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords,
		TSharedPtr<TArray<FName>> ParameterWithNamespaceModifierRenamePending,
		int32 InSectionID = 0);

	const FNiagaraVariable& GetParameter() const { return Parameter; }

	bool GetIsNamespaceModifierRenamePending() const;

	void SetIsNamespaceModifierRenamePending(bool bIsNamespaceModifierRenamePending);

	FNiagaraVariable Parameter;

	TArray<FNiagaraGraphParameterReferenceCollection> ReferenceCollection;

	bool bIsExternallyReferenced;

private:
	TWeakPtr<TArray<FName>> ParameterWithNamespaceModifierRenamePendingWeak;
};

struct NIAGARAEDITOR_API FNiagaraScriptParameterAction : public FEdGraphSchemaAction
{
	FNiagaraScriptParameterAction() {}
	FNiagaraScriptParameterAction(const FNiagaraVariable& InVariable, const FNiagaraVariableMetaData& InVariableMetaData);
};

class NIAGARAEDITOR_API FNiagaraParameterGraphDragOperation : public FGraphSchemaActionDragDropAction
{
public:
	DRAG_DROP_OPERATOR_TYPE(FNiagaraParameterGraphDragOperation, FGraphSchemaActionDragDropAction)

	static TSharedRef<FNiagaraParameterGraphDragOperation> New(const TSharedPtr<FEdGraphSchemaAction>& InActionNode);

	// FGraphEditorDragDropAction interface
	virtual void HoverTargetChanged() override;
	virtual FReply DroppedOnNode(FVector2D ScreenPosition, FVector2D GraphPosition) override;
	virtual FReply DroppedOnPanel(const TSharedRef< class SWidget >& Panel, FVector2D ScreenPosition, FVector2D GraphPosition, UEdGraph& Graph) override;
	// End of FGraphEditorDragDropAction

	/** Set if operation is modified by alt */
	void SetAltDrag(bool InIsAltDrag) { bAltDrag = InIsAltDrag; }

	/** Set if operation is modified by the ctrl key */
	void SetCtrlDrag(bool InIsCtrlDrag) { bControlDrag = InIsCtrlDrag; }

	/** Returns true if the drag operation is currently hovering over the supplied node */
	bool IsCurrentlyHoveringNode(const UEdGraphNode* TestNode) const;

protected:
	/** Constructor */
	FNiagaraParameterGraphDragOperation();

	/** Structure for required node construction parameters */
	struct FNiagaraParameterNodeConstructionParams
	{
		FVector2D GraphPosition;
		UEdGraph* Graph;
		FNiagaraVariable Parameter;
	};

	static void MakeGetMap(FNiagaraParameterNodeConstructionParams InParams);
	static void MakeSetMap(FNiagaraParameterNodeConstructionParams InParams);

	virtual EVisibility GetIconVisible() const override;
	virtual EVisibility GetErrorIconVisible() const override;

	/** Was ctrl held down at start of drag */
	bool bControlDrag;
	/** Was alt held down at the start of drag */
	bool bAltDrag;
};

class NIAGARAEDITOR_API FNiagaraParameterDragOperation : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FNiagaraParameterDragOperation, FDecoratedDragDropOp)

	FNiagaraParameterDragOperation(TSharedPtr<FEdGraphSchemaAction> InSourceAction)
		: SourceAction(InSourceAction)
	{
	}

	TSharedPtr<FEdGraphSchemaAction> GetSourceAction() const { return SourceAction; }

private:
	TSharedPtr<FEdGraphSchemaAction> SourceAction;
};