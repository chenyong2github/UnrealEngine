// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TargetInterfaces/MeshTargetInterfaceTypes.h"
#include "MeshDescription.h"

#include "MeshDescriptionProvider.generated.h"

UINTERFACE()
class INTERACTIVETOOLSFRAMEWORK_API UMeshDescriptionProvider : public UInterface
{
	GENERATED_BODY()
};

class INTERACTIVETOOLSFRAMEWORK_API IMeshDescriptionProvider
{
	GENERATED_BODY()

public:
	/**
	 * Access the MeshDescription available through this Provider. Note that this MeshDescription may or may not 
	 * be owned by the provider and should not be modified directly. Use IMeshDescriptionCommitter for writes.
	 * @return pointer to MeshDescription 
	 */
	virtual const FMeshDescription* GetMeshDescription(const FGetMeshParameters& GetMeshParams = FGetMeshParameters()) = 0;


	/**
	 * Get a copy of the MeshDescription available through this Provider. 
	 */
	virtual FMeshDescription GetMeshDescriptionCopy(const FGetMeshParameters& GetMeshParams)
	{
		return *GetMeshDescription(GetMeshParams);
	}

};