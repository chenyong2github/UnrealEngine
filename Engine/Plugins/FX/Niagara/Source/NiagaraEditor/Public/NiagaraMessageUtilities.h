// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "NiagaraMessages.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"

class INiagaraMessage;

namespace FNiagaraMessageUtilities
{
	FText MakePostCompileSummaryText(const FText& CompileObjectNameText, ENiagaraScriptCompileStatus LatestCompileStatus, const int32& WarningCount, const int32& ErrorCount);

	UNiagaraStackEntry::FStackIssue MessageToStackIssue(TSharedRef<const INiagaraMessage> InMessage, FString InStackEditorDataKey);
}
