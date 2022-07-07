// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Gltf/InterchangeGltfTranslator.h"

#include "GLTFAsset.h"
#include "GLTFMeshFactory.h"
#include "GLTFReader.h"

#include "InterchangeCameraNode.h"
#include "InterchangeLightNode.h"
#include "InterchangeManager.h"
#include "InterchangeMaterialDefinitions.h"
#include "InterchangeMeshNode.h"
#include "InterchangeSceneNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeTexture2DNode.h"

#include "Texture/InterchangeImageWrapperTranslator.h"

#include "Algo/Find.h"
#include "StaticMeshAttributes.h"
#include "UObject/GCObjectScopeGuard.h"

namespace UE::Interchange::Gltf::Private
{
	FString GenerateNameForGltfNode( const FString& NodeName, int32 NodeIndex )
	{
		if ( NodeIndex == 0 )
		{
			return NodeName;
		}
		else
		{
			return NodeName + TEXT("_") + LexToString( NodeIndex );
		}
	}

	FString GenerateTextureName( const GLTF::FTexture& Texture, int32 NodeIndex )
	{
		if ( !Texture.Name.IsEmpty() )
		{
			return GenerateNameForGltfNode( Texture.Name, NodeIndex );
		}
		else
		{
			// If texture has no URI, we consider it an embedded texture (coming from a .glb file)
			const FString NodeName = Texture.Source.URI.IsEmpty() ? "EmbeddedTexture" : FPaths::GetBaseFilename(Texture.Source.URI);
			return GenerateNameForGltfNode(NodeName, NodeIndex);
		}
	}

	EInterchangeTextureWrapMode ConvertWrap(GLTF::FSampler::EWrap Wrap)
	{
		switch (Wrap)
		{
		case GLTF::FSampler::EWrap::Repeat:
			return EInterchangeTextureWrapMode::Wrap;
		case GLTF::FSampler::EWrap::MirroredRepeat:
			return EInterchangeTextureWrapMode::Mirror;
		case GLTF::FSampler::EWrap::ClampToEdge:
			return EInterchangeTextureWrapMode::Clamp;

		default:
			return EInterchangeTextureWrapMode::Wrap;
		}
	}
}

void UInterchangeGltfTranslator::HandleGltfNode( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FNode& GltfNode, const FString& ParentNodeUid, const int32 NodeIndex ) const
{
	using namespace UE::Interchange::Gltf::Private;

	const FString NodeName = GenerateNameForGltfNode( GltfNode.Name, NodeIndex );
	const FString NodeUid = ParentNodeUid + TEXT("\\") + NodeName;

	const UInterchangeSceneNode* ParentSceneNode = Cast< UInterchangeSceneNode >( NodeContainer.GetNode( ParentNodeUid ) );

	UInterchangeSceneNode* InterchangeSceneNode = NewObject< UInterchangeSceneNode >( &NodeContainer );
	InterchangeSceneNode->InitializeNode( NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedScene );
	NodeContainer.AddNode( InterchangeSceneNode );

	FTransform Transform = GltfNode.Transform;

	constexpr float MetersToCentimeters = 100.f;
	Transform.SetTranslation( Transform.GetTranslation() * MetersToCentimeters );

	switch ( GltfNode.Type )
	{
		case GLTF::FNode::EType::Mesh:
		{
			if ( GltfAsset.Meshes.IsValidIndex( GltfNode.MeshIndex ) )
			{
				const FString MeshNodeUid = TEXT("\\Mesh\\") + GenerateNameForGltfNode( GltfAsset.Meshes[ GltfNode.MeshIndex ].Name, GltfNode.MeshIndex );
				InterchangeSceneNode->SetCustomAssetInstanceUid( MeshNodeUid );
			}
			break;
		}

		case GLTF::FNode::EType::Camera:
		{
			Transform.ConcatenateRotation(FRotator(0, -90, 0).Quaternion());

			if ( GltfAsset.Cameras.IsValidIndex( GltfNode.CameraIndex ) )
			{
				const FString CameraNodeUid = TEXT("\\Camera\\") + GenerateNameForGltfNode( GltfAsset.Cameras[ GltfNode.CameraIndex ].Name, GltfNode.CameraIndex );
				InterchangeSceneNode->SetCustomAssetInstanceUid( CameraNodeUid );
			}
			break;
		}

		case GLTF::FNode::EType::Light:
		{
			Transform.ConcatenateRotation(FRotator(0, -90, 0).Quaternion());

			if ( GltfAsset.Lights.IsValidIndex( GltfNode.LightIndex ) )
			{
				const FString LightNodeUid = TEXT("\\Light\\") + GenerateNameForGltfNode( GltfAsset.Lights[ GltfNode.LightIndex ].Name, GltfNode.LightIndex );
				InterchangeSceneNode->SetCustomAssetInstanceUid( LightNodeUid );
			}
		}

		case GLTF::FNode::EType::Transform:
		default:
		{
			break;
		}
	}

	constexpr bool bResetCache = false;

	InterchangeSceneNode->SetCustomLocalTransform(&NodeContainer, Transform, bResetCache);

	if ( !ParentNodeUid.IsEmpty() )
	{
		NodeContainer.SetNodeParentUid( NodeUid, ParentNodeUid );
	}

	for ( const int32 ChildIndex : GltfNode.Children )
	{
		if ( GltfAsset.Nodes.IsValidIndex( ChildIndex ) )
		{
			HandleGltfNode( NodeContainer, GltfAsset.Nodes[ ChildIndex ], NodeUid, ChildIndex );
		}
	}
}

void UInterchangeGltfTranslator::HandleGltfMaterialParameter( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FTextureMap& TextureMap, UInterchangeShaderNode& ShaderNode,
		const FString& MapName, const TVariant< FLinearColor, float >& MapFactor, const FString& OutputChannel, const bool bInverse, const bool bIsNormal ) const
{
	using namespace UE::Interchange::Materials;
	using namespace UE::Interchange::Gltf::Private;

	UInterchangeShaderNode* NodeToConnectTo = &ShaderNode;
	FString InputToConnectTo = MapName;

	if (bInverse)
	{
		const FString OneMinusNodeName = MapName + TEXT("OneMinus");
		UInterchangeShaderNode* OneMinusNode = UInterchangeShaderNode::Create( &NodeContainer, OneMinusNodeName, ShaderNode.GetUniqueID() );

		OneMinusNode->SetCustomShaderType(Standard::Nodes::OneMinus::Name.ToString());

		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(NodeToConnectTo, InputToConnectTo, OneMinusNode->GetUniqueID());

		NodeToConnectTo = OneMinusNode;
		InputToConnectTo = Standard::Nodes::OneMinus::Inputs::Input.ToString();
	}

	bool bTextureHasImportance = true;

	if ( MapFactor.IsType< float >() )
	{
		bTextureHasImportance = !FMath::IsNearlyZero( MapFactor.Get< float >() );
	}
	else if ( MapFactor.IsType< FLinearColor >() )
	{
		bTextureHasImportance = !MapFactor.Get< FLinearColor >().IsAlmostBlack();
	}

	if ( bTextureHasImportance && GltfAsset.Textures.IsValidIndex( TextureMap.TextureIndex ) )
	{
		const FString ColorNodeName = MapName;
		UInterchangeShaderNode* ColorNode = UInterchangeShaderNode::Create( &NodeContainer, ColorNodeName, ShaderNode.GetUniqueID() );
		ColorNode->SetCustomShaderType( Standard::Nodes::TextureSample::Name.ToString() );

		const FString TextureName = GenerateTextureName( GltfAsset.Textures[ TextureMap.TextureIndex ], TextureMap.TextureIndex );
		const FString TextureUid = UInterchangeTextureNode::MakeNodeUid( TextureName );

		ColorNode->AddStringAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::TextureSample::Inputs::Texture.ToString() ), TextureUid );

		if ( TextureMap.bHasTextureTransform )
		{
			HandleGltfTextureTransform( NodeContainer, TextureMap.TextureTransform, TextureMap.TexCoord, *ColorNode );
		}

		bool bNeedsFactorNode = false;

		if ( MapFactor.IsType< float >() )
		{
			bNeedsFactorNode = !FMath::IsNearlyEqual( MapFactor.Get< float >(), 1.f );
		}
		else if ( MapFactor.IsType< FLinearColor >() )
		{
			bNeedsFactorNode = !MapFactor.Get< FLinearColor >().Equals( FLinearColor::White );
		}

		if ( bNeedsFactorNode )
		{
			UInterchangeShaderNode* FactorNode = UInterchangeShaderNode::Create( &NodeContainer, ColorNodeName + TEXT("_Factor"), ShaderNode.GetUniqueID() );

			if ( bIsNormal )
			{
				FactorNode->SetCustomShaderType( Standard::Nodes::FlattenNormal::Name.ToString() );

				UInterchangeShaderNode* FactorOneMinusNode = UInterchangeShaderNode::Create( &NodeContainer, ColorNodeName + TEXT("_Factor_OneMinus"), ShaderNode.GetUniqueID() );
				FactorOneMinusNode->SetCustomShaderType(Standard::Nodes::OneMinus::Name.ToString());

				if ( MapFactor.IsType< float >() )
				{
					FactorOneMinusNode->AddFloatAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::OneMinus::Inputs::Input.ToString() ), MapFactor.Get< float >() );
				}

				UInterchangeShaderPortsAPI::ConnectOuputToInput( FactorNode, Standard::Nodes::FlattenNormal::Inputs::Normal.ToString(), ColorNode->GetUniqueID(), OutputChannel );
				UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput( FactorNode, Standard::Nodes::FlattenNormal::Inputs::Flatness.ToString(), FactorOneMinusNode->GetUniqueID() );
			}
			else
			{
				FactorNode->SetCustomShaderType( Standard::Nodes::Multiply::Name.ToString() );

				if ( MapFactor.IsType< float >() )
				{
					FactorNode->AddFloatAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::Multiply::Inputs::B.ToString() ), MapFactor.Get< float >() );
				}
				else if ( MapFactor.IsType< FLinearColor >() )
				{
					FactorNode->AddLinearColorAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::Multiply::Inputs::B.ToString() ), MapFactor.Get< FLinearColor >() );
				}

				UInterchangeShaderPortsAPI::ConnectOuputToInput( FactorNode, Standard::Nodes::Multiply::Inputs::A.ToString(), ColorNode->GetUniqueID(), OutputChannel );
			}

			UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput( NodeToConnectTo, InputToConnectTo, FactorNode->GetUniqueID() );
		}
		else
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput( NodeToConnectTo, InputToConnectTo, ColorNode->GetUniqueID(), OutputChannel );
		}
	}
	else
	{
		if ( MapFactor.IsType< FLinearColor >() )
		{
			NodeToConnectTo->AddLinearColorAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( InputToConnectTo ), MapFactor.Get< FLinearColor >() );
		}
		else if ( MapFactor.IsType< float >() )
		{
			NodeToConnectTo->AddFloatAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( InputToConnectTo ), MapFactor.Get< float >() );
		}
	}
}

void UInterchangeGltfTranslator::HandleGltfMaterial( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FMaterial& GltfMaterial, UInterchangeShaderGraphNode& ShaderGraphNode ) const
{
	using namespace UE::Interchange::Materials;

	ShaderGraphNode.SetCustomTwoSided( GltfMaterial.bIsDoubleSided );

	if ( GltfMaterial.ShadingModel == GLTF::FMaterial::EShadingModel::MetallicRoughness )
	{
		// Base Color
		{
			TVariant< FLinearColor, float > BaseColorFactor;
			BaseColorFactor.Set< FLinearColor >( FLinearColor( GltfMaterial.BaseColorFactor ) );

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.BaseColor, ShaderGraphNode, PBR::Parameters::BaseColor.ToString(),
				BaseColorFactor, Standard::Nodes::TextureSample::Outputs::RGB.ToString() );
		}

		// Metallic
		{
			TVariant< FLinearColor, float > MetallicFactor;
			MetallicFactor.Set< float >( GltfMaterial.MetallicRoughness.MetallicFactor );

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.MetallicRoughness.Map, ShaderGraphNode, PBR::Parameters::Metallic.ToString(),
				MetallicFactor, Standard::Nodes::TextureSample::Outputs::B.ToString() );
		}

		// Roughness
		{
			TVariant< FLinearColor, float > RoughnessFactor;
			RoughnessFactor.Set< float >( GltfMaterial.MetallicRoughness.RoughnessFactor );

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.MetallicRoughness.Map, ShaderGraphNode, PBR::Parameters::Roughness.ToString(),
				RoughnessFactor, Standard::Nodes::TextureSample::Outputs::G.ToString() );
		}

		// Specular
		if (GltfMaterial.bHasSpecular)
		{
			TVariant< FLinearColor, float > SpecularFactor;
			SpecularFactor.Set< float >( GltfMaterial.Specular.SpecularFactor );

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.Specular.SpecularMap, ShaderGraphNode, PBR::Parameters::Specular.ToString(),
				SpecularFactor, Standard::Nodes::TextureSample::Outputs::RGB.ToString() );
		}
	}
	else if ( GltfMaterial.ShadingModel == GLTF::FMaterial::EShadingModel::SpecularGlossiness )
	{
		// Diffuse Color
		{
			TVariant< FLinearColor, float > DiffuseColorFactor;
			DiffuseColorFactor.Set< FLinearColor >( FLinearColor( GltfMaterial.BaseColorFactor ) );

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.BaseColor, ShaderGraphNode, Phong::Parameters::DiffuseColor.ToString(),
				DiffuseColorFactor, Standard::Nodes::TextureSample::Outputs::RGB.ToString() );
		}

		// Specular Color
		{
			TVariant< FLinearColor, float > SpecularColorFactor;
			SpecularColorFactor.Set< FLinearColor >( FLinearColor( GltfMaterial.SpecularGlossiness.SpecularFactor ) );

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.SpecularGlossiness.Map, ShaderGraphNode, Phong::Parameters::SpecularColor.ToString(),
				SpecularColorFactor, Standard::Nodes::TextureSample::Outputs::RGB.ToString() );
		}

		// Glossiness
		{
			TVariant< FLinearColor, float > GlossinessFactor;
			GlossinessFactor.Set< float >( GltfMaterial.SpecularGlossiness.GlossinessFactor );

			const bool bInverse = true;
			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.SpecularGlossiness.Map, ShaderGraphNode, PBR::Parameters::Roughness.ToString(),
				GlossinessFactor, Standard::Nodes::TextureSample::Outputs::A.ToString(), bInverse );
		}
	}

	// Additional maps
	{
		// Normal
		if ( GltfMaterial.Normal.TextureIndex != INDEX_NONE )
		{
			TVariant< FLinearColor, float > NormalFactor;
			NormalFactor.Set< float >( GltfMaterial.NormalScale );

			const bool bInverse = false;
			const bool bIsNormal = true;

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.Normal, ShaderGraphNode, Common::Parameters::Normal.ToString(),
				NormalFactor, Standard::Nodes::TextureSample::Outputs::RGB.ToString(), bInverse, bIsNormal );
		}

		// Emissive
		if ( GltfMaterial.Emissive.TextureIndex != INDEX_NONE || !GltfMaterial.EmissiveFactor.IsNearlyZero() )
		{
			TVariant< FLinearColor, float > EmissiveFactor;
			EmissiveFactor.Set< FLinearColor >( FLinearColor( GltfMaterial.EmissiveFactor ) );

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.Emissive, ShaderGraphNode, Common::Parameters::EmissiveColor.ToString(),
				EmissiveFactor, Standard::Nodes::TextureSample::Outputs::RGB.ToString() );
		}

		// Occlusion
		if ( GltfMaterial.Occlusion .TextureIndex != INDEX_NONE )
		{
			TVariant< FLinearColor, float > OcclusionFactor;
			OcclusionFactor.Set< float >( GltfMaterial.OcclusionStrength );

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.Occlusion, ShaderGraphNode, PBR::Parameters::Occlusion.ToString(),
				OcclusionFactor, Standard::Nodes::TextureSample::Outputs::R.ToString() );
		}

		// Opacity (use the base color alpha channel)
		if ( GltfMaterial.AlphaMode != GLTF::FMaterial::EAlphaMode::Opaque )
		{
			TVariant< FLinearColor, float > OpacityFactor;
			OpacityFactor.Set< float >( GltfMaterial.BaseColorFactor.W );

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.BaseColor, ShaderGraphNode, PBR::Parameters::Opacity.ToString(),
				OpacityFactor, Standard::Nodes::TextureSample::Outputs::A.ToString() );
		}

		// Alpha cutoff
		if ( GltfMaterial.AlphaMode == GLTF::FMaterial::EAlphaMode::Mask )
		{
			ShaderGraphNode.SetCustomOpacityMaskClipValue( GltfMaterial.AlphaCutoff );
		}

		// IOR
		if ( GltfMaterial.bHasIOR )
		{
			ShaderGraphNode.AddFloatAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( PBR::Parameters::IndexOfRefraction.ToString() ), GltfMaterial.IOR );
		}
	}

	if ( GltfMaterial.bHasClearCoat )
	{
		HandleGltfClearCoat( NodeContainer, GltfMaterial, ShaderGraphNode );
	}

	if ( GltfMaterial.bHasSheen )
	{
		HandleGltfSheen( NodeContainer, GltfMaterial, ShaderGraphNode );
	}

	if ( GltfMaterial.bHasTransmission )
	{
		HandleGltfTransmission( NodeContainer, GltfMaterial, ShaderGraphNode );
	}
}

void UInterchangeGltfTranslator::HandleGltfClearCoat( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FMaterial& GltfMaterial, UInterchangeShaderGraphNode& ShaderGraphNode ) const
{
	using namespace UE::Interchange::Materials;

	if ( !GltfMaterial.bHasClearCoat || FMath::IsNearlyZero( GltfMaterial.ClearCoat.ClearCoatFactor ) )
	{
		return;
	}

	// ClearCoat::Parameters::ClearCoat
	{
		TVariant< FLinearColor, float > ClearCoatFactor;
		ClearCoatFactor.Set< float >( GltfMaterial.ClearCoat.ClearCoatFactor );

		HandleGltfMaterialParameter( NodeContainer, GltfMaterial.ClearCoat.ClearCoatMap, ShaderGraphNode, ClearCoat::Parameters::ClearCoat.ToString(),
			ClearCoatFactor, Standard::Nodes::TextureSample::Outputs::R.ToString() );
	}

	//  ClearCoat::Parameters::ClearCoatRoughness
	{
		TVariant< FLinearColor, float > ClearCoatRoughnessFactor;
		ClearCoatRoughnessFactor.Set< float >( GltfMaterial.ClearCoat.Roughness );

		HandleGltfMaterialParameter( NodeContainer, GltfMaterial.ClearCoat.RoughnessMap, ShaderGraphNode, ClearCoat::Parameters::ClearCoatRoughness.ToString(),
			ClearCoatRoughnessFactor, Standard::Nodes::TextureSample::Outputs::G.ToString() );
	}

	// ClearCoat::Parameters::ClearCoatNormal
	{
		TVariant< FLinearColor, float > ClearCoatNormalFactor;
		ClearCoatNormalFactor.Set< float >( GltfMaterial.ClearCoat.NormalMapUVScale );

		const bool bInverse = false;
		const bool bIsNormal = true;

		HandleGltfMaterialParameter( NodeContainer, GltfMaterial.ClearCoat.NormalMap, ShaderGraphNode, ClearCoat::Parameters::ClearCoatNormal.ToString(),
			ClearCoatNormalFactor, Standard::Nodes::TextureSample::Outputs::RGB.ToString(), bInverse, bIsNormal );
	}
}

void UInterchangeGltfTranslator::HandleGltfSheen( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FMaterial& GltfMaterial, UInterchangeShaderGraphNode& ShaderGraphNode ) const
{
	using namespace UE::Interchange::Materials;

	if ( !GltfMaterial.bHasSheen )
	{
		return;
	}

	// Sheen::Parameters::SheenColor
	{
		TVariant< FLinearColor, float > SheenColorFactor;
		SheenColorFactor.Set< FLinearColor >( FLinearColor( GltfMaterial.Sheen.SheenColorFactor ) );

		HandleGltfMaterialParameter( NodeContainer, GltfMaterial.Sheen.SheenColorMap, ShaderGraphNode, Sheen::Parameters::SheenColor.ToString(),
			SheenColorFactor, Standard::Nodes::TextureSample::Outputs::RGB.ToString() );
	}

	// Sheen::Parameters::SheenRoughness
	{
		TVariant< FLinearColor, float > SheenRoughnessFactor;
		SheenRoughnessFactor.Set< float >( GltfMaterial.Sheen.SheenRoughnessFactor );

		HandleGltfMaterialParameter( NodeContainer, GltfMaterial.Sheen.SheenRoughnessMap, ShaderGraphNode, Sheen::Parameters::SheenRoughness.ToString(),
			SheenRoughnessFactor, Standard::Nodes::TextureSample::Outputs::A.ToString() );
	}
}

/**
 * GLTF transmission is handled a little differently than UE's.
 * GLTF doesn't allow having different reflected and transmitted colors, UE does (base color vs transmittance color).
 * GLTF controls the amount of reflected light vs transmitted light using the transmission factor, UE does that through opacity.
 * GLTF opacity means that the medium is present of not, so it's normal for transmission materials to be considered opaque,
 * meaning that the medium is full present, and the transmission factor determines how much lights is transmitted.
 * When a transmission material isn't fully opaque, we reduce the transmission color by the opacity to mimic GLTF's BTDF.
 * Ideally, this would be better represented by blending a default lit alpha blended material with a thin translucent material based on GLTF's opacity.
 */
void UInterchangeGltfTranslator::HandleGltfTransmission( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FMaterial& GltfMaterial, UInterchangeShaderGraphNode& ShaderGraphNode ) const
{
	using namespace UE::Interchange::Materials;

	if ( !GltfMaterial.bHasTransmission || FMath::IsNearlyZero( GltfMaterial.Transmission.TransmissionFactor ) )
	{
		return;
	}
 
	FString OpacityNodeUid;
	FString OpacityNodeOutput;

	// Common::Parameters::Opacity
	{
		/**
		 * Per the spec, the red channel of the transmission texture drives how much light is transmitted vs diffused.
		 * So we're setting the inverse of the red channel as the opacity.
		 */
		const FString OneMinusNodeName =  TEXT("OpacityOneMinus");
		UInterchangeShaderNode* OneMinusNode = UInterchangeShaderNode::Create( &NodeContainer, OneMinusNodeName, ShaderGraphNode.GetUniqueID() );

		OneMinusNode->SetCustomShaderType(Standard::Nodes::OneMinus::Name.ToString());

		UInterchangeShaderNode* CurrentNode = OneMinusNode;
		FString CurrentInput = Standard::Nodes::OneMinus::Inputs::Input.ToString();

		TVariant< FLinearColor, float > TransmissionFactor;
		TransmissionFactor.Set< float >( GltfMaterial.Transmission.TransmissionFactor );

		HandleGltfMaterialParameter( NodeContainer, GltfMaterial.Transmission.TransmissionMap, *CurrentNode, CurrentInput,
			TransmissionFactor, Standard::Nodes::TextureSample::Outputs::R.ToString() );

		// The GLTF transmission model specifies that metallic surfaces don't transmit light, so adjust Common::Parameters::Opacity so that metallic surfaces are opaque.
		{
			FString MetallicNodeUid;
			FString MetallicNodeOutput;

			if ( UInterchangeShaderPortsAPI::GetInputConnection( &ShaderGraphNode, PBR::Parameters::Metallic.ToString(), MetallicNodeUid, MetallicNodeOutput ) )
			{
				const FString MetallicLerpNodeName =  TEXT("OpacityMetallicLerp");
				UInterchangeShaderNode* LerpMetallicNode = UInterchangeShaderNode::Create( &NodeContainer, MetallicLerpNodeName, ShaderGraphNode.GetUniqueID() );
				LerpMetallicNode->SetCustomShaderType( Standard::Nodes::Lerp::Name.ToString() );


				LerpMetallicNode->AddFloatAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::Lerp::Inputs::B.ToString() ), 1.f );
				UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput( LerpMetallicNode, Standard::Nodes::Lerp::Inputs::A.ToString(), CurrentNode->GetUniqueID() );
				UInterchangeShaderPortsAPI::ConnectOuputToInput( LerpMetallicNode, Standard::Nodes::Lerp::Inputs::Factor.ToString(), MetallicNodeUid, MetallicNodeOutput );

				CurrentNode = LerpMetallicNode;
				CurrentInput = TEXT("");
			}
		}

		if ( GltfMaterial.AlphaMode != GLTF::FMaterial::EAlphaMode::Opaque )
		{
			if ( UInterchangeShaderPortsAPI::GetInputConnection( &ShaderGraphNode, PBR::Parameters::Opacity.ToString(), OpacityNodeUid, OpacityNodeOutput ) )
			{
				const FString OpacityLerpNodeName =  TEXT("OpacityLerp");
				UInterchangeShaderNode* OpacityLerpNode = UInterchangeShaderNode::Create( &NodeContainer, OpacityLerpNodeName, ShaderGraphNode.GetUniqueID() );
				OpacityLerpNode->SetCustomShaderType( Standard::Nodes::Lerp::Name.ToString() );

				OpacityLerpNode->AddFloatAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::Lerp::Inputs::A.ToString() ), 0.f );
				UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput( OpacityLerpNode, Standard::Nodes::Lerp::Inputs::B.ToString(), CurrentNode->GetUniqueID() );
				UInterchangeShaderPortsAPI::ConnectOuputToInput( OpacityLerpNode, Standard::Nodes::Lerp::Inputs::Factor.ToString(), OpacityNodeUid, OpacityNodeOutput );

				CurrentNode = OpacityLerpNode;
				CurrentInput = TEXT("");
			}
		}

		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput( &ShaderGraphNode, Common::Parameters::Opacity.ToString(), CurrentNode->GetUniqueID() );
	}

	// ThinTranslucent::Parameters::Transmissioncolor
	{
		// There's no separation of reflected and transmitted color in this model. So the same color is used for the base color and the transmitted color.
		// Since this extension is only supported with the metallic-roughness model, we can reuse its base color
		const UInterchangeBaseNode* CurrentNode = &ShaderGraphNode;
		FString CurrentOuput = TEXT("");
		FLinearColor CurrentColor = FLinearColor::White;

		FString BaseColorNodeUid;
		FString BaseColorNodeOutput;

		if ( UInterchangeShaderPortsAPI::GetInputConnection( CurrentNode, PBR::Parameters::BaseColor.ToString(), BaseColorNodeUid, BaseColorNodeOutput ) )
		{
			CurrentNode = NodeContainer.GetNode( BaseColorNodeUid );
			CurrentOuput = BaseColorNodeOutput;
		}
		else
		{
			FLinearColor BaseColor;
			if ( ShaderGraphNode.GetLinearColorAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( PBR::Parameters::BaseColor.ToString() ), BaseColor ) )
			{
				CurrentNode = nullptr;
				CurrentColor = BaseColor;
			}
		}

		if ( GltfMaterial.AlphaMode != GLTF::FMaterial::EAlphaMode::Opaque )
		{
			if ( !OpacityNodeUid.IsEmpty() )
			{
				const FString OpacityLerpNodeName =  TEXT("OpacityTransmissionLerp");
				UInterchangeShaderNode* OpacityLerpNode = UInterchangeShaderNode::Create( &NodeContainer, OpacityLerpNodeName, ShaderGraphNode.GetUniqueID() );
				OpacityLerpNode->SetCustomShaderType( Standard::Nodes::Lerp::Name.ToString() );

				OpacityLerpNode->AddLinearColorAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::Lerp::Inputs::A.ToString() ), FLinearColor::White );
				UInterchangeShaderPortsAPI::ConnectOuputToInput( OpacityLerpNode, Standard::Nodes::Lerp::Inputs::Factor.ToString(), OpacityNodeUid, OpacityNodeOutput );

				if ( CurrentNode )
				{
					UInterchangeShaderPortsAPI::ConnectOuputToInput( OpacityLerpNode, Standard::Nodes::Lerp::Inputs::B.ToString(), CurrentNode->GetUniqueID(), CurrentOuput );
				}
				else
				{
					OpacityLerpNode->AddLinearColorAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::Lerp::Inputs::B.ToString() ), CurrentColor );
				}

				CurrentNode = OpacityLerpNode;
				CurrentOuput = TEXT("");
			}
		}

		if ( CurrentNode )
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput( &ShaderGraphNode, ThinTranslucent::Parameters::TransmissionColor.ToString(), CurrentNode->GetUniqueID(), CurrentOuput );
		}
		else
		{
			ShaderGraphNode.AddLinearColorAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( ThinTranslucent::Parameters::TransmissionColor.ToString() ), CurrentColor );
		}
	}
}

void UInterchangeGltfTranslator::HandleGltfTextureTransform( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FTextureTransform& TextureTransform, const int32 TexCoordIndex, UInterchangeShaderNode& ShaderNode ) const
{
	using namespace UE::Interchange::Materials;

	UInterchangeShaderNode* CurrentNode = &ShaderNode;

	// Texture Coordinates
	{
		UInterchangeShaderNode* TexCoordNode = UInterchangeShaderNode::Create( &NodeContainer, TEXT("TexCoord"), ShaderNode.GetUniqueID() );
		TexCoordNode->SetCustomShaderType( Standard::Nodes::TextureCoordinate::Name.ToString() );

		TexCoordNode->AddInt32Attribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::TextureCoordinate::Inputs::Index.ToString() ), TexCoordIndex );

		CurrentNode = TexCoordNode;
	}

	// Scale
	if ( !FMath::IsNearlyEqual( TextureTransform.Scale[0], 1.f ) || 
		 !FMath::IsNearlyEqual( TextureTransform.Scale[1], 1.f ) )
	{
		FVector2f TextureScale;
		TextureScale.X = TextureTransform.Scale[0];
		TextureScale.Y = TextureTransform.Scale[1];

		CurrentNode->SetAttribute< FVector2f >( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::TextureCoordinate::Inputs::Scale.ToString() ), TextureScale );
	}

	// Offset
	if ( !FMath::IsNearlyZero( TextureTransform.Offset[0] ) || 
		 !FMath::IsNearlyZero( TextureTransform.Offset[1] ) )
	{
		FVector2f TextureOffset;
		TextureOffset.X = TextureTransform.Offset[0];
		TextureOffset.Y = TextureTransform.Offset[1];

		CurrentNode->SetAttribute< FVector2f >( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::TextureCoordinate::Inputs::Offset.ToString() ), TextureOffset );
	}

	// Rotate
	if ( !FMath::IsNearlyZero( TextureTransform.Rotation ) )
	{
		float AngleRadians = TextureTransform.Rotation;

		if ( AngleRadians < 0.0f )
		{
			AngleRadians = TWO_PI - AngleRadians;
		}

		AngleRadians = 1.0f - ( AngleRadians / TWO_PI );

		CurrentNode->AddFloatAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::TextureCoordinate::Inputs::Rotate.ToString() ), AngleRadians );

		FVector2f RotationCenter = FVector2f::Zero();
		CurrentNode->SetAttribute< FVector2f >( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::TextureCoordinate::Inputs::RotationCenter.ToString() ), RotationCenter );
	}

	if ( CurrentNode != &ShaderNode )
	{
		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput( &ShaderNode, Standard::Nodes::TextureSample::Inputs::Coordinates.ToString(), CurrentNode->GetUniqueID() );
	}
}

EInterchangeTranslatorType UInterchangeGltfTranslator::GetTranslatorType() const
{
	return EInterchangeTranslatorType::Scenes;
}

bool UInterchangeGltfTranslator::DoesSupportAssetType(EInterchangeTranslatorAssetType AssetType) const
{
	//gltf translator support Meshes and Materials
	if (AssetType == EInterchangeTranslatorAssetType::Materials || AssetType == EInterchangeTranslatorAssetType::Meshes)
	{
		return true;
	}

	return false;
}

TArray<FString> UInterchangeGltfTranslator::GetSupportedFormats() const
{
	TArray<FString> GltfExtensions;
	GltfExtensions.Reserve(2);
	GltfExtensions.Add(TEXT("gltf;GL Transmission Format"));
	GltfExtensions.Add(TEXT("glb;GL Transmission Format (Binary)"));

	return GltfExtensions;
}

bool UInterchangeGltfTranslator::Translate( UInterchangeBaseNodeContainer& NodeContainer ) const
{
	using namespace UE::Interchange::Gltf::Private;

	FString Filename = GetSourceData()->GetFilename();
	if ( !FPaths::FileExists( Filename ) )
	{
		return false;
	}

	GLTF::FFileReader GltfFileReader;

	const bool bLoadImageData = false;
	const bool bLoadMetaData = false;
	GltfFileReader.ReadFile( Filename, bLoadImageData, bLoadMetaData, const_cast< UInterchangeGltfTranslator* >( this )->GltfAsset );

	// Textures
	{
		int32 TextureIndex = 0;
		for ( const GLTF::FTexture& GltfTexture : GltfAsset.Textures )
		{
			const FString TextureName = GenerateTextureName( GltfTexture, TextureIndex );

			UInterchangeTexture2DNode* TextureNode = UInterchangeTexture2DNode::Create( &NodeContainer, TextureName );
			TextureNode->SetPayLoadKey( LexToString( TextureIndex++ ) );

			TextureNode->SetCustomWrapU( UE::Interchange::Gltf::Private::ConvertWrap( GltfTexture.Sampler.WrapS ) );
			TextureNode->SetCustomWrapV( UE::Interchange::Gltf::Private::ConvertWrap( GltfTexture.Sampler.WrapT ) );
		}
	}

	// Materials
	{
		int32 MaterialIndex = 0;
		for ( const GLTF::FMaterial& GltfMaterial : GltfAsset.Materials )
		{
			UInterchangeShaderGraphNode* ShaderGraphNode = UInterchangeShaderGraphNode::Create( &NodeContainer, TEXT("Material_") + GenerateNameForGltfNode( GltfMaterial.Name, MaterialIndex ) );

			HandleGltfMaterial( NodeContainer, GltfMaterial, *ShaderGraphNode );
			++MaterialIndex;
		}
	}

	// Meshes
	{
		int32 MeshIndex = 0;
		for ( const GLTF::FMesh& GltfMesh : GltfAsset.Meshes )
		{
			UInterchangeMeshNode* MeshNode = NewObject< UInterchangeMeshNode >( &NodeContainer );
			FString MeshNodeUid = TEXT("\\Mesh\\") + GenerateNameForGltfNode( GltfMesh.Name, MeshIndex );

			MeshNode->InitializeNode( MeshNodeUid, GenerateNameForGltfNode( GltfMesh.Name, MeshIndex ), EInterchangeNodeContainerType::TranslatedAsset );
			MeshNode->SetPayLoadKey( LexToString( MeshIndex++ ) );
			NodeContainer.AddNode( MeshNode );

			// Assign materials
			for ( const GLTF::FPrimitive& Primitive : GltfMesh.Primitives )
			{
				if ( GltfAsset.Materials.IsValidIndex( Primitive.MaterialIndex ) )
				{
					const FString MaterialName = GenerateNameForGltfNode(GltfAsset.Materials[Primitive.MaterialIndex].Name, Primitive.MaterialIndex);
					const FString ShaderGraphNodeUid = UInterchangeShaderGraphNode::MakeNodeUid( TEXT("Material_") + MaterialName);
					MeshNode->SetSlotMaterialDependencyUid(MaterialName, ShaderGraphNodeUid);
				}
			}
		}
	}

	// Cameras
	{
		int32 CameraIndex = 0;
		for ( const GLTF::FCamera& GltfCamera : GltfAsset.Cameras )
		{
			UInterchangeCameraNode* CameraNode = NewObject< UInterchangeCameraNode >( &NodeContainer );
			FString CameraNodeUid = TEXT("\\Camera\\") + GenerateNameForGltfNode( GltfCamera.Name, CameraIndex );
			CameraNode->InitializeNode( CameraNodeUid, GltfCamera.Name, EInterchangeNodeContainerType::TranslatedAsset );
			NodeContainer.AddNode( CameraNode );
			++CameraIndex;
		}
	}

	// Lights
	{
		int32 LightIndex = 0;
		for ( const GLTF::FLight& GltfLight : GltfAsset.Lights )
		{
			UInterchangeLightNode* LightNode = NewObject< UInterchangeLightNode >( &NodeContainer );
			FString LightNodeUid = TEXT("\\Light\\") + GenerateNameForGltfNode( GltfLight.Name, LightIndex );
			LightNode->InitializeNode( LightNodeUid, GltfLight.Name, EInterchangeNodeContainerType::TranslatedAsset );
			NodeContainer.AddNode( LightNode );
			++LightIndex;
		}
	}

	// Scenes
	{
		int32 SceneIndex = 0;
		for ( const GLTF::FScene& GltfScene : GltfAsset.Scenes )
		{
			UInterchangeSceneNode* SceneNode = NewObject< UInterchangeSceneNode >( &NodeContainer );

			FString SceneName = GltfScene.Name;
			if (SceneName.IsEmpty())
			{
				SceneName = TEXT("Scene");
			}

			SceneName = GenerateNameForGltfNode( SceneName, SceneIndex );

			FString SceneNodeUid = TEXT("\\Scene\\") + SceneName;
			SceneNode->InitializeNode( SceneNodeUid, SceneName, EInterchangeNodeContainerType::TranslatedScene );
			NodeContainer.AddNode( SceneNode );

			for ( const int32 NodeIndex : GltfScene.Nodes )
			{
				if ( GltfAsset.Nodes.IsValidIndex( NodeIndex ) )
				{
					HandleGltfNode( NodeContainer, GltfAsset.Nodes[ NodeIndex ], SceneNodeUid, NodeIndex );
				}
			}

			++SceneIndex;
		}
	}

	return true;
}

TFuture< TOptional< UE::Interchange::FStaticMeshPayloadData > > UInterchangeGltfTranslator::GetStaticMeshPayloadData( const FString& PayLoadKey ) const
{
	using namespace UE::Interchange::Gltf::Private;

	TPromise< TOptional< UE::Interchange::FStaticMeshPayloadData > > MeshPayloadDataPromise;

	int32 MeshIndex = 0;
	LexFromString( MeshIndex, *PayLoadKey );

	if ( !GltfAsset.Meshes.IsValidIndex( MeshIndex ) )
	{
		MeshPayloadDataPromise.SetValue(TOptional< UE::Interchange::FStaticMeshPayloadData >());
		return MeshPayloadDataPromise.GetFuture();
	}

	UE::Interchange::FStaticMeshPayloadData StaticMeshPayloadData;

	const GLTF::FMesh& GltfMesh = GltfAsset.Meshes[ MeshIndex ];
	GLTF::FMeshFactory MeshFactory;
	MeshFactory.SetUniformScale( 100.f ); // GLTF is in meters while UE is in centimeters
	MeshFactory.FillMeshDescription( GltfMesh, &StaticMeshPayloadData.MeshDescription );

	// Patch polygon groups material slot names to match Interchange expectations (rename material slots from indices to material names)
	{
		FStaticMeshAttributes StaticMeshAttributes( StaticMeshPayloadData.MeshDescription );

		for ( int32 MaterialSlotIndex = 0; MaterialSlotIndex < StaticMeshAttributes.GetPolygonGroupMaterialSlotNames().GetNumElements(); ++MaterialSlotIndex )
		{
			int32 MaterialIndex = 0;
			LexFromString( MaterialIndex, *StaticMeshAttributes.GetPolygonGroupMaterialSlotNames()[ MaterialSlotIndex ].ToString() );

			if ( GltfAsset.Materials.IsValidIndex( MaterialIndex ) )
			{
				const FString MaterialName = GenerateNameForGltfNode(GltfAsset.Materials[MaterialIndex].Name, MaterialIndex);
				StaticMeshAttributes.GetPolygonGroupMaterialSlotNames()[ MaterialSlotIndex ] = *MaterialName;
			}
		}
	}

	MeshPayloadDataPromise.SetValue( StaticMeshPayloadData );

	return MeshPayloadDataPromise.GetFuture();
}

TOptional< UE::Interchange::FImportImage > UInterchangeGltfTranslator::GetTexturePayloadData( const UInterchangeSourceData* InSourceData, const FString& PayLoadKey ) const
{
	int32 TextureIndex = 0;
	LexFromString( TextureIndex, *PayLoadKey );

	if ( !GltfAsset.Textures.IsValidIndex( TextureIndex ) )
	{
		return TOptional< UE::Interchange::FImportImage >();
	}

	const GLTF::FTexture& GltfTexture = GltfAsset.Textures[ TextureIndex ];

	if (GltfTexture.Source.FilePath.IsEmpty())
	{
		// Embedded texture -- try using ImageWrapper to decode it
		TArray64<uint8> ImageData(GltfTexture.Source.Data, GltfTexture.Source.DataByteLength);
		return UInterchangeImageWrapperTranslator::GetTexturePayloadDataFromBuffer(ImageData);
	}
	else
	{
		const FString TextureFilePath = FPaths::ConvertRelativePathToFull(GltfTexture.Source.FilePath);

		UInterchangeSourceData* PayloadSourceData = UInterchangeManager::GetInterchangeManager().CreateSourceData(TextureFilePath);
		FGCObjectScopeGuard ScopedSourceData(PayloadSourceData);

		if (!PayloadSourceData)
		{
			return TOptional<UE::Interchange::FImportImage>();
		}

		UInterchangeTranslatorBase* SourceTranslator = UInterchangeManager::GetInterchangeManager().GetTranslatorForSourceData(PayloadSourceData);
		FGCObjectScopeGuard ScopedSourceTranslator(SourceTranslator);
		const IInterchangeTexturePayloadInterface* TextureTranslator = Cast< IInterchangeTexturePayloadInterface >(SourceTranslator);
		if (!ensure(TextureTranslator))
		{
			return TOptional<UE::Interchange::FImportImage>();
		}

		return TextureTranslator->GetTexturePayloadData(PayloadSourceData, TextureFilePath);
	}
}