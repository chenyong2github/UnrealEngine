// Copyright Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"
#include "MixedRealityInterop.h"

#include "SceneUnderstandingObserver.h"
#include "FastConversion.h"

#include <streambuf>
#include <robuffer.h>

#include <WindowsNumerics.h>
#include <winrt/windows.foundation.numerics.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.Perception.Spatial.Preview.h>

#include <set>
#include <string>
#include <sstream>
#include <mutex>


using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Foundation::Numerics;

#if WITH_SCENE_UNDERSTANDING
	#pragma message("SceneUnderstanding API is being compiled in")
#else
	#pragma message("SceneUnderstanding API is being compiled out")
#endif

/** Controls access to our references */
static std::mutex RefsLock;
/** The last known set of mesh guids. Used to handle removals */
std::set<guid> LastMeshGuidSet;
/** The last known set of plane guids. Used to handle removals */
std::set<guid> LastPlaneGuidSet;

SceneUnderstandingObserver* SceneUnderstandingObserver::ObserverInstance = nullptr;

SceneUnderstandingObserver::SceneUnderstandingObserver()
	: OnLog(nullptr)
	, OnStartUpdates(nullptr)
	, OnAddPlane(nullptr)
	, OnRemovedPlane(nullptr)
	, OnAllocateMeshBuffers(nullptr)
	, OnRemovedMesh()
	, OnFinishUpdates(nullptr)
{
}

SceneUnderstandingObserver::~SceneUnderstandingObserver()
{
}

SceneUnderstandingObserver& SceneUnderstandingObserver::Get()
{
	if (ObserverInstance == nullptr)
	{
		ObserverInstance = new SceneUnderstandingObserver();
	}
	return *ObserverInstance;
}

void SceneUnderstandingObserver::Release()
{
	if (ObserverInstance != nullptr)
	{
		ObserverInstance->StopSceneUnderstandingObserver();
		delete ObserverInstance;
		ObserverInstance = nullptr;
	}
}

void SceneUnderstandingObserver::SetOnLog(void(*FunctionPointer)(const wchar_t* LogMsg))
{
	OnLog = FunctionPointer;
}

void SceneUnderstandingObserver::Log(const wchar_t* LogMsg)
{
	if (OnLog != nullptr)
	{
		OnLog(LogMsg);
	}
}

void SceneUnderstandingObserver::StartSceneUnderstandingObserver(
	bool bGeneratePlanes,
	bool bGenerateSceneMeshes,
	float InVolumeSize,
	void(*StartFunctionPointer)(),
	void(*AddPlaneFunctionPointer)(PlaneUpdate*),
	void(*RemovePlaneFunctionPointer)(PlaneUpdate*),
	void(*AllocMeshFunctionPointer)(MeshUpdate*),
	void(*RemoveMeshFunctionPointer)(MeshUpdate*),
	void(*FinishFunctionPointer)()
)
{
	bWantsPlanes = bGeneratePlanes;
	bWantsSceneMeshes = bGenerateSceneMeshes;
	if (!bWantsPlanes && !bWantsSceneMeshes)
	{
		Log(L"Either plane generation or scene mesh generation must be enabled for SceneUnderstanding to work. Aborting.");
		return;
	}

	VolumeSize = InVolumeSize;
	if (VolumeSize <= 0.f)
	{
		Log(L"Invalid volume size to StartSceneUnderstandingObserver(). Aborting.");
		return;
	}

	OnStartUpdates = StartFunctionPointer;
	if (OnStartUpdates == nullptr)
	{
		Log(L"Null start updates function pointer passed to StartSceneUnderstandingObserver(). Aborting.");
		return;
	}

	OnAddPlane = AddPlaneFunctionPointer;
	if (OnAddPlane == nullptr)
	{
		Log(L"Null add planes function pointer passed to StartSceneUnderstandingObserver(). Aborting.");
		return;
	}

	OnRemovedPlane = RemovePlaneFunctionPointer;
	if (OnRemovedPlane == nullptr)
	{
		Log(L"Null remove planes function pointer passed to StartSceneUnderstandingObserver(). Aborting.");
		return;
	}

	OnAllocateMeshBuffers = AllocMeshFunctionPointer;
	if (OnAllocateMeshBuffers == nullptr)
	{
		Log(L"Null allocate buffers function pointer passed to StartSceneUnderstandingObserver(). Aborting.");
		return;
	}

	OnRemovedMesh = RemoveMeshFunctionPointer;
	if (OnRemovedMesh == nullptr)
	{
		Log(L"Null removed mesh function pointer passed to StartSceneUnderstandingObserver(). Aborting.");
		return;
	}

	OnFinishUpdates = FinishFunctionPointer;
	if (OnFinishUpdates == nullptr)
	{
		Log(L"Null finish updates function pointer passed to StartSceneUnderstandingObserver(). Aborting.");
		return;
	}

#if WITH_SCENE_UNDERSTANDING
	// If it's supported, request access
	if (SceneObserver::IsSupported())
	{
		SceneObserver::RequestAccessAsync().Completed([=](auto&& asyncInfo, auto&&  asyncStatus)
		{
			if (asyncInfo.GetResults() == SceneObserverAccessStatus::Allowed)
			{
				{
					std::lock_guard<std::mutex> lock(RefsLock);
					bIsRunning = true;
				}
				InitSettings();
				RequestAsyncUpdate();
			}
			else
			{
				Log(L"User denied permission for scene understanding. No updates will occur.");
			}
		});
	}
	else
	{
		Log(L"SceneObserver::IsSupported() returned false. No updates will occur.");
	}
#endif
}

void SceneUnderstandingObserver::StopSceneUnderstandingObserver()
{
	std::lock_guard<std::mutex> lock(RefsLock);

#if WITH_SCENE_UNDERSTANDING
	bIsRunning = false;
	OriginCoordinateSystem = nullptr;
#endif
}

void SceneUnderstandingObserver::RequestAsyncUpdate()
{
#if WITH_SCENE_UNDERSTANDING
	Scene LastScene = nullptr;
	{
		std::lock_guard<std::mutex> lock(RefsLock);
		LastScene = ObservedScene;
	}

	auto Handler = [this](Scene NewScene)
	{
		{
			std::lock_guard<std::mutex> lock(RefsLock);
			ObservedScene = NewScene;
			OriginCoordinateSystem = Preview::SpatialGraphInteropPreview::CreateCoordinateSystemForNode(ObservedScene.OriginSpatialGraphNodeId());
		}
		OnSceneUnderstandingUpdateComplete();
	};

	if (ObservedScene == nullptr)
	{
		SceneObserver::ComputeAsync(Settings, VolumeSize).Completed([=](auto&& asyncInfo, auto&&  asyncStatus) { Handler(asyncInfo.GetResults()); });
	}
	else
	{
		SceneObserver::ComputeAsync(Settings, VolumeSize, ObservedScene).Completed([=](auto&& asyncInfo, auto&&  asyncStatus) { Handler(asyncInfo.GetResults()); });
	}

#endif
}

#if WITH_SCENE_UNDERSTANDING
void SceneUnderstandingObserver::InitSettings()
{
	Settings.EnableSceneObjectQuads = bWantsPlanes;
	Settings.EnableSceneObjectMeshes = bWantsSceneMeshes;
	Settings.EnableOnlyObservedSceneObjects = !bWantsPlanes && bWantsSceneMeshes;
	// This comes from the mesh observer
	Settings.EnableWorldMesh = false;
	Settings.RequestedMeshLevelOfDetail = SceneMeshLevelOfDetail::Medium;
}

void SceneUnderstandingObserver::OnSceneUnderstandingUpdateComplete()
{
	{
		std::lock_guard<std::mutex> lock(RefsLock);
		OnStartUpdates();

		// Tracks current vs last known for removal notifications
		std::set<guid> CurrentPlaneGuidSet;
		std::set<guid> CurrentMeshGuidSet;

		auto SceneObjects = ObservedScene.SceneObjects();
		const unsigned int SceneObjectCount = SceneObjects != nullptr ? SceneObjects.Size() : 0;
		for (unsigned int ObjectIndex = 0; ObjectIndex < SceneObjectCount; ObjectIndex++)
		{
			SceneObject SCObject = SceneObjects.GetAt(ObjectIndex);
			auto localTransform = SCObject.GetLocationAsMatrix();

			// Process each quad this scene object has
			auto QuadObjects = SCObject.Quads();
			const unsigned int QuadCount = QuadObjects != nullptr ? QuadObjects.Size() : 0;
			for (unsigned int QuadIndex = 0; QuadIndex < QuadCount; QuadIndex++)
			{
				SceneQuad QuadObject = QuadObjects.GetAt(QuadIndex);
				CurrentPlaneGuidSet.insert(QuadObject.Id());
				PlaneUpdate CurrentPlane;
				CurrentPlane.Id = QuadObject.Id();

				CurrentPlane.Width = QuadObject.Extents().x * 100.f;
				CurrentPlane.Height = QuadObject.Extents().y * 100.f;
				CurrentPlane.Orientation = (int32_t)QuadObject.Alignment();
				CurrentPlane.ObjectLabel = (int32_t)SCObject.Kind();

				CopyTransform(CurrentPlane, localTransform);
			}

			// Process each mesh this scene object has
			auto MeshObjects = SCObject.Meshes();
			const unsigned int MeshCount = MeshObjects != nullptr ? MeshObjects.Size() : 0;
			for (unsigned int MeshIndex = 0; MeshIndex < MeshCount; MeshIndex++)
			{
				SceneMesh MeshObject = MeshObjects.GetAt(MeshIndex);
				CurrentMeshGuidSet.insert(MeshObject.Id());
				MeshUpdate CurrentMesh;
				CurrentMesh.Id = MeshObject.Id();

				CopyTransform(CurrentMesh, localTransform);

				const unsigned int TriangleIndexCount = MeshObject.TriangleIndexCount();
				const unsigned int VertexCount = MeshObject.VertexCount();
				if (TriangleIndexCount > 0 && VertexCount > 0)
				{
					CurrentMesh.NumVertices = VertexCount;
					CurrentMesh.NumIndices = TriangleIndexCount;
					OnAllocateMeshBuffers(&CurrentMesh);

					std::vector<unsigned int> Indices(TriangleIndexCount);
					MeshObject.GetTriangleIndices(Indices);
					std::vector<float3> VertexComponents(VertexCount);
					MeshObject.GetVertexPositions(VertexComponents);

					CopyMeshData(CurrentMesh, VertexComponents, Indices);
				}
			}
		}

		// Remove any planes that were seen last time, but not this time
		for (const auto PlaneKey : LastPlaneGuidSet)
		{
			guid Id = PlaneKey;
			// If this one is not in the new set, then it was removed
			if (CurrentPlaneGuidSet.find(Id) == CurrentPlaneGuidSet.end())
			{
				PlaneUpdate RemovedPlane;
				RemovedPlane.Id = Id;
				OnRemovedPlane(&RemovedPlane);
			}
		}
		LastPlaneGuidSet = CurrentPlaneGuidSet;

		for (const auto MeshKey : LastMeshGuidSet)
		{
			guid Id = MeshKey;
			// If this one is not in the new set, then it was removed
			if (CurrentMeshGuidSet.find(Id) == CurrentMeshGuidSet.end())
			{
				MeshUpdate RemovedMesh;
				RemovedMesh.Id = Id;
				OnRemovedMesh(&RemovedMesh);
			}
		}
		LastMeshGuidSet = CurrentMeshGuidSet;

		OnFinishUpdates();
	}
	RequestAsyncUpdate();
}

void SceneUnderstandingObserver::CopyMeshData(MeshUpdate& DestMesh, const std::vector<float3>& Vertices, const std::vector<unsigned int>& Indices)
{
	int Floats = DestMesh.NumVertices;
	float* DestVertices = (float*)DestMesh.Vertices;

	for (int Index = 0; Index < Floats; Index++)
	{
		float X = Vertices[Index].x;
		float Y = Vertices[Index].y;
		float Z = Vertices[Index].z;
		// Use DX math on the theory this is faster
		XMFLOAT4 Source(X, Y, Z, 0.f);
		XMFLOAT3 Dest = ToUE4Translation(Source);

		DestVertices[0] = Dest.x;
		DestVertices[1] = Dest.y;
		DestVertices[2] = Dest.z;
		DestVertices += 3;
	}

	short* DestIndices = (short*)DestMesh.Indices;
	// Reverse triangle order
	for (int Index = 0; Index < DestMesh.NumIndices; Index += 3)
	{
		DestIndices[0] = Indices[Index + 2];
		DestIndices[1] = Indices[Index + 1];
		DestIndices[2] = Indices[Index + 0];
		DestIndices += 3;
	}
}
#endif

void SceneUnderstandingObserver::CopyTransform(TransformUpdate& Transform, float4x4 offset)
{
	XMMATRIX ConvertTransform = XMMatrixIdentity();
	XMMATRIX localOffset = DirectX::XMLoadFloat4x4(&offset);

	if (TrackingSpaceCoordinateSystem)
	{
		auto MeshTransform = OriginCoordinateSystem.TryGetTransformTo(TrackingSpaceCoordinateSystem);
	if (MeshTransform != nullptr)
	{
			ConvertTransform = XMLoadFloat4x4(&MeshTransform.Value());
		}
	}

	XMVECTOR TransformScale;
	XMVECTOR TransformRot;
	XMVECTOR TransformTrans;
	XMMatrixDecompose(&TransformScale, &TransformRot, &TransformTrans, ConvertTransform);

	XMFLOAT3 Translation = ToUE4Translation(TransformTrans);
	XMFLOAT4 Rotation = ToUE4Quat(TransformRot);
	XMFLOAT3 Scale = ToUE4Scale(TransformScale);

	Transform.Translation[0] = Translation.x;
	Transform.Translation[1] = Translation.y;
	Transform.Translation[2] = Translation.z;

	Transform.Scale[0] = Scale.x;
	Transform.Scale[1] = Scale.y;
	Transform.Scale[2] = Scale.z;

	Transform.Rotation[0] = Rotation.x;
	Transform.Rotation[1] = Rotation.y;
	Transform.Rotation[2] = Rotation.z;
	Transform.Rotation[3] = Rotation.w;
}
