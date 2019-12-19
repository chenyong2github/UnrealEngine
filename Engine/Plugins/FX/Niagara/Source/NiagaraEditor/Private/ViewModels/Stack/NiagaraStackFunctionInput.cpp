// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackFunctionInputCollection.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
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
	, bIsLocalOverride(false)
{
}

void UNiagaraStackFunctionInput::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UNiagaraStackFunctionInput* This = Cast<UNiagaraStackFunctionInput>(InThis);
	if (This != nullptr)
	{
		This->AddReferencedObjects(Collector);
	}
	Super::AddReferencedObjects(InThis, Collector);
}

void UNiagaraStackFunctionInput::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (InputValues.DataObjects.IsValid() && InputValues.DataObjects.GetDefaultValueOwner() == FDataValues::EDefaultValueOwner::LocallyOwned)
	{
		Collector.AddReferencedObject(InputValues.DataObjects.GetDefaultValueObjectRef(), this);
	}
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
	OwningAssignmentNode = Cast<UNiagaraNodeAssignment>(OwningFunctionCallNode.Get());

	UNiagaraNodeOutput* OutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*OwningModuleNode.Get());
	UNiagaraEmitter* ParentEmitter = GetEmitterViewModel().IsValid() ? GetEmitterViewModel()->GetEmitter() : nullptr;
	UNiagaraSystem* ParentSystem = &GetSystemViewModel()->GetSystem();
	if (OutputNode)
	{
		TArray<UNiagaraScript*> Scripts;
		if (ParentEmitter != nullptr)
		{
			ParentEmitter->GetScripts(Scripts, false);
		}
		if (ParentSystem != nullptr)
		{
			Scripts.Add(ParentSystem->GetSystemSpawnScript());
			Scripts.Add(ParentSystem->GetSystemUpdateScript());
		}

		for (UNiagaraScript* Script : Scripts)
		{
			if (OutputNode->GetUsage() == ENiagaraScriptUsage::ParticleEventScript)
			{
				if (Script->GetUsage() == ENiagaraScriptUsage::ParticleEventScript && Script->GetUsageId() == OutputNode->GetUsageId())
				{
					AffectedScripts.Add(Script);
					break;
				}
			}
			else if (Script->ContainsUsage(OutputNode->GetUsage()))
			{
				AffectedScripts.Add(Script);
			}
		}

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
	AliasedInputParameterHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputParameterHandle, OwningFunctionCallNode.Get());

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

	Super::FinalizeInternal();
}

const UNiagaraNodeFunctionCall& UNiagaraStackFunctionInput::GetInputFunctionCallNode() const
{
	return *OwningFunctionCallNode.Get();
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
	return GetTooltipText(InputValues.Mode);
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
	if (InputValues.Mode == EValueMode::Invalid)
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
	if (InputValues.Mode == EValueMode::Invalid)
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
		if (ClipboardContent->FunctionInputs[0] != nullptr)
		{
			if (ClipboardContent->FunctionInputs[0]->InputType == InputType)
			{
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

void UNiagaraStackFunctionInput::Paste(const UNiagaraClipboardContent* ClipboardContent)
{
	checkf(ClipboardContent != nullptr && ClipboardContent->FunctionInputs.Num() == 1, TEXT("Clipboard must not be null, and must contain a single input.  Call TestCanPasteWithMessage to validate"));

	const UNiagaraClipboardFunctionInput* ClipboardInput = ClipboardContent->FunctionInputs[0];
	if (ClipboardInput != nullptr && ClipboardInput->InputType == InputType)
	{
		SetValueFromClipboardFunctionInput(*ClipboardInput);
	}
}

FText UNiagaraStackFunctionInput::GetTooltipText(EValueMode InValueMode) const
{
	FNiagaraVariable ValueVariable;
	UNiagaraGraph* NodeGraph = nullptr;

	if (InValueMode == EValueMode::Linked)
	{
		UEdGraphPin* OverridePin = GetOverridePin();
		UEdGraphPin* ValuePin = OverridePin != nullptr ? OverridePin : GetDefaultPin();
		ValueVariable = FNiagaraVariable(InputType, InputValues.LinkedHandle.GetParameterHandleString());
		if (ValuePin != nullptr)
		{
			NodeGraph = Cast<UNiagaraGraph>(ValuePin->GetOwningNode()->GetGraph());
		}
	}
	else
	{
		ValueVariable = FNiagaraVariable(InputType, InputParameterHandle.GetParameterHandleString());
		if (OwningFunctionCallNode.IsValid() && OwningFunctionCallNode->FunctionScript != nullptr)
		{
			UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(OwningFunctionCallNode->FunctionScript->GetSource());
			const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
			NodeGraph = Source->NodeGraph;
		}
	}

	TOptional<FNiagaraVariableMetaData> MetaData;
	if (FNiagaraConstants::IsNiagaraConstant(ValueVariable))
	{
		const FNiagaraVariableMetaData* FoundMetaData = FNiagaraConstants::GetConstantMetaData(ValueVariable);
		if (FoundMetaData)
		{
			MetaData = *FoundMetaData;
		}
	}
	else if (NodeGraph != nullptr)
	{
		MetaData = NodeGraph->GetMetaData(ValueVariable);
	}

	FText Description = FText::GetEmpty();
	if (MetaData.IsSet())
	{
		Description = MetaData->Description;
	}

	return FText::Format(LOCTEXT("FunctionInputTooltip", "Name: {0} \nType: {1} \nDesc: {2}"),
		FText::FromName(ValueVariable.GetName()),
		ValueVariable.GetType().GetNameText(),
		Description);
}

void UNiagaraStackFunctionInput::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	RapidIterationParameter = CreateRapidIterationVariable(AliasedInputParameterHandle.GetParameterHandleString());

	RefreshFromMetaData();
	RefreshValues();

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
					FText LongMessage = InputValues.DynamicNode->FunctionScript->DeprecationRecommendation != nullptr ?
						FText::Format(LOCTEXT("ModuleScriptDeprecationLong", "The script asset for the assigned module {0} has been deprecated. Suggested replacement: {1}"), FText::FromString(InputValues.DynamicNode->GetName()), FText::FromString(InputValues.DynamicNode->FunctionScript->DeprecationRecommendation->GetPathName())) :
						FText::Format(LOCTEXT("ModuleScriptDeprecationUnknownLong", "The script asset for the assigned module {0} has been deprecated."), FText::FromString(InputValues.DynamicNode->GetName()));

					int32 AddIdx = NewIssues.Add(FStackIssue(
						EStackIssueSeverity::Warning,
						LOCTEXT("ModuleScriptDeprecationShort", "Deprecated module"),
						LongMessage,
						GetStackEditorDataKey(),
						false,
						{
							FStackIssueFix(
								LOCTEXT("SelectNewDynamicInputScriptFix", "Select a new dynamic input script"),
								FStackIssueFixDelegate::CreateLambda([this]() { this->bIsDynamicInputScriptReassignmentPending = true; })),
							FStackIssueFix(
								LOCTEXT("ResetFix", "Reset this input to it's default value"),
								FStackIssueFixDelegate::CreateLambda([this]() { this->Reset(); }))
						}));

					if (InputValues.DynamicNode->FunctionScript->DeprecationRecommendation != nullptr)
					{
						NewIssues[AddIdx].InsertFix(0,
							FStackIssueFix(
							LOCTEXT("SelectNewModuleScriptFixUseRecommended", "Use recommended replacement"),
							FStackIssueFixDelegate::CreateLambda([this]() { ReassignDynamicInputScript(InputValues.DynamicNode->FunctionScript->DeprecationRecommendation); })));
					}
				}

				if (InputValues.DynamicNode->FunctionScript->bExperimental)
				{
					NewIssues.Add(FStackIssue(
						EStackIssueSeverity::Info,
						LOCTEXT("ModuleScriptExperimentalShort", "Experimental module"),
						FText::Format(LOCTEXT("ModuleScriptExperimental", "The script asset for the assigned module {0} is experimental, use with care!"), FText::FromString(InputValues.DynamicNode->GetName())),
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
						LOCTEXT("ResetFix", "Reset this input to it's default value"),
						FStackIssueFixDelegate::CreateLambda([this]() { this->Reset(); }))
				}));
		}
	}

	if (InputValues.Mode == EValueMode::Data && InputValues.DataObjects.GetValueObject() != nullptr)
	{
		UNiagaraStackObject* ValueObjectEntry = FindCurrentChildOfTypeByPredicate<UNiagaraStackObject>(CurrentChildren,
			[=](UNiagaraStackObject* CurrentObjectEntry) { return CurrentObjectEntry->GetObject() == InputValues.DataObjects.GetValueObject(); });

		if(ValueObjectEntry == nullptr)
		{
			ValueObjectEntry = NewObject<UNiagaraStackObject>(this);
			ValueObjectEntry->Initialize(CreateDefaultChildRequiredData(), InputValues.DataObjects.GetValueObject(), GetOwnerStackItemEditorDataKey(), OwningFunctionCallNode.Get());
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


TSharedPtr<FStructOnScope> UNiagaraStackFunctionInput::FInputValues::GetLocalStructToReuse()
{
	return Mode == EValueMode::Local ? LocalStruct : TSharedPtr<FStructOnScope>();
}

UNiagaraDataInterface* UNiagaraStackFunctionInput::FInputValues::GetDataDefaultValueObjectToReuse()
{
	return Mode == EValueMode::Data && DataObjects.IsValid() && DataObjects.GetDefaultValueOwner() == FDataValues::EDefaultValueOwner::LocallyOwned
		? DataObjects.GetDefaultValueObject()
		: nullptr;
}

bool UNiagaraStackFunctionInput::TryGetDefaultBinding(FNiagaraParameterHandle& LinkedValueHandle, UNiagaraScriptVariable* InVariable, UEdGraphPin& ValuePin)
{
	if (!InVariable)
	{
		return false;
	}

	if (InVariable->DefaultMode != ENiagaraDefaultMode::Binding || !InVariable->DefaultBinding.IsValid())
	{
		return false;
	}

	LinkedValueHandle = FNiagaraParameterHandle(InVariable->DefaultBinding.GetName());
	return true;
}

void UNiagaraStackFunctionInput::RefreshValues(bool bFromSetLocalValue)
{
	if (ensureMsgf(IsStaticParameter() || InputParameterHandle.IsModuleHandle(), TEXT("Function inputs can only be generated for module paramters.")) == false)
	{
		return;
	}

	FInputValues OldValues = InputValues;
	InputValues = FInputValues();

	UEdGraphPin* DefaultPin = GetDefaultPin();
	if (DefaultPin != nullptr)
	{
		UEdGraphPin* OverridePin = GetOverridePin();
		UEdGraphPin* ValuePin = OverridePin != nullptr ? OverridePin : DefaultPin;

		if (UNiagaraGraph* FunctionGraph = Cast<UNiagaraScriptSource>(OwningFunctionCallNode->FunctionScript->GetSource())->NodeGraph)
		{
			Variable = FunctionGraph->GetScriptVariable(*(TEXT("Module.") + DisplayName.ToString()));
		}

		if (TryGetCurrentDataValue(InputValues.DataObjects, OverridePin, *DefaultPin, OldValues.GetDataDefaultValueObjectToReuse()))
		{
			InputValues.Mode = EValueMode::Data;
			bIsLocalOverride = false;
		}
		else if (TryGetCurrentExpressionValue(InputValues.ExpressionNode, OverridePin))
		{
			InputValues.Mode = EValueMode::Expression;
			bIsLocalOverride = false;
		}
		else if (TryGetCurrentDynamicValue(InputValues.DynamicNode, OverridePin))
		{
			InputValues.Mode = EValueMode::Dynamic;
			bIsLocalOverride = false;
		}
		else if (TryGetCurrentLinkedValue(InputValues.LinkedHandle, *ValuePin))
		{
			InputValues.Mode = EValueMode::Linked;
			bIsLocalOverride = false;
		}
		else if (TryGetCurrentLocalValue(InputValues.LocalStruct, *DefaultPin, *ValuePin, OldValues.GetLocalStructToReuse(), Variable))
		{
			if (OverridePin)
			{
				InputValues.Mode = EValueMode::Local;
			}
			else
			{
				if (!bIsLocalOverride && TryGetDefaultBinding(InputValues.LinkedHandle, Variable, *ValuePin))
				{
					InputValues.Mode = EValueMode::Linked;
					bIsLocalOverride = false; 
				}
				else
				{
					InputValues.Mode = EValueMode::Local;
				}
			}
		}
		else if (TryGetDefaultBinding(InputValues.LinkedHandle, Variable, *ValuePin))
		{
			InputValues.Mode = EValueMode::Linked;
			bIsLocalOverride = false;
		}
	}

	bCanResetCache.Reset();
	bCanResetToBaseCache.Reset();
	ValueChangedDelegate.Broadcast();
}

void UNiagaraStackFunctionInput::RefreshFromMetaData()
{
	if (OwningFunctionCallNode->FunctionScript != nullptr)
	{
		UNiagaraGraph* FunctionGraph = CastChecked<UNiagaraScriptSource>(OwningFunctionCallNode->FunctionScript->GetSource())->NodeGraph;
		FNiagaraVariable InputVariable(InputType, InputParameterHandle.GetParameterHandleString());
		InputMetaData = FunctionGraph->GetMetaData(InputVariable);

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
	UEdGraphPin& OverridePin = GetOrCreateOverridePin();
	RemoveNodesForOverridePin(OverridePin);
	if (IsRapidIterationCandidate())
	{
		RemoveRapidIterationParametersForAffectedScripts();
	}

	FNiagaraStackGraphUtilities::SetLinkedValueHandleForFunctionInput(OverridePin, InParameterHandle);
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
		ENiagaraScriptUsage::ParticleEventScript	// When using interpolated spawn and is spawn
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
		return FNiagaraParameterHandle::ParticleAttributeNamespace;
	case ENiagaraScriptUsage::EmitterSpawnScript:
	case ENiagaraScriptUsage::EmitterUpdateScript:
		return FNiagaraParameterHandle::EmitterNamespace;
	case ENiagaraScriptUsage::SystemSpawnScript:
	case ENiagaraScriptUsage::SystemUpdateScript:
		return FNiagaraParameterHandle::SystemNamespace;
	default:
		return NAME_None;
	}
}

void UNiagaraStackFunctionInput::GetAvailableParameterHandles(TArray<FNiagaraParameterHandle>& AvailableParameterHandles)
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

	for (const FAssetData& DynamicInputAsset : DynamicInputAssets)
	{
		UNiagaraScript* DynamicInputScript = Cast<UNiagaraScript>(DynamicInputAsset.GetAsset());
		if (DynamicInputScript != nullptr)
		{
			UNiagaraScriptSource* DynamicInputScriptSource = Cast<UNiagaraScriptSource>(DynamicInputScript->GetSource());
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
						AvailableDynamicInputs.Add(DynamicInputScript);
					}
				}
			}
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

TSharedPtr<FStructOnScope> UNiagaraStackFunctionInput::GetLocalValueStruct()
{
	return InputValues.LocalStruct;
}

UNiagaraDataInterface* UNiagaraStackFunctionInput::GetDataValueObject()
{
	return InputValues.DataObjects.GetValueObject();
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
	return !IsStaticParameter() && FNiagaraStackGraphUtilities::IsRapidIterationType(InputType);
}

void UNiagaraStackFunctionInput::SetLocalValue(TSharedRef<FStructOnScope> InLocalValue, bool bIsOverride)
{
	if (bIsOverride)
	{
		bIsLocalOverride = bIsOverride;
	}
	TGuardValue<bool> UpdateGuard(bUpdatingLocalValueDirectly, true);

	const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
	UEdGraphPin* DefaultPin = GetDefaultPin();
	UEdGraphPin* OverridePin = GetOverridePin();
	UEdGraphPin* ValuePin = DefaultPin;

	// If the parameter we set is from a static switch then we don't want to use an override pin, but directly change the default pin.
	if (IsStaticParameter())
	{
		FNiagaraVariable LocalValueVariable(InputType, NAME_None);
		LocalValueVariable.SetData(InLocalValue->GetStructMemory());
		FString PinDefaultValue;
		if (ensureMsgf(NiagaraSchema->TryGetPinDefaultValueFromNiagaraVariable(LocalValueVariable, PinDefaultValue),
			TEXT("Could not generate value string for static switch parameter.")))
		{
			DefaultPin->Modify();
			DefaultPin->DefaultValue = PinDefaultValue;
			Cast<UNiagaraNode>(DefaultPin->GetOwningNode())->MarkNodeRequiresSynchronization(TEXT("Default Value Changed"), true);
		}
		RefreshValues(true);
		return;
	}
	
	// If the default pin in the function graph is connected internally, rapid iteration parameters can't be used since
	// the compilation currently won't use them.
	bool bCanUseRapidIterationParameter = IsRapidIterationCandidate() && DefaultPin->LinkedTo.Num() == 0;
	if (Variable && Variable->DefaultMode == ENiagaraDefaultMode::Binding)
	{
		bCanUseRapidIterationParameter = false;
	}

	if (bCanUseRapidIterationParameter == false)
	{
		ValuePin = OverridePin != nullptr ? OverridePin : DefaultPin;
	}

	TSharedPtr<FStructOnScope> CurrentValue;
	bool bCanHaveLocalValue = ValuePin != nullptr;
	bool bHasLocalValue = bCanHaveLocalValue && InputValues.Mode == EValueMode::Local && TryGetCurrentLocalValue(CurrentValue, *DefaultPin, *ValuePin, TSharedPtr<FStructOnScope>(), nullptr);
	bool bLocalValueMatchesSetValue = bHasLocalValue && FNiagaraEditorUtilities::DataMatches(*CurrentValue.Get(), InLocalValue.Get());

	if (bCanHaveLocalValue == false || bLocalValueMatchesSetValue)
	{
		return;
	}

	FScopedTransaction ScopedTransaction(LOCTEXT("UpdateInputLocalValue", "Update input local value"));
	UNiagaraGraph* EmitterGraph = Cast<UNiagaraGraph>(OwningFunctionCallNode->GetGraph());

	bool bGraphWillNeedRelayout = false;
	if (OverridePin != nullptr && OverridePin->LinkedTo.Num() > 0)
	{
		RemoveNodesForOverridePin(*OverridePin);
		bGraphWillNeedRelayout = true;
	}

	if (bCanUseRapidIterationParameter)
	{
		for (TWeakObjectPtr<UNiagaraScript> Script : AffectedScripts)
		{
			Script->Modify();
		}

		// If there is currently an override, we need to get rid of it.
		if (OverridePin != nullptr)
		{
			UNiagaraNode* OverrideNode = CastChecked<UNiagaraNode>(OverridePin->GetOwningNode());
			OverrideNode->Modify();
			OverrideNode->RemovePin(OverridePin);
		}

		for (TWeakObjectPtr<UNiagaraScript> Script : AffectedScripts)
		{
			bool bAddParameterIfMissing = true;
			Script->RapidIterationParameters.SetParameterData(InLocalValue->GetStructMemory(), RapidIterationParameter, bAddParameterIfMissing);
		}
	}
	else 
	{
		FNiagaraVariable LocalValueVariable(InputType, NAME_None);
		LocalValueVariable.SetData(InLocalValue->GetStructMemory());
		FString PinDefaultValue;
		if(ensureMsgf(NiagaraSchema->TryGetPinDefaultValueFromNiagaraVariable(LocalValueVariable, PinDefaultValue),
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
		FNiagaraStackGraphUtilities::RelayoutGraph(*EmitterGraph);
	}

	RefreshValues(true);
}

bool UNiagaraStackFunctionInput::CanReset() const
{
	if (IsStaticParameter())
	{
		// Static switch parameters only hold a single value in the default pin, so we disable resetting to prevent a special implementation for them
		return false;
	}
	if (bCanResetCache.IsSet() == false)
	{
		bool bNewCanReset;
		if (InputValues.Mode == EValueMode::Data)
		{
			// For data values a copy of the default object should have been created automatically and attached to the override pin for this input.  If a 
			// copy of the default object wasn't created, the input can be reset to create one.  If a copy of the data object is available it can be
			// reset if it's different from it's default value.
			bool bHasDataValueObject = InputValues.DataObjects.GetValueObject() != nullptr;
			bool bHasDefaultDataValueObject = InputValues.DataObjects.GetDefaultValueObject() != nullptr;
			bool bIsDataValueDifferentFromDefaultDataValue = bHasDataValueObject && bHasDefaultDataValueObject
				&& InputValues.DataObjects.GetValueObject()->Equals(InputValues.DataObjects.GetDefaultValueObject()) == false;
			bNewCanReset = bHasDataValueObject == false || bHasDefaultDataValueObject == false || bIsDataValueDifferentFromDefaultDataValue;
		}
		else
		{
			UEdGraphPin* DefaultPin = GetDefaultPin();
			if (ensure(DefaultPin != nullptr))
			{			
				if(DefaultPin->LinkedTo.Num() == 0)
				{
					if (UEdGraphPin* OverridePin = GetOverridePin())
					{
						bNewCanReset = true;
						UNiagaraGraph* FunctionGraph = CastChecked<UNiagaraScriptSource>(OwningFunctionCallNode->FunctionScript->GetSource())->NodeGraph;
						if (FunctionGraph && OverridePin && OverridePin->LinkedTo.Num() == 1)
						{
							UNiagaraScriptVariable* ScriptVariable = FunctionGraph->GetScriptVariable(*(TEXT("Module.") + DisplayName.ToString()));
							if (ScriptVariable && OverridePin->LinkedTo[0]->PinName == ScriptVariable->DefaultBinding.GetName())
							{
								bNewCanReset = false;
							}
						}
					}
					else if (IsRapidIterationCandidate())
					{
						FNiagaraVariable DefaultVar = GetDefaultVariableForRapidIterationParameter();
						bool bHasValidLocalValue = InputValues.LocalStruct.IsValid();
						bool bHasValidDefaultValue = DefaultVar.IsValid();
						bNewCanReset = bHasValidLocalValue && bHasValidDefaultValue && FNiagaraEditorUtilities::DataMatches(DefaultVar, *InputValues.LocalStruct.Get()) == false;
					}
					else
					{
						bNewCanReset = false;
					}
				}
				else
				{
					if (FNiagaraStackGraphUtilities::IsValidDefaultDynamicInput(*SourceScript, *DefaultPin))
					{
						UEdGraphPin* OverridePin = GetOverridePin();
						FString UniqueEmitterName = GetEmitterViewModel().IsValid() ? GetEmitterViewModel()->GetEmitter()->GetUniqueEmitterName() : FString();
						bNewCanReset = OverridePin == nullptr || FNiagaraStackGraphUtilities::DoesDynamicInputMatchDefault(UniqueEmitterName, *SourceScript,
							*OwningFunctionCallNode, *OverridePin, InputParameterHandle.GetName(), *DefaultPin) == false;
					}
					else
					{
						bNewCanReset = GetOverridePin() != nullptr;
					}
				}
			}
		}
		bCanResetCache = bNewCanReset;
	}
	return bCanResetCache.GetValue();
}

FNiagaraVariable UNiagaraStackFunctionInput::GetDefaultVariableForRapidIterationParameter() const
{
	FNiagaraVariable Var;
	UEdGraphPin* DefaultPin = GetDefaultPin();
	if (DefaultPin != nullptr)
	{
		const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
		Var = NiagaraSchema->PinToNiagaraVariable(DefaultPin, true);
		Var.SetName(*RapidIterationParameter.GetName().ToString());
	}
	return Var;
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
	bIsLocalOverride = false;
	if(InputValues.Mode == EValueMode::Data)
	{
		// For data values they are reset by making sure the data object owned by this input matches the default
		// data object.  If there is no data object owned by the input, one is created and and updated to match the default.
		FScopedTransaction ScopedTransaction(LOCTEXT("ResetInputObjectTransaction", "Reset the inputs data interface object to default."));
		if (InputValues.DataObjects.GetValueObject() != nullptr && InputValues.DataObjects.GetDefaultValueObject() != nullptr)
		{
			InputValues.DataObjects.GetDefaultValueObject()->CopyTo(InputValues.DataObjects.GetValueObject());
		}
		else
		{
			UEdGraphPin& OverridePin = GetOrCreateOverridePin();
			RemoveNodesForOverridePin(OverridePin);

			FString InputNodeName = InputParameterHandlePath[0].GetName().ToString();
			for (int32 i = 1; i < InputParameterHandlePath.Num(); i++)
			{
				InputNodeName += "." + InputParameterHandlePath[i].GetName().ToString();
			}

			UNiagaraDataInterface* InputValueObject;
			FNiagaraStackGraphUtilities::SetDataValueObjectForFunctionInput(OverridePin, const_cast<UClass*>(InputType.GetClass()), InputNodeName, InputValueObject);
			if (InputValues.DataObjects.GetDefaultValueObject() != nullptr)
			{
				InputValues.DataObjects.GetDefaultValueObject()->CopyTo(InputValueObject);
			}

			FNiagaraStackGraphUtilities::RelayoutGraph(*OwningFunctionCallNode->GetGraph());
		}
	}
	else
	{
		// For all other value modes removing the nodes connected to the override pin resets them.
		UNiagaraNodeParameterMapSet* OverrideNode = GetOverrideNode();
		UEdGraphPin* OverridePin = GetOverridePin();
		UEdGraphPin* DefaultPin = GetDefaultPin();
		bool bGraphNeedsRecompile = false;
		{
			FScopedTransaction ScopedTransaction(LOCTEXT("ResetInputStructTransaction", "Reset the inputs value to default."));
			
			if(DefaultPin->LinkedTo.Num() == 0 && IsRapidIterationCandidate())
			{
				if (OverrideNode != nullptr && OverridePin != nullptr)
				{
					RemoveNodesForOverridePin(*OverridePin);
					OverrideNode->Modify();
					OverrideNode->RemovePin(OverridePin);
					bGraphNeedsRecompile = true;
				}

				// Get the default value of the graph pin and use that to reset the rapid iteration variables...
				FNiagaraVariable DefaultVar = GetDefaultVariableForRapidIterationParameter();
				if (DefaultVar.IsValid())
				{
					UpdateRapidIterationParametersForAffectedScripts(DefaultVar.GetData());
				}
			}
			else if(DefaultPin->LinkedTo.Num() == 0 || FNiagaraStackGraphUtilities::IsValidDefaultDynamicInput(*SourceScript, *DefaultPin) == false)
			{
				if (ensureMsgf(OverrideNode != nullptr && OverridePin != nullptr, TEXT("Can not reset the value of an input that doesn't have a valid override node and override pin")))
				{
					RemoveNodesForOverridePin(*OverridePin);
					OverrideNode->Modify();
					OverrideNode->RemovePin(OverridePin);
					bGraphNeedsRecompile =  true;
				}
			}
			else
			{
				if (OverridePin != nullptr)
				{
					RemoveNodesForOverridePin(*OverridePin);
				}
				FNiagaraStackGraphUtilities::ResetToDefaultDynamicInput(GetSystemViewModel(), GetEmitterViewModel(), GetStackEditorData(),
					*SourceScript, AffectedScripts, *OwningModuleNode, *OwningFunctionCallNode, InputParameterHandle.GetName(), *DefaultPin);
				bGraphNeedsRecompile = true;
			}

			if (bGraphNeedsRecompile)
			{
				OwningFunctionCallNode->GetNiagaraGraph()->NotifyGraphNeedsRecompile();
				FNiagaraStackGraphUtilities::RelayoutGraph(*OwningFunctionCallNode->GetGraph());
			}
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

bool UNiagaraStackFunctionInput::CanRenameInput() const
{
	// Only module level assignment node inputs can be renamed.
	return OwningAssignmentNode.IsValid() && InputParameterHandlePath.Num() == 1 &&
		OwningAssignmentNode->FindAssignmentTarget(InputParameterHandle.GetName()) != INDEX_NONE;
}

bool UNiagaraStackFunctionInput::GetIsRenamePending() const
{
	return CanRenameInput() && GetStackEditorData().GetModuleInputIsRenamePending(StackEditorDataKey);
}

void UNiagaraStackFunctionInput::SetIsRenamePending(bool bIsRenamePending)
{
	if (CanRenameInput())
	{
		GetStackEditorData().SetModuleInputIsRenamePending(StackEditorDataKey, bIsRenamePending);
	}
}

void UNiagaraStackFunctionInput::RenameInput(FName NewName)
{
	if (OwningAssignmentNode.IsValid() && InputParameterHandlePath.Num() == 1 && InputParameterHandle.GetName() != NewName)
	{
		FScopedTransaction ScopedTransaction(LOCTEXT("RenameInput", "Rename this function's input."));

		FInputValues OldInputValues = InputValues;
		UEdGraphPin* OriginalOverridePin = GetOverridePin();

		// We'll be making changes, so go ahead and keep track of the override pointer if it exists.
		if (OriginalOverridePin != nullptr)
		{
			OriginalOverridePin->GetOwningNode()->Modify();
		}

		bool bIsCurrentlyExpanded = GetStackEditorData().GetStackEntryIsExpanded(FNiagaraStackGraphUtilities::GenerateStackModuleEditorDataKey(*OwningAssignmentNode), false);


		int32 FoundIdx = OwningAssignmentNode->FindAssignmentTarget(InputParameterHandle.GetName());
		check(FoundIdx != INDEX_NONE);
		FNiagaraParameterHandle TargetHandle(OwningAssignmentNode->GetAssignmentTargetName(FoundIdx));

		OwningAssignmentNode->Modify();
		if (OwningAssignmentNode->FunctionScript != nullptr)
		{
			OwningAssignmentNode->FunctionScript->Modify();
			OwningAssignmentNode->FunctionScript->GetSource()->Modify();
		}
		if (OwningAssignmentNode->SetAssignmentTargetName(FoundIdx, NewName))
		{
			OwningAssignmentNode->RefreshFromExternalChanges();
		}

		InputParameterHandle = FNiagaraParameterHandle(InputParameterHandle.GetNamespace(), NewName);
		InputParameterHandlePath.Empty(1);
		InputParameterHandlePath.Add(InputParameterHandle);
		AliasedInputParameterHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputParameterHandle, OwningAssignmentNode.Get());
		DisplayName = FText::FromName(InputParameterHandle.GetName());

		if (IsRapidIterationCandidate())
		{
			FNiagaraVariable OldRapidIterationParameter = RapidIterationParameter;
			RapidIterationParameter = CreateRapidIterationVariable(AliasedInputParameterHandle.GetParameterHandleString());

			for (TWeakObjectPtr<UNiagaraScript> Script : AffectedScripts)
			{
				Script->Modify();
				Script->RapidIterationParameters.RenameParameter(OldRapidIterationParameter, *RapidIterationParameter.GetName().ToString());
			}

			UE_LOG(LogNiagaraEditor, Log, TEXT("Renaming %s to %s"), *OldRapidIterationParameter.GetName().ToString(), *RapidIterationParameter.GetName().ToString());
		}

		// Go ahead and have the override pin point to the new name instead of the old name..
		if (OriginalOverridePin != nullptr)
		{
			OriginalOverridePin->PinName = AliasedInputParameterHandle.GetParameterHandleString();
		}
		
		StackEditorDataKey = FNiagaraStackGraphUtilities::GenerateStackFunctionInputEditorDataKey(*OwningFunctionCallNode.Get(), InputParameterHandle);
		GetStackEditorData().SetStackEntryIsExpanded(FNiagaraStackGraphUtilities::GenerateStackModuleEditorDataKey(*OwningAssignmentNode), bIsCurrentlyExpanded);

		CastChecked<UNiagaraGraph>(OwningAssignmentNode->GetGraph())->NotifyGraphNeedsRecompile();
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

		UEdGraphPin* OverridePin = GetOverridePin();
		if (OverridePin != nullptr)
		{
			// If there is an override pin and connected nodes, remove them before removing the input since removing
			// the input will prevent us from finding the override pin.
			RemoveNodesForOverridePin(*OverridePin);
			UNiagaraNodeParameterMapSet* OverrideNode = GetOverrideNode();
			OverrideNode->RemovePin(OverridePin);
		}

		FNiagaraVariable Var = FNiagaraVariable(GetInputType(), GetInputParameterHandle().GetName());
		NodeAssignment->Modify();
		NodeAssignment->RemoveParameter(Var);
	}
}

void UNiagaraStackFunctionInput::GetNamespacesForNewParameters(TArray<FName>& OutNamespacesForNewParameters) const
{
	UNiagaraNodeOutput* OutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*OwningFunctionCallNode);
	bool bIsEditingSystem = GetSystemViewModel()->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset;

	if (OutputNode->GetUsage() == ENiagaraScriptUsage::ParticleSpawnScript || OutputNode->GetUsage() == ENiagaraScriptUsage::ParticleUpdateScript)
	{
		OutNamespacesForNewParameters.Add(FNiagaraParameterHandle::ParticleAttributeNamespace);
		OutNamespacesForNewParameters.Add(FNiagaraParameterHandle::EmitterNamespace);
		if (bIsEditingSystem)
		{
			OutNamespacesForNewParameters.Add(FNiagaraParameterHandle::SystemNamespace);
			OutNamespacesForNewParameters.Add(FNiagaraParameterHandle::UserNamespace);
		}
	}
	else if (OutputNode->GetUsage() == ENiagaraScriptUsage::EmitterSpawnScript || OutputNode->GetUsage() == ENiagaraScriptUsage::EmitterUpdateScript)
	{
		OutNamespacesForNewParameters.Add(FNiagaraParameterHandle::EmitterNamespace);
		if (bIsEditingSystem)
		{
			OutNamespacesForNewParameters.Add(FNiagaraParameterHandle::SystemNamespace);
			OutNamespacesForNewParameters.Add(FNiagaraParameterHandle::UserNamespace);
		}
	}
	else if ((OutputNode->GetUsage() == ENiagaraScriptUsage::SystemSpawnScript || OutputNode->GetUsage() == ENiagaraScriptUsage::SystemUpdateScript) && bIsEditingSystem)
	{
		OutNamespacesForNewParameters.Add(FNiagaraParameterHandle::SystemNamespace);
		OutNamespacesForNewParameters.Add(FNiagaraParameterHandle::UserNamespace);
	}
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
		InputValues.DynamicNode->Modify();
		InputValues.DynamicNode->FunctionScript = DynamicInputScript;
		InputValues.DynamicNode->RefreshFromExternalChanges();
		InputValues.DynamicNode->MarkNodeRequiresSynchronization(TEXT("Dynamic input script reassigned."), true);
		RefreshChildren();
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
		ClipboardInput = UNiagaraClipboardFunctionInput::CreateDataValue(InOuter, InputName, InputType, bEditConditionValue, InputValues.DataObjects.GetValueObject());
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
	case EValueMode::Invalid:
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
			ClipboardFunctionInput.Data->CopyTo(GetDataValueObject());
			break;
		case ENiagaraClipboardFunctionInputValueMode::Expression:
			SetCustomExpression(ClipboardFunctionInput.Expression);
			break;
		case ENiagaraClipboardFunctionInputValueMode::Dynamic:
			if (ensureMsgf(ClipboardFunctionInput.Dynamic->ScriptMode == ENiagaraClipboardFunctionScriptMode::ScriptAsset,
				TEXT("Can not set dynamic input value from clipboard, only script asset funcitons can be set.")))
			{
				SetDynamicInput(ClipboardFunctionInput.Dynamic->Script, ClipboardFunctionInput.Dynamic->FunctionName);
				TArray<UNiagaraStackFunctionInputCollection*> DynamicInputCollections;
				GetUnfilteredChildrenOfType(DynamicInputCollections);
				for (UNiagaraStackFunctionInputCollection* DynamicInputCollection : DynamicInputCollections)
				{
					DynamicInputCollection->SetValuesFromClipboardFunctionInputs(ClipboardFunctionInput.Dynamic->Inputs);
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
		else if (InputValues.Mode == EValueMode::Data && InputValues.DataObjects.IsValid())
		{
			if (InputValues.DataObjects.GetValueObject() != nullptr)
			{
				SearchItems.Add({ FName("LinkedDataInterfaceName"), FText::FromString(InputValues.DataObjects.GetValueObject()->GetName()) });
			}
			if (InputValues.DataObjects.GetDefaultValueObject() != nullptr)
			{
				SearchItems.Add({ FName("LinkedDataInterfaceDefaultName"), InputValues.DataObjects.GetDefaultValueObject()->GetClass()->GetDisplayNameText() });
			}
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

UEdGraphPin* UNiagaraStackFunctionInput::GetDefaultPin() const
{
	// If we have a static switch parameter, we check the pins of the function call node for a matching name,
	// otherwise we search the parameter map node inside the function for a matching pin
	if (IsStaticParameter())
	{
		const UEdGraphSchema_Niagara* NiagaraSchema = CastChecked<UEdGraphSchema_Niagara>(OwningFunctionCallNode->GetSchema());
		FName InputName = InputParameterHandle.GetParameterHandleString();
		for (UEdGraphPin* Pin : OwningFunctionCallNode->Pins)
		{
			FNiagaraVariable InputVariable = NiagaraSchema->PinToNiagaraVariable(Pin);
			if (InputVariable.GetName() == InputName && InputVariable.GetType() == InputType)
			{
				return Pin;
			}
		}
		return nullptr;
	}
	return OwningFunctionCallNode->FindParameterMapDefaultValuePin(InputParameterHandle.GetParameterHandleString(), SourceScript->GetUsage());
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

bool UNiagaraStackFunctionInput::TryGetCurrentLocalValue(TSharedPtr<FStructOnScope>& LocalValue, UEdGraphPin& DefaultPin, UEdGraphPin& ValuePin, TSharedPtr<FStructOnScope> OldValueToReuse, UNiagaraScriptVariable* InVariable)
{
	if (InputType.IsUObject() == false && ValuePin.LinkedTo.Num() == 0)
	{
		const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
		FNiagaraVariable ValueVariable = NiagaraSchema->PinToNiagaraVariable(&ValuePin, true);
		if (OldValueToReuse.IsValid() && OldValueToReuse->GetStruct() == ValueVariable.GetType().GetStruct())
		{
			LocalValue = OldValueToReuse;
		}
		else
		{
			LocalValue = MakeShared<FStructOnScope>(ValueVariable.GetType().GetStruct());
		}

		// If the default pin in the function graph is connected internally, rapid iteration parameters can't be used since
		// the compilation currently won't use them.
		bool bCanUseRapidIterationParameter = IsRapidIterationCandidate() && DefaultPin.LinkedTo.Num() == 0;
		bool bFoundRapidIterationParameter = false;
		if (InVariable && InVariable->DefaultMode == ENiagaraDefaultMode::Binding)
		{
			bCanUseRapidIterationParameter = false;
		}
		if (bCanUseRapidIterationParameter)
		{
			const uint8* RapidIterationParameterData = SourceScript->RapidIterationParameters.GetParameterData(RapidIterationParameter);
			if(RapidIterationParameterData != nullptr)
			{
				FMemory::Memcpy(LocalValue->GetStructMemory(), RapidIterationParameterData, ValueVariable.GetSizeInBytes());
				bFoundRapidIterationParameter = true;
			}
		}

		if(bFoundRapidIterationParameter == false)
		{
			ValueVariable.CopyTo(LocalValue->GetStructMemory());
		}
		return true;
	}
	return false;
}

bool UNiagaraStackFunctionInput::TryGetCurrentDataValue(FDataValues& DataValues, UEdGraphPin* OverrideValuePin, UEdGraphPin& DefaultValuePin, UNiagaraDataInterface* LocallyOwnedDefaultDataValueObjectToReuse)
{
	if (InputType.IsDataInterface())
	{
		UNiagaraDataInterface* DataValueObject = nullptr;
		if (OverrideValuePin != nullptr)
		{
			if (OverrideValuePin->LinkedTo.Num() == 1)
			{
				UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(OverrideValuePin->LinkedTo[0]->GetOwningNode());
				if (InputNode != nullptr)
				{
					if (InputNode->Usage == ENiagaraInputNodeUsage::Parameter)
					{
						DataValueObject = InputNode->GetDataInterface();
					}
				}
			}
		}

		UNiagaraDataInterface* DefaultDataValueObject = nullptr;
		FDataValues::EDefaultValueOwner DefaultDataValueOwner = FDataValues::EDefaultValueOwner::Invalid;
		if (DefaultValuePin.LinkedTo.Num() == 1)
		{
			UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(DefaultValuePin.LinkedTo[0]->GetOwningNode());
			if (InputNode != nullptr && InputNode->Usage == ENiagaraInputNodeUsage::Parameter && InputNode->GetDataInterface() != nullptr)
			{
				DefaultDataValueObject = InputNode->GetDataInterface();
				DefaultDataValueOwner = FDataValues::EDefaultValueOwner::FunctionOwned;
			}
		}

		if (DefaultDataValueObject == nullptr)
		{
			if (LocallyOwnedDefaultDataValueObjectToReuse == nullptr)
			{
				DefaultDataValueObject = NewObject<UNiagaraDataInterface>(this, const_cast<UClass*>(InputType.GetClass()), NAME_None, RF_Transactional | RF_Public);
			}
			else
			{
				DefaultDataValueObject = LocallyOwnedDefaultDataValueObjectToReuse;
			}
			DefaultDataValueOwner = FDataValues::EDefaultValueOwner::LocallyOwned;
		}
		
		DataValues = FDataValues(DataValueObject, DefaultDataValueObject, DefaultDataValueOwner);
		return true;
	}
	return false;
}

bool UNiagaraStackFunctionInput::TryGetCurrentLinkedValue(FNiagaraParameterHandle& LinkedValueHandle, UEdGraphPin& ValuePin)
{
	if (ValuePin.LinkedTo.Num() == 1)
	{
		UEdGraphPin* CurrentValuePin = &ValuePin;
		TSharedPtr<TArray<FNiagaraParameterHandle>> AvailableHandles;
		while (CurrentValuePin != nullptr)
		{
			UEdGraphPin* LinkedValuePin = CurrentValuePin->LinkedTo[0];
			CurrentValuePin = nullptr;

			UNiagaraNodeParameterMapGet* GetNode = Cast<UNiagaraNodeParameterMapGet>(LinkedValuePin->GetOwningNode());
			if (GetNode == nullptr)
			{
				// Only parameter map get nodes are supported for linked values.
				return false;
			}

			// If a parameter map get node was found, the linked handle will be stored in the pin name.  
			FNiagaraParameterHandle LinkedValueHandleFromNode(LinkedValuePin->PinName);

			UEdGraphPin* LinkedValueHandleDefaultPin = GetNode->GetDefaultPin(LinkedValuePin);
			if (LinkedValueHandleDefaultPin->LinkedTo.Num() == 0)
			{
				// If the default value pin for this get node isn't connected this is the last read in the chain
				// so return the handle.
				LinkedValueHandle = LinkedValueHandleFromNode;
				return true;
			}
			else
			{
				// If the default value pin for the get node is connected then there are a chain of possible values.
				// if the value of the current get node is available it can be returned, otherwise we need to check the
				// next node.
				if (AvailableHandles.IsValid() == false)
				{
					AvailableHandles = MakeShared<TArray<FNiagaraParameterHandle>>();
					GetAvailableParameterHandles(*AvailableHandles);
				}

				if (AvailableHandles->Contains(LinkedValueHandleFromNode))
				{
					LinkedValueHandle = LinkedValueHandleFromNode;
					return true;
				}
				else
				{
					CurrentValuePin = LinkedValueHandleDefaultPin;
				}
			}
		}
	}
	return false;
}

bool UNiagaraStackFunctionInput::TryGetCurrentExpressionValue(TWeakObjectPtr<UNiagaraNodeCustomHlsl>& ExpressionValue, UEdGraphPin* OverridePin)
{
	if (OverridePin != nullptr && OverridePin->LinkedTo.Num() == 1)
	{
		UNiagaraNodeCustomHlsl* DynamicNode = Cast<UNiagaraNodeCustomHlsl>(OverridePin->LinkedTo[0]->GetOwningNode());
		if (DynamicNode != nullptr)
		{
			ExpressionValue = DynamicNode;
			return true;
		}
	}
	return false;
}

bool UNiagaraStackFunctionInput::TryGetCurrentDynamicValue(TWeakObjectPtr<UNiagaraNodeFunctionCall>& DynamicValue, UEdGraphPin* OverridePin)
{
	if (OverridePin != nullptr && OverridePin->LinkedTo.Num() == 1)
	{
		UNiagaraNodeFunctionCall* DynamicNode = Cast<UNiagaraNodeFunctionCall>(OverridePin->LinkedTo[0]->GetOwningNode());
		if (DynamicNode != nullptr)
		{
			DynamicValue = DynamicNode;
			return true;
		}
	}
	return false;
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

#undef LOCTEXT_NAMESPACE
