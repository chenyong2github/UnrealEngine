// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "KismetNodes/SGraphNodeK2CreateDelegate.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/SBoxPanel.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CreateDelegate.h"

#include "Editor/UnrealEd/Public/Kismet2/BlueprintEditorUtils.h"
#include "Editor/UnrealEd/Public/Kismet2/KismetEditorUtilities.h"
#include "Editor/UnrealEd/Public/ScopedTransaction.h"
#include "Editor/BlueprintGraph/Classes/BlueprintNodeBinder.h"
#include "Editor/BlueprintGraph/Classes/BlueprintEventNodeSpawner.h"
#include "Editor/BlueprintGraph/Classes/K2Node_CustomEvent.h"
#include "Editor/GraphEditor/Public/SGraphNode.h"

FText SGraphNodeK2CreateDelegate::FunctionDescription(const UFunction* Function, const bool bOnlyDescribeSignature /*= false*/, const int32 CharacterLimit /*= 32*/)
{
	if (!Function || !Function->GetOuter())
	{
		return NSLOCTEXT("GraphNodeK2Create", "Error", "Error");
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	FString Result;
	
	// Show function name.
	if (!bOnlyDescribeSignature)
	{
		Result = Function->GetName();
	}

	Result += TEXT("(");

	// Describe input parameters.
	{
		bool bFirst = true;
		for (TFieldIterator<UProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
		{
			UProperty* const Param = *PropIt;
			const bool bIsFunctionInput = Param && (!Param->HasAnyPropertyFlags(CPF_OutParm) || Param->HasAnyPropertyFlags(CPF_ReferenceParm));
			if (bIsFunctionInput)
			{
				if (!bFirst)
				{
					Result += TEXT(", ");
				}
				if (CharacterLimit > INDEX_NONE && Result.Len() > CharacterLimit)
				{
					Result += TEXT("...");
					break;
				}
				Result += bOnlyDescribeSignature ? UEdGraphSchema_K2::TypeToText(Param).ToString() : Param->GetName();
				bFirst = false;
			}
		}
	}

	Result += TEXT(")");

	// Describe outputs.
	{
		TArray<FString> Outputs;

		UProperty* const FunctionReturnProperty = Function->GetReturnProperty();
		if (FunctionReturnProperty)
		{
			Outputs.Add(UEdGraphSchema_K2::TypeToText(FunctionReturnProperty).ToString());
		}

		for (TFieldIterator<UProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
		{
			UProperty* const Param = *PropIt;
			const bool bIsFunctionOutput = Param && Param->HasAnyPropertyFlags(CPF_OutParm);
			if (bIsFunctionOutput)
			{
				Outputs.Add(bOnlyDescribeSignature ? UEdGraphSchema_K2::TypeToText(Param).ToString() : Param->GetName());
			}
		}

		if (Outputs.Num() > 0)
		{
			Result += TEXT(" -> ");
		}

		if (Outputs.Num() > 1)
		{
			Result += TEXT("[");
		}

		bool bFirst = true;
		for (const FString& Output : Outputs)
		{
			if (!bFirst)
			{
				Result += TEXT(", ");
			}
			if (CharacterLimit > INDEX_NONE && Result.Len() > CharacterLimit)
			{
				Result += TEXT("...");
				break;
			}
			Result += Output;
			bFirst = false;
		}

		if (Outputs.Num() > 1)
		{
			Result += TEXT("]");
		}
	}

	return FText::FromString(Result);
}

void SGraphNodeK2CreateDelegate::Construct(const FArguments& InArgs, UK2Node* InNode)
{
	GraphNode = InNode;
	UpdateGraphNode();
}

FText SGraphNodeK2CreateDelegate::GetCurrentFunctionDescription() const
{
	UK2Node_CreateDelegate* Node = Cast<UK2Node_CreateDelegate>(GraphNode);
	UFunction* FunctionSignature = Node ? Node->GetDelegateSignature() : nullptr;
	UClass* ScopeClass = Node ? Node->GetScopeClass() : nullptr;

	if (!FunctionSignature || !ScopeClass)
	{
		return FText::GetEmpty();
	}

	if (const UFunction* Func = FindField<UFunction>(ScopeClass, Node->GetFunctionName()))
	{
		return FunctionDescription(Func);
	}

	if (Node->GetFunctionName() != NAME_None)
	{
		return FText::Format(NSLOCTEXT("GraphNodeK2Create", "ErrorLabelFmt", "Error? {0}"), FText::FromName(Node->GetFunctionName()));
	}

	return NSLOCTEXT("GraphNodeK2Create", "SelectFunctionLabel", "Select Function...");
}

TSharedRef<ITableRow> SGraphNodeK2CreateDelegate::HandleGenerateRowFunction(TSharedPtr<FFunctionItemData> FunctionItemData, const TSharedRef<STableViewBase>& OwnerTable)
{
	check(FunctionItemData.IsValid());
	return SNew(STableRow< TSharedPtr<FFunctionItemData> >, OwnerTable).Content()
		[
			SNew(STextBlock).Text(FunctionItemData->Description)
		];
}

void SGraphNodeK2CreateDelegate::OnFunctionSelected(TSharedPtr<FFunctionItemData> FunctionItemData, ESelectInfo::Type SelectInfo)
{
	const FScopedTransaction Transaction(NSLOCTEXT("GraphNodeK2Create", "CreateMatchingSigniture", "Create matching signiture"));

	if (FunctionItemData.IsValid())
	{
		if (UK2Node_CreateDelegate* Node = Cast<UK2Node_CreateDelegate>(GraphNode))
		{
			UBlueprint* NodeBP = Node->GetBlueprint();
			UEdGraph* const SourceGraph = Node->GetGraph();
			check(NodeBP && SourceGraph);
			SourceGraph->Modify();
			NodeBP->Modify();
			Node->Modify();

			if (FunctionItemData == CreateMatchingFunctionData)
			{
				// Get a valid name for the function graph
				FString ProposedFuncName = NodeBP->GetName() + "_AutoGenFunc";
				FName NewFuncName = FBlueprintEditorUtils::GenerateUniqueGraphName(NodeBP, ProposedFuncName);
				
				UEdGraph* NewGraph = nullptr;
				NewGraph = FBlueprintEditorUtils::CreateNewGraph(NodeBP, NewFuncName, SourceGraph->GetClass(), SourceGraph->GetSchema() ? SourceGraph->GetSchema()->GetClass() : GetDefault<UEdGraphSchema_K2>()->GetClass());

				if (NewGraph != nullptr)
				{
					FBlueprintEditorUtils::AddFunctionGraph<UFunction>(NodeBP, NewGraph, true, Node->GetDelegateSignature());
					FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(NewGraph);
				}
				
				Node->SetFunction(NewFuncName);
			}
			else if (FunctionItemData == CreateMatchingEventData)
			{
				// Get a valid name for the function graph
				FName NewEventName = FBlueprintEditorUtils::FindUniqueCustomEventName(NodeBP);

				UBlueprintEventNodeSpawner* Spawner = UBlueprintEventNodeSpawner::Create(UK2Node_CustomEvent::StaticClass(), NewEventName);
				UEdGraphNode* NewNode = Spawner->Invoke(Node->GetGraph(), IBlueprintNodeBinder::FBindingSet(), FVector2D(Node->NodePosX, Node->NodePosY + 200));

				if (UK2Node_CustomEvent* NewEventNode = Cast<UK2Node_CustomEvent>(NewNode))
				{
					NewEventNode->SetDelegateSignature(Node->GetDelegateSignature());
					// Reconstruct to get the new parameters to show in the editor
					NewEventNode->ReconstructNode();
					NewEventNode->bIsEditable = true;
					FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(NewEventNode);
				}
				
				Node->SetFunction(NewEventName);
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(NodeBP);				
			}
			else
			{
				Node->SetFunction(FunctionItemData->Name);
			}
			
			Node->HandleAnyChange(true);

			auto SelectFunctionWidgetPtr = SelectFunctionWidget.Pin();
			if (SelectFunctionWidgetPtr.IsValid())
			{
				SelectFunctionWidgetPtr->SetIsOpen(false);
			}
		}
	}
}

TSharedPtr<SGraphNodeK2CreateDelegate::FFunctionItemData> SGraphNodeK2CreateDelegate::AddDefaultFunctionDataOption(const FText& DisplayName)
{
	TSharedPtr<FFunctionItemData> NewEntry = MakeShareable(new FFunctionItemData());
	NewEntry->Description = DisplayName;
	FunctionDataItems.Add(NewEntry);
	return NewEntry;
}

void SGraphNodeK2CreateDelegate::CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox)
{
	if (UK2Node_CreateDelegate* Node = Cast<UK2Node_CreateDelegate>(GraphNode))
	{
		UFunction* FunctionSignature = Node->GetDelegateSignature();
		UClass* ScopeClass = Node->GetScopeClass();

		if (FunctionSignature && ScopeClass)
		{
			FText FunctionSignaturePrompt;
			{
				FFormatNamedArguments FormatArguments;
				FormatArguments.Add(TEXT("FunctionSignature"), FunctionDescription(FunctionSignature, true));
				FunctionSignaturePrompt = FText::Format(NSLOCTEXT("GraphNodeK2Create", "FunctionSignaturePrompt", "Signature: {FunctionSignature}"), FormatArguments);
			}

			FText FunctionSignatureToolTipText;
			{
				FFormatNamedArguments FormatArguments;
				FormatArguments.Add(TEXT("FullFunctionSignature"), FunctionDescription(FunctionSignature, true, INDEX_NONE));
				FunctionSignatureToolTipText = FText::Format(NSLOCTEXT("GraphNodeK2Create", "FunctionSignatureToolTip", "Signature Syntax: (Inputs) -> [Outputs]\nFull Signature:{FullFunctionSignature}"), FormatArguments);
			}

			MainBox->AddSlot()
				.AutoHeight()
				.VAlign(VAlign_Fill)
				.Padding(4.0f)
				[
					SNew(STextBlock)
					.Text(FunctionSignaturePrompt)
					.ToolTipText(FunctionSignatureToolTipText)
				];

			FunctionDataItems.Empty();

			// add an empty row, so the user can clear the selection if they want
			AddDefaultFunctionDataOption(NSLOCTEXT("GraphNodeK2Create", "EmptyFunctionOption", "[None]"));

			// Option to create a function based on the event parameters
			CreateMatchingFunctionData = AddDefaultFunctionDataOption(NSLOCTEXT("GraphNodeK2Create", "CreateMatchingFunctionOption", "[Create a matching function]"));

			// Only signatures with no output parameters can be events
			if (!UEdGraphSchema_K2::HasFunctionAnyOutputParameter(FunctionSignature))
			{
				CreateMatchingEventData = AddDefaultFunctionDataOption(NSLOCTEXT("GraphNodeK2Create", "CreateMatchingEventOption", "[Create a matching event]"));
			}

			for (TFieldIterator<UFunction> It(ScopeClass); It; ++It)
			{
				UFunction* Func = *It;
				if (Func && FunctionSignature->IsSignatureCompatibleWith(Func) &&
					UEdGraphSchema_K2::FunctionCanBeUsedInDelegate(Func))
				{
					TSharedPtr<FFunctionItemData> ItemData = MakeShareable(new FFunctionItemData());
					ItemData->Name = Func->GetFName();
					ItemData->Description = FunctionDescription(Func);
					FunctionDataItems.Add(ItemData);
				}
			}

			TSharedRef<SComboButton> SelectFunctionWidgetRef = SNew(SComboButton)
				.Method(EPopupMethod::CreateNewWindow)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(this, &SGraphNodeK2CreateDelegate::GetCurrentFunctionDescription)
				]
			.MenuContent()
				[
					SNew(SVerticalBox)
 					 + SVerticalBox::Slot()
 					 .AutoHeight()
 					 .MaxHeight(500.f)
 					 [
 						SNew(SListView<TSharedPtr<FFunctionItemData> >)
 						.ListItemsSource(&FunctionDataItems)
 						.OnGenerateRow(this, &SGraphNodeK2CreateDelegate::HandleGenerateRowFunction)
 						.OnSelectionChanged(this, &SGraphNodeK2CreateDelegate::OnFunctionSelected)
 					 ]
				];

			MainBox->AddSlot()
				.AutoHeight()
				.VAlign(VAlign_Fill)
				.Padding(4.0f)
				[
					SelectFunctionWidgetRef
				];

			SelectFunctionWidget = SelectFunctionWidgetRef;
		}
	}
}

SGraphNodeK2CreateDelegate::~SGraphNodeK2CreateDelegate()
{
	auto SelectFunctionWidgetPtr = SelectFunctionWidget.Pin();
	if (SelectFunctionWidgetPtr.IsValid())
	{
		SelectFunctionWidgetPtr->SetIsOpen(false);
	}
}
