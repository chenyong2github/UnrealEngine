// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Perception.Spatial.Surfaces.h>

using namespace Windows::Perception::Spatial;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Platform;

/**
 * The mesh observer singleton that notifies UE4 of changes
 */
class MeshUpdateObserver
{
public:
	static MeshUpdateObserver& Get();
	static void Release();

	/** To route logging messages back to the UE_LOG() macros */
	void SetOnLog(void(*FunctionPointer)(const wchar_t* LogMsg));

	void StartMeshObserver(
		float InTriangleDensity,
		float InVolumeSize,
		void(*StartFunctionPointer)(),
		void(*AllocFunctionPointer)(MeshUpdate*),
		void(*RemovedMeshPointer)(MeshUpdate*),
		void(*FinishFunctionPointer)()
	);
	/** Called to move the bounding volume that surrounds the player */
	void UpdateBoundingVolume(Windows::Perception::Spatial::SpatialCoordinateSystem^ InCoordinateSystem, Windows::Foundation::Numerics::float3 Position);
	void StopMeshObserver();

	void Log(const wchar_t* LogMsg);

private:
	MeshUpdateObserver();
	~MeshUpdateObserver();

	void OnSurfacesChanged(SpatialSurfaceObserver^ Observer, Platform::Object^);
	
	void CopyTransform(MeshUpdate& DestMesh, SpatialSurfaceMesh^ SurfaceMesh);
	void CopyMeshData(MeshUpdate& DestMesh, SpatialSurfaceMesh^ SurfaceMesh);

	/** Inits and optionally logs out the formats that the API supports so we can request the one that matches ours best */
	void InitSupportedMeshFormats();

	/** Function pointer for logging */
	void(*OnLog)(const wchar_t*);
	/** Function pointer for telling UE4 to prepare for updates */
	void(*OnStartMeshUpdates)();
	/** Function pointer for asking UE4 to allocate buffers (avoids an extra copy) */
	void(*OnAllocateBuffers)(MeshUpdate*);
	/** Function pointer for telling UE4 that the mesh was removed */
	void(*OnRemovedMesh)(MeshUpdate*);
	/** Function pointer for telling UE4 updates have completed */
	void(*OnFinishMeshUpdates)();

	static MeshUpdateObserver* ObserverInstance;

	float TriangleDensityPerCubicMeter;
	float VolumeSize;

	Windows::Perception::Spatial::SpatialCoordinateSystem^ LastCoordinateSystem = nullptr;
	/** Lets us customize the options we want when requesting meshes from the surfaces */
	SpatialSurfaceMeshOptions^ MeshOptions = nullptr;
};
