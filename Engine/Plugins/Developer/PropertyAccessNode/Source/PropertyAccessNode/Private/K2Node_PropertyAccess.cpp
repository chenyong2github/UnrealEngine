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
#include "KismetCompiler.h"
#include "K2Node_VariableGet.h"
#include "AnimationGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AnimBlueprintExtension_PropertyAccess.h"
#include "IPropertyAccessEditor.h"
#include "IPropertyAccessCompiler.h"
#include "Features/IModularFeatures.h"
#include "IAnimBlueprintCompilationContext.h"
#include "IPropertyAccessBlueprintBinding.h"
#include "Animation/AnimBlueprint.h"

#define LOCTEXT_NAMESPACE "K2Node_PropertyAccess"

void UK2Node_PropertyAccess::CreateClassVariablesFromBlueprint(IAnimBlueprintVariableCreationContext& InCreationContext)
{
	GeneratedPropertyName = NAME_None;

	const bool bRequiresCachedVariable = !bWasResolvedThreadSafe || UAnimBlueprintExtension_PropertyAccess::ContextRequiresCachedVariable(ContextId);
	
	if(ResolvedPinType != FEdGraphPinType() && ResolvedPinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard && bRequiresCachedVariable)
	{
		// Create internal generated destination property (only if we were not identified as thread safe)
		if(FProperty* DestProperty = InCreationContext.CreateUniqueVariable(this, ResolvedPinType))
		{
			GeneratedPropertyName = DestProperty->GetFName();
		}
	}
}

void UK2Node_PropertyAccess::ExpandNode(FKismetCompilerContext& InCompilerContext, UEdGraph* InSourceGraph)
{
	ResolvePropertyAccess();

	UK2Node_PropertyAccess* OriginalNode = CastChecked<UK2Node_PropertyAccess>(InCompilerContext.MessageLog.FindSourceObject(this));
	OriginalNode->CompiledContext = FText();
	OriginalNode->CompiledContextDesc = FText();

	TUniquePtr<IAnimBlueprintCompilationContext> CompilationContext = IAnimBlueprintCompilationContext::Get(InCompilerContext);
	UAnimBlueprintExtension_PropertyAccess* PropertyAccessExtension = UAnimBlueprintExtension::GetExtension<UAnimBlueprintExtension_PropertyAccess>(CastChecked<UAnimBlueprint>(GetBlueprint()));
	check(PropertyAccessExtension);
	
	if(GeneratedPropertyName != NAME_None)
	{
		TArray<FString> DestPropertyPath;

		// We are using an intermediate object-level property (as we need to call this access and cache its result
		// until later) 
		DestPropertyPath.Add(GeneratedPropertyName.ToString());

		// Create a copy event in the complied generated class
		FPropertyAccessHandle Handle = PropertyAccessExtension->AddCopy(Path, DestPropertyPath, ContextId, this);
		
		PostLibraryCompiledHandle = PropertyAccessExtension->OnPostLibraryCompiled().AddLambda([this, OriginalNode, PropertyAccessExtension, Handle](IAnimBlueprintCompilationBracketContext& /*InCompilationContext*/, IAnimBlueprintGeneratedClassCompiledData& /*OutCompiledData*/)
		{
			const FCompiledPropertyAccessHandle CompiledHandle = PropertyAccessExtension->GetCompiledHandle(Handle);
			if(CompiledHandle.IsValid())
			{
				OriginalNode->CompiledContext = UAnimBlueprintExtension_PropertyAccess::GetCompiledHandleContext(CompiledHandle);
				OriginalNode->CompiledContextDesc = UAnimBlueprintExtension_PropertyAccess::GetCompiledHandleContextDesc(CompiledHandle);
			}
			PropertyAccessExtension->OnPostLibraryCompiled().Remove(PostLibraryCompiledHandle);
		});
		
		// Replace us with a get node
		UK2Node_VariableGet* VariableGetNode = InCompilerContext.SpawnIntermediateNode<UK2Node_VariableGet>(this, InSourceGraph);
		VariableGetNode->VariableReference.SetSelfMember(GeneratedPropertyName);
		VariableGetNode->AllocateDefaultPins();

		// Move pin links from Get node we are expanding, to the new pure one we've created
		UEdGraphPin* VariableValuePin = VariableGetNode->GetValuePin();
		check(VariableValuePin);
		InCompilerContext.MovePinLinksToIntermediate(*GetOutputPin(), *VariableValuePin);
	}
	else
	{
		const bool bRequiresCachedVariable = !bWasResolvedThreadSafe || UAnimBlueprintExtension_PropertyAccess::ContextRequiresCachedVariable(ContextId);
		check(!bRequiresCachedVariable);

		UEnum* EnumClass = StaticEnum<EAnimPropertyAccessCallSite>();
		check(EnumClass != nullptr);

		OriginalNode->CompiledContext = EnumClass->GetDisplayNameTextByValue((int32)EAnimPropertyAccessCallSite::WorkerThread_Unbatched);
		OriginalNode->CompiledContextDesc = EnumClass->GetToolTipTextByIndex((int32)EAnimPropertyAccessCallSite::WorkerThread_Unbatched);
		
		PropertyAccessExtension->ExpandPropertyAccess(InCompilerContext, Path, InSourceGraph, GetOutputPin());
	}
}

void UK2Node_PropertyAccess::ResolvePropertyAccess()
{
	if(UBlueprint* Blueprint = GetBlueprint())
	{
		ResolvedProperty = nullptr;
		ResolvedArrayIndex = INDEX_NONE;
		FProperty* PropertyToResolve = nullptr;
		if(IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
		{
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
			FPropertyAccessResolveResult Result = PropertyAccessEditor.ResolvePropertyAccess(Blueprint->SkeletonGeneratedClass, Path, PropertyToResolve, ResolvedArrayIndex);
			bWasResolvedThreadSafe = Result.bIsThreadSafe;
			ResolvedProperty = PropertyToResolve;
		}
	}
	else
	{
		ResolvedProperty = nullptr;
		ResolvedArrayIndex = INDEX_NONE;
	}
}

void UK2Node_PropertyAccess::SetPath(const TArray<FString>& InPath)
{
	Path = InPath;

	IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
	TextPath = PropertyAccessEditor.MakeTextPath(Path);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	ResolvePropertyAccess();
	ReconstructNode();
}

void UK2Node_PropertyAccess::SetPath(TArray<FString>&& InPath)
{
	Path = MoveTemp(InPath);
	
	IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
	TextPath = PropertyAccessEditor.MakeTextPath(Path);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	ResolvePropertyAccess();
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
	ResolvePropertyAccess();

	if(UBlueprint* Blueprint = GetBlueprint())
	{
		UEdGraphPin* OutputPin = nullptr;
	
		if(InOldOutputPin != nullptr && InOldOutputPin->LinkedTo.Num() > 0 && InOldOutputPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
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
	return !TextPath.IsEmpty() ? TextPath : LOCTEXT("PropertyAccess", "Property Access");
}

void UK2Node_PropertyAccess::AddSearchMetaDataInfo(TArray<FSearchTagDataPair>& OutTaggedMetaData) const
{
	Super::AddSearchMetaDataInfo(OutTaggedMetaData);

	if(!TextPath.IsEmpty())
	{
		OutTaggedMetaData.Emplace(LOCTEXT("PropertyAccess", "Property Access"), TextPath);
	}
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
	IPropertyAccessBlueprintBinding::FContext BindingContext;
	BindingContext.Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
	BindingContext.Graph = TargetGraph;
	BindingContext.Node = this;
	BindingContext.Pin = FindPin(TEXT("Value"));
	
	// Check any property access blueprint bindings we might have registered
	for(IPropertyAccessBlueprintBinding* Binding : IModularFeatures::Get().GetModularFeatureImplementations<IPropertyAccessBlueprintBinding>("PropertyAccessBlueprintBinding"))
	{
		if(Binding->CanBindToContext(BindingContext))
		{
			return true;
		}
	}

	return false;
}

FText UK2Node_PropertyAccess::GetTooltipText() const
{
	return LOCTEXT("PropertyAccessTooltip", "Accesses properties according to property path");
}

bool UK2Node_PropertyAccess::HasResolvedPinType() const
{
	return ResolvedPinType != FEdGraphPinType() && ResolvedPinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard;
}

#undef LOCTEXT_NAMESPACE