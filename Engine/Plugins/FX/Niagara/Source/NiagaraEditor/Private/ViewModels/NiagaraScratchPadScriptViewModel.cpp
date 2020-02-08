// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"

#include "ObjectTools.h"

FNiagaraScratchPadScriptViewModel::FNiagaraScratchPadScriptViewModel()
	: FNiagaraScriptViewModel(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FNiagaraScratchPadScriptViewModel::GetDisplayNameInternal)), ENiagaraParameterEditMode::EditAll)
	, bIsPendingRename(false)
{
}

void FNiagaraScratchPadScriptViewModel::Initialize(UNiagaraScript* Script)
{
	SetScript(Script);
}

UNiagaraScript* FNiagaraScratchPadScriptViewModel::GetScript() const
{
	return GetScripts()[0].Get();
}

bool FNiagaraScratchPadScriptViewModel::GetIsPendingRename() const
{
	return bIsPendingRename;
}

void FNiagaraScratchPadScriptViewModel::SetIsPendingRename(bool bInIsPendingRename)
{
	bIsPendingRename = bInIsPendingRename;
}

void FNiagaraScratchPadScriptViewModel::SetScriptName(FText InScriptName)
{
	UNiagaraScript* Script = GetScript();
	FString NewName = ObjectTools::SanitizeObjectName(InScriptName.ToString());
	if (Script->GetName() != NewName)
	{
		FName NewUniqueName = MakeUniqueObjectName(Script->GetOuter(), UNiagaraScript::StaticClass(), *NewName);
		Script->Rename(*NewUniqueName.ToString());

		for (TObjectIterator<UNiagaraNodeFunctionCall> It; It; ++It)
		{
			UNiagaraNodeFunctionCall* FunctionCallNode = *It;
			if (FunctionCallNode->FunctionScript == Script)
			{
				FString OldFunctionName = FunctionCallNode->GetFunctionName();
				FunctionCallNode->SuggestName(FString());
				const FString NewFunctionName = FunctionCallNode->GetFunctionName();
				UNiagaraSystem* System = FunctionCallNode->GetTypedOuter<UNiagaraSystem>();
				UNiagaraEmitter* Emitter = FunctionCallNode->GetTypedOuter<UNiagaraEmitter>();
				if (System != nullptr)
				{
					FNiagaraStackGraphUtilities::RenameReferencingParameters(*System, Emitter, *FunctionCallNode, OldFunctionName, NewFunctionName);
				}
			}
		}

		OnRenamedDelegate.Broadcast();
	}
}

FNiagaraScratchPadScriptViewModel::FOnRenamed& FNiagaraScratchPadScriptViewModel::OnRenamed()
{
	return OnRenamedDelegate;
}

FText FNiagaraScratchPadScriptViewModel::GetDisplayNameInternal() const
{
	return FText::FromString(GetScript()->GetName());
}