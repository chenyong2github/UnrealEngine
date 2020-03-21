// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Perception.Spatial.Surfaces.h>
#include <winrt/Windows.Foundation.h>

using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Perception::Spatial::Surfaces;
using namespace Platform;

#if WITH_SCENE_UNDERSTANDING
	#include <winrt/Microsoft.MixedReality.SceneUnderstanding.h>
	using namespace winrt::Microsoft::MixedReality::SceneUnderstanding;
#endif

/**
 * The scene understanding observer singleton that notifies UE4 of changes
 */
class SceneUnderstandingObserver
{
public:
	static SceneUnderstandingObserver& Get();
	static void Release();

	/** To route logging messages back to the UE_LOG() macros */
	void SetOnLog(void(*FunctionPointer)(const wchar_t* LogMsg));

	void StartSceneUnderstandingObserver(
		bool bGeneratePlanes,
		bool bGenerateSceneMeshes,
		float InVolumeSize,
		void(*StartFunctionPointer)(),
		void(*AddPlaneFunctionPointer)(PlaneUpdate*),
		void(*RemovePlaneFunctionPointer)(PlaneUpdate*),
		void(*AllocMeshFunctionPointer)(MeshUpdate*),
		void(*RemoveMeshFunctionPointer)(MeshUpdate*),
		void(*FinishFunctionPointer)()
	);
	/** Starts an async update that will call back into this object when complete */
	void RequestAsyncUpdate();
	void StopSceneUnderstandingObserver();

	void Log(const wchar_t* LogMsg);

#if WITH_SCENE_UNDERSTANDING
	void OnSceneUnderstandingUpdateComplete();
#endif

	void SetTrackingCoordinateSystem(winrt::Windows::Perception::Spatial::SpatialCoordinateSystem cs)
	{
		TrackingSpaceCoordinateSystem = cs;
	}
private:
	SceneUnderstandingObserver();
	~SceneUnderstandingObserver();

	/** Function pointer for logging */
	void(*OnLog)(const wchar_t*);
	/** Function pointer for telling UE4 to prepare for updates */
	void(*OnStartUpdates)();
	/** Function pointer for sending plane updates to UE4 */
	void(*OnAddPlane)(PlaneUpdate*);
	/** Function pointer for sending plane removals to UE4 */
	void(*OnRemovedPlane)(PlaneUpdate*);
	/** Function pointer for asking UE4 to allocate buffers (avoids an extra copy) */
	void(*OnAllocateMeshBuffers)(MeshUpdate*);
	/** Function pointer for telling UE4 a mesh was removed */
	void(*OnRemovedMesh)(MeshUpdate*);
	/** Function pointer for telling UE4 updates have completed */
	void(*OnFinishUpdates)();

	static SceneUnderstandingObserver* ObserverInstance;

	/** The size of the volume that we update each time there is an update */
	float VolumeSize = 1.f;
	/** Whether scene understanding should generate planes or not */
	bool bWantsPlanes = false;
	/** Whether scene understanding should generate scene meshes or not */
	bool bWantsSceneMeshes = false;

	winrt::Windows::Perception::Spatial::SpatialCoordinateSystem TrackingSpaceCoordinateSystem = nullptr;
	winrt::Windows::Perception::Spatial::SpatialCoordinateSystem OriginCoordinateSystem = nullptr;

	/** Copies the transform information to the update object in UE4 coordinate space */
	void CopyTransform(TransformUpdate& Transform, winrt::Windows::Foundation::Numerics::float4x4 offset);

#if WITH_SCENE_UNDERSTANDING
	/** Sets our query settings */
	void InitSettings();
	
	/** Copies the mesh data to the UE4 array data */
	void CopyMeshData(MeshUpdate& DestMesh, const std::vector<winrt::Windows::Foundation::Numerics::float3>& Vertices, const std::vector<unsigned int>& Indices);

	/** The scene understanding query settings that we'll use to observe the scene */
	SceneQuerySettings Settings;
	/** The last scene the observer returned to us */
	Scene ObservedScene = nullptr;
#endif
	/** Whether we are running and requesting updates */
	bool bIsRunning = false;
};
