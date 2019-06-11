// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"
#include "MixedRealityInterop.h"

#include "QRCodeObserver.h"

#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#include <WindowsNumerics.h>
#include <windows.foundation.numerics.h>
#include <ppltasks.h>
#include <pplawait.h>

#include <map>
#include <string>
#include <sstream>

//using namespace concurrency;
using namespace Microsoft::WRL;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;
using namespace std::placeholders;

using namespace DirectX;

/** Controls access to our references */
std::mutex QRCodeRefsLock;

Windows::Foundation::EventRegistrationToken OnAddedEventToken;
Windows::Foundation::EventRegistrationToken OnUpdatedEventToken;
Windows::Foundation::EventRegistrationToken OnRemovedEventToken;

static double QPCSecondsPerTick = 0.0f;
static Windows::Perception::Spatial::SpatialCoordinateSystem^ LastCoordinateSystem = nullptr;

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

static void CopyQRCodeDataManually(QRCodeData* code, Guid InId, int32 InVersion, float InPhysicalSize, long long InQPCTicks, uint32 InDataSize, String^ InData)
{
	code->Id = InId;
	code->Version = InVersion;
	code->SizeInMeters = InPhysicalSize;
	code->LastSeenTimestamp = (float)(QPCSecondsPerTick * (double)InQPCTicks);
	code->DataSize = InDataSize;
	code->Data = nullptr;
	if (code->DataSize > 0)
	{
		code->Data = new wchar_t[code->DataSize + 1];
		if (code->Data != nullptr)
		{
			memcpy(code->Data, InData->Data(), code->DataSize * sizeof(wchar_t));
			code->Data[code->DataSize] = 0;
		}
	}

	code->Translation[0] = 0.0f;
	code->Translation[1] = 0.0f;
	code->Translation[2] = 0.0f;
	code->Rotation[0] = 0.0f;
	code->Rotation[1] = 0.0f;
	code->Rotation[2] = 0.0f;
	code->Rotation[3] = 1.0f;
	if (LastCoordinateSystem != nullptr)
	{
		Windows::Perception::Spatial::SpatialCoordinateSystem^ qrCoordinateSystem = Windows::Perception::Spatial::Preview::SpatialGraphInteropPreview::CreateCoordinateSystemForNode(InId);
		auto CodeTransform = qrCoordinateSystem->TryGetTransformTo(LastCoordinateSystem);
		if (CodeTransform != nullptr)
		{
			XMMATRIX ConvertTransform = XMLoadFloat4x4(&CodeTransform->Value);
			XMVECTOR Scale, Rot, Trans;
			if (XMMatrixDecompose(&Scale, &Rot, &Trans, ConvertTransform))
			{
				XMFLOAT3 OutTrans;
				XMFLOAT4 OutRot;
				XMStoreFloat4(&OutRot, Rot);
				XMStoreFloat3(&OutTrans, Trans);
				code->Translation[0] = OutTrans.x;
				code->Translation[1] = OutTrans.y;
				code->Translation[2] = OutTrans.z;
				code->Rotation[0] = OutRot.x;
				code->Rotation[1] = OutRot.y;
				code->Rotation[2] = OutRot.z;
				code->Rotation[3] = OutRot.w;
			}
		}
	}
}

#pragma warning(disable:4691)

// Why are all of these *EventArgs different even though they seem to have the same data?
void QRCodeUpdateObserver::OnAdded(QRCodesTrackerPlugin::QRCodeAddedEventArgs ^args)
{
	if ((args != nullptr) && (args->Code != nullptr))
	{
		QRCodeData* code = new QRCodeData;
		if (code != nullptr)
		{
			CopyQRCodeDataManually(code, args->Code->Id, args->Code->Version, args->Code->PhysicalSizeMeters, args->Code->LastDetectedQPCTicks, args->Code->Code->Length(), args->Code->Code);
			ObserverInstance->OnAddedQRCode(code);
			if (code->Data != nullptr)
			{
				delete[] code->Data;
			}
			delete code;
		}
	}
}

void QRCodeUpdateObserver::OnUpdated(QRCodesTrackerPlugin::QRCodeUpdatedEventArgs ^args)
{
	if ((args != nullptr) && (args->Code != nullptr))
	{
		QRCodeData* code = new QRCodeData;
		if (code != nullptr)
		{
			CopyQRCodeDataManually(code, args->Code->Id, args->Code->Version, args->Code->PhysicalSizeMeters, args->Code->LastDetectedQPCTicks, args->Code->Code->Length(), args->Code->Code);
			ObserverInstance->OnUpdatedQRCode(code);
			if (code->Data != nullptr)
			{
				delete[] code->Data;
			}
			delete code;
		}
	}
}

void QRCodeUpdateObserver::OnRemoved(QRCodesTrackerPlugin::QRCodeRemovedEventArgs ^args)
{
	if ((args != nullptr) && (args->Code != nullptr))
	{
		QRCodeData* code = new QRCodeData;
		if (code != nullptr)
		{
			CopyQRCodeDataManually(code, args->Code->Id, args->Code->Version, args->Code->PhysicalSizeMeters, args->Code->LastDetectedQPCTicks, args->Code->Code->Length(), args->Code->Code);
			ObserverInstance->OnRemovedQRCode(code);
			if (code->Data != nullptr)
			{
				delete[] code->Data;
			}
			delete code;
		}
	}
}

void QRCodeUpdateObserver::StartQRCodeObserver(void(*AddedFunctionPointer)(QRCodeData*), void(*UpdatedFunctionPointer)(QRCodeData*), void(*RemovedFunctionPointer)(QRCodeData*))
{
	OnAddedQRCode = AddedFunctionPointer;
	if (OnAddedQRCode == nullptr)
	{
		Log(L"Null added function pointer passed to StartQRCodeObserver(). Aborting.");
		return;
	}

	OnUpdatedQRCode = UpdatedFunctionPointer;
	if (OnUpdatedQRCode == nullptr)
	{
		Log(L"Null updated function pointer passed to StartQRCodeObserver(). Aborting.");
		return;
	}

	OnRemovedQRCode = UpdatedFunctionPointer;
	if (OnRemovedQRCode == nullptr)
	{
		Log(L"Null removed function pointer passed to StartQRCodeObserver(). Aborting.");
		return;
	}

	std::lock_guard<std::mutex> lock(QRCodeRefsLock);
	// Create the tracker and register the callbacks
	if (QRTrackerInstance == nullptr)
	{
		QRTrackerInstance = ref new QRCodesTrackerPlugin::QRTracker();
		OnAddedEventToken = QRTrackerInstance->Added += ref new QRCodesTrackerPlugin::QRCodeAddedHandler(&OnAdded);
		OnUpdatedEventToken = QRTrackerInstance->Updated += ref new QRCodesTrackerPlugin::QRCodeUpdatedHandler(&OnUpdated);
		OnRemovedEventToken = QRTrackerInstance->Removed += ref new QRCodesTrackerPlugin::QRCodeRemovedHandler(&OnRemoved);

		// Start the tracker
		QRCodesTrackerPlugin::QRTrackerStartResult ret = QRTrackerInstance->Start();
		if (ret != QRCodesTrackerPlugin::QRTrackerStartResult::Success)
		{
			{ std::wstringstream string; string << L"QRCodesTrackerPlugin failed to start! Aborting with error code " << static_cast<int32>(ret); Log(string); }
			QRTrackerInstance->Stop();
			QRTrackerInstance = nullptr;

			return;
		}

		Log(L"Interop: StartQRCodeObserver() success!");
	}
	else
	{
		Log(L"Interop: StartQRCodeObserver() already called!");
	}
}

#pragma warning(default:4691)

void QRCodeUpdateObserver::UpdateCoordinateSystem(Windows::Perception::Spatial::SpatialCoordinateSystem^ InCoordinateSystem)
{
	if (InCoordinateSystem == nullptr)
	{
		return;
	}

	LastCoordinateSystem = InCoordinateSystem;
}

void QRCodeUpdateObserver::StopQRCodeObserver()
{
	std::lock_guard<std::mutex> lock(QRCodeRefsLock);
	if (QRTrackerInstance != nullptr)
	{
		QRTrackerInstance->Added -= OnAddedEventToken;
		QRTrackerInstance->Updated -= OnUpdatedEventToken;
		QRTrackerInstance->Removed -= OnRemovedEventToken;

		// Stop the tracker
		QRTrackerInstance->Stop();
		QRTrackerInstance = nullptr;

		Log(L"Interop: StopQRCodeObserver() success!");
	}
}
