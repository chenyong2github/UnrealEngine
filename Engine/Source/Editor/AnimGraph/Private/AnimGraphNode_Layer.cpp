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
		else
		{
			// check we implement this interface
			bool bImplementsInterface = false;

			if (CurrentBlueprint)
			{
				for (FBPInterfaceDescription& InterfaceDesc : CurrentBlueprint->ImplementedInterfaces)
				{
					if (InterfaceDesc.Interface.Get() == TargetClass)
					{
						bImplementsInterface = true;
						break;
					}
				}
			}

			if(!bImplementsInterface)
			{
				// Its possible we have a left-over interface referenced here that needs clearing now we are a 'self' layer
				if(GetInterfaceForLayer() == nullptr)
				{
					Node.Interface = nullptr;

					// No interface any more, use this class
					if (CurrentBlueprint)
					{
						TargetClass = *CurrentBlueprint->SkeletonGeneratedClass;
					}
				}
				else
				{
					MessageLog.Error(*LOCTEXT("MissingInterfaceError", "Layer node @@ uses interface @@ that this blueprint does not implement.").ToString(), this, Node.Interface.Get());
				}
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

	// Customize Layer
	{
		TSharedRef<IPropertyHandle> LayerHandle = DetailBuilder.GetProperty(TEXT("Node.Layer"), GetClass());
		if(LayerHandle->IsValidHandle())
		{
			LayerHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateUObject(this, &UAnimGraphNode_Layer::OnLayerChanged, &DetailBuilder));
		}

		LayerHandle->MarkHiddenByCustomization();

		// Check layers available in this BP
		FDetailWidgetRow& LayerWidgetRow = CategoryBuilder.AddCustomRow(LOCTEXT("FilterStringLayer", "Layer"));
		LayerWidgetRow.NameContent()
		[
			LayerHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(150.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.Visibility_Lambda([this](){ return HasAvailableLayers() ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					PropertyCustomizationHelpers::MakePropertyComboBox(
						LayerHandle, 
						FOnGetPropertyComboBoxStrings::CreateUObject(this,  &UAnimGraphNode_Layer::GetLayerNames),
						FOnGetPropertyComboBoxValue::CreateUObject(this,  &UAnimGraphNode_Layer::GetLayerName)
					)
				]
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this](){ return !HasAvailableLayers() ? EVisibility::Visible : EVisibility::Collapsed; })
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("NoLayersWarning", "No available layers."))
				.ToolTipText(LOCTEXT("NoLayersWarningTooltip", "This Animation Blueprint has no layers to choose from.\nTo add some, either implement an Animation Layer Interface via the Class Settings, or add an animation layer in the My Blueprint tab."))
			]
		];
	}

	GenerateExposedPinsDetails(DetailBuilder);
	UAnimGraphNode_CustomProperty::CustomizeDetails(DetailBuilder);

	// Customize InstanceClass with unique visibility (identical to parent class apart from this)
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
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(SObjectPropertyEntryBox)
				.Visibility_Lambda([this](){ return HasValidNonSelfLayer() ? EVisibility::Visible : EVisibility::Collapsed; })
				.ObjectPath_UObject(this, &UAnimGraphNode_Layer::GetCurrentInstanceBlueprintPath)
				.AllowedClass(UAnimBlueprint::StaticClass())
				.NewAssetFactories(TArray<UFactory*>())
				.OnShouldFilterAsset(FOnShouldFilterAsset::CreateUObject(this, &UAnimGraphNode_Layer::OnShouldFilterInstanceBlueprint))
				.OnObjectChanged(FOnSetObject::CreateUObject(this, &UAnimGraphNode_Layer::OnSetInstanceBlueprint, &DetailBuilder))
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this](){ return !HasValidNonSelfLayer() ? EVisibility::Visible : EVisibility::Collapsed; })
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("SelfLayersWarning", "Uses layer in this Blueprint."))
				.ToolTipText(LOCTEXT("SelfLayersWarningTooltip", "This layer node refers to a layer only in this blueprint, so cannot be overriden by an external blueprint implementation.\nChange to use a layer from an implemented interface to allow this override."))
			]
		];
	}
}

bool UAnimGraphNode_Layer::OnShouldFilterInstanceBlueprint(const FAssetData& AssetData) const
{
	if(Super::OnShouldFilterInstanceBlueprint(AssetData))
	{
		return true;
	}

	if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
	{
		TArray<TSubclassOf<UInterface>> AnimInterfaces;
		for(const FBPInterfaceDescription& InterfaceDesc : CurrentBlueprint->ImplementedInterfaces)
		{
			if(InterfaceDesc.Interface && InterfaceDesc.Interface->IsChildOf<UAnimLayerInterface>())
			{
				if(Node.Layer == NAME_None || InterfaceDesc.Interface->FindFunctionByName(Node.Layer))
				{
					AnimInterfaces.Add(InterfaceDesc.Interface);
				}
			}
		}

		// Check interface compatibility
		if(AnimInterfaces.Num() > 0)
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

							// Verify against all interfaces we currently implement
							for(TSubclassOf<UInterface> AnimInterface : AnimInterfaces)
							{
								bMatchesInterface |= ResolvedInterfaceName.ObjectName == AnimInterface->GetFName();
							}
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
		else
		{
			// No interfaces, so no compatible BPs
			return true;
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
	// If no interface specified, use this class
	if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
	{
		UClass* TargetClass = *CurrentBlueprint->SkeletonGeneratedClass;
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
}

FString UAnimGraphNode_Layer::GetLayerName() const
{
	return Node.Layer.ToString();
}

bool UAnimGraphNode_Layer::IsStructuralProperty(UProperty* InProperty) const
{
	return Super::IsStructuralProperty(InProperty) ||
		InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_Layer, Layer);
}

UClass* UAnimGraphNode_Layer::GetTargetSkeletonClass() const
{
	UClass* SuperTargetSkeletonClass = Super::GetTargetSkeletonClass();
	if(SuperTargetSkeletonClass == nullptr)
	{
		// If no concrete class specified, use this class
		if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
		{
			SuperTargetSkeletonClass = *CurrentBlueprint->SkeletonGeneratedClass;
		}
	}
	return SuperTargetSkeletonClass;
}

TSubclassOf<UInterface> UAnimGraphNode_Layer::GetInterfaceForLayer() const
{
	if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
	{
		UClass* TargetClass = *CurrentBlueprint->SkeletonGeneratedClass;
		if(TargetClass)
		{
			// Find layer with this name in interfaces
			for(FBPInterfaceDescription& InterfaceDesc : CurrentBlueprint->ImplementedInterfaces)
			{
				for(UEdGraph* InterfaceGraph : InterfaceDesc.Graphs)
				{
					if(InterfaceGraph->GetFName() == Node.Layer)
					{
						return InterfaceDesc.Interface;
					}
				}
			}
		}
	}

	return nullptr;
}

void UAnimGraphNode_Layer::OnLayerChanged(IDetailLayoutBuilder* DetailBuilder)
{
	OnStructuralPropertyChanged(DetailBuilder);

	// Get the interface for this layer. If null, then we are using a 'self' layer.
	Node.Interface = GetInterfaceForLayer();

	if(Node.Interface.Get() == nullptr)
	{
		// Self layers cannot have override implementations
		Node.InstanceClass = nullptr;
	}
}

bool UAnimGraphNode_Layer::HasAvailableLayers() const
{
	if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
	{
		UClass* TargetClass = *CurrentBlueprint->SkeletonGeneratedClass;
		if(TargetClass)
		{
			IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(TargetClass);
			for(const FAnimBlueprintFunction& AnimBlueprintFunction : AnimClassInterface->GetAnimBlueprintFunctions())
			{
				if(AnimBlueprintFunction.Name != UEdGraphSchema_K2::GN_AnimGraph)
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool UAnimGraphNode_Layer::HasValidNonSelfLayer() const
{
	if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
	{
		if(Node.Interface.Get())
		{
			for(const FBPInterfaceDescription& InterfaceDesc : CurrentBlueprint->ImplementedInterfaces)
			{
				if(InterfaceDesc.Interface && InterfaceDesc.Interface->IsChildOf<UAnimLayerInterface>())
				{
					if(InterfaceDesc.Interface->FindFunctionByName(Node.Layer))
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
