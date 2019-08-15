// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"
#include "MixedRealityInterop.h"

#include "SceneUnderstandingObserver.h"
#include "FastConversion.h"
#include "CxDataFromBuffer.h"

#include <streambuf>
#include <robuffer.h>
#include <wrl.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#include <WindowsNumerics.h>
#include <windows.foundation.numerics.h>
#include <ppltasks.h>
#include <pplawait.h>

#include <set>
#include <string>
#include <sstream>


using namespace Microsoft::WRL;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Graphics::DirectX::Direct3D11;

using namespace std::placeholders;
using namespace concurrency;

#if WITH_SCENE_UNDERSTANDING
	#pragma message("SceneUnderstanding API is being compiled in")
#else
	#pragma message("SceneUnderstanding API is being compiled out")
#endif

struct GUIDComparer
{
	bool operator()(const Guid& Left, const Guid& Right) const
	{
		return memcmp(&Left, &Right, sizeof(Right)) < 0;
	}
};

/** Controls access to our references */
static std::mutex RefsLock;
/** The last known set of mesh guids. Used to handle removals */
std::set<Guid, GUIDComparer> LastMeshGuidSet;
/** The last known set of plane guids. Used to handle removals */
std::set<Guid, GUIDComparer> LastPlaneGuidSet;

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
		auto RequestTask = concurrency::create_task(SceneObserver::RequestAccessAsync());
		RequestTask.then([this](SceneObserverAccessStatus AccessStatus)
		{
			if (AccessStatus == SceneObserverAccessStatus::Allowed)
			{
				std::lock_guard<std::mutex> lock(RefsLock);
				bIsRunning = true;
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
#endif
}

void SceneUnderstandingObserver::RequestAsyncUpdate()
{
#if WITH_SCENE_UNDERSTANDING
	Scene^ LastScene = nullptr;
	{
		std::lock_guard<std::mutex> lock(RefsLock);
		LastScene = ObservedScene;
	}

	auto RequestTask = concurrency::create_task(SceneObserver::ComputeAsync(Settings, VolumeSize, LastScene));
	RequestTask.then([this](Scene^ NewScene)
	{
		{
			std::lock_guard<std::mutex> lock(RefsLock);
			ObservedScene = NewScene;
		}
		OnSceneUnderstandingUpdateComplete();
	});
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
		std::set<Guid, GUIDComparer> CurrentPlaneGuidSet;
		std::set<Guid, GUIDComparer> CurrentMeshGuidSet;

		IVectorView<SceneObject^>^ SceneObjects = ObservedScene->SceneObjects;
		const unsigned int SceneObjectCount = SceneObjects != nullptr ? SceneObjects->Size : 0;
		for (unsigned int ObjectIndex = 0; ObjectIndex < SceneObjectCount; ObjectIndex++)
		{
			SceneObject^ SCObject = SceneObjects->GetAt(ObjectIndex);

			// Process each quad this scene object has
			IVectorView<SceneQuad^>^ QuadObjects = SCObject->Quads;
			const unsigned int QuadCount = QuadObjects != nullptr ? QuadObjects->Size : 0;
			for (unsigned int QuadIndex = 0; QuadIndex < QuadCount; QuadIndex++)
			{
				SceneQuad^ QuadObject = QuadObjects->GetAt(QuadIndex);
				PlaneUpdate CurrentPlane;
				CurrentPlane.Id = QuadObject->Id;
				CurrentPlaneGuidSet.insert(CurrentPlane.Id);

				CurrentPlane.Width = QuadObject->Extents.x * 100.f;
				CurrentPlane.Height = QuadObject->Extents.y * 100.f;
				CurrentPlane.Orientation = (int32)QuadObject->Alignment;
				CurrentPlane.ObjectLabel = (int32)SCObject->Kind;

				Windows::Perception::Spatial::SpatialCoordinateSystem^ QuadCoordSystem = Windows::Perception::Spatial::Preview::SpatialGraphInteropPreview::CreateCoordinateSystemForNode(CurrentPlane.Id);
				CopyTransform(CurrentPlane, QuadCoordSystem);
			}

			// Process each mesh this scene object has
			IVectorView<SceneMesh^>^ MeshObjects = SCObject->Meshes;
			const unsigned int MeshCount = MeshObjects != nullptr ? MeshObjects->Size : 0;
			for (unsigned int MeshIndex = 0; MeshIndex < MeshCount; MeshIndex++)
			{
				SceneMesh^ MeshObject = MeshObjects->GetAt(MeshIndex);
				MeshUpdate CurrentMesh;
				CurrentMesh.Id = MeshObject->Id;

				Windows::Perception::Spatial::SpatialCoordinateSystem^ MeshCoordSystem = Windows::Perception::Spatial::Preview::SpatialGraphInteropPreview::CreateCoordinateSystemForNode(CurrentMesh.Id);
				CopyTransform(CurrentMesh, MeshCoordSystem);

				const unsigned int TriangleIndexCount = MeshObject->TriangleIndexCount;
				const unsigned int VertexCount = MeshObject->VertexCount;
				if (TriangleIndexCount > 0 && VertexCount > 0)
				{
					CurrentMesh.NumVertices = VertexCount;
					CurrentMesh.NumIndices = TriangleIndexCount;
					OnAllocateMeshBuffers(&CurrentMesh);

					Array<unsigned int>^ Indices = ref new Array<unsigned int>(TriangleIndexCount);
					MeshObject->GetTriangleIndices(Indices);
					Array<float>^ VertexComponents = ref new Array<float>(VertexCount * 3);
					MeshObject->GetVertexPositions(VertexComponents);

					CopyMeshData(CurrentMesh, VertexComponents, Indices);
				}
			}
		}

		// Remove any planes that were seen last time, but not this time
		std::set<Guid>::iterator PlaneIt = LastPlaneGuidSet.begin();
		while (PlaneIt != LastPlaneGuidSet.end())
		{
			Guid Id = *PlaneIt;
			// If this one is not in the new set, then it was removed
			if (CurrentPlaneGuidSet.find(Id) == CurrentPlaneGuidSet.end())
			{
				PlaneUpdate RemovedPlane;
				RemovedPlane.Id = Id;
				OnRemovedPlane(&RemovedPlane);
			}
		}
		LastPlaneGuidSet = CurrentPlaneGuidSet;

		std::set<Guid>::iterator MeshIt = LastMeshGuidSet.begin();
		while (MeshIt != LastMeshGuidSet.end())
		{
			Guid Id = *MeshIt;
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

void SceneUnderstandingObserver::CopyMeshData(MeshUpdate& DestMesh, Array<float>^ Vertices, Array<unsigned int>^ Indices)
{
	int Floats = DestMesh.NumVertices;
	float* DestVertices = (float*)DestMesh.Vertices;

	for (int Index = 0; Index < Floats; Index += 3)
	{
		float X = Vertices->get(Index + 0);
		float Y = Vertices->get(Index + 1);
		float Z = Vertices->get(Index + 2);
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
		DestIndices[0] = Indices->get(Index + 2);
		DestIndices[1] = Indices->get(Index + 1);
		DestIndices[2] = Indices->get(Index + 0);
		DestIndices += 3;
	}
}
#endif

void SceneUnderstandingObserver::CopyTransform(TransformUpdate& Transform, Windows::Perception::Spatial::SpatialCoordinateSystem^ CoordSystem)
{
	XMMATRIX ConvertTransform = XMMatrixIdentity();
	auto MeshTransform = CoordSystem->TryGetTransformTo(LastCoordinateSystem);
	if (MeshTransform != nullptr)
	{
		ConvertTransform = XMLoadFloat4x4(&MeshTransform->Value);
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
