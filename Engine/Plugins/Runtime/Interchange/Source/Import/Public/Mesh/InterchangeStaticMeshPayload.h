// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"

namespace UE
{
	namespace Interchange
	{
		struct FStaticMeshPayloadData
		{
			TArray<FMeshDescription> MeshDescriptions;
		};
	}//ns Interchange
}//ns UE
