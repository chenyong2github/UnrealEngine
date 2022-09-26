// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenerateMutableSourceColor.h"

#include "GenerateMutableSource/GenerateMutableSourceFloat.h"
#include "GenerateMutableSource/GenerateMutableSourceImage.h"
#include "GenerateMutableSource/GenerateMutableSourceTable.h"

#include "Templates/UnrealTemplate.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "Engine/AssetManager.h"
#include "Engine/TextureLODSettings.h"
#include "ClothingAsset.h"
#include "MeshUtilities.h"
#include "EdGraphToken.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UnrealEditorPortabilityHelpers.h"
#include "PlatformInfo.h"

#include "CustomizableObjectCompiler.h"
#include "UnrealToMutableTextureConversionUtils.h"
#include "UnrealConversionUtils.h"
#include "CustomizableObjectEditorModule.h"
#include "CustomizableObject.h"
#include "GraphTraversal.h"

#include "MutableTools/Public/Node.h"
#include "MutableTools/Public/NodeScalarConstant.h"
#include "MutableTools/Public/NodeScalarEnumParameter.h"
#include "MutableTools/Public/NodeColourConstant.h"
#include "MutableTools/Public/NodeColourParameter.h"
#include "MutableTools/Public/NodeColourSwitch.h"
#include "MutableTools/Public/NodeColourSampleImage.h"
#include "MutableTools/Public/NodeColourArithmeticOperation.h"
#include "MutableTools/Public/NodeColourFromScalars.h"
#include "MutableTools/Public/NodeColourVariation.h"
#include "MutableTools/Public/NodeColourTable.h"
#include "MutableTools/Public/NodeImageBinarise.h"
#include "MutableTools/Public/NodeSurfaceNew.h"
#include "MutableTools/Public/Compiler.h"
#include "MutableTools/Public/Streams.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


mu::NodeColourPtr GenerateMutableSourceColor(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext)
{
	check(Pin)
	RETURN_ON_CYCLE(*Pin, GenerationContext)

	CheckNumOutputs(*Pin, GenerationContext);
	
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(Pin->GetOwningNode());

	const FGeneratedKey Key(reinterpret_cast<void*>(&GenerateMutableSourceColor), *Pin, *Node, GenerationContext);
	if (const FGeneratedData* Generated = GenerationContext.Generated.Find(Key))
	{
		return static_cast<mu::NodeColour*>(Generated->Node.get());
	}

	mu::NodeColourPtr Result;

	if (const UCustomizableObjectNodeColorConstant* TypedNodeColorConst = Cast<UCustomizableObjectNodeColorConstant>(Node))
	{
		mu::NodeColourConstantPtr ColorNode = new mu::NodeColourConstant();
		Result = ColorNode;

		ColorNode->SetValue(TypedNodeColorConst->Value.R, TypedNodeColorConst->Value.G, TypedNodeColorConst->Value.B);
	}

	else if (const UCustomizableObjectNodeColorParameter* TypedNodeColorParam = Cast<UCustomizableObjectNodeColorParameter>(Node))
	{
		mu::NodeColourParameterPtr ColorNode = new mu::NodeColourParameter();
		Result = ColorNode;

		GenerationContext.AddParameterNameUnique(Node, TypedNodeColorParam->ParameterName);

		ColorNode->SetName(TCHAR_TO_ANSI(*TypedNodeColorParam->ParameterName));
		ColorNode->SetUid(TCHAR_TO_ANSI(*GenerationContext.GetNodeIdUnique(Node).ToString()));
		ColorNode->SetDefaultValue(TypedNodeColorParam->DefaultValue.R, TypedNodeColorParam->DefaultValue.G, TypedNodeColorParam->DefaultValue.B);

		GenerationContext.ParameterUIDataMap.Add(TypedNodeColorParam->ParameterName, FParameterUIData(
			TypedNodeColorParam->ParameterName,
			TypedNodeColorParam->ParamUIMetadata,
			EMutableParameterType::Color));
	}

	else if (const UCustomizableObjectNodeColorSwitch* TypedNodeColorSwitch = Cast<UCustomizableObjectNodeColorSwitch>(Node))
	{
		Result = [&]()
		{
			if (const int32 NumParameters = FollowInputPinArray(*TypedNodeColorSwitch->SwitchParameter()).Num();
				NumParameters != 1)
			{
				const FText Message = NumParameters
					? LOCTEXT("NoEnumParamInSwitch", "Switch nodes must have an enum switch parameter. Please connect an enum and refesh the switch node.")
					: LOCTEXT("InvalidEnumInSwitch", "Switch nodes must have a single enum with all the options inside. Please remove all the enums but one and refresh the switch node.");

				GenerationContext.Compiler->CompilerLog(Message, Node);
				return Result;
			}

			const UEdGraphPin* EnumPin = FollowInputPin(*TypedNodeColorSwitch->SwitchParameter());
			mu::NodeScalarPtr SwitchParam = EnumPin ? GenerateMutableSourceFloat(EnumPin, GenerationContext) : nullptr;

			// Switch Param not generated
			if (!SwitchParam)
			{
				// Warn about a failure.
				if (EnumPin)
				{
					const FText Message = LOCTEXT("FailedToGenerateSwitchParam", "Could not generate switch enum parameter. Please refesh the switch node and connect an enum.");
					GenerationContext.Compiler->CompilerLog(Message, Node);
				}

				return Result;
			}

			if (SwitchParam->GetType() != mu::NodeScalarEnumParameter::GetStaticType())
			{
				const FText Message = LOCTEXT("WrongSwitchParamType", "Switch parameter of incorrect type.");
				GenerationContext.Compiler->CompilerLog(Message, Node);

				return Result;
			}

			const int32 NumSwitchOptions = TypedNodeColorSwitch->GetNumElements();

			mu::NodeScalarEnumParameter* EnumParameter = static_cast<mu::NodeScalarEnumParameter*>(SwitchParam.get());
			if (NumSwitchOptions != EnumParameter->GetValueCount())
			{
				const FText Message = LOCTEXT("MismatchedSwitch", "Switch enum and switch node have different number of options. Please refresh the switch node to make sure the outcomes are labeled properly.");
				GenerationContext.Compiler->CompilerLog(Message, Node);
			}

			mu::NodeColourSwitchPtr SwitchNode = new mu::NodeColourSwitch;
			SwitchNode->SetParameter(SwitchParam);
			SwitchNode->SetOptionCount(NumSwitchOptions);

			for (int SelectorIndex = 0; SelectorIndex < NumSwitchOptions; ++SelectorIndex)
			{
				const UEdGraphPin* const ColorPin = TypedNodeColorSwitch->GetElementPin(SelectorIndex);
				if (const UEdGraphPin* ConnectedPin = FollowInputPin(*ColorPin))
				{
					SwitchNode->SetOption(SelectorIndex, GenerateMutableSourceColor(ConnectedPin, GenerationContext));
				}
			}

			return static_cast<mu::NodeColourPtr>(SwitchNode);
		}(); // invoke lambda;
	}

	else if (const UCustomizableObjectNodeTextureSample* TypedNodeTexSample = Cast<UCustomizableObjectNodeTextureSample>(Node))
	{
		mu::NodeColourSampleImagePtr ColorNode = new mu::NodeColourSampleImage();
		Result = ColorNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeTexSample->TexturePin()))
		{
			mu::NodeImagePtr TextureNode = GenerateMutableSourceImage(ConnectedPin, GenerationContext, 0.f);
			ColorNode->SetImage(TextureNode);
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeTexSample->XPin()))
		{
			mu::NodeScalarPtr XNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			ColorNode->SetX(XNode);
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeTexSample->YPin()))
		{
			mu::NodeScalarPtr YNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			ColorNode->SetY(YNode);
		}
	}

	else if (const UCustomizableObjectNodeColorArithmeticOp* TypedNodeColorArith = Cast<UCustomizableObjectNodeColorArithmeticOp>(Node))
	{
		mu::NodeColourArithmeticOperationPtr OpNode = new mu::NodeColourArithmeticOperation();
		Result = OpNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeColorArith->XPin()))
		{
			mu::NodeColourPtr XNode = GenerateMutableSourceColor(ConnectedPin, GenerationContext);
			OpNode->SetA(XNode);
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeColorArith->YPin()))
		{
			mu::NodeColourPtr YNode = GenerateMutableSourceColor(ConnectedPin, GenerationContext);
			OpNode->SetB(YNode);
		}

		switch (TypedNodeColorArith->Operation)
		{
		case EColorArithmeticOperation::E_Add:
			OpNode->SetOperation(mu::NodeColourArithmeticOperation::OPERATION::AO_ADD);
			break;

		case EColorArithmeticOperation::E_Sub:
			OpNode->SetOperation(mu::NodeColourArithmeticOperation::OPERATION::AO_SUBTRACT);
			break;

		case EColorArithmeticOperation::E_Mul:
			OpNode->SetOperation(mu::NodeColourArithmeticOperation::OPERATION::AO_MULTIPLY);
			break;

		case EColorArithmeticOperation::E_Div:
			OpNode->SetOperation(mu::NodeColourArithmeticOperation::OPERATION::AO_DIVIDE);
			break;
		}
	}

	else if (const UCustomizableObjectNodeColorFromFloats* TypedNodeFrom = Cast<UCustomizableObjectNodeColorFromFloats>(Node))
	{
		mu::NodeColourFromScalarsPtr OpNode = new mu::NodeColourFromScalars();
		Result = OpNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeFrom->RPin()))
		{
			mu::NodeScalarPtr FloatNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			OpNode->SetX(FloatNode);
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeFrom->GPin()))
		{
			mu::NodeScalarPtr FloatNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			OpNode->SetY(FloatNode);
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeFrom->BPin()))
		{
			mu::NodeScalarPtr FloatNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			OpNode->SetZ(FloatNode);
		}

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeFrom->APin()))
		{
			mu::NodeScalarPtr FloatNode = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);
			OpNode->SetW(FloatNode);
		}
	}

	else if (const UCustomizableObjectNodeColorVariation* TypedNodeColorVar = Cast<const UCustomizableObjectNodeColorVariation>(Node))
	{
		mu::NodeColourVariationPtr ColorNode = new mu::NodeColourVariation();
		Result = ColorNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeColorVar->DefaultPin()))
		{
			mu::NodeColourPtr ChildNode = GenerateMutableSourceColor(ConnectedPin, GenerationContext);
			if (ChildNode)
			{
				ColorNode->SetDefaultColour(ChildNode.get());
			}
			else
			{
				GenerationContext.Compiler->CompilerLog(LOCTEXT("ColorFailed", "Color generation failed."), Node);
			}
		}
		else
		{
			GenerationContext.Compiler->CompilerLog(LOCTEXT("ColorVarMissingDef", "Color variation node requires a default value."), Node);
		}

		ColorNode->SetVariationCount(TypedNodeColorVar->Variations.Num());
		for (int VariationIndex = 0; VariationIndex < TypedNodeColorVar->Variations.Num(); ++VariationIndex)
		{
			const UEdGraphPin* VariationPin = TypedNodeColorVar->VariationPin(VariationIndex);
			if (!VariationPin) continue;

			ColorNode->SetVariationTag(VariationIndex, TCHAR_TO_ANSI(*TypedNodeColorVar->Variations[VariationIndex].Tag));
			if (const UEdGraphPin* ConnectedPin = FollowInputPin(*VariationPin))
			{
				mu::NodeColourPtr ChildNode = GenerateMutableSourceColor(ConnectedPin, GenerationContext);
				ColorNode->SetVariationColour(VariationIndex, ChildNode.get());
			}
		}
	}

	else if (const UCustomizableObjectNodeTable* TypedNodeTable = Cast<UCustomizableObjectNodeTable>(Node))
	{
		mu::NodeColourTablePtr ColorTableNode = new mu::NodeColourTable();
		Result = ColorTableNode;

		mu::TablePtr Table;
		if (TypedNodeTable->Table)
		{
			FString ColumnName = Pin->PinFriendlyName.ToString();

			if (Pin->PinType.PinCategory == Schema->PC_MaterialAsset)
			{
				ColumnName = GenerationContext.CurrentMaterialTableParameterId;
			}

			Table = GenerateMutableSourceTable(TypedNodeTable->Table->GetName(), Pin, GenerationContext);

			ColorTableNode->SetTable(Table);
			ColorTableNode->SetColumn(TCHAR_TO_ANSI(*ColumnName));
			ColorTableNode->SetParameterName(TCHAR_TO_ANSI(*TypedNodeTable->ParameterName));

			GenerationContext.AddParameterNameUnique(Node, TypedNodeTable->ParameterName);

			if (Table->FindColumn(TCHAR_TO_ANSI(*ColumnName)) == -1)
			{
				FString Msg = FString::Printf(TEXT("Couldn't find pin column with name %s"), *ColumnName);
				GenerationContext.Compiler->CompilerLog(FText::FromString(Msg), Node);
			}

		}
		else
		{
			Table = new mu::Table();
			ColorTableNode->SetTable(Table);

			GenerationContext.Compiler->CompilerLog(LOCTEXT("ColorTableError", "Couldn't find the data table of the node."), Node);
		}
	}

	else
	{
		GenerationContext.Compiler->CompilerLog(LOCTEXT("UnimplementedNode", "Node type not implemented yet."), Node);
	}

	FGeneratedData CacheData = FGeneratedData(Node, Result);
	GenerationContext.Generated.Add(Key, CacheData);
	GenerationContext.GeneratedNodes.Add(Node);
	
	if (Result)
	{
		Result->SetMessageContext(Node);
	}

	return Result;
}
