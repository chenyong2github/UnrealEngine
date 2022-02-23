// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeGenericTexturePipeline.generated.h"

class UInterchangeTextureFactoryNode;
class UInterchangeTextureNode;

UCLASS(BlueprintType, Experimental)
class INTERCHANGEPIPELINES_API UInterchangeGenericTexturePipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:
	/** If enabled, imports the texture assets found in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Textures")
	bool bImportTextures = true;

#if WITH_EDITORONLY_DATA
	/** 
	 * If enable, after a new import a test will be run to see if the texture is a normal map.
	 * If the texture is a normal map the SRG, CompressionSettings and LODGroup settings will be adjusted.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Textures")
	bool bDetectNormalMapTexture = true;

	/** If enabled, the texture's green channel will be inverted for normal maps. */
	UPROPERTY(EditAnywhere, Category = "Textures")
	bool bFlipNormalMapGreenChannel = false;

	/** If enabled detect if a texture use a UDIM pattern and if so import it as UIDMs. */
	UPROPERTY(EditAnywhere, Category = "Textures")
	bool bImportUDIMs = true;

	/** Specify the files type that should be imported as long/lat cubemap */
	UPROPERTY(EditAnywhere, Category = "Textures")
	TSet<FString> FileExtensionsToImportAsLongLatCubemap = {"hdr"};
#endif

public:
	virtual void ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas) override;
	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* InBaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;

protected:
	UInterchangeTextureFactoryNode* HandleCreationOfTextureFactoryNode(const UInterchangeTextureNode* TextureNode);

	UInterchangeTextureFactoryNode* CreateTextureFactoryNode(const UInterchangeTextureNode* TextureNode, const TSubclassOf<UInterchangeTextureFactoryNode>& FactorySubclass);

	void PostImportTextureAssetImport(UObject* CreatedAsset, bool bIsAReimport);

private:
	UPROPERTY()
	TObjectPtr<UInterchangeBaseNodeContainer> BaseNodeContainer;

	TArray<const UInterchangeSourceData*> SourceDatas;
	
	/** Texture translated assets nodes */
	TArray<UInterchangeTextureNode*> TextureNodes;

	/** Texture factory assets nodes */
	TArray<UInterchangeTextureFactoryNode*> TextureFactoryNodes;
};


