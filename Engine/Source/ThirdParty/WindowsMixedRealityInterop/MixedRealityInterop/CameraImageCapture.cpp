// Copyright Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"
#include "MixedRealityInterop.h"

#include <WindowsNumerics.h>
#include <winrt/windows.foundation.numerics.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/Windows.Media.Capture.Frames.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.Media.Devices.Core.h>
#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Perception.Spatial.Surfaces.h>

#include <string>
#include <sstream>
#include <mutex>

#include <DXGI1_4.h>
#include <Windows.Perception.Spatial.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>

using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Perception::Spatial::Surfaces;

using namespace winrt::Windows::Media::Capture;
using namespace winrt::Windows::Media::Capture::Frames;

/** Controls access to our references */
std::mutex RefsLock;
/** The objects we need in order to receive frames of camera data */
winrt::agile_ref<MediaCapture> CameraCapture = nullptr;
MediaFrameReader CameraFrameReader = nullptr;
MediaFrameSource CameraFrameSource = nullptr;
winrt::Windows::Media::Devices::Core::CameraIntrinsics CameraIntrinsics = nullptr;

CameraImageCapture* CameraImageCapture::CaptureInstance = nullptr;

CameraImageCapture::CameraImageCapture()
	: OnLog(nullptr)
	, OnReceivedFrame(nullptr)
{
}

CameraImageCapture::~CameraImageCapture()
{
}

CameraImageCapture& CameraImageCapture::Get()
{
	if (CaptureInstance == nullptr)
	{
		CaptureInstance = new CameraImageCapture();
	}
	return *CaptureInstance;
}

void CameraImageCapture::Release()
{
	if (CaptureInstance != nullptr)
	{
		CaptureInstance->StopCameraCapture();
		delete CaptureInstance;
		CaptureInstance = nullptr;
	}
}

void CameraImageCapture::SetOnLog(void(*FunctionPointer)(const wchar_t* LogMsg))
{
	OnLog = FunctionPointer;
}


void CameraImageCapture::Log(const wchar_t* LogMsg)
{
	if (OnLog != nullptr)
	{
		OnLog(LogMsg);
	}
}

bool CameraImageCapture::GetCameraIntrinsics(DirectX::XMFLOAT2& focalLength, int& width, int& height, DirectX::XMFLOAT2& principalPoint, DirectX::XMFLOAT3& radialDistortion, DirectX::XMFLOAT2& tangentialDistortion)
{
	if (CameraIntrinsics == nullptr)
	{
		return false;
	}

	focalLength = DirectX::XMFLOAT2(CameraIntrinsics.FocalLength().x, CameraIntrinsics.FocalLength().y);
	width = CameraIntrinsics.ImageWidth();
	height = CameraIntrinsics.ImageHeight();
	principalPoint = DirectX::XMFLOAT2(CameraIntrinsics.PrincipalPoint().x, CameraIntrinsics.PrincipalPoint().y);
	radialDistortion = DirectX::XMFLOAT3(CameraIntrinsics.RadialDistortion().x, CameraIntrinsics.RadialDistortion().y, CameraIntrinsics.RadialDistortion().z);
	tangentialDistortion = DirectX::XMFLOAT2(CameraIntrinsics.TangentialDistortion().x, CameraIntrinsics.TangentialDistortion().y);

	return true;
}

DirectX::XMFLOAT2 CameraImageCapture::UnprojectPVCamPointAtUnitDepth(DirectX::XMFLOAT2 pixelCoordinate)
{
	if (CameraIntrinsics == nullptr)
	{
		return DirectX::XMFLOAT2(pixelCoordinate.x, pixelCoordinate.y);
	}

	winrt::Windows::Foundation::Point point;
	point.X = pixelCoordinate.x;
	point.Y = pixelCoordinate.y;
	float2 unprojected = CameraIntrinsics.UnprojectAtUnitDepth(point);
	
	return DirectX::XMFLOAT2(unprojected.x, unprojected.y);
}

template <typename T>
T convert_from_abi(::IUnknown* from)
{
	T to{ nullptr }; // `T` is a projected type.

	winrt::check_hresult(from->QueryInterface(winrt::guid_of<T>(),
		winrt::put_abi(to)));

	return to;
}


/** Used to keep from leaking WinRT types to the header file, so this forwards to the real handler */
void OnFrameRecevied(MediaFrameReader SendingFrameReader, MediaFrameArrivedEventArgs FrameArrivedArgs)
{
	if (MediaFrameReference CurrentFrame = SendingFrameReader.TryAcquireLatestFrame())
	{
		CameraImageCapture& CaptureInstance = CameraImageCapture::Get();

		// Drill down through the objects to get the underlying D3D texture
		VideoMediaFrame VideoFrame = CurrentFrame.VideoMediaFrame();
		auto ManagedSurface = VideoFrame.Direct3DSurface();

		if (ManagedSurface == nullptr)
		{
			CaptureInstance.Log(L"OnFrameRecevied() : VideoMediaFrame->Direct3DSurface was null, so no image to process");
			return;
		}

		// Get camera intrinsics, since we just have the one camera, cache the intrinsics.
		if (CameraIntrinsics == nullptr)
		{
			CameraIntrinsics = VideoFrame.CameraIntrinsics();
		}

		// Find current frame's tracking information from the frame's coordinate system.
		SpatialCoordinateSystem CameraCoordinateSystem = CurrentFrame.CoordinateSystem();

		DirectX::XMFLOAT4X4 cameraToTracking = DirectX::XMFLOAT4X4();
		if (CameraCoordinateSystem != nullptr)
		{
			// Get CX coordinate system from ABI tracking coordinate system.
			winrt::com_ptr<ABI::Windows::Perception::Spatial::ISpatialCoordinateSystem> TrackingCoordinateSystemABI;
			winrt::Windows::Perception::Spatial::SpatialCoordinateSystem TrackingCoordinateSystemWinRT = nullptr;

			if (WindowsMixedReality::MixedRealityInterop::QueryCoordinateSystem(*TrackingCoordinateSystemABI.put()))
			{
				TrackingCoordinateSystemWinRT = convert_from_abi<winrt::Windows::Perception::Spatial::SpatialCoordinateSystem>((::IUnknown*)TrackingCoordinateSystemABI.get());
				if (TrackingCoordinateSystemWinRT != nullptr)
				{
					auto cameraToTrackingRT = CameraCoordinateSystem.TryGetTransformTo(TrackingCoordinateSystemWinRT);
					if (cameraToTrackingRT != nullptr)
					{
						float4x4 m = cameraToTrackingRT.Value();

						// Load winrt transform from cx input
						cameraToTracking = DirectX::XMFLOAT4X4(
							m.m11, m.m12, m.m13, m.m14,
							m.m21, m.m22, m.m23, m.m24,
							m.m31, m.m32, m.m33, m.m34,
							m.m41, m.m42, m.m43, m.m44);
					}
				}
			}
		}

		winrt::com_ptr<IDXGIResource1> srcResource = nullptr;
		winrt::com_ptr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> DxgiInterfaceAccess =
			ManagedSurface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
		if (DxgiInterfaceAccess)
		{
			DxgiInterfaceAccess->GetInterface(IID_PPV_ARGS(srcResource.put()));
		}
		else
		{
			CaptureInstance.Log(L"OnFrameRecevied() : Failed to get DxgiInterfaceAccess from ManagedSurface.  Cannot process image.");
			return;
		}

		if (srcResource != nullptr)
		{
			HANDLE sharedHandle = NULL;
			srcResource->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ, NULL, &sharedHandle);

			CaptureInstance.NotifyReceivedFrame(sharedHandle, cameraToTracking);
		}
		else
		{
			std::wstringstream LogString;
			LogString << L"Unable to get the underlying video texture";
			CaptureInstance.Log(LogString.str().c_str());
		}

		ManagedSurface = nullptr;
	}
}

void CameraImageCapture::NotifyReceivedFrame(void* handle, DirectX::XMFLOAT4X4 CamToTracking)
{
	std::lock_guard<std::mutex> lock(RefsLock);
	if (OnReceivedFrame == nullptr)
	{
		if (handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
		}
		return;
	}

	// Pass the D3D texture handle to the UE4 code via the function pointer
	OnReceivedFrame(handle, CamToTracking);
}

bool CameraImageCapture::StartCameraCapture(void(*FunctionPointer)(void*, DirectX::XMFLOAT4X4), int DesiredWidth, int DesiredHeight, int DesiredFPS)
{
	if (CameraFrameReader)
	{
		Log(L"Camera is already capturing frames. Aborting.");
		return true;
	}

	OnReceivedFrame = FunctionPointer;
	if (OnReceivedFrame == nullptr)
	{
		Log(L"Null function pointer passed to StartCameraCapture() for new image callbacks. Aborting.");
		return false;
	}

	MediaFrameSourceGroup::FindAllAsync().Completed([this, DesiredWidth, DesiredHeight, DesiredFPS](auto&& asyncInfo, auto&&  asyncStatus)
	{
		auto DiscoveredGroups = asyncInfo.GetResults();
		MediaFrameSourceGroup ChosenSourceGroup = nullptr;
		MediaFrameSourceInfo ChosenSourceInfo = nullptr;

		MediaCaptureInitializationSettings CaptureSettings = MediaCaptureInitializationSettings();
		CaptureSettings.StreamingCaptureMode(StreamingCaptureMode::Video);
		CaptureSettings.MemoryPreference(MediaCaptureMemoryPreference::Auto); // For GPU
		CaptureSettings.VideoProfile(nullptr);

		const unsigned int DiscoveredCount = DiscoveredGroups.Size();
		{
			std::wstringstream LogString;
			LogString << L"Discovered (" << DiscoveredCount << L") media frame sources";
			Log(LogString.str().c_str());
		}

		for (unsigned int GroupIndex = 0; GroupIndex < DiscoveredCount; GroupIndex++)
		{
			MediaFrameSourceGroup Group = DiscoveredGroups.GetAt(GroupIndex);

			// For HoloLens, use the video conferencing video profile - this will give the best power consumption.
			auto profileList = MediaCapture::FindKnownVideoProfiles(Group.Id(), KnownVideoProfile::VideoConferencing);
			if (profileList.Size() == 0)
			{
				// No video conferencing profiles in this group, move to the next one.
				continue;
			}

			// Cache the first valid group and profile in case we do not find a profile that matches the input description.
			if (ChosenSourceGroup == nullptr)
			{
				ChosenSourceGroup = Group;

				CaptureSettings.SourceGroup(ChosenSourceGroup);
				CaptureSettings.VideoProfile(profileList.GetAt(0));
			}

			if (DesiredWidth > 0 && DesiredHeight > 0 && DesiredFPS > 0)
			{
				for (unsigned int profileIndex = 0; profileIndex < profileList.Size(); profileIndex++)
				{
					MediaCaptureVideoProfile profile = profileList.GetAt(profileIndex);

					auto descriptions = profile.SupportedRecordMediaDescription();
					for (unsigned int descIndex = 0; descIndex < descriptions.Size(); descIndex++)
					{
						MediaCaptureVideoProfileMediaDescription desc = descriptions.GetAt(descIndex);

						// Check for a profile that matches our desired dimensions.
						if (desc.Width() == DesiredWidth && desc.Height() == DesiredHeight && desc.FrameRate() == DesiredFPS)
						{
							ChosenSourceGroup = Group;

							CaptureSettings.SourceGroup(Group);
							CaptureSettings.VideoProfile(profile);
							CaptureSettings.RecordMediaDescription(desc);

							break;
						}
					}
				}
			}
		}

		// If there was no camera available, then log it and bail
		if (ChosenSourceGroup == nullptr)
		{
			Log(L"No media frame source found, so no camera images will be delivered");
			return;
		}

		if (DesiredWidth > 0 && DesiredHeight > 0 && DesiredFPS > 0
			&& CaptureSettings.RecordMediaDescription() == nullptr)
		{
			Log(L"No matching video format found, using default profile instead.");
		}

		// Find the color camera source
		const unsigned int InfosCount = ChosenSourceGroup.SourceInfos().Size();
		// Search through the infos to determine if this is the color camera source
		for (unsigned int InfoIndex = 0; InfoIndex < InfosCount; InfoIndex++)
		{
			MediaFrameSourceInfo Info = ChosenSourceGroup.SourceInfos().GetAt(InfoIndex);
			if (Info.SourceKind() == MediaFrameSourceKind::Color)
			{
				ChosenSourceInfo = Info;
				break;
			}
		}

		// If there was no camera available, then log it and bail
		if (ChosenSourceInfo == nullptr)
		{
			Log(L"No media frame source info found, so no camera images will be delivered");
			return;
		}

		// Create our capture object with our settings
		winrt::agile_ref < winrt::Windows::Media::Capture::MediaCapture > Capture{ MediaCapture() };
		Capture.get().InitializeAsync(CaptureSettings).Completed([=](auto&& asyncInfo, auto&& asyncStatus)
		{
			if (asyncStatus != winrt::Windows::Foundation::AsyncStatus::Completed)
			{
				Log(L"Failed to open camera, please check Webcam capability");
				return;
			}
			
			// Get the frame source from the source info we got earlier
			MediaFrameSource FrameSource = Capture.get().FrameSources().Lookup(ChosenSourceInfo.Id());

			// Now create and start the frame reader
			Capture.get().CreateFrameReaderAsync(FrameSource).Completed([=](auto&& asyncInfo, auto&& asyncStatus)
			{
				MediaFrameReader FrameReader = asyncInfo.GetResults();
				FrameReader.StartAsync().Completed([=](auto&& asyncInfo, auto&& asyncStatus)
				{
					MediaFrameReaderStartStatus StartStatus = asyncInfo.GetResults();
					if (StartStatus == MediaFrameReaderStartStatus::Success)
					{
						{
							// Finally, copy to our object
							std::lock_guard<std::mutex> lock(RefsLock);
							CameraCapture = std::move(Capture);
							CameraFrameReader = std::move(FrameReader);
							CameraFrameSource = std::move(FrameSource);
							Log(L"Successfully created the camera reader");
						}

						// Subscribe the inbound frame event
						CameraFrameReader.FrameArrived([this](auto&& sender, auto&& args) { OnFrameRecevied(sender, args); });
					}
					else
					{
						std::wstringstream LogString;
						LogString << L"Failed to start the frame reader with status =" << static_cast<int>(StartStatus);
						Log(LogString.str().c_str());
					}
				});
			});
		});
	});

	return true;
}

bool CameraImageCapture::StopCameraCapture()
{
	if (CameraFrameReader)
	{
		CameraFrameReader.StopAsync().Completed([=](auto&& asyncInfo, auto&&  asyncStatus)
		{
			std::lock_guard<std::mutex> lock(RefsLock);
			CameraCapture = nullptr;
			CameraFrameReader = nullptr;
			CameraFrameSource = nullptr;

			OnReceivedFrame = nullptr;
		});
	}

	return true;
}
