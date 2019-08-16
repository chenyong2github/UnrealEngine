// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackModuleSpacer.h"
#include "NiagaraActions.h"
#include "NiagaraTypes.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "DragAndDrop/AssetDragDropOp.h"

void UNiagaraStackModuleSpacer::Initialize(FRequiredEntryData InRequiredEntryData, ENiagaraScriptUsage InScriptUsage, FName InSpacerKey, float InSpacerScale, EStackRowStyle InRowStyle)
{
	Super::Initialize(InRequiredEntryData, InSpacerKey, InSpacerScale, InRowStyle);
	ItemGroupScriptUsage = InScriptUsage;
}

FReply UNiagaraStackModuleSpacer::OnStackSpacerDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	if (DragDropOperation->IsOfType<FNiagaraStackDragOperation>())
	{
		TSharedPtr<FNiagaraStackDragOperation> InputDragDropOperation = StaticCastSharedPtr<FNiagaraStackDragOperation>(DragDropOperation);
		TSharedPtr<FNiagaraParameterAction> Action = StaticCastSharedPtr<FNiagaraParameterAction>(InputDragDropOperation->GetAction());
		
		OnStackSpacerAcceptDrop.ExecuteIfBound(this, Action->GetParameter());
		return FReply::Handled();
	}
	else if (DragDropOperation->IsOfType<FAssetDragDropOp>())
	{
		TSharedPtr<FAssetDragDropOp> InputDragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(DragDropOperation);
		for (const FAssetData& AssetData : InputDragDropOp->GetAssets())
		{
			OnStackSpacerAssetDrop.ExecuteIfBound(this, AssetData);
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

bool UNiagaraStackModuleSpacer::OnStackSpacerAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	if (DragDropOperation->IsOfType<FNiagaraStackDragOperation>())
	{
		TSharedPtr<FNiagaraStackDragOperation> InputDragDropOperation = StaticCastSharedPtr<FNiagaraStackDragOperation>(DragDropOperation);
		TSharedPtr<FNiagaraParameterAction> Action = StaticCastSharedPtr<FNiagaraParameterAction>(InputDragDropOperation->GetAction());
		if (Action.IsValid())
		{
			return FNiagaraStackGraphUtilities::ParameterIsCompatibleWithScriptUsage(Action->GetParameter(), ItemGroupScriptUsage);
		}	
	}
	else if (DragDropOperation->IsOfType<FAssetDragDropOp>())
	{
		TSharedPtr<FAssetDragDropOp> InputDragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(DragDropOperation);
		for (const FAssetData& AssetData : InputDragDropOp->GetAssets())
		{
			if (OnStackSpacerRequestAssetDrop.IsBound() && OnStackSpacerRequestAssetDrop.Execute(this, AssetData) == false)
			{
				return false;
			}
		}
		return true;
	}
	return false;
}
