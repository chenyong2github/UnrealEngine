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

#include "Algo/Find.h"
#include "StaticMeshAttributes.h"
#include "UObject/GCObjectScopeGuard.h"

void UInterchangeGltfTranslator::HandleGltfNode( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FNode& GltfNode, const FString& ParentNodeUid ) const
{
	const FString NodeUid = ParentNodeUid + TEXT("\\") + GltfNode.Name;

	UInterchangeSceneNode* ParentSceneNode = Cast< UInterchangeSceneNode >( NodeContainer.GetNode( ParentNodeUid ) );

	UInterchangeSceneNode* InterchangeSceneNode = NewObject< UInterchangeSceneNode >( &NodeContainer );
	InterchangeSceneNode->InitializeNode( NodeUid, GltfNode.Name, EInterchangeNodeContainerType::TranslatedScene );
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
				const FString MeshNodeUid = TEXT("\\Mesh\\") + GltfAsset.Meshes[ GltfNode.MeshIndex ].Name;
				InterchangeSceneNode->SetCustomAssetInstanceUid( MeshNodeUid );
			}
			break;
		}

		case GLTF::FNode::EType::Camera:
		{
			Transform.ConcatenateRotation(FRotator(0, -90, 0).Quaternion());

			if ( GltfAsset.Cameras.IsValidIndex( GltfNode.CameraIndex ) )
			{
				const FString CameraNodeUid = TEXT("\\Camera\\") + GltfAsset.Cameras[ GltfNode.CameraIndex ].Name;
				InterchangeSceneNode->SetCustomAssetInstanceUid( CameraNodeUid );
			}
			break;
		}

		case GLTF::FNode::EType::Light:
		{
			Transform.ConcatenateRotation(FRotator(0, -90, 0).Quaternion());

			if ( GltfAsset.Lights.IsValidIndex( GltfNode.LightIndex ) )
			{
				const FString LightNodeUid = TEXT("\\Light\\") + GltfAsset.Lights[ GltfNode.LightIndex ].Name;
				InterchangeSceneNode->SetCustomAssetInstanceUid( LightNodeUid );
			}
		}

		case GLTF::FNode::EType::Transform:
		default:
		{
			break;
		}
	}

	InterchangeSceneNode->SetCustomLocalTransform(&NodeContainer, Transform );

	if ( !ParentNodeUid.IsEmpty() )
	{
		NodeContainer.SetNodeParentUid( NodeUid, ParentNodeUid );
	}

	for ( const int32 ChildIndex : GltfNode.Children )
	{
		if ( GltfAsset.Nodes.IsValidIndex( ChildIndex ) )
		{
			HandleGltfNode( NodeContainer, GltfAsset.Nodes[ ChildIndex ], NodeUid );
		}
	}
}

void UInterchangeGltfTranslator::HandleGltfMaterialParameter( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FTextureMap& TextureMap, UInterchangeShaderGraphNode& ShaderGraphNode,
		const FString& MapName, const TVariant< FLinearColor, float >& MapFactor, const FString& OutputChannel, const bool bInverse ) const
{
	using namespace UE::Interchange::Materials;

	UInterchangeShaderNode* NodeToConnectTo = &ShaderGraphNode;
	FString InputToConnectTo = MapName;

	if (bInverse)
	{
		const FString OneMinusNodeName = MapName + TEXT("OneMinus");
		const FString OneMinusNodeUid = ShaderGraphNode.GetUniqueID() + TEXT("_") + OneMinusNodeName;
		UInterchangeShaderNode* OneMinusNode = NewObject< UInterchangeShaderNode >( &NodeContainer );
		OneMinusNode->InitializeNode( OneMinusNodeUid, OneMinusNodeName, EInterchangeNodeContainerType::TranslatedAsset );
		NodeContainer.AddNode( OneMinusNode );
		NodeContainer.SetNodeParentUid( OneMinusNodeUid, ShaderGraphNode.GetUniqueID() );

		OneMinusNode->SetCustomShaderType(Standard::Nodes::OneMinus::Name.ToString());

		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(NodeToConnectTo, InputToConnectTo, OneMinusNode->GetUniqueID());

		NodeToConnectTo = OneMinusNode;
		InputToConnectTo = Standard::Nodes::OneMinus::Inputs::Input.ToString();
	}

	if ( GltfAsset.Textures.IsValidIndex( TextureMap.TextureIndex ) )
	{
		const FString NodeName = MapName;
		const FString NodeUid = ShaderGraphNode.GetUniqueID() + TEXT("_") + NodeName;

		UInterchangeShaderNode* ColorNode = NewObject< UInterchangeShaderNode >( &NodeContainer );
		ColorNode->InitializeNode( NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset );
		NodeContainer.AddNode( ColorNode );
		NodeContainer.SetNodeParentUid( NodeUid, ShaderGraphNode.GetUniqueID() );

		ColorNode->SetCustomShaderType( Standard::Nodes::TextureSample::Name.ToString() );
		const FString TextureUid = TEXT("\\Texture\\") + GltfAsset.Textures[ TextureMap.TextureIndex ].Source.URI;
		ColorNode->AddStringAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::TextureSample::Inputs::Texture.ToString() ), TextureUid );

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
			const FString FactorNodeUid = NodeUid + TEXT("_Factor");
			UInterchangeShaderNode* FactorNode = NewObject< UInterchangeShaderNode >( &NodeContainer );
			FactorNode->InitializeNode( FactorNodeUid, NodeName + TEXT("_Factor"), EInterchangeNodeContainerType::TranslatedAsset );
			NodeContainer.AddNode( FactorNode );
			NodeContainer.SetNodeParentUid( FactorNodeUid, ShaderGraphNode.GetUniqueID() );

			FactorNode->SetCustomShaderType( Standard::Nodes::Multiply::Name.ToString() );

			if ( MapFactor.IsType< float >() )
			{
				FactorNode->AddFloatAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::Multiply::Inputs::B.ToString() ), MapFactor.Get< float >() );
			}
			else if ( MapFactor.IsType< FLinearColor >() )
			{
				FactorNode->AddLinearColorAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( Standard::Nodes::Multiply::Inputs::B.ToString() ), MapFactor.Get< FLinearColor >() );
			}

			UInterchangeShaderPortsAPI::ConnectOuputToInput( FactorNode, Standard::Nodes::Multiply::Inputs::A.ToString(), NodeUid, OutputChannel );
			UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput( NodeToConnectTo, InputToConnectTo, FactorNodeUid );
		}
		else
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput( NodeToConnectTo, InputToConnectTo, NodeUid, OutputChannel );
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

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.Normal, ShaderGraphNode, Common::Parameters::Normal.ToString(),
				NormalFactor, Standard::Nodes::TextureSample::Outputs::RGB.ToString() );
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
				OcclusionFactor, Standard::Nodes::TextureSample::Outputs::RGB.ToString() );
		}

		// Opacity (use the base color alpha channel)
		if ( GltfMaterial.AlphaMode != GLTF::FMaterial::EAlphaMode::Opaque )
		{
			TVariant< FLinearColor, float > OpacityFactor;
			OpacityFactor.Set< float >( GltfMaterial.BaseColorFactor.W );

			HandleGltfMaterialParameter( NodeContainer, GltfMaterial.BaseColor, ShaderGraphNode, PBR::Parameters::Opacity.ToString(),
				OpacityFactor, Standard::Nodes::TextureSample::Outputs::A.ToString() );

			ShaderGraphNode.AddFloatAttribute( UInterchangeShaderPortsAPI::MakeInputValueKey( PBR::Parameters::IndexOfRefraction.ToString() ), GltfMaterial.IOR );
		}
	}
}

EInterchangeTranslatorType UInterchangeGltfTranslator::GetTranslatorType() const
{
	return EInterchangeTranslatorType::Scenes;
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
			UInterchangeTexture2DNode* TextureNode = NewObject< UInterchangeTexture2DNode >( &NodeContainer );
			FString TextureNodeUid = TEXT("\\Texture\\") + GltfTexture.Source.URI;
			TextureNode->InitializeNode( TextureNodeUid, GltfTexture.Source.URI, EInterchangeNodeContainerType::TranslatedAsset );
			TextureNode->SetPayLoadKey( LexToString( TextureIndex++ ) );
			NodeContainer.AddNode( TextureNode );
		}
	}

	// Materials
	{
		for ( const GLTF::FMaterial& GltfMaterial : GltfAsset.Materials )
		{
			UInterchangeShaderGraphNode* ShaderGraphNode = NewObject< UInterchangeShaderGraphNode >( &NodeContainer );
			FString ShaderGraphNodeUid = TEXT("\\Material\\") + GltfMaterial.Name;
			ShaderGraphNode->InitializeNode( ShaderGraphNodeUid, GltfMaterial.Name, EInterchangeNodeContainerType::TranslatedAsset );
			NodeContainer.AddNode( ShaderGraphNode );

			HandleGltfMaterial( NodeContainer, GltfMaterial, *ShaderGraphNode );
		}
	}

	// Meshes
	{
		int32 MeshIndex = 0;
		for ( const GLTF::FMesh& GltfMesh : GltfAsset.Meshes )
		{
			UInterchangeMeshNode* MeshNode = NewObject< UInterchangeMeshNode >( &NodeContainer );
			FString MeshNodeUid = TEXT("\\Mesh\\") + GltfMesh.Name;
			MeshNode->InitializeNode( MeshNodeUid, GltfMesh.Name, EInterchangeNodeContainerType::TranslatedAsset );
			MeshNode->SetPayLoadKey( LexToString( MeshIndex++ ) );
			NodeContainer.AddNode( MeshNode );

			// Assign materials
			for ( const GLTF::FPrimitive& Primitive : GltfMesh.Primitives )
			{
				if ( GltfAsset.Materials.IsValidIndex( Primitive.MaterialIndex ) )
				{
					const FString ShaderGraphNodeUid = TEXT("\\Material\\") + GltfAsset.Materials[ Primitive.MaterialIndex ].Name;
					MeshNode->SetMaterialDependencyUid( ShaderGraphNodeUid );
				}
			}
		}
	}

	// Cameras
	{
		for ( const GLTF::FCamera& GltfCamera : GltfAsset.Cameras )
		{
			UInterchangeCameraNode* CameraNode = NewObject< UInterchangeCameraNode >( &NodeContainer );
			FString CameraNodeUid = TEXT("\\Camera\\") + GltfCamera.Name;
			CameraNode->InitializeNode( CameraNodeUid, GltfCamera.Name, EInterchangeNodeContainerType::TranslatedAsset );
			NodeContainer.AddNode( CameraNode );
		}
	}

	// Lights
	{
		for ( const GLTF::FLight& GltfLight : GltfAsset.Lights )
		{
			UInterchangeLightNode* LightNode = NewObject< UInterchangeLightNode >( &NodeContainer );
			FString LightNodeUid = TEXT("\\Light\\") + GltfLight.Name;
			LightNode->InitializeNode( LightNodeUid, GltfLight.Name, EInterchangeNodeContainerType::TranslatedAsset );
			NodeContainer.AddNode( LightNode );
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
				if ( GltfAsset.Scenes.Num() > 1 )
				{
					SceneName += TEXT("_") + LexToString( SceneIndex );
				}
			}

			FString SceneNodeUid = TEXT("\\Scene\\") + SceneName;
			SceneNode->InitializeNode( SceneNodeUid, SceneName, EInterchangeNodeContainerType::TranslatedScene );
			NodeContainer.AddNode( SceneNode );

			for ( const int32 NodeIndex : GltfScene.Nodes )
			{
				if ( GltfAsset.Nodes.IsValidIndex( NodeIndex ) )
				{
					HandleGltfNode( NodeContainer, GltfAsset.Nodes[ NodeIndex ], SceneNodeUid );
				}
			}

			++SceneIndex;
		}
	}

	return true;
}

TFuture< TOptional< UE::Interchange::FStaticMeshPayloadData > > UInterchangeGltfTranslator::GetStaticMeshPayloadData( const FString& PayLoadKey ) const
{
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
				StaticMeshAttributes.GetPolygonGroupMaterialSlotNames()[ MaterialSlotIndex ] = *GltfAsset.Materials[ MaterialIndex ].Name;
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

	GLTF::FTexture GltfTexture = GltfAsset.Textures[ TextureIndex ];

	UInterchangeSourceData* PayloadSourceData = UInterchangeManager::GetInterchangeManager().CreateSourceData( GltfTexture.Source.FilePath );
	FGCObjectScopeGuard ScopedSourceData( PayloadSourceData );
	
	if ( !PayloadSourceData )
	{
		return TOptional<UE::Interchange::FImportImage>();
	}

	UInterchangeTranslatorBase* SourceTranslator = UInterchangeManager::GetInterchangeManager().GetTranslatorForSourceData( PayloadSourceData );
	FGCObjectScopeGuard ScopedSourceTranslator( SourceTranslator );
	const IInterchangeTexturePayloadInterface* TextureTranslator = Cast< IInterchangeTexturePayloadInterface >( SourceTranslator );
	if ( !ensure( TextureTranslator ) )
	{
		return TOptional<UE::Interchange::FImportImage>();
	}

	return TextureTranslator->GetTexturePayloadData( PayloadSourceData, GltfTexture.Source.FilePath );
}