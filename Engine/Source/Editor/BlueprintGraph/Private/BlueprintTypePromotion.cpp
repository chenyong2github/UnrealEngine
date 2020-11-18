// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintTypePromotion.h"
#include "Modules/ModuleManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/UObjectHash.h"

FTypePromotion* FTypePromotion::Instance = nullptr;

namespace OperatorNames
{
	static const FName NoOp			= TEXT("NO_OP");

	static const FName Add			= TEXT("Add");
	static const FName Multiply		= TEXT("Multiply");
	static const FName Subtract		= TEXT("Subtract");
	static const FName Divide		= TEXT("Divide");
	
	static const FName Greater		= TEXT("Greater");
	static const FName GreaterEq	= TEXT("GreaterEqual");
	static const FName Less			= TEXT("Less");
	static const FName LessEq		= TEXT("LessEqual");
	static const FName NotEq		= TEXT("NotEqual");
}

namespace TypePromoDebug
{
	static bool bIsTypePromoEnabled = false;
	static FAutoConsoleVariableRef CVarIsTypePromoEnabled(
		TEXT("BP.TypePromo.IsEnabled"), bIsTypePromoEnabled,
		TEXT("If true then type promotion inside of blueprints will be enabled"),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
		{
			// Clear the node spawner so that we create the new BP actions correctly
			FTypePromotion::ClearNodeSpawners();

			// Refresh all the actions so that the context menu goes back to the normal options
			if (FBlueprintActionDatabase* Actions = FBlueprintActionDatabase::TryGet())
			{
				Actions->RefreshAll();
			}
		}),
		ECVF_Default);
}

FTypePromotion& FTypePromotion::Get()
{
	if (Instance == nullptr)
	{
		Instance = new FTypePromotion();
	}
	return *Instance;
}

void FTypePromotion::Shutdown()
{
	if (Instance)
	{
		delete Instance;
		Instance = nullptr;
	}
}

FTypePromotion::FTypePromotion()
{
	CreatePromotionTable();
	CreateOpTable();
	OnModulesChangedDelegateHandle = FModuleManager::Get().OnModulesChanged().AddStatic(&FTypePromotion::OnModulesChanged);
}

FTypePromotion::~FTypePromotion()
{
	FModuleManager::Get().OnModulesChanged().Remove(OnModulesChangedDelegateHandle);
}

void FTypePromotion::OnModulesChanged(FName ModuleThatChanged, EModuleChangeReason ReasonForChange)
{
	if (Instance)
	{
		// Any time a module is changed, there could possibly be new UFunctions that we 
		// need to process, so we need to recreate the op table and clear the node spawners
		// that we are using in order to avoid invalid duplicates in the graph action menu
		FTypePromotion::ClearNodeSpawners();
		Instance->CreateOpTable();
	}
}

void FTypePromotion::CreatePromotionTable()
{
	PromotionTable =
	{
		// Type_X...						Can be promoted to...
		{ UEdGraphSchema_K2::PC_Int,		{ UEdGraphSchema_K2::PC_Float, UEdGraphSchema_K2::PC_Double, UEdGraphSchema_K2::PC_Int64 } },
		{ UEdGraphSchema_K2::PC_Byte,		{ UEdGraphSchema_K2::PC_Float, UEdGraphSchema_K2::PC_Int, UEdGraphSchema_K2::PC_Int64, UEdGraphSchema_K2::PC_Double } },
		{ UEdGraphSchema_K2::PC_Float,		{ UEdGraphSchema_K2::PC_Double, UEdGraphSchema_K2::PC_Int64 } },
		{ UEdGraphSchema_K2::PC_Double,		{ UEdGraphSchema_K2::PC_Int64 } },
		{ UEdGraphSchema_K2::PC_Wildcard,	{ UEdGraphSchema_K2::PC_Int, UEdGraphSchema_K2::PC_Int64, UEdGraphSchema_K2::PC_Float, UEdGraphSchema_K2::PC_Double, UEdGraphSchema_K2::PC_Byte, UEdGraphSchema_K2::PC_Boolean } },
	};
}

const TMap<FName, TArray<FName>>* const FTypePromotion::GetPrimativePromotionTable()
{
	if (Instance)
	{
		return &Instance->PromotionTable;
	}

	return nullptr;
}

bool FTypePromotion::IsValidPromotion(const FEdGraphPinType& A, const FEdGraphPinType& B)
{
	// If either of these pin types is a struct, than we have to have some kind of valid
	// conversion function, otherwise we can't possibly connect them
	if (A.PinCategory == UEdGraphSchema_K2::PC_Struct || B.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

		FName DummyName;
		UClass* DummyClass = nullptr;
		UK2Node* DummyNode = nullptr;

		return K2Schema->SearchForAutocastFunction(A, B, /*out*/ DummyName, DummyClass);
	}
	else
	{
		return FTypePromotion::GetHigherType(A, B) == ETypeComparisonResult::TypeBHigher;
	}
}

bool FTypePromotion::HasStructConversion(const UEdGraphPin* InputPin, const UEdGraphPin* OutputPin)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	FName DummyName;
	UClass* DummyClass = nullptr;
	UK2Node* DummyNode = nullptr;

	const bool bCanAutocast = K2Schema->SearchForAutocastFunction(OutputPin->PinType, InputPin->PinType, /*out*/ DummyName, DummyClass);
	const bool bCanAutoConvert = K2Schema->FindSpecializedConversionNode(OutputPin, InputPin, false, /* out */ DummyNode);
	
	return bCanAutocast || bCanAutoConvert;
}

FTypePromotion::ETypeComparisonResult FTypePromotion::GetHigherType(const FEdGraphPinType& A, const FEdGraphPinType& B)
{
	return FTypePromotion::Get().GetHigherType_Internal(A, B);
}

FTypePromotion::ETypeComparisonResult FTypePromotion::GetHigherType_Internal(const FEdGraphPinType& A, const FEdGraphPinType& B) const
{
	if(A == B)
	{
		return ETypeComparisonResult::TypesEqual;
	}
	// Is this A promotable type?					  Can type A be promoted to type B?
	else if(PromotionTable.Contains(A.PinCategory) && PromotionTable[A.PinCategory].Contains(B.PinCategory))
	{
		return ETypeComparisonResult::TypeBHigher;
	}
	// Can B get promoted to A?
	else if(PromotionTable.Contains(B.PinCategory) && PromotionTable[B.PinCategory].Contains(A.PinCategory))
	{
		return ETypeComparisonResult::TypeAHigher;
	}
	// Handle "No" Pin type, the default value of FEdGraphPinType
	else if(A.PinCategory == NAME_None && B.PinCategory != NAME_None)
	{
		return ETypeComparisonResult::TypeBHigher;
	}
	else if(B.PinCategory == NAME_None && A.PinCategory != NAME_None)
	{
		return ETypeComparisonResult::TypeAHigher;
	}
	// A is a struct and B is not a struct
	else if(A.PinCategory == UEdGraphSchema_K2::PC_Struct && B.PinCategory != UEdGraphSchema_K2::PC_Struct)
	{
		return ETypeComparisonResult::TypeAHigher;
	}
	// A is not a struct and B is a struct
	else if (A.PinCategory != UEdGraphSchema_K2::PC_Struct && B.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		return ETypeComparisonResult::TypeBHigher;
	}

	// We couldn't find any possible promotions, so this is an invalid comparison
	return ETypeComparisonResult::InvalidComparison;
}

bool FTypePromotion::IsFunctionPromotionReady(const UFunction* const FuncToConsider)
{
	return FTypePromotion::Get().IsFunctionPromotionReady_Internal(FuncToConsider);
}

bool FTypePromotion::IsFunctionPromotionReady_Internal(const UFunction* const FuncToConsider) const
{
	// This could be better served as just keeping all known UFunctions in a TArray or TSet, and
	// using that index in the Map so we don't need to iterate table pairs and arrays inside of them
	for (const TPair<FName, FFunctionsList>& Pair : OperatorTable)
	{
		for(const UFunction* const Func : Pair.Value)
		{
			if(Func == FuncToConsider)
			{
				return true;
			}
		}
	}

	return false;
}

FEdGraphPinType FTypePromotion::GetPromotedType(const TArray<UEdGraphPin*>& WildcardPins)
{
	return FTypePromotion::Get().GetPromotedType_Internal(WildcardPins);
}

FEdGraphPinType FTypePromotion::GetPromotedType_Internal(const TArray<UEdGraphPin*>& WildcardPins) const
{
	// There must be some wildcard pins in order to get the promoted type
	TRACE_CPUPROFILER_EVENT_SCOPE(FTypePromotionTable::GetPromotedType);
	
	FEdGraphPinType HighestPinType = FEdGraphPinType();

	for (const UEdGraphPin* CurPin : WildcardPins)
	{
		if(CurPin)
		{
			ETypeComparisonResult Res = GetHigherType(/* A */ HighestPinType, /* B */ CurPin->PinType);

			// If this pin is a different type and "higher" than set our out pin type to that
			switch(Res)
			{
				case ETypeComparisonResult::TypeBHigher:
					HighestPinType = CurPin->PinType;
				break;
			}
		}
	}
	return HighestPinType;
}

bool FTypePromotion::PromotePin(FEdGraphPinType& InTypeA, const FEdGraphPinType& TypeB)
{
	return FTypePromotion::Get().PromotePin_Internal(InTypeA, TypeB);
}

bool FTypePromotion::PromotePin_Internal(FEdGraphPinType& InTypeA, const FEdGraphPinType& TypeB)
{
	// If type B is not the higher type, than we shouldn't do anything
	if(GetHigherType(InTypeA, TypeB) != ETypeComparisonResult::TypeBHigher)
	{
		return false;
	}

	InTypeA = TypeB;

	return true;
}

UFunction* FTypePromotion::FindBestMatchingFunc(FName Operation, const TArray<UEdGraphPin*>& PinsToConsider)
{
	return FTypePromotion::Get().FindBestMatchingFunc_Internal(Operation, PinsToConsider);
}

static bool PropertyCompatibleWithPin(const FProperty* Param, FEdGraphPinType const& TypeToMatch)
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	FEdGraphPinType ParamType;
	if (Schema->ConvertPropertyToPinType(Param, /* out */ ParamType))
	{
		if (Schema->ArePinTypesCompatible(TypeToMatch, ParamType) &&
			FTypePromotion::GetHigherType(TypeToMatch, ParamType) != FTypePromotion::ETypeComparisonResult::InvalidComparison)
		{
			return true;
		}
	}
	return false;
}

UFunction* FTypePromotion::FindBestMatchingFunc_Internal(FName Operation, const TArray<UEdGraphPin*>& PinsToConsider)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FTypePromotionTable::FindBestMatchingFunc_Internal);

	const FFunctionsList* FuncList = OperatorTable.Find(Operation);
	if (!FuncList)
	{
		return nullptr;
	}

	// Track the function with the best score, input, and output types
	UFunction* BestFunc = nullptr;
	FEdGraphPinType BestFuncLowestInputType;
	FEdGraphPinType BestFuncOutputType;
	int32 BestScore = -1;

	const bool bIsSinglePin = PinsToConsider.Num() == 1;
	const bool bIsComparisonOp = GetComparisonOpNames().Contains(Operation);

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		
	// We have to keep track of what pins we have already given points for, otherwise 
	// we will end up giving the same pin multiple points.
	TSet<const UEdGraphPin*> CheckedPins;
		
	for (UFunction* Func : *FuncList)
	{
		int32 FuncScore = -1;
		CheckedPins.Reset();

		// Track this functions highest input and output types so that if there is a function with
		// the same score as it we can prefer the correct one. 
		FEdGraphPinType CurFuncHighestInputType;
		FEdGraphPinType CurFuncOutputType;

		// For each property in the func, see if it matches any of the given pins
		for (TFieldIterator<FProperty> PropIt(Func); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
		{
			const FProperty* Param = *PropIt;
			FEdGraphPinType ParamType;
			if (Schema->ConvertPropertyToPinType(Param, /* out */ ParamType))
			{
				for (const UEdGraphPin* Pin : PinsToConsider)
				{
					// Give a point for each function parameter that matches up with a pin to consider
					if ((!CheckedPins.Contains(Pin) || bIsSinglePin) && Schema->ArePinTypesEquivalent(ParamType, Pin->PinType))
					{
						// Are the directions compatible? 
						// If we are a comparison or only a single pin then we don't care about the direction
						if (bIsSinglePin || bIsComparisonOp ||
						   (Param->HasAnyPropertyFlags(CPF_ReturnParm) && Pin->Direction == EGPD_Output) ||
						   (!Param->HasAnyPropertyFlags(CPF_ReturnParm) && Pin->Direction == EGPD_Input))
						{
							++FuncScore;
							CheckedPins.Add(Pin);
						}

						break;
					}
				}

				// Keep track of the highest input pin type on this function
				if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					CurFuncOutputType = ParamType;
				}
				else if (CurFuncHighestInputType.PinCategory == NAME_None || FTypePromotion::GetHigherType(ParamType, CurFuncHighestInputType) == ETypeComparisonResult::TypeBHigher)
				{
					CurFuncHighestInputType = ParamType;
				}
			}
		}

		// If the pin type has no name, then this is an invalid comparison
		ETypeComparisonResult InputCompareRes = BestFuncLowestInputType.PinCategory != NAME_None ? FTypePromotion::GetHigherType(CurFuncHighestInputType, BestFuncLowestInputType) : ETypeComparisonResult::InvalidComparison;
		ETypeComparisonResult OutputCompareRes = BestFuncOutputType.PinCategory != NAME_None ? FTypePromotion::GetHigherType(CurFuncOutputType, BestFuncOutputType) : ETypeComparisonResult::InvalidComparison;
		
		// We want to prefer a HIGHER input, and a LOWER output. 
		const bool bHasInputOutputPreference =
			InputCompareRes != ETypeComparisonResult::TypeBHigher &&
			OutputCompareRes != ETypeComparisonResult::TypeAHigher;

		// If the scores are equal, then prefer the LARGER input and output type because we can promote up, but we can never go back down
		const bool bScoresEqualAndPreferred = 
			FuncScore == BestScore && FuncScore != -1 &&
			(bIsComparisonOp || bIsSinglePin || 
			(InputCompareRes == ETypeComparisonResult::TypeAHigher ||
			OutputCompareRes == ETypeComparisonResult::TypeAHigher));

		// Keep track of the best function!
		if (bScoresEqualAndPreferred || (FuncScore > BestScore && (bHasInputOutputPreference || bIsComparisonOp)))
		{
			BestScore = FuncScore;
			BestFuncLowestInputType = CurFuncHighestInputType;
			BestFuncOutputType = CurFuncOutputType;
			BestFunc = Func;
		}
	}
	return BestFunc;
}

void FTypePromotion::GetAllFuncsForOp(FName Operation, TArray<UFunction*>& OutFuncs)
{
	return FTypePromotion::Get().GetAllFuncsForOp_Internal(Operation, OutFuncs);
}

const TSet<FName>& FTypePromotion::GetAllOpNames()
{
	static const TSet<FName> OpsArray =
	{
		OperatorNames::Add,
		OperatorNames::Multiply,
		OperatorNames::Subtract,
		OperatorNames::Divide,
		OperatorNames::Greater,
		OperatorNames::GreaterEq,
		OperatorNames::Less,
		OperatorNames::LessEq,
		OperatorNames::NotEq
	};

	return OpsArray;
}

const TSet<FName>& FTypePromotion::GetComparisonOpNames()
{
	static const TSet<FName> ComparisonOps =
	{
		OperatorNames::Greater,
		OperatorNames::GreaterEq,
		OperatorNames::Less,
		OperatorNames::LessEq,
		OperatorNames::NotEq
	};
	return ComparisonOps;
}

bool FTypePromotion::IsComparisonFunc(UFunction const* const Func)
{
	return Func && GetComparisonOpNames().Contains(GetOpNameFromFunction(Func));
}

bool FTypePromotion::IsComparisonOpName(const FName OpName)
{
	return GetComparisonOpNames().Contains(OpName);
}

void FTypePromotion::GetAllFuncsForOp_Internal(FName Operation, TArray<UFunction*>& OutFuncs)
{
	OutFuncs.Empty();

	OutFuncs.Append(OperatorTable[Operation]);
}

FName FTypePromotion::GetOpNameFromFunction(UFunction const* const Func)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FTypePromotion::GetOpNameFromFunction);
	if(!Func)
	{
		return OperatorNames::NoOp;
	}

	FString FuncName = Func->GetName();
	// Get everything before the "_"
	int32 Index = FuncName.Find(TEXT("_"));
	
	FName FuncNameChopped(FuncName.Mid(0, Index));
	if (GetAllOpNames().Contains(FuncNameChopped))
	{
		return FuncNameChopped;
	}

	return OperatorNames::NoOp;
}

void FTypePromotion::CreateOpTable()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FTypePromotion::CreateOpTable);
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	OperatorTable.Empty();

	TArray<UClass*> Libraries;
	GetDerivedClasses(UBlueprintFunctionLibrary::StaticClass(), Libraries);
	for (UClass* Library : Libraries)
	{
		// Ignore abstract libraries/classes
		if (!Library || Library->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		for (UFunction* Function : TFieldRange<UFunction>(Library, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::ExcludeDeprecated))
		{
			if(!IsPromotableFunction(Function))
			{
				continue;
			}

			FEdGraphPinType FuncPinType;
			FName OpName = GetOpNameFromFunction(Function);

			if (OpName != OperatorNames::NoOp && Schema->ConvertPropertyToPinType(Function->GetReturnProperty(), /* out */ FuncPinType))
			{
				AddOpFunction(OpName, Function);
			}
		}
	}
}

void FTypePromotion::AddOpFunction(FName OpName, UFunction* Function)
{
	OperatorTable.FindOrAdd(OpName).Add(Function);
}

bool FTypePromotion::IsPromotableFunction(const UFunction* Function)
{
	// Ensure that we don't have an invalid OpName as well for extra safety when this function 
	// is called outside of this class, not during the OpTable creation process
	FName OpName = GetOpNameFromFunction(Function);
	return Function && Function->HasAnyFunctionFlags(FUNC_BlueprintPure) && Function->GetReturnProperty() && OpName != OperatorNames::NoOp;
}

bool FTypePromotion::IsOperatorSpawnerRegistered(UFunction const* const Func)
{
	return FTypePromotion::GetOperatorSpawner(FTypePromotion::GetOpNameFromFunction(Func)) != nullptr;
}

void FTypePromotion::RegisterOperatorSpawner(FName OpName, UBlueprintFunctionNodeSpawner* Spawner)
{
	if(Instance && !Instance->OperatorNodeSpawnerMap.Contains(OpName) && OpName != OperatorNames::NoOp)
	{
		Instance->OperatorNodeSpawnerMap.Add(OpName, Spawner);
	}
}

UBlueprintFunctionNodeSpawner* FTypePromotion::GetOperatorSpawner(FName OpName)
{
	if(Instance && Instance->OperatorNodeSpawnerMap.Contains(OpName))
	{
		return Instance->OperatorNodeSpawnerMap[OpName];
	}

	return nullptr;
}

void FTypePromotion::ClearNodeSpawners()
{
	if (Instance)
	{
		Instance->OperatorNodeSpawnerMap.Empty();
	}
}