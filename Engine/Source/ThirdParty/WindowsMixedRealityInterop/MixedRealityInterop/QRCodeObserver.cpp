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

/** Controls access to our references */
std::mutex QRCodeRefsLock;

Windows::Foundation::EventRegistrationToken OnAddedEventToken;
Windows::Foundation::EventRegistrationToken OnUpdatedEventToken;
Windows::Foundation::EventRegistrationToken OnRemovedEventToken;

QRCodeUpdateObserver* QRCodeUpdateObserver::ObserverInstance = nullptr;

QRCodeUpdateObserver::QRCodeUpdateObserver()
	: OnLog(nullptr)
	, OnAddedQRCode(nullptr)
	, OnUpdatedQRCode(nullptr)
	, OnRemovedQRCode(nullptr)
{
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

std::vector<float3> CreateRectangle(float width, float height)
{
	std::vector<float3> vertices(4);

	vertices[0] = { 0, 0, 0 };
	vertices[1] = { width, 0, 0 };
	vertices[2] = { width, height, 0 };
	vertices[3] = { 0, height, 0 };

	return vertices;
}

void QRCodeUpdateObserver::OnAdded(QRCodesTrackerPlugin::QRCodeAddedEventArgs ^args)
{
 	std::vector<float3> qrVertices = CreateRectangle(args->Code->PhysicalSizeMeters, args->Code->PhysicalSizeMeters);
	std::vector<uint16> qrCodeIndices{ 0, 1, 2, 0, 2, 3 };
	Windows::Perception::Spatial::SpatialCoordinateSystem^ qrCoordinateSystem = Windows::Perception::Spatial::Preview::SpatialGraphInteropPreview::CreateCoordinateSystemForNode(args->Code->Id);

	// @todo: CNN add QR struct to pass data back to UE4 to add to list; probably need alloc cb
	// Tell UE4
	ObserverInstance->OnAddedQRCode();
}

void QRCodeUpdateObserver::OnUpdated(QRCodesTrackerPlugin::QRCodeUpdatedEventArgs ^args)
{
	// Tell UE4
	ObserverInstance->OnUpdatedQRCode();
}

void QRCodeUpdateObserver::OnRemoved(QRCodesTrackerPlugin::QRCodeRemovedEventArgs ^args)
{
	// Tell UE4
	ObserverInstance->OnRemovedQRCode();
}

void QRCodeUpdateObserver::StartQRCodeObserver(void(*AddedFunctionPointer)(), void(*UpdatedFunctionPointer)(), void(*RemovedFunctionPointer)())
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
			{ std::wstringstream string; string << L"QRCodesTrackerPlugin failed to start! Aborting with error code " << static_cast<int32>(ret); Log(string.str().c_str()); }
			QRTrackerInstance->Stop();
			QRTrackerInstance = nullptr;

			return;
		}

		Log(L"Interop: StartQRCodeObserver() success!");
	}
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
