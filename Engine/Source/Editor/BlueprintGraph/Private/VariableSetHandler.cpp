// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariableSetHandler.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Net/NetPushModelHelpers.h"

#include "EdGraphUtilities.h"
#include "KismetCompiler.h"

#define LOCTEXT_NAMESPACE "VariableSetHandler"

//////////////////////////////////////////////////////////////////////////
// FKCHandler_VariableSet

void FKCHandler_VariableSet::RegisterNet(FKismetFunctionContext& Context, UEdGraphPin* Net)
{
	// This net is a variable write
	ResolveAndRegisterScopedTerm(Context, Net, Context.VariableReferences);
}

void FKCHandler_VariableSet::RegisterNets(FKismetFunctionContext& Context, UEdGraphNode* Node)
{
	if(UK2Node_Variable* SetterNode = Cast<UK2Node_Variable>(Node))
	{
		SetterNode->CheckForErrors(CompilerContext.GetSchema(), Context.MessageLog);

		// Report an error that the local variable could not be found
		if(SetterNode->VariableReference.IsLocalScope() && SetterNode->GetPropertyForVariable() == nullptr)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("VariableName"), FText::FromName(SetterNode->VariableReference.GetMemberName()));

			if(SetterNode->VariableReference.GetMemberScopeName() != Context.Function->GetName())
			{
				Args.Add(TEXT("ScopeName"), FText::FromString(SetterNode->VariableReference.GetMemberScopeName()));
				CompilerContext.MessageLog.Warning(*FText::Format(LOCTEXT("LocalVariableNotFoundInScope_Error", "Unable to find local variable with name '{VariableName}' for @@, scope expected: @@, scope found: {ScopeName}"), Args).ToString(), Node, Node->GetGraph());
			}
			else
			{
				CompilerContext.MessageLog.Warning(*FText::Format(LOCTEXT("LocalVariableNotFound_Error", "Unable to find local variable with name '{VariableName}' for @@"), Args).ToString(), Node);
			}
		}
	}

	for (UEdGraphPin* Net : Node->Pins)
	{
		if (!Net->bOrphanedPin && (Net->Direction == EGPD_Input) && !CompilerContext.GetSchema()->IsMetaPin(*Net))
		{
			if (ValidateAndRegisterNetIfLiteral(Context, Net))
			{
				RegisterNet(Context, Net);
			}
		}
	}
}

void FKCHandler_VariableSet::InnerAssignment(FKismetFunctionContext& Context, UEdGraphNode* Node, UEdGraphPin* VariablePin, UEdGraphPin* ValuePin)
{
	FBPTerminal** VariableTerm = Context.NetMap.Find(VariablePin);
	if (VariableTerm == nullptr)
	{
		VariableTerm = Context.NetMap.Find(FEdGraphUtilities::GetNetFromPin(VariablePin));
	}

	FBPTerminal** ValueTerm = Context.LiteralHackMap.Find(ValuePin);
	if (ValueTerm == nullptr)
	{
		ValueTerm = Context.NetMap.Find(FEdGraphUtilities::GetNetFromPin(ValuePin));
	}

	if (VariableTerm && ValueTerm)
	{
		FKismetCompilerUtilities::CreateObjectAssignmentStatement(Context, Node, *ValueTerm, *VariableTerm);

		if (!(*VariableTerm)->IsTermWritable())
		{
			// If the term is not explicitly marked as read-only, then we're attempting to set a variable on a const target
			if(!(*VariableTerm)->AssociatedVarProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
			{
				if(Context.EnforceConstCorrectness())
				{
					CompilerContext.MessageLog.Error(*LOCTEXT("WriteToReadOnlyContext_Error", "Variable @@ is read-only within this context and cannot be set to a new value").ToString(), VariablePin);
				}
				else
				{
					// Warn, but still allow compilation to succeed
					CompilerContext.MessageLog.Warning(*LOCTEXT("WriteToReadOnlyContext_Warning", "Variable @@ is considered to be read-only within this context and should not be set to a new value").ToString(), VariablePin);
				}
			}
			else
			{
				CompilerContext.MessageLog.Error(*LOCTEXT("WriteConst_Error", "Cannot write to const @@").ToString(), VariablePin);
			}
		}
	}
	else
	{
		if (VariablePin != ValuePin)
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("ResolveValueIntoVariablePin_Error", "Failed to resolve term @@ passed into @@").ToString(), ValuePin, VariablePin);
		}
		else
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("ResolveTermPassed_Error", "Failed to resolve term passed into @@").ToString(), VariablePin);
		}
	}
}

void FKCHandler_VariableSet::GenerateAssigments(FKismetFunctionContext& Context, UEdGraphNode* Node)
{
	// SubCategory is an object type or "" for the stack frame, default scope is Self
	// Each input pin is the name of a variable

	// Each input pin represents an assignment statement
	for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
	{
		UEdGraphPin* Pin = Node->Pins[PinIndex];

		if (CompilerContext.GetSchema()->IsMetaPin(*Pin))
		{
		}
		else if (Pin->Direction == EGPD_Input)
		{
			InnerAssignment(Context, Node, Pin, Pin);
		}
		else
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("ExpectedOnlyInputPins_Error", "Expected only input pins on @@ but found @@").ToString(), Node, Pin);
		}
	}
}

void FKCHandler_VariableSet::Compile(FKismetFunctionContext& Context, UEdGraphNode* Node)
{
	GenerateAssigments(Context, Node);

	// Generate the output impulse from this node
	GenerateSimpleThenGoto(Context, *Node);
}

void FKCHandler_VariableSet::Transform(FKismetFunctionContext& Context, UEdGraphNode* Node)
{
	// Expands node out to include a (local) call to the RepNotify function if necessary
	UK2Node_VariableSet* SetNotify = Cast<UK2Node_VariableSet>(Node);
	if ((SetNotify != NULL))
	{
		if (SetNotify->ShouldFlushDormancyOnSet())
		{
			// Create CallFuncNode
			UK2Node_CallFunction* CallFuncNode = Node->GetGraph()->CreateIntermediateNode<UK2Node_CallFunction>();
			CallFuncNode->FunctionReference.SetExternalMember(NAME_FlushNetDormancy, AActor::StaticClass() );
			CallFuncNode->AllocateDefaultPins();

			// Copy self pin
			UEdGraphPin* NewSelfPin = CallFuncNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);
			UEdGraphPin* OldSelfPin = Node->FindPinChecked(UEdGraphSchema_K2::PN_Self);
			NewSelfPin->CopyPersistentDataFromOldPin(*OldSelfPin);

			// link new CallFuncNode -> Set Node
			UEdGraphPin* OldExecPin = Node->FindPin(UEdGraphSchema_K2::PN_Execute);
			check(OldExecPin);

			UEdGraphPin* NewExecPin = CallFuncNode->GetExecPin();
			if (ensure(NewExecPin))
			{
				NewExecPin->CopyPersistentDataFromOldPin(*OldExecPin);
				OldExecPin->BreakAllPinLinks();
				CallFuncNode->GetThenPin()->MakeLinkTo(OldExecPin);
			}
		}

		if (SetNotify->HasLocalRepNotify())
		{
			UK2Node_CallFunction* CallFuncNode = Node->GetGraph()->CreateIntermediateNode<UK2Node_CallFunction>();
			CallFuncNode->FunctionReference.SetExternalMember(SetNotify->GetRepNotifyName(), SetNotify->GetVariableSourceClass() );
			CallFuncNode->AllocateDefaultPins();

			// Copy self pin
			UEdGraphPin* NewSelfPin = CallFuncNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);
			UEdGraphPin* OldSelfPin = Node->FindPinChecked(UEdGraphSchema_K2::PN_Self);
			NewSelfPin->CopyPersistentDataFromOldPin(*OldSelfPin);

			// link Set Node -> new CallFuncNode
			UEdGraphPin* OldThenPin = Node->FindPin(UEdGraphSchema_K2::PN_Then);
			check(OldThenPin);

			UEdGraphPin* NewThenPin = CallFuncNode->GetThenPin();
			if (ensure(NewThenPin))
			{
				// Link Set Node -> Notify
				NewThenPin->CopyPersistentDataFromOldPin(*OldThenPin);
				OldThenPin->BreakAllPinLinks();
				OldThenPin->MakeLinkTo(CallFuncNode->GetExecPin());
			}
		}

		if (SetNotify->IsNetProperty())
		{
			/**
			 * Warning!! Similar code exists in CallFunctionHandler.cpp
			 *
			 * This code is for property dirty tracking.
			 * It works by injecting in extra nodes while compiling that will call UNetPushModelHelpers::MarkPropertyDirtyFromRepIndex.
			 *
			 * That function will be called with the Owner of the property (either Self or whatever is connected to the Target pin
			 * of the BP Node), and the RepIndex of the property.
			 *
			 * Note, this assumes that there's no way that a native class can add or remove replicated properties without also
			 * recompiling the blueprint. The only scenario that seems possible is cooked games with custom built binaries,
			 * but that still seems unsafe.
			 *
			 * If that can happen, we can instead switch to using the property name and resorting to a FindField call at runtime,
			 * but that will be more expensive.
			 */

			static const FName MarkPropertyDirtyFuncName(TEXT("MarkPropertyDirtyFromRepIndex"));
			static const FName ObjectPinName(TEXT("Object"));
			static const FName RepIndexPinName(TEXT("RepIndex"));
			static const FName PropertyNamePinName(TEXT("PropertyName"));

			if (FProperty * Property = SetNotify->GetPropertyForVariable())
			{
				if (UClass* Class = Property->GetOwnerClass())
				{
					// We need to make sure this class already has its property offsets setup, otherwise
					// the order of our replicated properties won't match, meaning the RepIndex will be
					// invalid.
					if (Property->GetOffset_ForGC() == 0)
					{
						// Make sure that we're using the correct class and that it has replication data set up.
						if (Class->ClassGeneratedBy == Context.Blueprint && Context.NewClass && Context.NewClass != Class)
						{
							Class = Context.NewClass;
							Property = FindFieldChecked<FProperty>(Class, Property->GetFName());
						}
						if (Property->GetOffset_ForGC() == 0)
						{
							if (UBlueprint * Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
							{
								if (UClass * UseClass = Blueprint->GeneratedClass)
								{
									Class = UseClass;
									Property = FindFieldChecked<FProperty>(Class, Property->GetFName());
								}
							}
						}
					}

					ensureAlwaysMsgf(Property->GetOffset_ForGC() != 0,
						TEXT("Class does not have Property Offsets setup. This will cause issues with Push Model. Blueprint=%s, Class=%s, Property=%s"),
						*Context.Blueprint->GetPathName(), *Class->GetPathName(), *Property->GetName());

					if (!Class->HasAnyClassFlags(CLASS_ReplicationDataIsSetUp))
					{
						Class->SetUpRuntimeReplicationData();
					}

					UK2Node_CallFunction* CallFuncNode = Node->GetGraph()->CreateIntermediateNode<UK2Node_CallFunction>();
					CallFuncNode->FunctionReference.SetExternalMember(MarkPropertyDirtyFuncName, UNetPushModelHelpers::StaticClass());
					CallFuncNode->AllocateDefaultPins();

					// Take our old Self (Target) pin and hook it up to the Object pin for UNetPushModelHelpers::MarkPropertyDirty.
					// If our Self pin isn't hooked up to anything, then create an intermediate Self node and use that.
					UEdGraphPin* SelfPin = Node->FindPinChecked(UEdGraphSchema_K2::PN_Self);
					UEdGraphPin* ObjectPin = CallFuncNode->FindPinChecked(ObjectPinName);

					if (SelfPin->LinkedTo.Num() > 0)
					{
						SelfPin = SelfPin->LinkedTo[0];
					}
					else
					{
						UK2Node_Self* SelfNode = Node->GetGraph()->CreateIntermediateNode<UK2Node_Self>();
						SelfNode->AllocateDefaultPins();
						SelfPin = SelfNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);
					}

					ObjectPin->MakeLinkTo(SelfPin);

					UEdGraphPin* RepIndexPin = CallFuncNode->FindPinChecked(RepIndexPinName);
					RepIndexPin->DefaultValue = FString::FromInt(Property->RepIndex);

					UEdGraphPin* PropertyNamePin = CallFuncNode->FindPinChecked(PropertyNamePinName);
					PropertyNamePin->DefaultValue = Property->GetFName().ToString();

					// Hook up our exec pins.
					UEdGraphPin* OldThenPin = Node->FindPinChecked(UEdGraphSchema_K2::PN_Then);
					UEdGraphPin* NewThenPin = CallFuncNode->GetThenPin();
					if (ensure(NewThenPin))
					{
						NewThenPin->CopyPersistentDataFromOldPin(*OldThenPin);
						OldThenPin->BreakAllPinLinks();
						OldThenPin->MakeLinkTo(CallFuncNode->GetExecPin());
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
