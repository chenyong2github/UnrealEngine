// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_SubInstanceBase.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Animation/AnimNode_SubInput.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "AnimationGraphSchema.h"
#include "Widgets/Layout/SBox.h"
#include "UObject/CoreRedirects.h"
#include "Animation/AnimNode_SubInstance.h"

#define LOCTEXT_NAMESPACE "SubInstanceNode"

namespace SubInstanceGraphNodeConstants
{
	FLinearColor TitleColor(0.2f, 0.2f, 0.8f);
}

FLinearColor UAnimGraphNode_SubInstanceBase::GetNodeTitleColor() const
{
	return SubInstanceGraphNodeConstants::TitleColor;
}

FText UAnimGraphNode_SubInstanceBase::GetTooltipText() const
{
	return LOCTEXT("ToolTip", "Runs a sub-anim instance to process animation");
}

FText UAnimGraphNode_SubInstanceBase::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	UClass* TargetClass = GetTargetClass();
	UAnimBlueprint* TargetAnimBlueprint = TargetClass ? CastChecked<UAnimBlueprint>(TargetClass->ClassGeneratedBy) : nullptr;

	const FAnimNode_SubInstance& Node = *GetSubInstanceNode();

	FFormatNamedArguments Args;
	Args.Add(TEXT("NodeTitle"), LOCTEXT("Title", "Sub Anim Instance"));
	Args.Add(TEXT("TargetClass"), TargetAnimBlueprint ? FText::FromString(TargetAnimBlueprint->GetName()) : LOCTEXT("ClassNone", "None"));

	if(TitleType == ENodeTitleType::MenuTitle)
	{
		return LOCTEXT("NodeTitle", "Sub Anim Instance");
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

void UAnimGraphNode_SubInstanceBase::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	UAnimBlueprint* AnimBP = CastChecked<UAnimBlueprint>(GetBlueprint());

	UObject* OriginalNode = MessageLog.FindSourceObject(this);

	if(HasInstanceLoop())
	{
		MessageLog.Error(TEXT("Detected loop in sub instance chain starting at @@ inside class @@"), this, AnimBP->GetAnimBlueprintGeneratedClass());
	}

	// Check for cycles from other sub instance nodes
	TArray<UEdGraph*> Graphs;
	AnimBP->GetAllGraphs(Graphs);

	const FAnimNode_SubInstance& Node = *GetSubInstanceNode();

	// Check for duplicate tags in this anim blueprint
	for(UEdGraph* Graph : Graphs)
	{
		TArray<UAnimGraphNode_SubInstanceBase*> SubInstanceNodes;
		Graph->GetNodesOfClass(SubInstanceNodes);

		for(UAnimGraphNode_SubInstanceBase* SubInstanceNode : SubInstanceNodes)
		{
			if(SubInstanceNode == OriginalNode)
			{
				continue;
			}

			FAnimNode_SubInstance& InnerNode = *SubInstanceNode->GetSubInstanceNode();

			if(InnerNode.Tag != NAME_None && InnerNode.Tag == Node.Tag)
			{
				MessageLog.Error(*FText::Format(LOCTEXT("DuplicateTagErrorFormat", "Node @@ and node @@ both have the same tag '{0}'."), FText::FromName(Node.Tag)).ToString(), this, SubInstanceNode);
			}
		}
	}

	// Check we don't try to spawn our own blueprint
	if(GetTargetClass() == AnimBP->GetAnimBlueprintGeneratedClass())
	{
		MessageLog.Error(TEXT("Sub instance node @@ targets instance class @@ which it is inside, this would cause a loop."), this, AnimBP->GetAnimBlueprintGeneratedClass());
	}
}

void UAnimGraphNode_SubInstanceBase::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
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

	const FAnimNode_SubInstance& Node = *GetSubInstanceNode();

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

void UAnimGraphNode_SubInstanceBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	bool bRequiresNodeReconstruct = false;
	UProperty* ChangedProperty = PropertyChangedEvent.Property;

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

bool UAnimGraphNode_SubInstanceBase::HasInstanceLoop()
{
	TArray<FGuid> VisitedList;
	TArray<FGuid> CurrentStack;
	return HasInstanceLoop_Recursive(this, VisitedList, CurrentStack);
}

bool UAnimGraphNode_SubInstanceBase::HasInstanceLoop_Recursive(UAnimGraphNode_SubInstanceBase* CurrNode, TArray<FGuid>& VisitedNodes, TArray<FGuid>& NodeStack)
{
	if(!VisitedNodes.Contains(CurrNode->NodeGuid))
	{
		VisitedNodes.Add(CurrNode->NodeGuid);
		NodeStack.Add(CurrNode->NodeGuid);

		if(UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UBlueprint::GetBlueprintFromClass(CurrNode->GetTargetClass())))
		{
			// Check for cycles from other sub instance nodes
			TArray<UEdGraph*> Graphs;
			AnimBP->GetAllGraphs(Graphs);

			for(UEdGraph* Graph : Graphs)
			{
				TArray<UAnimGraphNode_SubInstanceBase*> SubInstanceNodes;
				Graph->GetNodesOfClass(SubInstanceNodes);

				for(UAnimGraphNode_SubInstanceBase* SubInstanceNode : SubInstanceNodes)
				{
					// If we haven't visited this node, then check it for loops, otherwise if we're pointing to a previously visited node that is in the current instance stack we have a loop
					if((!VisitedNodes.Contains(SubInstanceNode->NodeGuid) && HasInstanceLoop_Recursive(SubInstanceNode, VisitedNodes, NodeStack)) || NodeStack.Contains(SubInstanceNode->NodeGuid))
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

void UAnimGraphNode_SubInstanceBase::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
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
			.ObjectPath_UObject(this, &UAnimGraphNode_SubInstanceBase::GetCurrentInstanceBlueprintPath)
			.AllowedClass(UAnimBlueprint::StaticClass())
			.NewAssetFactories(TArray<UFactory*>())
			.OnShouldFilterAsset(FOnShouldFilterAsset::CreateUObject(this, &UAnimGraphNode_SubInstanceBase::OnShouldFilterInstanceBlueprint))
			.OnObjectChanged(FOnSetObject::CreateUObject(this, &UAnimGraphNode_SubInstanceBase::OnSetInstanceBlueprint, &DetailBuilder))
		];
	}
}

void UAnimGraphNode_SubInstanceBase::GenerateExposedPinsDetails(IDetailLayoutBuilder &DetailBuilder)
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

	TArray<UProperty*> ExposableProperties;
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

		for(UProperty* Property : ExposableProperties)
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

bool UAnimGraphNode_SubInstanceBase::IsStructuralProperty(UProperty* InProperty) const
{
	return InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_SubInstance, InstanceClass);
}

FString UAnimGraphNode_SubInstanceBase::GetCurrentInstanceBlueprintPath() const
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

bool UAnimGraphNode_SubInstanceBase::OnShouldFilterInstanceBlueprint(const FAssetData& AssetData) const
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

void UAnimGraphNode_SubInstanceBase::OnSetInstanceBlueprint(const FAssetData& AssetData, IDetailLayoutBuilder* InDetailBuilder)
{
	FScopedTransaction Transaction(LOCTEXT("SetInstanceBlueprint", "Set Instance Blueprint"));

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

FPoseLinkMappingRecord UAnimGraphNode_SubInstanceBase::GetLinkIDLocation(const UScriptStruct* NodeType, UEdGraphPin* SourcePin)
{
	FPoseLinkMappingRecord Record = Super::GetLinkIDLocation(NodeType, SourcePin);
	if(Record.IsValid())
	{
		return Record;	
	}
	else if(SourcePin->LinkedTo.Num() > 0 && SourcePin->Direction == EGPD_Input)
	{
		const FAnimNode_SubInstance& Node = *GetSubInstanceNode();

		check(Node.InputPoses.Num() == Node.InputPoseNames.Num());

		// perform name-based logic for input pose pins
		if (UAnimGraphNode_Base* LinkedNode = Cast<UAnimGraphNode_Base>(FBlueprintEditorUtils::FindFirstCompilerRelevantNode(SourcePin->LinkedTo[0])))
		{
			UArrayProperty* ArrayProperty = FindFieldChecked<UArrayProperty>(NodeType, GET_MEMBER_NAME_CHECKED(FAnimNode_SubInstance, InputPoses));
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
