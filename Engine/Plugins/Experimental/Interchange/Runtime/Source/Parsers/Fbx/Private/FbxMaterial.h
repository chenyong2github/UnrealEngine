// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxInclude.h"

/** Forward declarations */
class UInterchangeBaseNode;
class UInterchangeBaseNodeContainer;
class UInterchangeMaterialNode;
class UInterchangeTexture2DNode;
class UInterchangeSceneNode;

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FFbxParser;

			class FFbxMaterial
			{
			public:
				explicit FFbxMaterial(FFbxParser& InParser)
					: Parser(InParser)
				{}

				/**
				 * Create a UInterchangeFextureNode and add it to the NodeContainer for all texture of type FbxFileTexture the fbx file contain.
				 *
				 * @note - Any node that already exist in the NodeContainer will not be created or modify.
				 */
				void AddAllTextures(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer);
				
				/**
				 * Create a UInterchangeMaterialNode and add it to the NodeContainer for all material of type FbxSurfaceMaterial the fbx file contain.
				 * 
				 * @note - Any node that already exist in the NodeContainer will not be created or modify.
				 */
				void AddAllMaterials(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer);

				/**
				 * Create a UInterchangeMaterialNode and add it to the NodeContainer for all material of type FbxSurfaceMaterial the fbx ParentFbxNode contain.
				 * It also set the dependencies of the node materials on the interchange ParentNode.
				 * 
				 * @note - Any material node that already exist in the NodeContainer will simply be add has a dependency.
				 */
				void AddAllNodeMaterials(UInterchangeSceneNode* SceneNode, FbxNode* ParentFbxNode, UInterchangeBaseNodeContainer& NodeContainer);

			protected:
				UInterchangeMaterialNode* CreateMaterialNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeUID, const FString& NodeName);
				UInterchangeTexture2DNode* CreateTexture2DNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeUID, const FString& TextureFilePath);

			private:
				UInterchangeMaterialNode* AddNodeMaterial(FbxSurfaceMaterial* SurfaceMaterial, UInterchangeBaseNodeContainer& NodeContainer);

				FFbxParser& Parser;
			};
		}//ns Private
	}//ns Interchange
}//ns UE
