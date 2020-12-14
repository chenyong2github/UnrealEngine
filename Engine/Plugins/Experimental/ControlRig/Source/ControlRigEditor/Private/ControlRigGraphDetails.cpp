// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigGraphDetails.h"
#include "Widgets/SWidget.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "EditorStyleSet.h"
#include "SPinTypeSelector.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "PropertyCustomizationHelpers.h"
#include "NodeFactory.h"
#include "Graph/ControlRigGraphNode.h"
#include "ControlRig.h"
#include "RigVMCore/RigVMExternalVariable.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "ControlRigGraphDetails"

FControlRigArgumentGroupLayout::FControlRigArgumentGroupLayout(URigVMGraph* InGraph, UControlRigBlueprint* InBlueprint, bool bInputs)
	: GraphPtr(InGraph)
	, ControlRigBlueprintPtr(InBlueprint)
	, bIsInputGroup(bInputs)
{
	if (ControlRigBlueprintPtr.IsValid())
	{
		ControlRigBlueprintPtr.Get()->OnModified().AddRaw(this, &FControlRigArgumentGroupLayout::HandleModifiedEvent);
	}
}

FControlRigArgumentGroupLayout::~FControlRigArgumentGroupLayout()
{
	if (ControlRigBlueprintPtr.IsValid())
	{
		ControlRigBlueprintPtr.Get()->OnModified().RemoveAll(this);
	}
}

void FControlRigArgumentGroupLayout::GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder)
{
	bool WasContentAdded = false;
	if (GraphPtr.IsValid())
	{
		URigVMGraph* Graph = GraphPtr.Get();
		if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Graph->GetOuter()))
		{
			for (URigVMPin* Pin : LibraryNode->GetPins())
			{
				if ((bIsInputGroup && (Pin->GetDirection() == ERigVMPinDirection::Input || Pin->GetDirection() == ERigVMPinDirection::IO)) ||
					(!bIsInputGroup && (Pin->GetDirection() == ERigVMPinDirection::Output || Pin->GetDirection() == ERigVMPinDirection::IO)))
				{
					TSharedRef<class FControlRigArgumentLayout> ControlRigArgumentLayout = MakeShareable(new FControlRigArgumentLayout(
						Pin,
						Graph,
						ControlRigBlueprintPtr.Get()));
					ChildrenBuilder.AddCustomBuilder(ControlRigArgumentLayout);
					WasContentAdded = true;
				}
			}
		}
	}
	if (!WasContentAdded)
	{
		// Add a text widget to let the user know to hit the + icon to add parameters.
		ChildrenBuilder.AddCustomRow(FText::GetEmpty()).WholeRowContent()
		.MaxDesiredWidth(980.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoArgumentsAddedForControlRig", "Please press the + icon above to add parameters"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
	}
}

void FControlRigArgumentGroupLayout::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	if (!GraphPtr.IsValid())
	{
		return;
	}
	
	URigVMGraph* Graph = GraphPtr.Get();
	URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Graph->GetOuter());
	if (LibraryNode == nullptr)
	{
		return;
	}

	switch (InNotifType)
	{
		case ERigVMGraphNotifType::PinAdded:
		case ERigVMGraphNotifType::PinRemoved:
		{
			URigVMPin* Pin = CastChecked<URigVMPin>(InSubject);
			if (Pin->GetNode() == LibraryNode)
			{
				OnRebuildChildren.ExecuteIfBound();
			}
			break;
		}
		default:
		{
			break;
		}
	}
}

void FControlRigArgumentLayout::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	ETypeTreeFilter TypeTreeFilter = ETypeTreeFilter::None;
	TypeTreeFilter |= ETypeTreeFilter::AllowExec;

	NodeRow
	.NameContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		.VAlign(VAlign_Center)
		[
			SAssignNew(ArgumentNameWidget, SEditableTextBox)
			.Text(this, &FControlRigArgumentLayout::OnGetArgNameText)
			.OnTextChanged(this, &FControlRigArgumentLayout::OnArgNameChange)
			.OnTextCommitted(this, &FControlRigArgumentLayout::OnArgNameTextCommitted)
			.ToolTipText(this, &FControlRigArgumentLayout::OnGetArgToolTipText)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.IsEnabled(!ShouldPinBeReadOnly())
		]
	]
	.ValueContent()
	.MaxDesiredWidth(980.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		.AutoWidth()
		[
			SNew(SPinTypeSelector, FGetPinTypeTree::CreateUObject(K2Schema, &UEdGraphSchema_K2::GetVariableTypeTree))
			.TargetPinType(this, &FControlRigArgumentLayout::OnGetPinInfo)
			.OnPinTypePreChanged(this, &FControlRigArgumentLayout::OnPrePinInfoChange)
			.OnPinTypeChanged(this, &FControlRigArgumentLayout::PinInfoChanged)
			.Schema(K2Schema)
			.TypeTreeFilter(TypeTreeFilter)
			.bAllowArrays(!ShouldPinBeReadOnly())
			.IsEnabled(!ShouldPinBeReadOnly(true))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.ContentPadding(0)
			.IsEnabled(!IsPinEditingReadOnly())
			.OnClicked(this, &FControlRigArgumentLayout::OnArgMoveUp)
			.ToolTipText(LOCTEXT("FunctionArgDetailsArgMoveUpTooltip", "Move this parameter up in the list."))
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Icons.ChevronUp"))
			.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.ContentPadding(0)
			.IsEnabled(!IsPinEditingReadOnly())
			.OnClicked(this, &FControlRigArgumentLayout::OnArgMoveDown)
			.ToolTipText(LOCTEXT("FunctionArgDetailsArgMoveDownTooltip", "Move this parameter down in the list."))
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Icons.ChevronDown"))
			.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(10, 0, 0, 0)
		.AutoWidth()
		[
			PropertyCustomizationHelpers::MakeClearButton(FSimpleDelegate::CreateSP(this, &FControlRigArgumentLayout::OnRemoveClicked), LOCTEXT("FunctionArgDetailsClearTooltip", "Remove this parameter."), !IsPinEditingReadOnly())
		]
	];
}

void FControlRigArgumentLayout::GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder)
{
	if (UEdGraphPin* FoundPin = GetPin())
	{
		// Certain types are outlawed at the compiler level, or to keep consistency with variable rules for actors
		const UClass* ClassObject = Cast<UClass>(FoundPin->PinType.PinSubCategoryObject.Get());
		const bool bTypeWithNoDefaults = (FoundPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object) || (FoundPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class) || (FoundPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface)
			|| (FoundPin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject && ClassObject && ClassObject->IsChildOf(AActor::StaticClass()))
			|| UEdGraphSchema_K2::IsExecPin(*FoundPin)
			|| PinPtr.Get()->IsExecuteContext()
			|| FoundPin->PinType.bIsReference;

		if (!bTypeWithNoDefaults)
		{
			DefaultValuePinWidget = FNodeFactory::CreatePinWidget(FoundPin);
			DefaultValuePinWidget->SetOnlyShowDefaultValue(true);
			TSharedRef<SWidget> DefaultValueWidget = DefaultValuePinWidget->GetDefaultValueWidget();

			if (DefaultValueWidget != SNullWidget::NullWidget)
			{
				ChildrenBuilder.AddCustomRow(LOCTEXT("FunctionArgDetailsDefaultValue", "Default Value"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FunctionArgDetailsDefaultValue", "Default Value"))
					.ToolTipText(LOCTEXT("FunctionArgDetailsDefaultValueParamTooltip", "The default value of the parameter."))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MaxDesiredWidth(512)
				[
					DefaultValueWidget
				];
			}
			else
			{
				DefaultValuePinWidget.Reset();
			}
		}
	}
}

void FControlRigArgumentLayout::OnRemoveClicked()
{
	if (PinPtr.IsValid() && ControlRigBlueprintPtr.IsValid())
	{
		URigVMPin* Pin = PinPtr.Get();
		UControlRigBlueprint* Blueprint = ControlRigBlueprintPtr.Get();
		if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Pin->GetNode()))
		{
			if (URigVMController* Controller = Blueprint->GetController(LibraryNode->GetContainedGraph()))
			{
				Controller->RemoveExposedPin(Pin->GetFName(), true);
			}
		}
	}
}

FReply FControlRigArgumentLayout::OnArgMoveUp()
{
	/* todo

	const int32 ThisParamIndex = TargetNode->UserDefinedPins.Find(ParamItemPtr.Pin());
	const int32 NewParamIndex = ThisParamIndex - 1;
	if (ThisParamIndex != INDEX_NONE && NewParamIndex >= 0)
	{
		const FScopedTransaction Transaction(LOCTEXT("K2_MovePinUp", "Move Pin Up"));
		TArray<UK2Node_EditablePinBase*> TargetNodes = GatherAllResultNodes(TargetNode);
		for (UK2Node_EditablePinBase* Node : TargetNodes)
		{
			Node->Modify();
			Node->UserDefinedPins.Swap(ThisParamIndex, NewParamIndex);

			TSharedPtr<FBaseBlueprintGraphActionDetails> GraphActionDetails = GraphActionDetailsPtr.Pin();
			if (GraphActionDetails.IsValid())
			{
				GraphActionDetails->OnParamsChanged(Node, true);
			}
		}
	}

	*/
	return FReply::Handled();
}

FReply FControlRigArgumentLayout::OnArgMoveDown()
{
	/* todo
	const int32 ThisParamIndex = TargetNode->UserDefinedPins.Find(ParamItemPtr.Pin());
	const int32 NewParamIndex = ThisParamIndex + 1;
	if (ThisParamIndex != INDEX_NONE && NewParamIndex < TargetNode->UserDefinedPins.Num())
	{
		const FScopedTransaction Transaction(LOCTEXT("K2_MovePinDown", "Move Pin Down"));
		TArray<UK2Node_EditablePinBase*> TargetNodes = GatherAllResultNodes(TargetNode);
		for (UK2Node_EditablePinBase* Node : TargetNodes)
		{
			Node->Modify();
			Node->UserDefinedPins.Swap(ThisParamIndex, NewParamIndex);

			TSharedPtr<FBaseBlueprintGraphActionDetails> GraphActionDetails = GraphActionDetailsPtr.Pin();
			if (GraphActionDetails.IsValid())
			{
				GraphActionDetails->OnParamsChanged(Node, true);
			}
		}
	}
	*/
	return FReply::Handled();
}

bool FControlRigArgumentLayout::ShouldPinBeReadOnly(bool bIsEditingPinType/* = false*/) const
{
	if (PinPtr.IsValid())
	{
		return PinPtr.Get()->IsExecuteContext();
	}
	return false;
}

bool FControlRigArgumentLayout::IsPinEditingReadOnly(bool bIsEditingPinType/* = false*/) const
{
	if (PinPtr.IsValid())
	{
		return PinPtr.Get()->IsExecuteContext();
	}
	return false;
}

FText FControlRigArgumentLayout::OnGetArgNameText() const
{
	if (PinPtr.IsValid())
	{
		return FText::FromName(PinPtr.Get()->GetFName());
	}
	return FText();
}

FText FControlRigArgumentLayout::OnGetArgToolTipText() const
{
	return OnGetArgNameText(); // for now since we don't have tooltips
}

void FControlRigArgumentLayout::OnArgNameChange(const FText& InNewText)
{
	// do we need validation?
}

void FControlRigArgumentLayout::OnArgNameTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit)
{
	if (!NewText.IsEmpty() && PinPtr.IsValid() && ControlRigBlueprintPtr.IsValid() && !ShouldPinBeReadOnly())
	{
		URigVMPin* Pin = PinPtr.Get();
		UControlRigBlueprint* Blueprint = ControlRigBlueprintPtr.Get();
		if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Pin->GetNode()))
		{
			if (URigVMController* Controller = Blueprint->GetController(LibraryNode->GetContainedGraph()))
			{
				const FString& NewName = NewText.ToString();
				Controller->RenameExposedPin(Pin->GetFName(), *NewName, true);
			}
		}
	}
}

FEdGraphPinType FControlRigArgumentLayout::OnGetPinInfo() const
{
	if (PinPtr.IsValid())
	{
		return UControlRigGraphNode::GetPinTypeForModelPin(PinPtr.Get());
	}
	return FEdGraphPinType();
}

UEdGraphPin* FControlRigArgumentLayout::GetPin() const
{
	if (PinPtr.IsValid() && ControlRigBlueprintPtr.IsValid())
	{
		UControlRigBlueprint* Blueprint = ControlRigBlueprintPtr.Get();
		if(UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Blueprint->GetEdGraph(PinPtr.Get()->GetGraph())))
		{
			if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(RigGraph->FindNodeForModelNodeName(PinPtr.Get()->GetNode()->GetFName())))
			{
				return RigNode->FindPin(PinPtr.Get()->GetPinPath());
			}
		}
	}
	return nullptr;
}

ECheckBoxState FControlRigArgumentLayout::IsRefChecked() const
{
	FEdGraphPinType PinType = OnGetPinInfo();
	return PinType.bIsReference ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FControlRigArgumentLayout::OnRefCheckStateChanged(ECheckBoxState InState)
{
	const FScopedTransaction Transaction(LOCTEXT("ChangeByRef", "Change Pass By Reference"));

	FEdGraphPinType PinType = OnGetPinInfo();
	PinType.bIsReference = (InState == ECheckBoxState::Checked) ? true : false;

	PinInfoChanged(PinType);
}

void FControlRigArgumentLayout::PinInfoChanged(const FEdGraphPinType& PinType)
{
	if (PinPtr.IsValid() && ControlRigBlueprintPtr.IsValid() && FBlueprintEditorUtils::IsPinTypeValid(PinType))
	{
		URigVMPin* Pin = PinPtr.Get();
		UControlRigBlueprint* Blueprint = ControlRigBlueprintPtr.Get();
		if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Pin->GetNode()))
		{
			if (URigVMController* Controller = Blueprint->GetController(LibraryNode->GetContainedGraph()))
			{
				FRigVMExternalVariable ExternalVariable = UControlRig::GetExternalVariableFromPinType(Pin->GetFName(), PinType, true, false);
				if (!ExternalVariable.IsValid(true /* allow nullptr memory */))
				{
					return;
				}

				FString CPPType = ExternalVariable.TypeName.ToString();
				FName CPPTypeObjectName = NAME_None;
				if (ExternalVariable.TypeObject)
				{
					CPPTypeObjectName = *ExternalVariable.TypeObject->GetPathName();

					if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(ExternalVariable.TypeObject))
					{
						CPPType = ScriptStruct->GetStructCPPName();
					}
				}

				if (ExternalVariable.bIsArray)
				{
					CPPType = FString::Printf(TEXT("TArray<%s>"), *CPPType);
				}

				Controller->ChangeExposedPinType(Pin->GetFName(), CPPType, CPPTypeObjectName, true);
			}
		}
	}
}

void FControlRigArgumentLayout::OnPrePinInfoChange(const FEdGraphPinType& PinType)
{
	// not needed for Control Rig
}

TSharedPtr<IDetailCustomization> FControlRigGraphDetails::MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor)
{
	const TArray<UObject*>* Objects = (InBlueprintEditor.IsValid() ? InBlueprintEditor->GetObjectsCurrentlyBeingEdited() : nullptr);
	if (Objects && Objects->Num() == 1)
	{
		if (UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>((*Objects)[0]))
		{
			return MakeShareable(new FControlRigGraphDetails(StaticCastSharedPtr<IControlRigEditor>(InBlueprintEditor), ControlRigBlueprint));
		}
	}

	return nullptr;
}

void FControlRigGraphDetails::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailLayout.GetObjectsBeingCustomized(Objects);

	GraphPtr = CastChecked<UControlRigGraph>(Objects[0].Get());
	UControlRigGraph* Graph = GraphPtr.Get();

	UControlRigBlueprint* Blueprint = ControlRigBlueprintPtr.Get();
	URigVMGraph* Model = nullptr;
	URigVMController* Controller = nullptr;

	if (Blueprint)
	{
		Model = Blueprint->GetModel(Graph);
		Controller = Blueprint->GetController(Model);
	}

	if (Blueprint == nullptr || Model == nullptr || Controller == nullptr)
	{
		IDetailCategoryBuilder& Category = DetailLayout.EditCategory("Graph", LOCTEXT("FunctionDetailsGraph", "Graph"));
		Category.AddCustomRow(FText::GetEmpty())
		[
			SNew(STextBlock)
			.Text(LOCTEXT("GraphPresentButNotEditable", "Graph is not editable."))
		];
		return;
	}

	if (Model->IsTopLevelGraph())
	{
		IDetailCategoryBuilder& Category = DetailLayout.EditCategory("Graph", LOCTEXT("FunctionDetailsGraph", "Graph"));
		Category.AddCustomRow(FText::GetEmpty())
			[
				SNew(STextBlock)
				.Text(LOCTEXT("GraphIsTopLevelGraph", "Top-level Graphs are not editable."))
			];
		return;
	}

	IDetailCategoryBuilder& InputsCategory = DetailLayout.EditCategory("Inputs", LOCTEXT("FunctionDetailsInputs", "Inputs"));
	TSharedRef<FControlRigArgumentGroupLayout> InputArgumentGroup = MakeShareable(new FControlRigArgumentGroupLayout(Model, Blueprint, true));
	InputsCategory.AddCustomBuilder(InputArgumentGroup);

	TSharedRef<SHorizontalBox> InputsHeaderContentWidget = SNew(SHorizontalBox);
	TWeakPtr<SWidget> WeakInputsHeaderWidget = InputsHeaderContentWidget;

	InputsHeaderContentWidget->AddSlot()
	.HAlign(HAlign_Right)
	[
		SNew(SButton)
		.ButtonStyle(FEditorStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(1, 0))
		.OnClicked(this, &FControlRigGraphDetails::OnAddNewInputClicked)
		.Visibility(this, &FControlRigGraphDetails::GetAddNewInputOutputVisibility)
		.HAlign(HAlign_Right)
		.ToolTipText(LOCTEXT("FunctionNewInputArgTooltip", "Create a new input argument"))
		.VAlign(VAlign_Center)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("FunctionNewInputArg")))
		.IsEnabled(this, &FControlRigGraphDetails::IsAddNewInputOutputEnabled)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		]
	];
	InputsCategory.HeaderContent(InputsHeaderContentWidget);

	IDetailCategoryBuilder& OutputsCategory = DetailLayout.EditCategory("Outputs", LOCTEXT("FunctionDetailsOutputs", "Outputs"));
	TSharedRef<FControlRigArgumentGroupLayout> OutputArgumentGroup = MakeShareable(new FControlRigArgumentGroupLayout(Model, Blueprint, false));
	OutputsCategory.AddCustomBuilder(OutputArgumentGroup);

	TSharedRef<SHorizontalBox> OutputsHeaderContentWidget = SNew(SHorizontalBox);
	TWeakPtr<SWidget> WeakOutputsHeaderWidget = OutputsHeaderContentWidget;

	OutputsHeaderContentWidget->AddSlot()
	.HAlign(HAlign_Right)
	[
		SNew(SButton)
		.ButtonStyle(FEditorStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(1, 0))
		.OnClicked(this, &FControlRigGraphDetails::OnAddNewOutputClicked)
		.Visibility(this, &FControlRigGraphDetails::GetAddNewInputOutputVisibility)
		.HAlign(HAlign_Right)
		.ToolTipText(LOCTEXT("FunctionNewOutputArgTooltip", "Create a new output argument"))
		.VAlign(VAlign_Center)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("FunctionNewOutputArg")))
		.IsEnabled(this, &FControlRigGraphDetails::IsAddNewInputOutputEnabled)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		]
	];
	OutputsCategory.HeaderContent(OutputsHeaderContentWidget);
}

bool FControlRigGraphDetails::IsAddNewInputOutputEnabled() const
{
	return true;
}

EVisibility FControlRigGraphDetails::GetAddNewInputOutputVisibility() const
{
	return EVisibility::Visible;
}

FReply FControlRigGraphDetails::OnAddNewInputClicked()
{
	if (GraphPtr.IsValid() && ControlRigBlueprintPtr.IsValid())
	{
		UControlRigBlueprint* Blueprint = ControlRigBlueprintPtr.Get();
		URigVMGraph* Model = Blueprint->GetModel(GraphPtr.Get());
		if (URigVMController* Controller = Blueprint->GetController(Model))
		{
			FName ArgumentName = TEXT("Argument");
			FString CPPType = TEXT("bool");
			FName CPPTypeObjectPath = NAME_None;
			FString DefaultValue = TEXT("False");
			// todo: base decisions on types on last argument

			Controller->AddExposedPin(ArgumentName, ERigVMPinDirection::Input, CPPType, CPPTypeObjectPath, DefaultValue, true);
		}
	}
	return FReply::Unhandled();
}

FReply FControlRigGraphDetails::OnAddNewOutputClicked()
{
	if (GraphPtr.IsValid() && ControlRigBlueprintPtr.IsValid())
	{
		UControlRigBlueprint* Blueprint = ControlRigBlueprintPtr.Get();
		URigVMGraph* Model = Blueprint->GetModel(GraphPtr.Get());
		if (URigVMController* Controller = Blueprint->GetController(Model))
		{
			FName ArgumentName = TEXT("Argument");
			FString CPPType = TEXT("bool");
			FName CPPTypeObjectPath = NAME_None;
			FString DefaultValue = TEXT("False");
			// todo: base decisions on types on last argument

			Controller->AddExposedPin(ArgumentName, ERigVMPinDirection::Output, CPPType, CPPTypeObjectPath, DefaultValue, true);
		}
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
