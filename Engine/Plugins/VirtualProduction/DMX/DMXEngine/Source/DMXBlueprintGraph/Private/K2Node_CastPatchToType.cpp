// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_CastPatchToType.h"

#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphPin.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "DMXProtocolConstants.h"
#include "UObject/Object.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "UObject/Class.h"
#include "DMXProtocolTypes.h"
#include "Library/DMXEntityFixturePatch.h"
#include "UObject/ReflectedTypeAccessors.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/FrameworkObjectVersion.h"
#include "DMXSubsystem.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_EditablePinBase.h"
#include "Serialization/Archive.h"

#define LOCTEXT_NAMESPACE "UK2Node_DMXCastToFixtureType"

const FName UK2Node_CastPatchToType::InputPinName_FixturePatch(TEXT("Input_FixturePatch"));
const FName UK2Node_CastPatchToType::InputPinName_FixtureTypeRef(TEXT("Input_FixtureTypeRef"));

const FName UK2Node_CastPatchToType::OutputPinName_FunctionsMap(TEXT("Output_FunctionsMap"));

UK2Node_CastPatchToType::UK2Node_CastPatchToType()
{
	bIsEditable = true;
	bIsExposed = false;
}

/** 
 * Since we are overriding serialization (read comment below in ::Serialize(), and we are trying to serialize
 * FUserPinInfo data (for the user defined pins we are creating in this node), it'll not compile if we don't
 * define this operator that "explains" how to serialize the FUserPinInfos
 */ 
FArchive& operator<<(FArchive& Ar, FUserPinInfo& Info)
{
	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if (Ar.CustomVer(FFrameworkObjectVersion::GUID) >= FFrameworkObjectVersion::PinsStoreFName)
	{
		Ar << Info.PinName;
	}
	else
	{
		FString PinNameStr;
		Ar << PinNameStr;
		Info.PinName = *PinNameStr;
	}

	if (Ar.UE4Ver() >= VER_UE4_SERIALIZE_PINTYPE_CONST)
	{
		Info.PinType.Serialize(Ar);
		Ar << Info.DesiredPinDirection;
	}
	else
	{
		check(Ar.IsLoading());

		bool bIsArray = (Info.PinType.ContainerType == EPinContainerType::Array);
		Ar << bIsArray;

		bool bIsReference = Info.PinType.bIsReference;
		Ar << bIsReference;

		Info.PinType.ContainerType = (bIsArray ? EPinContainerType::Array : EPinContainerType::None);
		Info.PinType.bIsReference = bIsReference;

		FString PinCategoryStr;
		FString PinSubCategoryStr;
		
		Ar << PinCategoryStr;
		Ar << PinSubCategoryStr;

		Info.PinType.PinCategory = *PinCategoryStr;
		Info.PinType.PinSubCategory = *PinSubCategoryStr;

		Ar << Info.PinType.PinSubCategoryObject;
	}

	Ar << Info.PinDefaultValue;

	return Ar;
}

/**
 * What we need here is to serialize this node in both the parent and grand parent ways, because one will serialize the 
 * user defined pins (since this extends from UK2Node_EditablePinBase), but by doing only the parent serialization, 
 * it'll skip the serialization of the structs we have as IN pins (UE bug?). However, the grandparent class serializes it 
 * correctly. This method basically merges both serializations so it works as expected
 */
void UK2Node_CastPatchToType::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	// Check if it not SavingPackage
	if (Ar.IsSaving() && !GIsSavingPackage)
	{
		if (Ar.IsObjectReferenceCollector() || Ar.Tell() < 0)
		{
			// When this is a reference collector/modifier, serialize some pins as structs
			FixupPinStringDataReferences(&Ar);
		}
	}

	// Do not call parent, but call grandparent
	UEdGraphNode::Serialize(Ar);

	if (Ar.IsLoading() && ((Ar.GetPortFlags() & PPF_Duplicate) == 0))
	{
		// Fix up pin default values, must be done before post load
		FixupPinDefaultValues();

		if (GIsEditor)
		{
			// We need to serialize string data references on load in editor builds so the cooker knows about them
			FixupPinStringDataReferences(nullptr);
		}
	}

	// Pins Serialization 
	TArray<FUserPinInfo> SerializedItems;

	if (Ar.IsLoading())
	{
		Ar << SerializedItems;

		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

		UserDefinedPins.Empty(SerializedItems.Num());

		for (int32 Index = 0; Index < SerializedItems.Num(); ++Index)
		{
			TSharedPtr<FUserPinInfo> PinInfo = MakeShareable(new FUserPinInfo(SerializedItems[Index]));
			
			const bool bValidateConstRefPinTypes = Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::EditableEventsUseConstRefParameters
				&& ShouldUseConstRefParams();
			{
				if (UEdGraphPin* NodePin = FindPin(PinInfo->PinName))
				{
					{
						// NOTE: the second FindPin call here to keep us from altering a pin with the same 
						//       name but different direction (in case there is two)
						if (PinInfo->DesiredPinDirection != NodePin->Direction && FindPin(PinInfo->PinName, PinInfo->DesiredPinDirection) == nullptr)
						{
							PinInfo->DesiredPinDirection = NodePin->Direction;
						}
					}

					if (bValidateConstRefPinTypes)
					{
						// Note that we should only get here if ShouldUseConstRefParams() indicated this node represents an event function with no outputs (above).
						if (!NodePin->PinType.bIsConst
							&& NodePin->Direction == EGPD_Output
							&& !K2Schema->IsExecPin(*NodePin)
							&& !K2Schema->IsDelegateCategory(NodePin->PinType.PinCategory))
						{
							// Add 'const' to either an array pin type (always passed by reference) or a pin type that's explicitly flagged to be passed by reference.
							NodePin->PinType.bIsConst = NodePin->PinType.IsArray() || NodePin->PinType.bIsReference;

							// Also mirror the flag into the UserDefinedPins array.
							PinInfo->PinType.bIsConst = NodePin->PinType.bIsConst;
						}
					}
				}
			}

			UserDefinedPins.Add(PinInfo);
		}
	}
	else if (Ar.IsSaving())
	{
		SerializedItems.Empty(UserDefinedPins.Num());

		for (int32 PinsIndex = 0; PinsIndex < UserDefinedPins.Num(); ++PinsIndex)
		{
			SerializedItems.Add(*(UserDefinedPins[PinsIndex].Get()));
		}

		Ar << SerializedItems;
	}
	else
	{
		// We want to avoid destroying and recreating FUserPinInfo, because that will invalidate 
		// any WeakPtrs to those entries:
		for (TSharedPtr<FUserPinInfo>& PinInfo : UserDefinedPins)
		{
			Ar << *PinInfo;
		}
	}
}


void UK2Node_CastPatchToType::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, TEXT("Success"));
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, TEXT("Failure"));

	// Input pins
	UEdGraphPin* InPin_FixturePatch = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UDMXEntityFixturePatch::StaticClass(), InputPinName_FixturePatch);
	K2Schema->ConstructBasicPinTooltip(*InPin_FixturePatch, LOCTEXT("InputDMXFixturePatchPin", "Get the fixture patch reference."), InPin_FixturePatch->PinToolTip);

	UEdGraphPin* InPin_FixtureTypeRef = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, FDMXEntityFixtureTypeRef::StaticStruct(), InputPinName_FixtureTypeRef);
	K2Schema->ConstructBasicPinTooltip(*InPin_FixtureTypeRef, LOCTEXT("InputDMXFixtureTypePin", "Get the fixture Type reference."), InPin_FixtureTypeRef->PinToolTip);
	InPin_FixtureTypeRef->bNotConnectable = true;

 	// Output pins
	FCreatePinParams PinParams_OutputFunctions;
	PinParams_OutputFunctions.ContainerType = EPinContainerType::Map;
	PinParams_OutputFunctions.ValueTerminalType.TerminalCategory = UEdGraphSchema_K2::PC_Int;

	UEdGraphPin* OutputPin_FunctionsMapPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Struct, FDMXAttributeName::StaticStruct(), OutputPinName_FunctionsMap, PinParams_OutputFunctions);
	K2Schema->ConstructBasicPinTooltip(*OutputPin_FunctionsMapPin, LOCTEXT("OutputPin_FunctionsMap", "FunctionsMap"), OutputPin_FunctionsMapPin->PinToolTip);

	Super::AllocateDefaultPins();
}

FText UK2Node_CastPatchToType::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("TooltipText", "Cast Fixture Patch to Fixture Type");
}

void UK2Node_CastPatchToType::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);	

	UEdGraphPin* MeIn_FixturePatch = FindPinChecked(InputPinName_FixturePatch);
	UEdGraphPin* MeIn_FixtureTypeRef = FindPinChecked(InputPinName_FixtureTypeRef);
	UEdGraphPin* MeIn_Exec = GetExecPin();
	
	UEdGraphPin* MeOut_ThenSuccess = FindPinChecked(TEXT("Success"));
	UEdGraphPin* MeOut_ThenFailure = FindPinChecked(TEXT("Failure"));
	UEdGraphPin* MeOut_FunctionsMap = FindPinChecked(OutputPinName_FunctionsMap);
	
	const UEdGraphSchema_K2* K2Schema = CompilerContext.GetSchema();

	// This will be moved to MeOut_ThenSuccess
	UEdGraphPin* LastThenPin = nullptr;
 	

 	// NODE 1.  GetDMXSubsystem
	FName FunName_GetDMXSubsystem = GET_FUNCTION_NAME_CHECKED(UDMXSubsystem, GetDMXSubsystem_Callable);
	UK2Node_CallFunction* DMXSubsystem_Node = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	DMXSubsystem_Node->FunctionReference.SetExternalMember(FunName_GetDMXSubsystem, UDMXSubsystem::StaticClass());
	DMXSubsystem_Node->AllocateDefaultPins();

	// Move Parent Exec to GetDMXSubsystem's Exec
	CompilerContext.MovePinLinksToIntermediate(*MeIn_Exec, *DMXSubsystem_Node->GetExecPin());

	UEdGraphPin* DMXSubsytem_ReturnValue = DMXSubsystem_Node->GetReturnValuePin();
	LastThenPin = DMXSubsystem_Node->GetThenPin();

 	// NODE 2. UDMXSubsystem::PatchIsOfSelectedType 
	FName FunName_IsPatchOfType = GET_FUNCTION_NAME_CHECKED(UDMXSubsystem, PatchIsOfSelectedType);
	const UFunction* Fun_IsPatchOfType = FindUField<UFunction>(UDMXSubsystem::StaticClass(), FunName_IsPatchOfType);
	check(nullptr != Fun_IsPatchOfType);

	UK2Node_CallFunction * PatchIsOfType_Node = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	PatchIsOfType_Node->SetFromFunction(Fun_IsPatchOfType);
	PatchIsOfType_Node->AllocateDefaultPins();

	K2Schema->TryCreateConnection(LastThenPin, PatchIsOfType_Node->GetExecPin());
	K2Schema->TryCreateConnection(PatchIsOfType_Node->FindPin(UEdGraphSchema_K2::PN_Self), DMXSubsytem_ReturnValue);

	UEdGraphPin* PatchIsOfType_IN_FixturePatch = PatchIsOfType_Node->FindPinChecked(TEXT("InFixturePatch"));
	UEdGraphPin* PatchIsOfType_IN_FixtureTypeRef = PatchIsOfType_Node->FindPinChecked(TEXT("RefTypeValue"));

	CompilerContext.CopyPinLinksToIntermediate(*MeIn_FixturePatch, *PatchIsOfType_IN_FixturePatch);
	
	K2Schema->TrySetDefaultValue(*PatchIsOfType_IN_FixtureTypeRef, FindPinChecked(InputPinName_FixtureTypeRef)->DefaultValue);

	UEdGraphPin* PatchIsOfType_Out_Result = PatchIsOfType_Node->GetReturnValuePin();

	LastThenPin = PatchIsOfType_Node->GetThenPin();

 	// NODE 3: Branch (if cast success)
 	UK2Node_IfThenElse* Branch_Node = CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>(this, SourceGraph);
	Branch_Node->AllocateDefaultPins();

 	UEdGraphPin* Branch_In_Condition = Branch_Node->FindPinChecked(UEdGraphSchema_K2::PN_Condition);
 	K2Schema->TryCreateConnection(Branch_In_Condition, PatchIsOfType_Out_Result);

 	K2Schema->TryCreateConnection(Branch_Node->GetExecPin(), LastThenPin);
 
 	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(TEXT("Failure")), *Branch_Node->GetElsePin());
 
 	LastThenPin = Branch_Node->GetThenPin();

	// NODE 4. UDMXSubsystem::GetFunctionsMap
	static const FName FuncName_GetFunctionsMapForPatch = GET_FUNCTION_NAME_CHECKED(UDMXSubsystem, GetFunctionsMapForPatch);
	UFunction* FuncPtr_GetFunctionsMapForPatch = FindUField<UFunction>(UDMXSubsystem::StaticClass(), FuncName_GetFunctionsMapForPatch);
	check(FuncPtr_GetFunctionsMapForPatch);

	UK2Node_CallFunction* GetFunctionsMapForPatch_Node = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	GetFunctionsMapForPatch_Node->SetFromFunction(FuncPtr_GetFunctionsMapForPatch);
	GetFunctionsMapForPatch_Node->AllocateDefaultPins();

	UEdGraphPin* GetFunctionsMap_In_Self = GetFunctionsMapForPatch_Node->FindPinChecked(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* GetFunctionsMap_In_Exec = GetFunctionsMapForPatch_Node->GetExecPin();
	UEdGraphPin* GetFunctionsMap_In_FixturePatch = GetFunctionsMapForPatch_Node->FindPinChecked(TEXT("InFixturePatch"));

	UEdGraphPin* GetFunctionsMap_InOut_FunctionsMap = GetFunctionsMapForPatch_Node->FindPinChecked(TEXT("OutFunctionsMap"));

	UEdGraphPin* GetFunctionsMap_Out_Then = GetFunctionsMapForPatch_Node->GetThenPin();

	// inputs
 	K2Schema->TryCreateConnection(GetFunctionsMap_In_Self, DMXSubsytem_ReturnValue);
	CompilerContext.CopyPinLinksToIntermediate(*MeIn_FixturePatch, *GetFunctionsMap_In_FixturePatch);
	K2Schema->TryCreateConnection(LastThenPin, GetFunctionsMap_In_Exec);

	// outputs
	CompilerContext.MovePinLinksToIntermediate(*MeOut_FunctionsMap, *GetFunctionsMap_InOut_FunctionsMap);
	LastThenPin = GetFunctionsMap_Out_Then;

	if(UserDefinedPins.Num() > 0)
	{
		TArray<UEdGraphPin*> Map_OUT_Ints;
		TArray<UEdGraphPin*> Map_IN_Names;

		// Call functions for dmx function values
		for (const TSharedPtr<FUserPinInfo>& PinInfo : UserDefinedPins)
		{
			UEdGraphPin* Pin = FindPinChecked(PinInfo->PinName);
			{
				if (Pin->Direction == EGPD_Output)
				{
					Map_OUT_Ints.Add(Pin);
				}
				else if (Pin->Direction == EGPD_Input)
				{
					Map_IN_Names.Add(Pin);
				}
			}
		}

		check(Map_OUT_Ints.Num() == Map_IN_Names.Num());

		for (int32 PairIndex = 0; PairIndex < Map_IN_Names.Num(); ++PairIndex)
		{		
			UEdGraphPin* IN_FuncName = Map_IN_Names[PairIndex];
			UEdGraphPin* OUT_FuncInt = Map_OUT_Ints[PairIndex];

			const FName FuncName_GetFunctionsValue = GET_FUNCTION_NAME_CHECKED(UDMXSubsystem, GetFunctionsValue);
			UFunction* FuncPtr_GetFunctionsValue = FindUField<UFunction>(UDMXSubsystem::StaticClass(), FuncName_GetFunctionsValue);
			check(FuncPtr_GetFunctionsValue);

			UK2Node_CallFunction* GetFunctionsValue_Node = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			GetFunctionsValue_Node->SetFromFunction(FuncPtr_GetFunctionsValue);
			GetFunctionsValue_Node->AllocateDefaultPins();

			UEdGraphPin* GetFunctionsValue_In_Self = GetFunctionsValue_Node->FindPinChecked(UEdGraphSchema_K2::PN_Self);
			UEdGraphPin* GetFunctionsValue_In_Exec = GetFunctionsValue_Node->GetExecPin();
			UEdGraphPin* GetFunctionsValue_In_FunctionAttribute = GetFunctionsValue_Node->FindPinChecked(TEXT("FunctionAttributeName"));
			UEdGraphPin* GetFunctionsValue_In_InFunctionsMapPin = GetFunctionsValue_Node->FindPinChecked(TEXT("InFunctionsMap"));

			UEdGraphPin* GetFunctionsValue_Out_ReturnValue = GetFunctionsValue_Node->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
			UEdGraphPin* GetFunctionsValue_Out_Then= GetFunctionsValue_Node->GetThenPin();

			// Input
			K2Schema->TryCreateConnection(GetFunctionsValue_In_Self, DMXSubsytem_ReturnValue);
			CompilerContext.MovePinLinksToIntermediate(*IN_FuncName, *GetFunctionsValue_In_FunctionAttribute);			

			// Output			
			K2Schema->TryCreateConnection(GetFunctionsValue_In_InFunctionsMapPin, GetFunctionsMap_InOut_FunctionsMap);
			CompilerContext.MovePinLinksToIntermediate(*OUT_FuncInt, *GetFunctionsValue_Out_ReturnValue);

			// Execution
			K2Schema->TryCreateConnection(LastThenPin, GetFunctionsValue_In_Exec);
			LastThenPin = GetFunctionsValue_Out_Then;
		}

		CompilerContext.MovePinLinksToIntermediate(*MeOut_ThenSuccess, *LastThenPin);
	}
	else
	{
		CompilerContext.MovePinLinksToIntermediate(*MeOut_ThenSuccess, *LastThenPin);
	}
}

void UK2Node_CastPatchToType::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_CastPatchToType::GetMenuCategory() const
{
	return FText::FromString(DMX_K2_CATEGORY_NAME);
}

void UK2Node_CastPatchToType::NotifyPinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::NotifyPinConnectionListChanged(Pin);
}


UEdGraphPin* UK2Node_CastPatchToType::CreatePinFromUserDefinition(const TSharedPtr<FUserPinInfo> NewPinInfo)
{
	UEdGraphPin* NewPin = CreatePin(NewPinInfo->DesiredPinDirection, NewPinInfo->PinType, NewPinInfo->PinName);
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	Schema->SetPinAutogeneratedDefaultValue(NewPin, NewPinInfo->PinDefaultValue);

	if(NewPinInfo->DesiredPinDirection == EEdGraphPinDirection::EGPD_Input)
	{
		NewPin->bHidden = true;
	}

	return NewPin;
}

bool UK2Node_CastPatchToType::ModifyUserDefinedPinDefaultValue(TSharedPtr<FUserPinInfo> PinInfo, const FString& NewDefaultValue)
{
	if (Super::ModifyUserDefinedPinDefaultValue(PinInfo, NewDefaultValue))
	{
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		K2Schema->HandleParameterDefaultValueChanged(this);

		return true;
	}
	return false;
}

void UK2Node_CastPatchToType::ExposeFunctions()
{
	ResetFunctions();

	if(bIsExposed && UserDefinedPins.Num())
	{
		return;
	}	

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	if(const UDMXEntityFixtureType* SelectedFixtureType = GetSelectedFixtureType())
	{
		for(const FDMXFixtureMode& Mode : SelectedFixtureType->Modes)
		{
			for (const FDMXFixtureFunction& Function : Mode.Functions)
			{
				FDMXAttributeName AttributeName = Function.Attribute;

				if(AttributeName.Name.IsNone())
				{
					continue;
				}

				FString EnumString = StaticEnum<EDMXFixtureSignalFormat>()->GetDisplayNameTextByIndex((int64)Function.DataType).ToString();
				FString PinFunctionName = *FString::Printf(TEXT("%s_%s"), *Function.Attribute.GetName().ToString(), *EnumString);

				{
					FEdGraphPinType PinType;
					PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
					UEdGraphPin* Pin = CreateUserDefinedPin(*PinFunctionName, PinType, EGPD_Output);
				}
 				
				{

					FEdGraphPinType PinType;
					PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
					UEdGraphPin* Pin = CreateUserDefinedPin(*(PinFunctionName + FString("_Input")), PinType, EGPD_Input);
					Schema->TrySetDefaultValue(*Pin, Function.Attribute.Name.ToString());					
 				}			

				UBlueprint* BP = GetBlueprint();
				if (!BP->bBeingCompiled)
				{
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
				}
			}
		}		

		Modify();
		bIsExposed = true;
	}
	else
	{
		ResetFunctions();
	}	
}

void UK2Node_CastPatchToType::ResetFunctions()
{
	if(bIsExposed)
	{
	    // removes all the pins
		while(UserDefinedPins.Num())
		{
			TSharedPtr<FUserPinInfo> Pin = UserDefinedPins[0];
			RemoveUserDefinedPin(Pin);
		}

		bDisableOrphanPinSaving = true;

		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		K2Schema->HandleParameterDefaultValueChanged(this);
	}

	bIsExposed = false;
}

UDMXEntityFixtureType* UK2Node_CastPatchToType::GetSelectedFixtureType()
{
	UEdGraphPin* InPin_FixtureTypeRef = FindPinChecked(InputPinName_FixtureTypeRef);

	if (InPin_FixtureTypeRef->DefaultValue.Len() && InPin_FixtureTypeRef->LinkedTo.Num() == 0)
	{
		FString StringValue = InPin_FixtureTypeRef->DefaultValue;

		FDMXEntityFixtureTypeRef FixtureTypeRef;

		FDMXEntityReference::StaticStruct()
			->ImportText(*StringValue, &FixtureTypeRef, nullptr, EPropertyPortFlags::PPF_None, GLog, FDMXEntityReference::StaticStruct()->GetName());

		if (FixtureTypeRef.DMXLibrary != nullptr)
		{
			return FixtureTypeRef.GetFixtureType();
		}
	}

	return nullptr;
}

FString UK2Node_CastPatchToType::GetFixturePatchValueAsString() const
{
	UEdGraphPin* FixturePatchPin = FindPinChecked(InputPinName_FixtureTypeRef);

	FString PatchRefString;

	// Case with default object
	if (FixturePatchPin->LinkedTo.Num() == 0)
	{
		PatchRefString = FixturePatchPin->GetDefaultAsString();
	}
	// Case with linked object
	else
	{
		PatchRefString = FixturePatchPin->LinkedTo[0]->GetDefaultAsString();
	}

	return PatchRefString;
}

#undef LOCTEXT_NAMESPACE