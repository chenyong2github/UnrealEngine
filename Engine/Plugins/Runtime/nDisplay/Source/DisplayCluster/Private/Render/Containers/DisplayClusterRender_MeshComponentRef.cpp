// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Containers/DisplayClusterRender_MeshComponentRef.h"
#include "Engine/StaticMesh.h"

//*************************************************************************
//* FDisplayClusterRender_MeshComponentRef
//*************************************************************************

inline FName GetStaticMeshName(UStaticMeshComponent* StaticMeshComponent)
{
	if (StaticMeshComponent)
	{
		UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();

		// Return null geometry ptr as text
		if (StaticMesh == nullptr)
		{
			return FName(TEXT("nullptr"));
		}

		// Return geometry asset name
		return StaticMesh->GetFName();
	}

	// Return empty FName for null component ptr
	return NAME_None;
}

UStaticMeshComponent* FDisplayClusterRender_MeshComponentRef::GetOrFindMeshComponent() const
{
	FScopeLock lock(&DataGuard);

	USceneComponent* SceneComponent = GetOrFindSceneComponent();
	if (SceneComponent)
	{
		UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent);
		if (StaticMeshComponent)
		{
			// Raise the flag [mutable bIsStaticMeshChanged] for the changed mesh geometry and store the new mesh name in [mutable StaticMeshName]
			if (!bIsStaticMeshChanged && GetStaticMeshName(StaticMeshComponent) != StaticMeshName)
			{
				bIsStaticMeshChanged = true;
			}

			return StaticMeshComponent;
		}
	}

	return nullptr;
}

void FDisplayClusterRender_MeshComponentRef::ResetMeshComponentChangedFlag()
{
	bIsStaticMeshChanged = false;
	StaticMeshName = FName();
}

void FDisplayClusterRender_MeshComponentRef::ResetComponentRef()
{
	ResetMeshComponentChangedFlag();
	ResetSceneComponent();
}

bool FDisplayClusterRender_MeshComponentRef::SetComponentRef(UStaticMeshComponent* StaticMeshComponent)
{
	FScopeLock lock(&DataGuard);

	ResetMeshComponentChangedFlag();
	if(SetSceneComponent(StaticMeshComponent))
	{ 
		// Store the name of the current mesh geometry
		StaticMeshName = GetStaticMeshName(StaticMeshComponent);
		return true;
	}

	return false;
}
