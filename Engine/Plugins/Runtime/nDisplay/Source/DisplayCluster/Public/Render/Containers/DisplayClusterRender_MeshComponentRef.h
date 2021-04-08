// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Misc/DisplayClusterObjectRef.h"


class DISPLAYCLUSTER_API FDisplayClusterRender_MeshComponentRef
	: public FDisplayClusterSceneComponentRef
{
public:
	FDisplayClusterRender_MeshComponentRef()
		: FDisplayClusterSceneComponentRef()
	{ }

	FDisplayClusterRender_MeshComponentRef(const FDisplayClusterRender_MeshComponentRef& InComponentRef)
		: FDisplayClusterSceneComponentRef(InComponentRef)
		, StaticMeshName(InComponentRef.StaticMeshName)
		, bIsStaticMeshChanged(InComponentRef.bIsStaticMeshChanged)
	{ }

	// Get or find the scene warp mesh component
	// Raise the flag [mutable bIsStaticMeshChanged] for the changed mesh geometry and store the new mesh name in [mutable StaticMeshName]
	UStaticMeshComponent* GetOrFindMeshComponent() const;

	bool SetComponentRef(UStaticMeshComponent* ComponentPtr);
	void ResetComponentRef();

	// Detect mesh object changes for warp logic
	bool IsMeshComponentChanged() const
	{
		return bIsStaticMeshChanged;
	}

protected:
	// Clear flags [bIsStaticMeshChanged]
	void ResetMeshComponentChangedFlag();

private:
	// Compares the assigned static mesh object by name, and raise for changed [mutable] bIsStaticMeshChanged
	FName StaticMeshName;
	mutable bool  bIsStaticMeshChanged = false;
};

