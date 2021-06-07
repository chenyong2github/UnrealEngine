// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackRendererItem.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
#include "NiagaraEmitter.h"
#include "NiagaraStackEditorData.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraScriptViewModel.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraConstants.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraGraph.h"
#include "ScopedTransaction.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptSource.h"
#include "ScopedTransaction.h"
#include "ViewModels/Stack/NiagaraStackErrorItem.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraScriptMergeManager.h"
#include "NiagaraClipboard.h"
#include "NiagaraEmitterEditorData.h"

#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "UNiagaraStackRendererItem"

UNiagaraStackRendererItem::UNiagaraStackRendererItem()
	: RendererObject(nullptr)
{
}

void UNiagaraStackRendererItem::Initialize(FRequiredEntryData InRequiredEntryData, UNiagaraRendererProperties* InRendererProperties)
{
	checkf(RendererProperties.IsValid() == false, TEXT("Can not initialize more than once."));
	FString RendererStackEditorDataKey = FString::Printf(TEXT("Renderer-%s"), *InRendererProperties->GetName());
	Super::Initialize(InRequiredEntryData, RendererStackEditorDataKey);
	RendererProperties = InRendererProperties;
	RendererProperties->OnChanged().AddUObject(this, &UNiagaraStackRendererItem::RendererChanged);
}

void UNiagaraStackRendererItem::FinalizeInternal()
{
	if (RendererProperties.IsValid())
	{
		RendererProperties->OnChanged().RemoveAll(this);
	}
	Super::FinalizeInternal();
}

TArray<FNiagaraVariable> UNiagaraStackRendererItem::GetMissingVariables(UNiagaraRendererProperties* RendererProperties, UNiagaraEmitter* Emitter)
{
	TArray<FNiagaraVariable> MissingAttributes;
	const TArray<FNiagaraVariable>& RequiredAttrs = RendererProperties->GetRequiredAttributes();
	const UNiagaraScript* Script = Emitter->SpawnScriptProps.Script;
	if (Script != nullptr && Script->IsReadyToRun(ENiagaraSimTarget::CPUSim))
	{
		MissingAttributes.Empty();
		for (FNiagaraVariable Attr : RequiredAttrs)
		{
			FNiagaraVariable OriginalAttr = Attr;
			// TODO .. should we always be namespaced?
			FString AttrName = Attr.GetName().ToString();
			if (AttrName.RemoveFromStart(TEXT("Particles.")))
			{
				Attr.SetName(*AttrName);
			}

			bool ContainsVar = Script->GetVMExecutableData().Attributes.ContainsByPredicate([&Attr](const FNiagaraVariable& Var) { return Var.GetName() == Attr.GetName(); });
			if (!ContainsVar)
			{
				MissingAttributes.Add(OriginalAttr);
			}
		}
	}
	return MissingAttributes;
}

bool UNiagaraStackRendererItem::AddMissingVariable(UNiagaraEmitter* Emitter, const FNiagaraVariable& Variable)
{
	UNiagaraScript* Script = Emitter->SpawnScriptProps.Script;
	if (!Script)
	{
		return false;
	}
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
	if (!Source)
	{
		return false;
	}

	UNiagaraGraph* Graph = Source->NodeGraph;
	if (!Graph)
	{
		return false;
	}

	UNiagaraNodeOutput* OutputNode = Graph->FindOutputNode(ENiagaraScriptUsage::ParticleSpawnScript);
	if (!OutputNode)
	{
		return false;
	}

	FScopedTransaction ScopedTransaction(LOCTEXT("FixRendererError", "Fixing rendering module error: Add Attribute"));
	Graph->Modify();

	FGraphNodeCreator<UNiagaraNodeAssignment> NodeBuilder(*Graph);
	UNiagaraNodeAssignment* NewAssignmentNode = NodeBuilder.CreateNode();
	FString VarDefaultValue = FNiagaraConstants::GetAttributeDefaultValue(Variable);
	NewAssignmentNode->AddAssignmentTarget(Variable, &VarDefaultValue);
	NodeBuilder.Finalize();

	TArray<FNiagaraStackGraphUtilities::FStackNodeGroup> StackNodeGroups;
	FNiagaraStackGraphUtilities::GetStackNodeGroups(*OutputNode, StackNodeGroups);

	FNiagaraStackGraphUtilities::FStackNodeGroup AssignmentGroup;
	AssignmentGroup.StartNodes.Add(NewAssignmentNode);
	AssignmentGroup.EndNode = NewAssignmentNode;

	FNiagaraStackGraphUtilities::FStackNodeGroup& OutputGroup = StackNodeGroups[StackNodeGroups.Num() - 1];
	FNiagaraStackGraphUtilities::FStackNodeGroup& OutputGroupPrevious = StackNodeGroups[StackNodeGroups.Num() - 2];
	FNiagaraStackGraphUtilities::ConnectStackNodeGroup(AssignmentGroup, OutputGroupPrevious, OutputGroup);

	FNiagaraStackGraphUtilities::RelayoutGraph(*Graph);
	return true;
}

UNiagaraRendererProperties* UNiagaraStackRendererItem::GetRendererProperties()
{
	return RendererProperties.Get();
}

FText UNiagaraStackRendererItem::GetDisplayName() const
{
	if(DisplayNameCache.IsSet() == false)
	{
		if (RendererProperties != nullptr)
		{
			DisplayNameCache = RendererProperties->GetWidgetDisplayName();
		}
		else
		{
			DisplayNameCache = FText::FromName(NAME_None);
		}
	}
	return DisplayNameCache.GetValue();
}

bool UNiagaraStackRendererItem::TestCanCutWithMessage(FText& OutMessage) const
{
	FText CanCopyMessage;
	if (TestCanCopyWithMessage(CanCopyMessage) == false)
	{
		OutMessage = FText::Format(LOCTEXT("CantCutBecauseCantCopyFormat", "This renderer can not be cut because it can't be copied.  {0}"), CanCopyMessage);
		return false;
	}

	FText CanDeleteMessage;
	if (TestCanDeleteWithMessage(CanDeleteMessage) == false)
	{
		OutMessage = FText::Format(LOCTEXT("CantCutBecauseCantDeleteFormat", "This renderer can't be cut because it can't be deleted.  {0}"), CanDeleteMessage);
		return false;
	}

	OutMessage = LOCTEXT("CanCut", "Cut this renderer.");
	return true;
}

FText UNiagaraStackRendererItem::GetCutTransactionText() const
{
	return LOCTEXT("CutRendererTransactionText", "Cut renderers");
}

void UNiagaraStackRendererItem::CopyForCut(UNiagaraClipboardContent* ClipboardContent) const
{
	Copy(ClipboardContent);
}

void UNiagaraStackRendererItem::RemoveForCut()
{
	Delete();
}

bool UNiagaraStackRendererItem::TestCanCopyWithMessage(FText& OutMessage) const
{
	OutMessage = LOCTEXT("CopyRenderer", "Copy this renderer.");
	return true;
}

void UNiagaraStackRendererItem::Copy(UNiagaraClipboardContent* ClipboardContent) const
{
	ClipboardContent->Renderers.Add(CastChecked<UNiagaraRendererProperties>(StaticDuplicateObject(RendererProperties.Get(), ClipboardContent)));
}

bool UNiagaraStackRendererItem::TestCanPasteWithMessage(const UNiagaraClipboardContent* ClipboardContent, FText& OutMessage) const
{
	if (RequestCanPasteDelegete.IsBound())
	{
		return RequestCanPasteDelegete.Execute(ClipboardContent, OutMessage);
	}
	OutMessage = FText();
	return false;
}

FText UNiagaraStackRendererItem::GetPasteTransactionText(const UNiagaraClipboardContent* ClipboardContent) const
{
	return LOCTEXT("PasteRenderersTransactionText", "Paste renderers");
}

void UNiagaraStackRendererItem::Paste(const UNiagaraClipboardContent* ClipboardContent, FText& OutPasteWarning)
{
	RequestPasteDelegate.ExecuteIfBound(ClipboardContent, INDEX_NONE, OutPasteWarning);
}

bool UNiagaraStackRendererItem::TestCanDeleteWithMessage(FText& OutCanDeleteMessage) const
{
	if (GetOwnerIsEnabled() == false)
	{
		OutCanDeleteMessage = LOCTEXT("CantDeleteOwnerDisabledToolTip", "This renderer can not be deleted because its owner is disabled.");
		return false;
	}
	else if (HasBaseRenderer() == false)
	{
		OutCanDeleteMessage = LOCTEXT("DeleteToolTip", "Delete this renderer.");
		return true;
	}
	else
	{
		OutCanDeleteMessage = LOCTEXT("CantDeleteToolTip", "This renderer can not be deleted becaue it is inherited.");
		return false;
	}
}

FText UNiagaraStackRendererItem::GetDeleteTransactionText() const
{
	return LOCTEXT("DeleteRenderer", "Delete Renderer");
}

void UNiagaraStackRendererItem::Delete()
{
	UNiagaraEmitter* Emitter = GetEmitterViewModel()->GetEmitter();

	UNiagaraRendererProperties* Renderer = RendererProperties.Get();
	Emitter->Modify();
	Emitter->RemoveRenderer(Renderer);
	if (UNiagaraEmitterEditorData* Data = Cast<UNiagaraEmitterEditorData>(Emitter->GetEditorData()))
	{
		Data->GetStackEditorData().Modify();
		Data->GetStackEditorData().SetStackEntryDisplayName(GetStackEditorDataKey(), FText());
	}

	TArray<UObject*> ChangedObjects;
	ChangedObjects.Add(Renderer);
	OnDataObjectModified().Broadcast(ChangedObjects, ENiagaraDataObjectChange::Removed);
}

bool UNiagaraStackRendererItem::HasBaseRenderer() const
{
	if (HasBaseEmitter())
	{
		if (bHasBaseRendererCache.IsSet() == false)
		{
			TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
			const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
			bHasBaseRendererCache = BaseEmitter != nullptr && MergeManager->HasBaseRenderer(*BaseEmitter, RendererProperties->GetMergeId());
		}
		return bHasBaseRendererCache.GetValue();
	}
	return false;
}

bool UNiagaraStackRendererItem::TestCanResetToBaseWithMessage(FText& OutCanResetToBaseMessage) const
{
	if (bCanResetToBaseCache.IsSet() == false)
	{
		if (HasBaseRenderer())
		{
			TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
			const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
			bCanResetToBaseCache = BaseEmitter != nullptr && MergeManager->IsRendererDifferentFromBase(*GetEmitterViewModel()->GetEmitter(), *BaseEmitter, RendererProperties->GetMergeId());
		}
		else
		{
			bCanResetToBaseCache = false;
		}
	}
	if (bCanResetToBaseCache.GetValue())
	{
		OutCanResetToBaseMessage = LOCTEXT("CanResetToBase", "Reset this renderer to the state defined by the parent emitter.");
		return true;
	}
	else
	{
		OutCanResetToBaseMessage = LOCTEXT("CanNotResetToBase", "No parent to reset to, or not different from parent.");
		return false;
	}
}

void UNiagaraStackRendererItem::ResetToBase()
{
	FText Unused;
	if (TestCanResetToBaseWithMessage(Unused))
	{
		TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		MergeManager->ResetRendererToBase(*GetEmitterViewModel()->GetEmitter(), *BaseEmitter, RendererProperties->GetMergeId());
		ModifiedGroupItemsDelegate.Broadcast();
	}
}

bool UNiagaraStackRendererItem::GetIsEnabled() const
{
	if (IsFinalized() == false && RendererProperties.IsValid())
	{
		return RendererProperties->GetIsEnabled();
	}
	else
	{
		return false;
	}
}

void UNiagaraStackRendererItem::SetIsEnabledInternal(bool bInIsEnabled)
{
	FScopedTransaction ScopedTransaction(LOCTEXT("SetRendererEnabledState", "Set renderer enabled/disabled state."));
	RendererProperties->Modify();
	RendererProperties->SetIsEnabled(bInIsEnabled);
	TArray<UObject*> ChangedObjects;
	ChangedObjects.Add(RendererProperties.Get());
	OnDataObjectModified().Broadcast(ChangedObjects, ENiagaraDataObjectChange::Changed);
	RefreshChildren();
}

const FSlateBrush* UNiagaraStackRendererItem::GetIconBrush() const
{
	if (IsFinalized() == false && RendererProperties.IsValid())
	{
		return RendererProperties->GetStackIcon();
	}
	else
	{
		return nullptr;
	}
}

void UNiagaraStackRendererItem::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (RendererObject == nullptr)
	{
		RendererObject = NewObject<UNiagaraStackObject>(this);
		RendererObject->Initialize(CreateDefaultChildRequiredData(), RendererProperties.Get(), GetStackEditorDataKey());
	}

	NewChildren.Add(RendererObject);
	MissingAttributes = GetMissingVariables(RendererProperties.Get(), GetEmitterViewModel()->GetEmitter());
	bHasBaseRendererCache.Reset();
	bCanResetToBaseCache.Reset();
	DisplayNameCache.Reset();
	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
	
	RefreshIssues(NewIssues);
}

void UNiagaraStackRendererItem::ProcessRendererIssues(const TArray<FNiagaraRendererFeedback>& InIssues, EStackIssueSeverity Severity, TArray<FStackIssue>& OutIssues)
{
	for (const FNiagaraRendererFeedback& Item : InIssues)
	{
		TArray<FStackIssueFix> Fixes;
		if (Item.IsFixable())
		{
			Fixes.Add(FStackIssueFix(Item.GetFixDescriptionText(), FStackIssueFixDelegate::CreateLambda([Item]() { Item.TryFix(); })));
		}
		FStackIssue TargetSupportError(Severity, Item.GetSummaryText(), Item.GetDescriptionText(), GetStackEditorDataKey(), Item.IsDismissable(), Fixes);
		OutIssues.Add(TargetSupportError);
	}
}

void UNiagaraStackRendererItem::RefreshIssues(TArray<FStackIssue>& NewIssues)
{
	if (!GetIsEnabled())
	{
		NewIssues.Empty();
		return;
	}
	for (FNiagaraVariable Attribute : MissingAttributes)
	{
		FText FixDescription = LOCTEXT("AddMissingVariable", "Add missing variable");
		FStackIssueFix AddAttributeFix(
			FixDescription,
			FStackIssueFixDelegate::CreateLambda([=]()
		{
			FScopedTransaction ScopedTransaction(FixDescription);
			if (AddMissingVariable(GetEmitterViewModel()->GetEmitter(), Attribute))
			{
				FNotificationInfo Info(FText::Format(LOCTEXT("AddedVariableForFix", "Added {0} to the Spawn script to support the renderer."), FText::FromName(Attribute.GetName())));
				Info.ExpireDuration = 5.0f;
				Info.bFireAndForget = true;
				Info.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Info"));
				FSlateNotificationManager::Get().AddNotification(Info);
			}
		}));

		FStackIssue MissingAttributeError(
			EStackIssueSeverity::Error,
			LOCTEXT("FailedRendererBindShort", "An attribute is missing."),
			FText::Format(LOCTEXT("FailedRendererBind", "Missing attribute \"{0}\" of Type \"{1}\"."), FText::FromName(Attribute.GetName()), Attribute.GetType().GetNameText()),
			GetStackEditorDataKey(),
			false,
			AddAttributeFix);

		NewIssues.Add(MissingAttributeError);
	}

	if (RendererProperties->GetIsEnabled() && !RendererProperties->IsSimTargetSupported(GetEmitterViewModel()->GetEmitter()->SimTarget))
	{
		
		FStackIssue TargetSupportError(
			EStackIssueSeverity::Error,
			LOCTEXT("FailedRendererDueToSimTarget", "Renderer incompatible with SimTarget mode."),
			FText::Format(LOCTEXT("FailedRendererDueToSimTargetLong", "Renderer incompatible with SimTarget mode \"{0}\"."), FText::FromName(UEnum::GetValueAsName(GetEmitterViewModel()->GetEmitter()->SimTarget))),
			GetStackEditorDataKey(),
			false);

		NewIssues.Add(TargetSupportError);
	}

	if (RendererProperties->GetIsEnabled())
	{
		TArray<FNiagaraRendererFeedback> Errors;
		TArray<FNiagaraRendererFeedback> Warnings;
		TArray<FNiagaraRendererFeedback> Infos;

		RendererProperties->GetRendererFeedback(GetEmitterViewModel()->GetEmitter(), Errors, Warnings, Infos);

		ProcessRendererIssues(Errors, EStackIssueSeverity::Error, NewIssues);		
		ProcessRendererIssues(Warnings, EStackIssueSeverity::Warning, NewIssues);		
		ProcessRendererIssues(Infos, EStackIssueSeverity::Info, NewIssues);	
	}
}

void UNiagaraStackRendererItem::RendererChanged()
{
	if (IsFinalized() == false)
	{
		// Undo/redo can cause objects to disappear and reappear which can prevent safe removal of delegates
		// so guard against receiving an event when finalized here.
		bCanResetToBaseCache.Reset();
		RefreshChildren();
	}
}

#undef LOCTEXT_NAMESPACE
