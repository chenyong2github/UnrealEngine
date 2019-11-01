// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once
#pragma warning(disable:4668)
#pragma warning(disable:4005)  

#include <DirectXMath.h>
#pragma warning(default:4005)
#pragma warning(default:4668)

//#include <sstream>
#include <map>
#include <functional>
#include "winrt/base.h"
#include "winrt/Windows.Perception.Spatial.h"


namespace WindowsMixedReality
{
	class SpatialAnchorHelper
	{
	public:
		SpatialAnchorHelper(class MixedRealityInterop& interop, void(*logFunctionPointer)(const wchar_t*));
		~SpatialAnchorHelper();

		void InitializeSpatialAnchorStore();
		bool IsSpatialAnchorStoreLoaded() const;
		bool CreateAnchor(const wchar_t* anchorId, const DirectX::XMFLOAT3 position, DirectX::XMFLOAT4 rotationQuat, winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& coordinateSystem);
		void RemoveAnchor(const wchar_t* anchorId);
		bool DoesAnchorExist(const wchar_t* anchorId) const;
		bool GetAnchorPose(const wchar_t* anchorId, DirectX::XMFLOAT3& outScale, DirectX::XMFLOAT4& outRot, DirectX::XMFLOAT3& outTrans, winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& inCoordinateSystem) const;
		bool SaveAnchor(const wchar_t* anchorId);
		void RemoveSavedAnchor(const wchar_t* anchorId);
		bool SaveAnchors();
		bool LoadAnchors(std::function<void(const wchar_t* text)> anchorIdWritingFunctionPointer);
		void ClearSavedAnchors();
		void SubscribeToRawCoordinateSystemAdjusted(winrt::Windows::Perception::Spatial::SpatialAnchor& anchor, const wchar_t* strName);
		bool DidAnchorCoordinateSystemChange();
		void OnRawCoordinateSystemAdjusted(const winrt::Windows::Perception::Spatial::SpatialAnchor& anchor, const winrt::Windows::Perception::Spatial::SpatialAnchorRawCoordinateSystemAdjustedEventArgs& args, const wchar_t* strName);

		void SetLogCallback(void(*functionPointer)(const wchar_t* text));

	private:
		void Log(const wchar_t* text) const;
		void Log(class std::wstringstream& stream) const;
		void(*m_logCallback)(const wchar_t*) = nullptr;

		class MixedRealityInterop&														m_interop;
		std::map<std::wstring, winrt::Windows::Perception::Spatial::SpatialAnchor>		m_spatialAnchorMap;
		mutable std::mutex																m_spatialAnchorStoreLock;
		winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Perception::Spatial::SpatialAnchorStore>		m_spatialAnchorStoreAsyncOperation;
		winrt::Windows::Perception::Spatial::SpatialAnchorStore							m_spatialAnchorStore{ nullptr };
		winrt::event_token																m_spatialAnchorStoreLoadedEvent;

		bool																			m_bCoordinateSystemChanged = false;
	};
}

