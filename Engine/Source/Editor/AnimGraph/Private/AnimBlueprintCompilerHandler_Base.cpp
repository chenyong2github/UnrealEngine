// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintCompilerHandler_Base.h"
#include "AnimGraphNode_Base.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_CustomProperty.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableSet.h"
#include "K2Node_StructMemberSet.h"
#include "K2Node_StructMemberGet.h"
#include "K2Node_CallArrayFunction.h"
#include "Kismet/KismetArrayLibrary.h"
#include "K2Node_Knot.h"
#include "String/ParseTokens.h"
#include "K2Node_VariableGet.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "Kismet/KismetMathLibrary.h"
#include "K2Node_TransitionRuleGetter.h"
#include "Algo/Accumulate.h"
#include "K2Node_GetArrayItem.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyAccessCompilerHandler.h"
#include "IPropertyAccessEditor.h"
#include "IPropertyAccessCompiler.h"
#include "IAnimBlueprintGeneratedClassCompiledData.h"
#include "IAnimBlueprintCompilerCreationContext.h"
#include "IAnimBlueprintCompilationContext.h"
#include "IAnimBlueprintCopyTermDefaultsContext.h"
#include "IAnimBlueprintPostExpansionStepContext.h"
#include "IAnimBlueprintCompilationBracketContext.h"

#define LOCTEXT_NAMESPACE "AnimBlueprintCompilerHandler_Base"

FAnimBlueprintCompilerHandler_Base::FAnimBlueprintCompilerHandler_Base(IAnimBlueprintCompilerCreationContext& InCreationContext)
{
	InCreationContext.OnStartCompilingClass().AddRaw(this, &FAnimBlueprintCompilerHandler_Base::StartCompilingClass);
	InCreationContext.OnFinishCompilingClass().AddRaw(this, &FAnimBlueprintCompilerHandler_Base::FinishCompilingClass);
	InCreationContext.OnPostExpansionStep().AddRaw(this, &FAnimBlueprintCompilerHandler_Base::PostExpansionStep);
	InCreationContext.OnCopyTermDefaultsToDefaultObject().AddRaw(this, &FAnimBlueprintCompilerHandler_Base::CopyTermDefaultsToDefaultObject);
}

void FAnimBlueprintCompilerHandler_Base::CopyTermDefaultsToDefaultObject(UObject* InDefaultObject, IAnimBlueprintCopyTermDefaultsContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	UAnimInstance* DefaultAnimInstance = Cast<UAnimInstance>(InDefaultObject);

	if(DefaultAnimInstance)
	{
		// And patch in constant values that don't need to be re-evaluated every frame
		for (auto LiteralLinkIt = ValidAnimNodePinConstants.CreateIterator(); LiteralLinkIt; ++LiteralLinkIt)
		{
			FEffectiveConstantRecord& ConstantRecord = *LiteralLinkIt;

			//const FString ArrayClause = (ConstantRecord.ArrayIndex != INDEX_NONE) ? FString::Printf(TEXT("[%d]"), ConstantRecord.ArrayIndex) : FString();
			//const FString ValueClause = ConstantRecord.LiteralSourcePin->GetDefaultAsString();
			//GetMessageLog().Note(*FString::Printf(TEXT("Want to set %s.%s%s to %s"), *ConstantRecord.NodeVariableProperty->GetName(), *ConstantRecord.ConstantProperty->GetName(), *ArrayClause, *ValueClause));

			if (!ConstantRecord.Apply(DefaultAnimInstance))
			{
				InCompilationContext.GetMessageLog().Error(TEXT("ICE: Failed to push literal value from @@ into CDO"), ConstantRecord.LiteralSourcePin);
			}
		}

		for (const FEffectiveConstantRecord& ConstantRecord : ValidAnimNodePinConstants)
		{
			UAnimGraphNode_Base* Node = CastChecked<UAnimGraphNode_Base>(ConstantRecord.LiteralSourcePin->GetOwningNode());
			UAnimGraphNode_Base* TrueNode = InCompilationContext.GetMessageLog().FindSourceObjectTypeChecked<UAnimGraphNode_Base>(Node);
			TrueNode->BlueprintUsage = EBlueprintUsage::DoesNotUseBlueprint;
		}

		for(const FEvaluationHandlerRecord& EvaluationHandler : ValidEvaluationHandlerList)
		{
			if(EvaluationHandler.EvaluationHandlerIdx != INDEX_NONE && EvaluationHandler.ServicedProperties.Num() > 0)
			{
				const FAnimNodeSinglePropertyHandler& Handler = EvaluationHandler.ServicedProperties.CreateConstIterator()->Value;
				check(Handler.CopyRecords.Num() > 0);
				if(Handler.CopyRecords[0].DestPin != nullptr)
				{
					UAnimGraphNode_Base* Node = CastChecked<UAnimGraphNode_Base>(Handler.CopyRecords[0].DestPin->GetOwningNode());
					UAnimGraphNode_Base* TrueNode = InCompilationContext.GetMessageLog().FindSourceObjectTypeChecked<UAnimGraphNode_Base>(Node);	

					const FExposedValueHandler& ValueHandler = OutCompiledData.GetExposedValueHandlers()[ EvaluationHandler.EvaluationHandlerIdx ];
					TrueNode->BlueprintUsage = ValueHandler.BoundFunction != NAME_None ? EBlueprintUsage::UsesBlueprint : EBlueprintUsage::DoesNotUseBlueprint; 

#if WITH_EDITORONLY_DATA // ANIMINST_PostCompileValidation
					const bool bWarnAboutBlueprintUsage = InCompilationContext.GetAnimBlueprint()->bWarnAboutBlueprintUsage || DefaultAnimInstance->PCV_ShouldWarnAboutNodesNotUsingFastPath();
					const bool bNotifyAboutBlueprintUsage = DefaultAnimInstance->PCV_ShouldNotifyAboutNodesNotUsingFastPath();
#else
					const bool bWarnAboutBlueprintUsage = InCompilationContext.GetAnimBlueprint()->bWarnAboutBlueprintUsage;
					const bool bNotifyAboutBlueprintUsage = false;
#endif
					if ((TrueNode->BlueprintUsage == EBlueprintUsage::UsesBlueprint) && (bWarnAboutBlueprintUsage || bNotifyAboutBlueprintUsage))
					{
						const FString MessageString = LOCTEXT("BlueprintUsageWarning", "Node @@ uses Blueprint to update its values, access member variables directly or use a constant value for better performance.").ToString();
						if (bWarnAboutBlueprintUsage)
						{
							InCompilationContext.GetMessageLog().Warning(*MessageString, Node);
						}
						else
						{
							InCompilationContext.GetMessageLog().Note(*MessageString, Node);
						}
					}
				}
			}
		}
	}
}

void FAnimBlueprintCompilerHandler_Base::PostExpansionStep(const UEdGraph* InGraph, IAnimBlueprintPostExpansionStepContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	UEdGraph* ConsolidatedEventGraph = InCompilationContext.GetConsolidatedEventGraph();
	if(InGraph == ConsolidatedEventGraph)
	{
		FPropertyAccessCompilerHandler* PropertyAccessHandler = InCompilationContext.GetHandler<FPropertyAccessCompilerHandler>("PropertyAccessCompilerHandler");

		// Skip fast-path generation if the property access system is unavailable.
		// Disable fast-path generation for nativized anim BPs, we dont run the VM anyways and 
		// the property names are 'decorated' by the backend, so records dont match.
		// Note that this wont prevent property access 'binding' copy records from running, only
		// old-style 'fast-path' records that are derived from BP pure chains
		if(PropertyAccessHandler != nullptr && !InCompilationContext.GetCompileOptions().DoesRequireCppCodeGeneration())
		{
			for(FEvaluationHandlerRecord& HandlerRecord : ValidEvaluationHandlerList)
			{
				HandlerRecord.BuildFastPathCopyRecords(*this, InCompilationContext);

				if(HandlerRecord.IsFastPath())
				{
					for(UEdGraphNode* CustomEventNode : HandlerRecord.CustomEventNodes)
					{
						// Remove custom event nodes as we dont need it any more
						ConsolidatedEventGraph->RemoveNode(CustomEventNode);
					}
				}
			}
		}

		// Cull out all anim nodes as they dont contribute to execution at all
		for (int32 NodeIndex = 0; NodeIndex < ConsolidatedEventGraph->Nodes.Num(); ++NodeIndex)
		{
			if(UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(ConsolidatedEventGraph->Nodes[NodeIndex]))
			{
				Node->BreakAllNodeLinks();
				ConsolidatedEventGraph->Nodes.RemoveAtSwap(NodeIndex);
				--NodeIndex;
			}
		}
	}
}

void FAnimBlueprintCompilerHandler_Base::StartCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	FPropertyAccessCompilerHandler* PropertyAccessHandler = InCompilationContext.GetHandler<FPropertyAccessCompilerHandler>("PropertyAccessCompilerHandler");
	if(PropertyAccessHandler)
	{
		if(!PreLibraryCompiledDelegateHandle.IsValid())
		{
			PreLibraryCompiledDelegateHandle = PropertyAccessHandler->OnPreLibraryCompiled().AddLambda([this, PropertyAccessHandler, InClass]()
			{
				if(IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
				{
					IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

					// Build the classes property access library before the library is compiled
					for(FEvaluationHandlerRecord& HandlerRecord : ValidEvaluationHandlerList)
					{
						for(TPair<FName, FAnimNodeSinglePropertyHandler>& PropertyHandler : HandlerRecord.ServicedProperties)
						{
							for(FPropertyCopyRecord& Record : PropertyHandler.Value.CopyRecords)
							{
								if(Record.IsFastPath())
								{
									// Check if the resolved copy
									FProperty* LeafProperty = nullptr;
									int32 ArrayIndex = INDEX_NONE;
									EPropertyAccessResolveResult Result = PropertyAccessEditor.ResolveLeafProperty(InClass, Record.SourcePropertyPath, LeafProperty, ArrayIndex);

									// Batch all external accesses, we cant call them safely from a worker thread.
									Record.LibraryBatchType = Result == EPropertyAccessResolveResult::SucceededExternal ? EPropertyAccessBatchType::Batched : EPropertyAccessBatchType::Unbatched;
									Record.LibraryCopyIndex = PropertyAccessHandler->AddCopy(Record.SourcePropertyPath, Record.DestPropertyPath, Record.LibraryBatchType, HandlerRecord.AnimGraphNode);
								}
							}
						}
					}
				}
			});
		}

		if(!PostLibraryCompiledDelegateHandle.IsValid())
		{
			PostLibraryCompiledDelegateHandle = PropertyAccessHandler->OnPostLibraryCompiled().AddLambda([this, PropertyAccessHandler](IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
			{
				TArray<FExposedValueHandler>& ExposedValueHandlers = OutCompiledData.GetExposedValueHandlers();

				for(FEvaluationHandlerRecord& HandlerRecord : ValidEvaluationHandlerList)
				{
					// Map global copy index to batched indices
					for(TPair<FName, FAnimNodeSinglePropertyHandler>& PropertyHandler : HandlerRecord.ServicedProperties)
					{
						for(FPropertyCopyRecord& CopyRecord : PropertyHandler.Value.CopyRecords)
						{
							if(CopyRecord.IsFastPath())
							{
								CopyRecord.LibraryCopyIndex = PropertyAccessHandler->MapCopyIndex(CopyRecord.LibraryCopyIndex);
							}
						}
					}

					// Patch either fast-path copy records or generated function names into the class
					HandlerRecord.EvaluationHandlerIdx = ExposedValueHandlers.Num();
					FExposedValueHandler& ExposedValueHandler = ExposedValueHandlers.AddDefaulted_GetRef();
					HandlerRecord.PatchFunctionNameAndCopyRecordsInto(ExposedValueHandler);
				}
			});
		}
	}
}

void FAnimBlueprintCompilerHandler_Base::FinishCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	FPropertyAccessCompilerHandler* PropertyAccessHandler = InCompilationContext.GetHandler<FPropertyAccessCompilerHandler>("PropertyAccessCompilerHandler");
	if(PropertyAccessHandler == nullptr)
	{
		TArray<FExposedValueHandler>& ExposedValueHandlers = OutCompiledData.GetExposedValueHandlers();

		// Without the property access system we need to patch generated function names here
		for(FEvaluationHandlerRecord& HandlerRecord : ValidEvaluationHandlerList)
		{
			HandlerRecord.EvaluationHandlerIdx = ExposedValueHandlers.Num();
			FExposedValueHandler& ExposedValueHandler = ExposedValueHandlers.AddDefaulted_GetRef();
			HandlerRecord.PatchFunctionNameAndCopyRecordsInto(ExposedValueHandler);
		}
	}
}

void FAnimBlueprintCompilerHandler_Base::AddStructEvalHandlers(UAnimGraphNode_Base* InNode, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	const UAnimationGraphSchema* AnimGraphDefaultSchema = GetDefault<UAnimationGraphSchema>();

	FEvaluationHandlerRecord& EvalHandler = PerNodeStructEvalHandlers.Add(InNode);

	FStructProperty* NodeProperty = CastFieldChecked<FStructProperty>(InCompilationContext.GetAllocatedPropertiesByNode().FindChecked(InNode));

	for (auto SourcePinIt = InNode->Pins.CreateIterator(); SourcePinIt; ++SourcePinIt)
	{
		UEdGraphPin* SourcePin = *SourcePinIt;
		bool bConsumed = false;

		// Register pose links for future use
		if ((SourcePin->Direction == EGPD_Input) && (AnimGraphDefaultSchema->IsPosePin(SourcePin->PinType)))
		{
			// Input pose pin, going to need to be linked up
			FPoseLinkMappingRecord LinkRecord = InNode->GetLinkIDLocation(NodeProperty->Struct, SourcePin);
			if (LinkRecord.IsValid())
			{
				InCompilationContext.AddPoseLinkMappingRecord(LinkRecord);
				bConsumed = true;
			}
		}
		else
		{
			// The property source for our data, either the struct property for an anim node, or the
			// owning anim instance if using a linked instance node.
			FProperty* SourcePinProperty = nullptr;
			int32 SourceArrayIndex = INDEX_NONE;
			bool bInstancePropertyExists = false;

			// We have special handling below if we're targeting a linked instance instead of our own instance properties
			UAnimGraphNode_CustomProperty* CustomPropertyNode = Cast<UAnimGraphNode_CustomProperty>(InNode);

			InNode->GetPinAssociatedProperty(NodeProperty->Struct, SourcePin, /*out*/ SourcePinProperty, /*out*/ SourceArrayIndex);

			// Does this pin have an associated evaluation handler?
			if(!SourcePinProperty && CustomPropertyNode)
			{
				// Custom property nodes use instance properties not node properties as they aren't UObjects
				// and we can't store non-native properties there
				CustomPropertyNode->GetInstancePinProperty(InCompilationContext, SourcePin, SourcePinProperty);
				bInstancePropertyExists = true;
			}
			
			if (SourcePinProperty != NULL)
			{
				if (SourcePin->LinkedTo.Num() == 0)
				{
					// Literal that can be pushed into the CDO instead of re-evaluated every frame
					new (ValidAnimNodePinConstants) FEffectiveConstantRecord(NodeProperty, SourcePin, SourcePinProperty, SourceArrayIndex);
					bConsumed = true;
				}
				else
				{
					// Dynamic value that needs to be wired up and evaluated each frame
					const FString& EvaluationHandlerStr = SourcePinProperty->GetMetaData(AnimGraphDefaultSchema->NAME_OnEvaluate);
					FName EvaluationHandlerName(*EvaluationHandlerStr);
					if (EvaluationHandlerName != NAME_None)
					{
						// warn that NAME_OnEvaluate is deprecated:
						InCompilationContext.GetMessageLog().Warning(*LOCTEXT("OnEvaluateDeprecated", "OnEvaluate meta data is deprecated, found on @@").ToString(), SourcePinProperty);
					}
					
					ensure(EvalHandler.NodeVariableProperty == nullptr || EvalHandler.NodeVariableProperty == NodeProperty);
					EvalHandler.AnimGraphNode = InNode;
					EvalHandler.NodeVariableProperty = NodeProperty;
					EvalHandler.RegisterPin(SourcePin, SourcePinProperty, SourceArrayIndex);
					// if it's not instance property, ensure we mark it
					EvalHandler.bServicesNodeProperties = EvalHandler.bServicesNodeProperties | !bInstancePropertyExists;

					if (CustomPropertyNode)
					{
						EvalHandler.bServicesInstanceProperties = EvalHandler.bServicesInstanceProperties | bInstancePropertyExists;

						FAnimNodeSinglePropertyHandler* SinglePropHandler = EvalHandler.ServicedProperties.Find(SourcePinProperty->GetFName());
						check(SinglePropHandler); // Should have been added in RegisterPin

						// Flag that the target property is actually on the instance class and not the node
						SinglePropHandler->bInstanceIsTarget = bInstancePropertyExists;
					}

					bConsumed = true;
				}

				UEdGraphPin* TrueSourcePin = InCompilationContext.GetMessageLog().FindSourcePin(SourcePin);
				if (TrueSourcePin)
				{
					OutCompiledData.GetBlueprintDebugData().RegisterClassPropertyAssociation(TrueSourcePin, SourcePinProperty);
				}
			}
		}

		if (!bConsumed && (SourcePin->Direction == EGPD_Input))
		{
			//@TODO: ANIMREFACTOR: It's probably OK to have certain pins ignored eventually, but this is very helpful during development
			InCompilationContext.GetMessageLog().Note(TEXT("@@ was visible but ignored"), SourcePin);
		}
	}

	// Add any property bindings
	for(const TPair<FName, FAnimGraphNodePropertyBinding>& PropertyBinding : InNode->PropertyBindings)
	{
		if(PropertyBinding.Value.bIsBound)
		{
			EvalHandler.AnimGraphNode = InNode;
			EvalHandler.NodeVariableProperty = NodeProperty;
			EvalHandler.bServicesNodeProperties = true;

			if (FProperty* Property = FindFProperty<FProperty>(NodeProperty->Struct, PropertyBinding.Key))
			{
				EvalHandler.RegisterPropertyBinding(Property, PropertyBinding.Value);
			}
			else
			{
				InCompilationContext.GetMessageLog().Warning(*FString::Printf(TEXT("ICE: @@ Failed to find a property '%s'"), *PropertyBinding.Key.ToString()), InNode);
			}
		}
	}
}

void FAnimBlueprintCompilerHandler_Base::CreateEvaluationHandlerForNode(IAnimBlueprintCompilationContext& InCompilationContext, UAnimGraphNode_Base* InNode)
{
	if(FEvaluationHandlerRecord* RecordPtr = PerNodeStructEvalHandlers.Find(InNode))
	{
		// Generate a new event to update the value of these properties
		FEvaluationHandlerRecord& Record = *RecordPtr;

		if (Record.NodeVariableProperty)
		{
			CreateEvaluationHandler(InCompilationContext, InNode, Record);

			int32 NewIndex = ValidEvaluationHandlerList.Add(Record);
			ValidEvaluationHandlerMap.Add(InNode, NewIndex);
		}
	}
}

void FAnimBlueprintCompilerHandler_Base::CreateEvaluationHandler(IAnimBlueprintCompilationContext& InCompilationContext, UAnimGraphNode_Base* InNode, FEvaluationHandlerRecord& Record)
{
	// Shouldn't create a handler if there is nothing to work with
	check(Record.ServicedProperties.Num() > 0);
	check(Record.NodeVariableProperty != NULL);

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	const UAnimationGraphSchema* AnimGraphDefaultSchema = GetDefault<UAnimationGraphSchema>();

	// Use the node GUID for a stable name across compiles
	FString FunctionName = FString::Printf(TEXT("%s_%s_%s_%s"), *AnimGraphDefaultSchema->DefaultEvaluationHandlerName.ToString(), *InNode->GetOuter()->GetName(), *InNode->GetClass()->GetName(), *InNode->NodeGuid.ToString());
	Record.HandlerFunctionName = FName(*FunctionName);

	// check function name isnt already used (data exists that can contain duplicate GUIDs) and apply a numeric extension until it is unique
	int32 ExtensionIndex = 0;
	FName* ExistingName = HandlerFunctionNames.Find(Record.HandlerFunctionName);
	while (ExistingName != nullptr)
	{
		FunctionName = FString::Printf(TEXT("%s_%s_%s_%s_%d"), *AnimGraphDefaultSchema->DefaultEvaluationHandlerName.ToString(), *InNode->GetOuter()->GetName(), *InNode->GetClass()->GetName(), *InNode->NodeGuid.ToString(), ExtensionIndex);
		Record.HandlerFunctionName = FName(*FunctionName);
		ExistingName = HandlerFunctionNames.Find(Record.HandlerFunctionName);
		ExtensionIndex++;
	}

	HandlerFunctionNames.Add(Record.HandlerFunctionName);

	// Add a custom event in the graph
	UK2Node_CustomEvent* CustomEventNode = InCompilationContext.SpawnIntermediateEventNode<UK2Node_CustomEvent>(InNode, nullptr, InCompilationContext.GetConsolidatedEventGraph());
	CustomEventNode->bInternalEvent = true;
	CustomEventNode->CustomFunctionName = Record.HandlerFunctionName;
	CustomEventNode->AllocateDefaultPins();
	Record.CustomEventNodes.Add(CustomEventNode);

	// The ExecChain is the current exec output pin in the linear chain
	UEdGraphPin* ExecChain = K2Schema->FindExecutionPin(*CustomEventNode, EGPD_Output);
	if (Record.bServicesInstanceProperties)
	{
		// Need to create a variable set call for each serviced property in the handler
		for (TPair<FName, FAnimNodeSinglePropertyHandler>& PropHandlerPair : Record.ServicedProperties)
		{
			FAnimNodeSinglePropertyHandler& PropHandler = PropHandlerPair.Value;
			FName PropertyName = PropHandlerPair.Key;

			// Should be true, we only want to deal with instance targets in here
			if (PropHandler.bInstanceIsTarget)
			{
				for (FPropertyCopyRecord& CopyRecord : PropHandler.CopyRecords)
				{
					// New set node for the property
					UK2Node_VariableSet* VarAssignNode = InCompilationContext.SpawnIntermediateNode<UK2Node_VariableSet>(InNode, InCompilationContext.GetConsolidatedEventGraph());
					VarAssignNode->VariableReference.SetSelfMember(CopyRecord.DestProperty->GetFName());
					VarAssignNode->AllocateDefaultPins();
					Record.CustomEventNodes.Add(VarAssignNode);

					// Wire up the exec line, and update the end of the chain
					UEdGraphPin* ExecVariablesIn = K2Schema->FindExecutionPin(*VarAssignNode, EGPD_Input);
					ExecChain->MakeLinkTo(ExecVariablesIn);
					ExecChain = K2Schema->FindExecutionPin(*VarAssignNode, EGPD_Output);

					// Find the property pin on the set node and configure
					for (UEdGraphPin* TargetPin : VarAssignNode->Pins)
					{
						FName PinPropertyName(TargetPin->PinName);

						if (PinPropertyName == PropertyName)
						{
							// This is us, wire up the variable
							UEdGraphPin* DestPin = CopyRecord.DestPin;

							// Copy the data (link up to the source nodes)
							TargetPin->CopyPersistentDataFromOldPin(*DestPin);
							InCompilationContext.GetMessageLog().NotifyIntermediatePinCreation(TargetPin, DestPin);

							break;
						}
					}
				}
			}
		}
	}

	if (Record.bServicesNodeProperties)
	{
		// Create a struct member write node to store the parameters into the animation node
		UK2Node_StructMemberSet* AssignmentNode = InCompilationContext.SpawnIntermediateNode<UK2Node_StructMemberSet>(InNode, InCompilationContext.GetConsolidatedEventGraph());
		AssignmentNode->VariableReference.SetSelfMember(Record.NodeVariableProperty->GetFName());
		AssignmentNode->StructType = Record.NodeVariableProperty->Struct;
		AssignmentNode->AllocateDefaultPins();
		Record.CustomEventNodes.Add(AssignmentNode);

		// Wire up the variable node execution wires
		UEdGraphPin* ExecVariablesIn = K2Schema->FindExecutionPin(*AssignmentNode, EGPD_Input);
		ExecChain->MakeLinkTo(ExecVariablesIn);
		ExecChain = K2Schema->FindExecutionPin(*AssignmentNode, EGPD_Output);

		// Run thru each property
		TSet<FName> PropertiesBeingSet;

		for (auto TargetPinIt = AssignmentNode->Pins.CreateIterator(); TargetPinIt; ++TargetPinIt)
		{
			UEdGraphPin* TargetPin = *TargetPinIt;
			FName PropertyName(TargetPin->PinName);

			// Does it get serviced by this handler?
			if (FAnimNodeSinglePropertyHandler* SourceInfo = Record.ServicedProperties.Find(PropertyName))
			{
				if (TargetPin->PinType.IsArray())
				{
					// Grab the array that we need to set members for
					UK2Node_StructMemberGet* FetchArrayNode = InCompilationContext.SpawnIntermediateNode<UK2Node_StructMemberGet>(InNode, InCompilationContext.GetConsolidatedEventGraph());
					FetchArrayNode->VariableReference.SetSelfMember(Record.NodeVariableProperty->GetFName());
					FetchArrayNode->StructType = Record.NodeVariableProperty->Struct;
					FetchArrayNode->AllocatePinsForSingleMemberGet(PropertyName);
					Record.CustomEventNodes.Add(FetchArrayNode);

					UEdGraphPin* ArrayVariableNode = FetchArrayNode->FindPin(PropertyName);

					if (SourceInfo->CopyRecords.Num() > 0)
					{
						// Set each element in the array
						for (FPropertyCopyRecord& CopyRecord : SourceInfo->CopyRecords)
						{
							int32 ArrayIndex = CopyRecord.DestArrayIndex;
							if(UEdGraphPin* DestPin = CopyRecord.DestPin)
							{
								// Create an array element set node
								UK2Node_CallArrayFunction* ArrayNode = InCompilationContext.SpawnIntermediateNode<UK2Node_CallArrayFunction>(InNode, InCompilationContext.GetConsolidatedEventGraph());
								ArrayNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UKismetArrayLibrary, Array_Set), UKismetArrayLibrary::StaticClass());
								ArrayNode->AllocateDefaultPins();
								Record.CustomEventNodes.Add(ArrayNode);

								// Connect the execution chain
								ExecChain->MakeLinkTo(ArrayNode->GetExecPin());
								ExecChain = ArrayNode->GetThenPin();

								// Connect the input array
								UEdGraphPin* TargetArrayPin = ArrayNode->FindPinChecked(TEXT("TargetArray"));
								TargetArrayPin->MakeLinkTo(ArrayVariableNode);
								ArrayNode->PinConnectionListChanged(TargetArrayPin);

								// Set the array index
								UEdGraphPin* TargetIndexPin = ArrayNode->FindPinChecked(TEXT("Index"));
								TargetIndexPin->DefaultValue = FString::FromInt(ArrayIndex);

								// Wire up the data input
								UEdGraphPin* TargetItemPin = ArrayNode->FindPinChecked(TEXT("Item"));
								TargetItemPin->CopyPersistentDataFromOldPin(*DestPin);
								InCompilationContext.GetMessageLog().NotifyIntermediatePinCreation(TargetItemPin, DestPin);
							}
						}
					}
				}
				else
				{
					// Single property
					if (SourceInfo->CopyRecords.Num() > 0 && SourceInfo->CopyRecords[0].DestPin != nullptr)
					{
						UEdGraphPin* DestPin = SourceInfo->CopyRecords[0].DestPin;

						PropertiesBeingSet.Add(DestPin->PinName);
						TargetPin->CopyPersistentDataFromOldPin(*DestPin);
						InCompilationContext.GetMessageLog().NotifyIntermediatePinCreation(TargetPin, DestPin);
					}
				}
			}
		}

		// Remove any unused pins from the assignment node to avoid smashing constant values
		for (int32 PinIndex = 0; PinIndex < AssignmentNode->ShowPinForProperties.Num(); ++PinIndex)
		{
			FOptionalPinFromProperty& TestProperty = AssignmentNode->ShowPinForProperties[PinIndex];
			TestProperty.bShowPin = PropertiesBeingSet.Contains(TestProperty.PropertyName);
		}

		AssignmentNode->ReconstructNode();
	}
}

void FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::PatchFunctionNameAndCopyRecordsInto(FExposedValueHandler& Handler) const
{
	Handler.CopyRecords.Empty();
	Handler.ValueHandlerNodeProperty = NodeVariableProperty;

	if (IsFastPath())
	{
		for (const TPair<FName, FAnimNodeSinglePropertyHandler>& ServicedPropPair : ServicedProperties)
		{
			const FName& PropertyName = ServicedPropPair.Key;
			const FAnimNodeSinglePropertyHandler& PropertyHandler = ServicedPropPair.Value;

			for (const FPropertyCopyRecord& PropertyCopyRecord : PropertyHandler.CopyRecords)
			{
				// Only unbatched copies can be processed on a per-node basis
				// Skip invalid copy indices as these are usually the result of BP errors/warnings
				if(PropertyCopyRecord.LibraryCopyIndex != INDEX_NONE && PropertyCopyRecord.LibraryBatchType == EPropertyAccessBatchType::Unbatched)
				{
					Handler.CopyRecords.Emplace(PropertyCopyRecord.LibraryCopyIndex, PropertyCopyRecord.Operation);
				}
			}
		}
	}
	else
	{
		// not all of our pins use copy records so we will need to call our exposed value handler
		Handler.BoundFunction = HandlerFunctionName;
	}
}

static UEdGraphPin* FindFirstInputPin(UEdGraphNode* InNode)
{
	const UAnimationGraphSchema* Schema = GetDefault<UAnimationGraphSchema>();

	for(UEdGraphPin* Pin : InNode->Pins)
	{
		if(Pin && Pin->Direction == EGPD_Input && !Schema->IsExecPin(*Pin) && !Schema->IsSelfPin(*Pin))
		{
			return Pin;
		}
	}

	return nullptr;
}

static bool ForEachInputPin(UEdGraphNode* InNode, TFunctionRef<bool(UEdGraphPin*)> InFunction)
{
	const UAnimationGraphSchema* Schema = GetDefault<UAnimationGraphSchema>();
	bool bResult = false;

	for(UEdGraphPin* Pin : InNode->Pins)
	{
		if(Pin && Pin->Direction == EGPD_Input && !Schema->IsExecPin(*Pin) && !Schema->IsSelfPin(*Pin))
		{
			bResult |= InFunction(Pin);
		}
	}

	return bResult;
}

static UEdGraphNode* FollowKnots(UEdGraphPin* FromPin, UEdGraphPin*& ToPin)
{
	if (FromPin->LinkedTo.Num() == 0)
	{
		return nullptr;
	}

	UEdGraphPin* LinkedPin = FromPin->LinkedTo[0];
	ToPin = LinkedPin;
	if(LinkedPin)
	{
		UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
		UK2Node_Knot* KnotNode = Cast<UK2Node_Knot>(LinkedNode);
		while(KnotNode)
		{
			if(UEdGraphPin* InputPin = FindFirstInputPin(KnotNode))
			{
				if (InputPin->LinkedTo.Num() > 0 && InputPin->LinkedTo[0])
				{
					ToPin = InputPin->LinkedTo[0];
					LinkedNode = InputPin->LinkedTo[0]->GetOwningNode();
					KnotNode = Cast<UK2Node_Knot>(LinkedNode);
				}
				else
				{
					KnotNode = nullptr;
				}
			}
		}
		return LinkedNode;
	}

	return nullptr;
}

void FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::RegisterPin(UEdGraphPin* DestPin, FProperty* AssociatedProperty, int32 AssociatedPropertyArrayIndex)
{
	FAnimNodeSinglePropertyHandler& Handler = ServicedProperties.FindOrAdd(AssociatedProperty->GetFName());

	TArray<FString> DestPropertyPath;

	// Prepend the destination property with the node's member property if the property is not on a UClass
	if(Cast<UClass>(AssociatedProperty->Owner.ToUObject()) == nullptr)
	{
		DestPropertyPath.Add(NodeVariableProperty->GetName());
	}

	if(AssociatedPropertyArrayIndex != INDEX_NONE)
	{
		DestPropertyPath.Add(FString::Printf(TEXT("%s[%d]"), *AssociatedProperty->GetName(), AssociatedPropertyArrayIndex));
	}
	else
	{
		DestPropertyPath.Add(AssociatedProperty->GetName());
	}

	Handler.CopyRecords.Emplace(DestPin, AssociatedProperty, AssociatedPropertyArrayIndex, MoveTemp(DestPropertyPath));
}

void FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::RegisterPropertyBinding(FProperty* InProperty, const FAnimGraphNodePropertyBinding& InBinding)
{
	FAnimNodeSinglePropertyHandler& Handler = ServicedProperties.FindOrAdd(InProperty->GetFName());

	TArray<FString> DestPropertyPath;

	// Prepend the destination property with the node's member property if the property is not on a UClass
	if(Cast<UClass>(InProperty->Owner.ToUObject()) == nullptr)
	{
		DestPropertyPath.Add(NodeVariableProperty->GetName());
	}

	DestPropertyPath.Add(InProperty->GetName());

	Handler.CopyRecords.Emplace(InBinding.PropertyPath, DestPropertyPath);
}

void FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::BuildFastPathCopyRecords(FAnimBlueprintCompilerHandler_Base& InHandler, IAnimBlueprintPostExpansionStepContext& InCompilationContext)
{
	typedef bool (FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::*GraphCheckerFunc)(FCopyRecordGraphCheckContext&, UEdGraphPin*);

	GraphCheckerFunc GraphCheckerFuncs[] =
	{
		&FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::CheckForSplitPinAccess,
		&FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::CheckForVariableGet,
		&FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::CheckForLogicalNot,
		&FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::CheckForStructMemberAccess,
		&FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::CheckForArrayAccess,
	};

	if (GetDefault<UEngine>()->bOptimizeAnimBlueprintMemberVariableAccess)
	{
		for (TPair<FName, FAnimNodeSinglePropertyHandler>& ServicedPropPair : ServicedProperties)
		{
			TArray<FPropertyCopyRecord> AllAdditionalCopyRecords;

			for (FPropertyCopyRecord& CopyRecord : ServicedPropPair.Value.CopyRecords)
			{
				if(CopyRecord.SourcePropertyPath.Num() == 0)
				{
					TArray<FPropertyCopyRecord> AdditionalCopyRecords;

					FCopyRecordGraphCheckContext Context(CopyRecord, AdditionalCopyRecords, InCompilationContext.GetMessageLog());

					for (GraphCheckerFunc& CheckFunc : GraphCheckerFuncs)
					{
						if ((this->*CheckFunc)(Context, CopyRecord.DestPin))
						{
							break;
						}
					}

					if(AdditionalCopyRecords.Num() > 0)
					{
						for(FPropertyCopyRecord& AdditionalCopyRecord : AdditionalCopyRecords)
						{
							CheckForMemberOnlyAccess(AdditionalCopyRecord, AdditionalCopyRecord.DestPin);
						}

						CopyRecord = AdditionalCopyRecords[0];

						for(int32 AdditionalRecordIndex = 1; AdditionalRecordIndex < AdditionalCopyRecords.Num(); ++AdditionalRecordIndex)
						{
							AllAdditionalCopyRecords.Add(AdditionalCopyRecords[AdditionalRecordIndex]);
						}
					}
					else
					{
						CheckForMemberOnlyAccess(CopyRecord, CopyRecord.DestPin);
					}
				}
			}

			// Append any additional copy records
			ServicedPropPair.Value.CopyRecords.Append(AllAdditionalCopyRecords);
		}
	}
}

static void GetFullyQualifiedPathFromPin(const UEdGraphPin* Pin, TArray<FString>& OutPath)
{
	FString PinName = Pin->PinName.ToString();
	while (Pin->ParentPin != nullptr)
	{
		PinName[Pin->ParentPin->PinName.GetStringLength()] = TEXT('.');
		Pin = Pin->ParentPin;
	}

	UE::String::ParseTokens(PinName, TEXT('.'), [&OutPath](FStringView InStringView)
	{
		OutPath.Add(FString(InStringView));
	});
}

bool FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::CheckForVariableGet(FCopyRecordGraphCheckContext& Context, UEdGraphPin* DestPin)
{
	if(DestPin)
	{
		UEdGraphPin* SourcePin = nullptr;
		if(UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(FollowKnots(DestPin, SourcePin)))
		{
			if(VariableGetNode && VariableGetNode->IsNodePure() && VariableGetNode->VariableReference.IsSelfContext())
			{
				if(SourcePin)
				{
					GetFullyQualifiedPathFromPin(SourcePin, Context.CopyRecord->SourcePropertyPath);
					return true;
				}
			}
		}
	}

	return false;
}

bool FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::CheckForLogicalNot(FCopyRecordGraphCheckContext& Context, UEdGraphPin* DestPin)
{
	if(DestPin)
	{
		UEdGraphPin* SourcePin = nullptr;
		UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(FollowKnots(DestPin, SourcePin));
		if(CallFunctionNode && CallFunctionNode->FunctionReference.GetMemberName() == FName(TEXT("Not_PreBool")))
		{
			// find and follow input pin
			if(UEdGraphPin* InputPin = FindFirstInputPin(CallFunctionNode))
			{
				check(InputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
				if(CheckForVariableGet(Context, InputPin) || CheckForStructMemberAccess(Context, InputPin) || CheckForArrayAccess(Context, InputPin))
				{
					check(Context.CopyRecord->SourcePropertyPath.Num() > 0);	// this should have been filled in by CheckForVariableGet() or CheckForStructMemberAccess() above
					Context.CopyRecord->Operation = EPostCopyOperation::LogicalNegateBool;
					return true;
				}
			}
		}
	}

	return false;
}

/** The functions that we can safely native-break */
static const FName NativeBreakFunctionNameWhitelist[] =
{
	FName(TEXT("BreakVector")),
	FName(TEXT("BreakVector2D")),
	FName(TEXT("BreakRotator")),
};

/** Check whether a native break function can be safely used in the fast-path copy system (ie. source and dest data will be the same) */
static bool IsWhitelistedNativeBreak(const FName& InFunctionName)
{
	for(const FName& FunctionName : NativeBreakFunctionNameWhitelist)
	{
		if(InFunctionName == FunctionName)
		{
			return true;
		}
	}

	return false;
}

/** The functions that we can safely native-make */
static const FName NativeMakeFunctionNameWhitelist[] =
{
	FName(TEXT("MakeVector")),
	FName(TEXT("MakeVector2D")),
	FName(TEXT("MakeRotator")),
};

/** Check whether a native break function can be safely used in the fast-path copy system (ie. source and dest data will be the same) */
static bool IsWhitelistedNativeMake(const FName& InFunctionName)
{
	for(const FName& FunctionName : NativeMakeFunctionNameWhitelist)
	{
		if(InFunctionName == FunctionName)
		{
			return true;
		}
	}

	return false;
}

bool FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::CheckForStructMemberAccess(FCopyRecordGraphCheckContext& Context, UEdGraphPin* DestPin)
{
	if(DestPin)
	{
		UEdGraphPin* SourcePin = nullptr;
		if(UK2Node_BreakStruct* BreakStructNode = Cast<UK2Node_BreakStruct>(FollowKnots(DestPin, SourcePin)))
		{
			if(UEdGraphPin* InputPin = FindFirstInputPin(BreakStructNode))
			{
				if(CheckForStructMemberAccess(Context, InputPin) || CheckForVariableGet(Context, InputPin) || CheckForArrayAccess(Context, InputPin))
				{
					check(Context.CopyRecord->SourcePropertyPath.Num() > 0);	// this should have been filled in by CheckForVariableGet() above
					Context.CopyRecord->SourcePropertyPath.Add(SourcePin->PinName.ToString());
					return true;
				}
			}
		}
		// could be a native break
		else if(UK2Node_CallFunction* NativeBreakNode = Cast<UK2Node_CallFunction>(FollowKnots(DestPin, SourcePin)))
		{
			UFunction* Function = NativeBreakNode->FunctionReference.ResolveMember<UFunction>(UKismetMathLibrary::StaticClass());
			if(Function && Function->HasMetaData(TEXT("NativeBreakFunc")) && IsWhitelistedNativeBreak(Function->GetFName()))
			{
				if(UEdGraphPin* InputPin = FindFirstInputPin(NativeBreakNode))
				{
					if(CheckForStructMemberAccess(Context, InputPin) || CheckForVariableGet(Context, InputPin) || CheckForArrayAccess(Context, InputPin))
					{
						check(Context.CopyRecord->SourcePropertyPath.Num() > 0);	// this should have been filled in by CheckForVariableGet() above
						Context.CopyRecord->SourcePropertyPath.Add(SourcePin->PinName.ToString());
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::CheckForSplitPinAccess(FCopyRecordGraphCheckContext& Context, UEdGraphPin* DestPin)
{
	if(DestPin)
	{
		FPropertyCopyRecord OriginalRecord = *Context.CopyRecord;

		UEdGraphPin* SourcePin = nullptr;
		if(UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(FollowKnots(DestPin, SourcePin)))
		{
			// Idea here is to account for split pins, so we want to narrow the scope to not also include user-placed makes
			UObject* SourceObject = Context.MessageLog.FindSourceObject(MakeStructNode);
			if(SourceObject && SourceObject->IsA<UAnimGraphNode_Base>())
			{
				return ForEachInputPin(MakeStructNode, [this, &Context, &OriginalRecord](UEdGraphPin* InputPin)
				{
					Context.CopyRecord->SourcePropertyPath = OriginalRecord.SourcePropertyPath;
					if(CheckForStructMemberAccess(Context, InputPin) || CheckForVariableGet(Context, InputPin) || CheckForArrayAccess(Context, InputPin))
					{
						check(Context.CopyRecord->DestPropertyPath.Num() > 0);
						FPropertyCopyRecord RecordCopy = *Context.CopyRecord;
						FPropertyCopyRecord& NewRecord = Context.AdditionalCopyRecords.Add_GetRef(MoveTemp(RecordCopy));

						NewRecord.DestPropertyPath = OriginalRecord.DestPropertyPath; 
						NewRecord.DestPropertyPath.Add(InputPin->PinName.ToString());
						return true;
					}

					return false;
				});
			}
		}
		else if(UK2Node_CallFunction* NativeMakeNode = Cast<UK2Node_CallFunction>(FollowKnots(DestPin, SourcePin)))
		{
			UFunction* Function = NativeMakeNode->FunctionReference.ResolveMember<UFunction>(UKismetMathLibrary::StaticClass());
			if(Function && Function->HasMetaData(TEXT("NativeMakeFunc")) && IsWhitelistedNativeMake(Function->GetFName()))
			{
				// Idea here is to account for split pins, so we want to narrow the scope to not also include user-placed makes
				UObject* SourceObject = Context.MessageLog.FindSourceObject(MakeStructNode);
				if(SourceObject && SourceObject->IsA<UAnimGraphNode_Base>())
				{
					return ForEachInputPin(NativeMakeNode, [this, &Context, &OriginalRecord](UEdGraphPin* InputPin)
					{
						Context.CopyRecord->SourcePropertyPath = OriginalRecord.SourcePropertyPath;
						if(CheckForStructMemberAccess(Context, InputPin) || CheckForVariableGet(Context, InputPin) || CheckForArrayAccess(Context, InputPin))
						{
							check(Context.CopyRecord->DestPropertyPath.Num() > 0);
							FPropertyCopyRecord RecordCopy = *Context.CopyRecord;
							FPropertyCopyRecord& NewRecord = Context.AdditionalCopyRecords.Add_GetRef(MoveTemp(RecordCopy));

							NewRecord.DestPropertyPath = OriginalRecord.DestPropertyPath;
							NewRecord.DestPropertyPath.Add(InputPin->PinName.ToString());
							return true;
						}

						return false;
					});
				}
			}
		}
	}

	return false;
}

bool FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::CheckForArrayAccess(FCopyRecordGraphCheckContext& Context, UEdGraphPin* DestPin)
{
	if(DestPin)
	{
		UEdGraphPin* SourcePin = nullptr;
		if(UK2Node_CallArrayFunction* CallArrayFunctionNode = Cast<UK2Node_CallArrayFunction>(FollowKnots(DestPin, SourcePin)))
		{
			if(CallArrayFunctionNode->GetTargetFunction() == UKismetArrayLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetArrayLibrary, Array_Get)))
			{
				// Check array index is constant
				int32 ArrayIndex = INDEX_NONE;
				if(UEdGraphPin* IndexPin = CallArrayFunctionNode->FindPin(TEXT("Index")))
				{
					if(IndexPin->LinkedTo.Num() > 0)
					{
						return false;
					}

					ArrayIndex = FCString::Atoi(*IndexPin->DefaultValue);
				}

				if(UEdGraphPin* TargetArrayPin = CallArrayFunctionNode->FindPin(TEXT("TargetArray")))
				{
					if(CheckForVariableGet(Context, TargetArrayPin) || CheckForStructMemberAccess(Context, TargetArrayPin))
					{
						check(Context.CopyRecord->SourcePropertyPath.Num() > 0);	// this should have been filled in by CheckForVariableGet() or CheckForStructMemberAccess() above
						Context.CopyRecord->SourcePropertyPath.Last().Append(FString::Printf(TEXT("[%d]"), ArrayIndex));
						return true;
					}
				}
			}

		
		}
	}

	return false;
}

bool FAnimBlueprintCompilerHandler_Base::FEvaluationHandlerRecord::CheckForMemberOnlyAccess(FPropertyCopyRecord& CopyRecord, UEdGraphPin* DestPin)
{
	const UAnimationGraphSchema* AnimGraphDefaultSchema = GetDefault<UAnimationGraphSchema>();

	if(DestPin)
	{
		// traverse pins to leaf nodes and check for member access/pure only
		TArray<UEdGraphPin*> PinStack;
		PinStack.Add(DestPin);
		while(PinStack.Num() > 0)
		{
			UEdGraphPin* CurrentPin = PinStack.Pop(false);
			for(auto& LinkedPin : CurrentPin->LinkedTo)
			{
				UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
				if(LinkedNode)
				{
					bool bLeafNode = true;
					for(auto& Pin : LinkedNode->Pins)
					{
						if(Pin != LinkedPin && Pin->Direction == EGPD_Input && !AnimGraphDefaultSchema->IsPosePin(Pin->PinType))
						{
							bLeafNode = false;
							PinStack.Add(Pin);
						}
					}

					if(bLeafNode)
					{
						if(UK2Node_VariableGet* LinkedVariableGetNode = Cast<UK2Node_VariableGet>(LinkedNode))
						{
							if(!LinkedVariableGetNode->IsNodePure() || !LinkedVariableGetNode->VariableReference.IsSelfContext())
							{
								// only local variable access is allowed for leaf nodes 
								CopyRecord.InvalidateFastPath();
							}
						}
						else if(UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(LinkedNode))
						{
							if(!CallFunctionNode->IsNodePure())
							{
								// only allow pure function calls
								CopyRecord.InvalidateFastPath();
							}
						}
						else if(!LinkedNode->IsA<UK2Node_TransitionRuleGetter>())
						{
							CopyRecord.InvalidateFastPath();
						}
					}
				}
			}
		}
	}

	return CopyRecord.IsFastPath();
}

bool FAnimBlueprintCompilerHandler_Base::FEffectiveConstantRecord::Apply(UObject* Object)
{
	uint8* PropertyPtr = nullptr;
	
	UClass* ClassOwner = ConstantProperty->GetOwner<UClass>();

	if(NodeVariableProperty->Struct->IsChildOf(FAnimNode_CustomProperty::StaticStruct()) && ClassOwner && Object->GetClass()->IsChildOf(ClassOwner))
	{
		PropertyPtr = ConstantProperty->ContainerPtrToValuePtr<uint8>(Object);
	}
	else
	{
		// Check the node property is a member of the object's class
		check(NodeVariableProperty->GetOwner<UClass>() && Object->GetClass()->IsChildOf(NodeVariableProperty->GetOwner<UClass>()));
		// Check the constant property is a member of the node's struct
		check(ConstantProperty->GetOwner<UStruct>() && NodeVariableProperty->Struct->IsChildOf(ConstantProperty->GetOwner<UStruct>()));
		uint8* StructPtr = NodeVariableProperty->ContainerPtrToValuePtr<uint8>(Object);
		PropertyPtr = ConstantProperty->ContainerPtrToValuePtr<uint8>(StructPtr);
	}

	if (ArrayIndex != INDEX_NONE)
	{
		FArrayProperty* ArrayProperty = CastFieldChecked<FArrayProperty>(ConstantProperty);

		// Peer inside the array
		FScriptArrayHelper ArrayHelper(ArrayProperty, PropertyPtr);

		if (ArrayHelper.IsValidIndex(ArrayIndex))
		{
			FBlueprintEditorUtils::PropertyValueFromString_Direct(ArrayProperty->Inner, LiteralSourcePin->GetDefaultAsString(), ArrayHelper.GetRawPtr(ArrayIndex));
		}
		else
		{
			return false;
		}
	}
	else
	{
		FBlueprintEditorUtils::PropertyValueFromString_Direct(ConstantProperty, LiteralSourcePin->GetDefaultAsString(), PropertyPtr);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE