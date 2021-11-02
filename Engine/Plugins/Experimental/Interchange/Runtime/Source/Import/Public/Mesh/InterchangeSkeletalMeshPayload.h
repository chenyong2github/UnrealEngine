// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"

namespace UE
{
	namespace Interchange
	{
		struct FSkeletalMeshLodPayloadData
		{
			//Currently the skeletalmesh payload data is editor only, we have to move to something available at runtime
			FMeshDescription LodMeshDescription;

			//This map the indice use in the meshdescription to the bone name, so we can use this information to remap properly the skinning when we merge the meshdescription
			TArray<FString> JointNames;
		};

		struct FSkeletalMeshBlendShapePayloadData
		{
			//BlendShape payload is a MeshDescription containing at least the vertex position.
			FMeshDescription LodMeshDescription;
			TOptional<FTransform> GlobalTransform;
			//The vertex offset of the blendshape in case we combine mesh node together
			int32 VertexOffset;
			//The name of the blendshape
			FString BlendShapeName;
		};
	}//ns Interchange
}//ns UE
