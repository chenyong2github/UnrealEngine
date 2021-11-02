// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxMaterial.h"

#include "CoreMinimal.h"
#include "FbxAPI.h"
#include "FbxConvert.h"
#include "FbxHelper.h"
#include "FbxInclude.h"
#include "InterchangeMaterialNode.h"
#include "InterchangeResultsContainer.h"
#include "InterchangeSceneNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTextureNode.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "InterchangeFbxMaterial"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			UInterchangeMaterialNode* FFbxMaterial::CreateMaterialNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeUid, const FString& NodeName)
			{
				UInterchangeMaterialNode* MaterialNode = NewObject<UInterchangeMaterialNode>(&NodeContainer, NAME_None);
				if (!ensure(MaterialNode))
				{
					UInterchangeResultError_Generic* Message = Parser.AddMessage<UInterchangeResultError_Generic>();
					Message->Text = LOCTEXT("CannotAllocateNode", "Cannot allocate a node when importing FBX.");
					return nullptr;
				}
				// Creating a UMaterialInterface
				MaterialNode->InitializeNode(NodeUid, NodeName, EInterchangeNodeContainerType::NodeContainerType_TranslatedAsset);
				MaterialNode->SetPayLoadKey(NodeUid);
				NodeContainer.AddNode(MaterialNode);
				return MaterialNode;
			}

			UInterchangeTexture2DNode* FFbxMaterial::CreateTexture2DNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeUid, const FString& TextureFilePath)
			{
				FString DisplayLabel = FPaths::GetBaseFilename(TextureFilePath);
				UInterchangeTexture2DNode* TextureNode = NewObject<UInterchangeTexture2DNode>(&NodeContainer, NAME_None);
				if (!ensure(TextureNode))
				{
					UInterchangeResultError_Generic* Message = Parser.AddMessage<UInterchangeResultError_Generic>();
					Message->Text = LOCTEXT("CannotAllocateNode", "Cannot allocate a node when importing FBX.");
					return nullptr;
				}
				// Creating a UTexture2D
				TextureNode->InitializeNode(NodeUid, DisplayLabel, EInterchangeNodeContainerType::NodeContainerType_TranslatedAsset);
				//All texture translator expect a file has the payload key
				FString NormalizeFilePath = TextureFilePath;
				FPaths::NormalizeFilename(NormalizeFilePath);
				TextureNode->SetPayLoadKey(NormalizeFilePath);
				NodeContainer.AddNode(TextureNode);
				return TextureNode;
			}

			UInterchangeMaterialNode* FFbxMaterial::AddNodeMaterial(FbxSurfaceMaterial* SurfaceMaterial, UInterchangeBaseNodeContainer& NodeContainer)
			{
				//Create a material node
				FString MaterialName = FFbxHelper::GetFbxObjectName(SurfaceMaterial);
				FString NodeUid = TEXT("\\Material\\") + MaterialName;
				UInterchangeMaterialNode* MaterialNode = Cast<UInterchangeMaterialNode>(NodeContainer.GetNode(NodeUid));
				if (!MaterialNode)
				{
					MaterialNode = CreateMaterialNode(NodeContainer, NodeUid, MaterialName);
					if (MaterialNode == nullptr)
					{
						FFormatNamedArguments Args
						{
							{ TEXT("MaterialName"), FText::FromString(MaterialName) }
						};
						UInterchangeResultError_Generic* Message = Parser.AddMessage<UInterchangeResultError_Generic>();
						Message->Text = FText::Format(LOCTEXT("CannotCreateFBXMaterial", "Cannot create FBX material '{MaterialName}'."), Args);
						return nullptr;
					}

					auto SetMaterialParameter = [this, &NodeContainer, &MaterialNode, &SurfaceMaterial, &MaterialName](const char* FbxMaterialProperty, EInterchangeMaterialNodeParameterName MaterialParameterName)
					{
						bool bSetMaterial = false;
						FbxProperty FbxProperty = SurfaceMaterial->FindProperty(FbxMaterialProperty);
						if (FbxProperty.IsValid())
						{
							int32 UnsupportedTextureCount = FbxProperty.GetSrcObjectCount<FbxLayeredTexture>();
							UnsupportedTextureCount += FbxProperty.GetSrcObjectCount<FbxProceduralTexture>();
							int32 TextureCount = FbxProperty.GetSrcObjectCount<FbxFileTexture>();
							bool bFoundValidTexture = false;
							if (UnsupportedTextureCount > 0)
							{
								FFormatNamedArguments Args
								{
									{ TEXT("MaterialName"), FText::FromString(MaterialName) }
								};
								UInterchangeResultWarning_Generic* Message = Parser.AddMessage<UInterchangeResultWarning_Generic>();
								Message->Text = FText::Format(LOCTEXT("TextureTypeNotSupported", "Layered or procedural textures are not supported (material '{MaterialName}')."), Args);
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
									FString NodeUid = TEXT("\\Texture\\") + TextureFilename;
									UInterchangeTexture2DNode* TextureNode = Cast<UInterchangeTexture2DNode>(NodeContainer.GetNode(NodeUid));
									if (!TextureNode)
									{
										TextureNode = CreateTexture2DNode(NodeContainer, NodeUid, TextureFilename);
									}
									// add/find UVSet and set it to the texture, we pass index 0 here, I think pipeline should be able to get the UVIndex channel from the name
									// and modify the parameter to set the correct value.
// 									FbxString UVSetName = FbxTextureFilePath->UVSet.Get();
// 									FString LocalUVSetName = UTF8_TO_TCHAR(UVSetName.Buffer());
									const int32 UVChannelIndex = 0;
									float ScaleU = (float)FbxTextureFilePath->GetScaleU();
									float ScaleV = (float)FbxTextureFilePath->GetScaleV();
									MaterialNode->AddTextureParameterData(MaterialParameterName, TextureNode->GetUniqueID(), UVChannelIndex, ScaleU, ScaleV);
									MaterialNode->SetTextureDependencyUid(TextureNode->GetUniqueID());
									bSetMaterial = true;
									bFoundValidTexture = true;
								}
							}
							
							if (!bFoundValidTexture && MaterialParameterName == EInterchangeMaterialNodeParameterName::BaseColor)
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
					TArray<FString> JsonErrorMessage;
					FString NodeUid = TEXT("\\Texture\\") + TextureFilename;
					UInterchangeTexture2DNode* TextureNode = Cast<UInterchangeTexture2DNode>(NodeContainer.GetNode(NodeUid));
					if (!TextureNode)
					{
						TextureNode = CreateTexture2DNode(NodeContainer, NodeUid, TextureFilename);
					}
				}
			}
			
			void FFbxMaterial::AddAllNodeMaterials(UInterchangeSceneNode* SceneNode, FbxNode* ParentFbxNode, UInterchangeBaseNodeContainer& NodeContainer)
			{
				int32 MaterialCount = ParentFbxNode->GetMaterialCount();
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					FbxSurfaceMaterial* SurfaceMaterial = ParentFbxNode->GetMaterial(MaterialIndex);
					UInterchangeMaterialNode* MaterialNode = AddNodeMaterial(SurfaceMaterial, NodeContainer);
					//The dependencies order is important because mesh will use index in that order to determine material use by a face
					SceneNode->AddMaterialDependencyUid(MaterialNode->GetUniqueID());
				}
			}

			void FFbxMaterial::AddAllMaterials(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer)
			{
				int32 MaterialCount = SDKScene->GetMaterialCount();
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					FbxSurfaceMaterial* SurfaceMaterial = SDKScene->GetMaterial(MaterialIndex);
					AddNodeMaterial(SurfaceMaterial, NodeContainer);
				}
			}
		} //ns Private
	} //ns Interchange
}//ns UE

#undef LOCTEXT_NAMESPACE
