// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_PropertyAccess.h"
#include "Algo/Accumulate.h"
#include "EdGraphSchema_K2.h"
#include "PropertyAccess.h"
#include "Engine/Blueprint.h"
#include "FindInBlueprintManager.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "EditorCategoryUtils.h"
#include "BlueprintNodeSpawner.h"
#include "KismetCompilerMisc.h"
#include "EdGraphUtilities.h"
#include "KismetCompiler.h"
#include "K2Node_VariableGet.h"
#include "AnimationGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyAccessCompilerHandler.h"
#include "Modules/ModuleManager.h"
#include "IPropertyAccessEditor.h"
#include "IPropertyAccessCompiler.h"
#include "Features/IModularFeatures.h"
#include "IAnimBlueprintCompilationContext.h"

#define LOCTEXT_NAMESPACE "K2Node_PropertyAccess"

void UK2Node_PropertyAccess::CreateClassVariablesFromBlueprint(IAnimBlueprintVariableCreationContext& InCreationContext)
{
	GeneratedPropertyName = NAME_None;

	if(ResolvedPinType != FEdGraphPinType() && ResolvedPinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
	{
		// Create internal generated destination property
		if (FProperty* DestProperty = InCreationContext.CreateUniqueVariable(this, ResolvedPinType))
		{
			GeneratedPropertyName = DestProperty->GetFName();
		}
	}
}

void UK2Node_PropertyAccess::ExpandNode(FKismetCompilerContext& InCompilerContext, UEdGraph* InSourceGraph)
{
	ResolveLeafProperty();

	if(GeneratedPropertyName != NAME_None)
	{
		TArray<FString> DestPropertyPath;
		DestPropertyPath.Add(GeneratedPropertyName.ToString());

		// Create a copy event in the complied generated class
		TUniquePtr<IAnimBlueprintCompilationContext> CompilationContext = IAnimBlueprintCompilationContext::Get(InCompilerContext);
		FPropertyAccessCompilerHandler* PropertyAccessHandler = CompilationContext->GetHandler<FPropertyAccessCompilerHandler>("PropertyAccessCompilerHandler");
		check(PropertyAccessHandler);
		PropertyAccessHandler->AddCopy(Path, DestPropertyPath, EPropertyAccessBatchType::Batched, this);

		// Replace us with a get node
		UK2Node_VariableGet* VariableGetNode = InCompilerContext.SpawnIntermediateNode<UK2Node_VariableGet>(this, InSourceGraph);
		VariableGetNode->VariableReference.SetSelfMember(GeneratedPropertyName);
		VariableGetNode->AllocateDefaultPins();
		InCompilerContext.MessageLog.NotifyIntermediateObjectCreation(VariableGetNode, this);

		// Move pin links from Get node we are expanding, to the new pure one we've created
		UEdGraphPin* VariableValuePin = VariableGetNode->GetValuePin();
		check(VariableValuePin);
		InCompilerContext.MovePinLinksToIntermediate(*GetOutputPin(), *VariableValuePin);
	}
	else
	{
		InCompilerContext.MessageLog.Error(*LOCTEXT("IntermediateProperty_Error", "Intermediate property could not be created on @@").ToString(), this);
	}
}

void UK2Node_PropertyAccess::ResolveLeafProperty()
{
	if(UBlueprint* Blueprint = GetBlueprint())
	{
		ResolvedProperty = nullptr;
		ResolvedArrayIndex = INDEX_NONE;
		FProperty* PropertyToResolve = nullptr;
		if(IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
		{
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
			PropertyAccessEditor.ResolveLeafProperty(Blueprint->SkeletonGeneratedClass, Path, PropertyToResolve, ResolvedArrayIndex);
			ResolvedProperty = PropertyToResolve;
		}
	}
	else
	{
		ResolvedProperty = nullptr;
		ResolvedArrayIndex = INDEX_NONE;
	}
}

static FText MakeTextPath(const TArray<FString>& InPath)
{
	return FText::FromString(Algo::Accumulate(InPath, FString(), [](const FString& InResult, const FString& InSegment)
		{ 
			return InResult.IsEmpty() ? InSegment : (InResult + TEXT(".") + InSegment);
		}));	
}

void UK2Node_PropertyAccess::SetPath(const TArray<FString>& InPath)
{
	Path = InPath;
	TextPath = MakeTextPath(Path);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	ResolveLeafProperty();
	ReconstructNode();
}

void UK2Node_PropertyAccess::SetPath(TArray<FString>&& InPath)
{
	Path = MoveTemp(InPath);
	TextPath = MakeTextPath(Path);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	ResolveLeafProperty();
	ReconstructNode();
}

void UK2Node_PropertyAccess::ClearPath()
{
	Path.Empty();
	TextPath = FText();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	ResolvedProperty = nullptr;
	ResolvedArrayIndex = INDEX_NONE;
	ReconstructNode();
}

void UK2Node_PropertyAccess::AllocatePins(UEdGraphPin* InOldOutputPin)
{
	// Resolve leaf to try to get a valid property type for an output pin
	ResolveLeafProperty();

	if(UBlueprint* Blueprint = GetBlueprint())
	{
		UEdGraphPin* OutputPin = nullptr;
	
		if(InOldOutputPin != nullptr && InOldOutputPin->LinkedTo.Num() > 0)
		{
			// Use old output pin if we have one and it is connected
			OutputPin = CreatePin(EGPD_Output, InOldOutputPin->PinType, TEXT("Value"));
			ResolvedPinType = InOldOutputPin->PinType;
		}

		if(OutputPin == nullptr && ResolvedProperty.Get() != nullptr)
		{
			// Otherwise use the resolved property
			FProperty* PropertyToUse = ResolvedProperty.Get();
			if(FArrayProperty* ArrayProperty = CastField<FArrayProperty>(PropertyToUse))
			{
				if(ResolvedArrayIndex != INDEX_NONE)
				{
					PropertyToUse = ArrayProperty->Inner;
				}
			}

			// Try to create a pin for the property
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
			if(K2Schema->ConvertPropertyToPinType(PropertyToUse, ResolvedPinType))
			{
				OutputPin = CreatePin(EGPD_Output, ResolvedPinType, TEXT("Value"));
			}
		}

		if(OutputPin == nullptr)
		{
			// Cant resolve a type from the path, make a wildcard pin to begin with
			OutputPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, TEXT("Value"));
			ResolvedPinType = OutputPin->PinType;
		}
	}
}

void UK2Node_PropertyAccess::AllocateDefaultPins()
{
	AllocatePins();
}

void UK2Node_PropertyAccess::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	// First find the old output pin, if any
	UEdGraphPin* OldOutputPin = nullptr;
	if(UEdGraphPin** OldOutputPinPtr = OldPins.FindByPredicate([](UEdGraphPin* InPin){ return InPin->PinName == TEXT("Value"); }))
	{
		OldOutputPin = *OldOutputPinPtr;
	}

	AllocatePins(OldOutputPin);

	RestoreSplitPins(OldPins);
}

FText UK2Node_PropertyAccess::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("PropertyAccess", "Property Access");
}

void UK2Node_PropertyAccess::AddSearchMetaDataInfo(TArray<FSearchTagDataPair>& OutTaggedMetaData) const
{
	auto MakeTextPath = [this]()
	{
		if(Path.Num())
		{
			return FText::FromString(Algo::Accumulate(Path, FString(), [](const FString& InResult, const FString& InSegment)
				{ 
					return InResult.IsEmpty() ? InSegment : (InResult + TEXT(".") + InSegment);
				}));	
		}
		else
		{
			return LOCTEXT("None", "None");
		}
	};

	OutTaggedMetaData.Emplace(LOCTEXT("PropertyAccess", "Property Access"), MakeTextPath());
}

void UK2Node_PropertyAccess::PinConnectionListChanged(UEdGraphPin* Pin)
{
	if(Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard && Pin->LinkedTo.Num() > 0)
	{
		Pin->PinType = ResolvedPinType = Pin->LinkedTo[0]->PinType;
	}
}

void UK2Node_PropertyAccess::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* NodeClass = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(NodeClass))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(NodeClass);
		check(NodeSpawner);
		ActionRegistrar.AddBlueprintAction(NodeClass, NodeSpawner);
	}
}

FText UK2Node_PropertyAccess::GetMenuCategory() const
{
	return FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::Variables);
}

bool UK2Node_PropertyAccess::IsCompatibleWithGraph(UEdGraph const* TargetGraph) const
{
	// Only allow placement in anim graphs, for now. If this changes then we need to address the 
	// dependency on the anim BP compiler's subsystems
	return TargetGraph->IsA<UAnimationGraph>();
}

#undef LOCTEXT_NAMESPACE