// Copyright Epic Games, Inc. All Rights Reserved.

#include "KismetCastingUtils.h"

#include "KismetCompiledFunctionContext.h"
#include "KismetCompilerMisc.h"

namespace UE::KismetCompiler::CastingUtils::Private
{

enum class EPinType : uint8
{
	Float,
	FloatArray,
	FloatSet,

	Double,
	DoubleArray,
	DoubleSet,

	Vector3f,
	Vector3fArray,
	Vector3fSet,

	Vector,
	VectorArray,
	VectorSet,

	// Maps are particularly sinister, and are categorized separately here.
	// Keys and values are casted independently of one another.

	FloatKeyOtherValueMap,
	DoubleKeyOtherValueMap,
	OtherKeyFloatValueMap,
	OtherKeyDoubleValueMap,
	FloatKeyFloatValueMap,
	FloatKeyDoubleValueMap,
	DoubleKeyFloatValueMap,
	DoubleKeyDoubleValueMap,

	Other,
};

EPinType GetPinType(const UEdGraphPin& Pin)
{
	static UScriptStruct* VectorStruct = TBaseStructure<FVector>::Get();
	static UScriptStruct* Vector3fStruct = TVariantStructure<FVector3f>::Get();

	if (Pin.PinType.IsMap())
	{
		if ((Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_Real) && (Pin.PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float))
		{
			if ((Pin.PinType.PinValueType.TerminalCategory == UEdGraphSchema_K2::PC_Real) && (Pin.PinType.PinValueType.TerminalSubCategory == UEdGraphSchema_K2::PC_Float))
			{
				return EPinType::FloatKeyFloatValueMap;
			}
			else if ((Pin.PinType.PinValueType.TerminalCategory == UEdGraphSchema_K2::PC_Real) && (Pin.PinType.PinValueType.TerminalSubCategory == UEdGraphSchema_K2::PC_Double))
			{
				return EPinType::FloatKeyDoubleValueMap;
			}
			else
			{
				return EPinType::FloatKeyOtherValueMap;
			}
		}
		else if ((Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_Real) && (Pin.PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double))
		{
			if ((Pin.PinType.PinValueType.TerminalCategory == UEdGraphSchema_K2::PC_Real) && (Pin.PinType.PinValueType.TerminalSubCategory == UEdGraphSchema_K2::PC_Float))
			{
				return EPinType::DoubleKeyFloatValueMap;
			}
			else if ((Pin.PinType.PinValueType.TerminalCategory == UEdGraphSchema_K2::PC_Real) && (Pin.PinType.PinValueType.TerminalSubCategory == UEdGraphSchema_K2::PC_Double))
			{
				return EPinType::DoubleKeyDoubleValueMap;
			}
			else
			{
				return EPinType::DoubleKeyOtherValueMap;
			}
		}
		else
		{
			if ((Pin.PinType.PinValueType.TerminalCategory == UEdGraphSchema_K2::PC_Real) && (Pin.PinType.PinValueType.TerminalSubCategory == UEdGraphSchema_K2::PC_Float))
			{
				return EPinType::OtherKeyFloatValueMap;
			}
			else if ((Pin.PinType.PinValueType.TerminalCategory == UEdGraphSchema_K2::PC_Real) && (Pin.PinType.PinValueType.TerminalSubCategory == UEdGraphSchema_K2::PC_Double))
			{
				return EPinType::OtherKeyDoubleValueMap;
			}
		}
	}
	else
	{
		if ((Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_Real) && (Pin.PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float))
		{
			if (Pin.PinType.IsArray())
			{
				return EPinType::FloatArray;
			}
			else if (Pin.PinType.IsSet())
			{
				return EPinType::FloatSet;
			}
			else
			{
				return EPinType::Float;
			}
		}
		else if ((Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_Real) && (Pin.PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double))
		{
			if (Pin.PinType.IsArray())
			{
				return EPinType::DoubleArray;
			}
			else if (Pin.PinType.IsSet())
			{
				return EPinType::DoubleSet;
			}
			else
			{
				return EPinType::Double;
			}
		}
		else if (Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			if (Pin.PinType.PinSubCategoryObject == Vector3fStruct)
			{
				if (Pin.PinType.IsArray())
				{
					return EPinType::Vector3fArray;
				}
				else if (Pin.PinType.IsSet())
				{
					return EPinType::Vector3fSet;
				}
				else
				{
					return EPinType::Vector3f;
				}
			}
			else if (Pin.PinType.PinSubCategoryObject == VectorStruct)
			{
				if (Pin.PinType.IsArray())
				{
					return EPinType::VectorArray;
				}
				else if (Pin.PinType.IsSet())
				{
					return EPinType::VectorSet;
				}
				else
				{
					return EPinType::Vector;
				}
			}
		}
	}

	return EPinType::Other;
}

} // namespace UE::KismetCompiler::CastingUtils::Private

namespace UE::KismetCompiler::CastingUtils
{

TOptional<EKismetCompiledStatementType> GetInverseCastStatement(EKismetCompiledStatementType Statement)
{
	TOptional<EKismetCompiledStatementType> Result;

	switch (Statement)
	{
	case KCST_FloatToDoubleCast:
		Result = KCST_DoubleToFloatCast;
		break;
	case KCST_FloatToDoubleArrayCast:
		Result = KCST_DoubleToFloatArrayCast;
		break;
	case KCST_FloatToDoubleSetCast:
		Result = KCST_DoubleToFloatSetCast;
		break;

	case KCST_DoubleToFloatCast:
		Result = KCST_FloatToDoubleCast;
		break;
	case KCST_DoubleToFloatArrayCast:
		Result = KCST_FloatToDoubleArrayCast;
		break;
	case KCST_DoubleToFloatSetCast:
		Result = KCST_FloatToDoubleSetCast;
		break;

	case KCST_Vector3fToVectorCast:
		Result = KCST_VectorToVector3fCast;
		break;
	case KCST_Vector3fToVectorArrayCast:
		Result = KCST_VectorToVector3fArrayCast;
		break;
	case KCST_Vector3fToVectorSetCast:
		Result = KCST_VectorToVector3fSetCast;
		break;

	case KCST_VectorToVector3fCast:
		Result = KCST_Vector3fToVectorCast;
		break;
	case KCST_VectorToVector3fArrayCast:
		Result = KCST_Vector3fToVectorArrayCast;
		break;
	case KCST_VectorToVector3fSetCast:
		Result = KCST_Vector3fToVectorSetCast;
		break;

	case KCST_FloatToDoubleKeys_MapCast:
		Result = KCST_DoubleToFloatKeys_MapCast;
		break;
	case KCST_DoubleToFloatKeys_MapCast:
		Result = KCST_FloatToDoubleKeys_MapCast;
		break;
	case KCST_FloatToDoubleValues_MapCast:
		Result = KCST_DoubleToFloatValues_MapCast;
		break;
	case KCST_DoubleToFloatValues_MapCast:
		Result = KCST_FloatToDoubleValues_MapCast;
		break;

	case KCST_FloatToDoubleKeys_FloatToDoubleValues_MapCast:
		Result = KCST_DoubleToFloatKeys_DoubleToFloatValues_MapCast;
		break;
	case KCST_DoubleToFloatKeys_FloatToDoubleValues_MapCast:
		Result = KCST_FloatToDoubleKeys_DoubleToFloatValues_MapCast;
		break;
	case KCST_DoubleToFloatKeys_DoubleToFloatValues_MapCast:
		Result = KCST_FloatToDoubleKeys_FloatToDoubleValues_MapCast;
		break;
	case KCST_FloatToDoubleKeys_DoubleToFloatValues_MapCast:
		Result = KCST_DoubleToFloatKeys_FloatToDoubleValues_MapCast;
		break;
	}

	return Result;
}

void RegisterImplicitCasts(FKismetFunctionContext& Context)
{
	using namespace UE::KismetCompiler::CastingUtils::Private;

	auto AddCastMapping = [&Context](UEdGraphPin* DestinationPin, EKismetCompiledStatementType CastType, const TCHAR* TermName)
	{
		check(DestinationPin);
		check(TermName);

		FBPTerminal* NewTerm = Context.CreateLocalTerminal();
		UEdGraphNode* OwningNode = DestinationPin->GetOwningNode();
		NewTerm->CopyFromPin(DestinationPin, Context.NetNameMap->MakeValidName(DestinationPin, TermName));
		NewTerm->Source = OwningNode;

		Context.ImplicitCastMap.Add(DestinationPin, FImplicitCastParams{ CastType, NewTerm, OwningNode });
	};

	// The current context's NetMap can be a mix of input and output pin types.
	// We need to check both pin types in order to get adequate coverage for potential cast points.
	for (const auto& It : Context.NetMap)
	{
		UEdGraphPin* CurrentPin = It.Key;
		check(CurrentPin);

		bool bIsConnectedOutput =
			(CurrentPin->Direction == EGPD_Output) && (CurrentPin->LinkedTo.Num() > 0);

		bool bIsConnectedInput =
			(CurrentPin->Direction == EGPD_Input) && (CurrentPin->LinkedTo.Num() > 0);

		if (bIsConnectedOutput)
		{
			for (UEdGraphPin* DestinationPin : CurrentPin->LinkedTo)
			{
				check(DestinationPin);

				if (Context.ImplicitCastMap.Contains(DestinationPin))
				{
					continue;
				}

				TOptional<StatementNamePair> ConversionType =
					GetFloatingPointConversionType(*CurrentPin, *DestinationPin);

				if (ConversionType)
				{
					AddCastMapping(DestinationPin, ConversionType->Get<0>(), ConversionType->Get<1>());
				}
			}
		}
		else if (bIsConnectedInput)
		{
			if (Context.ImplicitCastMap.Contains(CurrentPin))
			{
				continue;
			}

			if (CurrentPin->LinkedTo.Num() > 0)
			{
				const UEdGraphPin* SourcePin = CurrentPin->LinkedTo[0];
				check(SourcePin);

				TOptional<StatementNamePair> ConversionType =
					GetFloatingPointConversionType(*SourcePin, *CurrentPin);

				if (ConversionType)
				{
					AddCastMapping(CurrentPin, ConversionType->Get<0>(), ConversionType->Get<1>());
				}
			}
		}
	}
}

TOptional<TPair<FBPTerminal*, EKismetCompiledStatementType>>
InsertImplicitCastStatement(FKismetFunctionContext& Context, UEdGraphPin* DestinationPin, FBPTerminal* RHSTerm)
{
	using namespace UE::KismetCompiler::CastingUtils::Private;

	check(DestinationPin);
	check(RHSTerm);

	TOptional<TPair<FBPTerminal*, EKismetCompiledStatementType>> Result;

	const FImplicitCastParams* CastParams =
		Context.ImplicitCastMap.Find(DestinationPin);

	if (CastParams != nullptr)
	{
		check(CastParams->TargetTerminal);
		check(CastParams->TargetNode);

		FBlueprintCompiledStatement& CastStatement = Context.AppendStatementForNode(CastParams->TargetNode);
		CastStatement.LHS = CastParams->TargetTerminal;
		CastStatement.Type = CastParams->CastType;
		CastStatement.RHS.Add(RHSTerm);

		// Removal of the pin entry indicates to the compiler that the implicit cast has been processed.
		Context.ImplicitCastMap.Remove(DestinationPin);

		Result = TPair<FBPTerminal*, EKismetCompiledStatementType>{CastParams->TargetTerminal, CastParams->CastType};
	}

	return Result;
}

bool RemoveRegisteredImplicitCast(FKismetFunctionContext& Context, const UEdGraphPin* DestinationPin)
{
	check(DestinationPin);

	int32 RemovedCount = Context.ImplicitCastMap.Remove(DestinationPin);

	return (RemovedCount > 0);
}

TOptional<StatementNamePair> GetFloatingPointConversionType(const UEdGraphPin& SourcePin, const UEdGraphPin& DestinationPin)
{
	using namespace UE::KismetCompiler::CastingUtils::Private;

	using CastPair = TPair<EPinType, EPinType>;

	static TMap<CastPair, StatementNamePair> ImplicitCastTable =
	{
		{ CastPair{EPinType::Float,						EPinType::Double},					StatementNamePair{KCST_FloatToDoubleCast,								TEXT("WideningCast")}		},
		{ CastPair{EPinType::FloatArray,				EPinType::DoubleArray},				StatementNamePair{KCST_FloatToDoubleArrayCast,							TEXT("WideningArrayCast")}	},
		{ CastPair{EPinType::FloatSet,					EPinType::DoubleSet},				StatementNamePair{KCST_FloatToDoubleSetCast,							TEXT("WideningSetCast")}	},

		{ CastPair{EPinType::Double,					EPinType::Float},					StatementNamePair{KCST_DoubleToFloatCast,								TEXT("NarrowingCast")}		},
		{ CastPair{EPinType::DoubleArray,				EPinType::FloatArray},				StatementNamePair{KCST_DoubleToFloatArrayCast,							TEXT("NarrowingArrayCast")}	},
		{ CastPair{EPinType::DoubleSet,					EPinType::FloatSet},				StatementNamePair{KCST_DoubleToFloatSetCast,							TEXT("NarrowingSetCast")}	},

		{ CastPair{EPinType::Vector3f,					EPinType::Vector},					StatementNamePair{KCST_Vector3fToVectorCast,							TEXT("WideningCast")}		},
		{ CastPair{EPinType::Vector3fArray,				EPinType::VectorArray},				StatementNamePair{KCST_Vector3fToVectorArrayCast,						TEXT("WideningArrayCast")}	},
		{ CastPair{EPinType::Vector3fSet,				EPinType::VectorSet},				StatementNamePair{KCST_Vector3fToVectorSetCast,							TEXT("WideningSetCast")}	},

		{ CastPair{EPinType::Vector,					EPinType::Vector3f},				StatementNamePair{KCST_VectorToVector3fCast,							TEXT("NarrowingCast")}		},
		{ CastPair{EPinType::VectorArray,				EPinType::Vector3fArray},			StatementNamePair{KCST_VectorToVector3fArrayCast,						TEXT("NarrowingArrayCast")}	},
		{ CastPair{EPinType::VectorSet,					EPinType::Vector3fSet},				StatementNamePair{KCST_VectorToVector3fSetCast,							TEXT("NarrowingSetCast")}	},

		{ CastPair{EPinType::FloatKeyOtherValueMap,		EPinType::DoubleKeyOtherValueMap},	StatementNamePair{KCST_FloatToDoubleKeys_MapCast,						TEXT("MapCast")}	},
		{ CastPair{EPinType::DoubleKeyOtherValueMap,	EPinType::FloatKeyOtherValueMap},	StatementNamePair{KCST_DoubleToFloatKeys_MapCast,						TEXT("MapCast")}	},
		{ CastPair{EPinType::OtherKeyFloatValueMap,		EPinType::OtherKeyDoubleValueMap},	StatementNamePair{KCST_FloatToDoubleValues_MapCast,						TEXT("MapCast")}	},
		{ CastPair{EPinType::OtherKeyDoubleValueMap,	EPinType::OtherKeyFloatValueMap},	StatementNamePair{KCST_DoubleToFloatValues_MapCast,						TEXT("MapCast")}	},

		{ CastPair{EPinType::FloatKeyFloatValueMap,		EPinType::DoubleKeyDoubleValueMap},	StatementNamePair{KCST_FloatToDoubleKeys_FloatToDoubleValues_MapCast,	TEXT("MapCast")}	},
		{ CastPair{EPinType::FloatKeyFloatValueMap,		EPinType::FloatKeyDoubleValueMap},	StatementNamePair{KCST_FloatToDoubleValues_MapCast,						TEXT("MapCast")}	},
		{ CastPair{EPinType::FloatKeyFloatValueMap,		EPinType::DoubleKeyFloatValueMap},	StatementNamePair{KCST_FloatToDoubleKeys_MapCast,						TEXT("MapCast")}	},

		{ CastPair{EPinType::DoubleKeyFloatValueMap,	EPinType::DoubleKeyDoubleValueMap},	StatementNamePair{KCST_FloatToDoubleValues_MapCast,						TEXT("MapCast")}	},
		{ CastPair{EPinType::DoubleKeyFloatValueMap,	EPinType::FloatKeyDoubleValueMap},	StatementNamePair{KCST_DoubleToFloatKeys_FloatToDoubleValues_MapCast,	TEXT("MapCast")}	},
		{ CastPair{EPinType::DoubleKeyFloatValueMap,	EPinType::FloatKeyFloatValueMap},	StatementNamePair{KCST_DoubleToFloatKeys_MapCast,						TEXT("MapCast")}	},

		{ CastPair{EPinType::DoubleKeyDoubleValueMap,	EPinType::DoubleKeyFloatValueMap},	StatementNamePair{KCST_DoubleToFloatValues_MapCast,						TEXT("MapCast")}	},
		{ CastPair{EPinType::DoubleKeyDoubleValueMap,	EPinType::FloatKeyDoubleValueMap},	StatementNamePair{KCST_DoubleToFloatKeys_MapCast,						TEXT("MapCast")}	},
		{ CastPair{EPinType::DoubleKeyDoubleValueMap,	EPinType::FloatKeyFloatValueMap},	StatementNamePair{KCST_DoubleToFloatKeys_DoubleToFloatValues_MapCast,	TEXT("MapCast")}	},

		{ CastPair{EPinType::FloatKeyDoubleValueMap,	EPinType::DoubleKeyFloatValueMap},	StatementNamePair{KCST_FloatToDoubleKeys_DoubleToFloatValues_MapCast,	TEXT("MapCast")}	},
		{ CastPair{EPinType::FloatKeyDoubleValueMap,	EPinType::DoubleKeyDoubleValueMap},	StatementNamePair{KCST_FloatToDoubleKeys_MapCast,						TEXT("MapCast")}	},
		{ CastPair{EPinType::FloatKeyDoubleValueMap,	EPinType::FloatKeyFloatValueMap},	StatementNamePair{KCST_DoubleToFloatValues_MapCast,						TEXT("MapCast")}	},
	};

	CastPair LookupPair{ GetPinType(SourcePin), GetPinType(DestinationPin) };

	const StatementNamePair* TableLookupResult = ImplicitCastTable.Find(LookupPair);

	TOptional<StatementNamePair> FinalResult;

	if (TableLookupResult)
	{
		FinalResult = *TableLookupResult;
	}

	return FinalResult;
}

} // namespace UE::KismetCompiler::CastingUtils

