// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_LinkedAnimLayer.h"
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
#include "Kismet2/KismetEditorUtilities.h"
#include "AnimationStateGraph.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

#define LOCTEXT_NAMESPACE "LinkedAnimLayerNode"

void UAnimGraphNode_LinkedAnimLayer::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimLayerGuidConformation)
		{
			if (!InterfaceGuid.IsValid())
			{
				InterfaceGuid = GetGuidForLayer();
			}
		}
	}
}

void UAnimGraphNode_LinkedAnimLayer::ReconstructNode()
{
	if(SetObjectBeingDebuggedHandle.IsValid())
	{
		GetBlueprint()->OnSetObjectBeingDebugged().Remove(SetObjectBeingDebuggedHandle);
	}

	SetObjectBeingDebuggedHandle = GetBlueprint()->OnSetObjectBeingDebugged().AddUObject(this, &UAnimGraphNode_LinkedAnimLayer::HandleSetObjectBeingDebugged);

	Super::ReconstructNode();
}

FText UAnimGraphNode_LinkedAnimLayer::GetTooltipText() const
{
	return LOCTEXT("ToolTip", "Runs another linked animation layer graph to process animation");
}

FAnimNode_LinkedAnimLayer* UAnimGraphNode_LinkedAnimLayer::GetPreviewNode() const
{
	FAnimNode_LinkedAnimLayer* PreviewNode = nullptr;
	USkeletalMeshComponent* Component = nullptr;

	// look for a valid component in the object being debugged,
	// we might be set to something other than the preview.
	UObject* ObjectBeingDebugged = GetAnimBlueprint()->GetObjectBeingDebugged();
	if (ObjectBeingDebugged)
	{
		UAnimInstance* InstanceBeingDebugged = Cast<UAnimInstance>(ObjectBeingDebugged);
		if (InstanceBeingDebugged)
		{
			Component = InstanceBeingDebugged->GetSkelMeshComponent();
		}
	}

	if (Component != nullptr && Component->GetAnimInstance() != nullptr)
	{
		PreviewNode = static_cast<FAnimNode_LinkedAnimLayer*>(FindDebugAnimNode(Component));
	}

	return PreviewNode;
}

FText UAnimGraphNode_LinkedAnimLayer::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	UClass* TargetClass = *Node.Interface;
	UAnimBlueprint* TargetAnimBlueprintInterface = TargetClass ? CastChecked<UAnimBlueprint>(TargetClass->ClassGeneratedBy) : nullptr;

	if(TitleType == ENodeTitleType::MenuTitle)
	{
		return LOCTEXT("NodeTitle", "Linked Anim Layer");
	}
	else
	{
	FFormatNamedArguments Args;
		Args.Add(TEXT("NodeTitle"), LOCTEXT("NodeTitle", "Linked Anim Layer"));
		Args.Add(TEXT("TargetClass"), TargetAnimBlueprintInterface ? FText::FromString(TargetAnimBlueprintInterface->GetName()) : LOCTEXT("InterfaceNone", "None"));
		Args.Add(TEXT("Layer"), Node.Layer == NAME_None ? LOCTEXT("LayerNone", "None") : FText::FromName(Node.Layer));

		if (FAnimNode_LinkedAnimLayer* PreviewNode = GetPreviewNode())
		{
			if (UAnimInstance* PreviewAnimInstance = PreviewNode->GetTargetInstance<UAnimInstance>())
			{
				if (UClass* PreviewTargetClass = PreviewAnimInstance->GetClass())
	{
					Args.Add(TEXT("TargetClass"), PreviewTargetClass == GetAnimBlueprint()->GeneratedClass ? LOCTEXT("ClassSelf", "Self") : FText::FromName(PreviewTargetClass->GetFName()));
				}
	}
		}

		if (TitleType == ENodeTitleType::ListView)
	{
		return FText::Format(LOCTEXT("TitleListFormatOutputPose", "{NodeTitle}: {Layer} - {TargetClass}"), Args);
	}
	else
	{
		return FText::Format(LOCTEXT("TitleFormatOutputPose", "{NodeTitle}: {Layer}\n{TargetClass}"), Args);
	}
	}
}

void UAnimGraphNode_LinkedAnimLayer::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	if(Node.Layer == NAME_None)
	{
		MessageLog.Error(*LOCTEXT("NoLayerError", "Linked anim layer node @@ does not specify a layer.").ToString(), this);
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
					MessageLog.Error(*LOCTEXT("MissingInterfaceError", "Linked anim layer node @@ uses interface @@ that this blueprint does not implement.").ToString(), this, Node.Interface.Get());
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
				MessageLog.Error(*FText::Format(LOCTEXT("MissingLayerError", "Linked anim layer node @@ uses invalid layer '{0}'."), FText::FromName(Node.Layer)).ToString(), this);
			}
		}

		if(CurrentBlueprint)
		{
			UAnimGraphNode_LinkedAnimLayer* OriginalThis = Cast<UAnimGraphNode_LinkedAnimLayer>(MessageLog.FindSourceObject(this));

			// check layer is only used once in this blueprint
			auto CheckGraph = [this, OriginalThis, &MessageLog](const UEdGraph* InGraph)
			{
				TArray<UAnimGraphNode_LinkedAnimLayer*> LayerNodes;
				InGraph->GetNodesOfClass(LayerNodes);
				for(const UAnimGraphNode_LinkedAnimLayer* LayerNode : LayerNodes)
				{
					if(LayerNode != OriginalThis)
					{
						if(LayerNode->Node.Layer == Node.Layer)
						{
							MessageLog.Error(*FText::Format(LOCTEXT("DuplicateLayerError", "Linked anim layer node @@ also uses layer '{0}', layers can be used only once in an animation blueprint."), FText::FromName(Node.Layer)).ToString(), this);
						}
					}
				}
			};
		
			TArray<UEdGraph*> Graphs;
			CurrentBlueprint->GetAllGraphs(Graphs);

			auto ValidateOuterGraph = [this, OriginalThis, &MessageLog](const UEdGraph* InGraph)
			{
				static const FName DefaultAnimGraphName("AnimGraph");
				if (InGraph->Nodes.Contains(OriginalThis))
				{
					if (!InGraph->IsA<UAnimationStateGraph>() && InGraph->GetFName() != DefaultAnimGraphName)
					{
						MessageLog.Error(*FText::Format(LOCTEXT("NestedLayer", "Linked anim layer node @@ is part of Animation Layer Graph '{0}', layers cannot be nested."), FText::FromName(InGraph->GetFName())).ToString(), this);
					}
				}
			};
			
			for(const UEdGraph* Graph : Graphs)
			{
				CheckGraph(Graph);
				ValidateOuterGraph(Graph);				
			}
		}
	}
}

UObject* UAnimGraphNode_LinkedAnimLayer::GetJumpTargetForDoubleClick() const
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

void UAnimGraphNode_LinkedAnimLayer::JumpToDefinition() const
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

bool UAnimGraphNode_LinkedAnimLayer::HasExternalDependencies(TArray<class UStruct*>* OptionalOutput /*= NULL*/) const
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

void UAnimGraphNode_LinkedAnimLayer::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
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
			LayerHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateUObject(this, &UAnimGraphNode_LinkedAnimLayer::OnLayerChanged, &DetailBuilder));
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
						FOnGetPropertyComboBoxStrings::CreateUObject(this,  &UAnimGraphNode_LinkedAnimLayer::GetLayerNames),
						FOnGetPropertyComboBoxValue::CreateUObject(this,  &UAnimGraphNode_LinkedAnimLayer::GetLayerName)
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
				.ObjectPath_UObject(this, &UAnimGraphNode_LinkedAnimLayer::GetCurrentInstanceBlueprintPath)
				.AllowedClass(UAnimBlueprint::StaticClass())
				.NewAssetFactories(TArray<UFactory*>())
				.OnShouldFilterAsset(FOnShouldFilterAsset::CreateUObject(this, &UAnimGraphNode_LinkedAnimLayer::OnShouldFilterInstanceBlueprint))
				.OnObjectChanged(FOnSetObject::CreateUObject(this, &UAnimGraphNode_LinkedAnimLayer::OnSetInstanceBlueprint, &DetailBuilder))
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this](){ return !HasValidNonSelfLayer() ? EVisibility::Visible : EVisibility::Collapsed; })
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("SelfLayersWarning", "Uses layer in this Blueprint."))
				.ToolTipText(LOCTEXT("SelfLayersWarningTooltip", "This linked anim layer node refers to a layer only in this blueprint, so cannot be overriden by an external blueprint implementation.\nChange to use a layer from an implemented interface to allow this override."))
			]
		];
	}
}

bool UAnimGraphNode_LinkedAnimLayer::OnShouldFilterInstanceBlueprint(const FAssetData& AssetData) const
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

FString UAnimGraphNode_LinkedAnimLayer::GetCurrentInstanceBlueprintPath() const
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

void UAnimGraphNode_LinkedAnimLayer::GetExposableProperties(TArray<FProperty*>& OutExposableProperties) const
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
				for(const TFieldPath<FProperty>& Property : AnimBlueprintFunction.InputProperties)
				{
					OutExposableProperties.Add(Property.Get());
				}
			}
		}
	}
}

void UAnimGraphNode_LinkedAnimLayer::GetLayerNames(TArray<TSharedPtr<FString>>& OutStrings, TArray<TSharedPtr<SToolTip>>& OutToolTips, TArray<bool>& OutRestrictedItems)
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

FString UAnimGraphNode_LinkedAnimLayer::GetLayerName() const
{
	return Node.Layer.ToString();
}

bool UAnimGraphNode_LinkedAnimLayer::IsStructuralProperty(FProperty* InProperty) const
{
	return Super::IsStructuralProperty(InProperty) ||
		InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_LinkedAnimLayer, Layer);
}

UClass* UAnimGraphNode_LinkedAnimLayer::GetTargetSkeletonClass() const
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

TSubclassOf<UInterface> UAnimGraphNode_LinkedAnimLayer::GetInterfaceForLayer() const
{
	if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
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

	return nullptr;
}

void UAnimGraphNode_LinkedAnimLayer::UpdateGuidForLayer()
{
	if (!InterfaceGuid.IsValid())
	{
		InterfaceGuid = GetGuidForLayer();
	}
}

FGuid UAnimGraphNode_LinkedAnimLayer::GetGuidForLayer() const
{
	if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(GetBlueprint()))
	{
		// Find layer with this name in interfaces
		for (FBPInterfaceDescription& InterfaceDesc : CurrentBlueprint->ImplementedInterfaces)
		{
			for (UEdGraph* InterfaceGraph : InterfaceDesc.Graphs)
			{
				if (InterfaceGraph->GetFName() == Node.Layer)
				{
					return InterfaceGraph->InterfaceGuid;
				}
			}
		}
	}

	return FGuid();
}

void UAnimGraphNode_LinkedAnimLayer::OnLayerChanged(IDetailLayoutBuilder* DetailBuilder)
{
	OnStructuralPropertyChanged(DetailBuilder);

	// Get the interface for this layer. If null, then we are using a 'self' layer.
	Node.Interface = GetInterfaceForLayer();

	// Update the Guid for conforming
	InterfaceGuid = GetGuidForLayer();

	if(Node.Interface.Get() == nullptr)
	{
		// Self layers cannot have override implementations
		Node.InstanceClass = nullptr;
	}
}

bool UAnimGraphNode_LinkedAnimLayer::HasAvailableLayers() const
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

bool UAnimGraphNode_LinkedAnimLayer::HasValidNonSelfLayer() const
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

void UAnimGraphNode_LinkedAnimLayer::HandleSetObjectBeingDebugged(UObject* InDebugObj)
{
	if(HasValidBlueprint())
	{
		NodeTitleChangedEvent.Broadcast();

		FAnimNode_LinkedAnimLayer* PreviewNode = GetPreviewNode();
		if(PreviewNode)
		{
			PreviewNode->OnInstanceChanged().AddUObject(this, &UAnimGraphNode_LinkedAnimLayer::HandleInstanceChanged);
		}
	}
}

void UAnimGraphNode_LinkedAnimLayer::HandleInstanceChanged()
{
	NodeTitleChangedEvent.Broadcast();
}

#undef LOCTEXT_NAMESPACE
