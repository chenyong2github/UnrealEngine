// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "MaterialXSurfaceUnlitShader.h"

namespace mx = MaterialX;

FMaterialXSurfaceUnlitShader::FMaterialXSurfaceUnlitShader(UInterchangeBaseNodeContainer& BaseNodeContainer)
	: FMaterialXSurfaceShaderAbstract(BaseNodeContainer)
{
	NodeDefinition = mx::NodeDefinition::SurfaceUnlit;
}

TSharedRef<FMaterialXBase> FMaterialXSurfaceUnlitShader::MakeInstance(UInterchangeBaseNodeContainer& BaseNodeContainer)
{
	TSharedRef<FMaterialXSurfaceUnlitShader> Result = MakeShared<FMaterialXSurfaceUnlitShader>(BaseNodeContainer);
	Result->RegisterConnectNodeOutputToInputDelegates();
	return Result;
}

void FMaterialXSurfaceUnlitShader::Translate(mx::NodePtr SurfaceUnlitNode)
{
	this->SurfaceShaderNode = SurfaceUnlitNode;

	using namespace UE::Interchange::Materials;

	UInterchangeShaderGraphNode* ShaderGraphNode = CreateShaderNode<UInterchangeShaderGraphNode>(SurfaceShaderNode->getName().c_str(), SurfaceUnlit::Name.ToString());

	//Emission
	ConnectNodeOutputToInput(mx::SurfaceUnlit::Input::Emission, ShaderGraphNode, SurfaceUnlit::Parameters::Emission.ToString(), mx::SurfaceUnlit::DefaultValue::Float::Emission);

	//Emission Color
	ConnectNodeOutputToInput(mx::SurfaceUnlit::Input::EmissionColor, ShaderGraphNode, SurfaceUnlit::Parameters::EmissionColor.ToString(), mx::SurfaceUnlit::DefaultValue::Color3::EmissionColor);

	//Opacity
	ConnectNodeOutputToInput(mx::SurfaceUnlit::Input::Opacity, ShaderGraphNode, SurfaceUnlit::Parameters::Opacity.ToString(), mx::SurfaceUnlit::DefaultValue::Float::Opacity);

	//Transmission
	ConnectNodeOutputToInput(mx::SurfaceUnlit::Input::Transmission, ShaderGraphNode, SurfaceUnlit::Parameters::Transmission.ToString(), mx::SurfaceUnlit::DefaultValue::Float::Transmission);

	//Transmission Color
	ConnectNodeOutputToInput(mx::SurfaceUnlit::Input::TransmissionColor, ShaderGraphNode, SurfaceUnlit::Parameters::TransmissionColor.ToString(), mx::SurfaceUnlit::DefaultValue::Color3::TransmissionColor);
}
#endif