// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxInclude.h"

/** Forward declarations */
class UInterchangeBaseNode;
class UInterchangeBaseNodeContainer;
class UInterchangeMaterialNode;
class UInterchangeTextureNode;

namespace UE
{
	namespace FbxParser
	{
		namespace Private
		{
			class FFbxMaterial
			{
			public:
				static void AddAllNodeMaterials(UInterchangeBaseNode* ParentNode, FbxNode* ParentFbxNode, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages);
				static void AddAllSceneMaterials(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages);
				static UInterchangeMaterialNode* CreateMaterialNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, TArray<FString>& JSonErrorMessages);
				static UInterchangeTextureNode* CreateTexture2DNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& TextureFilePath, TArray<FString>& JSonErrorMessages);
			
			private:
				static UInterchangeMaterialNode* AddNodeMaterial(FbxSurfaceMaterial* SurfaceMaterial, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages);
			};
		}
	}
}
