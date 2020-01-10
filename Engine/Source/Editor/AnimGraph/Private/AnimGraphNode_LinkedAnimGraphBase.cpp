// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_LinkedAnimGraphBase.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Animation/AnimNode_LinkedInputPose.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "AnimationGraphSchema.h"
#include "Widgets/Layout/SBox.h"
#include "UObject/CoreRedirects.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"

#define LOCTEXT_NAMESPACE "LinkedAnimGraph"

namespace LinkedAnimGraphGraphNodeConstants
{
	FLinearColor TitleColor(0.2f, 0.2f, 0.8f);
}

FLinearColor UAnimGraphNode_LinkedAnimGraphBase::GetNodeTitleColor() const
{
	return LinkedAnimGraphGraphNodeConstants::TitleColor;
}

FText UAnimGraphNode_LinkedAnimGraphBase::GetTooltipText() const
{
	return LOCTEXT("ToolTip", "Runs a linked anim graph in another instance to process animation");
}

FText UAnimGraphNode_LinkedAnimGraphBase::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	UClass* TargetClass = GetTargetClass();
	UAnimBlueprint* TargetAnimBlueprint = TargetClass ? CastChecked<UAnimBlueprint>(TargetClass->ClassGeneratedBy) : nullptr;

	const FAnimNode_LinkedAnimGraph& Node = *GetLinkedAnimGraphNode();

	FFormatNamedArguments Args;
	Args.Add(TEXT("NodeTitle"), LOCTEXT("Title", "Linked Anim Graph"));
	Args.Add(TEXT("TargetClass"), TargetAnimBlueprint ? FText::FromString(TargetAnimBlueprint->GetName()) : LOCTEXT("ClassNone", "None"));

	if(TitleType == ENodeTitleType::MenuTitle)
	{
		return LOCTEXT("NodeTitle", "Linked Anim Graph");
	}
	if(TitleType == ENodeTitleType::ListView)
	{
		if(Node.Tag != NAME_None)
		{
			Args.Add(TEXT("Tag"), FText::FromName(Node.Tag));
			return FText::Format(LOCTEXT("TitleListFormatTagged", "{NodeTitle} ({Tag}) - {TargetClass}"), Args);
		}
		else
		{
			return FText::Format(LOCTEXT("TitleListFormat", "{NodeTitle} - {TargetClass}"), Args);
		}
	}
	else
	{

		if(Node.Tag != NAME_None)
		{
			Args.Add(TEXT("Tag"), FText::FromName(Node.Tag));
			return FText::Format(LOCTEXT("TitleFormatTagged", "{NodeTitle} ({Tag})\n{TargetClass}"), Args);
		}
		else
		{
			return FText::Format(LOCTEXT("TitleFormat", "{NodeTitle}\n{TargetClass}"), Args);
		}
	}
}

void UAnimGraphNode_LinkedAnimGraphBase::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	UAnimBlueprint* AnimBP = CastChecked<UAnimBlueprint>(GetBlueprint());

	UObject* OriginalNode = MessageLog.FindSourceObject(this);

	if(HasInstanceLoop())
	{
		MessageLog.Error(TEXT("Detected loop in linked instance chain starting at @@ inside class @@"), this, AnimBP->GetAnimBlueprintGeneratedClass());
	}

	// Check for cycles from other linked instance nodes
	TArray<UEdGraph*> Graphs;
	AnimBP->GetAllGraphs(Graphs);

	const FAnimNode_LinkedAnimGraph& Node = *GetLinkedAnimGraphNode();

	// Check for duplicate tags in this anim blueprint
	for(UEdGraph* Graph : Graphs)
	{
		TArray<UAnimGraphNode_LinkedAnimGraphBase*> LinkedAnimGraphNodes;
		Graph->GetNodesOfClass(LinkedAnimGraphNodes);

		for(UAnimGraphNode_LinkedAnimGraphBase* LinkedAnimGraphNode : LinkedAnimGraphNodes)
		{
			if(LinkedAnimGraphNode == OriginalNode)
			{
				continue;
			}

			FAnimNode_LinkedAnimGraph& InnerNode = *LinkedAnimGraphNode->GetLinkedAnimGraphNode();

			if(InnerNode.Tag != NAME_None && InnerNode.Tag == Node.Tag)
			{
				MessageLog.Error(*FText::Format(LOCTEXT("DuplicateTagErrorFormat", "Node @@ and node @@ both have the same tag '{0}'."), FText::FromName(Node.Tag)).ToString(), this, LinkedAnimGraphNode);
			}
		}
	}

	// Check we don't try to spawn our own blueprint
	if(GetTargetClass() == AnimBP->GetAnimBlueprintGeneratedClass())
	{
		MessageLog.Error(TEXT("Linked instance node @@ targets instance class @@ which it is inside, this would cause a loop."), this, AnimBP->GetAnimBlueprintGeneratedClass());
	}
}

void UAnimGraphNode_LinkedAnimGraphBase::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	// Grab the SKELETON class here as when we are reconstructed during during BP compilation
	// the full generated class is not yet present built.
	UClass* TargetClass = GetTargetSkeletonClass();

	if(!TargetClass)
	{
		// Nothing to search for properties
		return;
	}

	IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(TargetClass);

	const FAnimNode_LinkedAnimGraph& Node = *GetLinkedAnimGraphNode();

	// Add any pose pins
	for(const FAnimBlueprintFunction& AnimBlueprintFunction : AnimClassInterface->GetAnimBlueprintFunctions())
	{
		if(AnimBlueprintFunction.Name == Node.GetDynamicLinkFunctionName())
		{
			for(const FName& PoseName : AnimBlueprintFunction.InputPoseNames)
			{
				UEdGraphPin* NewPin = CreatePin(EEdGraphPinDirection::EGPD_Input, UAnimationGraphSchema::MakeLocalSpacePosePin(), PoseName);
				NewPin->PinFriendlyName = FText::FromName(PoseName);
				CustomizePinData(NewPin, PoseName, INDEX_NONE);
			}

			break;
		}
	}

	// Call super to add properties
	Super::ReallocatePinsDuringReconstruction(OldPins);
}

void UAnimGraphNode_LinkedAnimGraphBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	bool bRequiresNodeReconstruct = false;
	FProperty* ChangedProperty = PropertyChangedEvent.Property;

	if(ChangedProperty)
	{
		if (IsStructuralProperty(ChangedProperty))
		{
			bRequiresNodeReconstruct = true;
			RebuildExposedProperties();
		}
	}

	if(bRequiresNodeReconstruct)
	{
		ReconstructNode();
	}
}

bool UAnimGraphNode_LinkedAnimGraphBase::HasInstanceLoop()
{
	TArray<FGuid> VisitedList;
	TArray<FGuid> CurrentStack;
	return HasInstanceLoop_Recursive(this, VisitedList, CurrentStack);
}

bool UAnimGraphNode_LinkedAnimGraphBase::HasInstanceLoop_Recursive(UAnimGraphNode_LinkedAnimGraphBase* CurrNode, TArray<FGuid>& VisitedNodes, TArray<FGuid>& NodeStack)
{
	if(!VisitedNodes.Contains(CurrNode->NodeGuid))
	{
		VisitedNodes.Add(CurrNode->NodeGuid);
		NodeStack.Add(CurrNode->NodeGuid);

		if(UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UBlueprint::GetBlueprintFromClass(CurrNode->GetTargetClass())))
		{
			// Check for cycles from other linked instance nodes
			TArray<UEdGraph*> Graphs;
			AnimBP->GetAllGraphs(Graphs);

			for(UEdGraph* Graph : Graphs)
			{
				TArray<UAnimGraphNode_LinkedAnimGraphBase*> LinkedInstanceNodes;
				Graph->GetNodesOfClass(LinkedInstanceNodes);

				for(UAnimGraphNode_LinkedAnimGraphBase* LinkedInstanceNode : LinkedInstanceNodes)
				{
					// If we haven't visited this node, then check it for loops, otherwise if we're pointing to a previously visited node that is in the current instance stack we have a loop
					if((!VisitedNodes.Contains(LinkedInstanceNode->NodeGuid) && HasInstanceLoop_Recursive(LinkedInstanceNode, VisitedNodes, NodeStack)) || NodeStack.Contains(LinkedInstanceNode->NodeGuid))
					{
						return true;
					}
				}
			}
		}
	}

	NodeStack.Remove(CurrNode->NodeGuid);
	return false;
}

void UAnimGraphNode_LinkedAnimGraphBase::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Super::CustomizeDetails(DetailBuilder);

	GenerateExposedPinsDetails(DetailBuilder);

	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(FName(TEXT("Settings")));

	// Customize InstanceClass
	{
		TSharedRef<IPropertyHandle> ClassHandle = DetailBuilder.GetProperty(TEXT("Node.InstanceClass"), GetClass());
		ClassHandle->MarkHiddenByCustomization();

		FDetailWidgetRow& ClassWidgetRow = CategoryBuilder.AddCustomRow(LOCTEXT("FilterStringInstanceClass", "Instance Class"));
		ClassWidgetRow.NameContent()
		[
			ClassHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			SNew(SObjectPropertyEntryBox)
			.ObjectPath_UObject(this, &UAnimGraphNode_LinkedAnimGraphBase::GetCurrentInstanceBlueprintPath)
			.AllowedClass(UAnimBlueprint::StaticClass())
			.NewAssetFactories(TArray<UFactory*>())
			.OnShouldFilterAsset(FOnShouldFilterAsset::CreateUObject(this, &UAnimGraphNode_LinkedAnimGraphBase::OnShouldFilterInstanceBlueprint))
			.OnObjectChanged(FOnSetObject::CreateUObject(this, &UAnimGraphNode_LinkedAnimGraphBase::OnSetInstanceBlueprint, &DetailBuilder))
		];
	}
}

void UAnimGraphNode_LinkedAnimGraphBase::GenerateExposedPinsDetails(IDetailLayoutBuilder &DetailBuilder)
{
	// We dont allow multi-select here
	if(DetailBuilder.GetSelectedObjects().Num() > 1)
	{
		DetailBuilder.HideCategory(TEXT("Settings"));
		return;
	}

	// We dont allow multi-select here
	if(DetailBuilder.GetSelectedObjects().Num() > 1)
	{
		DetailBuilder.HideCategory(TEXT("Settings"));
		return;
	}

	TArray<FProperty*> ExposableProperties;
	GetExposableProperties(ExposableProperties);

	if(ExposableProperties.Num() > 0)
	{
		IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(FName(TEXT("Exposable Properties")));

		FDetailWidgetRow& HeaderWidgetRow = CategoryBuilder.AddCustomRow(LOCTEXT("ExposeAll", "Expose All"));
		
		HeaderWidgetRow.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PropertyName", "Name"))
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
		];

		HeaderWidgetRow.ValueContent()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ExposeAllPropertyValue", "Expose All"))
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
			]
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_UObject(this, &UAnimGraphNode_CustomProperty::AreAllPropertiesExposed)
				.OnCheckStateChanged_UObject(this, &UAnimGraphNode_CustomProperty::OnPropertyExposeAllCheckboxChanged)
			]
		];

		for(FProperty* Property : ExposableProperties)
		{
			FDetailWidgetRow& PropertyWidgetRow = CategoryBuilder.AddCustomRow(FText::FromString(Property->GetName()));

			FName PropertyName = Property->GetFName();
			FText PropertyTypeText = GetPropertyTypeText(Property);

			FFormatNamedArguments Args;
			Args.Add(TEXT("PropertyName"), FText::FromName(PropertyName));
			Args.Add(TEXT("PropertyType"), PropertyTypeText);

			FText TooltipText = FText::Format(LOCTEXT("PropertyTooltipText", "{PropertyName}\nType: {PropertyType}"), Args);

			PropertyWidgetRow.NameContent()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Property->GetName()))
				.ToolTipText(TooltipText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];

			PropertyWidgetRow.ValueContent()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ExposePropertyValue", "Expose:"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_UObject(this, &UAnimGraphNode_CustomProperty::IsPropertyExposed, PropertyName)
					.OnCheckStateChanged_UObject(this, &UAnimGraphNode_CustomProperty::OnPropertyExposeCheckboxChanged, PropertyName)
				]
			];
		}
	}
}

bool UAnimGraphNode_LinkedAnimGraphBase::IsStructuralProperty(FProperty* InProperty) const
{
	return InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_LinkedAnimGraph, InstanceClass);
}

FString UAnimGraphNode_LinkedAnimGraphBase::GetCurrentInstanceBlueprintPath() const
{
	UClass* InstanceClass = GetTargetClass();

	if(InstanceClass)
	{
		UBlueprint* ActualBlueprint = UBlueprint::GetBlueprintFromClass(InstanceClass);

		if(ActualBlueprint)
		{
			return ActualBlueprint->GetPathName();
		}
	}

	return FString();
}

bool UAnimGraphNode_LinkedAnimGraphBase::OnShouldFilterInstanceBlueprint(const FAssetData& AssetData) const
{
	// Check recursion
	if(AssetData.IsAssetLoaded() && Cast<UBlueprint>(AssetData.GetAsset()) == GetBlueprint())
	{
		return true;
	}

	// Check skeleton
	FAssetDataTagMapSharedView::FFindTagResult Result = AssetData.TagsAndValues.FindTag("TargetSkeleton");
	if (Result.IsSet())
	{
		if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
		{
			FString TargetSkeletonName = FString::Printf(TEXT("%s'%s'"), *CurrentBlueprint->TargetSkeleton->GetClass()->GetName(), *CurrentBlueprint->TargetSkeleton->GetPathName());
			if(Result.GetValue() != TargetSkeletonName)
			{
				return true;
			}
		}
	}

	return false;
}

void UAnimGraphNode_LinkedAnimGraphBase::OnSetInstanceBlueprint(const FAssetData& AssetData, IDetailLayoutBuilder* InDetailBuilder)
{
	FScopedTransaction Transaction(LOCTEXT("SetInstanceBlueprint", "Set Linked Blueprint"));

	Modify();
	
	TSharedRef<IPropertyHandle> ClassHandle = InDetailBuilder->GetProperty(TEXT("Node.InstanceClass"), GetClass());
	if(UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(AssetData.GetAsset()))
	{
		ClassHandle->SetValue(Blueprint->GetAnimBlueprintGeneratedClass());
	}
	else
	{
		ClassHandle->SetValue((UObject*)nullptr);
	}
}

FPoseLinkMappingRecord UAnimGraphNode_LinkedAnimGraphBase::GetLinkIDLocation(const UScriptStruct* NodeType, UEdGraphPin* SourcePin)
{
	FPoseLinkMappingRecord Record = Super::GetLinkIDLocation(NodeType, SourcePin);
	if(Record.IsValid())
	{
		return Record;	
	}
	else if(SourcePin->LinkedTo.Num() > 0 && SourcePin->Direction == EGPD_Input)
	{
		const FAnimNode_LinkedAnimGraph& Node = *GetLinkedAnimGraphNode();

		check(Node.InputPoses.Num() == Node.InputPoseNames.Num());

		// perform name-based logic for input pose pins
		if (UAnimGraphNode_Base* LinkedNode = Cast<UAnimGraphNode_Base>(FBlueprintEditorUtils::FindFirstCompilerRelevantNode(SourcePin->LinkedTo[0])))
		{
			FArrayProperty* ArrayProperty = FindFieldChecked<FArrayProperty>(NodeType, GET_MEMBER_NAME_CHECKED(FAnimNode_LinkedAnimGraph, InputPoses));
			int32 ArrayIndex = INDEX_NONE;
			if(Node.InputPoseNames.Find(SourcePin->GetFName(), ArrayIndex))
			{
				check(Node.InputPoses.IsValidIndex(ArrayIndex));
				return FPoseLinkMappingRecord::MakeFromArrayEntry(this, LinkedNode, ArrayProperty, ArrayIndex);
			}
		}
	}

	return FPoseLinkMappingRecord::MakeInvalid();
}

#undef LOCTEXT_NAMESPACE
