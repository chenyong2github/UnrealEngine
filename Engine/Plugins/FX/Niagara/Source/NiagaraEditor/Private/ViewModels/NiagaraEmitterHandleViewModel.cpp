// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "NiagaraScriptViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraObjectSelection.h"
#include "NiagaraScriptSource.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeInput.h"
#include "NiagaraScriptOutputCollectionViewModel.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "ScopedTransaction.h"
#include "Toolkits/AssetEditorManager.h"

#define LOCTEXT_NAMESPACE "EmitterHandleViewModel"

FNiagaraEmitterHandleViewModel::FNiagaraEmitterHandleViewModel()
	: EmitterHandle(nullptr)
	, EmitterViewModel(MakeShared<FNiagaraEmitterViewModel>())
	, EmitterStackViewModel(nullptr)
	, bIsRenamePending(false)
{
}

bool FNiagaraEmitterHandleViewModel::IsValid() const
{
	return EmitterHandle != nullptr;
}

void FNiagaraEmitterHandleViewModel::Cleanup()
{
	EmitterViewModel->Cleanup();
	if (EmitterStackViewModel != nullptr)
	{
		EmitterStackViewModel->Finalize();
		EmitterStackViewModel = nullptr;
	}
}

TSharedRef<FNiagaraSystemViewModel> FNiagaraEmitterHandleViewModel::GetOwningSystemViewModel() const
{
	TSharedPtr<FNiagaraSystemViewModel> OwningSystemViewModelPinned = OwningSystemViewModelWeak.Pin();
	checkf(OwningSystemViewModelPinned.IsValid(), TEXT("Owning system view model was destroyed before child handle view model."));
	return OwningSystemViewModelPinned.ToSharedRef();
}

FNiagaraEmitterHandleViewModel::~FNiagaraEmitterHandleViewModel()
{
	Cleanup();
}


void FNiagaraEmitterHandleViewModel::Initialize(TSharedRef<FNiagaraSystemViewModel> InOwningSystemViewModel, FNiagaraEmitterHandle* InEmitterHandle, TWeakPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> InSimulation)
{
	OwningSystemViewModelWeak = InOwningSystemViewModel;
	EmitterHandle = InEmitterHandle;
	UNiagaraEmitter* Emitter = EmitterHandle != nullptr ? EmitterHandle->GetInstance() : nullptr;
	EmitterViewModel->Initialize(Emitter, InSimulation);
	if (EmitterStackViewModel != nullptr)
	{
		EmitterStackViewModel->InitializeWithViewModels(InOwningSystemViewModel, this->AsShared(), FNiagaraStackViewModelOptions(false, true));
	}
}

void FNiagaraEmitterHandleViewModel::Reset()
{
	OwningSystemViewModelWeak.Reset();
	EmitterHandle = nullptr;
	EmitterViewModel->Reset();
	if (EmitterStackViewModel != nullptr)
	{
		EmitterStackViewModel->Finalize();
		EmitterStackViewModel = nullptr;
	}
}

void FNiagaraEmitterHandleViewModel::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (EmitterStackViewModel != nullptr)
	{
		Collector.AddReferencedObject(EmitterStackViewModel);
	}
}

void FNiagaraEmitterHandleViewModel::SetSimulation(TWeakPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> InSimulation)
{
	EmitterViewModel->SetSimulation(InSimulation);
}

FGuid FNiagaraEmitterHandleViewModel::GetId() const
{
	if (EmitterHandle)
	{
		return EmitterHandle->GetId();
	}
	return FGuid();
}

FText FNiagaraEmitterHandleViewModel::GetIdText() const
{
	return FText::FromString( GetId().ToString() );
}


FText FNiagaraEmitterHandleViewModel::GetErrorText() const
{
	switch (EmitterViewModel->GetLatestCompileStatus())
	{
	case ENiagaraScriptCompileStatus::NCS_Unknown:
	case ENiagaraScriptCompileStatus::NCS_BeingCreated:
		return LOCTEXT("NiagaraEmitterHandleCompileStatusUnknown", "Needs compilation & refresh.");
	case ENiagaraScriptCompileStatus::NCS_UpToDate:
		return LOCTEXT("NiagaraEmitterHandleCompileStatusUpToDate", "Compiled");
	default:
		return LOCTEXT("NiagaraEmitterHandleCompileStatusError", "Error! Needs compilation & refresh.");
	}
}

FSlateColor FNiagaraEmitterHandleViewModel::GetErrorTextColor() const
{
	switch (EmitterViewModel->GetLatestCompileStatus())
	{
	case ENiagaraScriptCompileStatus::NCS_Unknown:
	case ENiagaraScriptCompileStatus::NCS_BeingCreated:
		return FSlateColor(FLinearColor::Yellow);
	case ENiagaraScriptCompileStatus::NCS_UpToDate:
		return FSlateColor(FLinearColor::Green);
	default:
		return FSlateColor(FLinearColor::Red);
	}
}

EVisibility FNiagaraEmitterHandleViewModel::GetErrorTextVisibility() const
{
	return EmitterViewModel->GetLatestCompileStatus() != ENiagaraScriptCompileStatus::NCS_UpToDate ? EVisibility::Visible : EVisibility::Collapsed;
}

FName FNiagaraEmitterHandleViewModel::GetName() const
{
	if (EmitterHandle)
	{
		return EmitterHandle->GetName();
	}
	return FName();
}

void FNiagaraEmitterHandleViewModel::SetName(FName InName)
{
	if (EmitterHandle && EmitterHandle->GetName() == InName)
	{
		return;
	}

	if (EmitterHandle)
	{
		FScopedTransaction ScopedTransaction(NSLOCTEXT("NiagaraEmitterEditor", "EditEmitterNameTransaction", "Edit emitter name"));
		UNiagaraSystem& System = GetOwningSystemViewModel()->GetSystem();
		System.Modify();
		System.RemoveSystemParametersForEmitter(*EmitterHandle);
		EmitterHandle->SetName(InName, System);
		System.RefreshSystemParametersFromEmitter(*EmitterHandle);
		OnPropertyChangedDelegate.Broadcast();
		OnNameChangedDelegate.Broadcast();
	}
}

FText FNiagaraEmitterHandleViewModel::GetNameText() const
{
	if (EmitterHandle)
	{
		return FText::FromName(EmitterHandle->GetName());
	}
	return FText();
}

bool FNiagaraEmitterHandleViewModel::CanRenameEmitter() const
{
	return GetOwningSystemViewModel()->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset;
}

void FNiagaraEmitterHandleViewModel::OnNameTextComitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	SetName(*InText.ToString());
}

bool FNiagaraEmitterHandleViewModel::VerifyNameTextChanged(const FText& NewText, FText& OutErrorMessage)
{
	FName NewName = *NewText.ToString();
	if (NewName == FName())
	{
		OutErrorMessage = NSLOCTEXT("NiagaraEmitterEditor", "NiagaraInputNameEmptyWarn", "Cannot have empty name!");
		return false;
	}
	return true;
}

bool FNiagaraEmitterHandleViewModel::GetIsEnabled() const
{
	if (EmitterHandle)
	{
		return EmitterHandle->GetIsEnabled();
	}
	return false;
}

void FNiagaraEmitterHandleViewModel::SetIsEnabled(bool bInIsEnabled)
{
	if (EmitterHandle && EmitterHandle->GetIsEnabled() != bInIsEnabled)
	{
		FScopedTransaction ScopedTransaction(NSLOCTEXT("NiagaraEmitterEditor", "EditEmitterEnabled", "Change emitter enabled state"));
		GetOwningSystemViewModel()->GetSystem().Modify();
		EmitterHandle->SetIsEnabled(bInIsEnabled, GetOwningSystemViewModel()->GetSystem(), true);
		OnPropertyChangedDelegate.Broadcast();
	}
}

bool FNiagaraEmitterHandleViewModel::GetIsIsolated() const
{
	return EmitterHandle != nullptr && EmitterHandle->IsIsolated();
}

ECheckBoxState FNiagaraEmitterHandleViewModel::GetIsEnabledCheckState() const
{
	if (EmitterHandle)
	{
		return EmitterHandle->GetIsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Undetermined;
}

void FNiagaraEmitterHandleViewModel::OnIsEnabledCheckStateChanged(ECheckBoxState InCheckState)
{
	SetIsEnabled(InCheckState == ECheckBoxState::Checked);
}

FNiagaraEmitterHandle* FNiagaraEmitterHandleViewModel::GetEmitterHandle()
{
	return EmitterHandle;
}

TSharedRef<FNiagaraEmitterViewModel> FNiagaraEmitterHandleViewModel::GetEmitterViewModel()
{
	return EmitterViewModel;
}

UNiagaraStackViewModel* FNiagaraEmitterHandleViewModel::GetEmitterStackViewModel()
{
	if (EmitterStackViewModel == nullptr)
	{
		EmitterStackViewModel = NewObject<UNiagaraStackViewModel>(GetTransientPackage());
		EmitterStackViewModel->InitializeWithViewModels(GetOwningSystemViewModel(), this->AsShared(), FNiagaraStackViewModelOptions(false, true));
	}
	return EmitterStackViewModel;
}

bool FNiagaraEmitterHandleViewModel::GetIsRenamePending() const
{
	return bIsRenamePending;
}

void FNiagaraEmitterHandleViewModel::SetIsRenamePending(bool bInIsRenamePending)
{
	bIsRenamePending = bInIsRenamePending;
}

FNiagaraEmitterHandleViewModel::FOnPropertyChanged& FNiagaraEmitterHandleViewModel::OnPropertyChanged()
{
	return OnPropertyChangedDelegate;
}

FNiagaraEmitterHandleViewModel::FOnNameChanged& FNiagaraEmitterHandleViewModel::OnNameChanged()
{
	return OnNameChangedDelegate;
}

#undef LOCTEXT_NAMESPACE
