// Copyright Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"
#include "MixedRealityInterop.h"

#include "QRCodeObserver.h"
#include "FastConversion.h"

#include <WindowsNumerics.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.foundation.numerics.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.Perception.Spatial.Preview.h>

#include <map>
#include <string>
#include <sstream>
#include <mutex>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Foundation::Numerics;

using namespace DirectX;

/** Controls access to our references */
std::mutex QRCodeRefsLock;

event_token OnAddedEventToken;
event_token OnUpdatedEventToken;
event_token OnRemovedEventToken;

static double QPCSecondsPerTick = 0.0f;
static SpatialCoordinateSystem LastCoordinateSystem = nullptr;

QRCodeUpdateObserver* QRCodeUpdateObserver::ObserverInstance = nullptr;

QRCodeUpdateObserver::QRCodeUpdateObserver()
	: OnLog(nullptr)
	, OnAddedQRCode(nullptr)
	, OnUpdatedQRCode(nullptr)
	, OnRemovedQRCode(nullptr)
{
	LARGE_INTEGER QPCFreq;
	QueryPerformanceFrequency(&QPCFreq);
	QPCSecondsPerTick = 1.0f / QPCFreq.QuadPart;
}

QRCodeUpdateObserver::~QRCodeUpdateObserver()
{
}

QRCodeUpdateObserver& QRCodeUpdateObserver::Get()
{
	if (ObserverInstance == nullptr)
	{
		ObserverInstance = new QRCodeUpdateObserver();
	}
	return *ObserverInstance;
}

void QRCodeUpdateObserver::Release()
{
	if (ObserverInstance != nullptr)
	{
		ObserverInstance->StopQRCodeObserver();
		delete ObserverInstance;
		ObserverInstance = nullptr;
	}
}

void QRCodeUpdateObserver::SetOnLog(void(*FunctionPointer)(const wchar_t* LogMsg))
{
	OnLog = FunctionPointer;
}

void QRCodeUpdateObserver::Log(const wchar_t* LogMsg)
{
	if (OnLog != nullptr)
	{
		OnLog(LogMsg);
	}
}

void QRCodeUpdateObserver::Log(std::wstringstream& stream)
{
	if (OnLog != nullptr)
	{
		OnLog(stream.str().c_str());
	}
}

static void CopyQRCodeDataManually(QRCodeData* code, guid InId, int32_t InVersion, float InPhysicalSize, long long InQPCTicks, std::wstring_view InData)
{
	code->Id = InId;
	code->Version = InVersion;
	code->SizeInMeters = InPhysicalSize;
	code->LastSeenTimestamp = (float)(QPCSecondsPerTick * (double)InQPCTicks);
	code->DataSize = (unsigned int)InData.size();
	code->Data = nullptr;
	if (code->DataSize > 0)
	{
		code->Data = new wchar_t[code->DataSize + 1];
		if (code->Data != nullptr)
		{
			memcpy(code->Data, InData.data(), code->DataSize * sizeof(wchar_t));
			code->Data[code->DataSize] = 0;
		}
	}

	if (LastCoordinateSystem != nullptr)
	{
		SpatialCoordinateSystem qrCoordinateSystem = Preview::SpatialGraphInteropPreview::CreateCoordinateSystemForNode(InId);
		if (qrCoordinateSystem != nullptr)
		{
			auto CodeTransform = qrCoordinateSystem.TryGetTransformTo(LastCoordinateSystem);
			if (CodeTransform != nullptr)
			{
				XMMATRIX ConvertTransform = XMLoadFloat4x4(&CodeTransform.Value());

				DirectX::XMVECTOR r;
				DirectX::XMVECTOR t;
				DirectX::XMVECTOR s;
				DirectX::XMMatrixDecompose(&s, &r, &t, ConvertTransform);

				DirectX::XMFLOAT3 Translation = ToUE4Translation(t);
				DirectX::XMFLOAT4 Rotation = ToUE4Quat(r);
				code->Translation[0] = Translation.x;
				code->Translation[1] = Translation.y;
				code->Translation[2] = Translation.z;
				code->Rotation[0] = Rotation.x;
				code->Rotation[1] = Rotation.y;
				code->Rotation[2] = Rotation.z;
				code->Rotation[3] = Rotation.w;
			}
		}
	}
}

#pragma warning(disable:4691)

// Why are all of these *EventArgs different even though they seem to have the same data?
void QRCodeUpdateObserver::OnAdded(QRCodeWatcher sender, QRCodeAddedEventArgs args)
{
	if ((args != nullptr) && (args.Code() != nullptr))
	{
		QRCodeData* code = new QRCodeData;
		if (code != nullptr)
		{
			CopyQRCodeDataManually(code, args.Code().SpatialGraphNodeId(), (int)args.Code().Version(), args.Code().PhysicalSideLength(), args.Code().LastDetectedTime().time_since_epoch().count(), args.Code().Data());
			ObserverInstance->OnAddedQRCode(code);
			if (code->Data != nullptr)
			{
				delete[] code->Data;
			}
			delete code;
		}
	}
}

void QRCodeUpdateObserver::OnUpdated(QRCodeWatcher sender, QRCodeUpdatedEventArgs args)
{
	if ((args != nullptr) && (args.Code() != nullptr))
	{
		QRCodeData* code = new QRCodeData;
		if (code != nullptr)
		{
			CopyQRCodeDataManually(code, args.Code().SpatialGraphNodeId(), (int)args.Code().Version(), args.Code().PhysicalSideLength(), args.Code().LastDetectedTime().time_since_epoch().count(), args.Code().Data());
			ObserverInstance->OnUpdatedQRCode(code);
			if (code->Data != nullptr)
			{
				delete[] code->Data;
			}
			delete code;
		}
	}
}

void QRCodeUpdateObserver::OnRemoved(QRCodeWatcher sender, QRCodeRemovedEventArgs args)
{
	if ((args != nullptr) && (args.Code() != nullptr))
	{
		QRCodeData* code = new QRCodeData;
		if (code != nullptr)
		{
			CopyQRCodeDataManually(code, args.Code().SpatialGraphNodeId(), (int)args.Code().Version(), args.Code().PhysicalSideLength(), args.Code().LastDetectedTime().time_since_epoch().count(), args.Code().Data());
			ObserverInstance->OnRemovedQRCode(code);
			if (code->Data != nullptr)
			{
				delete[] code->Data;
			}
			delete code;
		}
	}
}

bool QRCodeUpdateObserver::StartQRCodeObserver(void(*AddedFunctionPointer)(QRCodeData*), void(*UpdatedFunctionPointer)(QRCodeData*), void(*RemovedFunctionPointer)(QRCodeData*))
{
	OnAddedQRCode = AddedFunctionPointer;
	if (OnAddedQRCode == nullptr)
	{
		Log(L"Null added function pointer passed to StartQRCodeObserver(). Aborting.");
		return false;
	}

	OnUpdatedQRCode = UpdatedFunctionPointer;
	if (OnUpdatedQRCode == nullptr)
	{
		Log(L"Null updated function pointer passed to StartQRCodeObserver(). Aborting.");
		return false;
	}

	OnRemovedQRCode = UpdatedFunctionPointer;
	if (OnRemovedQRCode == nullptr)
	{
		Log(L"Null removed function pointer passed to StartQRCodeObserver(). Aborting.");
		return false;
	}

	std::lock_guard<std::mutex> lock(QRCodeRefsLock);
	// Create the tracker and register the callbacks
	if (QRTrackerInstance == nullptr)
	{
		if (QRCodeWatcher::IsSupported())
		{
			QRCodeWatcher::RequestAccessAsync().Completed([=](auto&& asyncInfo, auto&&  asyncStatus) 
			{
				if (asyncInfo.GetResults() == QRCodeWatcherAccessStatus::Allowed)
				{
					QRTrackerInstance = QRCodeWatcher();
					OnAddedEventToken = QRTrackerInstance.Added([=](auto&& sender, auto&& args) { OnAdded(sender, args); });
					OnUpdatedEventToken = QRTrackerInstance.Updated([=](auto&& sender, auto&& args) { OnUpdated(sender, args); });
					OnRemovedEventToken = QRTrackerInstance.Removed([=](auto&& sender, auto&& args) { OnRemoved(sender, args); });

					// Start the tracker
					QRTrackerInstance.Start();
					Log(L"Interop: StartQRCodeObserver() success!");
				}
				else
				{
					Log(L"Interop: StartQRCodeObserver() Access Denied!");
				}
			});

			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		Log(L"Interop: StartQRCodeObserver() already called!");
		return true;
	}
}

#pragma warning(default:4691)

void QRCodeUpdateObserver::UpdateCoordinateSystem(SpatialCoordinateSystem InCoordinateSystem)
{
	if (InCoordinateSystem == nullptr)
	{
		return;
	}

	LastCoordinateSystem = InCoordinateSystem;
}

bool QRCodeUpdateObserver::StopQRCodeObserver()
{
	std::lock_guard<std::mutex> lock(QRCodeRefsLock);
	if (QRTrackerInstance != nullptr)
	{
		QRTrackerInstance.Added(OnAddedEventToken);
		QRTrackerInstance.Updated(OnUpdatedEventToken);
		QRTrackerInstance.Removed(OnRemovedEventToken);

		// Stop the tracker
		QRTrackerInstance.Stop();
		QRTrackerInstance = nullptr;

		Log(L"Interop: StopQRCodeObserver() success!");
	}

	return true;
}
