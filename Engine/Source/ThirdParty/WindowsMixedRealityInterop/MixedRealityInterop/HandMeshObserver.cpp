#include "stdafx.h"
#include "HandMeshObserver.h"
#include "FastConversion.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Perception.People.h>


#include <DirectXMath.h>


using namespace DirectX;

namespace WindowsMixedReality
{
	std::function<void()> HandMeshUpdateObserver::s_OnStartMeshUpdates;
	std::function<void(MeshUpdate*)> HandMeshUpdateObserver::s_OnAllocateBuffers;
	std::function<void()> HandMeshUpdateObserver::s_OnFinishMeshUpdates;

	HandMeshUpdateObserver::HandMeshUpdateObserver()
	{
		CoCreateGuid(&m_guid);
	}

	void HandMeshUpdateObserver::InitAsync(SpatialInteractionSource source)
	{
		if (!IsInitialized())
		{
			return;
		}

		std::lock_guard<std::recursive_mutex> lock(mutex);
		if (!source)
		{
			m_isReady = false;
			m_HandMeshObserver = nullptr;
			m_sourceId = 0;
			return;
		}

		m_sourceId = source.Id();
		switch (source.Handedness())
		{
		case SpatialInteractionSourceHandedness::Left:
			m_handness = HMDHand::Left;
			break;
		case SpatialInteractionSourceHandedness::Right:
			m_handness = HMDHand::Right;
			break;
		default:
			break;
		}
		m_isReady = false;
		m_HandMeshObserver = nullptr;

		try
		{
			source.TryCreateHandMeshObserverAsync().Completed([this](auto&& result, auto&& status)
				{
					if (status == winrt::Windows::Foundation::AsyncStatus::Completed)
					{
						std::lock_guard<std::recursive_mutex> lock(mutex);
						m_HandMeshObserver = result.GetResults();
						if (!m_HandMeshObserver)
						{
							m_sourceId = 0;
							return;
						}
						//allocate buffers and load indices
						std::vector<uint16_t> RawIndicesVect;
						RawIndicesVect.resize(m_HandMeshObserver.TriangleIndexCount());
						m_HandMeshObserver.GetTriangleIndices(RawIndicesVect);

						auto RawIndices = RawIndicesVect.data();
						int TriangleCount = (int)RawIndicesVect.size() / 3;
						m_indices.resize(RawIndicesVect.size());
						auto DestIndices = m_indices.data();
						// Reverse triangle order
						for (int Index = 0; Index < TriangleCount; Index++)
						{
							DestIndices[0] = RawIndices[2];
							DestIndices[1] = RawIndices[1];
							DestIndices[2] = RawIndices[0];
							DestIndices += 3;
							RawIndices += 3;
						}

						m_vertices.resize(m_HandMeshObserver.VertexCount());
						m_isReady = true;
					}
				});
		}
		catch (std::exception const& ex)
		{
			UNREFERENCED_PARAMETER(ex);
		}
		catch (winrt::hresult_error const& ex)
		{
			UNREFERENCED_PARAMETER(ex);
		}
	}

	void CopyTransform(MeshUpdate& DestMesh, HandMeshVertexState VertexState, SpatialCoordinateSystem coordinateSystem)
	{
		XMMATRIX ConvertTransform = XMMatrixIdentity();
		auto MeshTransform = VertexState.CoordinateSystem().TryGetTransformTo(coordinateSystem);
		if (MeshTransform != nullptr)
		{
			ConvertTransform = XMLoadFloat4x4(&MeshTransform.Value());
		}

		ConvertTransform = ConvertTransform;

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

	void HandMeshUpdateObserver::CopyMeshData(MeshUpdate& DestMesh, HandMeshVertexState VertexState)
	{
		VertexState.GetVertices(m_vertices);
		float* DestVertices = (float*)DestMesh.Vertices;
		float* DestNormals = (float*)DestMesh.Normals;

		for (auto&& v : m_vertices)
		{
			XMVECTOR SourceVertex = XMLoadFloat3(&v.Position);
			XMVECTOR SourceNormal = XMLoadFloat3(&v.Normal);
			XMFLOAT3 DestVertex = ToUE4Translation(SourceVertex);
			XMFLOAT3 DestNormal = ToUE4Translation(SourceNormal);

			DestVertices[0] = DestVertex.x;
			DestVertices[1] = DestVertex.y;
			DestVertices[2] = DestVertex.z;
			DestVertices += 3;

			DestNormals[0] = DestNormal.x;
			DestNormals[1] = DestNormal.y;
			DestNormals[2] = DestNormal.z;
			DestNormals += 3;
		}

#if PLATFORM_HOLOLENS
		short* indices = (short*)DestMesh.Indices;
#else
		uint32_t* indices = (uint32_t*)DestMesh.Indices;
#endif
		memcpy(DestMesh.Indices, m_indices.data(), m_indices.size() * sizeof(m_indices[0]));
	}

	void HandMeshUpdateObserver::Update(HandPose pose, SpatialCoordinateSystem coordinateSystem, bool isRightHand)
	{
		if (!IsInitialized())
		{
			return;
		}

		if (!IsReady())
		{
			return;
		}

		std::lock_guard<std::recursive_mutex> lock(mutex);
		auto VertexState = m_HandMeshObserver.GetVertexStateForPose(pose);

		s_OnStartMeshUpdates();

		MeshUpdate CurrentMesh;
		CurrentMesh.Id = m_guid;
		CurrentMesh.Type = MeshUpdate::Hand;

		CurrentMesh.NumVertices = (int)m_vertices.size();
		CurrentMesh.NumIndices = (int)m_indices.size();
		CurrentMesh.NumNormals = (int)m_vertices.size();
		CurrentMesh.IsRightHandMesh = isRightHand;

		// Copy the transform over so it can be copied in the allocate buffers
		CopyTransform(CurrentMesh, VertexState, coordinateSystem);
		s_OnAllocateBuffers(&CurrentMesh);
		// Copy the mesh data into the buffers supplied by UE4
		CopyMeshData(CurrentMesh, VertexState);

		s_OnFinishMeshUpdates();
	}


	void HandMeshUpdateObserver::InitStatic(std::function<void()> OnStartMeshUpdates, std::function<void(MeshUpdate*)> OnAllocateBuffers, std::function<void()> OnFinishMeshUpdates)
	{
		s_OnStartMeshUpdates = OnStartMeshUpdates;
		s_OnAllocateBuffers = OnAllocateBuffers;
		s_OnFinishMeshUpdates = OnFinishMeshUpdates;
	}

}