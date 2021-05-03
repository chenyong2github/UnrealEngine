// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxHelper.h"
#include "FbxInclude.h"

/** Forward declarations */
struct FMeshDescription;
class UInterchangeBaseNodeContainer;
class UInterchangeMeshNode;

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FMeshDescriptionImporter
			{
			public:
				FMeshDescriptionImporter(FMeshDescription* InMeshDescription, FbxScene* InSDKScene, FbxGeometryConverter* InSDKGeometryConverter);
				
				/*
				 * Fill the mesh description using the Mesh parameter.
				 */
				bool FillStaticMeshDescriptionFromFbxMesh(FbxMesh* Mesh);
				
				/*
				 * Fill the mesh description using the Mesh parameter and also fill the OutJointNodeUniqueIDs so the MeshDescription bone Index can be map to the correct interchange joint scene node.
				 */
				bool FillSkinnedMeshDescriptionFromFbxMesh(FbxMesh* Mesh, TArray<FString>& OutJointUniqueNames);

				/*
				 * Fill the mesh description using the Shape parameter.
				 */
				bool FillMeshDescriptionFromFbxShape(FbxShape* Shape);

			private:
				
				enum class EMeshType : uint8
				{
					None = 0, //No mesh type to import
					Static = 1, //static mesh
					Skinned = 2, //skinned mesh with joints
				};

				bool FillMeshDescriptionFromFbxMesh(FbxMesh* Mesh, TArray<FString>& OutJointUniqueNames, EMeshType MeshType);
				bool IsOddNegativeScale(FbxAMatrix& TotalMatrix);
				
				//TODO move the real function from RenderCore to FVector, so we do not have to add render core to compute such a simple thing
				float FbxGetBasisDeterminantSign(const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis)
				{
					FMatrix Basis(
						FPlane(XAxis, 0),
						FPlane(YAxis, 0),
						FPlane(ZAxis, 0),
						FPlane(0, 0, 0, 1)
					);
					return (Basis.Determinant() < 0) ? -1.0f : +1.0f;
				}
				FMeshDescription* MeshDescription;
				FbxScene* SDKScene;
				FbxGeometryConverter* SDKGeometryConverter;
				bool bInitialized = false;
			};

			class FMeshPayloadContext : public FPayloadContextBase
			{
			public:
				virtual ~FMeshPayloadContext() {}
				virtual FString GetPayloadType() const override { return TEXT("Mesh-PayloadContext"); }
				virtual bool FetchPayloadToFile(const FString& PayloadFilepath, TArray<FString>& JSonErrorMessages) override;
				FbxMesh* Mesh = nullptr;
				FbxScene* SDKScene = nullptr;
				FbxGeometryConverter* SDKGeometryConverter = nullptr;
			};

			class FShapePayloadContext : public FPayloadContextBase
			{
			public:
				virtual ~FShapePayloadContext() {}
				virtual FString GetPayloadType() const override { return TEXT("Shape-PayloadContext"); }
				virtual bool FetchPayloadToFile(const FString& PayloadFilepath, TArray<FString>& JSonErrorMessages) override;
				FbxShape* Shape = nullptr;
				FbxScene* SDKScene = nullptr;
				FbxGeometryConverter* SDKGeometryConverter = nullptr;
			};

			class FFbxMesh
			{
			public:
				static FString GetMeshName(FbxGeometryBase* Mesh);
				static FString GetMeshUniqueID(FbxGeometryBase* Mesh);
				static void ExtractSkinnedMeshNodeJoints(FbxScene* SDKScene, FbxMesh* Mesh, UInterchangeMeshNode* MeshNode, TArray<FString>& JSonErrorMessages);

				static void AddAllMeshes(FbxScene* SDKScene, FbxGeometryConverter* SDKGeometryConverter, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages, TMap<FString, TSharedPtr<FPayloadContextBase>>& PayloadContexts);
			
			protected:
				static UInterchangeMeshNode* CreateMeshNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& NodeUniqueID, TArray<FString>& JSonErrorMessages);
				static FString GetUniqueIDString(const uint64 UniqueID);
			};
		}//ns Private
	}//ns Interchange
}//ns UE
