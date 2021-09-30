// Copyright Epic Games, Inc. All Rights Reserved. 

#include "InterchangeGenericAssetsPipeline.h"

#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureLightProfile.h"
#include "InterchangeTexture2DArrayFactoryNode.h"
#include "InterchangeTexture2DArrayFactoryNode.h"
#include "InterchangeTexture2DArrayNode.h"
#include "InterchangeTexture2DArrayNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTextureCubeFactoryNode.h"
#include "InterchangeTextureCubeFactoryNode.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeTextureLightProfileFactoryNode.h"
#include "InterchangeTextureLightProfileFactoryNode.h"
#include "InterchangeTextureLightProfileNode.h"
#include "InterchangeTextureLightProfileNode.h"
#include "InterchangeTextureNode.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "NormalMapIdentification.h"
#include "TextureCompiler.h"
#endif //WITH_EDITOR

namespace UE::Interchange::Private
{
	UClass* GetDefaultFactoryClassFromTextureNodeClass(UClass* NodeClass)
	{
		if (UInterchangeTexture2DNode::StaticClass() == NodeClass)
		{
			return UInterchangeTextureFactoryNode::StaticClass();
		}

		if (UInterchangeTextureCubeNode::StaticClass() == NodeClass)
		{
			return UInterchangeTextureCubeFactoryNode::StaticClass();
		}

		if (UInterchangeTexture2DArrayNode::StaticClass() == NodeClass)
		{
			return UInterchangeTexture2DArrayFactoryNode::StaticClass();
		}

		if (UInterchangeTextureLightProfileNode::StaticClass() == NodeClass)
		{
			return UInterchangeTextureLightProfileFactoryNode::StaticClass();
		}

		return nullptr;
	}

	UClass* GetDefaultAssetClassFromFactoryClass(UClass* NodeClass)
	{
		if (UInterchangeTextureFactoryNode::StaticClass() == NodeClass)
		{
			return UTexture2D::StaticClass();
		}

		if (UInterchangeTextureCubeFactoryNode::StaticClass() == NodeClass)
		{
			return UTextureCube::StaticClass();
		}

		if (UInterchangeTexture2DArrayFactoryNode::StaticClass() == NodeClass)
		{
			return UTexture2DArray::StaticClass();
		}

		if (UInterchangeTextureLightProfileFactoryNode::StaticClass() == NodeClass)
		{
			return UTextureLightProfile::StaticClass();
		}

		return nullptr;
	}

#if WITH_EDITOR
	void AdjustTextureForNormalMap(UTexture* Texture, bool bFlipNormalMapGreenChannel)
	{
		if (Texture)
		{
			Texture->PreEditChange(nullptr);
			if (UE::NormalMapIdentification::HandleAssetPostImport(Texture))
			{
				if (bFlipNormalMapGreenChannel)
				{
					Texture->bFlipGreenChannel = true;
				}
			}
			Texture->PostEditChange();
		}
	}
#endif
}

UInterchangeTextureFactoryNode* UInterchangeGenericAssetsPipeline::HandleCreationOfTextureFactoryNode(const UInterchangeTextureNode* TextureNode)
{
	UClass* FactoryClass = UE::Interchange::Private::GetDefaultFactoryClassFromTextureNodeClass(TextureNode->GetClass());

#if WITH_EDITORONLY_DATA
	if (FactoryClass == UInterchangeTextureFactoryNode::StaticClass())
	{ 
		if (TOptional<FString> SourceFile = TextureNode->GetPayLoadKey())
		{
			const FString Extension = FPaths::GetExtension(SourceFile.GetValue()).ToLower();
			if (FileExtensionsToImportAsLongLatCubemap.Contains(Extension))
			{
				FactoryClass = UInterchangeTextureCubeFactoryNode::StaticClass();
			}
		}
	}
#endif

	return CreateTextureFactoryNode(TextureNode, FactoryClass);
}

UInterchangeTextureFactoryNode* UInterchangeGenericAssetsPipeline::CreateTextureFactoryNode(const UInterchangeTextureNode* TextureNode, const TSubclassOf<UInterchangeTextureFactoryNode>& FactorySubclass)
{
	FString DisplayLabel = TextureNode->GetDisplayLabel();
	FString NodeUid = UInterchangeTextureFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(TextureNode->GetUniqueID());
	UInterchangeTextureFactoryNode* TextureFactoryNode = nullptr;
	if (BaseNodeContainer->IsNodeUidValid(NodeUid))
	{
		TextureFactoryNode = Cast<UInterchangeTextureFactoryNode>(BaseNodeContainer->GetNode(NodeUid));
		if (!ensure(TextureFactoryNode))
		{
			//Log an error
		}
	}
	else
	{
		UClass* FactoryClass = FactorySubclass.Get();
		if (!ensure(FactoryClass))
		{
			// Log an error
			return nullptr;
		}

		UClass* TextureClass = UE::Interchange::Private::GetDefaultAssetClassFromFactoryClass(FactoryClass);
		if (!ensure(TextureClass))
		{
			// Log an error
			return nullptr;
		}

		TextureFactoryNode = NewObject<UInterchangeTextureFactoryNode>(BaseNodeContainer, FactoryClass);
		if (!ensure(TextureFactoryNode))
		{
			return nullptr;
		}
		//Creating a UTexture2D
		TextureFactoryNode->InitializeTextureNode(NodeUid, DisplayLabel, TextureClass->GetName(), TextureNode->GetDisplayLabel());
		TextureFactoryNode->SetCustomTranslatedTextureNodeUid(TextureNode->GetUniqueID());
		BaseNodeContainer->AddNode(TextureFactoryNode);
		TextureFactoryNodes.Add(TextureFactoryNode);
	}
	return TextureFactoryNode;
}

void UInterchangeGenericAssetsPipeline::PostImportTextureAssetImport(UObject* CreatedAsset, bool bIsAReimport)
{
#if WITH_EDITOR
	if (!bIsAReimport && bDetectNormalMapTexture)
	{
		// Verify if the texture is a normal map
		if (UTexture* Texture = Cast<UTexture>(CreatedAsset))
		{
			// This can create 2 build of the texture (we should revisit this at some point)
			if (FTextureCompilingManager::Get().IsCompilingTexture(Texture))
			{
				TWeakObjectPtr<UTexture> WeakTexturePtr = Texture;
				TSharedRef<FDelegateHandle> HandlePtr = MakeShared<FDelegateHandle>();
				HandlePtr.Get() = FTextureCompilingManager::Get().OnTexturePostCompileEvent().AddLambda([this, WeakTexturePtr, HandlePtr](const TArrayView<UTexture* const>&)
					{
						if (UTexture* TextureToTest = WeakTexturePtr.Get())
						{
							if (FTextureCompilingManager::Get().IsCompilingTexture(TextureToTest))
							{
								return;
							}

							UE::Interchange::Private::AdjustTextureForNormalMap(TextureToTest, bFlipNormalMapGreenChannel);
						}

						FTextureCompilingManager::Get().OnTexturePostCompileEvent().Remove(HandlePtr.Get());
					});
			}
			else
			{
				UE::Interchange::Private::AdjustTextureForNormalMap(Texture, bFlipNormalMapGreenChannel);
			}
		}
	}
#endif //WITH_EDITOR
}

