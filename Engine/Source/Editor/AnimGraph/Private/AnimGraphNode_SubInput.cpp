// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_SubInput.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "GraphEditorSettings.h"
#include "BlueprintActionFilter.h"
#include "Widgets/Input/SButton.h"
#include "DetailCategoryBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"

#include "Animation/AnimBlueprint.h"
#include "IAnimationBlueprintEditor.h"
#include "AnimationGraphSchema.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "K2Node_CallFunction.h"
#include "Containers/Ticker.h"
#include "Widgets/Input/SCheckBox.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "SubInputNode"

UAnimGraphNode_SubInput::UAnimGraphNode_SubInput()
	: InputPoseIndex(INDEX_NONE)
{
}

void UAnimGraphNode_SubInput::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if(PropertyChangedEvent.Property)
	{
		if( PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimGraphNode_SubInput, Inputs) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimGraphNode_SubInput, Node.Name) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimBlueprintFunctionPinInfo, Name) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimBlueprintFunctionPinInfo, Type))
		{
			HandleInputPinArrayChanged();
			ReconstructNode();
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetAnimBlueprint());
			ReconstructNode();
		}
	}
}

FLinearColor UAnimGraphNode_SubInput::GetNodeTitleColor() const
{
	return GetDefault<UGraphEditorSettings>()->ResultNodeTitleColor;
}

FText UAnimGraphNode_SubInput::GetTooltipText() const
{
	return LOCTEXT("ToolTip", "Inputs to a sub-animation graph from a parent instance.");
}

FText UAnimGraphNode_SubInput::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	FText DefaultTitle = LOCTEXT("Title", "Input Pose");

	if(TitleType != ENodeTitleType::FullTitle)
	{
		return DefaultTitle;
	}
	else
	{
		if(Node.Name != NAME_None)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("NodeTitle"), DefaultTitle);
			Args.Add(TEXT("Name"), FText::FromName(Node.Name));
			return FText::Format(LOCTEXT("TitleListFormatTagged", "{NodeTitle}\n{Name}"), Args);
		}
		else
		{
			return DefaultTitle;
		}
	}
}

bool UAnimGraphNode_SubInput::CanUserDeleteNode() const
{
	// Only allow sub-inputs to be deleted if their parent graph is mutable
	// Also allow anim graphs to delete these nodes even theough they are 'read-only'
	return GetGraph()->bAllowDeletion || GetGraph()->GetFName() == UEdGraphSchema_K2::GN_AnimGraph;
}

bool UAnimGraphNode_SubInput::CanDuplicateNode() const
{
	return false;
}

template <typename Predicate>
static FName CreateUniqueName(const FName& InBaseName, Predicate IsUnique)
{
	FName CurrentName = InBaseName;
	int32 CurrentIndex = 0;

	while (!IsUnique(CurrentName))
	{
		FString PossibleName = InBaseName.ToString() + TEXT("_") + FString::FromInt(CurrentIndex++);
		CurrentName = FName(*PossibleName);
	}

	return CurrentName;
}

void UAnimGraphNode_SubInput::HandleInputPinArrayChanged()
{
	TArray<UAnimGraphNode_SubInput*> SubInputNodes;
	UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();

	for(UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if(Graph->Schema->IsChildOf(UAnimationGraphSchema::StaticClass()))
		{
			// Create a unique name for this new sub-input
			Graph->GetNodesOfClass(SubInputNodes);
		}
	}

	for(FAnimBlueprintFunctionPinInfo& Input : Inputs)
	{
		// New names are created empty, so assign a unique name
		if(Input.Name == NAME_None)
		{
			Input.Name = CreateUniqueName(TEXT("InputParam"), [&SubInputNodes](FName InName)
			{
				for(UAnimGraphNode_SubInput* SubInputNode : SubInputNodes)
				{
					for(const FAnimBlueprintFunctionPinInfo& Input : SubInputNode->Inputs)
					{
						if(Input.Name == InName)
						{
							return false;
						}
					}
				}
				return true;
			});

			if(Input.Type.PinCategory == NAME_None)
			{
				IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(AnimBlueprint, false);
				check(AssetEditor->GetEditorName() == "AnimationBlueprintEditor");
				IAnimationBlueprintEditor* AnimationBlueprintEditor = static_cast<IAnimationBlueprintEditor*>(AssetEditor);
				Input.Type = AnimationBlueprintEditor->GetLastGraphPinTypeUsed();
			}
		}
	}
	
	bool bIsInterface = AnimBlueprint->BlueprintType == BPTYPE_Interface;
	if(bIsInterface)
	{
		UAnimationGraphSchema::AutoArrangeInterfaceGraph(*GetGraph());
	}
}

void UAnimGraphNode_SubInput::AllocatePinsInternal()
{
	// use member reference if valid
	if (UFunction* Function = FunctionReference.ResolveMember<UFunction>(GetBlueprintClassFromNode()))
	{
		CreatePinsFromStubFunction(Function);
	}

	if(IsEditable())
	{
		// use user-defined pins
		CreateUserDefinedPins();
	}
}

void UAnimGraphNode_SubInput::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	AllocatePinsInternal();
}

void UAnimGraphNode_SubInput::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	Super::ReallocatePinsDuringReconstruction(OldPins);

	AllocatePinsInternal();
}

void UAnimGraphNode_SubInput::CreateUserDefinedPins()
{
	for(FAnimBlueprintFunctionPinInfo& PinInfo : Inputs)
	{
		UEdGraphPin* NewPin = CreatePin(EEdGraphPinDirection::EGPD_Output, PinInfo.Type, PinInfo.Name);
		NewPin->PinFriendlyName = FText::FromName(PinInfo.Name);
	}
}

void UAnimGraphNode_SubInput::CreatePinsFromStubFunction(const UFunction* Function)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	IterateFunctionParameters([this, K2Schema, Function](const FName& InName, const FEdGraphPinType& InPinType)
	{
		if(!UAnimationGraphSchema::IsPosePin(InPinType))
		{
			UEdGraphPin* Pin = CreatePin(EGPD_Output, InPinType, InName);
			K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
			
			UK2Node_CallFunction::GeneratePinTooltipFromFunction(*Pin, Function);
		}
	});
}

void UAnimGraphNode_SubInput::ConformInputPoseName()
{
	IterateFunctionParameters([this](const FName& InName, const FEdGraphPinType& InPinType)
	{
		if(UAnimationGraphSchema::IsPosePin(InPinType))
		{
			Node.Name = InName;
		}
	});
}

bool UAnimGraphNode_SubInput::ValidateAgainstFunctionReference() const
{
	bool bValid = false;

	IterateFunctionParameters([this, &bValid](const FName& InName, const FEdGraphPinType& InPinType)
	{
		bValid = true;
	});

	return bValid;
}

void UAnimGraphNode_SubInput::PostPlacedNewNode()
{
	if(IsEditable())
	{
		TArray<UAnimGraphNode_SubInput*> SubInputNodes;
		UAnimBlueprint* AnimBlueprint = CastChecked<UAnimBlueprint>(GetGraph()->GetOuter());
		for(UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
		{
			if(Graph->Schema->IsChildOf(UAnimationGraphSchema::StaticClass()))
			{
				// Create a unique name for this new sub-input
				Graph->GetNodesOfClass(SubInputNodes);
			}
		}

		Node.Name = CreateUniqueName(FAnimNode_SubInput::DefaultInputPoseName, [this, &SubInputNodes](const FName& InNameToCheck)
		{
			for(UAnimGraphNode_SubInput* SubInput : SubInputNodes)
			{
				if(SubInput != this && SubInput->Node.Name == InNameToCheck)
				{
					return false;
				}
			}

			return true;
		});

		FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_SubInput>(this)](float InDeltaTime)
		{
			if(UAnimGraphNode_SubInput* SubInputNode = WeakThis.Get())
			{
				// refresh the BP editor's details panel in case we are viewing the graph
				IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(SubInputNode->GetAnimBlueprint(), false);
				check(AssetEditor->GetEditorName() == "AnimationBlueprintEditor");
				IAnimationBlueprintEditor* AnimationBlueprintEditor = static_cast<IAnimationBlueprintEditor*>(AssetEditor);
				AnimationBlueprintEditor->RefreshInspector();
			}
			return false;
		}));
	}
}

class SSubInputNodeLabelWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSubInputNodeLabelWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<IPropertyHandle> InNamePropertyHandle, UAnimGraphNode_SubInput* InSubInputNode)
	{
		NamePropertyHandle = InNamePropertyHandle;
		WeakSubInputNode = InSubInputNode;

		ChildSlot
		[
			SAssignNew(NameTextBox, SEditableTextBox)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(this, &SSubInputNodeLabelWidget::HandleGetNameText)
			.OnTextChanged(this, &SSubInputNodeLabelWidget::HandleTextChanged)
			.OnTextCommitted(this, &SSubInputNodeLabelWidget::HandleTextCommitted)
		];
	}

	FText HandleGetNameText() const
	{
		return FText::FromName(WeakSubInputNode->Node.Name);
	}

	bool IsNameValid(const FString& InNewName, FText& OutReason)
	{
		if(InNewName.Len() == 0)
		{
			OutReason = LOCTEXT("ZeroSizeSubInputError", "A name must be specified.");
			return false;
		}
		else if(InNewName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			OutReason = LOCTEXT("SubInputInvalidName", "This name is invalid.");
			return false;
		}
		else
		{
			UAnimBlueprint* AnimBlueprint = CastChecked<UAnimBlueprint>(WeakSubInputNode->GetGraph()->GetOuter());
			for(UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
			{
				if(Graph->Schema->IsChildOf(UAnimationGraphSchema::StaticClass()))
				{
					TArray<UAnimGraphNode_SubInput*> SubInputNodes;
					Graph->GetNodesOfClass(SubInputNodes);

					for(UAnimGraphNode_SubInput* SubInput : SubInputNodes)
					{
						if(SubInput != WeakSubInputNode.Get() && SubInput->Node.Name.ToString().Equals(InNewName, ESearchCase::IgnoreCase))
						{
							OutReason = LOCTEXT("DuplicateSubInputError", "This input pose name already exists in this blueprint.");
							return false;
						}
					}
				}
			}

			return true;
		}
	}

	void HandleTextChanged(const FText& InNewText)
	{
		const FString NewTextAsString = InNewText.ToString();
	
		FText Reason;
		if(!IsNameValid(NewTextAsString, Reason))
		{
			NameTextBox->SetError(Reason);
		}
		else
		{
			NameTextBox->SetError(FText::GetEmpty());
		}
	}

	void HandleTextCommitted(const FText& InNewText, ETextCommit::Type InCommitType)
	{
		const FString NewTextAsString = InNewText.ToString();
		FText Reason;
		if(IsNameValid(NewTextAsString, Reason))
		{
			FName NewName = *InNewText.ToString();
			NamePropertyHandle->SetValue(NewName);
		}

		NameTextBox->SetError(FText::GetEmpty());
	}

	TSharedPtr<SEditableTextBox> NameTextBox;
	TSharedPtr<IPropertyHandle> NamePropertyHandle;
	TWeakObjectPtr<UAnimGraphNode_SubInput> WeakSubInputNode;
};

void UAnimGraphNode_SubInput::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& InputsCategoryBuilder = DetailBuilder.EditCategory("Inputs");

	TArray<TWeakObjectPtr<UObject>> OuterObjects;
	DetailBuilder.GetObjectsBeingCustomized(OuterObjects);
	if(OuterObjects.Num() != 1)
	{
		InputsCategoryBuilder.SetCategoryVisibility(false);
		return;
	}

	// skip if we cant edit this node as it is an interface graph
	UAnimGraphNode_SubInput* OuterNode = CastChecked<UAnimGraphNode_SubInput>(OuterObjects[0].Get());
	if(!OuterNode->CanUserDeleteNode())
	{
		FText ReadOnlyWarning = LOCTEXT("ReadOnlyWarning", "This input pose is read-only and cannot be edited");

		InputsCategoryBuilder.SetCategoryVisibility(false);

		IDetailCategoryBuilder& WarningCategoryBuilder = DetailBuilder.EditCategory("InputPose", LOCTEXT("InputPoseCategory", "Input Pose"));
		WarningCategoryBuilder.AddCustomRow(ReadOnlyWarning)
			.WholeRowContent()
			[
				SNew(STextBlock)
				.Text(ReadOnlyWarning)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];

		return;
	}

	TSharedPtr<IPropertyHandle> NamePropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAnimGraphNode_SubInput, Node.Name), GetClass());
	InputsCategoryBuilder.AddProperty(NamePropertyHandle)
	.OverrideResetToDefault(FResetToDefaultOverride::Hide())
	.CustomWidget()
	.NameContent()
	[
		NamePropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		MakeNameWidget(DetailBuilder)
	];

	InputsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UAnimGraphNode_SubInput, Inputs), GetClass())
		.ShouldAutoExpand(true);
}

TSharedRef<SWidget> UAnimGraphNode_SubInput::MakeNameWidget(IDetailLayoutBuilder& DetailBuilder)
{
	TSharedPtr<IPropertyHandle> NamePropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAnimGraphNode_SubInput, Node.Name), GetClass());
	return SNew(SSubInputNodeLabelWidget, NamePropertyHandle, this);
}

bool UAnimGraphNode_SubInput::HasExternalDependencies(TArray<UStruct*>* OptionalOutput) const
{
	const UBlueprint* SourceBlueprint = GetBlueprint();

	UClass* SourceClass = FunctionReference.GetMemberParentClass(GetBlueprintClassFromNode());
	bool bResult = (SourceClass != nullptr) && (SourceClass->ClassGeneratedBy != SourceBlueprint);
	if (bResult && OptionalOutput)
	{
		OptionalOutput->AddUnique(SourceClass);
	}

	const bool bSuperResult = Super::HasExternalDependencies(OptionalOutput);
	return bSuperResult || bResult;
}

int32 UAnimGraphNode_SubInput::GetNumInputs() const
{
	if (UFunction* Function = FunctionReference.ResolveMember<UFunction>(GetBlueprintClassFromNode()))
	{
		// Count the inputs from parameters.
		int32 NumParameters = 0;

		IterateFunctionParameters([&NumParameters](const FName& InName, const FEdGraphPinType& InPinType)
		{
			if(!UAnimationGraphSchema::IsPosePin(InPinType))
			{
				NumParameters++;
			}
		});

		return NumParameters;
	}
	else
	{
		return Inputs.Num();
	}
}

void UAnimGraphNode_SubInput::PromoteFromInterfaceOverride()
{
	if (UFunction* Function = FunctionReference.ResolveMember<UFunction>(GetBlueprintClassFromNode()))
	{
		IterateFunctionParameters([this](const FName& InName, const FEdGraphPinType& InPinType)
		{
			if(!UAnimationGraphSchema::IsPosePin(InPinType))
			{
				Inputs.Emplace(InName, InPinType);	
			}
		});

		// Remove the signature class now, that is not relevant.
		FunctionReference.SetSelfMember(FunctionReference.GetMemberName());
		InputPoseIndex = INDEX_NONE;
	}
}

void UAnimGraphNode_SubInput::IterateFunctionParameters(TFunctionRef<void(const FName&, const FEdGraphPinType&)> InFunc) const
{
	if (UFunction* Function = FunctionReference.ResolveMember<UFunction>(GetBlueprintClassFromNode()))
	{
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

		// if the generated class is not up to date, use the skeleton's class function to create pins:
		Function = FBlueprintEditorUtils::GetMostUpToDateFunction(Function);

		// We need to find all parameters AFTER the pose we are representing
		int32 CurrentPoseIndex = 0;
		UProperty* PoseParam = nullptr;
		for (TFieldIterator<UProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
		{
			UProperty* Param = *PropIt;

			const bool bIsFunctionInput = !Param->HasAnyPropertyFlags(CPF_OutParm) || Param->HasAnyPropertyFlags(CPF_ReferenceParm);

			if (bIsFunctionInput)
			{
				FEdGraphPinType PinType;
				if(K2Schema->ConvertPropertyToPinType(Param, PinType))
				{
					if(PoseParam == nullptr)
					{
						if(UAnimationGraphSchema::IsPosePin(PinType))
						{
							if(CurrentPoseIndex == InputPoseIndex)
							{
								PoseParam = Param;
								InFunc(Param->GetFName(), PinType);
							}
							CurrentPoseIndex++;
						}
					}
					else
					{
						if(UAnimationGraphSchema::IsPosePin(PinType))
						{
							// Found next pose param, so exit
							break;
						}
						else
						{
							InFunc(Param->GetFName(), PinType);
						}
					}
				}
			}
		}
	}
	else
	{
		// First call pose
		InFunc(Node.Name, UAnimationGraphSchema::MakeLocalSpacePosePin());

		// Then each input
		for(const FAnimBlueprintFunctionPinInfo& PinInfo : Inputs)
		{
			InFunc(PinInfo.Name, PinInfo.Type);
		}
	}
}

#undef LOCTEXT_NAMESPACE
