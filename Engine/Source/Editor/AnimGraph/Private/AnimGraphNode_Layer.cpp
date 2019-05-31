// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_Layer.h"
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
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "LayerNode"

FText UAnimGraphNode_Layer::GetTooltipText() const
{
	return LOCTEXT("ToolTip", "Runs another graph to process animation");
}

FText UAnimGraphNode_Layer::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	UClass* TargetClass = *Node.Interface;
	UAnimBlueprint* TargetAnimBlueprint = TargetClass ? CastChecked<UAnimBlueprint>(TargetClass->ClassGeneratedBy) : nullptr;

	FFormatNamedArguments Args;
	Args.Add(TEXT("NodeTitle"), LOCTEXT("Title", "Layer"));
	Args.Add(TEXT("TargetClass"), TargetAnimBlueprint ? FText::FromString(TargetAnimBlueprint->GetName()) : LOCTEXT("ClassSelf", "Self"));

	if(TitleType == ENodeTitleType::MenuTitle)
	{
		return LOCTEXT("NodeTitle", "Layer");
	}
	if(TitleType == ENodeTitleType::ListView)
	{
		Args.Add(TEXT("Layer"), Node.Layer == NAME_None ? LOCTEXT("LayerNone", "None") : FText::FromName(Node.Layer));
		return FText::Format(LOCTEXT("TitleListFormatOutputPose", "{NodeTitle}: {Layer} - {TargetClass}"), Args);
	}
	else
	{
		Args.Add(TEXT("Layer"), Node.Layer == NAME_None ? LOCTEXT("LayerNone", "None") : FText::FromName(Node.Layer));
		return FText::Format(LOCTEXT("TitleFormatOutputPose", "{NodeTitle}: {Layer}\n{TargetClass}"), Args);
	}
}

void UAnimGraphNode_Layer::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	if(Node.Layer == NAME_None)
	{
		MessageLog.Error(*LOCTEXT("NoLayerError", "Layer node @@ does not specify a layer.").ToString(), this);
	}
	else
	{
		UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint());

		// check layer actually exists in the interface
		UClass* TargetClass = *Node.Interface;
		if(TargetClass == nullptr)
		{
			// If no interface specified, use this class
			if (CurrentBlueprint)
			{
				TargetClass = *CurrentBlueprint->SkeletonGeneratedClass;
			}
		}

		if(TargetClass)
		{
			bool bFoundFunction = false;
			IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(TargetClass);
			for(const FAnimBlueprintFunction& AnimBlueprintFunction : AnimClassInterface->GetAnimBlueprintFunctions())
			{
				if(AnimBlueprintFunction.Name == Node.Layer)
				{
					bFoundFunction = true;
				}
			}

			if(!bFoundFunction)
			{
				MessageLog.Error(*FText::Format(LOCTEXT("MissingLayerError", "Layer node @@ uses invalid layer '{0}'."), FText::FromName(Node.Layer)).ToString(), this);
			}
		}

		if(CurrentBlueprint)
		{
			UAnimGraphNode_Layer* OriginalThis = Cast<UAnimGraphNode_Layer>(MessageLog.FindSourceObject(this));

			// check layer is only used once in this blueprint
			auto CheckGraph = [this, OriginalThis, &MessageLog](const UEdGraph* InGraph)
			{
				TArray<UAnimGraphNode_Layer*> LayerNodes;
				InGraph->GetNodesOfClass(LayerNodes);
				for(const UAnimGraphNode_Layer* LayerNode : LayerNodes)
				{
					if(LayerNode != OriginalThis)
					{
						if(LayerNode->Node.Layer == Node.Layer)
						{
							MessageLog.Error(*FText::Format(LOCTEXT("DuplicateLayerError", "Layer node @@ also uses layer '{0}', layers can be used only once in an animation blueprint."), FText::FromName(Node.Layer)).ToString(), this);
						}
					}
				}
			};
		
			TArray<UEdGraph*> Graphs;
			CurrentBlueprint->GetAllGraphs(Graphs);

			for(const UEdGraph* Graph : Graphs)
			{
				CheckGraph(Graph);
			}
		}
	}
}

UObject* UAnimGraphNode_Layer::GetJumpTargetForDoubleClick() const
{
	auto JumpTargetFromClass = [this](UClass* InClass)
	{
		UObject* JumpTargetObject = nullptr;

		UAnimBlueprint* TargetAnimBlueprint = InClass ? CastChecked<UAnimBlueprint>(InClass->ClassGeneratedBy) : nullptr;
		if(TargetAnimBlueprint == nullptr || TargetAnimBlueprint == Cast<UAnimBlueprint>(GetBlueprint()))
		{
			// jump to graph in self
			TArray<UEdGraph*> Graphs;
			GetBlueprint()->GetAllGraphs(Graphs);

			UEdGraph** FoundGraph = Graphs.FindByPredicate([this](UEdGraph* InGraph){ return InGraph->GetFName() == Node.Layer; });
			if(FoundGraph)
			{
				JumpTargetObject = *FoundGraph;
			}
		}
		else if(TargetAnimBlueprint)
		{
			// jump to graph in other BP
			TArray<UEdGraph*> Graphs;
			TargetAnimBlueprint->GetAllGraphs(Graphs);

			UEdGraph** FoundGraph = Graphs.FindByPredicate([this](UEdGraph* InGraph){ return InGraph->GetFName() == Node.Layer; });
			if(FoundGraph)
			{
				JumpTargetObject = *FoundGraph;
			}
			else
			{
				JumpTargetObject = TargetAnimBlueprint;
			}
		}

		return JumpTargetObject;
	};

	// First try a concrete class, if any
	UObject* JumpTargetObject = JumpTargetFromClass(*Node.InstanceClass);
	if(JumpTargetObject == nullptr)
	{
		// then try the interface
		JumpTargetObject = JumpTargetFromClass(*Node.Interface);
	}

	return JumpTargetObject;
}

void UAnimGraphNode_Layer::JumpToDefinition() const
{
	if (UEdGraph* HyperlinkTarget = Cast<UEdGraph>(GetJumpTargetForDoubleClick()))
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(HyperlinkTarget);
	}
	else
	{
		Super::JumpToDefinition();
	}
}

bool UAnimGraphNode_Layer::HasExternalDependencies(TArray<class UStruct*>* OptionalOutput /*= NULL*/) const
{
	UClass* InterfaceClassToUse = *Node.Interface;

	// Add our interface class. If that changes we need a recompile
	if(InterfaceClassToUse && OptionalOutput)
	{
		OptionalOutput->AddUnique(InterfaceClassToUse);
	}

	bool bSuperResult = Super::HasExternalDependencies(OptionalOutput);
	return InterfaceClassToUse || bSuperResult;
}

void UAnimGraphNode_Layer::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// We dont allow multi-select here
	if(DetailBuilder.GetSelectedObjects().Num() > 1)
	{
		DetailBuilder.HideCategory(TEXT("Settings"));
		return;
	}

	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(TEXT("Settings"));

	// Hide Tag
	TSharedRef<IPropertyHandle> TagHandle = DetailBuilder.GetProperty(TEXT("Node.Tag"), GetClass());
	TagHandle->MarkHiddenByCustomization();

	// Customize Interface
	{
		TSharedRef<IPropertyHandle> InterfaceHandle = DetailBuilder.GetProperty(TEXT("Node.Interface"), GetClass());
		if(InterfaceHandle->IsValidHandle())
		{
			InterfaceHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateUObject(this, &UAnimGraphNode_Layer::OnStructuralPropertyChanged, &DetailBuilder));
		}

		InterfaceHandle->MarkHiddenByCustomization();

		FDetailWidgetRow& InterfaceWidgetRow = CategoryBuilder.AddCustomRow(LOCTEXT("FilterStringInterface", "Interface"));
		InterfaceWidgetRow.NameContent()
		[
			InterfaceHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			SNew(SObjectPropertyEntryBox)
			.ObjectPath_UObject(this, &UAnimGraphNode_Layer::GetCurrentInterfaceBlueprintPath)
			.AllowedClass(UAnimBlueprint::StaticClass())
			.NewAssetFactories(TArray<UFactory*>())
			.OnShouldFilterAsset(FOnShouldFilterAsset::CreateUObject(this, &UAnimGraphNode_Layer::OnShouldFilterInterfaceBlueprint))
			.OnObjectChanged(FOnSetObject::CreateUObject(this, &UAnimGraphNode_Layer::OnSetInterfaceBlueprint, InterfaceHandle))
		];
	}

	// Customize Layer
	{
		TSharedRef<IPropertyHandle> LayerHandle = DetailBuilder.GetProperty(TEXT("Node.Layer"), GetClass());
		if(LayerHandle->IsValidHandle())
		{
			LayerHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateUObject(this, &UAnimGraphNode_Layer::OnStructuralPropertyChanged, &DetailBuilder));
		}

		LayerHandle->MarkHiddenByCustomization();

		FDetailWidgetRow& LayerWidgetRow = CategoryBuilder.AddCustomRow(LOCTEXT("FilterStringLayer", "Layer"));
		LayerWidgetRow.NameContent()
		[
			LayerHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(150.0f)
		[
			PropertyCustomizationHelpers::MakePropertyComboBox(
				LayerHandle, 
				FOnGetPropertyComboBoxStrings::CreateUObject(this,  &UAnimGraphNode_Layer::GetLayerNames),
				FOnGetPropertyComboBoxValue::CreateUObject(this,  &UAnimGraphNode_Layer::GetLayerName)
			)
		];
	}

	Super::CustomizeDetails(DetailBuilder);
}

FString UAnimGraphNode_Layer::GetCurrentInterfaceBlueprintPath() const
{
	UClass* InterfaceClass = *Node.Interface;

	if(InterfaceClass)
	{
		UBlueprint* ActualBlueprint = UBlueprint::GetBlueprintFromClass(InterfaceClass);

		if(ActualBlueprint)
		{
			return ActualBlueprint->GetPathName();
		}
	}

	return FString();
}

bool UAnimGraphNode_Layer::OnShouldFilterInstanceBlueprint(const FAssetData& AssetData) const
{
	if(Super::OnShouldFilterInstanceBlueprint(AssetData))
	{
		return true;
	}

	if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
	{
		// Check interface compatibility
		if(UClass* InterfaceClass = Node.Interface.Get())
		{
			bool bMatchesInterface = false;

			const FString ImplementedInterfaces = AssetData.GetTagValueRef<FString>(FBlueprintTags::ImplementedInterfaces);
			if(!ImplementedInterfaces.IsEmpty())
			{
				FString FullInterface;
				FString RemainingString;
				FString InterfacePath;
				FString CurrentString = *ImplementedInterfaces;
				while(CurrentString.Split(TEXT(","), &FullInterface, &RemainingString) && !bMatchesInterface)
				{
					if (!CurrentString.StartsWith(TEXT("Graphs=(")))
					{
						if (FullInterface.Split(TEXT("\""), &CurrentString, &InterfacePath, ESearchCase::CaseSensitive))
						{
							// The interface paths in metadata end with "', so remove those
							InterfacePath.RemoveFromEnd(TEXT("\"'"));

							FCoreRedirectObjectName ResolvedInterfaceName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, FCoreRedirectObjectName(InterfacePath));
							bMatchesInterface = ResolvedInterfaceName.ObjectName == InterfaceClass->GetFName();
						}
					}
			
					CurrentString = RemainingString;
				}
			}

			if(!bMatchesInterface)
			{
				return true;
			}
		}
	}

	return false;
}

FString UAnimGraphNode_Layer::GetCurrentInstanceBlueprintPath() const
{
	UClass* InterfaceClass = *Node.InstanceClass;

	if(InterfaceClass)
	{
		UBlueprint* ActualBlueprint = UBlueprint::GetBlueprintFromClass(InterfaceClass);

		if(ActualBlueprint)
		{
			return ActualBlueprint->GetPathName();
		}
	}

	return FString();
}

bool UAnimGraphNode_Layer::OnShouldFilterInterfaceBlueprint(const FAssetData& AssetData) const
{
	FAssetDataTagMapSharedView::FFindTagResult Result = AssetData.TagsAndValues.FindTag("ParentClass");
	if (Result.IsSet())
	{
		FString ParentClassObjectPath;
		if (FPackageName::ParseExportTextPath(Result.GetValue(), nullptr, &ParentClassObjectPath))
		{
			ParentClassObjectPath.RemoveFromEnd(TEXT("_C"));
			return ParentClassObjectPath != UAnimLayerInterface::StaticClass()->GetPathName();
		}

	}

	return true;
}

void UAnimGraphNode_Layer::OnSetInterfaceBlueprint(const FAssetData& AssetData, TSharedRef<IPropertyHandle> InterfaceClassPropHandle)
{
	FScopedTransaction Transaction(LOCTEXT("SetInterface", "Set Instance Interface"));
	Modify();

	if(UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(AssetData.GetAsset()))
	{
		InterfaceClassPropHandle->SetValue(Blueprint->GetAnimBlueprintGeneratedClass());
	}
	else
	{
		InterfaceClassPropHandle->SetValue((UObject*)nullptr);
	}
}

void UAnimGraphNode_Layer::GetExposableProperties(TArray<UProperty*>& OutExposableProperties) const
{
	UClass* TargetClass = GetTargetSkeletonClass();
	if(TargetClass)
	{
		// add only sub-input properties
		IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(TargetClass);
		for(const FAnimBlueprintFunction& AnimBlueprintFunction : AnimClassInterface->GetAnimBlueprintFunctions())
		{
			// Check name matches.
			if(AnimBlueprintFunction.Name == Node.GetDynamicLinkFunctionName())
			{
				for(UProperty* Property : AnimBlueprintFunction.InputProperties)
				{
					OutExposableProperties.Add(Property);
				}
			}
		}
	}
}

void UAnimGraphNode_Layer::GetLayerNames(TArray<TSharedPtr<FString>>& OutStrings, TArray<TSharedPtr<SToolTip>>& OutToolTips, TArray<bool>& OutRestrictedItems)
{
	UClass* TargetClass = *Node.Interface;
	if(TargetClass == nullptr)
	{
		// If no interface specified, use this class
		if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
		{
			TargetClass = *CurrentBlueprint->SkeletonGeneratedClass;
		}
	}

	if(TargetClass)
	{
		IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(TargetClass);
		for(const FAnimBlueprintFunction& AnimBlueprintFunction : AnimClassInterface->GetAnimBlueprintFunctions())
		{
			if(AnimBlueprintFunction.Name != UEdGraphSchema_K2::GN_AnimGraph)
			{
				OutStrings.Add(MakeShared<FString>(AnimBlueprintFunction.Name.ToString()));
				OutToolTips.Add(nullptr);
				OutRestrictedItems.Add(false);
			}
		}
	}
}

FString UAnimGraphNode_Layer::GetLayerName() const
{
	return Node.Layer.ToString();
}

bool UAnimGraphNode_Layer::IsStructuralProperty(UProperty* InProperty) const
{
	return Super::IsStructuralProperty(InProperty) ||
		InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_Layer, Interface) ||
		InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_Layer, Layer);
}

UClass* UAnimGraphNode_Layer::GetTargetSkeletonClass() const
{
	UClass* SuperTargetSkeletonClass = Super::GetTargetSkeletonClass();
	if(SuperTargetSkeletonClass == nullptr)
	{
		// If no interface specified, use this class
		if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
		{
			SuperTargetSkeletonClass = *CurrentBlueprint->SkeletonGeneratedClass;
		}
	}
	return SuperTargetSkeletonClass;
}

#undef LOCTEXT_NAMESPACE
