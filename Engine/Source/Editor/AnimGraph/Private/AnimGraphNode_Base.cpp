// Copyright Epic Games, Inc. All Rights Reserved.


#include "AnimGraphNode_Base.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Animation/AnimInstance.h"
#include "AnimationGraphSchema.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "AnimBlueprintNodeOptionalPinManager.h"
#include "IAnimNodeEditMode.h"
#include "AnimNodeEditModes.h"
#include "AnimationGraph.h"
#include "EditorModeManager.h"

#include "AnimationEditorUtils.h"
#include "UObject/UnrealType.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AnimBlueprintCompiler.h"
#include "AnimBlueprintExtension_Base.h"
#include "AnimBlueprintExtension_Attributes.h"
#include "AnimBlueprintExtension_PropertyAccess.h"
#include "IAnimBlueprintCompilationContext.h"
#include "AnimBlueprintCompilationContext.h"
#include "AnimBlueprintExtension.h"
#include "FindInBlueprintManager.h"
#include "IPropertyAccessBlueprintBinding.h"
#include "IPropertyAccessEditor.h"
#include "ScopedTransaction.h"
#include "Algo/Accumulate.h"
#include "Fonts/FontMeasure.h"
#include "UObject/ReleaseObjectVersion.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Fonts/FontMeasure.h"

#define LOCTEXT_NAMESPACE "UAnimGraphNode_Base"

/////////////////////////////////////////////////////
// UAnimGraphNode_Base

UAnimGraphNode_Base::UAnimGraphNode_Base(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAnimGraphNode_Base::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	TUniquePtr<IAnimBlueprintCompilationContext> CompilationContext = IAnimBlueprintCompilationContext::Get(CompilerContext);
	UAnimBlueprintExtension_Base* Extension = UAnimBlueprintExtension_Base::GetExtension<UAnimBlueprintExtension_Base>(GetAnimBlueprint());
	Extension->CreateEvaluationHandlerForNode(*CompilationContext.Get(), this);
}

void UAnimGraphNode_Base::PreEditChange(FProperty* PropertyThatWillChange)
{
	Super::PreEditChange(PropertyThatWillChange);

	if (PropertyThatWillChange && PropertyThatWillChange->GetFName() == GET_MEMBER_NAME_CHECKED(FOptionalPinFromProperty, bShowPin))
	{
		FOptionalPinManager::CacheShownPins(ShowPinForProperties, OldShownPins);
	}
}

void UAnimGraphNode_Base::PostEditUndo()
{
	Super::PostEditUndo();

	if(HasValidBlueprint())
	{
		// Node may have been removed or added by undo/redo so make sure extensions are refreshed
		GetAnimBlueprint()->RequestRefreshExtensions();
	}
}

void UAnimGraphNode_Base::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if ((PropertyName == GET_MEMBER_NAME_CHECKED(FOptionalPinFromProperty, bShowPin)))
	{
		FOptionalPinManager::EvaluateOldShownPins(ShowPinForProperties, OldShownPins, this);
		GetSchema()->ReconstructNode(*this);
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	PropertyChangeEvent.Broadcast(PropertyChangedEvent);
}

void UAnimGraphNode_Base::SetPinVisibility(bool bInVisible, int32 InOptionalPinIndex)
{
	if(ShowPinForProperties[InOptionalPinIndex].bShowPin != bInVisible)
	{
		FOptionalPinManager::CacheShownPins(ShowPinForProperties, OldShownPins);

		ShowPinForProperties[InOptionalPinIndex].bShowPin = bInVisible;
		
		FOptionalPinManager::EvaluateOldShownPins(ShowPinForProperties, OldShownPins, this);
		GetSchema()->ReconstructNode(*this);
	}
}

void UAnimGraphNode_Base::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FReleaseObjectVersion::GUID) < FReleaseObjectVersion::AnimationGraphNodeBindingsDisplayedAsPins)
		{
			// Push any bindings to optional pins
			bool bPushedBinding = false;
			for(const TPair<FName, FAnimGraphNodePropertyBinding>& BindingPair : PropertyBindings)
			{
				for(FOptionalPinFromProperty& OptionalPin : ShowPinForProperties)
				{
					if(OptionalPin.bCanToggleVisibility && !OptionalPin.bShowPin && OptionalPin.PropertyName == BindingPair.Key)
					{
						OptionalPin.bShowPin = true;
						bPushedBinding = true;
					}
				}
			}

			if(bPushedBinding)
			{
				FOptionalPinManager::EvaluateOldShownPins(ShowPinForProperties, OldShownPins, this);
			}
		}
	}
}

void UAnimGraphNode_Base::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();

	// This makes sure that all anim BP extensions are registered that this node needs
	UAnimBlueprintExtension::RequestExtensionsForNode(this);
}

void UAnimGraphNode_Base::DestroyNode()
{
	// This node may have been the last using its extension, so refresh
	GetAnimBlueprint()->RequestRefreshExtensions();

	Super::DestroyNode();
}

void UAnimGraphNode_Base::CreateOutputPins()
{
	if (!IsSinkNode())
	{
		CreatePin(EGPD_Output, UAnimationGraphSchema::PC_Struct, FPoseLink::StaticStruct(), TEXT("Pose"));
	}
}

void UAnimGraphNode_Base::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	// Validate any bone references we have
	for(const TPair<FStructProperty*, const void*>& PropertyValuePair : TPropertyValueRange<FStructProperty>(GetClass(), this))
	{
		if(PropertyValuePair.Key->Struct == FBoneReference::StaticStruct())
		{
			const FBoneReference& BoneReference = *(const FBoneReference*)PropertyValuePair.Value;

			// Temporary fix where skeleton is not fully loaded during AnimBP compilation and thus virtual bone name check is invalid UE-39499 (NEED FIX) 
			if (ForSkeleton && !ForSkeleton->HasAnyFlags(RF_NeedPostLoad))
			{
				if (BoneReference.BoneName != NAME_None)
				{
					if (ForSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneReference.BoneName) == INDEX_NONE)
					{
						FFormatNamedArguments Args;
						Args.Add(TEXT("BoneName"), FText::FromName(BoneReference.BoneName));

						MessageLog.Warning(*FText::Format(LOCTEXT("NoBoneFoundToModify", "@@ - Bone {BoneName} not found in Skeleton"), Args).ToString(), this);
					}
				}
			}
		}
	}
}

void UAnimGraphNode_Base::CopyTermDefaultsToDefaultObject(IAnimBlueprintCopyTermDefaultsContext& InCompilationContext, IAnimBlueprintNodeCopyTermDefaultsContext& InPerNodeContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	InPerNodeContext.GetTargetProperty()->CopyCompleteValue(InPerNodeContext.GetDestinationPtr(), InPerNodeContext.GetSourcePtr());

	OnCopyTermDefaultsToDefaultObject(InCompilationContext, InPerNodeContext, OutCompiledData);
}

void UAnimGraphNode_Base::OverrideAssets(IAnimBlueprintNodeOverrideAssetsContext& InContext) const
{
	if(InContext.GetAssets().Num() > 0)
	{
		if(UAnimationAsset* AnimationAsset = Cast<UAnimationAsset>(InContext.GetAssets()[0]))
		{
			// Call the legacy implementation
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			InContext.GetAnimNode<FAnimNode_Base>().OverrideAsset(AnimationAsset);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
	}
	OnOverrideAssets(InContext);
}

void UAnimGraphNode_Base::InternalPinCreation(TArray<UEdGraphPin*>* OldPins)
{
	// preload required assets first before creating pins
	PreloadRequiredAssets();

	const UAnimationGraphSchema* Schema = GetDefault<UAnimationGraphSchema>();
	if (const FStructProperty* NodeStruct = GetFNodeProperty())
	{
		// Display any currently visible optional pins
		{
			UObject* NodeDefaults = GetArchetype();
			FAnimBlueprintNodeOptionalPinManager OptionalPinManager(this, OldPins);
			OptionalPinManager.AllocateDefaultPins(NodeStruct->Struct, NodeStruct->ContainerPtrToValuePtr<uint8>(this), NodeDefaults ? NodeStruct->ContainerPtrToValuePtr<uint8>(NodeDefaults) : nullptr);
		}

		// Create the output pin, if needed
		CreateOutputPins();
	}
}

void UAnimGraphNode_Base::AllocateDefaultPins()
{
	InternalPinCreation(NULL);
}

void UAnimGraphNode_Base::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	InternalPinCreation(&OldPins);

	RestoreSplitPins(OldPins);
}

bool UAnimGraphNode_Base::CanJumpToDefinition() const
{
	return GetJumpTargetForDoubleClick() != nullptr;
}

void UAnimGraphNode_Base::JumpToDefinition() const
{
	if (UObject* HyperlinkTarget = GetJumpTargetForDoubleClick())
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(HyperlinkTarget);
	}
}

FLinearColor UAnimGraphNode_Base::GetNodeTitleColor() const
{
	return FLinearColor::Black;
}

UScriptStruct* UAnimGraphNode_Base::GetFNodeType() const
{
	UScriptStruct* BaseFStruct = FAnimNode_Base::StaticStruct();

	for (TFieldIterator<FProperty> PropIt(GetClass(), EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
	{
		if (FStructProperty* StructProp = CastField<FStructProperty>(*PropIt))
		{
			if (StructProp->Struct->IsChildOf(BaseFStruct))
			{
				return StructProp->Struct;
			}
		}
	}

	return NULL;
}

FStructProperty* UAnimGraphNode_Base::GetFNodeProperty() const
{
	UScriptStruct* BaseFStruct = FAnimNode_Base::StaticStruct();

	for (TFieldIterator<FProperty> PropIt(GetClass(), EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
	{
		if (FStructProperty* StructProp = CastField<FStructProperty>(*PropIt))
		{
			if (StructProp->Struct->IsChildOf(BaseFStruct))
			{
				return StructProp;
			}
		}
	}

	return NULL;
}

FString UAnimGraphNode_Base::GetNodeCategory() const
{
	return TEXT("Misc.");
}

void UAnimGraphNode_Base::GetNodeAttributes( TArray<TKeyValuePair<FString, FString>>& OutNodeAttributes ) const
{
	OutNodeAttributes.Add( TKeyValuePair<FString, FString>( TEXT( "Type" ), TEXT( "AnimGraphNode" ) ));
	OutNodeAttributes.Add( TKeyValuePair<FString, FString>( TEXT( "Class" ), GetClass()->GetName() ));
	OutNodeAttributes.Add( TKeyValuePair<FString, FString>( TEXT( "Name" ), GetName() ));
}

void UAnimGraphNode_Base::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// actions get registered under specific object-keys; the idea is that 
	// actions might have to be updated (or deleted) if their object-key is  
	// mutated (or removed)... here we use the node's class (so if the node 
	// type disappears, then the action should go with it)
	UClass* ActionKey = GetClass();
	// to keep from needlessly instantiating a UBlueprintNodeSpawner, first   
	// check to make sure that the registrar is looking for actions of this type
	// (could be regenerating actions for a specific asset, and therefore the 
	// registrar would only accept actions corresponding to that asset)
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UAnimGraphNode_Base::GetMenuCategory() const
{
	return FText::FromString(GetNodeCategory());
}

void UAnimGraphNode_Base::GetPinAssociatedProperty(const UScriptStruct* NodeType, const UEdGraphPin* InputPin, FProperty*& OutProperty, int32& OutIndex) const
{
	OutProperty = nullptr;
	OutIndex = INDEX_NONE;

	//@TODO: Name-based hackery, avoid the roundtrip and better indicate when it's an array pose pin
	const FString PinNameStr = InputPin->PinName.ToString();
	const int32 UnderscoreIndex = PinNameStr.Find(TEXT("_"), ESearchCase::CaseSensitive);
	if (UnderscoreIndex != INDEX_NONE)
	{
		const FString ArrayName = PinNameStr.Left(UnderscoreIndex);

		if (FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(NodeType, *ArrayName))
		{
			const int32 ArrayIndex = FCString::Atoi(*(PinNameStr.Mid(UnderscoreIndex + 1)));

			OutProperty = ArrayProperty;
			OutIndex = ArrayIndex;
		}
	}
	
	// If the array check failed or we have no underscores
	if(OutProperty == nullptr)
	{
		if (FProperty* Property = FindFProperty<FProperty>(NodeType, InputPin->PinName))
		{
			OutProperty = Property;
			OutIndex = INDEX_NONE;
		}
	}
}

FPoseLinkMappingRecord UAnimGraphNode_Base::GetLinkIDLocation(const UScriptStruct* NodeType, UEdGraphPin* SourcePin)
{
	if (SourcePin->LinkedTo.Num() > 0)
	{
		if (UAnimGraphNode_Base* LinkedNode = Cast<UAnimGraphNode_Base>(FBlueprintEditorUtils::FindFirstCompilerRelevantNode(SourcePin->LinkedTo[0])))
		{
			//@TODO: Name-based hackery, avoid the roundtrip and better indicate when it's an array pose pin
			const FString SourcePinName = SourcePin->PinName.ToString();
			const int32 UnderscoreIndex = SourcePinName.Find(TEXT("_"), ESearchCase::CaseSensitive);
			if (UnderscoreIndex != INDEX_NONE)
			{
				const FString ArrayName = SourcePinName.Left(UnderscoreIndex);

				if (FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(NodeType, *ArrayName))
				{
					if (FStructProperty* Property = CastField<FStructProperty>(ArrayProperty->Inner))
					{
						if (Property->Struct->IsChildOf(FPoseLinkBase::StaticStruct()))
						{
							const int32 ArrayIndex = FCString::Atoi(*(SourcePinName.Mid(UnderscoreIndex + 1)));
							return FPoseLinkMappingRecord::MakeFromArrayEntry(this, LinkedNode, ArrayProperty, ArrayIndex);
						}
					}
				}
			}
			else
			{
				if (FStructProperty* Property = FindFProperty<FStructProperty>(NodeType, SourcePin->PinName))
				{
					if (Property->Struct->IsChildOf(FPoseLinkBase::StaticStruct()))
					{
						return FPoseLinkMappingRecord::MakeFromMember(this, LinkedNode, Property);
					}
				}
			}
		}
	}

	return FPoseLinkMappingRecord::MakeInvalid();
}

void UAnimGraphNode_Base::CreatePinsForPoseLink(FProperty* PoseProperty, int32 ArrayIndex)
{
	UScriptStruct* A2PoseStruct = FA2Pose::StaticStruct();

	// pose input
	const FName NewPinName = (ArrayIndex == INDEX_NONE) ? PoseProperty->GetFName() : *FString::Printf(TEXT("%s_%d"), *(PoseProperty->GetName()), ArrayIndex);
	CreatePin(EGPD_Input, UAnimationGraphSchema::PC_Struct, A2PoseStruct, NewPinName);
}

void UAnimGraphNode_Base::PostProcessPinName(const UEdGraphPin* Pin, FString& DisplayName) const
{
	if (Pin->Direction == EGPD_Output)
	{
		if (Pin->PinName == TEXT("Pose"))
		{
			DisplayName.Reset();
		}
	}
}

bool UAnimGraphNode_Base::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* DesiredSchema) const
{
	return DesiredSchema->GetClass()->IsChildOf(UAnimationGraphSchema::StaticClass());
}

FString UAnimGraphNode_Base::GetDocumentationLink() const
{
	return TEXT("Shared/GraphNodes/Animation");
}

void UAnimGraphNode_Base::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
	if (UAnimationGraphSchema::IsLocalSpacePosePin(Pin.PinType))
	{
		HoverTextOut = TEXT("Animation Pose");
	}
	else if (UAnimationGraphSchema::IsComponentSpacePosePin(Pin.PinType))
	{
		HoverTextOut = TEXT("Animation Pose (Component Space)");
	}
	else
	{
		Super::GetPinHoverText(Pin, HoverTextOut);
	}
}

void UAnimGraphNode_Base::ProcessDuringCompilation(IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	UAnimBlueprintExtension_Base* Extension = UAnimBlueprintExtension::GetExtension<UAnimBlueprintExtension_Base>(GetAnimBlueprint());

	// Record pose pins for later patchup and gather pins that have an associated evaluation handler
	Extension->ProcessNodePins(this, InCompilationContext, OutCompiledData);

	// Call the override point
	OnProcessDuringCompilation(InCompilationContext, OutCompiledData);
}

void UAnimGraphNode_Base::HandleAnimReferenceCollection(UAnimationAsset* AnimAsset, TArray<UAnimationAsset*>& AnimationAssets) const
{
	if(AnimAsset)
	{
		AnimAsset->HandleAnimReferenceCollection(AnimationAssets, true);
	}
}

void UAnimGraphNode_Base::OnNodeSelected(bool bInIsSelected, FEditorModeTools& InModeTools, FAnimNode_Base* InRuntimeNode)
{
	const FEditorModeID ModeID = GetEditorMode();
	if (ModeID != NAME_None)
	{
		if (bInIsSelected)
		{
			InModeTools.ActivateMode(ModeID);
			if (FEdMode* EdMode = InModeTools.GetActiveMode(ModeID))
			{
				static_cast<IAnimNodeEditMode*>(EdMode)->EnterMode(this, InRuntimeNode);
			}
		}
		else
		{
			if (FEdMode* EdMode = InModeTools.GetActiveMode(ModeID))
			{
				static_cast<IAnimNodeEditMode*>(EdMode)->ExitMode();
			}
			InModeTools.DeactivateMode(ModeID);
		}
	}
}

FEditorModeID UAnimGraphNode_Base::GetEditorMode() const
{
	return AnimNodeEditModes::AnimNode;
}

FAnimNode_Base* UAnimGraphNode_Base::FindDebugAnimNode(USkeletalMeshComponent * PreviewSkelMeshComp) const
{
	FAnimNode_Base* DebugNode = nullptr;

	if (PreviewSkelMeshComp != nullptr && PreviewSkelMeshComp->GetAnimInstance() != nullptr)
	{
		// find an anim node index from debug data
		UAnimBlueprintGeneratedClass* AnimBlueprintClass = Cast<UAnimBlueprintGeneratedClass>(PreviewSkelMeshComp->GetAnimInstance()->GetClass());
		if (AnimBlueprintClass)
		{
			FAnimBlueprintDebugData& DebugData = AnimBlueprintClass->GetAnimBlueprintDebugData();
			int32* IndexPtr = DebugData.NodePropertyToIndexMap.Find(this);

			if (IndexPtr)
			{
				int32 AnimNodeIndex = *IndexPtr;
				// reverse node index temporarily because of a bug in NodeGuidToIndexMap
				AnimNodeIndex = AnimBlueprintClass->GetAnimNodeProperties().Num() - AnimNodeIndex - 1;

				DebugNode = AnimBlueprintClass->GetAnimNodeProperties()[AnimNodeIndex]->ContainerPtrToValuePtr<FAnimNode_Base>(PreviewSkelMeshComp->GetAnimInstance());
			}
		}
	}

	return DebugNode;
}

EAnimAssetHandlerType UAnimGraphNode_Base::SupportsAssetClass(const UClass* AssetClass) const
{
	return EAnimAssetHandlerType::NotSupported;
}


void UAnimGraphNode_Base::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	CopyPinDefaultsToNodeData(Pin);

	if(UAnimationGraph* AnimationGraph = Cast<UAnimationGraph>(GetGraph()))
	{
		AnimationGraph->OnPinDefaultValueChanged.Broadcast(Pin);
	}
}

FString UAnimGraphNode_Base::GetPinMetaData(FName InPinName, FName InKey)
{
	FString MetaData = Super::GetPinMetaData(InPinName, InKey);
	if(MetaData.IsEmpty())
	{
		// Check properties of our anim node
		if(FStructProperty* NodeStructProperty = GetFNodeProperty())
		{
			for (TFieldIterator<FProperty> It(NodeStructProperty->Struct); It; ++It)
			{
				const FProperty* Property = *It;
				if (Property && Property->GetFName() == InPinName)
				{
					return Property->GetMetaData(InKey);
				}
			}
		}
	}
	return MetaData;
}

void UAnimGraphNode_Base::AddSearchMetaDataInfo(TArray<struct FSearchTagDataPair>& OutTaggedMetaData) const
{
	Super::AddSearchMetaDataInfo(OutTaggedMetaData);

	for(const TPair<FName, FAnimGraphNodePropertyBinding>& BindingPair : PropertyBindings)
	{
		OutTaggedMetaData.Add(FSearchTagDataPair(FFindInBlueprintSearchTags::FiB_Name, FText::FromName(BindingPair.Key)));
		OutTaggedMetaData.Add(FSearchTagDataPair(LOCTEXT("Binding", "Binding"), BindingPair.Value.PathAsText));
	}
}

bool UAnimGraphNode_Base::IsPinExposedAndLinked(const FString& InPinName, const EEdGraphPinDirection InDirection) const
{
	UEdGraphPin* Pin = FindPin(InPinName, InDirection);
	return Pin != nullptr && Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0] != nullptr;
}

bool UAnimGraphNode_Base::IsPinExposedAndBound(const FString& InPinName, const EEdGraphPinDirection InDirection) const
{
	UEdGraphPin* Pin = FindPin(InPinName, InDirection);
	return Pin != nullptr && Pin->LinkedTo.Num() == 0 && PropertyBindings.Find(Pin->GetFName()) != nullptr;
}

bool UAnimGraphNode_Base::IsPinBindable(const UEdGraphPin* InPin) const
{
	if(const FProperty* PinProperty = GetPinProperty(InPin))
	{
		const int32 OptionalPinIndex = ShowPinForProperties.IndexOfByPredicate([PinProperty](const FOptionalPinFromProperty& InOptionalPin)
		{
			return PinProperty->GetFName() == InOptionalPin.PropertyName;
		});

		return OptionalPinIndex != INDEX_NONE;
	}

	return false;
}

FProperty* UAnimGraphNode_Base::GetPinProperty(const UEdGraphPin* InPin) const
{
	// Compare FName without number to make sure we catch array properties that are split into multiple pins
	FName ComparisonName = InPin->GetFName();
	ComparisonName.SetNumber(0);
	
	return GetFNodeType()->FindPropertyByName(ComparisonName);
}

void UAnimGraphNode_Base::PinConnectionListChanged(UEdGraphPin* Pin)
{
	if(Pin->LinkedTo.Num() > 0)
	{
		// If we have links, clear any bindings
		// Compare FName without number to make sure we catch array properties that are split into multiple pins
		FName ComparisonName = Pin->GetFName();
		ComparisonName.SetNumber(0);

		PropertyBindings.Remove(ComparisonName);
	}
}

void UAnimGraphNode_Base::AutowireNewNode(UEdGraphPin* FromPin)
{
	// Ensure the pin is valid, a pose pin, and has a single link
	if (FromPin && UAnimationGraphSchema::IsPosePin(FromPin->PinType))
	{
		auto FindFirstPosePinInDirection = [this](EEdGraphPinDirection Direction) -> UEdGraphPin*
		{
			UEdGraphPin** PinToConnectTo = Pins.FindByPredicate([Direction](UEdGraphPin* Pin) -> bool
            {
                return Pin && Pin->Direction == Direction && UAnimationGraphSchema::IsPosePin(Pin->PinType);
            });

			return PinToConnectTo ? *PinToConnectTo : nullptr;
		};
		
		// Get the linked pin, if valid, and ensure it iss also a pose pin
		UEdGraphPin* LinkedPin = FromPin->LinkedTo.Num() == 1 ? FromPin->LinkedTo[0] : nullptr;
		if (LinkedPin && UAnimationGraphSchema::IsPosePin(LinkedPin->PinType))
		{
			// Retrieve the first pin, of similar direction, from this node
			UEdGraphPin* PinToConnectTo = FindFirstPosePinInDirection(FromPin->Direction);
			if (PinToConnectTo)
			{
				ensure(GetSchema()->TryCreateConnection(LinkedPin, PinToConnectTo));
			}
		}

		// Link this node to the FromPin, so find the first pose pin of opposite direction on this node
		UEdGraphPin* PinToConnectTo = FindFirstPosePinInDirection(FromPin->Direction == EEdGraphPinDirection::EGPD_Input ? EEdGraphPinDirection::EGPD_Output : EEdGraphPinDirection::EGPD_Input);
		if (PinToConnectTo)
		{
			ensure(GetSchema()->TryCreateConnection(FromPin, PinToConnectTo));
		}
	}
}

TSharedRef<SWidget> UAnimGraphNode_Base::MakePropertyBindingWidget(const FAnimPropertyBindingWidgetArgs& InArgs)
{
	UAnimGraphNode_Base* FirstAnimGraphNode = InArgs.Nodes[0];
	UBlueprint* Blueprint = FirstAnimGraphNode->GetAnimBlueprint();
	const bool bMultiSelect = InArgs.Nodes.Num() > 1;
	
	if(IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
	{
		int32 PinArrayIndex = InArgs.PinName.GetNumber() - 1;
		const bool bIsArrayOrArrayElement = InArgs.PinProperty->IsA<FArrayProperty>();
		const bool bIsArrayElement = bIsArrayOrArrayElement && PinArrayIndex != INDEX_NONE;
		const bool bIsArray = bIsArrayOrArrayElement && PinArrayIndex == INDEX_NONE;
		
		FProperty* BindingProperty = bIsArrayElement ? CastField<FArrayProperty>(InArgs.PinProperty)->Inner : InArgs.PinProperty;

		auto OnCanBindProperty = [BindingProperty](FProperty* InProperty)
		{
			// Note: We support type promotion here
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
			return PropertyAccessEditor.GetPropertyCompatibility(InProperty, BindingProperty) != EPropertyAccessCompatibility::Incompatible;
		};

		auto OnCanBindFunction = [BindingProperty](UFunction* InFunction)
		{
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

			// Note: We support type promotion here
			return InFunction->NumParms == 1 
				&& PropertyAccessEditor.GetPropertyCompatibility(InFunction->GetReturnProperty(), BindingProperty) != EPropertyAccessCompatibility::Incompatible
				&& InFunction->HasAnyFunctionFlags(FUNC_BlueprintPure);
		};

		auto OnAddBinding = [InArgs, Blueprint, BindingProperty](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

			for(UAnimGraphNode_Base* AnimGraphNode : InArgs.Nodes)
			{
				AnimGraphNode->Modify();

				// Pins are exposed if we have a binding or not - and after running this we do.
				AnimGraphNode->SetPinVisibility(true, InArgs.OptionalPinIndex);

				// Need to break all pin links now we have a binding
				if(UEdGraphPin* Pin = AnimGraphNode->FindPin(InArgs.PinName))
				{
					Pin->BreakAllPinLinks();
				}

				const FFieldVariant& LeafField = InBindingChain.Last().Field;

				FAnimGraphNodePropertyBinding Binding;
				Binding.PropertyName = InArgs.PinName;
				if(InArgs.PinProperty->IsA<FArrayProperty>())
				{
					// Pull array index from the pin's FName if this is an array property
					Binding.ArrayIndex = InArgs.PinName.GetNumber() - 1;
				}
				PropertyAccessEditor.MakeStringPath(InBindingChain, Binding.PropertyPath);
				Binding.PathAsText = PropertyAccessEditor.MakeTextPath(Binding.PropertyPath);
				Binding.Type = LeafField.IsA<UFunction>() ? EAnimGraphNodePropertyBindingType::Function : EAnimGraphNodePropertyBindingType::Property;
				Binding.bIsBound = true;
				if(LeafField.IsA<FProperty>())
				{
					const FProperty* LeafProperty = LeafField.Get<FProperty>();
					if(LeafProperty)
					{
						if(PropertyAccessEditor.GetPropertyCompatibility(LeafProperty, BindingProperty) == EPropertyAccessCompatibility::Promotable)
						{
							Binding.bIsPromotion = true;
							Schema->ConvertPropertyToPinType(LeafProperty, Binding.PromotedPinType);
						}

						Schema->ConvertPropertyToPinType(LeafProperty, Binding.PinType);
					}
				}
				else if(LeafField.IsA<UFunction>())
				{
					const UFunction* LeafFunction = LeafField.Get<UFunction>();
					if(LeafFunction)
					{
						if(FProperty* ReturnProperty = LeafFunction->GetReturnProperty())
						{
							if(PropertyAccessEditor.GetPropertyCompatibility(ReturnProperty, BindingProperty) == EPropertyAccessCompatibility::Promotable)
							{
								Binding.bIsPromotion = true;
								Schema->ConvertPropertyToPinType(ReturnProperty, Binding.PromotedPinType);
							}

							Schema->ConvertPropertyToPinType(ReturnProperty, Binding.PinType);
						}
					}
				}
				AnimGraphNode->PropertyBindings.Add(InArgs.PinName, Binding);
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		};

		auto OnRemoveBinding = [InArgs, Blueprint](FName InPropertyName)
		{
			for(UAnimGraphNode_Base* AnimGraphNode : InArgs.Nodes)
			{
				AnimGraphNode->Modify();
				AnimGraphNode->PropertyBindings.Remove(InArgs.PinName);
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		};

		auto CanRemoveBinding = [InArgs](FName InPropertyName)
		{
			for(UAnimGraphNode_Base* AnimGraphNode : InArgs.Nodes)
			{
				if(AnimGraphNode->PropertyBindings.Contains(InArgs.PinName))
				{
					return true;
				}
			}

			return false;
		}; 

		enum class ECurrentValueType : int32
		{
			None,
			Pin,
			Binding,
			MultipleValues,
		};

		auto CurrentBindingText = [InArgs]()
		{
			ECurrentValueType CurrentValueType = ECurrentValueType::None;

			const FText MultipleValues = LOCTEXT("MultipleValuesLabel", "Multiple Values");
			const FText Bind = LOCTEXT("BindLabel", "Bind");
			const FText ExposedAsPin = LOCTEXT("ExposedAsPinLabel", "Pin");
			FText CurrentValue = Bind;

			auto SetAssignValue = [&CurrentValueType, &CurrentValue, &MultipleValues](const FText& InValue, ECurrentValueType InType)
			{
				if(CurrentValueType != ECurrentValueType::MultipleValues)
				{
					if(CurrentValueType == ECurrentValueType::None)
					{
						CurrentValueType = InType;
						CurrentValue = InValue;
					}
					else if(CurrentValueType == InType)
					{
						if(!CurrentValue.EqualTo(InValue))
						{
							CurrentValueType = ECurrentValueType::MultipleValues;
							CurrentValue = MultipleValues;
						}
					}
					else
					{
						CurrentValueType = ECurrentValueType::MultipleValues;
						CurrentValue = MultipleValues;
					}
				}
			};

			for(UAnimGraphNode_Base* AnimGraphNode : InArgs.Nodes)
			{
				if(FAnimGraphNodePropertyBinding* BindingPtr = AnimGraphNode->PropertyBindings.Find(InArgs.PinName))
				{
					SetAssignValue(BindingPtr->PathAsText, ECurrentValueType::Binding);
				}
				else
				{
					if(AnimGraphNode->ShowPinForProperties[InArgs.OptionalPinIndex].bShowPin)
					{
						SetAssignValue(InArgs.bOnGraphNode ? Bind : ExposedAsPin, ECurrentValueType::Pin);
					}
					else
					{
						SetAssignValue(Bind, ECurrentValueType::None);
					}
				}
			}

			return CurrentValue;
		};

		auto CurrentBindingToolTipText = [InArgs]()
		{
			ECurrentValueType CurrentValueType = ECurrentValueType::None;

			const FText MultipleValues = LOCTEXT("MultipleValuesToolTip", "Bindings Have Multiple Values");
			const FText ExposedAsPin = LOCTEXT("ExposedAsPinToolTip", "Exposed As a Pin on the Node");
			const FText BindValue = LOCTEXT("BindValueToolTip", "Bind This Value");
			FText CurrentValue;
			
			auto SetAssignValue = [&CurrentValueType, &CurrentValue, &MultipleValues](const FText& InValue, ECurrentValueType InType)
			{
				if(CurrentValueType != ECurrentValueType::MultipleValues)
				{
					if(CurrentValueType == ECurrentValueType::None)
					{
						CurrentValueType = InType;
						CurrentValue = InValue;
					}
					else if(CurrentValueType == InType)
					{
						if(!CurrentValue.EqualTo(InValue))
						{
							CurrentValueType = ECurrentValueType::MultipleValues;
							CurrentValue = MultipleValues;
						}
					}
					else
					{
						CurrentValueType = ECurrentValueType::MultipleValues;
						CurrentValue = MultipleValues;
					}
				}
			};

			for(UAnimGraphNode_Base* AnimGraphNode : InArgs.Nodes)
			{
				if(FAnimGraphNodePropertyBinding* BindingPtr = AnimGraphNode->PropertyBindings.Find(InArgs.PinName))
				{
					if(BindingPtr->PathAsText.IsEmpty())
					{
						SetAssignValue(BindValue, ECurrentValueType::Binding);
					}
					else
					{
						const FText& CompilationContext = BindingPtr->CompiledContext;
						const FText& CompilationContextDesc = BindingPtr->CompiledContextDesc;
						if(CompilationContext.IsEmpty() && CompilationContextDesc.IsEmpty())
						{
							SetAssignValue(FText::Format(LOCTEXT("BindingToolTipFormat", "Pin is bound to property '{0}'"), BindingPtr->PathAsText), ECurrentValueType::Binding);
						}
						else
						{
							SetAssignValue(FText::Format(LOCTEXT("BindingToolTipFormatWithDesc", "Pin is bound to property '{0}'\n{1}\n{2}"), BindingPtr->PathAsText, CompilationContext, CompilationContextDesc), ECurrentValueType::Binding);
						}
					}
				}
				else
				{
					if(AnimGraphNode->ShowPinForProperties[InArgs.OptionalPinIndex].bShowPin)
					{
						SetAssignValue(InArgs.bOnGraphNode ? BindValue : ExposedAsPin, ECurrentValueType::Pin);
					}
					else
					{
						SetAssignValue(BindValue, ECurrentValueType::None);
					}
				}
			}

			return CurrentValue;
		};

		auto CurrentBindingImage = [InArgs, bIsArrayElement]() -> const FSlateBrush*
		{
			static FName PropertyIcon(TEXT("Kismet.VariableList.TypeIcon"));
			static FName FunctionIcon(TEXT("GraphEditor.Function_16x"));

			EAnimGraphNodePropertyBindingType BindingType = EAnimGraphNodePropertyBindingType::None;
			for(UObject* OuterObject : InArgs.Nodes)
			{
				if(UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(OuterObject))
				{
					if(AnimGraphNode->ShowPinForProperties[InArgs.OptionalPinIndex].bShowPin)
					{
						BindingType = EAnimGraphNodePropertyBindingType::None;
						break;
					}
					else if(FAnimGraphNodePropertyBinding* BindingPtr = AnimGraphNode->PropertyBindings.Find(InArgs.PinName))
					{
						if(BindingType == EAnimGraphNodePropertyBindingType::None)
						{
							BindingType = BindingPtr->Type;
						}
						else if(BindingType != BindingPtr->Type)
						{
							BindingType = EAnimGraphNodePropertyBindingType::None;
							break;
						}
					}
					else if(BindingType != EAnimGraphNodePropertyBindingType::None)
					{
						BindingType = EAnimGraphNodePropertyBindingType::None;
						break;
					}
				}
			}

			if (BindingType == EAnimGraphNodePropertyBindingType::Function)
			{
				return FEditorStyle::GetBrush(FunctionIcon);
			}
			else
			{
				const UAnimationGraphSchema* AnimationGraphSchema = GetDefault<UAnimationGraphSchema>();
				FEdGraphPinType PinType;
				FProperty* PropertyToUse = bIsArrayElement ? CastFieldChecked<FArrayProperty>(InArgs.PinProperty)->Inner : InArgs.PinProperty;
				if(AnimationGraphSchema->ConvertPropertyToPinType(PropertyToUse, PinType))
				{
					return FBlueprintEditorUtils::GetIconFromPin(PinType, false);
				}
				else
				{
					return FEditorStyle::GetBrush(PropertyIcon);
				}
			}
		};

		auto CurrentBindingColor = [InArgs, BindingProperty]() -> FLinearColor
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

			FEdGraphPinType PinType;
			Schema->ConvertPropertyToPinType(BindingProperty, PinType);
			FLinearColor BindingColor = Schema->GetPinTypeColor(PinType);

			enum class EPromotionState
			{
				NotChecked,
				NotPromoted,
				Promoted,
			} Promotion = EPromotionState::NotChecked;

			for(UAnimGraphNode_Base* AnimGraphNode : InArgs.Nodes)
			{
				if(AnimGraphNode->ShowPinForProperties[InArgs.OptionalPinIndex].bShowPin)
				{
					if(Promotion == EPromotionState::NotChecked)
					{
						Promotion = EPromotionState::NotPromoted;
					}
					else if(Promotion == EPromotionState::Promoted)
					{
						BindingColor = FLinearColor::Gray;
						break;
					}
				}
				else if(FAnimGraphNodePropertyBinding* BindingPtr = AnimGraphNode->PropertyBindings.Find(InArgs.PinName))
				{
					if(Promotion == EPromotionState::NotChecked)
					{
						if(BindingPtr->bIsPromotion)
						{
							Promotion = EPromotionState::Promoted;
							BindingColor = Schema->GetPinTypeColor(BindingPtr->PromotedPinType);
						}
						else
						{
							Promotion = EPromotionState::NotPromoted;
						}
					}
					else
					{
						EPromotionState NewPromotion = BindingPtr->bIsPromotion ? EPromotionState::Promoted : EPromotionState::NotPromoted;
						if(Promotion != NewPromotion)
						{
							BindingColor = FLinearColor::Gray;
							break;
						}
					}
				}
			}

			return BindingColor;
		};

		auto AddMenuExtension = [InArgs, Blueprint](FMenuBuilder& InMenuBuilder)
		{
			InMenuBuilder.BeginSection("Pins", LOCTEXT("Pin", "Pin"));
			{
				InMenuBuilder.AddMenuEntry(
					LOCTEXT("ExposeAsPin", "Expose As Pin"),
					LOCTEXT("ExposeAsPinTooltip", "Show/hide this property as a pin on the node"),
					FSlateIcon("EditorStyle", "GraphEditor.PinIcon"),
					FUIAction(
						FExecuteAction::CreateLambda([InArgs, Blueprint]()
						{
							bool bHasBinding = false;

							// Comparison without name index to deal with arrays
							const FName ComparisonName = FName(InArgs.PinName, 0);
							
							for(UAnimGraphNode_Base* AnimGraphNode : InArgs.Nodes)
							{
								for(const TPair<FName, FAnimGraphNodePropertyBinding>& BindingPair : AnimGraphNode->PropertyBindings)
								{
									if(ComparisonName == FName(BindingPair.Key, 0))
									{
										bHasBinding = true;
										break;
									}
								}
							}

							{
								FScopedTransaction Transaction(LOCTEXT("PinExposure", "Pin Exposure"));

								// Switching from non-pin to pin, remove any bindings
								for(UAnimGraphNode_Base* AnimGraphNode : InArgs.Nodes)
								{
									AnimGraphNode->Modify();

									bool bVisible = AnimGraphNode->ShowPinForProperties[InArgs.OptionalPinIndex].bShowPin;
									AnimGraphNode->SetPinVisibility(!bVisible || bHasBinding, InArgs.OptionalPinIndex);

									// Remove all bindings that match the property, array or array elements
									for(auto It = AnimGraphNode->PropertyBindings.CreateIterator(); It; ++It)
									{
										if(ComparisonName == FName(It.Key(), 0))
										{
											It.RemoveCurrent();
										}
									}
								}

								FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
							}
						}),
						FCanExecuteAction(),
						FGetActionCheckState::CreateLambda([InArgs]()
						{
							bool bPinShown = false;
							bool bHasBinding = false;

							// Comparison without name index to deal with arrays
							const FName ComparisonName = FName(InArgs.PinName, 0);
							
							for(UAnimGraphNode_Base* AnimGraphNode : InArgs.Nodes)
							{
								for(const TPair<FName, FAnimGraphNodePropertyBinding>& BindingPair : AnimGraphNode->PropertyBindings)
								{
									if(ComparisonName == FName(BindingPair.Key, 0))
									{
										bHasBinding = true;
										break;
									}
								}
								
								bPinShown |= AnimGraphNode->ShowPinForProperties[InArgs.OptionalPinIndex].bShowPin;
							}

							// Pins are exposed if we have a binding or not, so treat as unchecked only if we have
							// no binding
							return bPinShown && !bHasBinding ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
					),
					NAME_None,
					EUserInterfaceActionType::Check
				);
			}
			InMenuBuilder.EndSection();
		};

		FPropertyBindingWidgetArgs Args;
		Args.Property = BindingProperty;
		Args.OnCanBindProperty = FOnCanBindProperty::CreateLambda(OnCanBindProperty);
		Args.OnCanBindFunction = FOnCanBindFunction::CreateLambda(OnCanBindFunction);
		Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([](UClass* InClass){ return true; });
		Args.OnAddBinding = FOnAddBinding::CreateLambda(OnAddBinding);
		Args.OnRemoveBinding = FOnRemoveBinding::CreateLambda(OnRemoveBinding);
		Args.OnCanRemoveBinding = FOnCanRemoveBinding::CreateLambda(CanRemoveBinding);
		Args.CurrentBindingText = MakeAttributeLambda(CurrentBindingText);
		Args.CurrentBindingToolTipText = MakeAttributeLambda(CurrentBindingToolTipText);
		Args.CurrentBindingImage = MakeAttributeLambda(CurrentBindingImage);
		Args.CurrentBindingColor = MakeAttributeLambda(CurrentBindingColor);
		Args.MenuExtender = MakeShared<FExtender>();
		Args.MenuExtender->AddMenuExtension("BindingActions", EExtensionHook::Before, nullptr, FMenuExtensionDelegate::CreateLambda(AddMenuExtension));

		IPropertyAccessBlueprintBinding::FContext BindingContext;
		BindingContext.Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(FirstAnimGraphNode);
		BindingContext.Graph = FirstAnimGraphNode->GetGraph();
		BindingContext.Node = FirstAnimGraphNode;
		BindingContext.Pin = FirstAnimGraphNode->FindPin(InArgs.PinName);

		auto OnSetPropertyAccessContextId = [InArgs, Blueprint](const FName& InContextId)
		{
			for(UAnimGraphNode_Base* AnimGraphNode : InArgs.Nodes)
			{
				if(FAnimGraphNodePropertyBinding* Binding = AnimGraphNode->PropertyBindings.Find(InArgs.PinName))
				{
					AnimGraphNode->Modify();
					Binding->ContextId = InContextId;
				}
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		};

		auto OnCanSetPropertyAccessContextId = [InArgs, FirstAnimGraphNode](const FName& InContextId)
		{
			return FirstAnimGraphNode->PropertyBindings.Find(InArgs.PinName) != nullptr;
		};
		
		auto OnGetPropertyAccessContextId = [InArgs]() -> FName
		{
			FName CurrentContext = NAME_None;
			for(UAnimGraphNode_Base* AnimGraphNode : InArgs.Nodes)
			{
				if(FAnimGraphNodePropertyBinding* Binding = AnimGraphNode->PropertyBindings.Find(InArgs.PinName))
				{
					if(CurrentContext != NAME_None && CurrentContext != Binding->ContextId)
					{
						return NAME_None;
					}
					else
					{
						CurrentContext = Binding->ContextId;
					}
				}
			}
		
			return CurrentContext;
		};
		
		IPropertyAccessBlueprintBinding::FBindingMenuArgs MenuArgs;
		MenuArgs.OnSetPropertyAccessContextId = FOnSetPropertyAccessContextId::CreateLambda(OnSetPropertyAccessContextId);
		MenuArgs.OnCanSetPropertyAccessContextId = FOnCanSetPropertyAccessContextId::CreateLambda(OnCanSetPropertyAccessContextId);
		MenuArgs.OnGetPropertyAccessContextId = FOnGetPropertyAccessContextId::CreateLambda(OnGetPropertyAccessContextId);
		
		// Add the binding menu extenders
		TArray<TSharedPtr<FExtender>> Extenders( { Args.MenuExtender } );
		for(IPropertyAccessBlueprintBinding* Binding : IModularFeatures::Get().GetModularFeatureImplementations<IPropertyAccessBlueprintBinding>("PropertyAccessBlueprintBinding"))
		{
			TSharedPtr<FExtender> BindingExtender = Binding->MakeBindingMenuExtender(BindingContext, MenuArgs);
			if(BindingExtender)
			{
				Extenders.Add(BindingExtender);
			}
		}
		
		if(Extenders.Num() > 0)
		{
			Args.MenuExtender = FExtender::Combine(Extenders);
		}

		Args.bAllowNewBindings = false;
		Args.bAllowArrayElementBindings = !bIsArray;
		Args.bAllowUObjectFunctions = !bIsArray;
		Args.bAllowStructFunctions = !bIsArray;

		IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

		const FTextBlockStyle& TextBlockStyle = FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("PropertyAccess.CompiledContext.Text");
		
		return
			SNew(SBox)
			.MaxDesiredWidth(200.0f)
			[
				SNew(SOverlay)
				+SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				[
					SNew(SBorder)
					.Padding(FMargin(1.0f, 3.0f, 1.0f, 1.0f))
					.Visibility(InArgs.bOnGraphNode ? EVisibility::Visible : EVisibility::Collapsed)
					.BorderImage(FEditorStyle::GetBrush("PropertyAccess.CompiledContext.Border"))
					.RenderTransform_Lambda([InArgs, FirstAnimGraphNode, &TextBlockStyle]()
					{
						const FAnimGraphNodePropertyBinding* BindingPtr = FirstAnimGraphNode->PropertyBindings.Find(InArgs.PinName);
						FVector2D TextSize(0.0f, 0.0f);
						if(BindingPtr)
						{
							const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
							TextSize = FontMeasureService->Measure(BindingPtr->CompiledContext, TextBlockStyle.Font);
						}
						return FSlateRenderTransform(FVector2D(0.0f, TextSize.Y - 1.0f));
					})	
					[
						SNew(STextBlock)
						.TextStyle(&TextBlockStyle)
						.Visibility_Lambda([InArgs, FirstAnimGraphNode, bMultiSelect]()
						{
							const FAnimGraphNodePropertyBinding* BindingPtr = FirstAnimGraphNode->PropertyBindings.Find(InArgs.PinName);
							return bMultiSelect || BindingPtr == nullptr || BindingPtr->CompiledContext.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
						})
						.Text_Lambda([InArgs, FirstAnimGraphNode]()
						{
							const FAnimGraphNodePropertyBinding* BindingPtr = FirstAnimGraphNode->PropertyBindings.Find(InArgs.PinName);
							return BindingPtr != nullptr ? BindingPtr->CompiledContext : FText::GetEmpty();
						})
					]
				]
				+SOverlay::Slot()
				[
					PropertyAccessEditor.MakePropertyBindingWidget(Blueprint, Args)
				]	
			];
	}
	else
	{
		return SNew(SSpacer);
	}
}

#undef LOCTEXT_NAMESPACE
