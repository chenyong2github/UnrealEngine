// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackRenderItemGroup.h"
#include "ViewModels/Stack/NiagaraStackRendererItem.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterEditorData.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraClipboard.h"

#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "UNiagaraStackRenderItemGroup"

class FRenderItemGroupAddAction : public INiagaraStackItemGroupAddAction
{
public:
	FRenderItemGroupAddAction(UClass* InRendererClass)
		: RendererClass(InRendererClass)
	{
	}

	virtual FText GetCategory() const override
	{
		return LOCTEXT("AddRendererCategory", "Add Renderer");
	}

	virtual FText GetDisplayName() const override
	{
		return RendererClass->GetDisplayNameText();
	}

	virtual FText GetDescription() const override
	{
		return FText::FromString(RendererClass->GetDescription());
	}

	virtual FText GetKeywords() const override
	{
		return FText();
	}

	UClass* GetRendererClass() const
	{
		return RendererClass;
	}

private:
	UClass* RendererClass;
};

class FRenderItemGroupAddUtilities : public TNiagaraStackItemGroupAddUtilities<UNiagaraRendererProperties*>
{
public:
	FRenderItemGroupAddUtilities(TSharedRef<FNiagaraEmitterViewModel> InEmitterViewModel)
		: TNiagaraStackItemGroupAddUtilities(LOCTEXT("RenderGroupAddItemName", "Renderer"), EAddMode::AddFromAction, true, FRenderItemGroupAddUtilities::FOnItemAdded())
		, EmitterViewModel(InEmitterViewModel)
	{
	}

	virtual void AddItemDirectly() override { unimplemented(); }

	virtual void GenerateAddActions(TArray<TSharedRef<INiagaraStackItemGroupAddAction>>& OutAddActions, const FNiagaraStackItemGroupAddOptions& AddProperties) const override
	{
		TArray<UClass*> RendererClasses;
		GetDerivedClasses(UNiagaraRendererProperties::StaticClass(), RendererClasses);
		for (UClass* RendererClass : RendererClasses)
		{
			OutAddActions.Add(MakeShared<FRenderItemGroupAddAction>(RendererClass));
		}
	}

	virtual void ExecuteAddAction(TSharedRef<INiagaraStackItemGroupAddAction> AddAction, int32 TargetIndex) override
	{
		TSharedPtr<FNiagaraEmitterViewModel> EmitterViewModelPinned = EmitterViewModel.Pin();
		if (EmitterViewModelPinned.IsValid() == false)
		{
			return;
		}

		TSharedRef<FRenderItemGroupAddAction> RenderAddAction = StaticCastSharedRef<FRenderItemGroupAddAction>(AddAction);

		FScopedTransaction ScopedTransaction(LOCTEXT("AddNewRendererTransaction", "Add new renderer"));

		UNiagaraEmitter* Emitter = EmitterViewModelPinned->GetEmitter();
		Emitter->Modify();
		UNiagaraRendererProperties* RendererProperties = NewObject<UNiagaraRendererProperties>(Emitter, RenderAddAction->GetRendererClass(), NAME_None, RF_Transactional);
		Emitter->AddRenderer(RendererProperties);

		bool bVarsAdded = false;
		TArray<FNiagaraVariable> MissingAttributes = UNiagaraStackRendererItem::GetMissingVariables(RendererProperties, Emitter);
		for (int32 i = 0; i < MissingAttributes.Num(); i++)
		{
			if (UNiagaraStackRendererItem::AddMissingVariable(Emitter, MissingAttributes[i]))
			{
				bVarsAdded = true;
			}
		}

		FNiagaraSystemUpdateContext SystemUpdate(Emitter, true);

		if (bVarsAdded)
		{
			FNotificationInfo Info(LOCTEXT("AddedVariables", "One or more variables have been added to the Spawn script to support the added renderer."));
			Info.ExpireDuration = 5.0f;
			Info.bFireAndForget = true;
			Info.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Info"));
			FSlateNotificationManager::Get().AddNotification(Info);
		}

		OnItemAdded.ExecuteIfBound(RendererProperties);
	}

private:
	TWeakPtr<FNiagaraEmitterViewModel> EmitterViewModel;
};

void UNiagaraStackRenderItemGroup::Initialize(FRequiredEntryData InRequiredEntryData)
{
	FText DisplayName = LOCTEXT("RenderGroupName", "Render");
	FText ToolTip = LOCTEXT("RendererGroupTooltip", "Describes how we should display/present each particle. Note that this doesn't have to be visual. Multiple renderers are supported. Order in this stack is not necessarily relevant to draw order.");
	AddUtilities = MakeShared<FRenderItemGroupAddUtilities>(InRequiredEntryData.EmitterViewModel.ToSharedRef());
	Super::Initialize(InRequiredEntryData, DisplayName, ToolTip, AddUtilities.Get());
	EmitterWeak = GetEmitterViewModel()->GetEmitter();
	EmitterWeak->OnRenderersChanged().AddUObject(this, &UNiagaraStackRenderItemGroup::EmitterRenderersChanged);
}

bool UNiagaraStackRenderItemGroup::TestCanPasteWithMessage(const UNiagaraClipboardContent* ClipboardContent, FText& OutMessage) const
{
	if (ClipboardContent->Renderers.Num() > 0)
	{
		OutMessage = LOCTEXT("PasteRenderers", "Paste renderers from the clipboard.");
		return true;
	}
	OutMessage = LOCTEXT("NoRenderers", "No renderers on the clipboard.");
	return false;
}

FText UNiagaraStackRenderItemGroup::GetPasteTransactionText(const UNiagaraClipboardContent* ClipboardContent) const
{
	return LOCTEXT("PasteRenderersTransactionText", "Paste renderers");
}

void UNiagaraStackRenderItemGroup::Paste(const UNiagaraClipboardContent* ClipboardContent)
{
	if (EmitterWeak.IsValid())
	{
		for (const UNiagaraRendererProperties* ClipboardRenderer : ClipboardContent->Renderers)
		{
			if (ClipboardRenderer != nullptr)
			{
				EmitterWeak->AddRenderer(CastChecked<UNiagaraRendererProperties>(StaticDuplicateObject(ClipboardRenderer, EmitterWeak.Get())));
			}
		}
	}
}

void UNiagaraStackRenderItemGroup::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	int32 RendererIndex = 0;
	for (UNiagaraRendererProperties* RendererProperties : GetEmitterViewModel()->GetEmitter()->GetRenderers())
	{
		UNiagaraStackRendererItem* RendererItem = FindCurrentChildOfTypeByPredicate<UNiagaraStackRendererItem>(CurrentChildren,
			[=](UNiagaraStackRendererItem* CurrentRendererItem) { return CurrentRendererItem->GetRendererProperties() == RendererProperties; });

		if (RendererItem == nullptr)
		{
			RendererItem = NewObject<UNiagaraStackRendererItem>(this);
			RendererItem->Initialize(CreateDefaultChildRequiredData(), RendererProperties);
			RendererItem->OnRequestPaste().AddUObject(this, &UNiagaraStackRenderItemGroup::ChildRequestPaste);
		}

		NewChildren.Add(RendererItem);

		RendererIndex++;
	}

	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

void UNiagaraStackRenderItemGroup::EmitterRenderersChanged()
{
	if (IsFinalized() == false)
	{
		// With undo/redo sometimes it's not possible to unbind this delegate, so we have to check to insure safety in those cases.
		OnDataObjectModified().Broadcast(nullptr);
		RefreshChildren();
	}
}

void UNiagaraStackRenderItemGroup::ChildRequestPaste(const UNiagaraClipboardContent* ClipboardContent, int32 PasteIndex)
{
	Paste(ClipboardContent);
}

void UNiagaraStackRenderItemGroup::FinalizeInternal()
{
	if (EmitterWeak.IsValid())
	{
		EmitterWeak->OnRenderersChanged().RemoveAll(this);
	}
	Super::FinalizeInternal();
}

#undef LOCTEXT_NAMESPACE

