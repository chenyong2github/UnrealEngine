// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Material/InterchangeMaterialFactory.h"

#include "InterchangeImportCommon.h"
#include "InterchangeMaterialNode.h"
#include "InterchangeSourceData.h"
#include "LogInterchangeImportPlugin.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"


#if WITH_EDITORONLY_DATA

#include "EditorFramework/AssetImportData.h"

#endif //WITH_EDITORONLY_DATA

UClass* UInterchangeMaterialFactory::GetFactoryClass() const
{
	return UMaterialInterface::StaticClass();
}

UObject* UInterchangeMaterialFactory::CreateEmptyAsset(const FCreateAssetParams& Arguments) const
{
	UObject* Material = nullptr;

#if WITH_EDITORONLY_DATA
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetAssetClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeMaterialNode* MaterialNode = Cast<UInterchangeMaterialNode>(Arguments.AssetNode);
	if (MaterialNode == nullptr)
	{
		return nullptr;
	}

	const UClass* MaterialClass = MaterialNode->GetAssetClass();
	if (!ensure(MaterialClass && MaterialClass->IsChildOf(GetFactoryClass())))
	{
		return nullptr;
	}

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		Material = NewObject<UObject>(Arguments.Parent, MaterialClass, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if (ExistingAsset->GetClass()->IsChildOf(MaterialClass))
	{
		//This is a reimport, we are just re-updating the source data
		Material = ExistingAsset;
	}

	if (!Material)
	{
		UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Could not create Material asset %s"), *Arguments.AssetName);
		return nullptr;
	}
	Material->PreEditChange(nullptr);
#endif //WITH_EDITORONLY_DATA
	return Material;
}

UObject* UInterchangeMaterialFactory::CreateAsset(const UInterchangeMaterialFactory::FCreateAssetParams& Arguments) const
{
#if !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Cannot import Material asset in runtime, this is an editor only feature."));
	return nullptr;

#else

	if (!Arguments.AssetNode || !Arguments.AssetNode->GetAssetClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeMaterialNode* MaterialNode = Cast<UInterchangeMaterialNode>(Arguments.AssetNode);
	if (MaterialNode == nullptr)
	{
		return nullptr;
	}

	const UClass* MaterialClass = MaterialNode->GetAssetClass();
	check(MaterialClass && MaterialClass->IsChildOf(GetFactoryClass()));

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	UObject* MaterialObject = nullptr;
	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		//NewObject is not thread safe, the asset registry directory watcher tick on the main thread can trig before we finish initializing the UObject and will crash
		//The UObject should have been create by calling CreateEmptyAsset on the main thread.
		check(IsInGameThread());
		MaterialObject = NewObject<UObject>(Arguments.Parent, MaterialClass, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if(ExistingAsset->GetClass()->IsChildOf(MaterialClass))
	{
		//This is a reimport, we are just re-updating the source data
		MaterialObject = ExistingAsset;
	}

	if (!MaterialObject)
	{
		UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Could not create Material asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	if (MaterialObject)
	{
		//Currently material re-import will not touch the material at all
		//TODO design a re-import process for the material (expressions and input connections)
		if(!Arguments.ReimportObject)
		{
			UMaterial* Material = Cast<UMaterial>(MaterialObject);
			
			if (Material)
			{
				auto ApplyInputParameter = [&MaterialNode, &Material, &Arguments](EInterchangeMaterialNodeParameterName ParameterName, FExpressionInput& MaterialInput, const FVector2D& Location)
				{
					FName OutTextureUID = NAME_None;
					int32 OutUVSetIndex = 0;
					float ScaleU = 0.0f;
					float ScaleV = 0.0f;
					FVector OutVectorParameter = FVector(0.0f);
					float OutFloatParameter = 0.0f;
					if (MaterialNode->GetTextureParameterData(ParameterName, OutTextureUID, OutUVSetIndex, ScaleU, ScaleV))
					{
						UTexture* TextureReference = nullptr;
						if (Arguments.NodeContainer)
						{
							FName NodeUniqueID = MaterialNode->GetUniqueID();
							TArray<FName> MaterialDependencies;
							MaterialNode->GetDependecies(MaterialDependencies);
							int32 DependenciesCount = MaterialDependencies.Num();
							for (int32 DependIndex = 0; DependIndex < DependenciesCount; ++DependIndex)
							{
								const UInterchangeBaseNode* DepNode = Arguments.NodeContainer->GetNode(MaterialDependencies[DependIndex]);
								if (DepNode && DepNode->GetUniqueID() == OutTextureUID && DepNode->ReferenceObject.IsAsset())
								{
									//Use Resolve object so we just look for in memory UObject
									UObject* TextureObject = DepNode->ReferenceObject.ResolveObject();
									if (TextureObject)
									{
										TextureReference = Cast<UTexture>(TextureObject);
										break;
									}
								}
							}
						}

						if (!TextureReference)
						{
							//TODO: Try to load the texture by extracting an asset name from the filename
						}


						if (TextureReference)
						{
							//Set the input to use this texture
							// and link it to the material 
							UMaterialExpressionTextureSample* UnrealTextureExpression = NewObject<UMaterialExpressionTextureSample>(Material);
							Material->Expressions.Add(UnrealTextureExpression);
							MaterialInput.Expression = UnrealTextureExpression;
							UnrealTextureExpression->Texture = TextureReference;
							// 						UnrealTextureExpression->SamplerType = bSetupAsNormalMap ?
							// 							(bIsVirtualTexture ? SAMPLERTYPE_VirtualNormal : SAMPLERTYPE_Normal) :
							// 							(bIsVirtualTexture ? SAMPLERTYPE_VirtualColor : SAMPLERTYPE_Color);

							UnrealTextureExpression->MaterialExpressionEditorX = FMath::TruncToInt(Location.X);
							UnrealTextureExpression->MaterialExpressionEditorY = FMath::TruncToInt(Location.Y);

							//////////////////////////////////////////////////////////////////////////
							//TODO
							//This UV set come from fbx we have to find a way to have more context here, the UV index has to be set when creating the node
							//The name is not suffisant, we need an index

							// add/find UVSet and set it to the texture

							if ((OutUVSetIndex != 0 && OutUVSetIndex != INDEX_NONE) || ScaleU != 1.0f || ScaleV != 1.0f)
							{
								// Create a texture coord node for the texture sample
								UMaterialExpressionTextureCoordinate* MyCoordExpression = NewObject<UMaterialExpressionTextureCoordinate>(Material);
								Material->Expressions.Add(MyCoordExpression);
								MyCoordExpression->CoordinateIndex = (OutUVSetIndex >= 0) ? OutUVSetIndex : 0;
								MyCoordExpression->UTiling = ScaleU;
								MyCoordExpression->VTiling = ScaleV;
								UnrealTextureExpression->Coordinates.Expression = MyCoordExpression;
								MyCoordExpression->MaterialExpressionEditorX = FMath::TruncToInt(Location.X - 175);
								MyCoordExpression->MaterialExpressionEditorY = FMath::TruncToInt(Location.Y);

							}
						}
					}
					else if (MaterialNode->GetVectorParameterData(ParameterName, OutVectorParameter))
					{
						UMaterialExpressionVectorParameter* MyColorExpression = NewObject<UMaterialExpressionVectorParameter>(Material);
						Material->Expressions.Add( MyColorExpression );
						MaterialInput.Expression = MyColorExpression;

						MyColorExpression->DefaultValue.R = OutVectorParameter[0];
						MyColorExpression->DefaultValue.G = OutVectorParameter[1];
						MyColorExpression->DefaultValue.B = OutVectorParameter[2];

						MyColorExpression->MaterialExpressionEditorX = FMath::TruncToInt(Location.X);
						MyColorExpression->MaterialExpressionEditorY = FMath::TruncToInt(Location.Y);
					}
					else if (MaterialNode->GetScalarParameterData(ParameterName, OutFloatParameter))
					{
						UMaterialExpressionScalarParameter* MyScalarExpression = NewObject<UMaterialExpressionScalarParameter>(Material);
						Material->Expressions.Add(MyScalarExpression);
						MaterialInput.Expression = MyScalarExpression;

						MyScalarExpression->DefaultValue = OutFloatParameter;

						MyScalarExpression->MaterialExpressionEditorX = FMath::TruncToInt(Location.X);
						MyScalarExpression->MaterialExpressionEditorY = FMath::TruncToInt(Location.Y);
					}
					
					bool bResult = false;
					if (MaterialInput.Expression)
					{
						TArray<FExpressionOutput> Outputs = MaterialInput.Expression->GetOutputs();
						FExpressionOutput* Output = Outputs.GetData();
						MaterialInput.Mask = Output->Mask;
						MaterialInput.MaskR = Output->MaskR;
						MaterialInput.MaskG = Output->MaskG;
						MaterialInput.MaskB = Output->MaskB;
						MaterialInput.MaskA = Output->MaskA;
						//If we create an expression we return true
						bResult = true;
					}
					return bResult;
				};

				ApplyInputParameter(EInterchangeMaterialNodeParameterName::BaseColor, Material->BaseColor, FVector2D(-250, -100));
				ApplyInputParameter(EInterchangeMaterialNodeParameterName::Metallic, Material->Metallic, FVector2D(-750, 0));
				ApplyInputParameter(EInterchangeMaterialNodeParameterName::Specular, Material->Specular, FVector2D(-500, 100));
				ApplyInputParameter(EInterchangeMaterialNodeParameterName::Roughness, Material->Roughness, FVector2D(-250, 200));
				ApplyInputParameter(EInterchangeMaterialNodeParameterName::EmissiveColor, Material->EmissiveColor, FVector2D(-750, 300));
				if (ApplyInputParameter(EInterchangeMaterialNodeParameterName::Opacity, Material->Opacity, FVector2D(-500, 400)))
				{
					Material->BlendMode = BLEND_Translucent;
				}
				ApplyInputParameter(EInterchangeMaterialNodeParameterName::OpacityMask, Material->OpacityMask, FVector2D(-250, 500));
				ApplyInputParameter(EInterchangeMaterialNodeParameterName::Normal, Material->Normal, FVector2D(-750, 600));
			}

			/** Apply all MaterialNode custom attributes to the material asset */
			MaterialNode->ApplyAllCustomAttributeToAsset(Material);

			//TODO support material instance
		}
		
		//Getting the file Hash will cache it into the source data
		Arguments.SourceData->GetFileContentHash();

		//The interchange completion task (call in the GameThread after the factories pass), will call PostEditChange which will trig another asynchronous system that will build all material in parallel
	}
	else
	{
		//The material is not a UMaterialInterface
		MaterialObject->RemoveFromRoot();
		MaterialObject->MarkPendingKill();
	}
	return MaterialObject;
#endif
}

/* This function is call in the completion task on the main thread, use it to call main thread post creation step for your assets*/
void UInterchangeMaterialFactory::PostImportGameThreadCallback(const FPostImportGameThreadCallbackParams& Arguments) const
{
	check(IsInGameThread());
	Super::PostImportGameThreadCallback(Arguments);

	//TODO make sure this work at runtime
#if WITH_EDITORONLY_DATA
	if (ensure(Arguments.ImportedObject && Arguments.SourceData))
	{
		//We must call the Update of the asset source file in the main thread because UAssetImportData::Update execute some delegate we do not control
		UMaterialInterface* ImportedMaterial = CastChecked<UMaterialInterface>(Arguments.ImportedObject);

		UE::Interchange::FFactoryCommon::FUpdateImportAssetDataParameters UpdateImportAssetDataParameters(ImportedMaterial
																										  , &ImportedMaterial->AssetImportData
																										  , Arguments.SourceData
																										  , Arguments.NodeUniqueID
																										  , Arguments.NodeContainer);
		UE::Interchange::FFactoryCommon::UpdateImportAssetData(UpdateImportAssetDataParameters);
	}
#endif
}