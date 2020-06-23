// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraMessageUtilities.h"
#include "NiagaraMessages.h"
#include "CoreMinimal.h"

#define LOCTEXT_NAMESPACE "NiagaraMessageUtilities"

FText FNiagaraMessageUtilities::MakePostCompileSummaryText(const FText& CompiledObjectNameText, ENiagaraScriptCompileStatus LatestCompileStatus, const int32& WarningCount, const int32& ErrorCount)
{
	FText MessageText = FText();
	switch (LatestCompileStatus) {
	case ENiagaraScriptCompileStatus::NCS_Error:
		MessageText = NSLOCTEXT("NiagaraPostCompileSummary", "NiagaraCompileStatusErrorInfo", "{0} failed to compile with {1} warning(s) and {2} error(s).");
		MessageText = FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(WarningCount)), FText::FromString(FString::FromInt(ErrorCount)));
		break;
	case ENiagaraScriptCompileStatus::NCS_UpToDate:
		MessageText = NSLOCTEXT("NiagaraPostCompileSummary", "NiagaraCompileStatusSuccessInfo", "{0} successfully compiled.");
		MessageText = FText::Format(MessageText, CompiledObjectNameText);
		break;
	case ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings:
		MessageText = NSLOCTEXT("NiagaraPostCompileSummary", "NiagaraCompileStatusWarningInfo", "{0} successfully compiled with {1} warning(s).");
		MessageText = FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(WarningCount)));
		break;
	case ENiagaraScriptCompileStatus::NCS_Unknown:
	case ENiagaraScriptCompileStatus::NCS_Dirty:
	default:
		MessageText = NSLOCTEXT("NiagaraPostCompileSummary", " NiagaraCompileStatusUnknownInfo", "{0} compile status dirty with {1} warning(s) and {2} error(s).");
		MessageText = FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(WarningCount)), FText::FromString(FString::FromInt(ErrorCount)));
		break;
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
