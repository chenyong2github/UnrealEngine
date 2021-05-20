// Copyright Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"
#include "MixedRealityInterop.h"

#include "MeshObserver.h"
#include "FastConversion.h"

#include <streambuf>
#include <robuffer.h>

#include <WindowsNumerics.h>
#include <winrt/windows.foundation.numerics.h>
#include <winrt/windows.foundation.Collections.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <map>
#include <string>
#include <sstream>
#include <mutex>

#include <DirectXMath.h>
#include <DirectXPackedVector.h>


using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;


using namespace DirectX;


/** Controls access to our references */
std::mutex MeshRefsLock;
bool bIsRunning = false;
bool bIsStopping = false;
SpatialSurfaceObserver SurfaceObserver = nullptr;
event_token OnChangeEventToken;
std::map<guid, long long> LastGuidToLastUpdateMap;

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

void MeshUpdateObserver::CopyMeshData(MeshUpdate& DestMesh, SpatialSurfaceMesh SurfaceMesh)
{
	PackedVector::XMSHORTN4* RawVertices = reinterpret_cast<PackedVector::XMSHORTN4*>(SurfaceMesh.VertexPositions().Data().data());
	short* RawIndices = reinterpret_cast<short*>(SurfaceMesh.TriangleIndices().Data().data());
	int VertexCount = DestMesh.NumVertices;
	float* DestVertices = (float*)DestMesh.Vertices;

	for (int Index = 0; Index < VertexCount; Index++)
	{
		// Match alignment with MeshOptions->VertexPositionFormat
		PackedVector::XMSHORTN4 packedSource = RawVertices[Index];
		XMVECTOR Source = PackedVector::XMLoadShortN4(&packedSource);
		XMFLOAT3 Dest = ToUE4Translation(Source);

		DestVertices[0] = Dest.x;
		DestVertices[1] = Dest.y;
		DestVertices[2] = Dest.z;
		DestVertices += 3;
	}

	int TriangleCount = DestMesh.NumIndices / 3;
// The DestIndices format in UE4 is defined by MRMESH_INDEX_TYPE.  Depending on platform it may be 16 or 32 bits.  This needs to match that.
#if PLATFORM_HOLOLENS
	short* DestIndices = (short*)DestMesh.Indices; 
#else
	uint32_t* DestIndices = (uint32_t*)DestMesh.Indices;
#endif
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

void MeshUpdateObserver::CopyTransform(MeshUpdate& DestMesh, SpatialSurfaceMesh SurfaceMesh)
{
	XMMATRIX ConvertTransform = XMMatrixIdentity();
	auto MeshTransform = SurfaceMesh.CoordinateSystem().TryGetTransformTo(LastCoordinateSystem);
	if (MeshTransform != nullptr)
	{
		ConvertTransform = XMLoadFloat4x4(&MeshTransform.Value());
	}

	XMVECTOR MeshScale = XMLoadFloat3(&SurfaceMesh.VertexPositionScale());

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

void MeshUpdateObserver::OnSurfacesChanged(SpatialSurfaceObserver Observer, winrt::Windows::Foundation::IInspectable)
{
	// Serialize updates from this side
	std::lock_guard<std::mutex> lock(MeshRefsLock);
	if (bIsRunning)
	{
		OnStartMeshUpdates();
		std::map<guid, long long> CurrentGuidToLastUpdateMap;
		// Get the set of meshes that changed
		auto SurfaceCollection = Observer.GetObservedSurfaces();
		for (auto Iter = SurfaceCollection.First(); Iter.HasCurrent(); Iter.MoveNext())
		{
			auto KVPair = Iter.Current();

			guid Id = KVPair.Key();
			SpatialSurfaceInfo SurfaceInfo = KVPair.Value();
			long long UpdateTime = SurfaceInfo.UpdateTime().time_since_epoch().count();
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
				auto SurfaceMesh = SurfaceInfo.TryComputeLatestMeshAsync(TriangleDensityPerCubicMeter, MeshOptions).get();
				if (SurfaceMesh != nullptr)
				{
					CurrentMesh.NumVertices = SurfaceMesh.VertexPositions().ElementCount();
					CurrentMesh.NumIndices = SurfaceMesh.TriangleIndices().ElementCount();

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
				OnAllocateBuffers(&CurrentMesh);
			}
		}
		// Iterate through the last known guids to find ones that have been removed
		for (const auto& [Key, Value] : LastGuidToLastUpdateMap)
		{
			guid Id = Key;
			// If this one is not in the new set, then it was removed
			if (CurrentGuidToLastUpdateMap.find(Id) == CurrentGuidToLastUpdateMap.end())
			{
				MeshUpdate RemovedMesh;
				RemovedMesh.Id = Id;
				OnRemovedMesh(&RemovedMesh);
			}
		}
		OnFinishMeshUpdates();
		// Replace the last update map with the current one so we can ignore removals
		LastGuidToLastUpdateMap = CurrentGuidToLastUpdateMap;
	}
}

bool MeshUpdateObserver::StartMeshObserver(
	float InTriangleDensity,
	float InVolumeSize,
	void(*StartFunctionPointer)(),
	void(*AllocFunctionPointer)(MeshUpdate*),
	void(*RemovedMeshPointer)(MeshUpdate*),
	void(*FinishFunctionPointer)()
)
{
	bIsStopping = false;
	TriangleDensityPerCubicMeter = InTriangleDensity;
	VolumeSize = InVolumeSize;

	OnStartMeshUpdates = StartFunctionPointer;
	if (OnStartMeshUpdates == nullptr)
	{
		Log(L"Null start updates function pointer passed to StartMeshObserver(). Aborting.");
		return false;
	}

	OnAllocateBuffers = AllocFunctionPointer;
	if (OnAllocateBuffers == nullptr)
	{
		Log(L"Null allocate buffers function pointer passed to StartMeshObserver(). Aborting.");
		return false;
	}

	OnRemovedMesh = RemovedMeshPointer;
	if (OnRemovedMesh == nullptr)
	{
		Log(L"Null removed mesh function pointer passed to StartMeshObserver(). Aborting.");
		return false;
	}

	OnFinishMeshUpdates = FinishFunctionPointer;
	if (OnFinishMeshUpdates == nullptr)
	{
		Log(L"Null finish updates function pointer passed to StartMeshObserver(). Aborting.");
		return false;
	}

	// If it's supported, request access
	if (SpatialSurfaceObserver::IsSupported())
	{
		//auto RequestTask = concurrency::create_task(SpatialSurfaceObserver::RequestAccessAsync());
		//RequestTask.then([this](SpatialPerceptionAccessStatus AccessStatus)
		SpatialSurfaceObserver::RequestAccessAsync().Completed([=](auto&& asyncInfo, auto&&  asyncStatus)
		{
			if (asyncInfo.GetResults() == SpatialPerceptionAccessStatus::Allowed)
			{
				std::lock_guard<std::mutex> lock(MeshRefsLock);
				if (bIsStopping)
				{
					bIsStopping = false;
					return;
				}

				SurfaceObserver = SpatialSurfaceObserver();
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

		return true;
	}
	else
	{
		Log(L"SpatialSurfaceObserver::IsSupported() returned false. No updates will occur.");
		return false;
	}
}

void MeshUpdateObserver::UpdateBoundingVolume(SpatialCoordinateSystem InCoordinateSystem, float3 Position)
{
	std::lock_guard<std::mutex> lock(MeshRefsLock);

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

	SpatialBoundingVolume BoundingVolume = SpatialBoundingVolume::FromBox(InCoordinateSystem, AABB);
	SurfaceObserver.SetBoundingVolume(BoundingVolume);
	if (!OnChangeEventToken)
	{
		// Subscribe to the changed mesh event now that we have a valid transform
		OnChangeEventToken = SurfaceObserver.ObservedSurfacesChanged([this](auto&& sender, auto&& obj) { OnSurfacesChanged(sender, obj); });
	}
	LastCoordinateSystem = InCoordinateSystem;
}

void MeshUpdateObserver::StopMeshObserver()
{
	std::lock_guard<std::mutex> lock(MeshRefsLock);
	bIsStopping = true;

	if (SurfaceObserver != nullptr)
	{
		// Stop our callback from doing any processing first
		bIsRunning = false;
		SurfaceObserver.ObservedSurfacesChanged(OnChangeEventToken);
		OnChangeEventToken = event_token();

		SurfaceObserver = nullptr;
		LastGuidToLastUpdateMap.erase(LastGuidToLastUpdateMap.begin(), LastGuidToLastUpdateMap.end());
	}
}

/** Logs out the formats that the API supports so we can request the one that matches ours best */
void MeshUpdateObserver::InitSupportedMeshFormats(void)
{
	MeshOptions = SpatialSurfaceMeshOptions();
	// We have to recalc normals anyway, so skip
	MeshOptions.IncludeVertexNormals(false);
	MeshOptions.VertexPositionFormat(DirectXPixelFormat::R16G16B16A16IntNormalized);
	MeshOptions.TriangleIndexFormat(DirectXPixelFormat::R16UInt);

	auto VertexFormats = MeshOptions.SupportedVertexPositionFormats();
	const unsigned int VFormatCount = VertexFormats.Size();
	for (unsigned int Index = 0; Index < VFormatCount; Index++)
	{
		DirectXPixelFormat Format = VertexFormats.GetAt(Index);
		std::wstringstream LogString;
		LogString << L"Vertex buffer supports DirectXPixelFormat[" << static_cast<int>(Format) << L"]";
		Log(LogString.str().c_str());
	}

	auto IndexFormats = MeshOptions.SupportedTriangleIndexFormats();
	const unsigned int IFormatCount = IndexFormats.Size();
	for (unsigned int Index = 0; Index < IFormatCount; Index++)
	{
		DirectXPixelFormat Format = IndexFormats.GetAt(Index);
		std::wstringstream LogString;
		LogString << L"Index buffer supports DirectXPixelFormat[" << static_cast<int>(Format) << L"]";
		Log(LogString.str().c_str());
	}
}
