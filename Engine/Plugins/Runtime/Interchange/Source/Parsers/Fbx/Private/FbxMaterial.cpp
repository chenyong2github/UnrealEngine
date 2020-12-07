// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxMaterial.h"

#include "CoreMinimal.h"
#include "FbxConvert.h"
#include "FbxHelper.h"
#include "FbxInclude.h"
#include "InterchangeMaterialNode.h"
#include "InterchangeTextureNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			UInterchangeMaterialNode* FFbxMaterial::CreateMaterialNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, TArray<FString>& JSonErrorMessages)
			{
				FName NodeUID(*NodeName);
				FString MaterialNameNoSkin = UE::Interchange::Material::RemoveSkinFromName(NodeName);

				FName DisplayLabel = *MaterialNameNoSkin;
				UInterchangeMaterialNode* MaterialNode = NewObject<UInterchangeMaterialNode>(&NodeContainer, NAME_None);
				if (!ensure(MaterialNode))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot allocate a node when importing fbx\"}}"));
					return nullptr;
				}
				// Creating a UMaterialInterface
				MaterialNode->InitializeMaterialNode(NodeUID, DisplayLabel, TEXT("Material"));
				MaterialNode->SetPayLoadKey(NodeName);
				NodeContainer.AddNode(MaterialNode);
				return MaterialNode;
			}

			UInterchangeTextureNode* FFbxMaterial::CreateTexture2DNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& TextureFilePath, TArray<FString>& JSonErrorMessages)
			{
				FName DisplayLabel = *FPaths::GetBaseFilename(TextureFilePath);
				FName NodeUID(*TextureFilePath);
				UInterchangeTextureNode* TextureNode = NewObject<UInterchangeTextureNode>(&NodeContainer, NAME_None);
				if (!ensure(TextureNode))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot allocate a node when importing fbx\"}}"));
					return nullptr;
				}
				// Creating a UTexture2D
				TextureNode->InitializeTextureNode(NodeUID, DisplayLabel, TEXT("Texture2D"));
				TextureNode->SetPayLoadKey(TextureFilePath);
				NodeContainer.AddNode(TextureNode);
				return TextureNode;
			}

			UInterchangeMaterialNode* FFbxMaterial::AddNodeMaterial(FbxSurfaceMaterial* SurfaceMaterial, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages)
			{
				//Create a material node
				FString MaterialName = FFbxHelper::GetFbxObjectName(SurfaceMaterial);
				FName NodeUID(*MaterialName);
				UInterchangeMaterialNode* MaterialNode = Cast<UInterchangeMaterialNode>(NodeContainer.GetNode(NodeUID));
				if (!MaterialNode)
				{
					MaterialNode = CreateMaterialNode(NodeContainer, MaterialName, JSonErrorMessages);
					if (MaterialNode == nullptr)
					{
						JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot create fbx material (" + MaterialName + TEXT(")\"}}")));
						return nullptr;
					}

					auto SetMaterialParameter = [&NodeContainer, &MaterialNode, &SurfaceMaterial, &JSonErrorMessages, &MaterialName](const char* FbxMaterialProperty, EInterchangeMaterialNodeParameterName MaterialParameterName)
					{
						bool bSetMaterial = false;
						FbxProperty FbxProperty = SurfaceMaterial->FindProperty(FbxMaterialProperty);
						if (FbxProperty.IsValid())
						{
							int32 UnsupportedTextureCount = FbxProperty.GetSrcObjectCount<FbxLayeredTexture>();
							UnsupportedTextureCount += FbxProperty.GetSrcObjectCount<FbxProceduralTexture>();
							int32 TextureCount = FbxProperty.GetSrcObjectCount<FbxTexture>();
							if (UnsupportedTextureCount > 0)
							{
								JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Warning\",\n\"Msg\" : \"Layered or procedural Textures are not supported (material" + MaterialName + TEXT(")\"}}")));
							}
							else if (TextureCount > 0)
							{
								for (int32 TextureIndex = 0; TextureIndex < TextureCount; ++TextureIndex)
								{
									FbxFileTexture* FbxTextureFilePath = FbxProperty.GetSrcObject<FbxFileTexture>(TextureIndex);
									FString TextureFilename = UTF8_TO_TCHAR(FbxTextureFilePath->GetFileName());
									//Only import texture that exist on disk
									if (!FPaths::FileExists(TextureFilename))
									{
										continue;
									}
									//Create a texture node and make it child of the material node
									TArray<FString> JsonErrorMessage;
									FName NodeUID(*TextureFilename);
									UInterchangeTextureNode* TextureNode = Cast<UInterchangeTextureNode>(NodeContainer.GetNode(NodeUID));
									if (!TextureNode)
									{
										TextureNode = CreateTexture2DNode(NodeContainer, TextureFilename, JsonErrorMessage);
									}
									// add/find UVSet and set it to the texture, we pass index 0 here, I think pipeline should be able to get the UVIndex channel from the name
									// and modify the parameter to set the correct value.
// 									FbxString UVSetName = FbxTextureFilePath->UVSet.Get();
// 									FString LocalUVSetName = UTF8_TO_TCHAR(UVSetName.Buffer());
									const int32 UVChannelIndex = 0;
									float ScaleU = (float)FbxTextureFilePath->GetScaleU();
									float ScaleV = (float)FbxTextureFilePath->GetScaleV();
									MaterialNode->AddTextureParameterData(MaterialParameterName, TextureNode->GetUniqueID(), UVChannelIndex, ScaleU, ScaleV);
									MaterialNode->SetDependencyUID(TextureNode->GetUniqueID());
									bSetMaterial = true;
								}
							}
							else if (MaterialParameterName == EInterchangeMaterialNodeParameterName::BaseColor)
							{
								//We support only the base color has a vector color
								//TODO support all basic attributes has vector or scalar
								FbxDouble3 FbxColor;
								bool bFoundColor = true;
								if (SurfaceMaterial->GetClassId().Is(FbxSurfacePhong::ClassId))
								{
									FbxColor = ((FbxSurfacePhong&)(*SurfaceMaterial)).Diffuse.Get();
								}
								else if (SurfaceMaterial->GetClassId().Is(FbxSurfaceLambert::ClassId))
								{
									FbxColor = ((FbxSurfaceLambert&)(*SurfaceMaterial)).Diffuse.Get();
								}
								else
								{
									bFoundColor = false;
								}

								FVector ColorData;
								if (bFoundColor)
								{
									ColorData[0] = (float)(FbxColor[0]);
									ColorData[1] = (float)(FbxColor[1]);
									ColorData[2] = (float)(FbxColor[2]);
								}
								else
								{
									// use random color because there may be multiple materials, then they can be different 
									ColorData[0] = 0.5f + (0.5f * FMath::Rand()) / static_cast<float>(RAND_MAX);
									ColorData[1] = 0.5f + (0.5f * FMath::Rand()) / static_cast<float>(RAND_MAX);
									ColorData[2] = 0.5f + (0.5f * FMath::Rand()) / static_cast<float>(RAND_MAX);
								}
								MaterialNode->AddVectorParameterData(MaterialParameterName, ColorData);
								bSetMaterial = true;
							}
						}
						return bSetMaterial;
					};

					SetMaterialParameter(FbxSurfaceMaterial::sDiffuse, EInterchangeMaterialNodeParameterName::BaseColor);
					SetMaterialParameter(FbxSurfaceMaterial::sEmissive, EInterchangeMaterialNodeParameterName::EmissiveColor);
					SetMaterialParameter(FbxSurfaceMaterial::sSpecular, EInterchangeMaterialNodeParameterName::Specular);
					SetMaterialParameter(FbxSurfaceMaterial::sSpecularFactor, EInterchangeMaterialNodeParameterName::Roughness);
					SetMaterialParameter(FbxSurfaceMaterial::sShininess, EInterchangeMaterialNodeParameterName::Metallic);
					if (!SetMaterialParameter(FbxSurfaceMaterial::sNormalMap, EInterchangeMaterialNodeParameterName::Normal))
					{
						SetMaterialParameter(FbxSurfaceMaterial::sBump, EInterchangeMaterialNodeParameterName::Normal);
					}
					if (SetMaterialParameter(FbxSurfaceMaterial::sTransparentColor, EInterchangeMaterialNodeParameterName::Opacity))
					{
						SetMaterialParameter(FbxSurfaceMaterial::sTransparencyFactor, EInterchangeMaterialNodeParameterName::OpacityMask);
					}
				}

				return MaterialNode;
			}

			void FFbxMaterial::AddAllNodeMaterials(UInterchangeBaseNode* UnrealNode, FbxNode* ParentFbxNode, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages)
			{
				int32 MaterialCount = ParentFbxNode->GetMaterialCount();
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					FbxSurfaceMaterial* SurfaceMaterial = ParentFbxNode->GetMaterial(MaterialIndex);
					UInterchangeMaterialNode* MaterialNode = AddNodeMaterial(SurfaceMaterial, NodeContainer, JSonErrorMessages);
					//The dependencies order is important because mesh will use index in that order to determine material use by a face
					UnrealNode->SetDependencyUID(MaterialNode->GetUniqueID());
				}
			}

			void FFbxMaterial::AddAllSceneMaterials(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages)
			{
				int32 NodeMaterialCount = SDKScene->GetMaterialCount();
				for (int32 NodeMaterialIndex = 0; NodeMaterialIndex < NodeMaterialCount; ++NodeMaterialIndex)
				{
					FbxSurfaceMaterial* SurfaceMaterial = SDKScene->GetMaterial(NodeMaterialIndex);
					AddNodeMaterial(SurfaceMaterial, NodeContainer, JSonErrorMessages);
				}
			}
		} //ns Private
	} //ns Interchange
}//ns UE
