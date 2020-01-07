// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackShaderStageGroup.h"
#include "NiagaraEmitter.h"
#include "NiagaraShaderStageBase.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "NiagaraScriptViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackErrorItem.h"
#include "NiagaraScriptSource.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraScriptMergeManager.h"
#include "NiagaraStackEditorData.h"
#include "ViewModels/NiagaraSystemViewModel.h"

#include "Internationalization/Internationalization.h"
#include "ScopedTransaction.h"
#include "IDetailTreeNode.h"

#define LOCTEXT_NAMESPACE "UNiagaraStackShaderStageGroup"

void UNiagaraStackShaderStagePropertiesItem::Initialize(FRequiredEntryData InRequiredEntryData, UNiagaraShaderStageBase* InShaderStage)
{
	checkf(ShaderStage.IsValid() == false, TEXT("Can not initialize more than once."));
	ShaderStage = InShaderStage;
	FString ShaderStageStackEditorDataKey = FString::Printf(TEXT("ShaderStage-%s-Properties"), *ShaderStage->Script->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens));
	Super::Initialize(InRequiredEntryData, ShaderStageStackEditorDataKey);

	ShaderStage->OnChanged().AddUObject(this, &UNiagaraStackShaderStagePropertiesItem::ShaderStagePropertiesChanged);
}

void UNiagaraStackShaderStagePropertiesItem::FinalizeInternal()
{
	if (ShaderStage.IsValid())
	{
		ShaderStage->OnChanged().RemoveAll(this);
	}
	Super::FinalizeInternal();
}

FText UNiagaraStackShaderStagePropertiesItem::GetDisplayName() const
{
	return FText::Format(LOCTEXT("ShaderStagePropertiesDisplayNameFormat", "{0} Settings"), ShaderStage->GetClass()->GetDisplayNameText());
}

bool UNiagaraStackShaderStagePropertiesItem::TestCanResetToBaseWithMessage(FText& OutCanResetToBaseMessage) const
{
	if (bCanResetToBaseCache.IsSet() == false)
	{
		if (HasBaseShaderStage())
		{
			UNiagaraEmitter* Emitter = GetEmitterViewModel()->GetEmitter();
			const UNiagaraEmitter* BaseEmitter = Emitter->GetParent();
			if (BaseEmitter != nullptr && Emitter != BaseEmitter)
			{
				TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
				bCanResetToBaseCache = MergeManager->IsShaderStagePropertySetDifferentFromBase(*Emitter, *BaseEmitter, ShaderStage->Script->GetUsageId());
			}
			else
			{
				bCanResetToBaseCache = false;
			}
		}
		else
		{
			bCanResetToBaseCache = false;
		}
	}
	if (bCanResetToBaseCache.GetValue())
	{
		OutCanResetToBaseMessage = LOCTEXT("CanResetToBase", "Reset this shader stage to the one defined in the parent emitter.");
		return true;
	}
	else
	{
		OutCanResetToBaseMessage = LOCTEXT("CanNotResetToBase", "No parent to reset to, or not different from parent.");
		return false;
	}
}

void UNiagaraStackShaderStagePropertiesItem::ResetToBase()
{
	FText Unused;
	if (TestCanResetToBaseWithMessage(Unused))
	{
		UNiagaraEmitter* Emitter = GetEmitterViewModel()->GetEmitter();
		const UNiagaraEmitter* BaseEmitter = Emitter->GetParent();
		TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
		MergeManager->ResetShaderStagePropertySetToBase(*Emitter, *BaseEmitter, ShaderStage->Script->GetUsageId());
		RefreshChildren();
	}
}

void UNiagaraStackShaderStagePropertiesItem::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (ShaderStageObject == nullptr)
	{
		ShaderStageObject = NewObject<UNiagaraStackObject>(this);
		ShaderStageObject->Initialize(CreateDefaultChildRequiredData(), ShaderStage.Get(), GetStackEditorDataKey());
	}

	NewChildren.Add(ShaderStageObject);

	bCanResetToBaseCache.Reset();
	bHasBaseShaderStageCache.Reset();

	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

void UNiagaraStackShaderStagePropertiesItem::ShaderStagePropertiesChanged()
{
	bCanResetToBaseCache.Reset();
}

bool UNiagaraStackShaderStagePropertiesItem::HasBaseShaderStage() const
{
	if (bHasBaseShaderStageCache.IsSet() == false)
	{
		UNiagaraEmitter* Emitter = GetEmitterViewModel()->GetEmitter();
		const UNiagaraEmitter* BaseEmitter = Emitter->GetParent();
		if (BaseEmitter != nullptr && Emitter != BaseEmitter)
		{
			TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
			bHasBaseShaderStageCache = MergeManager->HasBaseShaderStage(*BaseEmitter, ShaderStage->Script->GetUsageId());
		}
		else
		{
			bHasBaseShaderStageCache = false;
		}
	}
	return bHasBaseShaderStageCache.GetValue();
}

void UNiagaraStackShaderStageGroup::Initialize(
	FRequiredEntryData InRequiredEntryData,
	TSharedRef<FNiagaraScriptViewModel> InScriptViewModel,
	UNiagaraShaderStageBase* InShaderStage)
{
	ShaderStage = InShaderStage;
	ShaderStage->OnChanged().AddUObject(this, &UNiagaraStackShaderStageGroup::ShaderStagePropertiesChanged);

	FText ToolTip = LOCTEXT("ShaderStageGroupTooltip", "Defines properties and script modules for a shader stage.");
	FText DisplayName = FText::FromName(ShaderStage->ShaderStageName);
	Super::Initialize(InRequiredEntryData, DisplayName, ToolTip, InScriptViewModel, ENiagaraScriptUsage::ParticleShaderStageScript, ShaderStage->Script->GetUsageId());
}

UNiagaraShaderStageBase* UNiagaraStackShaderStageGroup::GetShaderStage() const
{
	return ShaderStage.Get();
}

void UNiagaraStackShaderStageGroup::FinalizeInternal()
{
	if (ShaderStage.IsValid())
	{
		ShaderStage->OnChanged().RemoveAll(this);
	}
	Super::FinalizeInternal();
}

void UNiagaraStackShaderStageGroup::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	bHasBaseShaderStageCache.Reset();

	UNiagaraEmitter* Emitter = GetEmitterViewModel()->GetEmitter();

	SetDisplayName(FText::FromName(ShaderStage->ShaderStageName));

	if (ShaderStageProperties == nullptr)
	{
		ShaderStageProperties = NewObject<UNiagaraStackShaderStagePropertiesItem>(this);
		ShaderStageProperties->Initialize(CreateDefaultChildRequiredData(), ShaderStage.Get());
	}
	NewChildren.Add(ShaderStageProperties);
	
	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackShaderStageGroup::CanDropInternal(const FDropRequest& DropRequest)
{
	if (DropRequest.DragDropOperation->IsOfType<FNiagaraStackEntryDragDropOp>() && 
		(DropRequest.DropZone == EItemDropZone::AboveItem || DropRequest.DropZone == EItemDropZone::BelowItem))
	{
		TSharedRef<const FNiagaraStackEntryDragDropOp> StackEntryDragDropOp = StaticCastSharedRef<const FNiagaraStackEntryDragDropOp>(DropRequest.DragDropOperation);
		bool ShaderStageEntriesDragged = false;
		for (UNiagaraStackEntry* DraggedEntry : StackEntryDragDropOp->GetDraggedEntries())
		{
			if (DraggedEntry->IsA<UNiagaraStackShaderStageGroup>())
			{
				ShaderStageEntriesDragged = true;
				break;
			}
		}

		if (ShaderStageEntriesDragged == false)
		{
			// Only handle dragged ShaderStage items.
			return TOptional<FDropRequestResponse>();
		}

		if (DropRequest.DropOptions != UNiagaraStackEntry::EDropOptions::Overview)
		{
			// Only allow dropping in the overview stacks.
			return FDropRequestResponse(TOptional<EItemDropZone>(), LOCTEXT("CantDropShaderStageOnStack", "Shader stages can only be dropped into the overview."));
		}

		if (StackEntryDragDropOp->GetDraggedEntries().Num() != 1)
		{
			// Only handle a single items.
			return FDropRequestResponse(TOptional<EItemDropZone>(), LOCTEXT("CantDropMultipleShaderStages", "Only single shader stages can be dragged and dropped."));
		}

		UNiagaraStackShaderStageGroup* SourceShaderStageGroup = CastChecked<UNiagaraStackShaderStageGroup>(StackEntryDragDropOp->GetDraggedEntries()[0]);
		if (DropRequest.DragOptions != EDragOptions::Copy && SourceShaderStageGroup->HasBaseShaderStage())
		{
			return FDropRequestResponse(TOptional<EItemDropZone>(), LOCTEXT("CantMoveShaderStageError", "This inherited shader stage can't be moved."));
		}

		if (SourceShaderStageGroup == this)
		{
			// Don't allow dropping on yourself.
			return TOptional<FDropRequestResponse>();
		}

		UNiagaraEmitter* OwningEmitter = GetEmitterViewModel()->GetEmitter();
		int32 SourceIndex = OwningEmitter->GetShaderStages().IndexOfByKey(SourceShaderStageGroup->GetShaderStage());
		if (SourceIndex == INDEX_NONE)
		{
			return FDropRequestResponse(TOptional<EItemDropZone>(), LOCTEXT("CantDropShaderStageFromOtherEmitterError", "This shader stage can't be moved here\nbecause it's owned by a different emitter."));
		}

		int32 TargetIndexOffset = DropRequest.DropZone == EItemDropZone::BelowItem ? 1 : 0;
		int32 TargetIndex = OwningEmitter->GetShaderStages().IndexOfByKey(ShaderStage.Get()) + TargetIndexOffset;
		if (SourceIndex < TargetIndex)
		{
			// If the source index is less than the target index, the target index will decrease by 1 after the source is removed.
			TargetIndex--;
		}

		if (SourceIndex == TargetIndex)
		{
			// Only handle the drag if the item would actually move.
			return TOptional<FDropRequestResponse>();
		}

		return FDropRequestResponse(DropRequest.DropZone, LOCTEXT("MoveShaderStageDragMessage", "Move this shader stage here."));
	}
	return TOptional<FDropRequestResponse>();
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackShaderStageGroup::DropInternal(const FDropRequest& DropRequest)
{
	UNiagaraEmitter* OwningEmitter = GetEmitterViewModel()->GetEmitter();

	TSharedRef<const FNiagaraStackEntryDragDropOp> StackEntryDragDropOp = StaticCastSharedRef<const FNiagaraStackEntryDragDropOp>(DropRequest.DragDropOperation);
	UNiagaraStackShaderStageGroup* SourceShaderStageGroup = CastChecked<UNiagaraStackShaderStageGroup>(StackEntryDragDropOp->GetDraggedEntries()[0]);

	int32 SourceIndex = OwningEmitter->GetShaderStages().IndexOfByKey(SourceShaderStageGroup->GetShaderStage());
	if (SourceIndex != INDEX_NONE)
	{
		int32 TargetOffset = DropRequest.DropZone == EItemDropZone::BelowItem ? 1 : 0;
		int32 TargetIndex = OwningEmitter->GetShaderStages().IndexOfByKey(ShaderStage.Get()) + TargetOffset;

		FScopedTransaction Transaction(FText::Format(LOCTEXT("MoveShaderStage", "Move Shader Stage {0}"), GetDisplayName()));
		OwningEmitter->MoveShaderStageToIndex(SourceShaderStageGroup->GetShaderStage(), TargetIndex);

		OnRequestFullRefreshDeferred().Broadcast();
		return FDropRequestResponse(DropRequest.DropZone, FText());
	}
	return TOptional<FDropRequestResponse>();
}

bool UNiagaraStackShaderStageGroup::TestCanDeleteWithMessage(FText& OutCanDeleteMessage) const
{
	if (HasBaseShaderStage())
	{
		OutCanDeleteMessage = LOCTEXT("CantDeleteInherited", "Can not delete this shader stage because it's inherited.");
		return false;
	}
	else
	{
		OutCanDeleteMessage = LOCTEXT("CanDelete", "Delete this shader stage.");
		return true;
	}
}

void UNiagaraStackShaderStageGroup::Delete()
{
	TSharedPtr<FNiagaraScriptViewModel> ScriptViewModelPinned = ScriptViewModel.Pin();
	checkf(ScriptViewModelPinned.IsValid(), TEXT("Can not delete when the script view model has been deleted."));

	UNiagaraEmitter* Emitter = GetEmitterViewModel()->GetEmitter();
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Emitter->GraphSource);

	if (!Source || !Source->NodeGraph)
	{
		return;
	}

	FScopedTransaction Transaction(FText::Format(LOCTEXT("DeleteShaderStage", "Delete {0}"), GetDisplayName()));

	Emitter->Modify();
	Source->NodeGraph->Modify();
	TArray<UNiagaraNode*> ShaderStageNodes;
	Source->NodeGraph->BuildTraversal(ShaderStageNodes, GetScriptUsage(), GetScriptUsageId());
	for (UNiagaraNode* Node : ShaderStageNodes)
	{
		Node->Modify();
	}
	
	// First, remove the shader stage object.
	Emitter->RemoveShaderStage(ShaderStage.Get());
	
	// Now remove all graph nodes associated with the shader stage.
	for (UNiagaraNode* Node : ShaderStageNodes)
	{
		Node->DestroyNode();
	}

	// Set the emitter here to that the internal state of the view model is updated.
	// TODO: Move the logic for managing additional scripts into the emitter view model or script view model.
	ScriptViewModelPinned->SetScripts(Emitter);
	
	OnModifiedShaderStagesDelegate.ExecuteIfBound();
}

void UNiagaraStackShaderStageGroup::ShaderStagePropertiesChanged()
{
	SetDisplayName(FText::FromName(ShaderStage->ShaderStageName));
}

bool UNiagaraStackShaderStageGroup::HasBaseShaderStage() const
{
	if (bHasBaseShaderStageCache.IsSet() == false)
	{
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		bHasBaseShaderStageCache = BaseEmitter != nullptr && FNiagaraScriptMergeManager::Get()->HasBaseShaderStage(*BaseEmitter, GetScriptUsageId());
	}
	return bHasBaseShaderStageCache.GetValue();
}

void UNiagaraStackShaderStageGroup::SetOnModifiedShaderStages(FOnModifiedShaderStages OnModifiedShaderStages)
{
	OnModifiedShaderStagesDelegate = OnModifiedShaderStages;
}

#undef LOCTEXT_NAMESPACE
