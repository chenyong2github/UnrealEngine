// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/Containers/DisplayClusterRender_MeshComponentTypes.h"
#include "Render/Containers/DisplayClusterRender_StaticMeshComponentRef.h"
#include "Render/Containers/DisplayClusterRender_ProceduralMeshComponentRef.h"

class USceneComponent;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class FDisplayClusterRender_MeshGeometry;
class FDisplayClusterRender_MeshComponentProxy;

struct FStaticMeshLODResources;
struct FProcMeshSection;

class DISPLAYCLUSTER_API FDisplayClusterRender_MeshComponent
{
public:
	FDisplayClusterRender_MeshComponent();
	~FDisplayClusterRender_MeshComponent();

public:
	/**
	* Assign static mesh component and origin to this container
	* Then send geometry from the StaticMesh component to proxy
	*
	* @param InStaticMeshComponent - Pointer to the StaticMesh component with geometry for warp
	* @param InOriginComponent     - (optional) stage origin point component
	* @param InLODIndex            - (optional) LOD index
	*/
	void AssignStaticMeshComponentRefs(UStaticMeshComponent* InStaticMeshComponent, USceneComponent* InOriginComponent = nullptr, int32 InLODIndex = 0);

	/**
	* Assign procedural mesh component, section index and origin to this container
	* Then send geometry from the ProceduralMesh(SectionIndex) to proxy
	*
	* @param InProceduralMeshComponent - Pointer to the ProceduralMesh component with geometry for warp
	* @param InSectionIndex            - (optional) The section index in ProceduralMesh
	* @param InOriginComponent         - (optional) stage origin point component
	*/
	void AssignProceduralMeshComponentRefs(UProceduralMeshComponent* InProceduralMeshComponent, USceneComponent* InOriginComponent = nullptr, const int32 InSectionIndex = 0);

	/**
	* Assign procedural mesh section geometry
	* Then send geometry from the ProcMeshSection to proxy
	*
	* @param InStaticMesh - Pointer to the StaticMesh geometry
	*/
	void AssignProceduralMeshSection(const FProcMeshSection& InProcMeshSection);

	/**
	* Assign static mesh geometry
	* Then send geometry from the StaticMesh to proxy
	*
	* @param InStaticMesh - Pointer to the StaticMesh geometry
	*/
	void AssignStaticMesh(const UStaticMesh* InStaticMesh, int32 InLODIndex = 0);

	/**
	* Assign mesh geometry
	* Then send geometry to proxy
	*
	* @param InMeshGeometry - Pointer to the geometry data
	*/
	void AssignMeshGeometry(const FDisplayClusterRender_MeshGeometry* InMeshGeometry);

	/**
	* Release refs, proxy, RHI.
	*
	*/
	void ReleaseMeshComponent();

	/**
	* Release proxy
	*
	*/
	void ReleaseProxyGeometry();

	/**
	* Get referenced Origin component object
	*
	* @return - pointer to the Origin component
	*/
	USceneComponent*          GetOriginComponent();

	/**
	* Get referenced StaticMesh component object
	*
	* @return - pointer to the StaticMesh component
	*/
	UStaticMeshComponent*     GetStaticMeshComponent();

	/**
	* Get referenced ProceduralMesh component object
	*
	* @return - pointer to the ProceduralMesh component
	*/
	UProceduralMeshComponent* GetProceduralMeshComponent();

	/**
	* Get geometry from assigned StaticMesh component
	*
	* @param LodIndex - (optional) lod index
	*
	* @return - pointer to the static mesh geometry
	*/
	const FStaticMeshLODResources* GetStaticMeshComponentLODResources(int32 InLODIndex = 0) const;

	/**
	* Get geometry from assigned ProceduralMesh section by index
	*
	* @param SectionIndex - Geometry source section index
	*
	* @return - pointer to the geometry section data
	*/
	const FProcMeshSection* GetProceduralMeshComponentSection(const int32 SectionIndex = 0) const;

	/**
	* Get nDisplay mesh component proxy object
	*
	* @return - pointer to the nDisplay mesh component proxy
	*/
	FDisplayClusterRender_MeshComponentProxy* GetMeshComponentProxy_RenderThread() const;

	/**
	* Set geometry function. This function change geometry prepared for proxy
	*
	* @param InDataFunc - geometry function type
	*/
	void SetGeometryFunc(const EDisplayClusterRender_MeshComponentProxyDataFunc InDataFunc);

	/**
	* Return true if referenced component geometry changed
	*/
	bool IsMeshComponentRefGeometryDirty() const;

	/**
	* Mark referenced component geometry as changed
	*/
	void MarkMeshComponentRefGeometryDirty() const;

	/**
	* Clear referenced component geometry changed flag
	*/
	void ResetMeshComponentRefGeometryDirty() const;

	bool EqualsMeshComponentName(const FName& InMeshComponentName) const;

	EDisplayClusterRender_MeshComponentGeometrySource GetGeometrySource() const;

private:
	EDisplayClusterRender_MeshComponentGeometrySource GeometrySource = EDisplayClusterRender_MeshComponentGeometrySource::Disabled;
	EDisplayClusterRender_MeshComponentProxyDataFunc  DataFunc = EDisplayClusterRender_MeshComponentProxyDataFunc::Disabled;

	// Reference containers:
	FDisplayClusterSceneComponentRef                 OriginComponentRef;
	FDisplayClusterRender_StaticMeshComponentRef     StaticMeshComponentRef;
	FDisplayClusterRender_ProceduralMeshComponentRef ProceduralMeshComponentRef;

private:
	FDisplayClusterRender_MeshComponentProxy* MeshComponentProxy = nullptr;
};
