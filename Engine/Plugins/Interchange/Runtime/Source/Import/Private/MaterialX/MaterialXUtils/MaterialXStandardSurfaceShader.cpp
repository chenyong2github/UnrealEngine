// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "MaterialXStandardSurfaceShader.h"

namespace mx = MaterialX;

FMaterialXStandardSurfaceShader::FMaterialXStandardSurfaceShader(UInterchangeBaseNodeContainer& BaseNodeContainer)
	: FMaterialXSurfaceShaderAbstract(BaseNodeContainer)
{
	NodeDefinition = mx::NodeDefinition::StandardSurface;
}

TSharedRef<FMaterialXBase> FMaterialXStandardSurfaceShader::MakeInstance(UInterchangeBaseNodeContainer& BaseNodeContainer)
{
	TSharedRef<FMaterialXStandardSurfaceShader> Result= MakeShared<FMaterialXStandardSurfaceShader>(BaseNodeContainer);
	Result->RegisterConnectNodeOutputToInputDelegates();
	return Result;
}

void FMaterialXStandardSurfaceShader::Translate(mx::NodePtr StandardSurfaceNode)
{
	this->SurfaceShaderNode = StandardSurfaceNode;

	using namespace UE::Interchange::Materials;

	UInterchangeShaderGraphNode* ShaderGraphNode = CreateShaderNode<UInterchangeShaderGraphNode>(SurfaceShaderNode->getName().c_str(), StandardSurface::Name.ToString());

	//Base
	ConnectNodeOutputToInput(mx::StandardSurface::Input::Base, ShaderGraphNode, StandardSurface::Parameters::Base.ToString(), mx::StandardSurface::DefaultValue::Float::Base);

	//Base Color
	ConnectNodeOutputToInput(mx::StandardSurface::Input::BaseColor, ShaderGraphNode, StandardSurface::Parameters::BaseColor.ToString(), mx::StandardSurface::DefaultValue::Color3::BaseColor);

	//Diffuse Roughness
	ConnectNodeOutputToInput(mx::StandardSurface::Input::DiffuseRoughness, ShaderGraphNode, StandardSurface::Parameters::DiffuseRoughness.ToString(), mx::StandardSurface::DefaultValue::Float::DiffuseRoughness);

	//Specular
	ConnectNodeOutputToInput(mx::StandardSurface::Input::Specular, ShaderGraphNode, StandardSurface::Parameters::Specular.ToString(), mx::StandardSurface::DefaultValue::Float::Specular);

	//Specular Roughness
	ConnectNodeOutputToInput(mx::StandardSurface::Input::SpecularRoughness, ShaderGraphNode, StandardSurface::Parameters::SpecularRoughness.ToString(), mx::StandardSurface::DefaultValue::Float::SpecularRoughness);

	//Specular IOR
	ConnectNodeOutputToInput(mx::StandardSurface::Input::SpecularIOR, ShaderGraphNode, StandardSurface::Parameters::SpecularIOR.ToString(), mx::StandardSurface::DefaultValue::Float::SpecularIOR);

	//Specular Anisotropy
	ConnectNodeOutputToInput(mx::StandardSurface::Input::SpecularAnisotropy, ShaderGraphNode, StandardSurface::Parameters::SpecularAnisotropy.ToString(), mx::StandardSurface::DefaultValue::Float::SpecularAnisotropy);

	//Specular Rotation
	ConnectNodeOutputToInput(mx::StandardSurface::Input::SpecularRotation, ShaderGraphNode, StandardSurface::Parameters::SpecularRotation.ToString(), mx::StandardSurface::DefaultValue::Float::SpecularRotation);

	//Metallic
	ConnectNodeOutputToInput(mx::StandardSurface::Input::Metalness, ShaderGraphNode, StandardSurface::Parameters::Metalness.ToString(), mx::StandardSurface::DefaultValue::Float::Metalness);

	//Subsurface
	ConnectNodeOutputToInput(mx::StandardSurface::Input::Subsurface, ShaderGraphNode, StandardSurface::Parameters::Subsurface.ToString(), mx::StandardSurface::DefaultValue::Float::Subsurface);

	//Subsurface Color
	ConnectNodeOutputToInput(mx::StandardSurface::Input::SubsurfaceColor, ShaderGraphNode, StandardSurface::Parameters::SubsurfaceColor.ToString(), mx::StandardSurface::DefaultValue::Color3::SubsurfaceColor);

	//Subsurface Radius
	ConnectNodeOutputToInput(mx::StandardSurface::Input::SubsurfaceRadius, ShaderGraphNode, StandardSurface::Parameters::SubsurfaceRadius.ToString(), mx::StandardSurface::DefaultValue::Color3::SubsurfaceRadius);

	//Subsurface Scale
	ConnectNodeOutputToInput(mx::StandardSurface::Input::SubsurfaceScale, ShaderGraphNode, StandardSurface::Parameters::SubsurfaceScale.ToString(), mx::StandardSurface::DefaultValue::Float::SubsurfaceScale);

	//Sheen
	ConnectNodeOutputToInput(mx::StandardSurface::Input::Sheen, ShaderGraphNode, StandardSurface::Parameters::Sheen.ToString(), mx::StandardSurface::DefaultValue::Float::Sheen);

	//Sheen Color
	ConnectNodeOutputToInput(mx::StandardSurface::Input::SheenColor, ShaderGraphNode, StandardSurface::Parameters::SheenColor.ToString(), mx::StandardSurface::DefaultValue::Color3::SheenColor);

	//Sheen Roughness
	ConnectNodeOutputToInput(mx::StandardSurface::Input::SheenRoughness, ShaderGraphNode, StandardSurface::Parameters::SheenRoughness.ToString(), mx::StandardSurface::DefaultValue::Float::SheenRoughness);

	//Coat
	ConnectNodeOutputToInput(mx::StandardSurface::Input::Coat, ShaderGraphNode, StandardSurface::Parameters::Coat.ToString(), mx::StandardSurface::DefaultValue::Float::Coat);

	//Coat Color
	ConnectNodeOutputToInput(mx::StandardSurface::Input::CoatColor, ShaderGraphNode, StandardSurface::Parameters::CoatColor.ToString(), mx::StandardSurface::DefaultValue::Color3::CoatColor);

	//Coat Roughness
	ConnectNodeOutputToInput(mx::StandardSurface::Input::CoatRoughness, ShaderGraphNode, StandardSurface::Parameters::CoatRoughness.ToString(), mx::StandardSurface::DefaultValue::Float::CoatRoughness);

	//Coat Normal: No need to take the default input if there is no CoatNormal input
	ConnectNodeOutputToInput(mx::StandardSurface::Input::CoatNormal, ShaderGraphNode, StandardSurface::Parameters::CoatNormal.ToString(), nullptr);

	//Thin Film Thickness
	ConnectNodeOutputToInput(mx::StandardSurface::Input::ThinFilmThickness, ShaderGraphNode, StandardSurface::Parameters::ThinFilmThickness.ToString(), mx::StandardSurface::DefaultValue::Float::ThinFilmThickness);

	//Emission
	ConnectNodeOutputToInput(mx::StandardSurface::Input::Emission, ShaderGraphNode, StandardSurface::Parameters::Emission.ToString(), mx::StandardSurface::DefaultValue::Float::Emission);

	//Emission Color
	ConnectNodeOutputToInput(mx::StandardSurface::Input::EmissionColor, ShaderGraphNode, StandardSurface::Parameters::EmissionColor.ToString(), mx::StandardSurface::DefaultValue::Color3::EmissionColor);

	//Normal: No need to take the default input if there is no Normal input
	ConnectNodeOutputToInput(mx::StandardSurface::Input::Normal, ShaderGraphNode, StandardSurface::Parameters::Normal.ToString(), nullptr);

	//Tangent: No need to take the default input if there is no Tangent input
	ConnectNodeOutputToInput(mx::StandardSurface::Input::Tangent, ShaderGraphNode, StandardSurface::Parameters::Tangent.ToString(), nullptr);

	//Transmission
	ConnectNodeOutputToInput(mx::StandardSurface::Input::Transmission, ShaderGraphNode, StandardSurface::Parameters::Transmission.ToString(), mx::StandardSurface::DefaultValue::Float::Transmission);

	//Transmission Color
	ConnectNodeOutputToInput(mx::StandardSurface::Input::TransmissionColor, ShaderGraphNode, StandardSurface::Parameters::TransmissionColor.ToString(), mx::StandardSurface::DefaultValue::Color3::TransmissionColor);

	//Transmission Depth
	ConnectNodeOutputToInput(mx::StandardSurface::Input::TransmissionDepth, ShaderGraphNode, StandardSurface::Parameters::TransmissionDepth.ToString(), mx::StandardSurface::DefaultValue::Float::TransmissionDepth);

	//Transmission Scatter
	ConnectNodeOutputToInput(mx::StandardSurface::Input::TransmissionScatter, ShaderGraphNode, StandardSurface::Parameters::TransmissionScatter.ToString(), mx::StandardSurface::DefaultValue::Color3::TransmissionScatter);

	//Transmission Scatter Anisotropy
	ConnectNodeOutputToInput(mx::StandardSurface::Input::TransmissionScatterAnisotropy, ShaderGraphNode, StandardSurface::Parameters::TransmissionScatterAnisotropy.ToString(), mx::StandardSurface::DefaultValue::Float::TransmissionScatterAnisotropy);

	//Transmission Dispersion
	ConnectNodeOutputToInput(mx::StandardSurface::Input::TransmissionDispersion, ShaderGraphNode, StandardSurface::Parameters::TransmissionDispersion.ToString(), mx::StandardSurface::DefaultValue::Float::TransmissionDispersion);

	//Transmission Extra Roughness
	ConnectNodeOutputToInput(mx::StandardSurface::Input::TransmissionExtraRoughness, ShaderGraphNode, StandardSurface::Parameters::TransmissionExtraRoughness.ToString(), mx::StandardSurface::DefaultValue::Float::TransmissionExtraRoughness);
}
#endif