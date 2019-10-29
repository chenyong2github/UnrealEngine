// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraTypes.h"
#include "NiagaraGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "GraphEditorDragDropAction.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "NiagaraActions.generated.h"

USTRUCT()
struct NIAGARAEDITOR_API FNiagaraMenuAction : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	DECLARE_DELEGATE(FOnExecuteStackAction);
	DECLARE_DELEGATE_RetVal(bool, FCanExecuteStackAction);

	FNiagaraMenuAction()
	{
	}

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

private:
	FOnExecuteStackAction Action;
	FCanExecuteStackAction CanPerformAction;
};

struct NIAGARAEDITOR_API FNiagaraParameterAction : public FEdGraphSchemaAction
{
	FNiagaraParameterAction() {}
	FNiagaraParameterAction(const FNiagaraVariable& InParameter,
		const TArray<FNiagaraGraphParameterReferenceCollection>& InReferenceCollection,
		FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords, int32 InSectionID = 0);

	const FNiagaraVariable& GetParameter() const { return Parameter; }

	FNiagaraVariable Parameter;

	TArray<FNiagaraGraphParameterReferenceCollection> ReferenceCollection;
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