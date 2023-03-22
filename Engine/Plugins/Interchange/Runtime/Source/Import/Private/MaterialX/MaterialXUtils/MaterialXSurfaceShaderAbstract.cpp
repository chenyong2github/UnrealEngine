// Copyright Epic Games, Inc. All Rights Reserved. 

#if WITH_EDITOR
#include "MaterialX/MaterialXUtils/MaterialXSurfaceShaderAbstract.h"

#include "InterchangeImportLog.h"
#include "InterchangeManager.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTextureBlurNode.h"
#include "InterchangeTranslatorBase.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionVectorNoise.h"
#include "MaterialX/MaterialXUtils/MaterialXManager.h"

#define LOCTEXT_NAMESPACE "MaterialXSurfaceShaderAbstract"

namespace mx = MaterialX;

FMaterialXSurfaceShaderAbstract::FMaterialXSurfaceShaderAbstract(UInterchangeBaseNodeContainer& BaseNodeContainer)
	: FMaterialXBase(BaseNodeContainer)
{}

bool FMaterialXSurfaceShaderAbstract::AddAttribute(MaterialX::InputPtr Input, const FString& InputChannelName, UInterchangeShaderNode* ShaderNode) const
{
	if(Input)
	{
		if(Input->getType() == mx::Type::Float)
		{
			return ShaderNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputChannelName), mx::fromValueString<float>(Input->getValueString()));
		}
		else if(Input->getType() == mx::Type::Integer) //Let's add it a Float attribute, because Interchange doesn't create a scalar if it's an int
		{
			return ShaderNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputChannelName), mx::fromValueString<int32>(Input->getValueString()));
		}
		else if(Input->getType() == mx::Type::Color3 || Input->getType() == mx::Type::Color4)
		{
			FLinearColor LinearColor = GetLinearColor(Input);
			return ShaderNode->AddLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputChannelName), LinearColor);
		}
		else if(Input->getType() == mx::Type::Vector2)
		{
			FLinearColor Vector = GetVector(Input);
			return ShaderNode->AddVector2Attribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputChannelName), FVector2f{ Vector.R, Vector.G });
		}
		else if(Input->getType() == mx::Type::Vector3 || Input->getType() == mx::Type::Vector4)
		{
			FLinearColor Vector = GetVector(Input);
			return ShaderNode->AddLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputChannelName), Vector);
		}
	}

	return false;
}

bool FMaterialXSurfaceShaderAbstract::AddAttributeFromValueOrInterface(MaterialX::InputPtr Input, const FString& InputChannelName, UInterchangeShaderNode* ShaderNode) const
{
	if(Input)
	{
		if(Input->hasValue())
		{
			return AddAttribute(Input, InputChannelName, ShaderNode);
		}
		else if(Input->hasInterfaceName())
		{
			if(mx::InputPtr InputInterface = Input->getInterfaceInput(); InputInterface->hasValue())
			{
				return AddAttribute(InputInterface, InputChannelName, ShaderNode);
			}
		}
	}

	return false;
}

bool FMaterialXSurfaceShaderAbstract::AddFloatAttribute(MaterialX::InputPtr Input, const FString& InputChannelName, UInterchangeShaderNode* ShaderNode, float DefaultValue) const
{
	if(Input)
	{
		if(Input->hasValue())
		{
			float Value = mx::fromValueString<float>(Input->getValueString());

			if(!FMath::IsNearlyEqual(Value, DefaultValue))
			{
				return ShaderNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputChannelName), Value);
			}
		}
	}

	return false;
}

bool FMaterialXSurfaceShaderAbstract::AddLinearColorAttribute(MaterialX::InputPtr Input, const FString& InputChannelName, UInterchangeShaderNode* ShaderNode, const FLinearColor& DefaultValue) const
{
	if(Input)
	{
		if(Input->hasValue())
		{
			const FLinearColor Value = GetLinearColor(Input);

			if(!Value.Equals(DefaultValue))
			{
				return ShaderNode->AddLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputChannelName), Value);
			}
		}
	}

	return false;
}

bool FMaterialXSurfaceShaderAbstract::ConnectNodeGraphOutputToInput(MaterialX::InputPtr InputToNodeGraph, UInterchangeShaderNode* ShaderNode, const FString& ParentInputName)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	bool bHasNodeGraph = false;

	if(InputToNodeGraph->hasNodeGraphString())
	{
		bHasNodeGraph = true;

		mx::OutputPtr Output = InputToNodeGraph->getConnectedOutput();

		if(!Output)
		{
			UE_LOG(LogInterchangeImport, Warning, TEXT("Couldn't find a connected output to (%s)"), *GetInputName(InputToNodeGraph));
			return false;
		}

		for(mx::Edge Edge : Output->traverseGraph())
		{
			ConnectNodeCategoryOutputToInput(Edge, ShaderNode, ParentInputName);
		}
	}

	return bHasNodeGraph;
}

bool FMaterialXSurfaceShaderAbstract::ConnectMatchingNodeOutputToInput(MaterialX::NodePtr Node, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	FMaterialXManager& Manager = FMaterialXManager::GetInstance();

	bool bIsConnected = false;

	// First search a matching Material Expression
	if(const FString* ShaderType = Manager.FindMatchingMaterialExpression(Node->getCategory().c_str()))
	{
		UInterchangeShaderNode* OperatorNode = nullptr;

		OperatorNode = CreateShaderNode(Node->getName().c_str(), *ShaderType);

		for(mx::InputPtr Input : Node->getInputs())
		{
			if(const FString* InputNameFound = Manager.FindMaterialExpressionInput(GetInputName(Input)))
			{
				AddAttributeFromValueOrInterface(Input, *InputNameFound, OperatorNode);
			}
		}

		bIsConnected = UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, OperatorNode->GetUniqueID());
	}
	else if(FOnConnectNodeOutputToInput* Delegate = MatchingConnectNodeDelegates.Find(Node->getCategory().c_str()))
	{
		bIsConnected = Delegate->ExecuteIfBound(Node, ParentShaderNode, InputChannelName);
	}

	return bIsConnected;
}

void FMaterialXSurfaceShaderAbstract::ConnectNodeCategoryOutputToInput(const MaterialX::Edge& Edge, UInterchangeShaderNode* ShaderNode, const FString& ParentInputName)
{
	if(mx::NodePtr UpstreamNode = Edge.getUpstreamElement()->asA<mx::Node>())
	{
		UInterchangeShaderNode* ParentShaderNode = ShaderNode;
		FString InputChannelName = ParentInputName;

		//Replace the input's name by the one used in UE
		SetMatchingInputsNames(UpstreamNode);

		if(mx::ElementPtr DownstreamElement = Edge.getDownstreamElement())
		{
			if(mx::NodePtr DownstreamNode = DownstreamElement->asA<mx::Node>())
			{
				if(UInterchangeShaderNode** FoundNode = ShaderNodes.Find(GetAttributeParentName(DownstreamNode)))// DownstreamNode->getName().c_str()))
				{
					ParentShaderNode = *FoundNode;
				}

				if(mx::InputPtr ConnectedInput = Edge.getConnectingElement()->asA<mx::Input>())
				{
					InputChannelName = GetInputName(ConnectedInput);
				}
			}
		}

		if(!ConnectMatchingNodeOutputToInput(UpstreamNode, ParentShaderNode, InputChannelName))
		{
			UE_LOG(LogInterchangeImport, Warning, TEXT("<%s> is not supported yet"), ANSI_TO_TCHAR(UpstreamNode->getCategory().c_str()));
		}
	}
}

bool FMaterialXSurfaceShaderAbstract::ConnectNodeNameOutputToInput(MaterialX::InputPtr InputToConnectedNode, UInterchangeShaderNode* ShaderNode, const FString& ParentInputName)
{
	mx::NodePtr ConnectedNode = InputToConnectedNode->getConnectedNode();

	if(!ConnectedNode)
	{
		return false;
	}

	UInterchangeShaderNode* ParentShaderNode = ShaderNode;
	FString InputChannelName = ParentInputName;

	mx::Edge Edge(nullptr, InputToConnectedNode, ConnectedNode);

	TArray<mx::Edge> Stack{ Edge };

	while(!Stack.IsEmpty())
	{
		Edge = Stack.Pop();

		if(Edge.getUpstreamElement())
		{
			ConnectNodeCategoryOutputToInput(Edge, ShaderNode, ParentInputName);
			ConnectedNode = Edge.getUpstreamElement()->asA<mx::Node>();
			for(mx::InputPtr Input : ConnectedNode->getInputs())
			{
				Stack.Emplace(ConnectedNode, Input, Input->getConnectedNode());
			}
		}
	}

	return true;
}

void FMaterialXSurfaceShaderAbstract::ConnectConstantInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	AddAttributeFromValueOrInterface(UpstreamNode->getInput("value"), InputChannelName, ParentShaderNode);
}

void FMaterialXSurfaceShaderAbstract::ConnectExtractInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	UInterchangeShaderNode* MaskShaderNode = CreateShaderNode(UpstreamNode->getName().c_str(), Mask::Name.ToString());

	if(mx::InputPtr InputIndex = UpstreamNode->getInput("index"))
	{
		const int32 Index = mx::fromValueString<int>(InputIndex->getValueString());
		switch(Index)
		{
		case 0: MaskShaderNode->AddBooleanAttribute(Mask::Attributes::R, true); break;
		case 1: MaskShaderNode->AddBooleanAttribute(Mask::Attributes::G, true); break;
		case 2: MaskShaderNode->AddBooleanAttribute(Mask::Attributes::B, true); break;
		case 3: MaskShaderNode->AddBooleanAttribute(Mask::Attributes::A, true); break;
		default:
			UE_LOG(LogInterchangeImport, Warning, TEXT("Wrong index number for extract node, values are from [0-3]"));
			break;
		}
	}

	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, MaskShaderNode->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectDotInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	if(mx::InputPtr Input = UpstreamNode->getInput("in"))
	{
		SetAttributeNewName(Input, TCHAR_TO_UTF8(*InputChannelName)); //let's take the parent node's input name
		ShaderNodes.Add(UpstreamNode->getName().c_str(), ParentShaderNode);
	}
}

void FMaterialXSurfaceShaderAbstract::ConnectTransformPositionInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	UInterchangeShaderNode* TransformNode = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("TransformPosition"));
	AddAttributeFromValueOrInterface(UpstreamNode->getInput("in"), TEXT("Input"), TransformNode);
	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, TransformNode->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectTransformVectorInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	UInterchangeShaderNode* TransformNode = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("Transform"));
	AddAttributeFromValueOrInterface(UpstreamNode->getInput("in"), TEXT("Input"), TransformNode);
	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, TransformNode->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectRotate3DInputToOutput(mx::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	UInterchangeShaderNode* Rotate3DNode = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("RotateAboutAxis"));
	Rotate3DNode->AddLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(TEXT("PivotPoint")), FLinearColor(0.5, 0.5, 0));

	AddAttributeFromValueOrInterface(UpstreamNode->getInput("in"), TEXT("Position"), Rotate3DNode);
	AddAttributeFromValueOrInterface(UpstreamNode->getInput("axis"), TEXT("NormalizedRotationAxis"), Rotate3DNode);

	// we create a Divide node to convert MaterialX angle in degrees to UE's angle which is a value between [0,1]
	if(mx::InputPtr Input = UpstreamNode->getInput("amount"))
	{
		if(!AddAttributeFromValueOrInterface(Input, TEXT("RotationAngle"), Rotate3DNode))
		{
			//copy "in" input into "in1" and create "amount" input as "in2" with the value 360 because UE's angle is a value between [0,1]
			mx::NodePtr NewDivideNode = CreateNode(UpstreamNode->getParent()->asA<mx::NodeGraph>(),
												   UpstreamNode->getName().c_str(),
												   mx::Category::Divide,
												   { {"in1", Input} }, // rename input to match <divide> input "in1"
												   { {"in2", FAttributeValueArray{{"type", "float"}, {"value", "360"}}} });

			// Input now points to the new node
			Input->setNodeName(NewDivideNode->getName());
		}
	}

	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, Rotate3DNode->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectImageInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	if(UInterchangeTextureNode* TextureNode = CreateTextureNode<UInterchangeTexture2DNode>(UpstreamNode))
	{
		//By default set the output of a texture to RGB
		FString OutputChannel{ TEXT("RGB") };

		if(UpstreamNode->getType() == mx::Type::Vector4 || UpstreamNode->getType() == mx::Type::Color4)
		{
			OutputChannel = TEXT("RGBA");
		}
		else if(UpstreamNode->getType() == mx::Type::Float)
		{
			OutputChannel = TEXT("R");
		}

		UInterchangeShaderNode* TextureShaderNode = CreateShaderNode(UpstreamNode->getName().c_str(), TextureSample::Name.ToString());
		TextureShaderNode->AddStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(TextureSample::Inputs::Texture.ToString()), TextureNode->GetUniqueID());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ParentShaderNode, InputChannelName, TextureShaderNode->GetUniqueID(), OutputChannel);
	}
	else
	{
		AddAttributeFromValueOrInterface(UpstreamNode->getInput(mx::NodeGroup::Texture2D::Inputs::Default), InputChannelName, ParentShaderNode);
	}
}

void FMaterialXSurfaceShaderAbstract::ConnectConvertInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	//In case of an upwards conversion, let's do an append, downwards a mask, otherwise leave as is
	mx::InputPtr Input = UpstreamNode->getInput("in");
	const std::string& NodeType = UpstreamNode->getType();
	const std::string& InputType = Input->getType();

	//Ensure that the types are supported
	const bool bIsNodeTypeSupported =
		NodeType == mx::Type::Color4 ||
		NodeType == mx::Type::Color3 ||
		NodeType == mx::Type::Vector4 ||
		NodeType == mx::Type::Vector3 ||
		NodeType == mx::Type::Vector2 ||
		NodeType == mx::Type::Float ||
		NodeType == mx::Type::Integer ||
		NodeType == mx::Type::Boolean;

	const bool bIsInputTypeSupported =
		InputType == mx::Type::Color4 ||
		InputType == mx::Type::Color3 ||
		InputType == mx::Type::Vector4 ||
		InputType == mx::Type::Vector3 ||
		InputType == mx::Type::Vector2 ||
		InputType == mx::Type::Float ||
		InputType == mx::Type::Integer ||
		InputType == mx::Type::Boolean;

	if(!bIsNodeTypeSupported || !bIsInputTypeSupported)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("<convert> node has non supported types"));
		return;
	}

	char NodeN = NodeType.back();
	char InputN = InputType.back();

	constexpr auto Remap = [](auto Value, auto Low1, auto High1, auto Low2, auto High2)
	{
		return Low2 + (Value - Low1) * (High2 - Low2) / (High1 - Low1);
	};

	//Remap NodeN and InputN in a range lower than '2' in case of integer/boolean/float
	if(NodeN > '4')
	{
		NodeN = Remap(NodeN, 'a', 'z', 0, '1');
	}
	if(InputN > '4')
	{
		InputN = Remap(InputN, 'a', 'z', 0, '1');
	}

	if(InputN > NodeN) // Mask
	{
		UInterchangeShaderNode* MaskShaderNode = nullptr;
		if(NodeN == '3')
		{
			MaskShaderNode = CreateMaskShaderNode(0b1110, UpstreamNode->getName().c_str());
		}
		else if(NodeN == '2')
		{
			MaskShaderNode = CreateMaskShaderNode(0b1100, UpstreamNode->getName().c_str());
		}
		else
		{
			MaskShaderNode = CreateMaskShaderNode(0b1000, UpstreamNode->getName().c_str());
		}

		AddAttributeFromValueOrInterface(Input, TEXT("Input"), MaskShaderNode);

		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, MaskShaderNode->GetUniqueID());
	}
	else // Append 
	{
		//same as dot, just connect the next output to this parent input
		SetAttributeNewName(Input, TCHAR_TO_UTF8(*InputChannelName)); //let's take the parent node's input name
		ShaderNodes.Add(UpstreamNode->getName().c_str(), ParentShaderNode);

		//No need to create a node, since the input and the node have the same channels, we just check if there's a value
		if(NodeN == InputN || (NodeN < '2' && InputN < '2'))
		{
			AddAttributeFromValueOrInterface(Input, InputChannelName, ParentShaderNode);
			return;
		}
		std::string Category;
		TArray<FInputToCopy> InputsToCopy;
		TArray<FInputToCreate> InputsToCreate;

		// float to N
		if(InputN < '2')
		{
			if(NodeN == '2')
			{
				Category = mx::Category::Combine2;
				InputsToCopy.Add({ "in1", Input });
				InputsToCopy.Add({ "in2", Input });
			}
			else if(NodeN == '3')
			{
				Category = mx::Category::Combine3;
				InputsToCopy.Add({ "in1", Input });
				InputsToCopy.Add({ "in2", Input });
				InputsToCopy.Add({ "in3", Input });
			}
			else if(NodeN == '4')
			{
				Category = mx::Category::Combine4;
				InputsToCopy.Add({ "in1", Input });
				InputsToCopy.Add({ "in2", Input });
				InputsToCopy.Add({ "in3", Input });
				InputsToCopy.Add({ "in4", Input });
			}
		}
		else if((InputN == '2' && NodeN == '3') || (InputN == '3' && NodeN == '4'))
		{
			Category = mx::Category::Combine2;
			InputsToCopy.Add({ "in1", Input });
			InputsToCreate.Add({ "in2", FAttributeValueArray{{"type","float"}, {"value","1"}} });
		}

		//copy "in" input into "in1" and create "amount" input as "in2" with the value 360 because UE's angle is a value between [0,1]
		mx::NodePtr CombineNode = CreateNode(UpstreamNode->getParent()->asA<mx::NodeGraph>(),
											 UpstreamNode->getName().c_str(),
											 Category.c_str(),
											 InputsToCopy, // rename input to match <divide> input "in1"
											 InputsToCreate);

		// Input now points to the new node
		Input->setNodeName(CombineNode->getName());
	}
}

void FMaterialXSurfaceShaderAbstract::ConnectIfGreaterInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	UInterchangeShaderNode* NodeIf = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("If"));
	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, NodeIf->GetUniqueID());

	if(mx::InputPtr Input = UpstreamNode->getInput("value1"))
	{
		AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
	}

	if(mx::InputPtr Input = UpstreamNode->getInput("value2"))
	{
		AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
	}

	if(mx::InputPtr Input = UpstreamNode->getInput("in1"))
	{
		AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
	}

	//In that case we also need to add an attribute to AEqualsB
	mx::InputPtr Input = UpstreamNode->getInput("in2");
	if(Input)
	{
		bool bHasValue = AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
		AddAttributeFromValueOrInterface(Input, TEXT("AEqualsB"), NodeIf);

		if(bHasValue)
		{
			//Let's add a new input that is a copy of in2 to connect it to the equal input
			mx::InputPtr Input3 = UpstreamNode->addInput("in3");
			Input3->copyContentFrom(Input);
			SetAttributeNewName(Input3, "AEqualsB");
		}
	}
}

void FMaterialXSurfaceShaderAbstract::ConnectIfGreaterEqInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	UInterchangeShaderNode* NodeIf = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("If"));
	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, NodeIf->GetUniqueID());

	if(mx::InputPtr Input = UpstreamNode->getInput("value1"))
	{
		AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
	}

	if(mx::InputPtr Input = UpstreamNode->getInput("value2"))
	{
		AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
	}

	if(mx::InputPtr Input = UpstreamNode->getInput("in2"))
	{
		AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
	}

	//In that case we also need to add an attribute to AEqualsB
	mx::InputPtr Input = UpstreamNode->getInput("in1");
	if(Input)
	{
		bool bHasValue = AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
		AddAttributeFromValueOrInterface(Input, TEXT("AEqualsB"), NodeIf);

		if(bHasValue)
		{
			//Let's add a new input that is a copy of in2 to connect it to the equal input
			mx::InputPtr Input3 = UpstreamNode->addInput("in3");
			Input3->copyContentFrom(Input);
			SetAttributeNewName(Input3, "AEqualsB");
		}
	}
}

void FMaterialXSurfaceShaderAbstract::ConnectIfEqualInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	UInterchangeShaderNode* NodeIf = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("If"));
	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, NodeIf->GetUniqueID());

	if(mx::InputPtr Input = UpstreamNode->getInput("value1"); Input && Input->hasValue())
	{
		AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
	}

	if(mx::InputPtr Input = UpstreamNode->getInput("value2"); Input && Input->hasValue())
	{
		AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
	}

	if(mx::InputPtr Input = UpstreamNode->getInput("in1"); Input && Input->hasValue())
	{
		AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
	}

	//In that case we also need to add an attribute to AGreaterThanB
	mx::InputPtr Input = UpstreamNode->getInput("in2");
	if(Input)
	{
		bool bHasValue = AddAttributeFromValueOrInterface(Input, GetInputName(Input), NodeIf);
		AddAttributeFromValueOrInterface(Input, TEXT("AGreaterThanB"), NodeIf);

		if(bHasValue)
		{
			//Let's add a new input that is a copy of in2 to connect it to the equal input
			mx::InputPtr Input3 = UpstreamNode->addInput("in3");
			Input3->copyContentFrom(Input);
			SetAttributeNewName(Input3, "AGreaterThanB");
		}
	}
}

void FMaterialXSurfaceShaderAbstract::ConnectOutsideInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	//in * (1 - mask)
	UInterchangeShaderNode* NodeMultiply = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("Multiply"));
	AddAttributeFromValueOrInterface(UpstreamNode->getInput("in"), TEXT("A"), NodeMultiply);
	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, NodeMultiply->GetUniqueID());

	UInterchangeShaderNode* NodeOneMinus = CreateShaderNode(UpstreamNode->getName().c_str() + FString(TEXT("_OneMinus")), TEXT("OneMinus"));
	AddAttributeFromValueOrInterface(UpstreamNode->getInput("mask"), TEXT("Input"), NodeOneMinus);
	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(NodeMultiply, TEXT("B"), NodeOneMinus->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectPositionInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	// MaterialX defines the space as: object, model, world
	// model: The local coordinate space of the geometry, before any local deformations or global transforms have been applied.
	// object: The local coordinate space of the geometry, after local deformations have been applied, but before any global transforms.
	// world : The global coordinate space of the geometry, after local deformationsand global transforms have been applied.

	// For the moment we don't have the distinction between model/object, so let's just create an UMaterialExpressionWorldPosition
	// In case of model/object we need to add a TransformPoint from world to local space
	UInterchangeShaderNode* PositionNode = CreateShaderNode(UpstreamNode->getName().c_str() + FString(TEXT("_Position")), TEXT("WorldPosition"));

	// In case of the position node, it seems that the unit is different, we assume for now a conversion from mm -> m, even if UE by default is cm
	// See standard_surface_marble_solid file, especially on the fractal3d node
	UInterchangeShaderNode* UnitNode = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("Multiply"));
	UnitNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(TEXT("B")), 0.001f);
	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(UnitNode, TEXT("A"), PositionNode->GetUniqueID());

	UInterchangeShaderNode* NodeToConnectTo = ParentShaderNode;
	FString InputToConnectTo = InputChannelName;

	std::string Space = "world";
	mx::InputPtr InputSpace = UpstreamNode->getInput("space");

	if(InputSpace)
	{
		Space = InputSpace->getValueString();
	}

	//the default space defined by the nodedef is "object"
	if(Space != "world" || !InputSpace)
	{
		using namespace UE::Interchange::Materials::Standard::Nodes;

		UInterchangeShaderNode* TransformNode = CreateShaderNode(UpstreamNode->getName().c_str() + FString(TEXT("_Transform")), TransformPosition::Name.ToString());
		NodeToConnectTo = TransformNode;
		InputToConnectTo = TransformPosition::Inputs::Input.ToString();
		TransformNode->AddInt32Attribute(TransformPosition::Attributes::TransformSourceType, EMaterialPositionTransformSource::TRANSFORMPOSSOURCE_World);
		TransformNode->AddInt32Attribute(TransformPosition::Attributes::TransformType, EMaterialPositionTransformSource::TRANSFORMPOSSOURCE_Local);
		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, TransformNode->GetUniqueID());
	}

	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(NodeToConnectTo, InputToConnectTo, UnitNode->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectNormalInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	// MaterialX defines the space as: object, model, world
	// model: The local coordinate space of the geometry, before any local deformations or global transforms have been applied.
	// object: The local coordinate space of the geometry, after local deformations have been applied, but before any global transforms.
	// world : The global coordinate space of the geometry, after local deformationsand global transforms have been applied.

	// For the moment we don't have the distinction between model/object, so let's just create an UMaterialExpressionVertexNormalWS
	// In case of model/object we need to add a TransformVector from world to local space
	UInterchangeShaderNode* NormalNode = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("VertexNormalWS"));
	UInterchangeShaderNode* NodeToConnectTo = ParentShaderNode;
	FString InputToConnectTo = InputChannelName;

	std::string Space = "world";
	mx::InputPtr InputSpace = UpstreamNode->getInput("space");

	if(InputSpace)
	{
		Space = InputSpace->getValueString();
	}

	//the default space defined by the nodedef is "object"
	if(Space != "world" || !InputSpace)
	{
		using namespace UE::Interchange::Materials::Standard::Nodes;

		UInterchangeShaderNode* TransformNode = CreateShaderNode(UpstreamNode->getName().c_str() + FString(TEXT("_Transform")), TransformVector::Name.ToString());
		NodeToConnectTo = TransformNode;
		InputToConnectTo = TransformVector::Inputs::Input.ToString();
		TransformNode->AddInt32Attribute(TransformVector::Attributes::TransformSourceType, EMaterialVectorCoordTransformSource::TRANSFORMSOURCE_World);
		TransformNode->AddInt32Attribute(TransformVector::Attributes::TransformType, EMaterialVectorCoordTransform::TRANSFORM_Local);
		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, TransformNode->GetUniqueID());
	}

	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(NodeToConnectTo, InputToConnectTo, NormalNode->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectTangentInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	// MaterialX defines the space as: object, model, world
	// model: The local coordinate space of the geometry, before any local deformations or global transforms have been applied.
	// object: The local coordinate space of the geometry, after local deformations have been applied, but before any global transforms.
	// world : The global coordinate space of the geometry, after local deformationsand global transforms have been applied.

	// For the moment we don't have the distinction between model/object, so let's just create an UMaterialExpressionVertexTangentWS
	// In case of model/object we need to add a TransformVector from world to local space
	UInterchangeShaderNode* TangentNode = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("VertexTangentWS"));
	UInterchangeShaderNode* NodeToConnectTo = ParentShaderNode;
	FString InputToConnectTo = InputChannelName;

	std::string Space = "world";
	mx::InputPtr InputSpace = UpstreamNode->getInput("space");

	if(InputSpace)
	{
		Space = InputSpace->getValueString();
	}

	//the default space defined by the nodedef is "object"
	if(Space != "world" || !InputSpace)
	{
		using namespace UE::Interchange::Materials::Standard::Nodes;

		UInterchangeShaderNode* TransformNode = CreateShaderNode(UpstreamNode->getName().c_str() + FString(TEXT("_Transform")), TransformVector::Name.ToString());
		NodeToConnectTo = TransformNode;
		InputToConnectTo = TransformVector::Inputs::Input.ToString();
		TransformNode->AddInt32Attribute(TransformVector::Attributes::TransformSourceType, EMaterialVectorCoordTransformSource::TRANSFORMSOURCE_World);
		TransformNode->AddInt32Attribute(TransformVector::Attributes::TransformType, EMaterialVectorCoordTransform::TRANSFORM_Local);
		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, TransformNode->GetUniqueID());
	}

	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(NodeToConnectTo, InputToConnectTo, TangentNode->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectBitangentInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	// MaterialX defines the space as: object, model, world
	// model: The local coordinate space of the geometry, before any local deformations or global transforms have been applied.
	// object: The local coordinate space of the geometry, after local deformations have been applied, but before any global transforms.
	// world : The global coordinate space of the geometry, after local deformationsand global transforms have been applied.

	// For the moment we don't have the distinction between model/object, so let's just do the cross product between the normal and the tangent
	// In case of model/object we need to add a TransformVector from world to local space
	UInterchangeShaderNode* NormalNode = CreateShaderNode(UpstreamNode->getName().c_str() + FString(TEXT("_Normal")), TEXT("VertexNormalWS"));
	UInterchangeShaderNode* TangentNode = CreateShaderNode(UpstreamNode->getName().c_str() + FString(TEXT("_Tangent")), TEXT("VertexTangentWS"));
	UInterchangeShaderNode* BitangentNode = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("CrossProduct"));

	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(BitangentNode, TEXT("A"), NormalNode->GetUniqueID());
	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(BitangentNode, TEXT("B"), TangentNode->GetUniqueID());


	UInterchangeShaderNode* NodeToConnectTo = ParentShaderNode;
	FString InputToConnectTo = InputChannelName;

	std::string Space = "world";
	mx::InputPtr InputSpace = UpstreamNode->getInput("space");

	if(InputSpace)
	{
		Space = InputSpace->getValueString();
	}

	//the default space defined by the nodedef is "object"
	if(Space != "world" || !InputSpace)
	{
		using namespace UE::Interchange::Materials::Standard::Nodes;

		UInterchangeShaderNode* TransformNode = CreateShaderNode(UpstreamNode->getName().c_str() + FString(TEXT("_Transform")), TransformVector::Name.ToString());
		NodeToConnectTo = TransformNode;
		InputToConnectTo = TransformVector::Inputs::Input.ToString();
		TransformNode->AddInt32Attribute(TransformVector::Attributes::TransformSourceType, EMaterialVectorCoordTransformSource::TRANSFORMSOURCE_World);
		TransformNode->AddInt32Attribute(TransformVector::Attributes::TransformType, EMaterialVectorCoordTransform::TRANSFORM_Local);
		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, TransformNode->GetUniqueID());
	}

	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(NodeToConnectTo, InputToConnectTo, BitangentNode->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectTimeInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;
	UInterchangeShaderNode* TimeNode = CreateShaderNode(UpstreamNode->getName().c_str(), TEXT("Time"));
	TimeNode->AddBooleanAttribute(Time::Attributes::OverridePeriod, true);

	float FPS;
	mx::InputPtr Input = UpstreamNode->getInput("fps");

	//Take the default value from the node definition
	if(!Input)
	{
		Input = UpstreamNode->getNodeDef()->getInput("fps");
	}

	FPS = mx::fromValueString<float>(Input->getValueString());

	//UE is a period
	TimeNode->AddFloatAttribute(Time::Attributes::Period, 1.f / FPS);

	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, TimeNode->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectNoise3DInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	// MaterialX defines the Noise3d as Perlin Noise which is multiplied by the Amplitude then Added to Pivot
	UInterchangeShaderNode* NoiseNode = CreateShaderNode(UpstreamNode->getName().c_str(), Noise::Name.ToString());
	NoiseNode->AddInt32Attribute(Noise::Attributes::Function, ENoiseFunction::NOISEFUNCTION_GradientTex);
	NoiseNode->AddBooleanAttribute(Noise::Attributes::Turbulence, false);
	NoiseNode->AddFloatAttribute(Noise::Attributes::OutputMin, 0);

	UInterchangeShaderNode* NodeToConnect = NoiseNode;

	// Multiply Node
	auto ConnectNodeToInput = [&](mx::InputPtr Input, UInterchangeShaderNode* NodeToConnectTo, const FString& ShaderType, int32 IndexAttrib) -> UInterchangeShaderNode*
	{
		if(!Input)
		{
			return nullptr;
		}

		const FString ShaderNodeName = UpstreamNode->getName().c_str() + FString{ TEXT("_") } + ShaderType;
		UInterchangeShaderNode* ShaderNode = CreateShaderNode(ShaderNodeName, ShaderType);

		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ShaderNode, TEXT("A"), NodeToConnectTo->GetUniqueID());

		// Connect the amplitude node to the shader node not the noise
		// it will be handle during the upstream-downstream connection phase
		// The index is here for the unicity of the attribute
		UpstreamNode->setAttribute(mx::Attributes::ParentName + std::to_string(IndexAttrib), TCHAR_TO_ANSI(*ShaderNodeName));
		AddAttributeFromValueOrInterface(Input, TEXT("B"), ShaderNode);

		return ShaderNode;
	};

	if(UInterchangeShaderNode* MultiplyNode = ConnectNodeToInput(UpstreamNode->getInput("amplitude"), NoiseNode, TEXT("Multiply"), 0))
	{
		NodeToConnect = MultiplyNode;
	}


	if(UInterchangeShaderNode* AddNode = ConnectNodeToInput(UpstreamNode->getInput("pivot"), NodeToConnect, TEXT("Add"), 1))
	{
		NodeToConnect = AddNode;
	}

	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, NodeToConnect->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectCellNoise3DInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	// Let's use a vector noise for this one, the only one that is close to MaterialX implementation
	UInterchangeShaderNode* NoiseNode = CreateShaderNode(UpstreamNode->getName().c_str(), VectorNoise::Name.ToString());
	NoiseNode->AddInt32Attribute(VectorNoise::Attributes::Function, EVectorNoiseFunction::VNF_CellnoiseALU);

	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, NoiseNode->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectWorleyNoise3DInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	//Also called Voronoi, the implementation is a bit different in UE, especially we don't have access to the jitter
	UInterchangeShaderNode* NoiseNode = CreateShaderNode(UpstreamNode->getName().c_str(), Noise::Name.ToString());
	NoiseNode->AddInt32Attribute(Noise::Attributes::Function, ENoiseFunction::NOISEFUNCTION_VoronoiALU);

	UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, NoiseNode->GetUniqueID());
}

void FMaterialXSurfaceShaderAbstract::ConnectHeightToNormalInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	if(mx::InputPtr Input = UpstreamNode->getInput("in"))
	{
		// Image node will become this node
		if(mx::NodePtr ConnectedNode = Input->getConnectedNode();
		   ConnectedNode && ConnectedNode->getCategory() == mx::Category::Image)
		{
			//we need to copy the content of the image node to this node
			UpstreamNode->copyContentFrom(ConnectedNode);

			SetMatchingInputsNames(UpstreamNode);

			//the copy overwrite every attribute of the node, so we need to get them back, essentially the type and the renaming
			// the output is always a vec3
			UpstreamNode->setType(mx::Type::Vector3);

			mx::NodeGraphPtr Graph = UpstreamNode->getParent()->asA<mx::NodeGraph>();
			Graph->removeNode(ConnectedNode->getName());

			using namespace UE::Interchange::Materials::Standard::Nodes;

			if(UInterchangeTextureNode* TextureNode = CreateTextureNode<UInterchangeTexture2DNode>(UpstreamNode))
			{
				UInterchangeShaderNode* HeightMapNode = CreateShaderNode(UpstreamNode->getName().c_str(), NormalFromHeightMap::Name.ToString());
				UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, HeightMapNode->GetUniqueID());

				const FString TextureNodeName = UpstreamNode->getName().c_str() + FString{ "_texture" };
				UInterchangeShaderNode* TextureShaderNode = CreateShaderNode(TextureNodeName, TextureObject::Name.ToString());
				TextureShaderNode->AddStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(TextureObject::Inputs::Texture.ToString()), TextureNode->GetUniqueID());
				UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(HeightMapNode, NormalFromHeightMap::Inputs::HeightMap.ToString(), TextureShaderNode->GetUniqueID());

				AddAttributeFromValueOrInterface(UpstreamNode->getInput("scale"), NormalFromHeightMap::Inputs::Intensity.ToString(), HeightMapNode);
			}
			else
			{
				AddAttributeFromValueOrInterface(UpstreamNode->getInput(mx::NodeGroup::Texture2D::Inputs::Default), InputChannelName, ParentShaderNode);
			}
		}
		else
		{
			// For the moment it doesn't make sense to plug a value to it, so let's plug directly the child to the parent, in the future we could implement a Sobel and handle a multi output
			SetAttributeNewName(Input, TCHAR_TO_UTF8(*InputChannelName)); //let's take the parent node's input name
			ShaderNodes.Add(UpstreamNode->getName().c_str(), ParentShaderNode);
		}
	}
}

void FMaterialXSurfaceShaderAbstract::ConnectBlurInputToOutput(MaterialX::NodePtr UpstreamNode, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName)
{
	if(mx::InputPtr Input = UpstreamNode->getInput("in"))
	{
		// Image node will become this node
		if(mx::NodePtr ConnectedNode = Input->getConnectedNode();
		   ConnectedNode && ConnectedNode->getCategory() == mx::Category::Image)
		{
			std::string NodeType = UpstreamNode->getType();

			//we need to copy the content of the image node to this node
			UpstreamNode->copyContentFrom(ConnectedNode);

			SetMatchingInputsNames(UpstreamNode);

			//the copy overwrites every attribute of the node, so we need to get them back, essentially the type and the renaming
			UpstreamNode->setType(NodeType);

			mx::NodeGraphPtr Graph = UpstreamNode->getParent()->asA<mx::NodeGraph>();
			Graph->removeNode(ConnectedNode->getName());

			using namespace UE::Interchange::Materials::Standard::Nodes;

			if(UInterchangeTextureNode* TextureNode = CreateTextureNode<UInterchangeTextureBlurNode>(UpstreamNode))
			{
				FString OutputChannel{ TEXT("RGB") };

				if(NodeType == mx::Type::Vector4 || NodeType == mx::Type::Color4)
				{
					OutputChannel = TEXT("RGBA");
				}
				else if(NodeType == mx::Type::Float)
				{
					OutputChannel = TEXT("R");
				}

				UInterchangeShaderNode* TextureShaderNode = CreateShaderNode(UpstreamNode->getName().c_str(), TextureSampleBlur::Name.ToString());
				TextureShaderNode->AddStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(TextureSampleBlur::Inputs::Texture.ToString()), TextureNode->GetUniqueID());
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ParentShaderNode, InputChannelName, TextureShaderNode->GetUniqueID(), OutputChannel);

				if(mx::InputPtr InputKernel = UpstreamNode->getInput("filtertype"))
				{
					// By default TextureSampleBox uses gaussian filter
					if(InputKernel->getValueString() == "box")
					{
						TextureShaderNode->AddInt32Attribute(TextureSampleBlur::Attributes::Filter, 0);
					}
				}

				if(mx::InputPtr InputKernel = UpstreamNode->getInput("size"))
				{
					float KernelSize = mx::fromValueString<float>(InputKernel->getValueString());
					TextureShaderNode->AddFloatAttribute(TextureSampleBlur::Attributes::KernelSize, KernelSize);
				}
			}
			else
			{
				AddAttributeFromValueOrInterface(UpstreamNode->getInput(mx::NodeGroup::Texture2D::Inputs::Default), InputChannelName, ParentShaderNode);
			}
		}
		else
		{
			// For a blur it doesn't make sense if there's no image input
			SetAttributeNewName(Input, TCHAR_TO_UTF8(*InputChannelName)); //let's take the parent node's input name
			ShaderNodes.Add(UpstreamNode->getName().c_str(), ParentShaderNode);
		}
	}
}

UInterchangeShaderNode* FMaterialXSurfaceShaderAbstract::CreateMaskShaderNode(uint8 RGBA, const FString& NodeName)
{
	bool bR = (0b1000 & RGBA) >> 3;
	bool bG = (0b0100 & RGBA) >> 2;
	bool bB = (0b0010 & RGBA) >> 1;
	bool bA = (0b0001 & RGBA) >> 0;
	using namespace UE::Interchange::Materials::Standard::Nodes;
	UInterchangeShaderNode* MaskShaderNode = CreateShaderNode(NodeName, Mask::Name.ToString());
	MaskShaderNode->AddBooleanAttribute(Mask::Attributes::R, bR);
	MaskShaderNode->AddBooleanAttribute(Mask::Attributes::G, bG);
	MaskShaderNode->AddBooleanAttribute(Mask::Attributes::B, bB);
	MaskShaderNode->AddBooleanAttribute(Mask::Attributes::A, bA);

	return MaskShaderNode;
}

UInterchangeShaderNode* FMaterialXSurfaceShaderAbstract::CreateShaderNode(const FString& NodeName, const FString& ShaderType)
{
	UInterchangeShaderNode* Node;

	const FString NodeUID = UInterchangeShaderNode::MakeNodeUid(NodeName, FStringView{});

	//Test directly in the NodeContainer, because the ShaderNodes can be altered during the node graph either by the parent (dot/normalmap),
	//or by putting an intermediary node between the child and the parent (tiledimage)
	if(Node = const_cast<UInterchangeShaderNode*>(Cast<UInterchangeShaderNode>(NodeContainer.GetNode(NodeUID))); !Node)
	{
		Node = NewObject<UInterchangeShaderNode>(&NodeContainer);
		Node->InitializeNode(NodeUID, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
		NodeContainer.AddNode(Node);
		Node->SetCustomShaderType(ShaderType);

		ShaderNodes.Add(NodeName, Node);
	}

	return Node;
}

const FString& FMaterialXSurfaceShaderAbstract::GetMatchedInputName(MaterialX::NodePtr Node, MaterialX::InputPtr Input) const
{
	static FString EmptyString;

	FMaterialXManager& Manager = FMaterialXManager::GetInstance();

	if(Input)
	{
		const FString NodeCategory{ Node->getCategory().c_str() };
		const FString InputName{ GetInputName(Input) };
		
		if(const FString* Result = Manager.FindMatchingInput({ NodeCategory, InputName }))
		{
			return *Result;
		}
		else if((Result = Manager.FindMatchingInput({ EmptyString, InputName })))
		{
			return *Result;
		}
	}

	return EmptyString;
}

FString FMaterialXSurfaceShaderAbstract::GetInputName(MaterialX::InputPtr Input) const
{
	if(Input->hasAttribute(mx::Attributes::NewName))
	{
		return Input->getAttribute(mx::Attributes::NewName).c_str();
	}
	else
	{
		return Input->getName().c_str();
	}
}

FString FMaterialXSurfaceShaderAbstract::GetFilePrefix(MaterialX::ElementPtr Element) const
{
	FString FilePrefix;

	if(Element)
	{
		if(Element->hasFilePrefix())
		{
			return FString(Element->getFilePrefix().c_str());
		}
		else
		{
			return GetFilePrefix(Element->getParent());
		}
	}

	return FilePrefix;
}

FLinearColor FMaterialXSurfaceShaderAbstract::GetVector(MaterialX::InputPtr Input) const
{
	FLinearColor LinearColor;

	if(Input->getType() == mx::Type::Vector2)
	{
		mx::Vector2 Color = mx::fromValueString<mx::Vector2>(Input->getValueString());
		LinearColor = FLinearColor{ Color[0], Color[1], 0 };
	}
	else if(Input->getType() == mx::Type::Vector3)
	{
		mx::Vector3 Color = mx::fromValueString<mx::Vector3>(Input->getValueString());
		LinearColor = FLinearColor{ Color[0], Color[1], Color[2] };
	}
	else if(Input->getType() == mx::Type::Vector4)
	{
		mx::Vector4 Color = mx::fromValueString<mx::Vector4>(Input->getValueString());
		LinearColor = FLinearColor{ Color[0], Color[1], Color[2], Color[3] };
	}
	else
	{
		ensureMsgf(false, TEXT("input type can only be a vectorN"));
	}

	return LinearColor;
}

FString FMaterialXSurfaceShaderAbstract::GetAttributeParentName(MaterialX::NodePtr Node) const
{
	FString ParentName;
	const mx::StringVec& Attributes = Node->getAttributeNames();

	// For consistency the parent attribute has an index attach to it to ensure unicity
	// Attributes are set in order, we only need to take the first one (it will be remove after that)
	for(const std::string& Attrib : Attributes)
	{
		if(Attrib.find(mx::Attributes::ParentName) != std::string::npos)
		{
			ParentName = Node->getAttribute(Attrib).c_str();
			Node->removeAttribute(Attrib);
			break;
		}
	}

	return ParentName.IsEmpty() ? Node->getName().c_str() : ParentName;
}

void FMaterialXSurfaceShaderAbstract::RegisterConnectNodeOutputToInputDelegates()
{
	MatchingConnectNodeDelegates.Add(mx::Category::Constant, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectConstantInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Extract, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectExtractInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Dot, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectDotInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::NormalMap, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectDotInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::TransformPoint, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectTransformPositionInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::TransformVector, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectTransformVectorInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::TransformNormal, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectTransformVectorInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Rotate3D, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectRotate3DInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Image, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectImageInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Convert, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectConvertInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::IfGreater, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectIfGreaterInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::IfGreaterEq, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectIfGreaterEqInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::IfEqual, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectIfEqualInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Outside, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectOutsideInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Position, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectPositionInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Normal, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectNormalInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Tangent, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectTangentInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Bitangent, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectBitangentInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Time, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectTimeInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Noise3D, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectNoise3DInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::CellNoise3D, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectCellNoise3DInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::WorleyNoise3D, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectWorleyNoise3DInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::Blur, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectBlurInputToOutput));
	MatchingConnectNodeDelegates.Add(mx::Category::HeightToNormal, FOnConnectNodeOutputToInput::CreateSP(this, &FMaterialXSurfaceShaderAbstract::ConnectHeightToNormalInputToOutput));
}

void FMaterialXSurfaceShaderAbstract::SetMatchingInputsNames(MaterialX::NodePtr Node) const
{
	if(Node)
	{
		if(const std::string& IsVisited = Node->getAttribute(mx::Attributes::IsVisited); IsVisited.empty())
		{
			Node->setAttribute(mx::Attributes::IsVisited, "true");

			for(mx::InputPtr Input : Node->getInputs())
			{
				if(const FString& Name = GetMatchedInputName(Node, Input); !Name.IsEmpty())
				{
					SetAttributeNewName(Input, TCHAR_TO_UTF8(*Name));
				}
			}
		}
	}
}

void FMaterialXSurfaceShaderAbstract::SetAttributeNewName(MaterialX::InputPtr Input, const char* NewName) const
{
	Input->setAttribute(mx::Attributes::NewName, NewName);
}

#undef LOCTEXT_NAMESPACE
#endif