// Copyright Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"
#include "MixedRealityInterop.h"

#include <winrt/Windows.Media.Capture.Frames.h>
#include <ppltasks.h>
#include <string>
#include <sstream>

#include <DXGI1_4.h>
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>

using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;

using namespace Windows::Media::Capture;
using namespace Windows::Media::Capture::Frames;
using namespace concurrency;
using namespace Platform;

using namespace Windows::Graphics::DirectX;
using namespace Windows::Graphics::DirectX::Direct3D11;

/** Controls access to our references */
std::mutex RefsLock;
/** The objects we need in order to receive frames of camera data */
Platform::Agile<MediaCapture> CameraCapture = nullptr;
MediaFrameReader^ CameraFrameReader = nullptr;
MediaFrameSource^ CameraFrameSource = nullptr;

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

/** Used to keep from leaking WinRT types to the header file, so this forwards to the real handler */
void OnFrameRecevied(MediaFrameReader^ SendingFrameReader, MediaFrameArrivedEventArgs^ FrameArrivedArgs)
{
	if (MediaFrameReference^ CurrentFrame = SendingFrameReader->TryAcquireLatestFrame())
	{
		CameraImageCapture& CaptureInstance = CameraImageCapture::Get();

		// Drill down through the objects to get the underlying D3D texture
		VideoMediaFrame^ VideoFrame = CurrentFrame->VideoMediaFrame;
		Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface^ ManagedSurface = VideoFrame->Direct3DSurface;
		if (ManagedSurface == nullptr)
		{
			CaptureInstance.Log(L"OnFrameRecevied() : VideoMediaFrame->Direct3DSurface was null, so no image to process");
			return;
		}
		ID3D11Texture2D* VideoFrameTexture = nullptr;
		HRESULT Result = GetDXGIInterface(ManagedSurface, &VideoFrameTexture);
		if (SUCCEEDED(Result) && VideoFrameTexture != nullptr)
		{
			// If the callback hangs onto the pointer, it needs to AddRef/Release like any COM ptr consumer
			CaptureInstance.NotifyReceivedFrame(VideoFrameTexture);

			// GetDXGIInterface() does call AddRef() on our behalf, so we need to Release()
			VideoFrameTexture->Release();
		}
		else
		{
			std::wstringstream LogString;
			LogString << L"Unable to get the underlying video texture with HRESULT (" << Result << L")";
			CaptureInstance.Log(LogString.str().c_str());
		}
		// @todo JoeG - the docs say this is required, but it's not valid...
		// Dispose of the ManagedSurface manually because it gives us a hard ref
		//ManagedSurface->Dispose();
	}
}

void CameraImageCapture::NotifyReceivedFrame(ID3D11Texture2D* ReceivedFrame)
{
	std::lock_guard<std::mutex> lock(RefsLock);
	if (OnReceivedFrame == nullptr)
	{
		return;
	}
	// Pass the D3D texture to the UE4 code via the function pointer
	OnReceivedFrame(ReceivedFrame);
}

void CameraImageCapture::StartCameraCapture(void(*FunctionPointer)(ID3D11Texture2D*), int DesiredWidth, int DesiredHeight, int DesiredFPS)
{
	OnReceivedFrame = FunctionPointer;
	if (OnReceivedFrame == nullptr)
	{
		Log(L"Null function pointer passed to StartCameraCapture() for new image callbacks. Aborting.");
		return;
	}

	// We need to enumerate the devices and pick one (hopefully there's only one)
	auto EnumerationTask = create_task(MediaFrameSourceGroup::FindAllAsync());
	EnumerationTask.then([this, DesiredWidth, DesiredHeight, DesiredFPS](IVectorView<MediaFrameSourceGroup^>^ DiscoveredGroups)
	{
		MediaFrameSourceInfo^ ChosenSourceInfo = nullptr;
		MediaFrameSourceGroup^ ChosenSourceGroup = nullptr;

		const unsigned int DiscoveredCount = DiscoveredGroups->Size;
		{
			std::wstringstream LogString;
			LogString << L"Discovered (" << DiscoveredCount << L") media frame sources";
			Log(LogString.str().c_str());
		}

		for (unsigned int GroupIndex = 0; GroupIndex < DiscoveredCount; GroupIndex++)
		{
			MediaFrameSourceGroup^ Group = DiscoveredGroups->GetAt(GroupIndex);

			const unsigned int InfosCount = Group->SourceInfos->Size;
			// Search through the infos to determine if this is the color camera source
			for (unsigned int InfoIndex = 0; InfoIndex < InfosCount; InfoIndex++)
			{
				MediaFrameSourceInfo^ Info = Group->SourceInfos->GetAt(InfoIndex);
				if (Info->SourceKind == MediaFrameSourceKind::Color)
				{
					ChosenSourceInfo = Info;
					ChosenSourceGroup = Group;
					break;
				}
			}

			// If we have selected one, stop searching
			if (ChosenSourceGroup != nullptr)
			{
				break;
			}
		}

		// If there was no camera available, then log it and bail
		if (ChosenSourceGroup == nullptr)
		{
			Log(L"No media frame source found, so no camera images will be delivered");
			return;
		}

		// Select the description that matches their desired width, height, fps
		MediaCaptureVideoProfileMediaDescription^ ChosenVideoDesc = nullptr;
		if (DesiredWidth > 0 && DesiredHeight > 0 && DesiredFPS > 0)
		{
			// Find a video profile that supports what we want
			IVectorView<MediaCaptureVideoProfileMediaDescription^>^ VideoFormats = ChosenSourceInfo->VideoProfileMediaDescription;
			if (VideoFormats != nullptr)
			{
				const unsigned int FormatsCount = VideoFormats->Size;
				for (unsigned int FormatsIndex = 0; FormatsIndex < FormatsCount; FormatsIndex++)
				{
					MediaCaptureVideoProfileMediaDescription^ Desc = VideoFormats->GetAt(FormatsIndex);
					if (DesiredWidth == Desc->Width && DesiredHeight == Desc->Height && DesiredFPS == Desc->FrameRate)
					{
						ChosenVideoDesc = Desc;
						break;
					}
				}
				// Log out the supported formats if the user selected one that is not supported
				if (ChosenVideoDesc == nullptr)
				{
					std::wstringstream LogString;
					LogString << L"No matching video format: W(" << DesiredWidth << L") H(" << DesiredHeight<< L") FPS(" << DesiredFPS << L")";
					Log(LogString.str().c_str());
					Log(L"Enumerating supported formats");
					const unsigned int FormatsCount = VideoFormats->Size;
					for (unsigned int FormatsIndex = 0; FormatsIndex < FormatsCount; FormatsIndex++)
					{
						MediaCaptureVideoProfileMediaDescription^ Desc = VideoFormats->GetAt(FormatsIndex);
						std::wstringstream LogString;
						LogString << L"Supports video format: W(" << Desc->Width << L") H(" << Desc->Height << L") FPS(" << Desc->FrameRate << L")";
						Log(LogString.str().c_str());
					}
				}
			}
		}

		// Setup our capture settings (chosen group, video only, auto memory which should be GPU, but whatever)
		MediaCaptureInitializationSettings^ CaptureSettings = ref new MediaCaptureInitializationSettings();
		CaptureSettings->SourceGroup = ChosenSourceGroup;
		CaptureSettings->StreamingCaptureMode = StreamingCaptureMode::Video;
		CaptureSettings->MemoryPreference = MediaCaptureMemoryPreference::Auto;
		CaptureSettings->VideoProfile = nullptr;
		CaptureSettings->RecordMediaDescription = ChosenVideoDesc;
		
		// Create our capture object with our settings
		Platform::Agile<MediaCapture> Capture(ref new MediaCapture());
		auto CaptureCreateTask = create_task(Capture->InitializeAsync(CaptureSettings));
		CaptureCreateTask.then([=]
		{
			// Get the frame source from the source info we got earlier
			MediaFrameSource^ FrameSource = Capture->FrameSources->Lookup(ChosenSourceInfo->Id);

			// Now create and start the frame reader (omg, this process is tedious)
			auto ReaderCreateTask = create_task(Capture->CreateFrameReaderAsync(FrameSource));
			ReaderCreateTask.then([=](MediaFrameReader^ FrameReader)
			{
				auto ReaderStartTask = create_task(FrameReader->StartAsync());
				ReaderStartTask.then([=](MediaFrameReaderStartStatus StartStatus)
				{
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
						CameraFrameReader->FrameArrived += ref new TypedEventHandler<MediaFrameReader^, MediaFrameArrivedEventArgs^>(&OnFrameRecevied);
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
}

void CameraImageCapture::StopCameraCapture()
{
	if (CameraFrameReader)
	{
		auto StopCameraTask = create_task(CameraFrameReader->StopAsync());
		StopCameraTask.then([=]
		{
			std::lock_guard<std::mutex> lock(RefsLock);
			CameraCapture = nullptr;
			CameraFrameReader = nullptr;
			CameraFrameSource = nullptr;

			OnReceivedFrame = nullptr;
		});
	}
}
