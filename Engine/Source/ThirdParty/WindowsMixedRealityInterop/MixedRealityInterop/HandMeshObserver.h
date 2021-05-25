#pragma once

#include "MixedRealityInterop.h"


#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Perception.Spatial.Surfaces.h>
#include <winrt/Windows.UI.Input.Spatial.h>

#include <mutex>

using namespace winrt::Windows::UI::Input::Spatial;
using namespace winrt::Windows::Perception::People;
using namespace winrt::Windows::Perception::Spatial;
using namespace Platform;

namespace WindowsMixedReality
{
	class HandMeshUpdateObserver
	{
	public:
		HandMeshUpdateObserver();

		void InitAsync(SpatialInteractionSource source);
		bool IsReady() const
		{
			std::lock_guard<std::recursive_mutex> lock(mutex);
			return m_isReady;
		}

		static bool IsInitialized()
		{
			return s_OnStartMeshUpdates != nullptr && s_OnAllocateBuffers != nullptr && s_OnFinishMeshUpdates != nullptr;
		}

		void Update(HandPose pose, SpatialCoordinateSystem coordinateSystem, bool isRightHand);

		static void InitStatic(std::function<void()> OnStartMeshUpdates, std::function<void(MeshUpdate*)> OnAllocateBuffers, std::function<void()> OnFinishMeshUpdates);

		uint32_t GetSourceId() { return m_sourceId; }
	private:
		void CopyMeshData(MeshUpdate& DestMesh, HandMeshVertexState VertexState);

		HandMeshObserver m_HandMeshObserver = nullptr;
		HMDHand m_handness = HMDHand::AnyHand;
		uint32_t m_sourceId = 0;
		bool m_isReady = false;

		// The DestIndices format in UE4 is defined by MRMESH_INDEX_TYPE.  Depending on platform it may be 16 or 32 bits.  This needs to match that.
#if PLATFORM_HOLOLENS
		std::vector<short> m_indices;
#else
		std::vector<uint32_t> m_indices;
#endif
		std::vector<HandMeshVertex> m_vertices;

		GUID m_guid;

		mutable std::recursive_mutex mutex;

		/** Function pointer for telling UE4 to prepare for updates */
		static std::function<void()> s_OnStartMeshUpdates;
		/** Function pointer for asking UE4 to allocate buffers (avoids an extra copy) */
		static std::function<void(MeshUpdate*)> s_OnAllocateBuffers;
		/** Function pointer for telling UE4 updates have completed */
		static std::function<void()> s_OnFinishMeshUpdates;
	};

}
