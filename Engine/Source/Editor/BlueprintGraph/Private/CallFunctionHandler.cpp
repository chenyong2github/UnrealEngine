// Copyright Epic Games, Inc. All Rights Reserved.

#include "CallFunctionHandler.h"
#include "UObject/MetaData.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_CallFunction.h"

#include "EdGraphUtilities.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "KismetCompiler.h"
#include "Net/Core/PushModel/PushModelMacros.h"

#if WITH_PUSH_MODEL
#include "Net/NetPushModelHelpers.h"
#endif

#define LOCTEXT_NAMESPACE "CallFunctionHandler"

//////////////////////////////////////////////////////////////////////////
// FImportTextErrorContext

// Support class to pipe logs from FProperty->ImportText (for struct literals) to the message log as warnings
class FImportTextErrorContext : public FOutputDevice
{
protected:
	FCompilerResultsLog& MessageLog;
	UObject* TargetObject;
public:
	int32 NumErrors;

	FImportTextErrorContext(FCompilerResultsLog& InMessageLog, UObject* InTargetObject)
		: FOutputDevice()
		, MessageLog(InMessageLog)
		, TargetObject(InTargetObject)
		, NumErrors(0)
	{
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		if (TargetObject == NULL)
		{
			MessageLog.Error(V);
		}
		else
		{
			const FString ErrorString = FString::Printf(TEXT("Invalid default on node @@: %s"), V);
			MessageLog.Error(*ErrorString, TargetObject);		
		}
		NumErrors++;
	}
};

//////////////////////////////////////////////////////////////////////////
// FKCHandler_CallFunction

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4750)
#endif

/**
 * Searches for the function referenced by a graph node in the CallingContext class's list of functions,
 * validates that the wiring matches up correctly, and creates an execution statement.
 */
void FKCHandler_CallFunction::CreateFunctionCallStatement(FKismetFunctionContext& Context, UEdGraphNode* Node, UEdGraphPin* SelfPin)
{
	int32 NumErrorsAtStart = CompilerContext.MessageLog.NumErrors;

	// Find the function, starting at the parent class
	if (UFunction* Function = FindFunction(Context, Node))
	{
		CheckIfFunctionIsCallable(Function, Context, Node);
		// Make sure the pin mapping is sound (all pins wire up to a matching function parameter, and all function parameters match a pin)

		// Remaining unmatched pins
		// Note: Should maintain a stable order for variadic arguments
		TArray<UEdGraphPin*> RemainingPins;
		RemainingPins.Append(Node->Pins);

		const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

		// Remove expected exec and self pins
		RemainingPins.RemoveAll([Schema](UEdGraphPin* Pin) { return (Pin->bOrphanedPin || Schema->IsMetaPin(*Pin)); });

		// Check for magic pins
		const bool bIsLatent = Function->HasMetaData(FBlueprintMetadata::MD_Latent);
		if (bIsLatent && (CompilerContext.UbergraphContext != &Context))
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("ContainsLatentCall_Error", "@@ contains a latent call, which cannot exist outside of the event graph").ToString(), Node);
		}

		UEdGraphPin* LatentInfoPin = nullptr;

		if (TMap<FName, FString>* MetaData = UMetaData::GetMapForObject(Function))
		{
			for (TMap<FName, FString>::TConstIterator It(*MetaData); It; ++It)
			{
				const FName& Key = It.Key();

				if (Key == TEXT("LatentInfo"))
				{
					UEdGraphPin* Pin = Node->FindPin(It.Value());
					if (Pin && (Pin->Direction == EGPD_Input) && (Pin->LinkedTo.Num() == 0))
					{
						LatentInfoPin = Pin;

						UEdGraphPin* PinToTry = FEdGraphUtilities::GetNetFromPin(Pin);
						if (FBPTerminal** Term = Context.NetMap.Find(PinToTry))
						{
							check((*Term)->bIsLiteral);
						
							int32 LatentUUID = CompilerContext.MessageLog.CalculateStableIdentifierForLatentActionManager(LatentInfoPin->GetOwningNode());

							const FString ExecutionFunctionName = UEdGraphSchema_K2::FN_ExecuteUbergraphBase.ToString() + TEXT("_") + Context.Blueprint->GetName();
							(*Term)->Name = FString::Printf(TEXT("(Linkage=%s,UUID=%s,ExecutionFunction=%s,CallbackTarget=None)"), *FString::FromInt(INDEX_NONE), *FString::FromInt(LatentUUID), *ExecutionFunctionName);

							// Record the UUID in the debugging information
							UEdGraphNode* TrueSourceNode = Cast<UEdGraphNode>(Context.MessageLog.FindSourceObject(Node));
							Context.NewClass->GetDebugData().RegisterUUIDAssociation(TrueSourceNode, LatentUUID);
						}
					}
					else
					{
						CompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("FindPinFromLinkage_ErrorFmt", "Function {0} (called from @@) was specified with LatentInfo metadata but does not have a pin named {1}"),
							FText::FromString(Function->GetName()), FText::FromString(It.Value())).ToString(), Node);
					}
				}
			}
		}

		// Parameter info to be stored, and assigned to all function call statements generated below
		FBPTerminal* LHSTerm = nullptr;
		TArray<FBPTerminal*> RHSTerms;
		UEdGraphPin* ThenExecPin = nullptr;
		UEdGraphNode* LatentTargetNode = nullptr;
		int32 LatentTargetParamIndex = INDEX_NONE;

		// Grab the special case structs that use their own literal path
		UScriptStruct* VectorStruct = TBaseStructure<FVector>::Get();
		UScriptStruct* RotatorStruct = TBaseStructure<FRotator>::Get();
		UScriptStruct* TransformStruct = TBaseStructure<FTransform>::Get();

		// Check each property
		bool bMatchedAllParams = true;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;

			bool bFoundParam = false;
			for (int32 i = 0; !bFoundParam && (i < RemainingPins.Num()); ++i)
			{
				UEdGraphPin* PinMatch = RemainingPins[i];
				if (Property->GetFName() == PinMatch->PinName)
				{
					// Found a corresponding pin, does it match in type and direction?
					if (UK2Node_CallFunction::IsStructureWildcardProperty(Function, Property->GetFName()) ||
						FKismetCompilerUtilities::IsTypeCompatibleWithProperty(PinMatch, Property, CompilerContext.MessageLog, CompilerContext.GetSchema(), Context.NewClass))
					{
						UEdGraphPin* PinToTry = FEdGraphUtilities::GetNetFromPin(PinMatch);

						if (FBPTerminal** Term = Context.NetMap.Find(PinToTry))
						{
							// For literal structs, we have to verify the default here to make sure that it has valid formatting
							if( (*Term)->bIsLiteral && (PinMatch != LatentInfoPin))
							{
								FStructProperty* StructProperty = CastField<FStructProperty>(Property);
								if( StructProperty )
								{
									UScriptStruct* Struct = StructProperty->Struct;
									if( Struct != VectorStruct
										&& Struct != RotatorStruct
										&& Struct != TransformStruct )
									{
										// Ensure all literal struct terms can be imported if its empty
										if ( (*Term)->Name.IsEmpty() )
										{
											(*Term)->Name = TEXT("()");
										}

										int32 StructSize = Struct->GetStructureSize();
										[this, StructSize, StructProperty, Node, Term, &bMatchedAllParams]()
										{
											uint8* StructData = (uint8*)FMemory_Alloca(StructSize);
											StructProperty->InitializeValue(StructData);

											// Import the literal text to a dummy struct to verify it's well-formed
											FImportTextErrorContext ErrorPipe(CompilerContext.MessageLog, Node);
											StructProperty->ImportText(*((*Term)->Name), StructData, 0, nullptr, &ErrorPipe);
											if(ErrorPipe.NumErrors > 0)
											{
												bMatchedAllParams = false;
											}
										}();
									}
									
								}
							}

							if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
							{
								LHSTerm = *Term;
							}
							else
							{
								FBPTerminal* RHSTerm = *Term;

								// if this term is an object that needs to be cast to an interface
								if (FBPTerminal** InterfaceTerm = InterfaceTermMap.Find(PinMatch))
								{
									UClass* InterfaceClass = CastChecked<UClass>(PinMatch->PinType.PinSubCategoryObject.Get());

									FBPTerminal* ClassTerm = Context.CreateLocalTerminal(ETerminalSpecification::TS_Literal);
									ClassTerm->Name       = InterfaceClass->GetName();
									ClassTerm->bIsLiteral = true;
									ClassTerm->Source     = Node;
									ClassTerm->ObjectLiteral = InterfaceClass;
									ClassTerm->Type.PinCategory = UEdGraphSchema_K2::PC_Class;

									// insert a cast op before a call to the function (and replace
									// the param with the result from the cast)
									FBlueprintCompiledStatement& CastStatement = Context.AppendStatementForNode(Node);
									CastStatement.Type = InterfaceClass->HasAnyClassFlags(CLASS_Interface) ? KCST_CastObjToInterface : KCST_CastInterfaceToObj;
									CastStatement.LHS = *InterfaceTerm;
									CastStatement.RHS.Add(ClassTerm);
									CastStatement.RHS.Add(*Term);

									RHSTerm = *InterfaceTerm;
								}

								int32 ParameterIndex = RHSTerms.Add(RHSTerm);

								if (PinMatch == LatentInfoPin)
								{
									// Record the (latent) output impulse from this node
									ThenExecPin = CompilerContext.GetSchema()->FindExecutionPin(*Node, EGPD_Output);

									if( ThenExecPin && (ThenExecPin->LinkedTo.Num() > 0) )
									{
										LatentTargetNode = ThenExecPin->LinkedTo[0]->GetOwningNode();
									}

									if (LatentTargetNode)
									{
										LatentTargetParamIndex = ParameterIndex;
									}
								}
							}

							// Make sure it isn't trying to modify a const term
							if (Property->HasAnyPropertyFlags(CPF_OutParm) && !((*Term)->IsTermWritable()))
							{
								if (Property->HasAnyPropertyFlags(CPF_ReferenceParm))
								{
									if (!Property->HasAnyPropertyFlags(CPF_ConstParm))
									{
										CompilerContext.MessageLog.Error(*LOCTEXT("PassReadOnlyReferenceParam_Error", "Cannot pass a read-only variable to a reference parameter @@").ToString(), PinMatch);
									}
								}
								else
								{
									CompilerContext.MessageLog.Error(*LOCTEXT("PassReadOnlyOutputParam_Error", "Cannot pass a read-only variable to a output parameter @@").ToString(), PinMatch);
								}
							}
						}
						else
						{
							CompilerContext.MessageLog.Error(*LOCTEXT("ResolveTermPassed_Error", "Failed to resolve term passed into @@").ToString(), PinMatch);
							bMatchedAllParams = false;
						}
					}
					else
					{
						bMatchedAllParams = false;
					}

					bFoundParam = true;
					RemainingPins.RemoveAt(i);
				}
			}

			if (!bFoundParam)
			{
				CompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("FindPinParameter_ErrorFmt", "Could not find a pin for the parameter {0} of {1} on @@"), FText::FromString(Property->GetName()), FText::FromString(Function->GetName())).ToString(), Node);
				bMatchedAllParams = false;
			}
		}

		// If we have pins remaining then it's either an error, or extra variadic terms that need to be emitted
		if (RemainingPins.Num() > 0)
		{
			const bool bIsVariadic = Function->HasMetaData(FBlueprintMetadata::MD_Variadic);
			if (bIsVariadic)
			{
				// Add a RHS term for every remaining pin
				for (UEdGraphPin* RemainingPin : RemainingPins)
				{
					// Variadic pins are assumed to be wildcard pins that have been connected to something else
					if (RemainingPin->LinkedTo.Num() == 0)
					{
						CompilerContext.MessageLog.Error(*LOCTEXT("UnlinkedVariadicPin_Error", "The variadic pin @@ must be connected. Connect something to @@.").ToString(), RemainingPin, RemainingPin->GetOwningNodeUnchecked());
						continue;
					}

					UEdGraphPin* PinToTry = FEdGraphUtilities::GetNetFromPin(RemainingPin);
					if (FBPTerminal** Term = Context.NetMap.Find(PinToTry))
					{
						FBPTerminal* RHSTerm = *Term;
						RHSTerms.Add(RHSTerm);
					}
					else
					{
						CompilerContext.MessageLog.Error(*LOCTEXT("ResolveTermVariadic_Error", "Failed to resolve variadic term passed into @@").ToString(), RemainingPin);
						bMatchedAllParams = false;
					}
				}
			}
			else
			{
				// At this point, we should have consumed all pins.  If not, there are extras that need to be removed.
				for (const UEdGraphPin* RemainingPin : RemainingPins)
				{
					CompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("PinMismatchParameter_ErrorFmt", "Pin @@ named {0} doesn't match any parameters of function {1}"), FText::FromName(RemainingPin->PinName), FText::FromString(Function->GetName())).ToString(), RemainingPin);
				}
			}
		}

		if (NumErrorsAtStart == CompilerContext.MessageLog.NumErrors)
		{
			// Build up a list of contexts that this function will be called on
			TArray<FBPTerminal*> ContextTerms;
			if (SelfPin)
			{
				const bool bIsConstSelfContext = Context.IsConstFunction();
				const bool bIsNonConstFunction = !Function->HasAnyFunctionFlags(FUNC_Const|FUNC_Static);
				const bool bEnforceConstCorrectness = Context.EnforceConstCorrectness();
				auto CheckAndAddSelfTermLambda = [this, &Node, &ContextTerms, bIsConstSelfContext, bIsNonConstFunction, bEnforceConstCorrectness](FBPTerminal* Target)
				{
					bool bIsSelfTerm = true;
					if(Target != nullptr)
					{
						const UEdGraphPin* SourcePin = Target->SourcePin;
						bIsSelfTerm = (SourcePin == nullptr || CompilerContext.GetSchema()->IsSelfPin(*SourcePin));
					}

					// Ensure const correctness within the context of the function call:
					//	a) Attempting to call a non-const, non-static function within a const function graph (i.e. 'const self' as context)
					//	b) Attempting to call a non-const, non-static function with a 'const' term linked to the target pin as the function context
					if(bIsSelfTerm && bIsConstSelfContext && bIsNonConstFunction)
					{
						// If we're not enforcing const correctness in this context, emit a warning here rather than an error, and allow compilation of this statement to proceed
						if(Target != nullptr)
						{
							if(bEnforceConstCorrectness)
							{
								CompilerContext.MessageLog.Error(*LOCTEXT("NonConstFunctionCallOnReadOnlyTarget_Error", "Function @@ can modify state and cannot be called on @@ because it is a read-only Target in this context").ToString(), Node, Target->Source);
							}
							else
							{
								CompilerContext.MessageLog.Warning(*LOCTEXT("NonConstFunctionCallOnReadOnlyTarget_Warning", "Function @@ can modify state and should not be called on @@ because it is considered to be a read-only Target in this context").ToString(), Node, Target->Source);
							}
						}
						else
						{
							if(bEnforceConstCorrectness)
							{
								CompilerContext.MessageLog.Error(*LOCTEXT("NonConstFunctionCallOnReadOnlySelfScope_Error", "Function @@ can modify state and cannot be called on 'self' because it is a read-only Target in this context").ToString(), Node);
							}
							else
							{
								CompilerContext.MessageLog.Warning(*LOCTEXT("NonConstFunctionCallOnReadOnlySelfScope_Warning", "Function @@ can modify state and should not be called on 'self' because it is considered to be a read-only Target in this context").ToString(), Node);
							}
						}
					}

					ContextTerms.Add(Target);
				};

				if( SelfPin->LinkedTo.Num() > 0 )
				{
					for(int32 i = 0; i < SelfPin->LinkedTo.Num(); i++)
					{
						FBPTerminal** pContextTerm = Context.NetMap.Find(SelfPin->LinkedTo[i]);
						if(ensureMsgf(pContextTerm != nullptr, TEXT("'%s' is missing a target input - if this is a server build, the input may be a cosmetic only property which was discarded (if this is the case, and this is expecting component variable try resaving.)"), *Node->GetPathName()))
						{
							CheckAndAddSelfTermLambda(*pContextTerm);
						}
					}
				}
				else
				{
					FBPTerminal** pContextTerm = Context.NetMap.Find(SelfPin);
					CheckAndAddSelfTermLambda((pContextTerm != nullptr) ? *pContextTerm : nullptr);
				}
			}

			// Check for a call into the ubergraph, which will require a patchup later on for the exact state entry point
			UEdGraphNode** pSrcEventNode = NULL;
			if (!bIsLatent)
			{
				pSrcEventNode = CompilerContext.CallsIntoUbergraph.Find(Node);
			}

			bool bInlineEventCall = false;
			bool bEmitInstrumentPushState = false;
			FName EventName = NAME_None;

			// Iterate over all the contexts this functions needs to be called on, and emit a call function statement for each
			FBlueprintCompiledStatement* LatentStatement = nullptr;
			for (FBPTerminal* Target : ContextTerms)
			{
				FBlueprintCompiledStatement& Statement = Context.AppendStatementForNode(Node);
				Statement.FunctionToCall = Function;
				Statement.FunctionContext = Target;
				Statement.Type = KCST_CallFunction;
				Statement.bIsInterfaceContext = IsCalledFunctionFromInterface(Node);
				Statement.bIsParentContext = IsCalledFunctionFinal(Node);

				Statement.LHS = LHSTerm;
				Statement.RHS = RHSTerms;

				if (!bIsLatent)
				{
					// Fixup ubergraph calls
					if (pSrcEventNode)
					{
						UEdGraphPin* ExecOut = CompilerContext.GetSchema()->FindExecutionPin(**pSrcEventNode, EGPD_Output);

						check(CompilerContext.UbergraphContext);
						CompilerContext.UbergraphContext->GotoFixupRequestMap.Add(&Statement, ExecOut);
						Statement.UbergraphCallIndex = 0;
					}
				}
				else
				{
					// Fixup latent functions
					if (LatentTargetNode && (Target == ContextTerms.Last()))
					{
						check(LatentTargetParamIndex != INDEX_NONE);
						Statement.UbergraphCallIndex = LatentTargetParamIndex;
						Context.GotoFixupRequestMap.Add(&Statement, ThenExecPin);
						LatentStatement = &Statement;
					}
				}

				AdditionalCompiledStatementHandling(Context, Node, Statement);

				if(Statement.Type == KCST_CallFunction && Function->HasAnyFunctionFlags(FUNC_Delegate))
				{
					CompilerContext.MessageLog.Error(*LOCTEXT("CallingDelegate_Error", "@@ is trying to call a delegate function - delegates cannot be called directly").ToString(), Node);
					// Sanitize the statement, this would have ideally been detected earlier but we need
					// to run AdditionalCompiledStatementHandling to satisify the DelegateNodeHandler
					// implementation:
					Statement.Type = KCST_CallDelegate;
				}
			}

			// Create the exit from this node if there is one
			if (bIsLatent)
			{
				// End this thread of execution; the latent function will resume it at some point in the future
				FBlueprintCompiledStatement& PopStatement = Context.AppendStatementForNode(Node);
				PopStatement.Type = KCST_EndOfThread;
			}
			else
			{
				// Generate the output impulse from this node
				if (!IsCalledFunctionPure(Node))
				{
					GenerateSimpleThenGoto(Context, *Node);
				}
			}
		}
	}
	else
	{
		FString WarningMessage = FText::Format(LOCTEXT("FindFunction_ErrorFmt", "Could not find the function '{0}' called from @@"), FText::FromString(GetFunctionNameFromNode(Node).ToString())).ToString();
		CompilerContext.MessageLog.Warning(*WarningMessage, Node);
	}
}

UClass* FKCHandler_CallFunction::GetCallingContext(FKismetFunctionContext& Context, UEdGraphNode* Node)
{
	// Find the calling scope
	UClass* SearchScope = Context.NewClass;
	UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(Node);
	if (CallFuncNode && CallFuncNode->bIsFinalFunction)
	{
		if (UK2Node_CallParentFunction* ParentCall = Cast<UK2Node_CallParentFunction>(Node))
		{
			// Special Case:  super call functions should search up their class hierarchy, and find the first legitimate implementation of the function
			const FName FuncName = CallFuncNode->FunctionReference.GetMemberName();
			UClass* SearchContext = Context.NewClass->GetSuperClass();

			UFunction* ParentFunc = nullptr;
			if (SearchContext)
			{
				ParentFunc = SearchContext->FindFunctionByName(FuncName);
			}

			return ParentFunc ? ParentFunc->GetOuterUClass() : nullptr;
		}
		else
		{
			// Final functions need the call context to be the specified class, so don't bother checking for the self pin.   The schema should enforce this.
			return CallFuncNode->FunctionReference.GetMemberParentClass(CallFuncNode->GetBlueprintClassFromNode());
		}
	}
	else
	{
		if (UEdGraphPin* SelfPin = CompilerContext.GetSchema()->FindSelfPin(*Node, EGPD_Input))
		{
			SearchScope = Cast<UClass>(Context.GetScopeFromPinType(SelfPin->PinType, Context.NewClass));
		}
	}

	return SearchScope;
}

UClass* FKCHandler_CallFunction::GetTrueCallingClass(FKismetFunctionContext& Context, UEdGraphPin* SelfPin)
{
	if (SelfPin)
	{
		// TODO: here FBlueprintCompiledStatement::GetScopeFromPinType should be called, but since FEdGraphPinType::PinSubCategory is not always initialized properly that function works wrong
		// return Cast<UClass>(Context.GetScopeFromPinType(SelfPin->PinType, Context.NewClass));
		FEdGraphPinType& Type = SelfPin->PinType;
		if ((Type.PinCategory == UEdGraphSchema_K2::PC_Object) || (Type.PinCategory == UEdGraphSchema_K2::PC_Class) || (Type.PinCategory == UEdGraphSchema_K2::PC_Interface))
		{
			if (!Type.PinSubCategory.IsNone() && (Type.PinSubCategory != UEdGraphSchema_K2::PSC_Self))
			{
				return Cast<UClass>(Type.PinSubCategoryObject.Get());
			}
		}
	}
	return Context.NewClass;
}

void FKCHandler_CallFunction::RegisterNets(FKismetFunctionContext& Context, UEdGraphNode* Node)
{
	if (UFunction* Function = FindFunction(Context, Node))
	{
		TArray<FName> DefaultToSelfParamNames;
		TArray<FName> RequiresSetValue;

		if (Function->HasMetaData(FBlueprintMetadata::MD_DefaultToSelf))
		{
			const FName DefaltToSelfPinName = *Function->GetMetaData(FBlueprintMetadata::MD_DefaultToSelf);

			DefaultToSelfParamNames.Add(DefaltToSelfPinName);
		}
		if (Function->HasMetaData(FBlueprintMetadata::MD_WorldContext))
		{
			UEdGraphSchema_K2 const* K2Schema = CompilerContext.GetSchema();
			const bool bHasIntrinsicWorldContext = !K2Schema->IsStaticFunctionGraph(Context.SourceGraph) && FBlueprintEditorUtils::ImplementsGetWorld(Context.Blueprint);

			const FName WorldContextPinName = *Function->GetMetaData(FBlueprintMetadata::MD_WorldContext);

			if (bHasIntrinsicWorldContext)
			{
				DefaultToSelfParamNames.Add(WorldContextPinName);
			}
			else if (!Function->HasMetaData(FBlueprintMetadata::MD_CallableWithoutWorldContext))
			{
				RequiresSetValue.Add(WorldContextPinName);
			}
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			const bool bIsConnected = (Pin->LinkedTo.Num() != 0);

			// if this pin could use a default (it doesn't have a connection or default of its own)
			if (!bIsConnected && (Pin->DefaultObject == nullptr))
			{
				if (DefaultToSelfParamNames.Contains(Pin->PinName) && FKismetCompilerUtilities::ValidateSelfCompatibility(Pin, Context))
				{
					ensure(Pin->PinType.PinSubCategoryObject != nullptr);
					ensure((Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object) || (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface));

					FBPTerminal* Term = Context.RegisterLiteral(Pin);
					Term->Type.PinSubCategory = UEdGraphSchema_K2::PN_Self;
					Context.NetMap.Add(Pin, Term);
				}
				else if (RequiresSetValue.Contains(Pin->PinName))
				{
					CompilerContext.MessageLog.Error(*NSLOCTEXT("KismetCompiler", "PinMustHaveConnection_Error", "Pin @@ must have a connection").ToString(), Pin);
				}
			}
		}
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if ((Pin->Direction != EGPD_Input) || (Pin->LinkedTo.Num() == 0))
		{
			continue;
		}

		// if we have an object plugged into an interface pin, let's create a 
		// term that'll be used as an intermediate, holding the result of a cast 
		// from object to interface
		if (((Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface) && (Pin->LinkedTo[0]->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)) ||
			((Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object) && (Pin->LinkedTo[0]->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface)))
		{
			FBPTerminal* InterfaceTerm = Context.CreateLocalTerminal();
			InterfaceTerm->CopyFromPin(Pin, Context.NetNameMap->MakeValidName(Pin, TEXT("CastInput")));
			InterfaceTerm->Source = Node;

			InterfaceTermMap.Add(Pin, InterfaceTerm);
		}
	}

	FNodeHandlingFunctor::RegisterNets(Context, Node);
}

void FKCHandler_CallFunction::RegisterNet(FKismetFunctionContext& Context, UEdGraphPin* Net)
{
	// This net is an output from a function call
	FBPTerminal* Term = Context.CreateLocalTerminalFromPinAutoChooseScope(Net, Context.NetNameMap->MakeValidName(Net));
	Context.NetMap.Add(Net, Term);
}

UFunction* FKCHandler_CallFunction::FindFunction(FKismetFunctionContext& Context, UEdGraphNode* Node)
{
	UClass* CallingContext = GetCallingContext(Context, Node);

	if (CallingContext)
	{
		const FName FunctionName = GetFunctionNameFromNode(Node);
		return CallingContext->FindFunctionByName(FunctionName);
	}

	return nullptr;
}

void FKCHandler_CallFunction::Transform(FKismetFunctionContext& Context, UEdGraphNode* Node)
{
	// Add an object reference pin for this call

	
	UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(Node);
	if (!CallFuncNode)
	{
		return;
	}

	bool bIsPureAndNoUsedOutputs = false;
	if (CallFuncNode->bIsPureFunc)
	{
		// Flag for removal if pure and there are no consumers of the outputs
		//@TODO: This isn't recursive (and shouldn't be here), it'll just catch the last node in a line of pure junk
		bool bAnyOutputsUsed = false;
		for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
		{
			UEdGraphPin* Pin = Node->Pins[PinIndex];
			if ((Pin->Direction == EGPD_Output) && (Pin->LinkedTo.Num() > 0))
			{
				bAnyOutputsUsed = true;
				break;
			}
		}

		if (!bAnyOutputsUsed)
		{
			//@TODO: Remove this node, not just warn about it
			bIsPureAndNoUsedOutputs = true;
		}
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// Find the function, starting at the parent class
	if (UFunction* Function = FindFunction(Context, Node))
	{
		if (Function->HasMetaData(FBlueprintMetadata::MD_Latent))
		{
			UEdGraphPin* OldOutPin = K2Schema->FindExecutionPin(*CallFuncNode, EGPD_Output);

			if ((OldOutPin != NULL) && (OldOutPin->LinkedTo.Num() > 0))
			{
				// Create a dummy execution sequence that will be the target of the return call from the latent action
				UK2Node_ExecutionSequence* DummyNode = CompilerContext.SpawnIntermediateNode<UK2Node_ExecutionSequence>(CallFuncNode);
				DummyNode->AllocateDefaultPins();

				// Wire in the dummy node
				UEdGraphPin* NewInPin = K2Schema->FindExecutionPin(*DummyNode, EGPD_Input);
				UEdGraphPin* NewOutPin = K2Schema->FindExecutionPin(*DummyNode, EGPD_Output);

				if ((NewInPin != NULL) && (NewOutPin != NULL))
				{
					CompilerContext.MessageLog.NotifyIntermediatePinCreation(NewOutPin, OldOutPin);

					while (OldOutPin->LinkedTo.Num() > 0)
					{
						UEdGraphPin* LinkedPin = OldOutPin->LinkedTo[0];

						LinkedPin->BreakLinkTo(OldOutPin);
						LinkedPin->MakeLinkTo(NewOutPin);
					}

					OldOutPin->MakeLinkTo(NewInPin);
				}
			}
		}

#if WITH_PUSH_MODEL

		/**
		 * Warning!! Similar code exists in VariableSetHandler.cpp
		 *
		 * This code is for property dirty tracking.
		 * It works by injecting in extra nodes while compiling that will call UNetPushModelHelpers::MarkPropertyDirtyFromRepIndex.
		 *
		 * That function will be called with the Owner of the property, and the RepIndex of the property.
		 * One of these nodes needs to be added for ever Net Property that is passed by (out) reference to the function.
		 *
		 *
		 * Note, this assumes that there's no way that a native class can add or remove replicated properties without also
		 * recompiling the blueprint. The only scenario that seems possible is cooked games with custom built binaries,
		 * but that still seems unsafe.
		 *
		 * If that can happen, we can instead switch to using the property name and resorting to a FindField call at runtime,
		 * but that will be more expensive.
		 */

		// If the function is pure but won't actually be evaluated, if there are no out params,
		// or there are no input pins, then we don't need to worry about any extra generation
		// because there will either be no way to reference a NetProperty, or the node won't
		// have any effect.
		if (!bIsPureAndNoUsedOutputs && Function->NumParms > 0 && Function->HasAllFunctionFlags(FUNC_HasOutParms))
		{
			static const FName MarkPropertyDirtyFuncName(TEXT("MarkPropertyDirtyFromRepIndex"));
			static const FName ObjectPinName(TEXT("Object"));
			static const FName RepIndexPinName(TEXT("RepIndex"));
			static const FName PropertyNamePinName(TEXT("PropertyName"));

			TArray<UEdGraphPin*> RemainingPins(Node->Pins);
			UEdGraphPin* OldThenPin = CallFuncNode->GetThenPin();

			// Note: This feels like it's going to be a very hot path during compilation as it will be hit for every
			// CallFunction node. Any optimizations that can be made here are probably worth it to not terribly regress
			// BP Compile time for cooked games.

			// Iterate the properties looking for Out Params that are tied to Net Properties.
			// This is similar to the loop in CreateCallFunction
			for (TFieldIterator<UProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
			{
				UProperty* Property = *It;
				if (Property->HasAllPropertyFlags(CPF_OutParm | CPF_ReferenceParm))
				{
					for (int32 i = 0; i < RemainingPins.Num(); ++i)
					{
						if (Property->GetFName() == RemainingPins[i]->PinName)
						{
							UEdGraphPin* ParamPin = RemainingPins[i];
							if (UEdGraphPin * PinToTry = FEdGraphUtilities::GetNetFromPin(ParamPin))
							{
								if (UK2Node_VariableGet* GetPropertyNode = Cast<UK2Node_VariableGet>(PinToTry->GetOwningNode()))
								{
									UProperty* ToCheck = GetPropertyNode->GetPropertyForVariable();

									// We only need to do this work if we actually found a net property.
									if (UNLIKELY(ToCheck->HasAnyPropertyFlags(CPF_Net)))
									{
										if (OldThenPin == nullptr)
										{
											// TODO: Unfortunately, we don't have an efficient way to determine whether or not a property
											//			uses push model at this point. That is only known from a call to "GetLifetimeRepProperties"
											//			on a given object, and even then that is a user defined function that could do any number
											//			of things.
											//			We *might* be able to use something like Object Annotations to cache off the results of these
											//			calls and clear the cache post compile, but that seems hacky and prone to error.
											CompilerContext.MessageLog.Warning(TEXT("Passing net properties as Out Parameters with Push Model Enabled is not supported. Function=%s, Param=%s, Property=%s"),
												*Function->GetName(), *Property->GetName(), *ToCheck->GetName());
										}
										else if (UClass * OwningClass = ToCheck->GetTypedOuter<UClass>())
										{
											// TODO: As of CL-10376545 in //UE4/Main, we no longer sort Native Replicated Properties by GC Offset.
											//			This this chunk of code can probably be removed.

											// We need to make sure this class already has its property offsets setup, otherwise
											// the order of our replicated properties won't match, meaning the RepIndex will be
											// invalid.
											if (ToCheck->GetOffset_ForGC() == 0)
											{
												if (UBlueprint * Blueprint = Cast<UBlueprint>(OwningClass->ClassGeneratedBy))
												{
													if (UClass * UseClass = Blueprint->GeneratedClass)
													{
														OwningClass = UseClass;
														ToCheck = FindFieldChecked<UProperty>(OwningClass, ToCheck->GetFName());
													}
												}
											}

											ensureAlwaysMsgf(ToCheck->GetOffset_ForGC() != 0,
												TEXT("Class does not have Property Offsets setup. This will cause issues with Push Model. Blueprint=%s, Class=%s, Property=%s"),
												*Context.Blueprint->GetPathName(), *OwningClass->GetPathName(), *ToCheck->GetName());

											if (!OwningClass->HasAnyClassFlags(CLASS_ReplicationDataIsSetUp))
											{
												OwningClass->SetUpRuntimeReplicationData();
											}

											// Actually insert the nodes.
											// TODO: I don't expect this to be a very frequent occurence, but we could be more clever about this.
											//			Instead of inserting a new node per Out Ref Net Property, we might be able to create
											//			A separate node that takes in lists of Objects, RepIndices, and Names.
											//			Ideally, this would work like the String Append node where we could specify the values
											//			directly on the node.
											{
												// Create the node that will call MarkPropertyDirty.
												UK2Node_CallFunction* MarkPropertyDirtyNode = Node->GetGraph()->CreateIntermediateNode<UK2Node_CallFunction>();
												MarkPropertyDirtyNode->FunctionReference.SetExternalMember(MarkPropertyDirtyFuncName, UNetPushModelHelpers::StaticClass());
												MarkPropertyDirtyNode->AllocateDefaultPins();

												// Create the Pins for RepIndex, PropertyName, and Object.
												UEdGraphPin* RepIndexPin = MarkPropertyDirtyNode->FindPinChecked(RepIndexPinName);
												RepIndexPin->DefaultValue = FString::FromInt(Property->RepIndex);

												UEdGraphPin* PropertyNamePin = MarkPropertyDirtyNode->FindPinChecked(PropertyNamePinName);
												PropertyNamePin->DefaultValue = Property->GetFName().ToString();

												UEdGraphPin* ObjectPin = MarkPropertyDirtyNode->FindPinChecked(ObjectPinName);
												UEdGraphPin* PropertyOwnerPin = GetPropertyNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);

												// If the property is linked to some other object, go ahead and grab that.
												// Otherwise, create an intermediate self Pin and use that.
												if (PropertyOwnerPin->LinkedTo.Num() > 0)
												{
													PropertyOwnerPin = PropertyOwnerPin->LinkedTo[0];
												}
												else
												{
													UK2Node_Self* SelfNode = Node->GetGraph()->CreateIntermediateNode<UK2Node_Self>();
													SelfNode->AllocateDefaultPins();
													PropertyOwnerPin = SelfNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);
												}

												ObjectPin->MakeLinkTo(PropertyOwnerPin);

												UEdGraphPin* NewThenPin = MarkPropertyDirtyNode->GetThenPin();
												if (ensure(NewThenPin))
												{
													NewThenPin->CopyPersistentDataFromOldPin(*OldThenPin);
													OldThenPin->BreakAllPinLinks();
													OldThenPin->MakeLinkTo(MarkPropertyDirtyNode->GetExecPin());

													// Now that the connection is established, go ahead and set our OldThenPin to
													// the NewThenPin. If we find another NetProperty later, this will save us from
													// doing a lookup.
													OldThenPin = NewThenPin;
												}
											}
										}
									}
								}
							}

							RemainingPins.RemoveAtSwap(i);
							break;
						}
					}
				}
			}
		}
#endif
	}
}

void FKCHandler_CallFunction::Compile(FKismetFunctionContext& Context, UEdGraphNode* Node)
{
	check(NULL != Node);

	//@TODO: Can probably move this earlier during graph verification instead of compilation, but after island pruning
	if (!IsCalledFunctionPure(Node))
	{
		// For imperative nodes, make sure the exec function was actually triggered and not just included due to an output data dependency
		UEdGraphPin* ExecTriggeringPin = CompilerContext.GetSchema()->FindExecutionPin(*Node, EGPD_Input);
		if (ExecTriggeringPin == NULL)
		{
			CompilerContext.MessageLog.Error(*NSLOCTEXT("KismetCompiler", "NoValidExecutionPinForCallFunc_Error", "@@ must have a valid execution pin").ToString(), Node);
			return;
		}
		else if (ExecTriggeringPin->LinkedTo.Num() == 0)
		{
			CompilerContext.MessageLog.Warning(*NSLOCTEXT("KismetCompiler", "NodeNeverExecuted_Warning", "@@ will never be executed").ToString(), Node);
			return;
		}
	}

	// Validate the self pin again if it is disconnected, because pruning isolated nodes could have caused an invalid target
	UEdGraphPin* SelfPin = CompilerContext.GetSchema()->FindSelfPin(*Node, EGPD_Input);
	if (SelfPin && (SelfPin->LinkedTo.Num() == 0))
	{
		FEdGraphPinType SelfType;
		SelfType.PinCategory = UEdGraphSchema_K2::PC_Object;
		SelfType.PinSubCategory = UEdGraphSchema_K2::PSC_Self;

		if (!CompilerContext.GetSchema()->ArePinTypesCompatible(SelfType, SelfPin->PinType, Context.NewClass) && (SelfPin->DefaultObject == NULL))
		{
			CompilerContext.MessageLog.Error(*NSLOCTEXT("KismetCompiler", "PinMustHaveConnectionPruned_Error", "Pin @@ must have a connection.  Self pins cannot be connected to nodes that are culled.").ToString(), SelfPin);
		}
	}

	// Make sure the function node is valid to call
	CreateFunctionCallStatement(Context, Node, SelfPin);
}

void FKCHandler_CallFunction::CheckIfFunctionIsCallable(UFunction* Function, FKismetFunctionContext& Context, UEdGraphNode* Node)
{
	// Verify that the function is a Blueprint callable function (in case a BlueprintCallable specifier got removed)
	if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable) && (Function->GetOuter() != Context.NewClass))
	{
		if (!IsCalledFunctionFinal(Node) && Function->GetName().Find(UEdGraphSchema_K2::FN_ExecuteUbergraphBase.ToString()))
		{
			CompilerContext.MessageLog.Error(*FText::Format(NSLOCTEXT("KismetCompiler", "ShouldNotCallFromBlueprint_ErrorFmt", "Function '{0}' called from @@ should not be called from a Blueprint"), FText::FromString(Function->GetName())).ToString(), Node);
		}
	}
}

// Get the name of the function to call from the node
FName FKCHandler_CallFunction::GetFunctionNameFromNode(UEdGraphNode* Node) const
{
	UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(Node);
	if (CallFuncNode)
	{
		return CallFuncNode->FunctionReference.GetMemberName();
	}
	else
	{
		CompilerContext.MessageLog.Error(*NSLOCTEXT("KismetCompiler", "UnableResolveFunctionName_Error", "Unable to resolve function name for @@").ToString(), Node);
		return NAME_None;
	}
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
