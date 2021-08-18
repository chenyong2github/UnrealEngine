// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/Containers/DisplayClusterRender_MeshComponentRef.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentTypes.h"

struct FStaticMeshLODResources;
class FDisplayClusterRender_MeshGeometry;
class FDisplayClusterRender_MeshComponentProxy;

class DISPLAYCLUSTER_API FDisplayClusterRender_MeshComponent
{
public:
	FDisplayClusterRender_MeshComponent();
	~FDisplayClusterRender_MeshComponent();

public:
	void AssignMeshRefs(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent = nullptr);
	void UpdateDefferedRef();
	
	void UpdateDeffered(const UStaticMesh* InStaticMesh);
	void UpdateDeffered(const FDisplayClusterRender_MeshGeometry& InMeshGeometry);

	const FStaticMeshLODResources* GetStaticMeshLODResource(int LodIndex = 0) const;

	void Release();

	FDisplayClusterRender_MeshComponentProxy* GetProxy() const
	{ return MeshComponentProxy; }

public:
	FDisplayClusterRender_MeshComponentProxyDataFunc DataFunc = FDisplayClusterRender_MeshComponentProxyDataFunc::None;

	// Reference containers:
	FDisplayClusterRender_MeshComponentRef    MeshComponentRef;
	FDisplayClusterSceneComponentRef          OriginComponentRef;

private:
	FDisplayClusterRender_MeshComponentProxy* MeshComponentProxy;
};
