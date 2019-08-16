// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"
#include "MixedRealityInterop.h"

#include "MeshObserver.h"
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

#include <map>
#include <string>
#include <sstream>

#include <DirectXMath.h>
#include <windows.graphics.directx.h>


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

using namespace DirectX;

// Winrt guid needs a comparison function to use in a std::map
struct GUIDComparer
{
	bool operator()(const Guid& Left, const Guid& Right) const
	{
		return memcmp(&Left, &Right, sizeof(Right)) < 0;
	}
};

/** Controls access to our references */
std::mutex MeshRefsLock;
bool bIsRunning = false;
SpatialSurfaceObserver^ SurfaceObserver = nullptr;
Windows::Foundation::EventRegistrationToken OnChangeEventToken;
std::map<Guid, long long, GUIDComparer> LastGuidToLastUpdateMap;

MeshUpdateObserver* MeshUpdateObserver::ObserverInstance = nullptr;

MeshUpdateObserver::MeshUpdateObserver()
	: OnLog(nullptr)
	, OnStartMeshUpdates(nullptr)
	, OnAllocateBuffers(nullptr)
	, OnFinishMeshUpdates(nullptr)
{
}

MeshUpdateObserver::~MeshUpdateObserver()
{
}

MeshUpdateObserver& MeshUpdateObserver::Get()
{
	if (ObserverInstance == nullptr)
	{
		ObserverInstance = new MeshUpdateObserver();
	}
	return *ObserverInstance;
}

void MeshUpdateObserver::Release()
{
	if (ObserverInstance != nullptr)
	{
		ObserverInstance->StopMeshObserver();
		delete ObserverInstance;
		ObserverInstance = nullptr;
	}
}

void MeshUpdateObserver::SetOnLog(void(*FunctionPointer)(const wchar_t* LogMsg))
{
	OnLog = FunctionPointer;
}

void MeshUpdateObserver::Log(const wchar_t* LogMsg)
{
	if (OnLog != nullptr)
	{
		OnLog(LogMsg);
	}
}

void MeshUpdateObserver::CopyMeshData(MeshUpdate& DestMesh, SpatialSurfaceMesh^ SurfaceMesh)
{
	XMFLOAT4* RawVertices = GetDataFromIBuffer<XMFLOAT4>(SurfaceMesh->VertexPositions->Data);
	short* RawIndices = GetDataFromIBuffer<short>(SurfaceMesh->TriangleIndices->Data);
	int VertexCount = DestMesh.NumVertices;
	float* DestVertices = (float*)DestMesh.Vertices;

	for (int Index = 0; Index < VertexCount; Index++)
	{
		// Use DX math on the theory this is faster
		XMFLOAT4 Source = RawVertices[Index];
		XMFLOAT3 Dest = ToUE4Translation(Source);

		DestVertices[0] = Dest.x;
		DestVertices[1] = Dest.y;
		DestVertices[2] = Dest.z;
		DestVertices += 3;
	}

	int TriangleCount = DestMesh.NumIndices / 3;
	short* DestIndices = (short*)DestMesh.Indices;
	// Reverse triangle order
	for (int Index = 0; Index < TriangleCount; Index++)
	{
		DestIndices[0] = RawIndices[2];
		DestIndices[1] = RawIndices[1];
		DestIndices[2] = RawIndices[0];
		DestIndices += 3;
		RawIndices += 3;
	}
}

void MeshUpdateObserver::CopyTransform(MeshUpdate& DestMesh, SpatialSurfaceMesh^ SurfaceMesh)
{
	XMMATRIX ConvertTransform = XMMatrixIdentity();
	auto MeshTransform = SurfaceMesh->CoordinateSystem->TryGetTransformTo(LastCoordinateSystem);
	if (MeshTransform != nullptr)
	{
		ConvertTransform = XMLoadFloat4x4(&MeshTransform->Value);
	}

	XMVECTOR MeshScale = XMLoadFloat3(&SurfaceMesh->VertexPositionScale);

	XMMATRIX ScaleMatrix = XMMatrixScalingFromVector(MeshScale);
	ConvertTransform = ScaleMatrix * ConvertTransform;

	XMVECTOR TransformScale;
	XMVECTOR TransformRot;
	XMVECTOR TransformTrans;
	XMMatrixDecompose(&TransformScale, &TransformRot, &TransformTrans, ConvertTransform);

	XMFLOAT3 Translation = ToUE4Translation(TransformTrans);
	XMFLOAT4 Rotation = ToUE4Quat(TransformRot);
	XMFLOAT3 Scale = ToUE4Scale(TransformScale);

	DestMesh.Translation[0] = Translation.x;
	DestMesh.Translation[1] = Translation.y;
	DestMesh.Translation[2] = Translation.z;

	DestMesh.Scale[0] = Scale.x;
	DestMesh.Scale[1] = Scale.y;
	DestMesh.Scale[2] = Scale.z;

	DestMesh.Rotation[0] = Rotation.x;
	DestMesh.Rotation[1] = Rotation.y;
	DestMesh.Rotation[2] = Rotation.z;
	DestMesh.Rotation[3] = Rotation.w;
}

void MeshUpdateObserver::OnSurfacesChanged(SpatialSurfaceObserver^ Observer, Platform::Object^)
{
	// Serialize updates from this side
	std::lock_guard<std::mutex> lock(MeshRefsLock);
	if (bIsRunning)
	{
		OnStartMeshUpdates();
		std::map<Guid, long long, GUIDComparer> CurrentGuidToLastUpdateMap;
		// Get the set of meshes that changed
		IMapView<Guid, SpatialSurfaceInfo^>^ const& SurfaceCollection = Observer->GetObservedSurfaces();
		for (auto Iter = SurfaceCollection->First(); Iter->HasCurrent; Iter->MoveNext())
		{
			auto KVPair = Iter->Current;

			Guid Id = KVPair->Key;
			SpatialSurfaceInfo^ SurfaceInfo = KVPair->Value;
			long long UpdateTime = SurfaceInfo->UpdateTime.UniversalTime;
			// Add the current map which we store in the last one once done iterating (keeps from having to manage removals)
			CurrentGuidToLastUpdateMap.emplace(Id, UpdateTime);

			MeshUpdate CurrentMesh;
			CurrentMesh.Id = Id;

			// Check to see if this is an add or update
			bool bIsAdd = LastGuidToLastUpdateMap.find(Id) == LastGuidToLastUpdateMap.end();
			// Determine if we need to update the mesh data
			if (bIsAdd || LastGuidToLastUpdateMap.at(Id) != UpdateTime)
			{
				// We need to resolve the mesh so we can allocate the memory to copy the vertices/indices
				SpatialSurfaceMesh^ SurfaceMesh = create_task(SurfaceInfo->TryComputeLatestMeshAsync(TriangleDensityPerCubicMeter, MeshOptions)).get();
				if (SurfaceMesh != nullptr)
				{
					CurrentMesh.NumVertices = SurfaceMesh->VertexPositions->ElementCount;
					CurrentMesh.NumIndices = SurfaceMesh->TriangleIndices->ElementCount;

					// Copy the transform over so it can be copied in the allocate buffers
					CopyTransform(CurrentMesh, SurfaceMesh);
					OnAllocateBuffers(&CurrentMesh);
					// Copy the mesh data into the buffers supplied by UE4
					CopyMeshData(CurrentMesh, SurfaceMesh);
				}
				else
				{
					// Failed to get the mesh so act as if we never saw it to trigger an add next time we see it
					CurrentGuidToLastUpdateMap.erase(Id);
				}
			}
			else
			{
				// This will tell the UE4 side that the mesh still exists, but there was no update
				// This will prevent the UE4 code from marking it as no longer tracked and remove it from its list
				OnAllocateBuffers(&CurrentMesh);
			}
		}
		OnFinishMeshUpdates();
		// Replace the last update map with the current one so we can ignore removals
		LastGuidToLastUpdateMap = CurrentGuidToLastUpdateMap;
	}
}

void MeshUpdateObserver::StartMeshObserver(
	float InTriangleDensity,
	float InVolumeSize,
	void(*StartFunctionPointer)(),
	void(*AllocFunctionPointer)(MeshUpdate*),
	void(*FinishFunctionPointer)()
)
{
	TriangleDensityPerCubicMeter = InTriangleDensity;
	VolumeSize = InVolumeSize;

	OnStartMeshUpdates = StartFunctionPointer;
	if (OnStartMeshUpdates == nullptr)
	{
		Log(L"Null start updates function pointer passed to StartMeshObserver(). Aborting.");
		return;
	}

	OnAllocateBuffers = AllocFunctionPointer;
	if (OnAllocateBuffers == nullptr)
	{
		Log(L"Null allocate buffers function pointer passed to StartMeshObserver(). Aborting.");
		return;
	}

	OnFinishMeshUpdates = FinishFunctionPointer;
	if (OnFinishMeshUpdates == nullptr)
	{
		Log(L"Null finish updates function pointer passed to StartMeshObserver(). Aborting.");
		return;
	}

	// If it's supported, request access
	if (SpatialSurfaceObserver::IsSupported())
	{
		auto RequestTask = concurrency::create_task(SpatialSurfaceObserver::RequestAccessAsync());
		RequestTask.then([this](SpatialPerceptionAccessStatus AccessStatus)
		{
			if (AccessStatus == SpatialPerceptionAccessStatus::Allowed)
			{
				std::lock_guard<std::mutex> lock(MeshRefsLock);
				SurfaceObserver = ref new SpatialSurfaceObserver();
				if (SurfaceObserver != nullptr)
				{
					bIsRunning = true;
					InitSupportedMeshFormats();
				}
				else
				{
					Log(L"Failed to create spatial observer. No updates will occur.");
				}
			}
			else
			{
				Log(L"User denied permission for spatial mapping. No updates will occur.");
			}
		});
	}
	else
	{
		Log(L"SpatialSurfaceObserver::IsSupported() returned false. No updates will occur.");
	}
}

void MeshUpdateObserver::UpdateBoundingVolume(Windows::Perception::Spatial::SpatialCoordinateSystem^ InCoordinateSystem, float3 Position)
{
	if (InCoordinateSystem == nullptr)
	{
		return;
	}

	// Either the observer hasn't been started yet or the user forgot to put the spatial mapping permission in
	if (SurfaceObserver == nullptr)
	{
		return;
	}

	SpatialBoundingBox AABB =
	{
		{ Position.x, Position.y, Position.z },
		{ VolumeSize, VolumeSize, VolumeSize }
	};

	SpatialBoundingVolume^ BoundingVolume = SpatialBoundingVolume::FromBox(InCoordinateSystem, AABB);
	SurfaceObserver->SetBoundingVolume(BoundingVolume);
	if (OnChangeEventToken.Value == 0)
	{
		// Subscribe to the changed mesh event now that we have a valid transform
		OnChangeEventToken = SurfaceObserver->ObservedSurfacesChanged +=
			ref new TypedEventHandler<SpatialSurfaceObserver^, Platform::Object^>(
				bind(&MeshUpdateObserver::OnSurfacesChanged, this, _1, _2)
				);
	}
	LastCoordinateSystem = InCoordinateSystem;
}

void MeshUpdateObserver::StopMeshObserver()
{
	std::lock_guard<std::mutex> lock(MeshRefsLock);
	if (SurfaceObserver != nullptr)
	{
		// Stop our callback from doing any processing first
		bIsRunning = false;
		SurfaceObserver->ObservedSurfacesChanged -= OnChangeEventToken;

		SurfaceObserver = nullptr;
		LastGuidToLastUpdateMap.erase(LastGuidToLastUpdateMap.begin(), LastGuidToLastUpdateMap.end());
	}
}

/** Logs out the formats that the API supports so we can request the one that matches ours best */
void MeshUpdateObserver::InitSupportedMeshFormats(void)
{
	MeshOptions = ref new SpatialSurfaceMeshOptions();
	// We have to recalc normals anyway, so skip
	MeshOptions->IncludeVertexNormals = false;
	MeshOptions->VertexPositionFormat = DirectXPixelFormat::R32G32B32A32Float;
	MeshOptions->TriangleIndexFormat = DirectXPixelFormat::R16UInt;

	IVectorView<DirectXPixelFormat>^ VertexFormats = MeshOptions->SupportedVertexPositionFormats;
	const unsigned int VFormatCount = VertexFormats->Size;
	for (unsigned int Index = 0; Index < VFormatCount; Index++)
	{
		DirectXPixelFormat Format = VertexFormats->GetAt(Index);
		std::wstringstream LogString;
		LogString << L"Vertex buffer supports DirectXPixelFormat[" << static_cast<int>(Format) << L"]";
		Log(LogString.str().c_str());
	}

	IVectorView<DirectXPixelFormat>^ IndexFormats = MeshOptions->SupportedTriangleIndexFormats;
	const unsigned int IFormatCount = IndexFormats->Size;
	for (unsigned int Index = 0; Index < IFormatCount; Index++)
	{
		DirectXPixelFormat Format = IndexFormats->GetAt(Index);
		std::wstringstream LogString;
		LogString << L"Index buffer supports DirectXPixelFormat[" << static_cast<int>(Format) << L"]";
		Log(LogString.str().c_str());
	}
}
