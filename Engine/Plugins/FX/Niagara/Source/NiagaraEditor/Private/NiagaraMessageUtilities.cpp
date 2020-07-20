// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraMessageUtilities.h"
#include "NiagaraMessages.h"
#include "CoreMinimal.h"

#define LOCTEXT_NAMESPACE "NiagaraMessageUtilities"

FText FNiagaraMessageUtilities::MakePostCompileSummaryText(const FText& CompiledObjectNameText, ENiagaraScriptCompileStatus LatestCompileStatus, const int32& WarningCount, const int32& ErrorCount)
{
	FText MessageText = FText();
	bool bHasErrors = ErrorCount > 0;
	bool bHasWarnings = WarningCount > 0;

	switch (LatestCompileStatus) {
	case ENiagaraScriptCompileStatus::NCS_Error:
		if (bHasErrors)
		{
			if (bHasWarnings)
			{
				MessageText = LOCTEXT("NiagaraCompileStatusErrorInfo_ErrorsWarnings", "{0} failed to compile with {1} {1}|plural(one=warning,other=warnings) and {2} {2}|plural(one=error,other=errors).");
				MessageText = FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(WarningCount)), FText::FromString(FString::FromInt(ErrorCount)));
				break;
			}
			MessageText = LOCTEXT("NiagaraCompileStatusErrorInfo_Errors", "{0} failed to compile with {1} {1}|plural(one=error,other=errors).");
			MessageText = FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(ErrorCount)));
		}
		else
		{
			ensureMsgf(false, TEXT("Compile status came back as NCS_Error but no Error messages were generated! Inspect this asset!"));
			MessageText = LOCTEXT("NiagaraCompileStatusErrorInfo", "{0} failed to compile.");
			MessageText = FText::Format(MessageText, CompiledObjectNameText);
		}
		break;

	case ENiagaraScriptCompileStatus::NCS_UpToDate:
		MessageText = LOCTEXT("NiagaraCompileStatusSuccessInfo", "{0} successfully compiled.");
		MessageText = FText::Format(MessageText, CompiledObjectNameText);
		break;

	case ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings:
		if (bHasWarnings)
		{
			MessageText = LOCTEXT("NiagaraCompileStatusWarningInfo_Warnings", "{0} successfully compiled with {1} {1}|plural(one=warning,other=warnings).");
			MessageText = FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(WarningCount)));
		}
		else
		{
			ensureMsgf(false, TEXT("Compile status came back as NCS_UpToDateWithWarnings but no Warning messages were generated! Inspect this asset!"));
			MessageText = LOCTEXT("NiagaraCompileStatusWarningInfo", "{0} successfully compiled with warnings.");
			MessageText = FText::Format(MessageText, CompiledObjectNameText);
		}
		break;

	case ENiagaraScriptCompileStatus::NCS_Unknown:
	case ENiagaraScriptCompileStatus::NCS_Dirty:
		if (bHasWarnings && bHasErrors)
		{
			MessageText = LOCTEXT("NiagaraCompileStatusUnknownInfo_ErrorsWarnings", "{0} compile status unknown with {1} {1}|plural(one=warning,other=warnings) and {2} {2}|plural(one=error,other=errors).");
			MessageText = FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(WarningCount)), FText::FromString(FString::FromInt(ErrorCount)));
		}
		else if (bHasErrors)
		{
			MessageText = LOCTEXT("NiagaraCompileStatusUnknownInfo_Errors", "{0} compile status unknown with {1} {1}|plural(one=error,other=errors).");
			MessageText = FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(ErrorCount)));
		}
		else if (bHasWarnings)
		{
			MessageText = LOCTEXT("NiagaraCompileStatusUnknownInfo_Warnings", "{0} compile status unknown with {1} {1}|plural(one=warning,other=warnings).");
			MessageText = FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(WarningCount)));
		}
		else
		{
			MessageText = LOCTEXT("NiagaraCompileStatusUnknownInfo", "{0} compile status unknown.");
			MessageText = FText::Format(MessageText, CompiledObjectNameText);
		}
		break;

	default:
		ensureMsgf(false, TEXT("Unexpected niagara compile status encountered!"));

	}
	return MessageText;
}

UNiagaraStackEntry::FStackIssue FNiagaraMessageUtilities::MessageToStackIssue(TSharedRef<const INiagaraMessage> InMessage, FString InStackEditorDataKey)
{
	TSharedRef<FTokenizedMessage> TokenizedMessage = InMessage->GenerateTokenizedMessage();
	EStackIssueSeverity StackIssueSeverity;
	switch (TokenizedMessage->GetSeverity())
	{
	case EMessageSeverity::CriticalError:
	case EMessageSeverity::Error:
		StackIssueSeverity = EStackIssueSeverity::Error;
		break;
	case EMessageSeverity::PerformanceWarning:
	case EMessageSeverity::Warning:
		StackIssueSeverity = EStackIssueSeverity::Warning;
		break;
	case EMessageSeverity::Info:
		StackIssueSeverity = EStackIssueSeverity::Info;
		break;
	default:
		StackIssueSeverity = EStackIssueSeverity::Info;
		break;
	}

	FText ShortDescription;
	const FName& MessageTopic = InMessage->GetMessageTopic();
	if(MessageTopic == FNiagaraMessageTopics::CompilerTopicName)
	{
		ShortDescription = LOCTEXT("CompileErrorShortDescription", "Compile Error");
	}
	else
	{
		ShortDescription = LOCTEXT("UnspecifiedErrorShortDescription", "Unspecified Error");
	}

	TArray<UNiagaraStackEntry::FStackIssueFix> FixLinks;
	TArray<FText> LinkMessages;
	TArray<FSimpleDelegate> LinkNavigateActions;
	InMessage->GenerateLinks(LinkMessages, LinkNavigateActions);
	for (int32 LinkIndex = 0; LinkIndex < LinkMessages.Num(); LinkIndex++)
	{
		const FText& LinkMessage = LinkMessages[LinkIndex];
		const FSimpleDelegate& LinkNavigateAction = LinkNavigateActions[LinkIndex];
		FixLinks.Add(UNiagaraStackEntry::FStackIssueFix(
			LinkMessage,
			UNiagaraStackEntry::FStackIssueFixDelegate::CreateLambda([LinkNavigateAction]() { LinkNavigateAction.Execute(); }),
			UNiagaraStackEntry::EStackIssueFixStyle::Link));
	}

	return UNiagaraStackEntry::FStackIssue(
		StackIssueSeverity,
		ShortDescription,
		InMessage->GenerateMessageText(),
		InStackEditorDataKey,
		false,
		FixLinks);
}

#undef LOCTEXT_NAMESPACE /** NiagaraMessageUtilities */
