// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxMaterial.h"

#include "FbxAPI.h"
#include "FbxConvert.h"
#include "FbxHelper.h"
#include "FbxInclude.h"
#include "Fbx/InterchangeFbxMessages.h"
#include "InterchangeMaterialDefinitions.h"
#include "InterchangeResultsContainer.h"
#include "InterchangeSceneNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTextureNode.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"



#define LOCTEXT_NAMESPACE "InterchangeFbxMaterial"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			UInterchangeShaderGraphNode* FFbxMaterial::CreateShaderGraphNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeUid, const FString& NodeName)
			{
				UInterchangeShaderGraphNode* ShaderGraphNode = NewObject<UInterchangeShaderGraphNode>(&NodeContainer);
				ShaderGraphNode->InitializeNode(NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
				NodeContainer.AddNode(ShaderGraphNode);

				return ShaderGraphNode;
			}

			const UInterchangeTexture2DNode* FFbxMaterial::CreateTexture2DNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& TextureFilePath)
			{
				const FString TextureName = FPaths::GetBaseFilename(TextureFilePath);
				const FString TextureNodeID = UInterchangeTextureNode::MakeNodeUid(TextureName);

				if (const UInterchangeTexture2DNode* TextureNode = Cast<const UInterchangeTexture2DNode>(NodeContainer.GetNode(TextureNodeID)))
				{
					return TextureNode;
				}

				UInterchangeTexture2DNode* NewTextureNode = UInterchangeTexture2DNode::Create(&NodeContainer, TextureName);

				//All texture translator expect a file as the payload key
				FString NormalizeFilePath = TextureFilePath;
				FPaths::NormalizeFilename(NormalizeFilePath);
				NewTextureNode->SetPayLoadKey(NormalizeFilePath);

				return NewTextureNode;
			}

			UInterchangeShaderNode* FFbxMaterial::CreateTextureSampler(FbxFileTexture* FbxTexture, UInterchangeBaseNodeContainer& NodeContainer, const FString& ShaderUniqueID)
			{
				using namespace Materials::Standard::Nodes;

				if (!FbxTexture)
				{
					return nullptr;
				}

				const FString TextureFilename = FbxTexture ? UTF8_TO_TCHAR(FbxTexture->GetFileName()) : TEXT("");
				const FString TextureName = FPaths::GetBaseFilename(TextureFilename);

				UInterchangeShaderNode* TextureSampleShader = UInterchangeShaderNode::Create(&NodeContainer, TextureName, ShaderUniqueID);
				TextureSampleShader->SetCustomShaderType(TextureSample::Name.ToString());

				// Return incomplete texture sampler if texture file does not exist
				if (TextureFilename.IsEmpty() || !FPaths::FileExists(TextureFilename))
				{
					const UInterchangeBaseNode* ShaderGraphNode = NodeContainer.GetNode(ShaderUniqueID);
					UInterchangeResultTextureWarning_TextureFileDoNotExist* Message = Parser.AddMessage<UInterchangeResultTextureWarning_TextureFileDoNotExist>();
					Message->TextureName = TextureFilename;
					Message->MaterialName = ShaderGraphNode ? ShaderGraphNode->GetDisplayLabel() : TEXT("Unknown");

					return TextureSampleShader;
				}

				const UInterchangeTexture2DNode* TextureNode = Cast<const UInterchangeTexture2DNode>(NodeContainer.GetNode(UInterchangeTextureNode::MakeNodeUid(TextureName)));

				if (!TextureNode)
				{
					TextureNode = CreateTexture2DNode(NodeContainer, TextureFilename);
				}

				TextureSampleShader->AddStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(TextureSample::Inputs::Texture.ToString()), TextureNode->GetUniqueID());

				if (!FMath::IsNearlyEqual(FbxTexture->GetScaleU(), 1.0) || !FMath::IsNearlyEqual(FbxTexture->GetScaleV(), 1.0))
				{
					UInterchangeShaderNode* TextureCoordinateShader = UInterchangeShaderNode::Create(&NodeContainer, TextureName + TEXT("_Coordinate"), ShaderUniqueID);
					TextureCoordinateShader->SetCustomShaderType(TextureCoordinate::Name.ToString());

					TextureCoordinateShader->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(TextureCoordinate::Inputs::UTiling.ToString()), (float)FbxTexture->GetScaleU());
					TextureCoordinateShader->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(TextureCoordinate::Inputs::VTiling.ToString()), (float)FbxTexture->GetScaleV());

					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(TextureSampleShader, TextureSample::Inputs::Coordinates.ToString(), TextureCoordinateShader->GetUniqueID());
				}

				return TextureSampleShader;
			}

			bool FFbxMaterial::ConvertPropertyToShaderNode(UInterchangeBaseNodeContainer& NodeContainer, UInterchangeShaderGraphNode* ShaderGraphNode, FbxProperty& Property, float Factor, FName InputName,
				TVariant<FLinearColor, float> DefaultValue, bool bInverse)
			{
				using namespace Materials::Standard::Nodes;

				UInterchangeShaderNode* NodeToConnectTo = ShaderGraphNode;
				FString InputToConnectTo = InputName.ToString();

				const int32 TextureCount = Property.GetSrcObjectCount<FbxFileTexture>();
				const FbxDataType DataType = Property.GetPropertyDataType();

				if (bInverse)
				{
					const FString OneMinusNodeName = InputName.ToString() + TEXT("OneMinus");
					UInterchangeShaderNode* OneMinusNode = UInterchangeShaderNode::Create(&NodeContainer, OneMinusNodeName, ShaderGraphNode->GetUniqueID());
					OneMinusNode->SetCustomShaderType(OneMinus::Name.ToString());

					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ShaderGraphNode, InputName.ToString(), OneMinusNode->GetUniqueID());

					NodeToConnectTo = OneMinusNode;
					InputToConnectTo = OneMinus::Inputs::Input.ToString();
				}

				if (!FMath::IsNearlyEqual(Factor, 1.f))
				{
					FString LerpNodeName = InputName.ToString() + TEXT("Lerp");
					UInterchangeShaderNode* LerpNode = UInterchangeShaderNode::Create(&NodeContainer, LerpNodeName, ShaderGraphNode->GetUniqueID());
					LerpNode->SetCustomShaderType(Lerp::Name.ToString());

					if (DefaultValue.IsType<float>())
					{
						LerpNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Lerp::Inputs::B.ToString()), DefaultValue.Get<float>());
					}
					else if (DefaultValue.IsType<FLinearColor>())
					{
						LerpNode->AddLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Lerp::Inputs::B.ToString()), DefaultValue.Get<FLinearColor>());
					}

					const float InverseFactor = 1.f - Factor; // We lerp from A to B and prefer to put the strongest input in A so we need to flip the lerp factor
					LerpNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Lerp::Inputs::Factor.ToString()), InverseFactor);

					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(NodeToConnectTo, InputToConnectTo, LerpNode->GetUniqueID());

					NodeToConnectTo = LerpNode;
					InputToConnectTo = Lerp::Inputs::A.ToString();
				}

				// Handles max one texture per property.
				if (TextureCount > 0)
				{
					FbxFileTexture* FbxTexture = Property.GetSrcObject<FbxFileTexture>(0);
					if (UInterchangeShaderNode* TextureSampleShader = CreateTextureSampler(FbxTexture, NodeContainer, ShaderGraphNode->GetUniqueID()))
					{
						UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(NodeToConnectTo, InputToConnectTo, TextureSampleShader->GetUniqueID());
					}
					else
					{
						UInterchangeResultTextureWarning_TextureFileDoNotExist* Message = Parser.AddMessage<UInterchangeResultTextureWarning_TextureFileDoNotExist>();
						Message->TextureName = FbxTexture ? UTF8_TO_TCHAR(FbxTexture->GetFileName()) : TEXT("Undefined");
						Message->MaterialName = ShaderGraphNode->GetDisplayLabel();

						return false;
					}
				}
				else if (DataType.GetType() == eFbxDouble || DataType.GetType() == eFbxFloat || DataType.GetType() == eFbxInt)
				{
					NodeToConnectTo->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputToConnectTo), Property.Get<float>());
				}
				else if (DataType.GetType() == eFbxDouble3)
				{
					const FLinearColor PropertyValue = FFbxConvert::ConvertColor(Property.Get<FbxDouble3>());

					if (DefaultValue.IsType<FLinearColor>())
					{
						NodeToConnectTo->AddLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputToConnectTo), PropertyValue);
					}
					else if (DefaultValue.IsType<float>())
					{
						// We're connecting a linear color to a float input. Ideally, we'd go through a desaturate, but for now we'll just take the red channel and ignore the rest.
						NodeToConnectTo->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputToConnectTo), PropertyValue.R);
					}
				}

				return true;
			}

			void FFbxMaterial::ConvertShininessToShaderNode(FbxSurfaceMaterial& SurfaceMaterial, UInterchangeBaseNodeContainer& NodeContainer, UInterchangeShaderGraphNode* ShaderGraphNode)
			{
				using namespace UE::Interchange::Materials;
				using namespace UE::Interchange::Materials::Standard::Nodes;

				FBXSDK_NAMESPACE::FbxProperty MaterialProperty = SurfaceMaterial.FindProperty(FbxSurfaceMaterial::sShininess);

				if (!MaterialProperty.IsValid())
				{
					return;
				}

				const FString InputName = Phong::Parameters::Shininess.ToString();

				if (MaterialProperty.GetSrcObjectCount<FBXSDK_NAMESPACE::FbxTexture>() > 0)
				{
					FbxFileTexture* FbxTexture = MaterialProperty.GetSrcObject<FbxFileTexture>(0);
					if (UInterchangeShaderNode* TextureSampleShader = CreateTextureSampler(FbxTexture, NodeContainer, ShaderGraphNode->GetUniqueID()))
					{
						FString MultiplyNodeName = Phong::Parameters::Shininess.ToString() + TEXT("_Multiply");
						UInterchangeShaderNode* MultiplyNode = UInterchangeShaderNode::Create(&NodeContainer, MultiplyNodeName, ShaderGraphNode->GetUniqueID());
						MultiplyNode->SetCustomShaderType(Multiply::Name.ToString());
						
						UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ShaderGraphNode, InputName, MultiplyNode->GetUniqueID());

						// Scale texture output from [0-1] to [0-1000]
						MultiplyNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Multiply::Inputs::B.ToString()), 1000.0f);

						UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(MultiplyNode, Multiply::Inputs::A.ToString(), TextureSampleShader->GetUniqueID());
					}
				}
				else if (!FBXSDK_NAMESPACE::FbxProperty::HasDefaultValue(MaterialProperty))
				{
					const FString InputValueKey = UInterchangeShaderPortsAPI::MakeInputValueKey(InputName);
					ShaderGraphNode->AddFloatAttribute(InputValueKey, MaterialProperty.Get<float>());
				}
			}


			const UInterchangeShaderGraphNode* FFbxMaterial::AddShaderGraphNode(FbxSurfaceMaterial* SurfaceMaterial, UInterchangeBaseNodeContainer& NodeContainer)
			{
				using namespace UE::Interchange::Materials;

				if (!SurfaceMaterial)
				{
					return nullptr;
				}

				//Create a material node
				FString MaterialName = Parser.GetFbxHelper()->GetFbxObjectName(SurfaceMaterial);
				FString NodeUid = TEXT("\\Material\\") + MaterialName;
				const UInterchangeShaderGraphNode* ExistingShaderGraphNode = Cast<const UInterchangeShaderGraphNode>(NodeContainer.GetNode(NodeUid));
				if (ExistingShaderGraphNode)
				{
					return ExistingShaderGraphNode;
				}


				UInterchangeShaderGraphNode* ShaderGraphNode = CreateShaderGraphNode(NodeContainer, NodeUid, MaterialName);
				if (ShaderGraphNode == nullptr)
				{
					FFormatNamedArguments Args
					{
						{ TEXT("MaterialName"), FText::FromString(MaterialName) }
					};
					UInterchangeResultError_Generic* Message = Parser.AddMessage<UInterchangeResultError_Generic>();
					Message->Text = FText::Format(LOCTEXT("CannotCreateFBXMaterial", "Cannot create FBX material '{MaterialName}'."), Args);
					return nullptr;
				}

				// If not a Lambert nor a Phong material, match legacy FBX import
				// Use random color because there may be multiple materials, then they can be different
				if (!SurfaceMaterial->Is<FbxSurfaceLambert>())
				{
					FLinearColor BaseColor;
					BaseColor.R = 0.5f + 0.5f * FMath::FRand();
					BaseColor.G = 0.5f + 0.5f * FMath::FRand();
					BaseColor.B = 0.5f + 0.5f * FMath::FRand();

					const FString InputValueKey = UInterchangeShaderPortsAPI::MakeInputValueKey(Phong::Parameters::DiffuseColor.ToString());
					ShaderGraphNode->AddLinearColorAttribute(InputValueKey, BaseColor);

					return ShaderGraphNode;
				}

				TFunction<bool(FBXSDK_NAMESPACE::FbxProperty&)> ShouldConvertProperty = [&](FBXSDK_NAMESPACE::FbxProperty& MaterialProperty) -> bool
				{
					bool bShouldConvertProperty = false;

					if (MaterialProperty.IsValid())
					{
						// FbxProperty::HasDefaultValue(..) can return true while the property has textures attached to it.
						bShouldConvertProperty = MaterialProperty.GetSrcObjectCount<FBXSDK_NAMESPACE::FbxTexture>() > 0
							|| !FBXSDK_NAMESPACE::FbxProperty::HasDefaultValue(MaterialProperty);
					}

					return bShouldConvertProperty;
				};

				TFunction<bool(FName, const char*, float , TVariant<FLinearColor, float>&, bool)>  ConnectInput;
				ConnectInput = [&](FName InputName, const char* FbxPropertyName, float Factor, TVariant<FLinearColor, float>& DefaultValue, bool bInverse) -> bool
				{
					FBXSDK_NAMESPACE::FbxProperty MaterialProperty = SurfaceMaterial->FindProperty(FbxPropertyName);
					if (ShouldConvertProperty(MaterialProperty))
					{
						return ConvertPropertyToShaderNode(NodeContainer, ShaderGraphNode, MaterialProperty, Factor, InputName, DefaultValue, bInverse);
					}

					return false;
				};

				// Diffuse
				{
					const float Factor = (float)((FbxSurfaceLambert*)SurfaceMaterial)->DiffuseFactor.Get();
					TVariant<FLinearColor, float> DefaultValue;
					DefaultValue.Set<FLinearColor>(FLinearColor::Black);
					if (!ConnectInput(Phong::Parameters::DiffuseColor, FbxSurfaceMaterial::sDiffuse, Factor, DefaultValue, false))
					{
						// Nothing to retrieve from the Diffuse property, get Diffuse directly from the class
						const FbxDouble3 DiffuseColor = ((FbxSurfaceLambert*)SurfaceMaterial)->Diffuse.Get();
						const FString InputValueKey = UInterchangeShaderPortsAPI::MakeInputValueKey(Phong::Parameters::DiffuseColor.ToString());
						ShaderGraphNode->AddLinearColorAttribute(InputValueKey, FFbxConvert::ConvertColor(DiffuseColor));
					}
				}

				// Ambient
				{
					const float Factor = (float)((FbxSurfacePhong*)SurfaceMaterial)->AmbientFactor.Get();
					TVariant<FLinearColor, float> DefaultValue;
					DefaultValue.Set<FLinearColor>(FLinearColor::Black);
					ConnectInput(Phong::Parameters::AmbientColor, FbxSurfaceMaterial::sAmbient, Factor, DefaultValue, false);
				}

				// Emissive
				{
					const float Factor = (float)((FbxSurfaceLambert*)SurfaceMaterial)->EmissiveFactor.Get();
					TVariant<FLinearColor, float> DefaultValue;
					DefaultValue.Set<FLinearColor>(FLinearColor::Black);
					ConnectInput(Phong::Parameters::EmissiveColor, FbxSurfaceMaterial::sEmissive, Factor, DefaultValue, false);
				}

				// Normal
				{
					// FbxSurfaceMaterial can have either a normal map or a bump map, check for both
					float Factor = 1.f;
					FBXSDK_NAMESPACE::FbxProperty MaterialProperty = SurfaceMaterial->FindProperty(FbxSurfaceMaterial::sNormalMap);
					if (MaterialProperty.IsValid() && MaterialProperty.GetSrcObjectCount<FbxTexture>() > 0)
					{
						TVariant<FLinearColor, float> DefaultValue;
						DefaultValue.Set<FLinearColor>(FLinearColor(FVector::UpVector));
						ConvertPropertyToShaderNode(NodeContainer, ShaderGraphNode, MaterialProperty, Factor, Phong::Parameters::Normal, DefaultValue);
					}
					else
					{
						Factor = (float)((FbxSurfaceLambert*)SurfaceMaterial)->BumpFactor.Get();
						MaterialProperty = SurfaceMaterial->FindProperty(FbxSurfaceMaterial::sBump);
						if (MaterialProperty.IsValid() && MaterialProperty.GetSrcObjectCount<FbxTexture>() > 0)
						{
							using namespace Materials::Standard::Nodes;

							if (FbxFileTexture* FbxTexture = MaterialProperty.GetSrcObject<FbxFileTexture>(0))
							{
								const FString TexturePath = FbxTexture->GetFileName();

								if (const UInterchangeTexture2DNode* TextureNode = CreateTexture2DNode(NodeContainer, TexturePath))
								{
									const FString TextureName = FPaths::GetBaseFilename(TexturePath);

									// NormalFromHeightmap needs TextureObject(not just a sample as it takes multiple samples from it)
									UInterchangeShaderNode* TextureObjectNode = UInterchangeShaderNode::Create(&NodeContainer, TextureName, ShaderGraphNode->GetUniqueID());
									TextureObjectNode->SetCustomShaderType(TextureObject::Name.ToString());
									TextureObjectNode->AddStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(TextureObject::Inputs::Texture.ToString()), TextureNode->GetUniqueID());

									UInterchangeShaderNode* HeightMapNode = UInterchangeShaderNode::Create(&NodeContainer, NormalFromHeightMap::Name.ToString(), ShaderGraphNode->GetUniqueID());
									HeightMapNode->SetCustomShaderType(NormalFromHeightMap::Name.ToString());

									UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(HeightMapNode, NormalFromHeightMap::Inputs::HeightMap.ToString(), TextureObjectNode->GetUniqueID());
									HeightMapNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(NormalFromHeightMap::Inputs::Intensity.ToString()), Factor);

									UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ShaderGraphNode, Materials::Common::Parameters::Normal.ToString(), HeightMapNode->GetUniqueID());
								}
							}
						}
					}
				}

				// Opacity
				{
					const float TransparencyFactor = (float)((FbxSurfaceLambert*)SurfaceMaterial)->TransparencyFactor.Get();
					TVariant<FLinearColor, float> DefaultValue;
					DefaultValue.Set<float>(0.f); // Opaque
					ConnectInput(Phong::Parameters::Opacity, FbxSurfaceMaterial::sTransparentColor, TransparencyFactor, DefaultValue, true);
				}

				// Get specular color and shininess if FBX material is a Phong one
				if (SurfaceMaterial->Is<FbxSurfacePhong>())
				{
					// Specular
					{
						const float Factor = (float)((FbxSurfacePhong*)SurfaceMaterial)->SpecularFactor.Get();
						TVariant<FLinearColor, float> DefaultValue;
						DefaultValue.Set<FLinearColor>(FLinearColor::Black);
						ConnectInput(Phong::Parameters::SpecularColor, FbxSurfaceMaterial::sSpecular, Factor, DefaultValue, false);
					}

					// Shininess
					ConvertShininessToShaderNode(*SurfaceMaterial, NodeContainer, ShaderGraphNode);
				}

				return ShaderGraphNode;
			}

			void FFbxMaterial::AddAllTextures(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer)
			{
				int32 TextureCount = SDKScene->GetSrcObjectCount<FbxFileTexture>();
				for (int32 TextureIndex = 0; TextureIndex < TextureCount; ++TextureIndex)
				{
					FbxFileTexture* Texture = SDKScene->GetSrcObject<FbxFileTexture>(TextureIndex);
					FString TextureFilename = UTF8_TO_TCHAR(Texture->GetFileName());
					//Only import texture that exist on disk
					if (!FPaths::FileExists(TextureFilename))
					{
						continue;
					}
					//Create a texture node and make it child of the material node
					const FString TextureName = FPaths::GetBaseFilename(TextureFilename);
					const UInterchangeTexture2DNode* TextureNode = Cast<UInterchangeTexture2DNode>(NodeContainer.GetNode(UInterchangeTextureNode::MakeNodeUid(TextureName)));
					if (!TextureNode)
					{
						CreateTexture2DNode(NodeContainer, TextureFilename);
					}
				}
			}
			
			void FFbxMaterial::AddAllNodeMaterials(UInterchangeSceneNode* SceneNode, FbxNode* ParentFbxNode, UInterchangeBaseNodeContainer& NodeContainer)
			{
				int32 MaterialCount = ParentFbxNode->GetMaterialCount();
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					FbxSurfaceMaterial* SurfaceMaterial = ParentFbxNode->GetMaterial(MaterialIndex);
					const UInterchangeShaderGraphNode* ShaderGraphNode = AddShaderGraphNode(SurfaceMaterial, NodeContainer);
					SceneNode->SetSlotMaterialDependencyUid(Parser.GetFbxHelper()->GetFbxObjectName(SurfaceMaterial), ShaderGraphNode->GetUniqueID());
				}
			}

			void FFbxMaterial::AddAllMaterials(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer)
			{
				int32 MaterialCount = SDKScene->GetMaterialCount();
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					FbxSurfaceMaterial* SurfaceMaterial = SDKScene->GetMaterial(MaterialIndex);
					AddShaderGraphNode(SurfaceMaterial, NodeContainer);
				}
			}
		} //ns Private
	} //ns Interchange
}//ns UE

#undef LOCTEXT_NAMESPACE
