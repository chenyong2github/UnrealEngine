// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_MakeCurveExpressionMap.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "Internationalization/Regex.h"
#include "KismetCompiledFunctionContext.h"
#include "KismetCompilerMisc.h"
#include "String/ParseLines.h"
#include "Textures/SlateIcon.h"

#define LOCTEXT_NAMESPACE "MakeCurveExpressionMap"

// ---------------------------------------------------------------------------------------------
class FNodeHandler_MakeCurveExpressionMap final :
	public FNodeHandlingFunctor
{
public:
	FNodeHandler_MakeCurveExpressionMap(FKismetCompilerContext& InCompilerContext) :
		FNodeHandlingFunctor(InCompilerContext)
	{
		
	}
	
	virtual void RegisterNets(FKismetFunctionContext& InContext, UEdGraphNode* InNode) override
	{
		FNodeHandlingFunctor::RegisterNets(InContext, InNode);
		
		UEdGraphPin* OutputPin = CastChecked<UK2Node_MakeCurveExpressionMap>(InNode)->GetOutputPin();

		FBPTerminal* Terminal = InContext.CreateLocalTerminalFromPinAutoChooseScope(OutputPin, InContext.NetNameMap->MakeValidName(OutputPin));
		Terminal->bPassedByReference = false;
		Terminal->Source = InNode;
		InContext.NetMap.Add(OutputPin, Terminal);
	}
	
	virtual void Compile(FKismetFunctionContext& InContext, UEdGraphNode* InNode) override
	{
		const UK2Node_MakeCurveExpressionMap* MapNode = CastChecked<UK2Node_MakeCurveExpressionMap>(InNode);
		const UEdGraphPin* OutputPin = MapNode->GetOutputPin();

		FBPTerminal** ContainerTerm = InContext.NetMap.Find(OutputPin);
		if (!ensure(ContainerTerm))
		{
			return;
		}

		// Create a statement that assembles the map as a pile of literals.
		FBlueprintCompiledStatement& CreateMapStatement = InContext.AppendStatementForNode(InNode);
		CreateMapStatement.Type = KCST_CreateMap;
		CreateMapStatement.LHS = *ContainerTerm;

		FEdGraphPinType KeyType, ValueType;
		KeyType.PinCategory = UEdGraphSchema_K2::PC_Name;
		ValueType.PinCategory = UEdGraphSchema_K2::PC_String;
		
		for(TTuple<FName, FString>& Item: MapNode->GetExpressionMap())
		{
			FBPTerminal* KeyTerminal = new FBPTerminal();
			KeyTerminal->Name = Item.Key.ToString();
			KeyTerminal->Type = KeyType;
			KeyTerminal->Source = InNode;
			KeyTerminal->bIsLiteral = true;
			InContext.Literals.Add(KeyTerminal);
			
			FBPTerminal* ValueTerminal = new FBPTerminal();
			ValueTerminal->Name = MoveTemp(Item.Value);
			ValueTerminal->Type = ValueType;
			ValueTerminal->Source = InNode;
			ValueTerminal->bIsLiteral = true;
			InContext.Literals.Add(ValueTerminal);

			CreateMapStatement.RHS.Add(KeyTerminal);
			CreateMapStatement.RHS.Add(ValueTerminal);
		}
	}
};


// ---------------------------------------------------------------------------------------------

const FName UK2Node_MakeCurveExpressionMap::OutputPinName(TEXT("Map"));


UK2Node_MakeCurveExpressionMap::UK2Node_MakeCurveExpressionMap()
{
}


UEdGraphPin* UK2Node_MakeCurveExpressionMap::GetOutputPin() const
{
	return FindPin(OutputPinName);
}


TMap<FName, FString> UK2Node_MakeCurveExpressionMap::GetExpressionMap() const
{
	TMap<FName, FString> ExpressionMap;
	UE::String::ParseLines(Expressions.AssignmentExpressions, [&ExpressionMap](FStringView InLine)
	{
		int32 AssignmentPos;
		if (InLine.FindChar('=', AssignmentPos))
		{
			FStringView Target = InLine.Left(AssignmentPos).TrimStartAndEnd();
			FStringView Source = InLine.Mid(AssignmentPos + 1).TrimStartAndEnd();
			if (!Target.IsEmpty() && !Source.IsEmpty())
			{
				ExpressionMap.Add(FName(Target), FString(Source));
			}
		}
	});
	
	return ExpressionMap;
}


void UK2Node_MakeCurveExpressionMap::AllocateDefaultPins()
{
	FCreatePinParams PinParams;
	PinParams.ContainerType = EPinContainerType::Map;
	PinParams.ValueTerminalType.TerminalCategory = UEdGraphSchema_K2::PC_String;
	
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Name, OutputPinName, PinParams);
}


FText UK2Node_MakeCurveExpressionMap::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Make Expression Map");
}


FText UK2Node_MakeCurveExpressionMap::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Create an expression map from a list of assignment expressions");
}


FSlateIcon UK2Node_MakeCurveExpressionMap::GetIconAndTint(FLinearColor& OutColor) const
{
	static FSlateIcon Icon("EditorStyle", "GraphEditor.MakeMap_16x");
	return Icon;
}


void UK2Node_MakeCurveExpressionMap::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	// FIXME: Validate expressions.
	Super::ValidateNodeDuringCompilation(MessageLog);
}


void UK2Node_MakeCurveExpressionMap::GetMenuActions(FBlueprintActionDatabaseRegistrar& InActionRegistrar) const
{
	const UClass* ActionKey = GetClass();
	if (InActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		InActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}


FNodeHandlingFunctor* UK2Node_MakeCurveExpressionMap::CreateNodeHandler(FKismetCompilerContext& CompilerContext) const
{
	return new FNodeHandler_MakeCurveExpressionMap(CompilerContext);
}


FText UK2Node_MakeCurveExpressionMap::GetMenuCategory() const
{
	return LOCTEXT("NodeCategory", "Curve Expression");
}

#undef LOCTEXT_NAMESPACE
