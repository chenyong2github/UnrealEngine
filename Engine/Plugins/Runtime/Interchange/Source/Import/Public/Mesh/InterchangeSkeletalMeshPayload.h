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
		};
	}//ns Interchange
}//ns UE
