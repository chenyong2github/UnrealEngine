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
	{
		std::lock_guard<std::mutex> lock(RefsLock);
		Processor = ref new SceneProcessor();
	}
	RequestAsyncUpdate();
#endif
}

void SceneUnderstandingObserver::StopSceneUnderstandingObserver()
{
	std::lock_guard<std::mutex> lock(RefsLock);

#if WITH_SCENE_UNDERSTANDING
	Processor = nullptr;
#endif
}

void SceneUnderstandingObserver::RequestAsyncUpdate()
{
#if WITH_SCENE_UNDERSTANDING
	std::lock_guard<std::mutex> lock(RefsLock);
	create_task([this, UpdateProcessor = Processor]()
	{
		UpdateProcessor->Update(Settings, VolumeSize);
		OnSceneUnderstandingUpdateComplete(UpdateProcessor);
	});
#endif
}

#if WITH_SCENE_UNDERSTANDING
void SceneUnderstandingObserver::InitSettings()
{
	Settings.EnableSceneObjectQuads = bWantsPlanes;
	Settings.EnableOnlyObservedSceneObjects = !bWantsPlanes && bWantsSceneMeshes;
	Settings.EnablePersistentSceneObjects = bWantsSceneMeshes;
	// This comes from the mesh observer
	Settings.EnableWorldMesh = false;
	Settings.RequestedMeshLOD = MeshLOD::Medium;
}

void SceneUnderstandingObserver::OnSceneUnderstandingUpdateComplete(SceneProcessor^ UpdatedProcessor)
{
	{
		std::lock_guard<std::mutex> lock(RefsLock);
		// It's possible this processor was stopped while an update was pending
		// So only process this update if it's for the same scene processor object
		if (UpdatedProcessor == Processor)
		{
			OnStartUpdates();
			Array<SceneObject^>^ SceneObjects = nullptr;
			Processor->GetSceneObjects(&SceneObjects);
			const unsigned int SceneObjectCount = SceneObjects->Length;
			for (unsigned int ObjectIndex = 0; ObjectIndex < SceneObjectCount; ObjectIndex++)
			{
				SceneObject^ SCObject = SceneObjects->get(ObjectIndex);

				// Process each quad this scene object has
				{
					std::set<Guid, GUIDComparer> CurrentPlaneGuidSet;
					Array<Quad^>^ QuadObjects = nullptr;
					SCObject->GetAssociatedQuads(&QuadObjects);
					if (QuadObjects != nullptr)
					{
						const unsigned int QuadCount = SceneObjects->Length;
						for (unsigned int QuadIndex = 0; QuadIndex < QuadCount; QuadIndex++)
						{
							Quad^ QuadObject = QuadObjects->get(QuadIndex);
							PlaneUpdate CurrentPlane;
							CurrentPlane.Id = QuadObject->SpatialCoordinateSystem->SpatialCoordinateGuid;
							CurrentPlaneGuidSet.insert(CurrentPlane.Id);

							CurrentPlane.Width = QuadObject->WidthInMeters * 100.f;
							CurrentPlane.Height = QuadObject->HeightInMeters * 100.f;
							CurrentPlane.Orientation = (int32)QuadObject->Orientation;
							CurrentPlane.ObjectLabel = (int32)SCObject->Label;

							Windows::Perception::Spatial::SpatialCoordinateSystem^ QuadCoordSystem = Windows::Perception::Spatial::Preview::SpatialGraphInteropPreview::CreateCoordinateSystemForNode(CurrentPlane.Id);
							CopyTransform(CurrentPlane, QuadCoordSystem);
						}
					}
					// Remove any planes that were seen last time, but not this time
					std::set<Guid>::iterator It = LastPlaneGuidSet.begin();
					while (It != LastPlaneGuidSet.end())
					{
						Guid Id = *It;
						// If this one is not in the new set, then it was removed
						if (CurrentPlaneGuidSet.find(Id) == CurrentPlaneGuidSet.end())
						{
							PlaneUpdate RemovedPlane;
							RemovedPlane.Id = Id;
							OnRemovedPlane(&RemovedPlane);
						}
					}
					LastPlaneGuidSet = CurrentPlaneGuidSet;
				}

				// Process each mesh this scene object has
				{
					std::set<Guid, GUIDComparer> CurrentMeshGuidSet;
					Array<Mesh^>^ MeshObjects = nullptr;
					SCObject->GetAssociatedMeshes(&MeshObjects);
					if (MeshObjects != nullptr)
					{
						const unsigned int MeshCount = SceneObjects->Length;
						for (unsigned int MeshIndex = 0; MeshIndex < MeshCount; MeshIndex++)
						{
							Mesh^ MeshObject = MeshObjects->get(MeshIndex);
							MeshUpdate CurrentMesh;
							CurrentMesh.Id = MeshObject->SpatialCoordinateSystem->SpatialCoordinateGuid;

							Windows::Perception::Spatial::SpatialCoordinateSystem^ MeshCoordSystem = Windows::Perception::Spatial::Preview::SpatialGraphInteropPreview::CreateCoordinateSystemForNode(CurrentMesh.Id);
							CopyTransform(CurrentMesh, MeshCoordSystem);

							Array<int>^ Indices = nullptr;
							Array<float>^ Vertices = nullptr;
							MeshObject->GetTriangleIndices(&Indices);
							MeshObject->GetVertices(&Vertices);
							if (Indices != nullptr && Vertices != nullptr)
							{
								CurrentMesh.NumVertices = Vertices->Length / 3;
								CurrentMesh.NumIndices = Indices->Length;
								OnAllocateMeshBuffers(&CurrentMesh);
								CopyMeshData(CurrentMesh, Vertices, Indices);
							}
						}
					}
					std::set<Guid>::iterator It = LastMeshGuidSet.begin();
					while (It != LastMeshGuidSet.end())
					{
						Guid Id = *It;
						// If this one is not in the new set, then it was removed
						if (CurrentMeshGuidSet.find(Id) == CurrentMeshGuidSet.end())
						{
							MeshUpdate RemovedMesh;
							RemovedMesh.Id = Id;
							OnRemovedMesh(&RemovedMesh);
						}
					}
					LastMeshGuidSet = CurrentMeshGuidSet;
				}
			}
			OnFinishUpdates();
		}
	}
	RequestAsyncUpdate();
}

void SceneUnderstandingObserver::CopyMeshData(MeshUpdate& DestMesh, Array<float>^ Vertices, Array<int>^ Indices)
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
