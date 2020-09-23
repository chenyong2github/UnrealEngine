// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxInclude.h"

namespace UE
{
	namespace FbxParser
	{
		namespace Private
		{
			struct FFbxConvert
			{
			public:

				//////////////////////////////////////////////////////////////////////////
				/** Transform Conversion API Begin */

				/**
				 * Create a UE transform from a fbx matrix
				 */
				static FTransform GetTransform(const FbxAMatrix& Matrix);

				/** Transform Conversion API End */
				//////////////////////////////////////////////////////////////////////////

				
				//////////////////////////////////////////////////////////////////////////
				/** Scene Conversion API Begin */

				/**
				 * Convert a fbx scene 
				 */
				static void ConvertScene(FbxScene* SDKScene);
				
				/** Scene Conversion API End */
				//////////////////////////////////////////////////////////////////////////


				//////////////////////////////////////////////////////////////////////////
				/** String Conversion API Begin */

				/**
				 * Replace all special characters with '_', then remove all namespace
				 * Special characters are . , / ` %
				 */
				static ANSICHAR* MakeName(const ANSICHAR* Name);

				/**
				 * Convert ANSI char to a FString using ANSI_TO_TCHAR macro
				 */
				static FString MakeString(const ANSICHAR* Name);

				/** String Conversion API End */
				//////////////////////////////////////////////////////////////////////////

			private:
				
				//////////////////////////////////////////////////////////////////////////
				/** Scene Conversion Private Implementation Begin */

				/**
				 * The Unroll filter expects only rotation curves, we need to walk the scene and extract the
				 * rotation curves from the nodes property. This can become time consuming but we have no choice.
				 */
				static void ApplyUnroll(FbxNode* pNode, FbxAnimLayer* pLayer, FbxAnimCurveFilterUnroll* pUnrollFilter);
				static void MergeAllLayerAnimation(FbxScene* SDKScene, FbxAnimStack* AnimStack, float ResampleRate);

				/** Scene Conversion Private Implementation End */
				//////////////////////////////////////////////////////////////////////////
			};
		}
	}
}
