// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackFunctionInputCollection.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeParameterMapGet.h"
#include "NiagaraNodeParameterMapSet.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEditorUtilities.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraSystemScriptViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraStackEditorData.h"
#include "NiagaraComponent.h"
#include "NiagaraScriptSource.h"
#include "NiagaraConstants.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraScriptMergeManager.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraEditorModule.h"
#include "NiagaraRendererProperties.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraClipboard.h"
#include "Toolkits/NiagaraSystemToolkit.h"
#include "NiagaraMessageManager.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpressionDynamicParameter.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "UObject/StructOnScope.h"
#include "AssetRegistryModule.h"
#include "ARFilter.h"
#include "EdGraph/EdGraphPin.h"
#include "INiagaraEditorTypeUtilities.h"
#include "ViewModels/Stack/NiagaraStackInputCategory.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "NiagaraConvertInPlaceUtilityBase.h"

#include "NiagaraScriptVariable.h"

#define LOCTEXT_NAMESPACE "NiagaraStackViewModel"

UNiagaraStackFunctionInput::UNiagaraStackFunctionInput()
	: OwningModuleNode(nullptr)
	, OwningFunctionCallNode(nullptr)
	, bUpdatingGraphDirectly(false)
	, bUpdatingLocalValueDirectly(false)
	, bShowEditConditionInline(false)
	, bIsInlineEditConditionToggle(false)
	, bIsDynamicInputScriptReassignmentPending(false)
{
}

// Traverses the path between the owning module node and the function call node this input belongs too collecting up the input handles between them.
void GenerateInputParameterHandlePath(UNiagaraNodeFunctionCall& ModuleNode, UNiagaraNodeFunctionCall& FunctionCallNode, TArray<FNiagaraParameterHandle>& OutHandlePath)
{
	UNiagaraNodeFunctionCall* CurrentFunctionCallNode = &FunctionCallNode;
	while (CurrentFunctionCallNode != &ModuleNode)
	{
		TArray<UEdGraphPin*> FunctionOutputPins;
		CurrentFunctionCallNode->GetOutputPins(FunctionOutputPins);
		if (ensureMsgf(FunctionOutputPins.Num() == 1 && FunctionOutputPins[0]->LinkedTo.Num() == 1 && FunctionOutputPins[0]->LinkedTo[0]->GetOwningNode()->IsA<UNiagaraNodeParameterMapSet>(),
			TEXT("Invalid Stack Graph - Dynamic Input Function call didn't have a valid connected output.")))
		{
			FNiagaraParameterHandle AliasedHandle(FunctionOutputPins[0]->LinkedTo[0]->PinName);
			OutHandlePath.Add(FNiagaraParameterHandle::CreateModuleParameterHandle(AliasedHandle.GetName()));
			UNiagaraNodeParameterMapSet* NextOverrideNode = CastChecked<UNiagaraNodeParameterMapSet>(FunctionOutputPins[0]->LinkedTo[0]->GetOwningNode());
			UEdGraphPin* NextOverrideNodeOutputPin = FNiagaraStackGraphUtilities::GetParameterMapOutputPin(*NextOverrideNode);
			
			CurrentFunctionCallNode = nullptr;
			for (UEdGraphPin* NextOverrideNodeOutputPinLinkedPin : NextOverrideNodeOutputPin->LinkedTo)
			{
				UNiagaraNodeFunctionCall* NextFunctionCallNode = Cast<UNiagaraNodeFunctionCall>(NextOverrideNodeOutputPinLinkedPin->GetOwningNode());
				if (NextFunctionCallNode != nullptr && NextFunctionCallNode->GetFunctionName() == AliasedHandle.GetNamespace().ToString())
				{
					CurrentFunctionCallNode = NextFunctionCallNode;
					break;
				}
			}


			if (ensureMsgf(CurrentFunctionCallNode != nullptr, TEXT("Invalid Stack Graph - Function call node for override pin %s could not be found."), *FunctionOutputPins[0]->PinName.ToString()) == false)
			{
				OutHandlePath.Empty();
				return;
			}
		}
		else
		{
			OutHandlePath.Empty();
			return;
		}
	}
}

void UNiagaraStackFunctionInput::Initialize(
	FRequiredEntryData InRequiredEntryData,
	UNiagaraNodeFunctionCall& InModuleNode,
	UNiagaraNodeFunctionCall& InInputFunctionCallNode,
	FName InInputParameterHandle,
	FNiagaraTypeDefinition InInputType,
	EStackParameterBehavior InParameterBehavior,
	FString InOwnerStackItemEditorDataKey)
{
	checkf(OwningModuleNode.IsValid() == false && OwningFunctionCallNode.IsValid() == false, TEXT("Can only initialize once."));
	bool bInputIsAdvanced = false;
	ParameterBehavior = InParameterBehavior;
	FString InputStackEditorDataKey = FString::Printf(TEXT("%s-Input-%s"), *InInputFunctionCallNode.NodeGuid.ToString(EGuidFormats::DigitsWithHyphens), *InInputParameterHandle.ToString());
	Super::Initialize(InRequiredEntryData, bInputIsAdvanced, InOwnerStackItemEditorDataKey, InputStackEditorDataKey);
	OwningModuleNode = &InModuleNode;
	OwningFunctionCallNode = &InInputFunctionCallNode;
	OwningFunctionCallInitialScript = OwningFunctionCallNode->FunctionScript;
	OwningAssignmentNode = Cast<UNiagaraNodeAssignment>(OwningFunctionCallNode.Get());

	UNiagaraSystem& ParentSystem = GetSystemViewModel()->GetSystem();
	UNiagaraEmitter* ParentEmitter = GetEmitterViewModel().IsValid() ? GetEmitterViewModel()->GetEmitter() : nullptr;

	FNiagaraStackGraphUtilities::FindAffectedScripts(ParentSystem, ParentEmitter, *OwningModuleNode.Get(), AffectedScripts);

	UNiagaraNodeOutput* OutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*OwningModuleNode.Get());
	for (TWeakObjectPtr<UNiagaraScript> AffectedScript : AffectedScripts)
	{
		if (AffectedScript.IsValid() && AffectedScript->IsEquivalentUsage(OutputNode->GetUsage()) && AffectedScript->GetUsageId() == OutputNode->GetUsageId())
		{
			SourceScript = AffectedScript;
			RapidIterationParametersChangedHandle = SourceScript->RapidIterationParameters.AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraStackFunctionInput::OnRapidIterationParametersChanged));
			SourceScript->GetSource()->OnChanged().AddUObject(this, &UNiagaraStackFunctionInput::OnScriptSourceChanged);
			break;
		}
	}

	checkf(SourceScript.IsValid(), TEXT("Coudn't find source script in affected scripts."));

	GraphChangedHandle = OwningFunctionCallNode->GetGraph()->AddOnGraphChangedHandler(
		FOnGraphChanged::FDelegate::CreateUObject(this, &UNiagaraStackFunctionInput::OnGraphChanged));
	OnRecompileHandle = OwningFunctionCallNode->GetNiagaraGraph()->AddOnGraphNeedsRecompileHandler(
		FOnGraphChanged::FDelegate::CreateUObject(this, &UNiagaraStackFunctionInput::OnGraphChanged));

	InputParameterHandle = FNiagaraParameterHandle(InInputParameterHandle);
	GenerateInputParameterHandlePath(*OwningModuleNode, *OwningFunctionCallNode, InputParameterHandlePath);
	InputParameterHandlePath.Add(InputParameterHandle);

	DisplayName = FText::FromName(InputParameterHandle.GetName());

	InputType = InInputType;
	StackEditorDataKey = FNiagaraStackGraphUtilities::GenerateStackFunctionInputEditorDataKey(*OwningFunctionCallNode.Get(), InputParameterHandle);

	TArray<UNiagaraScript*> AffectedScriptsNotWeak;
	for (TWeakObjectPtr<UNiagaraScript> AffectedScript : AffectedScripts)
	{
		AffectedScriptsNotWeak.Add(AffectedScript.Get());
	}

	FString UniqueEmitterName = GetEmitterViewModel().IsValid() ? GetEmitterViewModel()->GetEmitter()->GetUniqueEmitterName() : FString();
	EditCondition.Initialize(SourceScript.Get(), AffectedScriptsNotWeak, UniqueEmitterName, OwningFunctionCallNode.Get());
	VisibleCondition.Initialize(SourceScript.Get(), AffectedScriptsNotWeak, UniqueEmitterName, OwningFunctionCallNode.Get());
}

void UNiagaraStackFunctionInput::FinalizeInternal()
{
	if (OwningFunctionCallNode.IsValid())
	{
		OwningFunctionCallNode->GetGraph()->RemoveOnGraphChangedHandler(GraphChangedHandle);
		OwningFunctionCallNode->GetNiagaraGraph()->RemoveOnGraphNeedsRecompileHandler(OnRecompileHandle);
	}

	if (SourceScript.IsValid())
	{
		SourceScript->RapidIterationParameters.RemoveOnChangedHandler(RapidIterationParametersChangedHandle);
		SourceScript->GetSource()->OnChanged().RemoveAll(this);
	}

	if (MessageManagerRefreshHandle.IsValid())
	{
		FNiagaraMessageManager::Get()->GetOnRequestRefresh().Remove(MessageManagerRefreshHandle);
		MessageManagerRefreshHandle.Reset();
	}

	Super::FinalizeInternal();
}

const UNiagaraNodeFunctionCall& UNiagaraStackFunctionInput::GetInputFunctionCallNode() const
{
	return *OwningFunctionCallNode.Get();
}

UNiagaraScript* UNiagaraStackFunctionInput::GetInputFunctionCallInitialScript() const
{
	return OwningFunctionCallInitialScript.Get();
}

UNiagaraStackFunctionInput::EValueMode UNiagaraStackFunctionInput::GetValueMode()
{
	return InputValues.Mode;
}

const FNiagaraTypeDefinition& UNiagaraStackFunctionInput::GetInputType() const
{
	return InputType;
}

FText UNiagaraStackFunctionInput::GetTooltipText() const
{
	FText Description = InputMetaData.IsSet() ? InputMetaData->Description : FText::GetEmpty();
	return FNiagaraEditorUtilities::FormatVariableDescription(Description, GetDisplayName(), InputType.GetNameText());
}

bool UNiagaraStackFunctionInput::GetIsEnabled() const
{
	return OwningFunctionCallNode->GetDesiredEnabledState() == ENodeEnabledState::Enabled;
}

UObject* UNiagaraStackFunctionInput::GetExternalAsset() const
{
	if (OwningFunctionCallNode.IsValid() && OwningFunctionCallNode->FunctionScript != nullptr && OwningFunctionCallNode->FunctionScript->IsAsset())
	{
		return OwningFunctionCallNode->FunctionScript;
	}
	return nullptr;
}

bool UNiagaraStackFunctionInput::TestCanCutWithMessage(FText& OutMessage) const
{
	if (InputValues.HasEditableData() == false)
	{
		OutMessage = LOCTEXT("CantCutInvalidMessage", "The current input state doesn't support cutting.");
		return false;
	}
	if (GetIsEnabledAndOwnerIsEnabled() == false)
	{
		OutMessage = LOCTEXT("CantCutDisabled", "Can not cut and input when it's owner is disabled.");
		return false;
	}
	OutMessage = LOCTEXT("CanCutMessage", "Cut will copy the value of this input including\nany data objects and dynamic inputs, and will reset it to default.");
	return true;
}

FText UNiagaraStackFunctionInput::GetCutTransactionText() const
{
	return LOCTEXT("CutInputTransaction", "Cut niagara input");
}

void UNiagaraStackFunctionInput::CopyForCut(UNiagaraClipboardContent* ClipboardContent) const
{
	Copy(ClipboardContent);
}

void UNiagaraStackFunctionInput::RemoveForCut()
{
	Reset();
}

bool UNiagaraStackFunctionInput::TestCanCopyWithMessage(FText& OutMessage) const
{
	if (InputValues.HasEditableData() == false)
	{
		OutMessage = LOCTEXT("CantCopyInvalidMessage", "The current input state doesn't support copying.");
		return false;
	}
	OutMessage = LOCTEXT("CanCopyMessage", "Copy the value of this input including\nany data objects and dynamic inputs.");
	return true;
}

void UNiagaraStackFunctionInput::Copy(UNiagaraClipboardContent* ClipboardContent) const
{
	const UNiagaraClipboardFunctionInput* ClipboardInput = ToClipboardFunctionInput(GetTransientPackage());
	if (ClipboardInput != nullptr)
	{
		ClipboardContent->FunctionInputs.Add(ClipboardInput);
	}
}

bool UNiagaraStackFunctionInput::TestCanPasteWithMessage(const UNiagaraClipboardContent* ClipboardContent, FText& OutMessage) const
{
	if (ClipboardContent->FunctionInputs.Num() == 0 || GetIsEnabledAndOwnerIsEnabled() == false)
	{
		// Empty clipboard, or disabled don't allow paste, but be silent.
		return false;
	}
	else if (ClipboardContent->FunctionInputs.Num() == 1)
	{
		const UNiagaraClipboardFunctionInput* ClipboardFunctionInput = ClipboardContent->FunctionInputs[0];
		if (ClipboardFunctionInput != nullptr)
		{
			if (ClipboardFunctionInput->InputType == InputType)
			{
				if (ClipboardFunctionInput->ValueMode == ENiagaraClipboardFunctionInputValueMode::Dynamic)
				{
					if (ClipboardFunctionInput->Dynamic->Script.IsValid() == false)
					{
						OutMessage = LOCTEXT("CantPasteInvalidDynamicInputScript", "Can not paste the dynamic input because its script is no longer valid.");
						return false;
					}
				}
				OutMessage = LOCTEXT("PastMessage", "Paste the input from the clipboard here.");
				return true;
			}
			else
			{
				OutMessage = LOCTEXT("CantPasteIncorrectType", "Can not paste inputs with mismatched types.");
				return false;
			}
		}
		return false;
	}
	else
	{
		OutMessage = LOCTEXT("CantPasteMultipleInputs", "Can't paste multiple inputs onto a single input.");
		return false;
	}
}

FText UNiagaraStackFunctionInput::GetPasteTransactionText(const UNiagaraClipboardContent* ClipboardContent) const
{
	return LOCTEXT("PasteInputTransactionText", "Paste niagara inputs");
}

void UNiagaraStackFunctionInput::Paste(const UNiagaraClipboardContent* ClipboardContent, FText& OutPasteWarning)
{
	checkf(ClipboardContent != nullptr && ClipboardContent->FunctionInputs.Num() == 1, TEXT("Clipboard must not be null, and must contain a single input.  Call TestCanPasteWithMessage to validate"));

	const UNiagaraClipboardFunctionInput* ClipboardInput = ClipboardContent->FunctionInputs[0];
	if (ClipboardInput != nullptr && ClipboardInput->InputType == InputType)
	{
		SetValueFromClipboardFunctionInput(*ClipboardInput);
	}
}

FText UNiagaraStackFunctionInput::GetValueToolTip() const
{
	if (IsFinalized())
	{
		return FText();
	}

	if (ValueToolTipCache.IsSet() == false)
	{
		ValueToolTipCache = FText();
		switch (InputValues.Mode)
		{
		case EValueMode::Data:
		{
			FString DataInterfaceDescription = InputValues.DataObject->GetClass()->GetDescription();
			if (DataInterfaceDescription.Len() > 0)
			{
				ValueToolTipCache = FText::FromString(DataInterfaceDescription);
			}
			break;
		}
		case EValueMode::DefaultFunction:
			if (InputValues.DefaultFunctionNode->FunctionScript != nullptr)
			{
				ValueToolTipCache = InputValues.DefaultFunctionNode->FunctionScript->Description;
			}
			break;
		case EValueMode::Dynamic:
			if (InputValues.DynamicNode->FunctionScript != nullptr)
			{
				ValueToolTipCache = InputValues.DynamicNode->FunctionScript->Description;
			}
			break;
		case EValueMode::InvalidOverride:
			ValueToolTipCache = LOCTEXT("InvalidOverrideToolTip", "The script is in an invalid and unrecoverable state for this\ninput.  Resetting to default may fix this issue.");
			break;
		case EValueMode::Linked:
		{
			FNiagaraVariable InputVariable(InputType, InputValues.LinkedHandle.GetParameterHandleString());
			if (FNiagaraConstants::IsNiagaraConstant(InputVariable))
			{
				const FNiagaraVariableMetaData* FoundMetaData = FNiagaraConstants::GetConstantMetaData(InputVariable);
				if (FoundMetaData != nullptr)
				{
					ValueToolTipCache = FoundMetaData->Description;
				}
			}
			break;
		}
		case EValueMode::UnsupportedDefault:
			ValueToolTipCache = LOCTEXT("UnsupportedDefault", "The defalut value defined in the script graph\nis custom and can not be shown in the selection stack.");
			break;
		}
	}
	return ValueToolTipCache.GetValue();
}

void UNiagaraStackFunctionInput::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	AliasedInputParameterHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputParameterHandle, OwningFunctionCallNode.Get());
	RapidIterationParameter = CreateRapidIterationVariable(AliasedInputParameterHandle.GetParameterHandleString());

	RefreshFromMetaData();
	RefreshValues();

	if (InputValues.DynamicNode.IsValid())
	{
		if (MessageManagerRefreshHandle.IsValid() == false)
		{
			MessageManagerRefreshHandle = FNiagaraMessageManager::Get()->GetOnRequestRefresh().AddUObject(this, &UNiagaraStackFunctionInput::OnMessageManagerRefresh);
		}
	}
	else
	{
		if (MessageManagerRefreshHandle.IsValid())
		{
			FNiagaraMessageManager::Get()->GetOnRequestRefresh().Remove(MessageManagerRefreshHandle);
			MessageManagerRefreshHandle.Reset();
		}
	}

	if (InputValues.Mode == EValueMode::Dynamic && InputValues.DynamicNode.IsValid())
	{
		if (InputValues.DynamicNode->FunctionScript != nullptr)
		{
			UNiagaraStackFunctionInputCollection* DynamicInputEntry = FindCurrentChildOfTypeByPredicate<UNiagaraStackFunctionInputCollection>(CurrentChildren,
				[=](UNiagaraStackFunctionInputCollection* CurrentFunctionInputEntry)
			{
				return CurrentFunctionInputEntry->GetInputFunctionCallNode() == InputValues.DynamicNode.Get() &&
					CurrentFunctionInputEntry->GetModuleNode() == OwningModuleNode.Get();
			});

			if (DynamicInputEntry == nullptr)
			{
				DynamicInputEntry = NewObject<UNiagaraStackFunctionInputCollection>(this);
				DynamicInputEntry->Initialize(CreateDefaultChildRequiredData(), *OwningModuleNode, *InputValues.DynamicNode.Get(), GetOwnerStackItemEditorDataKey());
				DynamicInputEntry->SetShouldShowInStack(false);
			}

			if (InputValues.DynamicNode->FunctionScript != nullptr)
			{
				if (InputValues.DynamicNode->FunctionScript->bDeprecated)
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("ScriptName"), FText::FromString(InputValues.DynamicNode->GetFunctionName()));

					if (InputValues.DynamicNode->FunctionScript->DeprecationRecommendation != nullptr)
					{
						Args.Add(TEXT("Recommendation"), FText::FromString(InputValues.DynamicNode->FunctionScript->DeprecationRecommendation->GetPathName()));
					}

					if (InputValues.DynamicNode->FunctionScript->DeprecationMessage.IsEmptyOrWhitespace() == false)
					{
						Args.Add(TEXT("Message"), InputValues.DynamicNode->FunctionScript->DeprecationMessage);
					}

					FText FormatString = LOCTEXT("DynamicInputScriptDeprecationUnknownLong", "The script asset for the assigned dynamic input {ScriptName} has been deprecated.");

					if (InputValues.DynamicNode->FunctionScript->DeprecationRecommendation != nullptr &&
						InputValues.DynamicNode->FunctionScript->DeprecationMessage.IsEmptyOrWhitespace() == false)
					{
						FormatString = LOCTEXT("DynamicInputScriptDeprecationMessageAndRecommendationLong", "The script asset for the assigned dynamic input {ScriptName} has been deprecated. Reason:\n{Message}.\nSuggested replacement: {Recommendation}");
					}
					else if (InputValues.DynamicNode->FunctionScript->DeprecationRecommendation != nullptr)
					{
						FormatString = LOCTEXT("DynamicInputScriptDeprecationLong", "The script asset for the assigned dynamic input {ScriptName} has been deprecated. Suggested replacement: {Recommendation}");
					}
					else if (InputValues.DynamicNode->FunctionScript->DeprecationMessage.IsEmptyOrWhitespace() == false)
					{
						FormatString = LOCTEXT("DynamicInputScriptDeprecationMessageLong", "The script asset for the assigned dynamic input {ScriptName} has been deprecated. Reason:\n{Message}");
					}

					FText LongMessage = FText::Format(FormatString, Args);

					int32 AddIdx = NewIssues.Add(FStackIssue(
						EStackIssueSeverity::Warning,
						LOCTEXT("DynamicInputScriptDeprecationShort", "Deprecated dynamic input"),
						LongMessage,
						GetStackEditorDataKey(),
						false,
						{
							FStackIssueFix(
								LOCTEXT("SelectNewDynamicInputScriptFix", "Select a new dynamic input script"),
								FStackIssueFixDelegate::CreateLambda([this]() { this->bIsDynamicInputScriptReassignmentPending = true; })),
							FStackIssueFix(
								LOCTEXT("ResetDynamicInputFix", "Reset this input to its default value"),
								FStackIssueFixDelegate::CreateLambda([this]() { this->Reset(); }))
						}));

					if (InputValues.DynamicNode->FunctionScript->DeprecationRecommendation != nullptr)
					{
						NewIssues[AddIdx].InsertFix(0,
							FStackIssueFix(
							LOCTEXT("SelectNewDynamicInputScriptFixUseRecommended", "Use recommended replacement"),
							FStackIssueFixDelegate::CreateLambda([this]() 
								{ 
									if (InputValues.DynamicNode->FunctionScript->DeprecationRecommendation->GetUsage() != ENiagaraScriptUsage::DynamicInput)
									{
										FNiagaraEditorUtilities::WarnWithToastAndLog(LOCTEXT("FailedDynamicInputDeprecationReplacement", "Failed to replace dynamic input as recommended replacement script is not a dynamic input!"));
										return;
									}
									ReassignDynamicInputScript(InputValues.DynamicNode->FunctionScript->DeprecationRecommendation); 
								})));
					}
				}

				if (InputValues.DynamicNode->FunctionScript->bExperimental)
				{
					FText ErrorMessage;
					if (InputValues.DynamicNode->FunctionScript->ExperimentalMessage.IsEmptyOrWhitespace())
					{
						ErrorMessage = FText::Format(LOCTEXT("DynamicInputScriptExperimental", "The script asset for the dynamic input {0} is experimental, use with care!"), FText::FromString(InputValues.DynamicNode->GetFunctionName()));
					}
					else
					{
						FFormatNamedArguments Args;
						Args.Add(TEXT("Function"), FText::FromString(InputValues.DynamicNode->GetFunctionName()));
						Args.Add(TEXT("Message"), InputValues.DynamicNode->FunctionScript->ExperimentalMessage);
						ErrorMessage = FText::Format(LOCTEXT("DynamicInputScriptExperimentalReason", "The script asset for the dynamic input {Function} is experimental, reason: {Message}."), Args);
					}

					NewIssues.Add(FStackIssue(
						EStackIssueSeverity::Info,
						LOCTEXT("DynamicInputScriptExperimentalShort", "Experimental dynamic input"),
						ErrorMessage,
						GetStackEditorDataKey(),
						true));
				}
			}

			NewChildren.Add(DynamicInputEntry);
		}
		else
		{
			NewIssues.Add(FStackIssue(
				EStackIssueSeverity::Error,
				LOCTEXT("DynamicInputScriptMissingShort", "Missing dynamic input script"),
				FText::Format(LOCTEXT("DynamicInputScriptMissingLong", "The script asset for the assigned dynamic input {0} is missing."), FText::FromString(InputValues.DynamicNode->GetFunctionName())),
				GetStackEditorDataKey(),
				false,
				{
					FStackIssueFix(
						LOCTEXT("SelectNewDynamicInputScriptFix", "Select a new dynamic input script"),
						FStackIssueFixDelegate::CreateLambda([this]() { this->bIsDynamicInputScriptReassignmentPending = true; })),
					FStackIssueFix(
						LOCTEXT("ResetFix", "Reset this input to its default value"),
						FStackIssueFixDelegate::CreateLambda([this]() { this->Reset(); }))
				}));
		}
	}

	if (InputValues.Mode == EValueMode::Data && InputValues.DataObject.IsValid())
	{
		UNiagaraStackObject* ValueObjectEntry = FindCurrentChildOfTypeByPredicate<UNiagaraStackObject>(CurrentChildren,
			[=](UNiagaraStackObject* CurrentObjectEntry) { return CurrentObjectEntry->GetObject() == InputValues.DataObject.Get(); });

		if(ValueObjectEntry == nullptr)
		{
			ValueObjectEntry = NewObject<UNiagaraStackObject>(this);
			ValueObjectEntry->Initialize(CreateDefaultChildRequiredData(), InputValues.DataObject.Get(), GetOwnerStackItemEditorDataKey(), OwningFunctionCallNode.Get());
		}
		NewChildren.Add(ValueObjectEntry);
	}

	DisplayNameOverride.Reset();

	if (InputMetaData)
	{
		const FString* FoundDisplayName = InputMetaData->PropertyMetaData.Find(TEXT("DisplayName"));
		const FString* FoundDisplayNameArg0 = InputMetaData->PropertyMetaData.Find(TEXT("DisplayNameArg0"));
		if (FoundDisplayName != nullptr)
		{
			FString DisplayNameStr = *FoundDisplayName;
			if (FoundDisplayNameArg0 != nullptr)
			{
				TArray<FStringFormatArg> Args;
				Args.Add(FStringFormatArg(ResolveDisplayNameArgument(*FoundDisplayNameArg0)));
				DisplayNameStr = FString::Format(*DisplayNameStr, Args);
			}
			DisplayNameOverride = FText::FromString(DisplayNameStr);
		}	
	}

	if (InputValues.DynamicNode.IsValid())
	{
		TArray<TSharedRef<const INiagaraMessage>> Messages = FNiagaraMessageManager::Get()->GetMessagesForAssetKeyAndObjectKey(
			GetSystemViewModel()->GetMessageLogGuid(), FObjectKey(InputValues.DynamicNode.Get()));
		for (TSharedRef<const INiagaraMessage> Message : Messages)
		{
			NewIssues.Add(FNiagaraStackGraphUtilities::MessageManagerMessageToStackIssue(Message, GetStackEditorDataKey()));
		}
	}
}

class UNiagaraStackFunctionInputUtilities
{
public:
	static bool GetMaterialExpressionDynamicParameter(UNiagaraEmitter* InEmitter, FNiagaraEmitterInstance* InEmitterInstance, TArray<UMaterialExpressionDynamicParameter*>& OutDynamicParameterExpressions)
	{
		TArray<UMaterial*> Materials = GetMaterialFromEmitter(InEmitter, InEmitterInstance);
		
		OutDynamicParameterExpressions.Reset();

		// Find the dynamic parameters expression from the material.
		// @YannickLange todo: Notify user that the material did not have any dynamic parameters. Maybe add them from code?
		for (UMaterial* Material : Materials)
		{
			if (Material != nullptr)
			{
				for (UMaterialExpression* Expression : Material->Expressions)
				{
					UMaterialExpressionDynamicParameter* DynParamExpFound = Cast<UMaterialExpressionDynamicParameter>(Expression);

					if (DynParamExpFound != nullptr)
					{
						OutDynamicParameterExpressions.Add(DynParamExpFound);
					}
				}
			}
		}

		return OutDynamicParameterExpressions.Num() > 0;
	}

	static TArray<UMaterial*> GetMaterialFromEmitter(UNiagaraEmitter* InEmitter, FNiagaraEmitterInstance* InEmitterInstance)
	{
		TArray<UMaterial*> ResultMaterials;
		if (InEmitter != nullptr)
		{
			for (UNiagaraRendererProperties* RenderProperties : InEmitter->GetRenderers())
			{
				TArray<UMaterialInterface*> UsedMaterialInteraces;
				RenderProperties->GetUsedMaterials(InEmitterInstance, UsedMaterialInteraces);
				for (UMaterialInterface* UsedMaterialInterface : UsedMaterialInteraces)
				{
					if (UsedMaterialInterface != nullptr)
					{
						UMaterial* UsedMaterial = UsedMaterialInterface->GetBaseMaterial();
						if (UsedMaterial != nullptr)
						{
							ResultMaterials.AddUnique(UsedMaterial);
							break;
						}
					}
				}
			}
		}
		return ResultMaterials;
	}
};

FString UNiagaraStackFunctionInput::ResolveDisplayNameArgument(const FString& InArg) const
{
	if (InArg.StartsWith(TEXT("MaterialDynamicParam")))
	{
		TSharedPtr<FNiagaraEmitterViewModel> ThisEmitterViewModel = GetEmitterViewModel();
		TArray<UMaterialExpressionDynamicParameter*> ExpressionParams;
		FNiagaraEmitterInstance* Instance = ThisEmitterViewModel->GetSimulation().Pin().Get();
		if (ThisEmitterViewModel.IsValid() == false || !UNiagaraStackFunctionInputUtilities::GetMaterialExpressionDynamicParameter(ThisEmitterViewModel->GetEmitter(), Instance, ExpressionParams))
		{
			return InArg.Replace(TEXT("MaterialDynamic"), TEXT("")) + TEXT(" (No material found using dynamic params)");
		}
		
		FString Suffix = InArg.Right(3);
		int32 ParamIdx;
		LexFromString(ParamIdx, *Suffix.Left(1));
		int32 ParamSlotIdx;
		LexFromString(ParamSlotIdx, *Suffix.Right(1));
		
		if (ParamIdx < 0 || ParamIdx > 3 || ParamSlotIdx < 0 || ParamSlotIdx > 3)
		{
			return InArg.Replace(TEXT("MaterialDynamic"), TEXT("")) + TEXT(" (error parsing parameter name)");
		}

		FName ParamName = NAME_None;
		bool bAllSame = true;
		for (UMaterialExpressionDynamicParameter* Expression : ExpressionParams)
		{
			const FExpressionOutput& Output = Expression->GetOutputs()[ParamIdx];
			if (ParamSlotIdx == Expression->ParameterIndex)
			{
				if (ParamName == NAME_None)
				{
					ParamName = Output.OutputName;
				}
				else if (ParamName != Output.OutputName)
				{
					bAllSame = false;
				}
			}
		}

		if (ParamName != NAME_None)
		{
			if (bAllSame)
			{
				return ParamName.ToString();
			}
			else
			{
				return ParamName.ToString() + TEXT(" (Multiple Aliases Found)");
			}
		}

		return InArg.Replace(TEXT("MaterialDynamic"), TEXT("")) + TEXT(" (Parameter not used in materials.)");
	}
	return FString();
}

void UNiagaraStackFunctionInput::RefreshValues()
{
	if (ensureMsgf(IsStaticParameter() || InputParameterHandle.IsModuleHandle(), TEXT("Function inputs can only be generated for module paramters.")) == false)
	{
		return;
	}

	// First collect the default values which are used to figure out if an input can be reset, and are used to
	// determine the current displayed value.
	DefaultInputValues = FInputValues();
	UpdateValuesFromScriptDefaults(DefaultInputValues);

	FInputValues OldValues = InputValues;
	InputValues = FInputValues();

	// If there is an override pin available it's value will take precedence so check that first.
	UEdGraphPin* OverridePin = GetOverridePin();
	if (OverridePin != nullptr)
	{
		UpdateValuesFromOverridePin(OldValues, InputValues, *OverridePin);
	}
	else
	{
		if(InputType.IsDataInterface())
		{
			// Data interfaces must always be overridden in the stack, if there wasn't an override pin found set the mode to invalid override since the 
			// stack graph state isn't valid.
			InputValues.Mode = EValueMode::InvalidOverride;
		}
		else if (IsRapidIterationCandidate())
		{
			// If the value is a rapid iteration parameter it's a local value so copy it's value from the rapid iteration parameter store if it's in there,
			// otherwise copy the value from the default.
			InputValues.Mode = EValueMode::Local;
			InputValues.LocalStruct = MakeShared<FStructOnScope>(InputType.GetStruct());
			const uint8* RapidIterationParameterData = SourceScript->RapidIterationParameters.GetParameterData(RapidIterationParameter);
			if (DefaultInputValues.LocalStruct) // Numeric types can trigger crashes below, so extra safety has been added.
			{
				if (RapidIterationParameterData == nullptr)
				{
					RapidIterationParameterData = DefaultInputValues.LocalStruct->GetStructMemory();
				}

				if (InputType.GetSize() > 0 && InputValues.LocalStruct->GetStructMemory() && RapidIterationParameterData)
				{
					FMemory::Memcpy(InputValues.LocalStruct->GetStructMemory(), RapidIterationParameterData, InputType.GetSize());
				}
				else
				{
					UE_LOG(LogNiagaraEditor, Warning, TEXT("Type %s has no data! Cannot refresh values."), *InputType.GetName())
				}
			}
		}
		else
		{
			// Otherwise if there isn't an override pin and it's not a rapid iteration parameter use the default value.
			InputValues = DefaultInputValues;
		}
	}

	bCanResetCache.Reset();
	bCanResetToBaseCache.Reset();
	ValueToolTipCache.Reset();
	bIsScratchDynamicInputCache.Reset();
	ValueChangedDelegate.Broadcast();
}

void UNiagaraStackFunctionInput::RefreshFromMetaData()
{
	InputMetaData.Reset();
	if (OwningFunctionCallNode->IsA<UNiagaraNodeAssignment>())
	{
		// Set variables nodes have no metadata, but if they're setting a defined constant see if there's metadata for that.
		FNiagaraVariable InputVariable(InputType, InputParameterHandle.GetName());
		if (FNiagaraConstants::IsNiagaraConstant(InputVariable))
		{
			const FNiagaraVariableMetaData* FoundMetaData = FNiagaraConstants::GetConstantMetaData(InputVariable);
			if (FoundMetaData)
			{
				InputMetaData = *FoundMetaData;
			}
		}
	}
	else if (OwningFunctionCallNode->FunctionScript != nullptr)
	{
		// Otherwise just get it from the defining graph.
		UNiagaraGraph* FunctionGraph = CastChecked<UNiagaraScriptSource>(OwningFunctionCallNode->FunctionScript->GetSource())->NodeGraph;
		FNiagaraVariable InputVariable(InputType, InputParameterHandle.GetParameterHandleString());
		InputMetaData = FunctionGraph->GetMetaData(InputVariable);
	}

	if (InputMetaData.IsSet())
	{
		SetIsAdvanced(InputMetaData->bAdvancedDisplay);

		FText EditConditionError;
		EditCondition.Refresh(InputMetaData->EditCondition, EditConditionError);
		if (EditCondition.IsValid() && EditCondition.GetConditionInputType() == FNiagaraTypeDefinition::GetBoolDef())
		{
			TOptional<FNiagaraVariableMetaData> EditConditionInputMetadata = EditCondition.GetConditionInputMetaData();
			if (EditConditionInputMetadata.IsSet())
			{
				bShowEditConditionInline = EditConditionInputMetadata->bInlineEditConditionToggle;
			}
		}
		else
		{
			bShowEditConditionInline = false;
		}

		if (EditConditionError.IsEmpty() == false)
		{
			UE_LOG(LogNiagaraEditor, Warning, TEXT("Edit condition failed to bind.  Function: %s Input: %s Message: %s"), 
				*OwningFunctionCallNode->GetFunctionName(), *InputParameterHandle.GetName().ToString(), *EditConditionError.ToString());
		}

		FText VisibleConditionError;
		VisibleCondition.Refresh(InputMetaData->VisibleCondition, VisibleConditionError);

		if (VisibleConditionError.IsEmpty() == false)
		{
			UE_LOG(LogNiagaraEditor, Warning, TEXT("Visible condition failed to bind.  Function: %s Input: %s Message: %s"),
				*OwningFunctionCallNode->GetFunctionName(), *InputParameterHandle.GetName().ToString(), *VisibleConditionError.ToString());
		}

		bIsInlineEditConditionToggle = InputType == FNiagaraTypeDefinition::GetBoolDef() && 
			InputMetaData->bInlineEditConditionToggle;
	}
}

FText UNiagaraStackFunctionInput::GetDisplayName() const
{
	return DisplayNameOverride.IsSet() ? DisplayNameOverride.GetValue() : DisplayName;
}

const TArray<FNiagaraParameterHandle>& UNiagaraStackFunctionInput::GetInputParameterHandlePath() const
{
	return InputParameterHandlePath;
}

const FNiagaraParameterHandle& UNiagaraStackFunctionInput::GetInputParameterHandle() const
{
	return InputParameterHandle;
}

const FNiagaraParameterHandle& UNiagaraStackFunctionInput::GetLinkedValueHandle() const
{
	return InputValues.LinkedHandle;
}

void UNiagaraStackFunctionInput::SetLinkedValueHandle(const FNiagaraParameterHandle& InParameterHandle)
{
	if (InParameterHandle == InputValues.LinkedHandle)
	{
		return;
	}

	FScopedTransaction ScopedTransaction(LOCTEXT("UpdateLinkedInputValue", "Update linked input value"));
	RemoveOverridePin();

	if (IsRapidIterationCandidate())
	{
		RemoveRapidIterationParametersForAffectedScripts();
	}

	if (InParameterHandle != DefaultInputValues.LinkedHandle)
	{
		// Only set the linked value if it's actually different from the default.
		FNiagaraStackGraphUtilities::SetLinkedValueHandleForFunctionInput(GetOrCreateOverridePin(), InParameterHandle);
	}

	FNiagaraStackGraphUtilities::RelayoutGraph(*OwningFunctionCallNode->GetGraph());
	RefreshValues();
}

bool UsageRunsBefore(ENiagaraScriptUsage UsageA, ENiagaraScriptUsage UsageB, bool bCheckInterpSpawn = false)
{
	static TArray<ENiagaraScriptUsage> UsagesOrderedByExecution
	{
		ENiagaraScriptUsage::SystemSpawnScript,
		ENiagaraScriptUsage::SystemUpdateScript,
		ENiagaraScriptUsage::EmitterSpawnScript,
		ENiagaraScriptUsage::EmitterUpdateScript,
		ENiagaraScriptUsage::ParticleSpawnScript,
		ENiagaraScriptUsage::ParticleEventScript,	// When not using interpolated spawn
		ENiagaraScriptUsage::ParticleUpdateScript,
		ENiagaraScriptUsage::ParticleEventScript,	// When using interpolated spawn and is spawn
		ENiagaraScriptUsage::ParticleSimulationStageScript
	};

	int32 IndexA;
	int32 IndexB;
	if (bCheckInterpSpawn)
	{
		UsagesOrderedByExecution.FindLast(UsageA, IndexA);
		UsagesOrderedByExecution.FindLast(UsageB, IndexB);
	}
	else
	{
		UsagesOrderedByExecution.Find(UsageA, IndexA);
		UsagesOrderedByExecution.Find(UsageB, IndexB);
	}
	return IndexA < IndexB;
}

bool IsSpawnUsage(ENiagaraScriptUsage Usage)
{
	return
		Usage == ENiagaraScriptUsage::SystemSpawnScript ||
		Usage == ENiagaraScriptUsage::EmitterSpawnScript ||
		Usage == ENiagaraScriptUsage::ParticleSpawnScript;
}

FName GetNamespaceForUsage(ENiagaraScriptUsage Usage)
{
	switch (Usage)
	{
	case ENiagaraScriptUsage::ParticleSpawnScript:
	case ENiagaraScriptUsage::ParticleUpdateScript:
	case ENiagaraScriptUsage::ParticleEventScript:
	case ENiagaraScriptUsage::ParticleSimulationStageScript:
		return FNiagaraConstants::ParticleAttributeNamespace;
	case ENiagaraScriptUsage::EmitterSpawnScript:
	case ENiagaraScriptUsage::EmitterUpdateScript:
		return FNiagaraConstants::EmitterNamespace;
	case ENiagaraScriptUsage::SystemSpawnScript:
	case ENiagaraScriptUsage::SystemUpdateScript:
		return FNiagaraConstants::SystemNamespace;
	default:
		return NAME_None;
	}
}

void UNiagaraStackFunctionInput::GetAvailableParameterHandles(TArray<FNiagaraParameterHandle>& AvailableParameterHandles) const
{
	// Engine Handles.
	for (const FNiagaraVariable& SystemVariable : FNiagaraConstants::GetEngineConstants())
	{
		if (SystemVariable.GetType() == InputType)
		{
			AvailableParameterHandles.Add(FNiagaraParameterHandle::CreateEngineParameterHandle(SystemVariable));
		}
	}

	UNiagaraNodeOutput* CurrentOutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*OwningModuleNode);

	TArray<UNiagaraNodeOutput*> AllOutputNodes;
	if (GetEmitterViewModel().IsValid())
	{
		GetEmitterViewModel()->GetSharedScriptViewModel()->GetGraphViewModel()->GetGraph()->GetNodesOfClass<UNiagaraNodeOutput>(AllOutputNodes);
	}
	if (GetSystemViewModel()->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		GetSystemViewModel()->GetSystemScriptViewModel()->GetGraphViewModel()->GetGraph()->GetNodesOfClass<UNiagaraNodeOutput>(AllOutputNodes);
	}

	TArray<FNiagaraVariable> ExposedVars;
	GetSystemViewModel()->GetSystem().GetExposedParameters().GetParameters(ExposedVars);
	for (const FNiagaraVariable& ExposedVar : ExposedVars)
	{
		if (ExposedVar.GetType() == InputType)
		{
			AvailableParameterHandles.Add(FNiagaraParameterHandle::CreateEngineParameterHandle(ExposedVar));
		}
	}

	for (UNiagaraNodeOutput* OutputNode : AllOutputNodes)
	{
		// Check if this is in a spawn event handler and the emitter is not using interpolated spawn so we
		// we can hide particle update parameters
		bool bSpawnScript = false;
		if (CurrentOutputNode != nullptr && CurrentOutputNode->GetUsage() == ENiagaraScriptUsage::ParticleEventScript)
		{
			for (const FNiagaraEventScriptProperties &EventHandlerProps : GetEmitterViewModel()->GetEmitter()->GetEventHandlers())
			{
				if (EventHandlerProps.Script->GetUsageId() == CurrentOutputNode->ScriptTypeId)
				{
					bSpawnScript = EventHandlerProps.ExecutionMode == EScriptExecutionMode::SpawnedParticles;
					break;
				}
			}
		}
		bool bInterpolatedSpawn = GetEmitterViewModel().IsValid() && GetEmitterViewModel()->GetEmitter()->bInterpolatedSpawning;
		bool bCheckInterpSpawn = bInterpolatedSpawn || !bSpawnScript;
		if (OutputNode == CurrentOutputNode || (CurrentOutputNode != nullptr && UsageRunsBefore(OutputNode->GetUsage(), CurrentOutputNode->GetUsage(), bCheckInterpSpawn)) || (CurrentOutputNode != nullptr && IsSpawnUsage(CurrentOutputNode->GetUsage())))
		{
			TArray<FNiagaraParameterHandle> AvailableParameterHandlesForThisOutput;
			TArray<FNiagaraStackGraphUtilities::FStackNodeGroup> StackGroups;
			FNiagaraStackGraphUtilities::GetStackNodeGroups(*OutputNode, StackGroups);

			int32 CurrentModuleIndex = OutputNode == CurrentOutputNode
				? StackGroups.IndexOfByPredicate([=](const FNiagaraStackGraphUtilities::FStackNodeGroup Group) { return Group.EndNode == OwningModuleNode; })
				: INDEX_NONE;

			int32 MaxGroupIndex = CurrentModuleIndex != INDEX_NONE ? CurrentModuleIndex : StackGroups.Num() - 1;
			for (int32 i = 1; i < MaxGroupIndex; i++)
			{
				UNiagaraNodeFunctionCall* ModuleToCheck = Cast<UNiagaraNodeFunctionCall>(StackGroups[i].EndNode);
				FNiagaraParameterMapHistoryBuilder Builder;
				ModuleToCheck->BuildParameterMapHistory(Builder, false);

				if (Builder.Histories.Num() == 1)
				{
					for (int32 j = 0; j < Builder.Histories[0].Variables.Num(); j++)
					{
						FNiagaraVariable& HistoryVariable = Builder.Histories[0].Variables[j];
						FNiagaraParameterHandle AvailableHandle = FNiagaraParameterHandle(HistoryVariable.GetName());
						if (HistoryVariable.GetType() == InputType)
						{
							TArray<const UEdGraphPin*>& WriteHistory = Builder.Histories[0].PerVariableWriteHistory[j];
							for (const UEdGraphPin* WritePin : WriteHistory)
							{
								if (Cast<UNiagaraNodeParameterMapSet>(WritePin->GetOwningNode()) != nullptr)
								{
									AvailableParameterHandles.AddUnique(AvailableHandle);
									AvailableParameterHandlesForThisOutput.AddUnique(AvailableHandle);
									break;
								}
							}
						}
					}
				}
			}

			if (OutputNode != CurrentOutputNode && IsSpawnUsage(OutputNode->GetUsage()))
			{
				FName OutputNodeNamespace = GetNamespaceForUsage(OutputNode->GetUsage());
				if (OutputNodeNamespace.IsNone() == false)
				{
					for (FNiagaraParameterHandle& AvailableParameterHandleForThisOutput : AvailableParameterHandlesForThisOutput)
					{
						if (AvailableParameterHandleForThisOutput.GetNamespace() == OutputNodeNamespace)
						{
							AvailableParameterHandles.AddUnique(FNiagaraParameterHandle::CreateInitialParameterHandle(AvailableParameterHandleForThisOutput));
						}
					}
				}
			}
		}
	}

	//Parameter Collections
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> CollectionAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UNiagaraParameterCollection::StaticClass()->GetFName(), CollectionAssets);

	for (FAssetData& CollectionAsset : CollectionAssets)
	{
		UNiagaraParameterCollection* Collection = CastChecked<UNiagaraParameterCollection>(CollectionAsset.GetAsset());
		if (Collection)
		{
			for (const FNiagaraVariable& CollectionParam : Collection->GetParameters())
			{
				if (CollectionParam.GetType() == InputType)
				{
					AvailableParameterHandles.AddUnique(FNiagaraParameterHandle(CollectionParam.GetName()));
				}
			}
		}
	}
}

UNiagaraNodeFunctionCall* UNiagaraStackFunctionInput::GetDefaultFunctionNode() const
{
	return InputValues.DefaultFunctionNode.Get();
}

UNiagaraNodeFunctionCall* UNiagaraStackFunctionInput::GetDynamicInputNode() const
{
	return InputValues.DynamicNode.Get();
}

void UNiagaraStackFunctionInput::GetAvailableDynamicInputs(TArray<UNiagaraScript*>& AvailableDynamicInputs, bool bIncludeNonLibraryInputs)
{
	TArray<FAssetData> DynamicInputAssets;
	FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions DynamicInputScriptFilterOptions;
	DynamicInputScriptFilterOptions.ScriptUsageToInclude = ENiagaraScriptUsage::DynamicInput;
	DynamicInputScriptFilterOptions.bIncludeNonLibraryScripts = bIncludeNonLibraryInputs;
	FNiagaraEditorUtilities::GetFilteredScriptAssets(DynamicInputScriptFilterOptions, DynamicInputAssets);

	auto MatchesInputType = [this](UNiagaraScript* Script)
	{
		UNiagaraScriptSource* DynamicInputScriptSource = Cast<UNiagaraScriptSource>(Script->GetSource());
		TArray<UNiagaraNodeOutput*> OutputNodes;
		DynamicInputScriptSource->NodeGraph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);
		if (OutputNodes.Num() == 1)
		{
			TArray<UEdGraphPin*> InputPins;
			OutputNodes[0]->GetInputPins(InputPins);
			if (InputPins.Num() == 1)
			{
				const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
				FNiagaraTypeDefinition PinType = NiagaraSchema->PinToTypeDefinition(InputPins[0]);
				if (PinType == InputType)
				{
					return true;
				}
			}
		}
		return false;
	};

	for (const FAssetData& DynamicInputAsset : DynamicInputAssets)
	{
		UNiagaraScript* DynamicInputScript = Cast<UNiagaraScript>(DynamicInputAsset.GetAsset());
		if (DynamicInputScript != nullptr)
		{
			if(MatchesInputType(DynamicInputScript))
			{
				AvailableDynamicInputs.Add(DynamicInputScript);
			}
		}
	}

	for (TSharedRef<FNiagaraScratchPadScriptViewModel> ScratchPadScriptViewModel : GetSystemViewModel()->GetScriptScratchPadViewModel()->GetScriptViewModels())
	{
		if (MatchesInputType(ScratchPadScriptViewModel->GetOriginalScript()))
		{
			AvailableDynamicInputs.Add(ScratchPadScriptViewModel->GetOriginalScript());
		}
	}
}

void UNiagaraStackFunctionInput::SetDynamicInput(UNiagaraScript* DynamicInput, FString SuggestedName)
{
	FScopedTransaction ScopedTransaction(LOCTEXT("SetDynamicInput", "Make dynamic input"));

	UEdGraphPin& OverridePin = GetOrCreateOverridePin();
	RemoveNodesForOverridePin(OverridePin);
	if (IsRapidIterationCandidate())
	{
		RemoveRapidIterationParametersForAffectedScripts();
	}

	UNiagaraNodeFunctionCall* FunctionCallNode;
	FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(OverridePin, DynamicInput, FunctionCallNode, FGuid(), SuggestedName);
	FNiagaraStackGraphUtilities::InitializeStackFunctionInputs(GetSystemViewModel(), GetEmitterViewModel(), GetStackEditorData(), *OwningModuleNode, *FunctionCallNode);
	FNiagaraStackGraphUtilities::RelayoutGraph(*OwningFunctionCallNode->GetGraph());

	RefreshChildren();
}

FText UNiagaraStackFunctionInput::GetCustomExpressionText() const
{
	return InputValues.ExpressionNode != nullptr ? FText::FromString(InputValues.ExpressionNode->GetCustomHlsl()) : FText();
}

void UNiagaraStackFunctionInput::SetCustomExpression(const FString& InCustomExpression)
{
	FScopedTransaction ScopedTransaction(LOCTEXT("SetCustomExpressionInput", "Make custom expression input"));

	UEdGraphPin& OverridePin = GetOrCreateOverridePin();
	RemoveNodesForOverridePin(OverridePin);
	if (IsRapidIterationCandidate())
	{
		RemoveRapidIterationParametersForAffectedScripts();
	}

	UNiagaraNodeCustomHlsl* FunctionCallNode;
	FNiagaraStackGraphUtilities::SetCustomExpressionForFunctionInput(OverridePin, InCustomExpression, FunctionCallNode);
	FNiagaraStackGraphUtilities::InitializeStackFunctionInputs(GetSystemViewModel(), GetEmitterViewModel(), GetStackEditorData(), *OwningModuleNode, *FunctionCallNode);
	FNiagaraStackGraphUtilities::RelayoutGraph(*OwningFunctionCallNode->GetGraph());

	RefreshChildren();
}

void UNiagaraStackFunctionInput::SetScratch()
{
	FScopedTransaction ScopedTransaction(LOCTEXT("SetScratch", "Make new scratch dynamic input"));
	TSharedPtr<FNiagaraScratchPadScriptViewModel> ScratchScriptViewModel = GetSystemViewModel()->GetScriptScratchPadViewModel()->CreateNewScript(ENiagaraScriptUsage::DynamicInput, SourceScript->GetUsage(), InputType);
	if (ScratchScriptViewModel.IsValid())
	{
		SetDynamicInput(ScratchScriptViewModel->GetOriginalScript());
		GetSystemViewModel()->GetScriptScratchPadViewModel()->FocusScratchPadScriptViewModel(ScratchScriptViewModel.ToSharedRef());
		ScratchScriptViewModel->SetIsPendingRename(true);
	}
}

TSharedPtr<const FStructOnScope> UNiagaraStackFunctionInput::GetLocalValueStruct()
{
	return InputValues.LocalStruct;
}

UNiagaraDataInterface* UNiagaraStackFunctionInput::GetDataValueObject()
{
	return InputValues.DataObject.Get();
}

void UNiagaraStackFunctionInput::NotifyBeginLocalValueChange()
{
	GEditor->BeginTransaction(LOCTEXT("BeginEditModuleInputLocalValue", "Edit input local value."));
}

void UNiagaraStackFunctionInput::NotifyEndLocalValueChange()
{
	if (GEditor->IsTransactionActive())
	{
		GEditor->EndTransaction();
	}
}

bool UNiagaraStackFunctionInput::IsRapidIterationCandidate() const
{
	// Rapid iteration parameters will only be used if the input is not static and the input value default is a
	// local value, if it's linked in graph or through metadata or a default dynamic input the compiler generates
	// code for that instead.
	return !IsStaticParameter() && FNiagaraStackGraphUtilities::IsRapidIterationType(InputType) && DefaultInputValues.Mode == EValueMode::Local;
}

void UNiagaraStackFunctionInput::SetLocalValue(TSharedRef<FStructOnScope> InLocalValue)
{
	checkf(InLocalValue->GetStruct() == InputType.GetStruct(), TEXT("Can not set an input to an unrelated type."));

	if (InputValues.Mode == EValueMode::Local && FNiagaraEditorUtilities::DataMatches(*InputValues.LocalStruct.Get(), InLocalValue.Get()))
	{
		// The value matches the current value so noop.
		return;
	}

	TGuardValue<bool> UpdateGuard(bUpdatingLocalValueDirectly, true);
	FScopedTransaction ScopedTransaction(LOCTEXT("UpdateInputLocalValue", "Update input local value"));
	bool bGraphWillNeedRelayout = false;
	UEdGraphPin* OverridePin = GetOverridePin();

	if (OverridePin != nullptr && OverridePin->LinkedTo.Num() > 0)
	{
		// If there is an override pin and it's linked we'll need to remove all of the linked nodes to set a local value.
		RemoveNodesForOverridePin(*OverridePin);
		bGraphWillNeedRelayout = true;
	}

	if (IsRapidIterationCandidate())
	{
		// If there is currently an override pin, it must be removed to allow the rapid iteration parameter to be used.
		if (OverridePin != nullptr)
		{
			UNiagaraNode* OverrideNode = CastChecked<UNiagaraNode>(OverridePin->GetOwningNode());
			OverrideNode->Modify();
			OverrideNode->RemovePin(OverridePin);
			bGraphWillNeedRelayout = true;
		}

		// Update the value on all affected scripts.
		for (TWeakObjectPtr<UNiagaraScript> Script : AffectedScripts)
		{
			bool bAddParameterIfMissing = true;
			Script->Modify();
			Script->RapidIterationParameters.SetParameterData(InLocalValue->GetStructMemory(), RapidIterationParameter, bAddParameterIfMissing);
		}
	}
	else
	{
		// If rapid iteration parameters can't be used the string representation of the value needs to be set on the override pin
		// for this input.  For static switch inputs the override pin in on the owning function call node and for standard parameter
		// pins the override pin is on the override parameter map set node.
		FNiagaraVariable LocalValueVariable(InputType, NAME_None);
		LocalValueVariable.SetData(InLocalValue->GetStructMemory());
		FString PinDefaultValue;
		const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
		if (ensureMsgf(NiagaraSchema->TryGetPinDefaultValueFromNiagaraVariable(LocalValueVariable, PinDefaultValue),
			TEXT("Could not generate default value string for non-rapid iteration parameter.")))
		{
			if (OverridePin == nullptr)
			{
				OverridePin = &GetOrCreateOverridePin();
				bGraphWillNeedRelayout = true;
			}

			OverridePin->Modify();
			OverridePin->DefaultValue = PinDefaultValue;
			Cast<UNiagaraNode>(OverridePin->GetOwningNode())->MarkNodeRequiresSynchronization(TEXT("OverridePin Default Value Changed"), true);
		}
	}
	
	if (bGraphWillNeedRelayout)
	{
		FNiagaraStackGraphUtilities::RelayoutGraph(*OwningFunctionCallNode->GetNiagaraGraph());
	}

	RefreshValues();
}

bool UNiagaraStackFunctionInput::CanReset() const
{
	if (bCanResetCache.IsSet() == false)
	{
		bool bNewCanReset = false;
		if (DefaultInputValues.Mode == EValueMode::None)
		{
			// Can't reset if no valid default was set.
			bNewCanReset = false;
		}
		else if (InputValues.Mode != DefaultInputValues.Mode)
		{
			// If the current value mode is different from the default value mode, it can always be reset.
			bNewCanReset = true;
		}
		else
		{
			switch (InputValues.Mode)
			{
			case EValueMode::Data:
				bNewCanReset = InputValues.DataObject->Equals(DefaultInputValues.DataObject.Get()) == false;
				break;
			case EValueMode::Dynamic:
				// For now assume that default dynamic inputs can always be reset to default since they're not currently supported properly.
				// TODO: Fix when default dynamic inputs are properly supported.
				bNewCanReset = false;
				break;
			case EValueMode::Linked:
				bNewCanReset = InputValues.LinkedHandle != DefaultInputValues.LinkedHandle;
				break;
			case EValueMode::Local:
				bNewCanReset =
					DefaultInputValues.LocalStruct.IsValid() &&
					InputValues.LocalStruct->GetStruct() == DefaultInputValues.LocalStruct->GetStruct() &&
					FMemory::Memcmp(InputValues.LocalStruct->GetStructMemory(), DefaultInputValues.LocalStruct->GetStructMemory(), InputType.GetSize());
				break;
			}
		}
		bCanResetCache = bNewCanReset;
	}
	return bCanResetCache.GetValue();
}

bool UNiagaraStackFunctionInput::UpdateRapidIterationParametersForAffectedScripts(const uint8* Data)
{
	for (TWeakObjectPtr<UNiagaraScript> Script : AffectedScripts)
	{
		Script->Modify();
	}

	for (TWeakObjectPtr<UNiagaraScript> Script : AffectedScripts)
	{
		bool bAddParameterIfMissing = true;
		Script->RapidIterationParameters.SetParameterData(Data, RapidIterationParameter, bAddParameterIfMissing);
	}
	GetSystemViewModel()->ResetSystem();
	return true;
}

bool UNiagaraStackFunctionInput::RemoveRapidIterationParametersForAffectedScripts()
{
	for (TWeakObjectPtr<UNiagaraScript> Script : AffectedScripts)
	{
		Script->Modify();
	}

	for (TWeakObjectPtr<UNiagaraScript> Script : AffectedScripts)
	{
		if (Script->RapidIterationParameters.RemoveParameter(RapidIterationParameter))
		{
			UE_LOG(LogNiagaraEditor, Log, TEXT("Removed Var '%s' from Script %s"), *RapidIterationParameter.GetName().ToString(), *Script->GetFullName());
		}
	}
	return true;
}

void UNiagaraStackFunctionInput::Reset()
{
	if (CanReset())
	{
		if (DefaultInputValues.Mode == EValueMode::Data)
		{
			FScopedTransaction ScopedTransaction(LOCTEXT("ResetInputObjectTransaction", "Reset the inputs data interface object to default."));
			if (InputValues.Mode == EValueMode::Data)
			{
				// If there is already a valid data object just copy from the default to the current value.
				InputValues.DataObject->Modify();
				DefaultInputValues.DataObject->CopyTo(InputValues.DataObject.Get());
			}
			else
			{
				// Otherwise remove the current nodes from the override pin and set a new data object and copy the values from the default.
				UEdGraphPin& OverridePin = GetOrCreateOverridePin();
				RemoveNodesForOverridePin(OverridePin);

				FString InputNodeName = InputParameterHandlePath[0].GetName().ToString();
				for (int32 i = 1; i < InputParameterHandlePath.Num(); i++)
				{
					InputNodeName += "." + InputParameterHandlePath[i].GetName().ToString();
				}

				UNiagaraDataInterface* InputValueObject;
				FNiagaraStackGraphUtilities::SetDataValueObjectForFunctionInput(OverridePin, const_cast<UClass*>(InputType.GetClass()), InputNodeName, InputValueObject);
				DefaultInputValues.DataObject->CopyTo(InputValueObject);

				FNiagaraStackGraphUtilities::RelayoutGraph(*OwningFunctionCallNode->GetGraph());
			}
		}
		else if (DefaultInputValues.Mode == EValueMode::Linked)
		{
			FScopedTransaction ScopedTransaction(LOCTEXT("ResetInputLinkedValueTransaction", "Reset the input to its default linked value."));
			SetLinkedValueHandle(DefaultInputValues.LinkedHandle);
		}
		else if (DefaultInputValues.Mode == EValueMode::Local)
		{
			FScopedTransaction ScopedTransaction(LOCTEXT("ResetInputLocalValueTransaction", "Reset the input to its default local value."));
			SetLocalValue(DefaultInputValues.LocalStruct.ToSharedRef());
		}
		else if (DefaultInputValues.Mode == EValueMode::DefaultFunction ||
			DefaultInputValues.Mode == EValueMode::UnsupportedDefault)
		{
			FScopedTransaction ScopedTransaction(LOCTEXT("ResetInputValueTransaction", "Reset the input to its default value."));
			RemoveOverridePin();
		}
		else
		{
			ensureMsgf(false, TEXT("Attempted to reset a function input to default without a valid default."));
		}
	}
	RefreshChildren();
}

bool UNiagaraStackFunctionInput::IsStaticParameter() const
{
	return ParameterBehavior == EStackParameterBehavior::Static;
}

bool UNiagaraStackFunctionInput::CanResetToBase() const
{
	if (HasBaseEmitter())
	{
		if (bCanResetToBaseCache.IsSet() == false)
		{
			bool bIsModuleInput = OwningFunctionCallNode == OwningModuleNode;
			if (bIsModuleInput)
			{
				TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();

				UNiagaraNodeOutput* OutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*OwningFunctionCallNode.Get());
				if(MergeManager->IsMergeableScriptUsage(OutputNode->GetUsage()))
				{
					const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetParentEmitter();

					bCanResetToBaseCache = BaseEmitter != nullptr && MergeManager->IsModuleInputDifferentFromBase(
						*GetEmitterViewModel()->GetEmitter(),
						*BaseEmitter,
						OutputNode->GetUsage(),
						OutputNode->GetUsageId(),
						OwningModuleNode->NodeGuid,
						InputParameterHandle.GetName().ToString());
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
		return bCanResetToBaseCache.GetValue();
	}
	return false;
}

void UNiagaraStackFunctionInput::ResetToBase()
{
	if (CanResetToBase())
	{
		TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();

		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		UNiagaraNodeOutput* OutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*OwningFunctionCallNode.Get());

		FScopedTransaction ScopedTransaction(LOCTEXT("ResetInputToBaseTransaction", "Reset this input to match the parent emitter."));
		FNiagaraScriptMergeManager::FApplyDiffResults Results = MergeManager->ResetModuleInputToBase(
			*GetEmitterViewModel()->GetEmitter(),
			*BaseEmitter,
			OutputNode->GetUsage(),
			OutputNode->GetUsageId(),
			OwningModuleNode->NodeGuid,
			InputParameterHandle.GetName().ToString());

		if (Results.bSucceeded)
		{
			// If resetting to the base succeeded, an unknown number of rapid iteration parameters may have been added.  To fix
			// this copy all of the owning scripts rapid iteration parameters to all other affected scripts.
			// TODO: Either the merge should take care of this directly, or at least provide more information about what changed.
			UNiagaraScript* OwningScript = GetEmitterViewModel()->GetEmitter()->GetScript(OutputNode->GetUsage(), OutputNode->GetUsageId());
			TArray<FNiagaraVariable> OwningScriptRapidIterationParameters;
			OwningScript->RapidIterationParameters.GetParameters(OwningScriptRapidIterationParameters);
			if (OwningScriptRapidIterationParameters.Num() > 0)
			{
				for (TWeakObjectPtr<UNiagaraScript> AffectedScript : AffectedScripts)
				{
					if (AffectedScript.Get() != OwningScript)
					{
						AffectedScript->Modify();
						for (FNiagaraVariable& OwningScriptRapidIterationParameter : OwningScriptRapidIterationParameters)
						{
							bool bAddParameterIfMissing = true;
							AffectedScript->RapidIterationParameters.SetParameterData(
								OwningScript->RapidIterationParameters.GetParameterData(OwningScriptRapidIterationParameter), OwningScriptRapidIterationParameter, bAddParameterIfMissing);
						}
					}
				}
			}
		}
		RefreshChildren();
	}
}

FNiagaraVariable UNiagaraStackFunctionInput::CreateRapidIterationVariable(const FName& InName)
{
	UNiagaraNodeOutput* OutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*OwningModuleNode.Get());
	FString UniqueEmitterName = GetEmitterViewModel().IsValid() ? GetEmitterViewModel()->GetEmitter()->GetUniqueEmitterName() : FString();
	return FNiagaraStackGraphUtilities::CreateRapidIterationParameter(UniqueEmitterName, OutputNode->GetUsage(), InName, InputType);
}

void UNiagaraStackFunctionInput::OnMessageManagerRefresh(const FGuid& MessageJobBatchAssetKey, const TArray<TSharedRef<const INiagaraMessage>> NewMessages)
{
	if (InputValues.DynamicNode.IsValid() && GetSystemViewModel()->GetMessageLogGuid() == MessageJobBatchAssetKey)
	{
		if (FNiagaraMessageManager::Get()->GetMessagesForAssetKeyAndObjectKey(MessageJobBatchAssetKey, FObjectKey(InputValues.DynamicNode.Get())).Num() > 0)
		{
			RefreshChildren();
		}
	}
}

bool UNiagaraStackFunctionInput::SupportsRename() const
{
	// Only module level assignment node inputs can be renamed.
	return OwningAssignmentNode.IsValid() && InputParameterHandlePath.Num() == 1 &&
		OwningAssignmentNode->FindAssignmentTarget(InputParameterHandle.GetName()) != INDEX_NONE;
}

void UNiagaraStackFunctionInput::OnRenamed(FText NewNameText)
{
	FName NewName(*NewNameText.ToString());
	if (InputParameterHandle.GetName() != NewName && OwningAssignmentNode.IsValid())
	{
		bool bIsCurrentlyExpanded = GetStackEditorData().GetStackEntryIsExpanded(FNiagaraStackGraphUtilities::GenerateStackModuleEditorDataKey(*OwningAssignmentNode.Get()), false);

		FScopedTransaction ScopedTransaction(LOCTEXT("RenameInput", "Rename this function's input."));
		if (ensureMsgf(OwningAssignmentNode->RenameAssignmentTarget(InputParameterHandle.GetName(), NewName), TEXT("Failed to rename assignment node input.")))
		{
			// Fixing up the stack graph and rapid iteration parameters must happen first so that when the stack is refreshed the UI is correct.
			FNiagaraParameterHandle NewInputParameterHandle = FNiagaraParameterHandle(InputParameterHandle.GetNamespace(), NewName);
			FNiagaraParameterHandle NewAliasedInputParameterHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(NewInputParameterHandle, OwningAssignmentNode.Get());
			UEdGraphPin* OverridePin = GetOverridePin();
			if (OverridePin != nullptr)
			{
				// If there is an override pin then the only thing that needs to happen is that it's name needs to be updated so that the value it
				// holds or is linked to stays intact.
				OverridePin->Modify();
				OverridePin->PinName = NewAliasedInputParameterHandle.GetParameterHandleString();
			}
			else if (IsRapidIterationCandidate())
			{
				// Otherwise if this is a valid rapid iteration parameter the values in the affected scripts need to be updated.
				FNiagaraVariable NewRapidIterationParameter = CreateRapidIterationVariable(NewAliasedInputParameterHandle.GetParameterHandleString());
				for (TWeakObjectPtr<UNiagaraScript> AffectedScript : AffectedScripts)
				{
					if (AffectedScript.IsValid())
					{
						AffectedScript->Modify();
						AffectedScript->RapidIterationParameters.RenameParameter(RapidIterationParameter, *NewRapidIterationParameter.GetName().ToString());
					}
				}
			}

			// Restore the expanded state with the new editor data key.
			FString NewStackEditorDataKey = FNiagaraStackGraphUtilities::GenerateStackFunctionInputEditorDataKey(*OwningAssignmentNode.Get(), NewInputParameterHandle);
			GetStackEditorData().SetStackEntryIsExpanded(NewStackEditorDataKey, bIsCurrentlyExpanded);

			// This refresh call must come last because it will finalize this input entry which would cause earlier fixup to fail.
			OwningAssignmentNode->RefreshFromExternalChanges();
			ensureMsgf(IsFinalized(), TEXT("Input not finalized when renamed."));
		}
	}
}

bool UNiagaraStackFunctionInput::CanDeleteInput() const
{
	return GetInputFunctionCallNode().IsA(UNiagaraNodeAssignment::StaticClass());
}

void UNiagaraStackFunctionInput::DeleteInput()
{
	if (UNiagaraNodeAssignment* NodeAssignment = Cast<UNiagaraNodeAssignment>(OwningFunctionCallNode.Get()))
	{
		FScopedTransaction ScopedTransaction(LOCTEXT("RemoveInputTransaction", "Remove Input"));
		
		// If there is an override pin and connected nodes, remove them before removing the input since removing
		// the input will prevent us from finding the override pin.
		RemoveOverridePin();

		FNiagaraVariable Var = FNiagaraVariable(GetInputType(), GetInputParameterHandle().GetName());
		NodeAssignment->Modify();
		NodeAssignment->RemoveParameter(Var);
	}
}

void UNiagaraStackFunctionInput::GetNamespacesForNewParameters(TArray<FName>& OutNamespacesForNewParameters) const
{
	UNiagaraNodeOutput* OutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*OwningFunctionCallNode);
	bool bIsEditingSystem = GetSystemViewModel()->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset;

	switch (OutputNode->GetUsage())
	{
	case ENiagaraScriptUsage::ParticleSpawnScript:
	case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated:
	case ENiagaraScriptUsage::ParticleUpdateScript:
	case ENiagaraScriptUsage::ParticleEventScript:
	case ENiagaraScriptUsage::ParticleSimulationStageScript:
	{
		OutNamespacesForNewParameters.Add(FNiagaraConstants::ParticleAttributeNamespace);
		OutNamespacesForNewParameters.Add(FNiagaraConstants::EmitterNamespace);
		break;
	}
	case ENiagaraScriptUsage::EmitterSpawnScript:
	case ENiagaraScriptUsage::EmitterUpdateScript:
	{
		OutNamespacesForNewParameters.Add(FNiagaraConstants::EmitterNamespace);
		break;
	}
	}

	if (bIsEditingSystem)
	{
		OutNamespacesForNewParameters.Add(FNiagaraConstants::UserNamespace);
		OutNamespacesForNewParameters.Add(FNiagaraConstants::SystemNamespace);
	}
	OutNamespacesForNewParameters.Add(FNiagaraConstants::TransientNamespace);
}

UNiagaraStackFunctionInput::FOnValueChanged& UNiagaraStackFunctionInput::OnValueChanged()
{
	return ValueChangedDelegate;
}

bool UNiagaraStackFunctionInput::GetHasEditCondition() const
{
	return EditCondition.IsValid();
}

bool UNiagaraStackFunctionInput::GetShowEditConditionInline() const
{
	return bShowEditConditionInline;
}

bool UNiagaraStackFunctionInput::GetEditConditionEnabled() const
{
	return EditCondition.IsValid() && EditCondition.GetConditionIsEnabled();
}

void UNiagaraStackFunctionInput::SetEditConditionEnabled(bool bIsEnabled)
{
	if(EditCondition.CanSetConditionIsEnabled())
	{ 
		EditCondition.SetConditionIsEnabled(bIsEnabled);
	}
}

bool UNiagaraStackFunctionInput::GetHasVisibleCondition() const
{
	return VisibleCondition.IsValid();
}

bool UNiagaraStackFunctionInput::GetVisibleConditionEnabled() const
{
	return VisibleCondition.IsValid() && VisibleCondition.GetConditionIsEnabled();
}

bool UNiagaraStackFunctionInput::GetIsInlineEditConditionToggle() const
{
	return bIsInlineEditConditionToggle;
}

bool UNiagaraStackFunctionInput::GetIsDynamicInputScriptReassignmentPending() const
{
	return bIsDynamicInputScriptReassignmentPending;
}

void UNiagaraStackFunctionInput::SetIsDynamicInputScriptReassignmentPending(bool bIsPending)
{
	bIsDynamicInputScriptReassignmentPending = bIsPending;
}

void UNiagaraStackFunctionInput::ReassignDynamicInputScript(UNiagaraScript* DynamicInputScript)
{
	if (ensureMsgf(InputValues.Mode == EValueMode::Dynamic && InputValues.DynamicNode != nullptr && InputValues.DynamicNode->GetClass() == UNiagaraNodeFunctionCall::StaticClass(),
		TEXT("Can not reassign the dynamic input script when tne input doesn't have a valid dynamic input.")))
	{
		FScopedTransaction ScopedTransaction(LOCTEXT("ReassignDynamicInputTransaction", "Reassign dynamic input script"));

		const FString OldName = InputValues.DynamicNode->GetFunctionName();

		InputValues.DynamicNode->Modify();

		UNiagaraClipboardContent* OldClipboardContent = nullptr;
		UNiagaraScript* OldScript = InputValues.DynamicNode->FunctionScript;
		if (DynamicInputScript->ConversionUtility != nullptr)
		{
			OldClipboardContent = UNiagaraClipboardContent::Create();
			Copy(OldClipboardContent);
		}

		InputValues.DynamicNode->FunctionScript = DynamicInputScript;

		// intermediate refresh to purge any rapid iteration parameters that have been removed in the new script
		RefreshChildren();

		InputValues.DynamicNode->SuggestName(FString());

		const FString NewName = InputValues.DynamicNode->GetFunctionName();
		UNiagaraSystem& System = GetSystemViewModel()->GetSystem();
		UNiagaraEmitter* Emitter = GetEmitterViewModel().IsValid() ? GetEmitterViewModel()->GetEmitter() : nullptr;
		FNiagaraStackGraphUtilities::RenameReferencingParameters(System, Emitter, *InputValues.DynamicNode.Get(), OldName, NewName);

		InputValues.DynamicNode->RefreshFromExternalChanges();

		InputValues.DynamicNode->MarkNodeRequiresSynchronization(TEXT("Dynamic input script reassigned."), true);
		RefreshChildren();
		
		if (DynamicInputScript->ConversionUtility != nullptr && OldClipboardContent != nullptr)
		{
			UNiagaraConvertInPlaceUtilityBase* ConversionUtility = NewObject< UNiagaraConvertInPlaceUtilityBase>(GetTransientPackage(), DynamicInputScript->ConversionUtility);

			UNiagaraClipboardContent* NewClipboardContent = UNiagaraClipboardContent::Create();
			Copy(NewClipboardContent);
			TArray<UNiagaraStackFunctionInputCollection*> DynamicInputCollections;
			GetUnfilteredChildrenOfType(DynamicInputCollections);

			FText ConvertMessage;
			if (ConversionUtility && DynamicInputCollections.Num() == 0)
			{
				bool bConverted = ConversionUtility->Convert(OldScript, OldClipboardContent, DynamicInputScript, DynamicInputCollections[0], NewClipboardContent, InputValues.DynamicNode.Get(), ConvertMessage);
				if (!ConvertMessage.IsEmptyOrWhitespace())
				{
					// Notify the end-user about the convert message, but continue the process as they could always undo.
					FNotificationInfo Msg(FText::Format(LOCTEXT("FixConvertInPlace", "Conversion Note: {0}"), ConvertMessage));
					Msg.ExpireDuration = 5.0f;
					Msg.bFireAndForget = true;
					Msg.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Note"));
					FSlateNotificationManager::Get().AddNotification(Msg);
				}
			}
		}

	}
}

bool UNiagaraStackFunctionInput::GetShouldPassFilterForVisibleCondition() const
{
	return bIsVisible && (GetHasVisibleCondition() == false || GetVisibleConditionEnabled());
}

const UNiagaraClipboardFunctionInput* UNiagaraStackFunctionInput::ToClipboardFunctionInput(UObject* InOuter) const
{
	const UNiagaraClipboardFunctionInput* ClipboardInput = nullptr;
	FName InputName = InputParameterHandle.GetName();
	TOptional<bool> bEditConditionValue = GetHasEditCondition() ? GetEditConditionEnabled() : TOptional<bool>();
	switch (InputValues.Mode)
	{
	case EValueMode::Local:
	{
		TArray<uint8> LocalValueData;
		LocalValueData.AddUninitialized(InputType.GetSize());
		FMemory::Memcpy(LocalValueData.GetData(), InputValues.LocalStruct->GetStructMemory(), InputType.GetSize());
		ClipboardInput = UNiagaraClipboardFunctionInput::CreateLocalValue(InOuter, InputName, InputType, bEditConditionValue, LocalValueData);
		break;
	}
	case EValueMode::Linked:
		ClipboardInput = UNiagaraClipboardFunctionInput::CreateLinkedValue(InOuter, InputName, InputType, bEditConditionValue, InputValues.LinkedHandle.GetParameterHandleString());
		break;
	case EValueMode::Data:
		ClipboardInput = UNiagaraClipboardFunctionInput::CreateDataValue(InOuter, InputName, InputType, bEditConditionValue, InputValues.DataObject.Get());
		break;
	case EValueMode::Expression:
		ClipboardInput = UNiagaraClipboardFunctionInput::CreateExpressionValue(InOuter, InputName, InputType, bEditConditionValue, InputValues.ExpressionNode->GetHlslText().ToString());
		break;
	case EValueMode::Dynamic:
	{
		ClipboardInput = UNiagaraClipboardFunctionInput::CreateDynamicValue(InOuter, InputName, InputType, bEditConditionValue, InputValues.DynamicNode->GetFunctionName(), InputValues.DynamicNode->FunctionScript);

		TArray<UNiagaraStackFunctionInputCollection*> DynamicInputCollections;
		GetUnfilteredChildrenOfType(DynamicInputCollections);
		for (UNiagaraStackFunctionInputCollection* DynamicInputCollection : DynamicInputCollections)
		{
			DynamicInputCollection->ToClipboardFunctionInputs(ClipboardInput->Dynamic, ClipboardInput->Dynamic->Inputs);
		}

		break;
	}
	case EValueMode::InvalidOverride:
	case EValueMode::UnsupportedDefault:
	case EValueMode::DefaultFunction:
		// Do nothing.
		break;
	default:
		ensureMsgf(false, TEXT("A new value mode was added without adding support for copy paste."));
		break;
	}
	return ClipboardInput;
}

void UNiagaraStackFunctionInput::SetValueFromClipboardFunctionInput(const UNiagaraClipboardFunctionInput& ClipboardFunctionInput)
{
	if (ensureMsgf(ClipboardFunctionInput.InputType == InputType, TEXT("Can not set input value from clipboard, input types don't match.")))
	{
		switch (ClipboardFunctionInput.ValueMode)
		{
		case ENiagaraClipboardFunctionInputValueMode::Local:
		{
			TSharedRef<FStructOnScope> ValueStruct = MakeShared<FStructOnScope>(InputType.GetStruct());
			FMemory::Memcpy(ValueStruct->GetStructMemory(), ClipboardFunctionInput.Local.GetData(), InputType.GetSize());
			SetLocalValue(ValueStruct);
			break;
		}
		case ENiagaraClipboardFunctionInputValueMode::Linked:
			SetLinkedValueHandle(FNiagaraParameterHandle(ClipboardFunctionInput.Linked));
			break;
		case ENiagaraClipboardFunctionInputValueMode::Data:
		{
			if (GetDataValueObject() == nullptr)
			{
				Reset();
			}
			UNiagaraDataInterface* InputDataInterface = GetDataValueObject();
			if (ensureMsgf(InputDataInterface != nullptr, TEXT("Data interface paste failed.  Current data value object null even after reset.")))
			{
				ClipboardFunctionInput.Data->CopyTo(InputDataInterface);
			}
			break;
		}
		case ENiagaraClipboardFunctionInputValueMode::Expression:
			SetCustomExpression(ClipboardFunctionInput.Expression);
			break;
		case ENiagaraClipboardFunctionInputValueMode::Dynamic:
			if (ensureMsgf(ClipboardFunctionInput.Dynamic->ScriptMode == ENiagaraClipboardFunctionScriptMode::ScriptAsset,
				TEXT("Dynamic input values can only be set from script asset clipboard functions.")))
			{
				UNiagaraScript* ClipboardFunctionScript = ClipboardFunctionInput.Dynamic->Script.Get();
				if (ClipboardFunctionScript != nullptr)
				{
					UNiagaraScript* NewDynamicInputScript;
					if (ClipboardFunctionScript->IsAsset() ||
						GetSystemViewModel()->GetScriptScratchPadViewModel()->GetViewModelForScript(ClipboardFunctionScript).IsValid())
					{
						// If the clipboard script is an asset, or it's in the scratch pad of the current asset, it can be used directly.
						NewDynamicInputScript = ClipboardFunctionScript;
					}
					else
					{
						// Otherwise it's a scratch pad script from another asset so we need to add a duplicate scratch pad script to this asset.
						NewDynamicInputScript = GetSystemViewModel()->GetScriptScratchPadViewModel()->CreateNewScriptAsDuplicate(ClipboardFunctionScript)->GetOriginalScript();
					}
					SetDynamicInput(NewDynamicInputScript, ClipboardFunctionInput.Dynamic->FunctionName);

					TArray<UNiagaraStackFunctionInputCollection*> DynamicInputCollections;
					GetUnfilteredChildrenOfType(DynamicInputCollections);
					for (UNiagaraStackFunctionInputCollection* DynamicInputCollection : DynamicInputCollections)
					{
						DynamicInputCollection->SetValuesFromClipboardFunctionInputs(ClipboardFunctionInput.Dynamic->Inputs);
					}
				}
			}
			break;
		default:
			ensureMsgf(false, TEXT("A new value mode was added without adding support for copy paste."));
			break;
		}
	}

	if (GetHasEditCondition() && ClipboardFunctionInput.bHasEditCondition)
	{
		SetEditConditionEnabled(ClipboardFunctionInput.bEditConditionValue);
	}
}

bool UNiagaraStackFunctionInput::IsScratchDynamicInput() const
{
	if (bIsScratchDynamicInputCache.IsSet() == false)
	{
		bIsScratchDynamicInputCache = 
			InputValues.Mode == EValueMode::Dynamic &&
			InputValues.DynamicNode.IsValid() &&
			GetSystemViewModel()->GetScriptScratchPadViewModel()->GetViewModelForScript(InputValues.DynamicNode->FunctionScript).IsValid();
	}
	return bIsScratchDynamicInputCache.GetValue();
}

void UNiagaraStackFunctionInput::GetSearchItems(TArray<FStackSearchItem>& SearchItems) const
{
	if (GetShouldPassFilterForVisibleCondition() && GetIsInlineEditConditionToggle() == false)
	{
		SearchItems.Add({ FName("DisplayName"), GetDisplayName() });

		if (InputValues.Mode == EValueMode::Local && InputType.IsValid() && InputValues.LocalStruct->IsValid())
		{
			FNiagaraVariable LocalValue = FNiagaraVariable(InputType, "");
			LocalValue.SetData(InputValues.LocalStruct->GetStructMemory());
			TSharedPtr<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> ParameterTypeUtilities = FNiagaraEditorModule::Get().GetTypeUtilities(RapidIterationParameter.GetType());
			if (ParameterTypeUtilities.IsValid() && ParameterTypeUtilities->CanHandlePinDefaults())
			{
				FText SearchText = ParameterTypeUtilities->GetSearchTextFromValue(LocalValue);
				if (SearchText.IsEmpty() == false)
				{
					SearchItems.Add({ FName("LocalValueText"), SearchText });
				}
			}
		}
		else if (InputValues.Mode == EValueMode::Linked)
		{
			SearchItems.Add({ FName("LinkedParamName"), FText::FromName(InputValues.LinkedHandle.GetParameterHandleString()) });
		}
		else if (InputValues.Mode == EValueMode::Dynamic && InputValues.DynamicNode.Get() != nullptr)
		{
			SearchItems.Add({ FName("LinkedDynamicInputName"), InputValues.DynamicNode->GetNodeTitle(ENodeTitleType::MenuTitle)});
		}
		else if (InputValues.Mode == EValueMode::Data && InputValues.DataObject.IsValid())
		{
			SearchItems.Add({ FName("LinkedDataInterfaceName"), FText::FromString(InputValues.DataObject->GetName()) });
		}
		else if (InputValues.Mode == EValueMode::Expression && InputValues.ExpressionNode.Get() != nullptr)
		{
			SearchItems.Add({ FName("LinkedExpressionText"), InputValues.ExpressionNode->GetHlslText() });
		}
	}
}

void UNiagaraStackFunctionInput::OnGraphChanged(const struct FEdGraphEditAction& InAction)
{
	if (bUpdatingGraphDirectly == false)
	{
		OverrideNodeCache.Reset();
		OverridePinCache.Reset();
	}
}

void UNiagaraStackFunctionInput::OnRapidIterationParametersChanged()
{
	bCanResetCache.Reset();
	bCanResetToBaseCache.Reset();
	if (ensureMsgf(OwningModuleNode.IsValid() && OwningFunctionCallNode.IsValid(), TEXT("Stack entry with invalid module or function call not cleaned up.")))
	{
		if (bUpdatingLocalValueDirectly == false && IsRapidIterationCandidate() && (OverridePinCache.IsSet() == false || OverridePinCache.GetValue() == nullptr))
		{
			RefreshValues();
		}
	}
}

void UNiagaraStackFunctionInput::OnScriptSourceChanged()
{
	bCanResetCache.Reset();
	bCanResetToBaseCache.Reset();
}

UNiagaraNodeParameterMapSet* UNiagaraStackFunctionInput::GetOverrideNode() const
{
	if (OverrideNodeCache.IsSet() == false)
	{
		UNiagaraNodeParameterMapSet* OverrideNode = nullptr;
		if (OwningFunctionCallNode.IsValid())
		{
			OverrideNode = FNiagaraStackGraphUtilities::GetStackFunctionOverrideNode(*OwningFunctionCallNode);
		}
		OverrideNodeCache = OverrideNode;
	}
	return OverrideNodeCache.GetValue();
}

UNiagaraNodeParameterMapSet& UNiagaraStackFunctionInput::GetOrCreateOverrideNode()
{
	UNiagaraNodeParameterMapSet* OverrideNode = GetOverrideNode();
	if (OverrideNode == nullptr)
	{
		TGuardValue<bool>(bUpdatingGraphDirectly, true);
		OverrideNode = &FNiagaraStackGraphUtilities::GetOrCreateStackFunctionOverrideNode(*OwningFunctionCallNode);
		OverrideNodeCache = OverrideNode;
	}
	return *OverrideNode;
}

UEdGraphPin* UNiagaraStackFunctionInput::GetOverridePin() const
{
	if (OverridePinCache.IsSet() == false)
	{
		OverridePinCache = FNiagaraStackGraphUtilities::GetStackFunctionInputOverridePin(*OwningFunctionCallNode, AliasedInputParameterHandle);
	}
	return OverridePinCache.GetValue();
}

UEdGraphPin& UNiagaraStackFunctionInput::GetOrCreateOverridePin()
{
	UEdGraphPin* OverridePin = GetOverridePin();
	if (OverridePin == nullptr)
	{
		TGuardValue<bool>(bUpdatingGraphDirectly, true);
		OverridePin = &FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*OwningFunctionCallNode, AliasedInputParameterHandle, InputType);
		OverridePinCache = OverridePin;
	}
	return *OverridePin;
}

void UNiagaraStackFunctionInput::GetDefaultDataInterfaceValueFromDefaultPin(UEdGraphPin* DefaultPin, UNiagaraStackFunctionInput::FInputValues& InInputValues) const
{
	// Default data interfaces are stored on the input node in the graph, but if it doesn't exist or it's data interface pointer is null, just use the CDO.
	InInputValues.Mode = EValueMode::Data;
	if (DefaultPin->LinkedTo.Num() == 1 && DefaultPin->LinkedTo[0]->GetOwningNode() != nullptr && DefaultPin->LinkedTo[0]->GetOwningNode()->IsA<UNiagaraNodeInput>())
	{
		UNiagaraNodeInput* DataInputNode = CastChecked<UNiagaraNodeInput>(DefaultPin->LinkedTo[0]->GetOwningNode());
		InInputValues.DataObject = DataInputNode->GetDataInterface();
	}
	if (InInputValues.DataObject.IsValid() == false)
	{
		InInputValues.DataObject = Cast<UNiagaraDataInterface>(InputType.GetClass()->GetDefaultObject());
	}
}

void UNiagaraStackFunctionInput::GetDefaultLocalValueFromDefaultPin(UEdGraphPin* DefaultPin, UNiagaraStackFunctionInput::FInputValues& InInputValues) const
{
	// Local default values are stored in the pin's default value string.
	InInputValues.Mode = EValueMode::Local;
	const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
	FNiagaraVariable LocalValueVariable = NiagaraSchema->PinToNiagaraVariable(DefaultPin);
	InInputValues.LocalStruct = MakeShared<FStructOnScope>(InputType.GetStruct());
	if (LocalValueVariable.IsDataAllocated() == false)
	{
		FNiagaraEditorUtilities::ResetVariableToDefaultValue(LocalValueVariable);
	}
	if (ensureMsgf(LocalValueVariable.IsDataAllocated(), TEXT("Neither PinToNiagaraVariable or ResetVariableToDefaultValue generated a value.  Allocating with 0s.")) == false)
	{
		LocalValueVariable.AllocateData();
	}
	FMemory::Memcpy(InInputValues.LocalStruct->GetStructMemory(), LocalValueVariable.GetData(), InputType.GetSize());
}

struct FLinkedHandleOrFunctionNode
{
	TOptional<FNiagaraParameterHandle> LinkedHandle;
	TWeakObjectPtr<UNiagaraNodeFunctionCall> LinkedFunctionCallNode;
};

void UNiagaraStackFunctionInput::GetDefaultLinkedHandleOrLinkedFunctionFromDefaultPin(UEdGraphPin* DefaultPin, UNiagaraStackFunctionInput::FInputValues& InInputValues) const
{
	// A default pin linked to a parameter map set is a default linked value.  Linked inputs can be setup in a chain and the first
	// available one will be used so that case must be handled too.  So first collect up the potential linked values and validate 
	// the linked node structure.
	TArray<FLinkedHandleOrFunctionNode> LinkedValues;
	UEdGraphPin* CurrentDefaultPin = DefaultPin;
	while (CurrentDefaultPin != nullptr && CurrentDefaultPin->LinkedTo.Num() == 1)
	{
		UEdGraphPin* LinkedPin = CurrentDefaultPin->LinkedTo[0];
	
		if (LinkedPin->GetOwningNode()->IsA<UNiagaraNodeParameterMapGet>())
		{
			UNiagaraNodeParameterMapGet* GetNode = CastChecked<UNiagaraNodeParameterMapGet>(LinkedPin->GetOwningNode());
			FLinkedHandleOrFunctionNode LinkedValue;
			LinkedValue.LinkedHandle = FNiagaraParameterHandle(*LinkedPin->GetName());
			LinkedValues.Add(LinkedValue);
			CurrentDefaultPin = GetNode->GetDefaultPin(LinkedPin);
		}
		else if (LinkedPin->GetOwningNode()->IsA<UNiagaraNodeFunctionCall>())
		{
			UNiagaraNodeFunctionCall* FunctionCallNode = CastChecked<UNiagaraNodeFunctionCall>(LinkedPin->GetOwningNode());
			FLinkedHandleOrFunctionNode LinkedValue;
			LinkedValue.LinkedFunctionCallNode = FunctionCallNode;
			LinkedValues.Add(LinkedValue);
			CurrentDefaultPin = nullptr;
		}
		else
		{
			// Only parameter map get nodes and function calls are valid for a default linked input chain so clear the linked
			// handles and stop searching.
			CurrentDefaultPin = nullptr;
			LinkedValues.Empty();
		}
	}

	FLinkedHandleOrFunctionNode* ValueToUse = nullptr;
	if (LinkedValues.Num() == 1)
	{
		ValueToUse = &LinkedValues[0];
	}
	else if(LinkedValues.Num() > 1)
	{
		// If there are a chain of linked values use the first one that's available, otherwise just use the last one.
		TArray<FNiagaraParameterHandle> AvailableHandles;
		GetAvailableParameterHandles(AvailableHandles);
		for (FLinkedHandleOrFunctionNode& LinkedValue : LinkedValues)
		{
			if (LinkedValue.LinkedFunctionCallNode.IsValid() ||
				AvailableHandles.Contains(LinkedValue.LinkedHandle.GetValue()))
			{
				ValueToUse = &LinkedValue;
				break;
			}
		}
	}

	if (ValueToUse != nullptr)
	{
		if (ValueToUse->LinkedHandle.IsSet())
		{
			InInputValues.Mode = EValueMode::Linked;
			InInputValues.LinkedHandle = ValueToUse->LinkedHandle.GetValue();
		}
		else
		{
			InInputValues.Mode = EValueMode::DefaultFunction;
			InInputValues.DefaultFunctionNode = ValueToUse->LinkedFunctionCallNode;
		}
	}
}

void UNiagaraStackFunctionInput::UpdateValuesFromScriptDefaults(FInputValues& InInputValues) const
{
	// Get the script variable first since it's used to determine static switch and bound input values.
	UNiagaraScriptVariable* InputScriptVariable = nullptr;
	if (OwningFunctionCallNode->FunctionScript != nullptr)
	{
		UNiagaraGraph* FunctionGraph = CastChecked<UNiagaraScriptSource>(OwningFunctionCallNode->FunctionScript->GetSource())->NodeGraph;
		InputScriptVariable = FunctionGraph->GetScriptVariable(InputParameterHandle.GetParameterHandleString());
	}

	if (IsStaticParameter())
	{
		// Static switch parameters are always locally set values.
		if (InputScriptVariable != nullptr)
		{
			TSharedPtr<FStructOnScope> StaticSwitchLocalStruct = FNiagaraEditorUtilities::StaticSwitchDefaultIntToStructOnScope(InputScriptVariable->Metadata.GetStaticSwitchDefaultValue(), InputType);
			if(ensureMsgf(StaticSwitchLocalStruct.IsValid(), TEXT("Unsupported static struct default value.")))
			{ 
				InInputValues.Mode = EValueMode::Local;
				InInputValues.LocalStruct = StaticSwitchLocalStruct;
			}
		}
	}
	else
	{
		if (InputScriptVariable != nullptr && InputScriptVariable->DefaultMode == ENiagaraDefaultMode::Binding && InputScriptVariable->DefaultBinding.IsValid())
		{
			// The next highest precedence value is a linked value from a variable binding so check that.
			InInputValues.Mode = EValueMode::Linked;
			InInputValues.LinkedHandle = InputScriptVariable->DefaultBinding.GetName();
		}
		else
		{
			// Otherwise we need to check the pin that defined the variable in the graph to determine the default.
			UEdGraphPin* DefaultPin = OwningFunctionCallNode->FindParameterMapDefaultValuePin(InputParameterHandle.GetParameterHandleString(), SourceScript->GetUsage());
			if (DefaultPin != nullptr)
			{
				if (InputType.IsDataInterface())
				{
					// Data interfaces are handled differently than other values types so collect them here.
					GetDefaultDataInterfaceValueFromDefaultPin(DefaultPin, InInputValues);
				}
				else
				{
					// Otherwise check for local and linked values.
					if (DefaultPin->LinkedTo.Num() == 0)
					{
						// If the default pin isn't wired to anything then it's a local value.
						GetDefaultLocalValueFromDefaultPin(DefaultPin, InInputValues);
					}
					else if(DefaultPin->LinkedTo.Num() == 1 && DefaultPin->LinkedTo[0]->GetOwningNode() != nullptr)
					{
						// If a default pin is linked to a parameter map it can be a linked value.
						GetDefaultLinkedHandleOrLinkedFunctionFromDefaultPin(DefaultPin, InInputValues);
					}

					if(InInputValues.Mode == EValueMode::None)
					{
						// If an input mode wasn't found than the graph is configured in a way that can't be displayed in the stack.
						InInputValues.Mode = EValueMode::UnsupportedDefault;
					}
				}
			}
		}
	}
}

void UNiagaraStackFunctionInput::UpdateValuesFromOverridePin(const FInputValues& OldInputValues, FInputValues& NewInputValues, UEdGraphPin& InOverridePin) const
{
	NewInputValues.Mode = EValueMode::InvalidOverride;
	if (InOverridePin.LinkedTo.Num() == 0)
	{
		// If an override pin exists but it's not connected, the only valid state is a local struct value stored in the pins default value string.
		if (InputType.IsUObject() == false)
		{
			// If there was an old local struct, reuse it if it's of the correct type.
			TSharedPtr<FStructOnScope> LocalStruct;
			if (OldInputValues.Mode == EValueMode::Local && OldInputValues.LocalStruct.IsValid() && OldInputValues.LocalStruct->GetStruct() == InputType.GetStruct())
			{
				LocalStruct = OldInputValues.LocalStruct;
			}
			else
			{
				LocalStruct = MakeShared<FStructOnScope>(InputType.GetStruct());
			}
			const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
			FNiagaraVariable ValueVariable = NiagaraSchema->PinToNiagaraVariable(&InOverridePin, false);
			if (ValueVariable.IsDataAllocated())
			{
				ValueVariable.CopyTo(LocalStruct->GetStructMemory());
				NewInputValues.Mode = EValueMode::Local;
				NewInputValues.LocalStruct = LocalStruct;
			}
		}
	}
	else if (InOverridePin.LinkedTo.Num() == 1 && InOverridePin.LinkedTo[0] != nullptr && InOverridePin.LinkedTo[0]->GetOwningNode() != nullptr)
	{
		UEdGraphNode* LinkedNode = InOverridePin.LinkedTo[0]->GetOwningNode();
		if (LinkedNode->IsA<UNiagaraNodeInput>())
		{
			// Input nodes handle data interface values.
			UNiagaraNodeInput* InputNode = CastChecked<UNiagaraNodeInput>(LinkedNode);
			if (InputNode->GetDataInterface() != nullptr)
			{
				NewInputValues.Mode = EValueMode::Data;
				NewInputValues.DataObject = InputNode->GetDataInterface();
			}
		}
		else if (LinkedNode->IsA<UNiagaraNodeParameterMapGet>())
		{
			// Parameter map get nodes handle linked values.
			NewInputValues.Mode = EValueMode::Linked;
			NewInputValues.LinkedHandle = FNiagaraParameterHandle(InOverridePin.LinkedTo[0]->PinName);
		}
		else if (LinkedNode->IsA<UNiagaraNodeCustomHlsl>())
		{
			// Custom hlsl nodes handle expression values.
			UNiagaraNodeCustomHlsl* ExpressionNode = CastChecked<UNiagaraNodeCustomHlsl>(LinkedNode);
			NewInputValues.Mode = EValueMode::Expression;
			NewInputValues.ExpressionNode = ExpressionNode;
		}
		else if (LinkedNode->IsA<UNiagaraNodeFunctionCall>())
		{
			// Function call nodes handle dynamic inputs.
			UNiagaraNodeFunctionCall* DynamicNode = CastChecked<UNiagaraNodeFunctionCall>(LinkedNode);
			NewInputValues.Mode = EValueMode::Dynamic;
			NewInputValues.DynamicNode = DynamicNode;
		}
	}
}

void UNiagaraStackFunctionInput::RemoveNodesForOverridePin(UEdGraphPin& OverridePin)
{
	TArray<TWeakObjectPtr<UNiagaraDataInterface>> RemovedDataObjects;
	FNiagaraStackGraphUtilities::RemoveNodesForStackFunctionInputOverridePin(OverridePin, RemovedDataObjects);
	for (TWeakObjectPtr<UNiagaraDataInterface> RemovedDataObject : RemovedDataObjects)
	{
		if (RemovedDataObject.IsValid())
		{
			OnDataObjectModified().Broadcast(RemovedDataObject.Get());
		}
	}
}

void UNiagaraStackFunctionInput::RemoveOverridePin()
{
	UEdGraphPin* OverridePin = GetOverridePin();
	if (OverridePin != nullptr)
	{
		RemoveNodesForOverridePin(*OverridePin);
		UNiagaraNodeParameterMapSet* OverrideNode = CastChecked<UNiagaraNodeParameterMapSet>(OverridePin->GetOwningNode());
		OverrideNode->Modify();
		OverrideNode->RemovePin(OverridePin);
	}
}

#undef LOCTEXT_NAMESPACE
