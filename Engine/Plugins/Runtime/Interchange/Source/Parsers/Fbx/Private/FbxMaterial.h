// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxInclude.h"

/** Forward declarations */
class UInterchangeBaseNode;
class UInterchangeBaseNodeContainer;
class UInterchangeMaterialNode;
class UInterchangeTextureNode;
class UInterchangeSceneNode;

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FFbxMaterial
			{
			public:
				/**
				 * Create a UInterchangeFextureNode and add it to the NodeContainer for all texture of type FbxFileTexture the fbx file contain.
				 *
				 * @note - Any node that already exist in the NodeContainer will not be created or modify.
				 */
				static void AddAllTextures(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages);
				
				/**
				 * Create a UInterchangeMaterialNode and add it to the NodeContainer for all material of type FbxSurfaceMaterial the fbx file contain.
				 * 
				 * @note - Any node that already exist in the NodeContainer will not be created or modify.
				 */
				static void AddAllMaterials(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages);

				/**
				 * Create a UInterchangeMaterialNode and add it to the NodeContainer for all material of type FbxSurfaceMaterial the fbx ParentFbxNode contain.
				 * It also set the dependencies of the node materials on the interchange ParentNode.
				 * 
				 * @note - Any material node that already exist in the NodeContainer will simply be add has a dependency.
				 */
				static void AddAllNodeMaterials(UInterchangeSceneNode* SceneNode, FbxNode* ParentFbxNode, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages);
			protected:
				static UInterchangeMaterialNode* CreateMaterialNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeUID, const FString& NodeName, TArray<FString>& JSonErrorMessages);
				static UInterchangeTextureNode* CreateTexture2DNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeUID, const FString& TextureFilePath, TArray<FString>& JSonErrorMessages);

			private:
				static UInterchangeMaterialNode* AddNodeMaterial(FbxSurfaceMaterial* SurfaceMaterial, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages);
			};
		}//ns Private
	}//ns Interchange
}//ns UE
