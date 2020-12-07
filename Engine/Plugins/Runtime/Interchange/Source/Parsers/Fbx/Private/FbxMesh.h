// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxHelper.h"
#include "FbxInclude.h"

/** Forward declarations */
struct FMeshDescription;


namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FMeshDescriptionImporter
			{
			public:
				FMeshDescriptionImporter(FMeshDescription* InMeshDescription, FbxNode* InMeshNode, FbxScene* InSDKScene, FbxGeometryConverter* InSDKGeometryConverter);
				bool FillStaticMeshDescriptionFromFbxMesh();
				bool FillSkinnedMeshDescriptionFromFbxMesh(TArray<FbxNode*>* SortedJoints);
			private:
				
				enum class EMeshType : uint8
				{
					None = 0, //No mesh type to import
					Static = 1, //static mesh
					Skinned = 2, //skinned mesh with joints
					Rigid = 3  //rigid mesh (joints will be created from the geometry transform)
				};

				bool FillMeshDescriptionFromFbxMesh(EMeshType MeshType, TArray<FbxNode*>* SortedJoints);
				FbxAMatrix ComputeNodeMatrix(FbxNode* Node);
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
				FbxNode* MeshNode;
				FbxScene* SDKScene;
				FbxGeometryConverter* SDKGeometryConverter;
				bool bIsStaticMesh;
				bool bInitialized = false;
			};
		}//ns Private
	}//ns Interchange
}//ns UE
