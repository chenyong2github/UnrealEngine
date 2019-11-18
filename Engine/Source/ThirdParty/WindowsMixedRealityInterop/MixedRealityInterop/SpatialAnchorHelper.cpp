// Copyright (c) Microsoft Corporation. All rights reserved.

// To use this lib in engines that do not build cppwinrt:
// WinRT headers and types must be in the cpp and not the header.

#include "stdafx.h"
#include "SpatialAnchorHelper.h"

#include "wrl/client.h"
#include "wrl/wrappers/corewrappers.h"

#include <Roapi.h>
#include <queue>

#include "winrt/Windows.Perception.h"
#include "winrt/Windows.Perception.Spatial.h"
#include "winrt/Windows.UI.Input.Spatial.h"
#include "winrt/Windows.Foundation.Numerics.h"

#include <HolographicSpaceInterop.h>
#include <SpatialInteractionManagerInterop.h>
#include <Windows.Graphics.Holographic.h>
#include <windows.ui.input.spatial.h>

#include <DXGI1_4.h>

#include "winrt/Windows.Graphics.Holographic.h"
#include "winrt/Windows.Graphics.DirectX.Direct3D11.h"
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>

// Remoting
#include <sstream>
//#include <ppltasks.h>


#pragma comment(lib, "OneCore")

using namespace Microsoft::WRL;
//using namespace Microsoft::Holographic;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Foundation::Numerics;

using namespace winrt::Windows::Devices::Haptics;
using namespace winrt::Windows::Graphics::Holographic;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::UI::Input::Spatial;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Perception;

namespace WindowsMixedReality
{
	SpatialAnchorHelper::SpatialAnchorHelper(WindowsMixedReality::MixedRealityInterop& interop, void(*logFunctionPointer)(const wchar_t*))
		: m_interop(interop)
		, m_logCallback(logFunctionPointer)
		, m_bCoordinateSystemChanged(false)
	{
//#if PLATFORM_HOLOLENS
		InitializeSpatialAnchorStore();
//#endif
	}

	SpatialAnchorHelper::~SpatialAnchorHelper()
	{
		if (m_spatialAnchorStoreAsyncOperation != nullptr && 
			m_spatialAnchorStoreAsyncOperation.Status() != winrt::Windows::Foundation::AsyncStatus::Completed)
		{
			Log(L"SpatialAnchorHelper ~SpatialAnchorHelper() canceling m_spatialAnchorStoreAsyncOperation");
			m_spatialAnchorStoreAsyncOperation.Cancel();
		}
		Log(L"SpatialAnchorHelper ~SpatialAnchorHelper() destructor complete");
	}

	void SpatialAnchorHelper::InitializeSpatialAnchorStore()
	{
		try
		{

			if (m_spatialAnchorStore != nullptr)
			{
				Log(L"m_spatialAnchorStore is not null, doing nothing.");
				return;
			}

			if (m_spatialAnchorStoreAsyncOperation != nullptr && 
				m_spatialAnchorStoreAsyncOperation.Status() == winrt::Windows::Foundation::AsyncStatus::Started)
			{
				Log(L"SpatialAnchorManager::RequestStoreAsync() has previously started, doing nothing.");
				return;
			}

			Log(L"InitializeSpatialAnchorStore started.");
			m_spatialAnchorStoreAsyncOperation = SpatialAnchorManager::RequestStoreAsync();
			{ std::wstringstream string; string << L"InitializeSpatialAnchorStore status = " << static_cast<INT32>(m_spatialAnchorStoreAsyncOperation.Status()); Log(string); }
#if FALSE
			m_spatialAnchorStore = m_spatialAnchorStoreAsyncOperation.get();
#else
			m_spatialAnchorStoreAsyncOperation.Completed([this, Log = m_logCallback](winrt::Windows::Foundation::IAsyncOperation<SpatialAnchorStore> asyncOperation, winrt::Windows::Foundation::AsyncStatus status)
			{
				if (asyncOperation.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
				{
					std::lock_guard<std::mutex> lock(m_spatialAnchorStoreLock);
					m_spatialAnchorStore = asyncOperation.GetResults();
					if (Log) Log(L"InitializeSpatialAnchorStore SpatialAnchorHelper RequestStoreAsync succeeded!");
				}
				else if (asyncOperation.Status() != winrt::Windows::Foundation::AsyncStatus::Canceled)
				{
					m_spatialAnchorStoreAsyncOperation = nullptr;
					if (Log) { std::wstringstream string; string << L"InitializeSpatialAnchorStore SpatialAnchorHelper RequestStoreAsync failed with status = " << static_cast<INT32>(asyncOperation.Status()); Log(string.str().c_str()); }
				}
				else if (asyncOperation.Status() == winrt::Windows::Foundation::AsyncStatus::Canceled)
				{
					m_spatialAnchorStoreAsyncOperation = nullptr;
					if (Log) Log(L"InitializeSpatialAnchorStore SpatialAnchorHelper RequestStoreAsync cancelled");
				}
			});
#endif
		}
		catch (winrt::hresult_error const&)
		{
			return ;
		}
	}

	bool SpatialAnchorHelper::IsSpatialAnchorStoreLoaded() const
	{
		if (m_spatialAnchorStoreAsyncOperation != nullptr)
		{
			{ std::wstringstream string; string << L"SpatialAnchorHelper IsSpatialAnchorStoreLoaded status = " << static_cast<INT32>(m_spatialAnchorStoreAsyncOperation.Status()); Log(string); }
		}
		std::lock_guard<std::mutex> lock(m_spatialAnchorStoreLock);
		return (m_spatialAnchorStore != nullptr);
	}

	bool SpatialAnchorHelper::CreateAnchor(const wchar_t* anchorId, const DirectX::XMFLOAT3 inPosition, DirectX::XMFLOAT4 inRotationQuat, winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& coordinateSystem)
	{
		try
		{
			if (m_spatialAnchorMap.count(anchorId) == 0)
			{
				winrt::Windows::Foundation::Numerics::float3 position(inPosition.x, inPosition.y, inPosition.z);
				winrt::Windows::Foundation::Numerics::quaternion rotationQuat(inRotationQuat.x, inRotationQuat.y, inRotationQuat.z, inRotationQuat.w);
				winrt::Windows::Perception::Spatial::SpatialAnchor newAnchor(SpatialAnchor::TryCreateRelativeTo(coordinateSystem, position, rotationQuat));
				if (newAnchor)
				{
					m_spatialAnchorMap.insert(std::make_pair(anchorId, newAnchor));
					{ std::wstringstream string; string << L"CreateAnchor: created " << anchorId; Log(string); }
					return true;
				}
				else
				{
					{ std::wstringstream string; string << L"CreateAnchor: failed to create " << anchorId; Log(string); }
					return false;
				}
			}
			else
			{
				{ std::wstringstream string; string << L"CreateAnchor: anchor " << anchorId << " already exists.  Not creating."; Log(string); }
				return false;
			}
		}
		catch (winrt::hresult_error const&)
		{
			return false;
		}
	}

	void SpatialAnchorHelper::RemoveAnchor(const wchar_t* anchorId)
	{
		if (m_spatialAnchorMap.count(anchorId) != 0)
		{
			{ std::wstringstream string; string << L"RemoveAnchor: removing " << anchorId; Log(string); }
			m_spatialAnchorMap.erase(anchorId);
		}
		else
		{
			{ std::wstringstream string; string << L"RemoveAnchor: anchor " << anchorId << " not found. Doing nothing."; Log(string); }
		}
	}

	bool SpatialAnchorHelper::DoesAnchorExist(const wchar_t* anchorId) const
	{
		return m_spatialAnchorMap.count(anchorId) != 0;
	}

	bool SpatialAnchorHelper::GetAnchorPose(const wchar_t* anchorId, DirectX::XMFLOAT3& outScale, DirectX::XMFLOAT4& outRot, DirectX::XMFLOAT3& outTrans, SpatialCoordinateSystem& inCoordinateSystem) const
	{
		try
		{
			if (m_spatialAnchorMap.count(anchorId) != 0)
			{
				auto anchorItr = m_spatialAnchorMap.find(anchorId);
				const SpatialAnchor& anchor = anchorItr->second;
				if (anchor == nullptr)
				{
					{ std::wstringstream string; string << L"GetAnchorPose: anchor " << anchorId << " == nullptr returning false."; Log(string); }
					return false;
				}
				const SpatialCoordinateSystem& anchorCoordinateSystem = anchorItr->second.CoordinateSystem();
				if (anchorCoordinateSystem == nullptr)
				{
					{ std::wstringstream string; string << L"GetAnchorPose: anchorCoordinateSystem for " << anchorId << " == nullptr returning false."; Log(string); }
					return false;
				}
				auto tryTransform = anchorCoordinateSystem.TryGetTransformTo(inCoordinateSystem);
				if (tryTransform != nullptr)
				{
					DirectX::XMMATRIX pose = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)&(tryTransform.Value()));
					DirectX::XMVECTOR scale;
					DirectX::XMVECTOR rot;
					DirectX::XMVECTOR trans;

					bool decomposed = DirectX::XMMatrixDecompose(&scale, &rot, &trans, pose);
					if (decomposed)
					{
						DirectX::XMStoreFloat3(&outScale, scale);
						DirectX::XMStoreFloat4(&outRot, rot);
						DirectX::XMStoreFloat3(&outTrans, trans);
						return true;
					}
					else
					{
						assert(false);
						return false;
					}
				}
			}
			return false;
		}
		catch (winrt::hresult_error const&)
		{
			return false;
		}
	}

	bool SpatialAnchorHelper::SaveAnchor(const wchar_t* anchorId)
	{
		try
		{
			{
				std::lock_guard<std::mutex> lock(m_spatialAnchorStoreLock);
				if (m_spatialAnchorStore == nullptr)
				{
					Log(L"SaveAnchor. Anchor store not ready.");
					return false;
				}
			}

			auto& iterator = m_spatialAnchorMap.find(anchorId);

			if (iterator == m_spatialAnchorMap.end())
			{
				{ std::wstringstream string; string << L"SaveAnchor: Saving failed because anchor " << anchorId << " does not exist."; Log(string); }

				return false;
			}

			// Failure may indicate the anchor ID is taken, or the anchor limit is reached for the app.
			bool success = m_spatialAnchorStore.TrySave(anchorId, iterator->second);
			{ std::wstringstream string; string << L"SaveAnchor: Saving " << anchorId << " success: " << success; Log(string); }
			return success;
		}
		catch (winrt::hresult_error const&)
		{
			return false;
		}
	}

	void SpatialAnchorHelper::RemoveSavedAnchor(const wchar_t* anchorId)
	{
		try
		{
			{
				std::lock_guard<std::mutex> lock(m_spatialAnchorStoreLock);
				if (m_spatialAnchorStore == nullptr)
				{
					Log(L"RemoveSavedAnchor. Anchor store not ready.");
					return;
				}
			}

			{ std::wstringstream string; string << L"RemoveSavedAnchor: Removing " << anchorId << " anchors."; Log(string); }

			m_spatialAnchorStore.Remove(anchorId);
		}
		catch (winrt::hresult_error const&)
		{
			return;
		}
	}

	bool SpatialAnchorHelper::SaveAnchors()
	{
		try
		{
			{
				std::lock_guard<std::mutex> lock(m_spatialAnchorStoreLock);
				if (m_spatialAnchorStore == nullptr)
				{
					Log(L"SaveAnchors. Anchor store not ready.");
					return false;
				}
			}

			Log(L"SaveAnchors.");

			// This function returns true if all the anchors in the in-memory collection are saved to the anchor
			// store. If zero anchors are in the in-memory collection, we will still return true because the
			// condition has been met.
			bool success = true;

			// If access is denied, 'anchorStore' will not be obtained.
			for (auto& pair : m_spatialAnchorMap)
			{
				//TODO currently we save all anchors to the store.
				// Maybe we only want to save some?  and others are run-time-only?

				// Try to save the anchors.
				if (!m_spatialAnchorStore.TrySave(pair.first, pair.second))
				{
					// This may indicate the anchor ID is taken, or the anchor limit is reached for the app.
					success = false;
				}
			}

			return success;
		}
		catch (winrt::hresult_error const&)
		{
			return false;
		}
	}

	bool SpatialAnchorHelper::LoadAnchors(std::function<void(const wchar_t* text)> anchorIdWritingFunctionPointer)
	{
		try
		{
			{
				std::lock_guard<std::mutex> lock(m_spatialAnchorStoreLock);
				if (m_spatialAnchorStore == nullptr)
				{
					Log(L"LoadAnchors. Anchor store not ready.");
					return false;
				}
			}

			Log(L"LoadAnchors. Loading...");

			// Get all saved anchors.
			auto anchorMapView = m_spatialAnchorStore.GetAllSavedAnchors();
			for (auto const& pair : anchorMapView)
			{
				{ std::wstringstream string; string << L"LoadAnchors: Loading anchor " << pair.Key().c_str(); Log(string); }

				// On WRT hmd currently we get a guid prepended to the anchor name.  We will strip that off.
				winrt::hstring Key = pair.Key();
				std::wstring_view KeyView = Key;
				size_t Pos = KeyView.find_last_of(L"==");
				if (Pos != std::wstring_view::npos)
				{
					size_t RealNameStart = Pos + 1;
					size_t RealNameEnd = KeyView.size() - 1;
					assert(Pos < RealNameEnd);
					KeyView = KeyView.substr(RealNameStart, RealNameEnd);
					{ std::wstringstream string; string << L"LoadAnchors: Stripping key to " << KeyView.data(); Log(string); }
				}

				// If we already have an anchor of this key overwrite it with the saved one.
				if (m_spatialAnchorMap.count(KeyView.data()) != 0)
				{
					{ std::wstringstream string; string << L"LoadAnchors:   Overwriting"; Log(string); }
					m_spatialAnchorMap.erase(KeyView.data());
				}
				m_spatialAnchorMap.insert(std::make_pair(KeyView.data(), pair.Value()));
				SubscribeToRawCoordinateSystemAdjusted(pair.Value(), KeyView.data());
				anchorIdWritingFunctionPointer(KeyView.data());
			}

			{ std::wstringstream string; string << L"LoadAnchors: Loaded " << anchorMapView.Size() << " anchors."; Log(string); }

			return true;
		}
		catch (winrt::hresult_error const&)
		{
			return false;
		}
	}

	void SpatialAnchorHelper::ClearSavedAnchors()
	{
		try
		{
			{
				std::lock_guard<std::mutex> lock(m_spatialAnchorStoreLock);
				if (m_spatialAnchorStore == nullptr)
				{
					Log(L"ClearSavedAnchors. Anchor store not ready.");
					return;
				}
			}
		
			Log(L"ClearSavedAnchors: Clearing.");
			m_spatialAnchorStore.Clear();
		}
		catch (winrt::hresult_error const&)
		{
			return ;
		}
	}

	void SpatialAnchorHelper::OnRawCoordinateSystemAdjusted(
		const SpatialAnchor& anchor,
		const SpatialAnchorRawCoordinateSystemAdjustedEventArgs& args,
		const wchar_t* strName)
	{
//		{ std::wstringstream string; string << L"OnRawCoordinateSystemAdjusted anchor " << strName << " changed"; Log(string); }

		m_bCoordinateSystemChanged = true;
	}

	void SpatialAnchorHelper::SubscribeToRawCoordinateSystemAdjusted(SpatialAnchor& anchor, const wchar_t* strName)
	{
		{ std::wstringstream string; string << L"SubscribeToRawCoordinateSystemAdjusted: Registering event for " << strName; Log(string); }

		anchor.RawCoordinateSystemAdjusted(
			[=](const SpatialAnchor& anchor, const SpatialAnchorRawCoordinateSystemAdjustedEventArgs& args)
		{
			OnRawCoordinateSystemAdjusted(anchor, args, strName);
		});
	}

	bool SpatialAnchorHelper::DidAnchorCoordinateSystemChange()
	{
		if (m_bCoordinateSystemChanged)
		{
			m_bCoordinateSystemChanged = false;
			return true;
		}

		return false;
	}

	void SpatialAnchorHelper::SetLogCallback(void(*functionPointer)(const wchar_t*))
	{
		m_logCallback = functionPointer;
	}

	void SpatialAnchorHelper::Log(const wchar_t* text) const
	{
		if (m_logCallback)
		{
			m_logCallback(text);
		}
	}

	void SpatialAnchorHelper::Log(std::wstringstream& stream) const
	{
		Log(stream.str().c_str());
	}

}
