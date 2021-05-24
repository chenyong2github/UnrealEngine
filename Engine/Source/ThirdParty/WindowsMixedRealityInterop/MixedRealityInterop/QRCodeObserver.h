// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <winrt/base.h>
#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Perception.Spatial.Surfaces.h>
#include <winrt/Microsoft.MixedReality.QR.h>

using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Perception::Spatial::Surfaces;
using namespace winrt::Microsoft::MixedReality::QR;

/**
 * The QR code observer singleton that notifies UE4 of changes
 */
class QRCodeUpdateObserver
{
public:
	static QRCodeUpdateObserver& Get();
	static void Release();

	/** To route logging messages back to the UE_LOG() macros */
	void SetOnLog(void(*FunctionPointer)(const wchar_t* LogMsg));

	bool StartQRCodeObserver(void(*AddedFunctionPointer)(QRCodeData*), void(*UpdatedFunctionPointer)(QRCodeData*), void(*RemovedFunctionPointer)(QRCodeData*));
	void UpdateCoordinateSystem(winrt::Windows::Perception::Spatial::SpatialCoordinateSystem InCoordinateSystem);
	bool StopQRCodeObserver();

	void Log(const wchar_t* LogMsg);
	void Log(std::wstringstream& stream);

private:
	QRCodeUpdateObserver();
	~QRCodeUpdateObserver();

	/** Function pointer for logging */
	void(*OnLog)(const wchar_t*);
	/** Function pointer for telling UE4 a new code has been found */
	void(*OnAddedQRCode)(QRCodeData*);
	/** Function pointer for telling UE4 a code has been updated */
	void(*OnUpdatedQRCode)(QRCodeData*);
	/** Function pointer for telling UE4 a code has been removed */
	void(*OnRemovedQRCode)(QRCodeData*);

	static QRCodeUpdateObserver* ObserverInstance;

	// WinRT handlers
	static void OnAdded(QRCodeWatcher sender, QRCodeAddedEventArgs args);
	static void OnUpdated(QRCodeWatcher sender, QRCodeUpdatedEventArgs args);
	static void OnRemoved(QRCodeWatcher sender, QRCodeRemovedEventArgs args);

	QRCodeWatcher QRTrackerInstance = nullptr;
};
