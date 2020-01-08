// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Perception.Spatial.Surfaces.h>

using namespace Windows::Perception::Spatial;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Platform;

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

	void StartQRCodeObserver(void(*AddedFunctionPointer)(QRCodeData*), void(*UpdatedFunctionPointer)(QRCodeData*), void(*RemovedFunctionPointer)(QRCodeData*));
	void UpdateCoordinateSystem(Windows::Perception::Spatial::SpatialCoordinateSystem^ InCoordinateSystem);
	void StopQRCodeObserver();

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
	static void OnAdded(QRCodesTrackerPlugin::QRCodeAddedEventArgs ^args);
	static void OnUpdated(QRCodesTrackerPlugin::QRCodeUpdatedEventArgs ^args);
	static void OnRemoved(QRCodesTrackerPlugin::QRCodeRemovedEventArgs ^args);

	QRCodesTrackerPlugin::QRTracker^ QRTrackerInstance;
};
