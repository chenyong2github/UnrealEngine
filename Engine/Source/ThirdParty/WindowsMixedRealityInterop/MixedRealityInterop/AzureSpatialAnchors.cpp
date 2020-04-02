// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// This file is modified from Microsoft's AzureSpatialAnchors DirectX sample.

#include "stdafx.h"

#include "AzureSpatialAnchors.h"

#include "SpatialAnchorHelper.h"

#include <ppltasks.h>
#include <string>
#include <sstream>
#include <winrt\Windows.Foundation.Metadata.h>
#include <winrt\Windows.Security.Authorization.AppCapabilityAccess.h>
#include <winrt\Windows.System.Threading.h>
#include <winrt\Windows.Foundation.Collections.h>

using namespace WindowsMixedReality;
using namespace concurrency;
using namespace Microsoft::WRL;
using namespace std::placeholders;
using namespace std::literals::chrono_literals;
using namespace winrt::Windows::Foundation::Metadata;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::UI::Input::Spatial;
using namespace winrt::Windows::Security::Authorization::AppCapabilityAccess;
using namespace winrt::Windows::System::Threading;
using namespace winrt::Microsoft::Azure::SpatialAnchors;

class AzureSpatialAnchorsInteropImpl : public AzureSpatialAnchorsInterop
{
public:
	static void Create(
		WindowsMixedReality::MixedRealityInterop& interop, 
		LogFunctionPtr LogFunctionPointer,
		AnchorLocatedCallbackPtr AnchorLocatedCallback,
		LocateAnchorsCompletedCallbackPtr LocateAnchorsCompletedCallback,
		SessionUpdatedCallbackPtr SessionUpdatedCallback
	);
	static AzureSpatialAnchorsInteropImpl& Get();
	static void Release();

	virtual bool CreateSession() override;
	virtual bool ConfigSession(const ConfigData& InConfigData) override;
	virtual bool StartSession() override;
	virtual void StopSession() override;
	virtual void DestroySession() override;

	virtual bool HasEnoughDataForSaving() override;
	const wchar_t* GetCloudSpatialAnchorIdentifier(CloudAnchorID cloudAnchorID) override;
	virtual bool CreateCloudAnchor(const LocalAnchorID& localAnchorId, CloudAnchorID& outCloudAnchorID) override;
	virtual bool SetCloudAnchorExpiration(CloudAnchorID cloudAnchorID, float lifetime) override;
	virtual bool GetCloudAnchorExpiration(CloudAnchorID cloudAnchorID, float& lifetime) override;
	virtual bool SetCloudAnchorAppProperties(CloudAnchorID cloudAnchorID, const std::vector<std::pair<std::wstring, std::wstring>>& AppProperties) override;
	virtual bool GetCloudAnchorAppProperties(CloudAnchorID cloudAnchorID, std::vector<std::pair<std::wstring, std::wstring>>& AppProperties) override;
	virtual bool SaveCloudAnchor(SaveAsyncDataPtr Data) override;
	virtual bool DeleteCloudAnchor(DeleteAsyncDataPtr Data) override;
	virtual bool LoadCloudAnchorByID(LoadByIDAsyncDataPtr Data) override;
	virtual bool UpdateCloudAnchorProperties(UpdateCloudAnchorPropertiesAsyncDataPtr Data) override;
	virtual bool RefreshCloudAnchorProperties(RefreshCloudAnchorPropertiesAsyncDataPtr Data) override;
	virtual bool GetCloudAnchorProperties(GetCloudAnchorPropertiesAsyncDataPtr Data) override;
	virtual bool CreateWatcher(CreateWatcherData& Data) override;
	virtual bool StopWatcher(int32 WatcherIdentifier) override;
	virtual bool CreateARPinAroundAzureCloudSpatialAnchor(const LocalAnchorID& localAnchorId, CloudAnchorID cloudAnchorID) override;


protected:
	AzureSpatialAnchorsInteropImpl(
		WindowsMixedReality::MixedRealityInterop& interop,
		LogFunctionPtr LogFunctionPointer,
		AnchorLocatedCallbackPtr InAnchorLocatedCallback,
		LocateAnchorsCompletedCallbackPtr InLocateAnchorsCompletedCallback,
		SessionUpdatedCallbackPtr InSessionUpdatedCallback
	);
	virtual ~AzureSpatialAnchorsInteropImpl();
	AzureSpatialAnchorsInteropImpl(const AzureSpatialAnchorsInteropImpl&) = delete;
	AzureSpatialAnchorsInteropImpl& operator=(const AzureSpatialAnchorsInteropImpl&) = delete;

private:
	bool CheckForSession(const wchar_t* contex) const;
	winrt::fire_and_forget SaveAnchor(SaveAsyncDataPtr Data, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor);
	winrt::fire_and_forget DeleteAnchor(DeleteAsyncDataPtr Data, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor);
	winrt::fire_and_forget UpdateCloudAnchorProperties(UpdateCloudAnchorPropertiesAsyncDataPtr Data, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor);
	winrt::fire_and_forget RefreshCloudAnchorProperties(RefreshCloudAnchorPropertiesAsyncDataPtr Data, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor);
	bool LoadCloudAnchorByID_Located(const winrt::Microsoft::Azure::SpatialAnchors::AnchorLocatedEventArgs& args);
	bool LoadCloudAnchorByID_Completed(const winrt::Microsoft::Azure::SpatialAnchors::LocateAnchorsCompletedEventArgs& args);
	bool CreateWatcher_Located(const winrt::Microsoft::Azure::SpatialAnchors::AnchorLocatedEventArgs& args);
	bool CreateWatcher_Completed(const winrt::Microsoft::Azure::SpatialAnchors::LocateAnchorsCompletedEventArgs& args);

	CloudAnchorID GetNextCloudAnchorID();

	void AddEventListeners();
	void RemoveEventListeners();

	void Log(const wchar_t* LogMsg) const;
	void Log(std::wstringstream& stream) const;

	WindowsMixedReality::MixedRealityInterop& m_mixedRealityInterop;

	/** Function pointer for logging */
	LogFunctionPtr OnLog;
	AnchorLocatedCallbackPtr AnchorLocatedCallback;
	LocateAnchorsCompletedCallbackPtr LocateAnchorsCompletedCallback;
	SessionUpdatedCallbackPtr SessionUpdatedCallback;


	static AzureSpatialAnchorsInteropImpl* Instance;

	bool m_enoughDataForSaving{ false };
	bool m_asyncOpInProgress{ false };
	bool m_sessionStarted{ false };

	std::wstring m_logText;

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchorSession m_cloudSession{ nullptr };

	winrt::event_revoker<winrt::Microsoft::Azure::SpatialAnchors::ICloudSpatialAnchorSession> m_anchorLocatedToken;
	winrt::event_revoker<winrt::Microsoft::Azure::SpatialAnchors::ICloudSpatialAnchorSession> m_locateAnchorsCompletedToken;
	winrt::event_revoker<winrt::Microsoft::Azure::SpatialAnchors::ICloudSpatialAnchorSession> m_sessionUpdatedToken;
	winrt::event_revoker<winrt::Microsoft::Azure::SpatialAnchors::ICloudSpatialAnchorSession> m_errorToken;
	winrt::event_revoker<winrt::Microsoft::Azure::SpatialAnchors::ICloudSpatialAnchorSession> m_onLogDebugToken;

	// map of cloud anchors ids to cloud anchors
	std::map<CloudAnchorID, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor> m_cloudAnchors;
	mutable std::mutex m_cloudAnchorsMutex;

	std::map<WatcherID, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchorWatcher> m_watcherMap;
	std::map<WatcherID, LoadByIDAsyncDataPtr> m_loadByIDAsyncDataMap;
	std::mutex m_loadByIDAsyncDataMapMutex;

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* GetCloudAnchor(CloudAnchorID cloudAnchorID);
	CloudAnchorID CloudAnchorIdentifierToID(const CloudAnchorIdentifier& CloudAnchorIdentifier) const;
	CloudAnchorID CloudAnchorIdentifierToID(const winrt::hstring& CloudAnchorIdentifier) const;
};

AzureSpatialAnchorsInteropImpl* AzureSpatialAnchorsInteropImpl::Instance = nullptr;

void AzureSpatialAnchorsInterop::Create(
	WindowsMixedReality::MixedRealityInterop& interop,
	LogFunctionPtr LogFunctionPointer,
	AnchorLocatedCallbackPtr AnchorLocatedCallback,
	LocateAnchorsCompletedCallbackPtr LocateAnchorsCompletedCallback,
	SessionUpdatedCallbackPtr SessionUpdatedCallback
	)
{
	AzureSpatialAnchorsInteropImpl::Create(interop, LogFunctionPointer, AnchorLocatedCallback, LocateAnchorsCompletedCallback, SessionUpdatedCallback);
}

void AzureSpatialAnchorsInteropImpl::Create(
	WindowsMixedReality::MixedRealityInterop& interop,
	LogFunctionPtr LogFunctionPointer,
	AnchorLocatedCallbackPtr AnchorLocatedCallback,
	LocateAnchorsCompletedCallbackPtr LocateAnchorsCompletedCallback,
	SessionUpdatedCallbackPtr SessionUpdatedCallback
	)
{
	assert(AzureSpatialAnchorsInteropImpl::Instance == nullptr);
	AzureSpatialAnchorsInteropImpl::Instance = new AzureSpatialAnchorsInteropImpl(interop, LogFunctionPointer, AnchorLocatedCallback, LocateAnchorsCompletedCallback, SessionUpdatedCallback);
	assert(AzureSpatialAnchorsInteropImpl::Instance);
}

AzureSpatialAnchorsInterop& AzureSpatialAnchorsInterop::Get()
{
	return AzureSpatialAnchorsInteropImpl::Get();
}

AzureSpatialAnchorsInteropImpl& AzureSpatialAnchorsInteropImpl::Get()
{
	assert(AzureSpatialAnchorsInteropImpl::Instance);

	return *AzureSpatialAnchorsInteropImpl::Instance;
}

void AzureSpatialAnchorsInterop::Release()
{
	AzureSpatialAnchorsInteropImpl::Release();
}

void AzureSpatialAnchorsInteropImpl::Release()
{
	if (AzureSpatialAnchorsInteropImpl::Instance != nullptr)
	{
		delete AzureSpatialAnchorsInteropImpl::Instance;
		AzureSpatialAnchorsInteropImpl::Instance = nullptr;
	}
}

//Log(L"testlog");
//{ std::wstringstream string; string << L"testlog function foo failed with error code " << ErrorCode; Log(string); }
void AzureSpatialAnchorsInteropImpl::Log(const wchar_t* LogMsg) const
{
	if (OnLog)
	{
		OnLog(LogMsg);
	}
}

void AzureSpatialAnchorsInteropImpl::Log(std::wstringstream& stream) const
{
	Log(stream.str().c_str());
}

AzureSpatialAnchorsInteropImpl::AzureSpatialAnchorsInteropImpl(
	WindowsMixedReality::MixedRealityInterop& interop, 
	LogFunctionPtr LogFunctionPointer,
	AnchorLocatedCallbackPtr InAnchorLocatedCallback,
	LocateAnchorsCompletedCallbackPtr InLocateAnchorsCompletedCallback,
	SessionUpdatedCallbackPtr InSessionUpdatedCallback
)
	: AzureSpatialAnchorsInterop(),
	m_mixedRealityInterop(interop),
	OnLog(LogFunctionPointer),
	AnchorLocatedCallback(InAnchorLocatedCallback),
	LocateAnchorsCompletedCallback(InLocateAnchorsCompletedCallback),
	SessionUpdatedCallback(InSessionUpdatedCallback)

{
}

AzureSpatialAnchorsInteropImpl::~AzureSpatialAnchorsInteropImpl()
{
}

bool AzureSpatialAnchorsInteropImpl::CheckForSession(const wchar_t* context) const
{
	if (m_cloudSession == nullptr)
	{
		{ std::wstringstream string; string << context << L" called, but session does not exist!  Ignoring."; Log(string); }
		return false;
	}

	if (m_sessionStarted == false)
	{
		{ std::wstringstream string; string << context << L" called, but session is already started.  Ignoring."; Log(string); }
		return false;
	}

	return true;
}

bool AzureSpatialAnchorsInteropImpl::CreateSession()
{
	Log(L"CreateSession");

	if (m_cloudSession != nullptr)
	{
		Log(L"CreateSession called, but session already exists!  Ignoring.");
		return false;
	}

	//CreateSession
	m_enoughDataForSaving = false;
	m_cloudSession = CloudSpatialAnchorSession();

	return m_cloudSession != nullptr;
}

bool AzureSpatialAnchorsInteropImpl::ConfigSession(const ConfigData& InConfigData)
{
	Log(L"ConfigSession");

	if (m_cloudSession == nullptr)
	{
		Log(L"ConfigSession called, but session does not exist!  Ignoring.");
		return false;
	}

	if (InConfigData.accountId == nullptr || InConfigData.accountId[0] == 0)
	{
		Log(L"ConfigSession called, but accountId is null or empty!  This session should not be useable.");
	}

	if (InConfigData.accountKey == nullptr || InConfigData.accountKey[0] == 0)
	{
		Log(L"ConfigSession called, but accountId is null or empty!  This session should not be useable.");
	}
	
	// Session configuration takes effect on Start() according to documentation.

	// Do Coarse Localization Setup
	if (InConfigData.bCoarseLocalizationEnabled)
	{
		// Create the sensor fingerprint provider
		PlatformLocationProvider sensorProvider = PlatformLocationProvider();
		SensorCapabilities sensors = sensorProvider.Sensors();

		// Allow GPS
		sensors.GeoLocationEnabled(InConfigData.bEnableGPS);

		// Allow WiFi scanning
		sensors.WifiEnabled(InConfigData.bEnableWifi);

		// Bluetooth beacons
		if (InConfigData.BLEBeaconUUIDs.size() > 0)
		{
			// Populate the set of known BLE beacons' UUIDs
			std::vector<winrt::hstring> uuids;
			for (const wchar_t* UUID : InConfigData.BLEBeaconUUIDs)
			{
				uuids.emplace_back(UUID);
			}

			// Allow the set of known BLE beacons
			sensors.BluetoothEnabled(true);
			sensors.KnownBeaconProximityUuids(uuids);
		}

		// Set the session's sensor fingerprint provider
		m_cloudSession.LocationProvider(sensorProvider);
	}

	//ConfigSession
	auto configuration = m_cloudSession.Configuration();
	configuration.AccountId(InConfigData.accountId);
	configuration.AccountKey(InConfigData.accountKey);

	SessionLogLevel logLevel = (SessionLogLevel)InConfigData.logVerbosity;
	if ((logLevel < SessionLogLevel::None) || (logLevel > SessionLogLevel::All))
	{
		logLevel = (logLevel < SessionLogLevel::None) ? SessionLogLevel::None : SessionLogLevel::All;
		{ std::wstringstream string; string << L"ConfigSession called with invalid log level " << InConfigData.logVerbosity << ".  Clamping the value to " << (int)logLevel; Log(string); }
	}

	m_cloudSession.LogLevel(logLevel);
	AddEventListeners();

	return true;
}

bool AzureSpatialAnchorsInteropImpl::StartSession() 
{
	Log(L"StartSession");

	if (m_cloudSession == nullptr)
	{
		Log(L"StartSession called, but session does not exist!  Ignoring.");
		return false;
	}

	if (m_sessionStarted == true)
	{
		Log(L"StartSession called, but session is already started.  Ignoring.");
		return true;
	}

	//StartSession
	m_cloudSession.Start();
	m_sessionStarted = true;

	return true;
}

void AzureSpatialAnchorsInteropImpl::StopSession()
{
	Log(L"StopSession");

	if (m_cloudSession == nullptr)
	{
		Log(L"StopSession called, but session has already been cleaned up.  Ignoring.");
		return;
	}

	if (m_sessionStarted == false)
	{
		Log(L"StopSession called, but session is not started.  Ignoring.");
		return;
	}

	//StopSession
	m_sessionStarted = false;
	m_cloudSession.Stop();
}

void AzureSpatialAnchorsInteropImpl::DestroySession()
{
	Log(L"StopSession");

	if (m_cloudSession == nullptr)
	{
		Log(L"StartSession called, but session does not exist!  Ignoring.");
		return;
	}

	//DestroySession
	RemoveEventListeners();
	{
		auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
		m_cloudAnchors.clear();
	}
	m_sessionStarted = false;
	m_cloudSession = nullptr;
}
bool AzureSpatialAnchorsInteropImpl::HasEnoughDataForSaving()
{
	return m_enoughDataForSaving;
}

const wchar_t* AzureSpatialAnchorsInteropImpl::GetCloudSpatialAnchorIdentifier(CloudAnchorID cloudAnchorID)
{
	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(cloudAnchorID);
	if (cloudAnchor)
	{
		return cloudAnchor->Identifier().c_str();
	}
	else
	{
		return nullptr;
	}
}


bool AzureSpatialAnchorsInteropImpl::CreateCloudAnchor(const LocalAnchorID& localAnchorId, CloudAnchorID& outCloudAnchorID)
{
	if (localAnchorId.length() == 0)
	{
		Log(L"CreateCloudAnchor failed because localAnchorId is empty!");
		return false;
	}

	{ std::wstringstream string; string << L"CreateCloudAnchor from a local anchor " << localAnchorId; Log(string); }

	std::shared_ptr<class SpatialAnchorHelper> spatialAnchorHelper = GetSpatialAnchorHelper();
	winrt::Windows::Perception::Spatial::SpatialAnchor* localAnchor = spatialAnchorHelper->GetSpatialAnchor(localAnchorId.c_str());
	if (localAnchor == nullptr)
	{
		{ std::wstringstream string; string << L"CreateCloudAnchor failed because localAnchorId " << localAnchorId << L" does not exist!  You must create the local anchor first."; Log(string); }
		return false;
	}
	else
	{
		winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor newCloudAnchor = CloudSpatialAnchor();
		newCloudAnchor.LocalAnchor(*localAnchor);
		outCloudAnchorID = GetNextCloudAnchorID();
		m_cloudAnchors.insert(std::make_pair(outCloudAnchorID, newCloudAnchor));
		return true;
	}
}

bool AzureSpatialAnchorsInteropImpl::SetCloudAnchorExpiration(CloudAnchorID cloudAnchorID, float lifetime)
{
	{ std::wstringstream string; string << L"SetCloudAnchorExpiration for anchor " << cloudAnchorID; Log(string); }

	if (lifetime <= 0.0f)
	{
		{ std::wstringstream string; string << L"Warning: SetCloudAnchorExpiration setting with lifetime " << lifetime << " which is invalid!  Expiration not set."; Log(string); }
		return false;
	}
	int64 lifetimeInt = static_cast<int64>(std::ceil(lifetime));
	const winrt::Windows::Foundation::TimeSpan future{ std::chrono::seconds{ lifetimeInt } };
	const winrt::Windows::Foundation::DateTime expiration = winrt::clock::now() + future;

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(cloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SetCloudAnchorExpiration failed because cloudAnchorID " << cloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		return false;
	}
	else
	{
		cloudAnchor->Expiration(expiration);
		return true;
	}
}

bool AzureSpatialAnchorsInteropImpl::GetCloudAnchorExpiration(CloudAnchorID cloudAnchorID, float& outLifetime)
{
	{ std::wstringstream string; string << L"GetCloudAnchorExpiration for anchor " << cloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(cloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SetCloudAnchorExpiration failed because cloudAnchorID " << cloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		return false;
	}
	else
	{
		const winrt::Windows::Foundation::TimeSpan lifetimeSpan = cloudAnchor->Expiration() - winrt::clock::now();
		typedef std::chrono::duration<float> floatseconds; // +- about 30 years is representable
		floatseconds seconds = std::chrono::duration_cast<floatseconds>(lifetimeSpan);
		outLifetime = seconds.count();
		return true;
	}
}

bool AzureSpatialAnchorsInteropImpl::SetCloudAnchorAppProperties(CloudAnchorID cloudAnchorID, const std::vector<std::pair<std::wstring, std::wstring>>& InAppProperties)
{
	{ std::wstringstream string; string << L"SetCloudAnchorAppProperties for anchor " << cloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(cloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SetCloudAnchorAppProperties failed because cloudAnchorID " << cloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		return false;
	}
	else
	{
		auto properties = cloudAnchor->AppProperties();
		for (const std::pair<std::wstring, std::wstring>& pair : InAppProperties)
		{
			properties.Insert(pair.first.c_str(), pair.second.c_str());
		}

		return true;
	}
}

bool AzureSpatialAnchorsInteropImpl::GetCloudAnchorAppProperties(CloudAnchorID cloudAnchorID, std::vector<std::pair<std::wstring, std::wstring>>& AppProperties)
{
	{ std::wstringstream string; string << L"GetCloudAnchorAppProperties for anchor " << cloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(cloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SetCloudAnchorAppProperties failed because cloudAnchorID " << cloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		return false;
	}
	else
	{
		AppProperties.clear();
		AppProperties.reserve(cloudAnchor->AppProperties().Size());
		for (auto itr : cloudAnchor->AppProperties())
		{
			AppProperties.emplace_back(itr.Key(), itr.Value());
		}

		return true;
	}

}

bool AzureSpatialAnchorsInteropImpl::SaveCloudAnchor(SaveAsyncDataPtr Data)
{
	assert(Data);

	{ std::wstringstream string; string << L"SaveCloudAnchor for CloudAnchorID " << Data->CloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(Data->CloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SaveCloudAnchor failed because cloud anchor for CloudAnchorID " << Data->CloudAnchorID << L" does not exist!"; Log(string); }
		Data->Result = AsyncResult::FailNoCloudAnchor;
		Data->Complete();
		return false;
	}

	if (!m_enoughDataForSaving)
	{
		Log(L"Cannot save AzureSpatialAnchor yet, not enough data. Look around more then try again.");
		Data->Result = AsyncResult::FailNotEnoughData;
		Data->Complete();
		return false;
	}

	if (!CheckForSession(L"SaveCloudAnchor"))
	{
		Data->Result = AsyncResult::FailNoSession;
		Data->Complete();
		return false;
	}

	SaveAnchor(Data, cloudAnchor);

	Data->Result = AsyncResult::Started;
	return true;
}

winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::SaveAnchor(SaveAsyncDataPtr Data, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor)
{
	assert(Data);
	assert(cloudAnchor);

	try
	{
		{ std::wstringstream string; string << L"SaveCloudAnchor saving cloud anchor " << Data->CloudAnchorID; Log(string); }

		m_asyncOpInProgress = true;
		co_await m_cloudSession.CreateAnchorAsync(*cloudAnchor);
		m_asyncOpInProgress = false;
		
		Data->Result = AsyncResult::Success;
		{ std::wstringstream string; string << L"SaveCloudAnchor saved cloud anchor [" << Data->CloudAnchorID << L"] with cloud Identifier [" << cloudAnchor->Identifier().c_str() << L"]"; Log(string); }
	}
	catch (winrt::hresult_error e)
	{
		m_asyncOpInProgress = false;
		Data->Result = AsyncResult::FailSeeErrorString;
		Data->OutError = e.message();
		{ std::wstringstream string; string << L"SaveCloudAnchor failed to save cloud anchor [" << Data->CloudAnchorID << L" message: " << e.message().c_str(); Log(string); }
	}

	Data->Complete();
}

bool AzureSpatialAnchorsInteropImpl::DeleteCloudAnchor(DeleteAsyncDataPtr Data)
{
	assert(Data);

	{ std::wstringstream string; string << L"DeleteCloudAnchor for cloud anchor " << Data->CloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(Data->CloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"DeleteCloudAnchor failed because cloud anchor " << Data->CloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		Data->Result = AsyncResult::FailNoAnchor;
		Data->Complete();
		return false;
	}

	if (!CheckForSession(L"DeleteCloudAnchor"))
	{
		Data->Result = AsyncResult::FailNoSession;
		Data->Complete();
		return false;
	}

	DeleteAnchor(Data, cloudAnchor);
	
	Data->Result = AsyncResult::Started;
	return true;
}

winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::DeleteAnchor(DeleteAsyncDataPtr Data, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor)
{
	assert(Data);
	assert(cloudAnchor);

	try
	{
		m_asyncOpInProgress = true;
		co_await m_cloudSession.DeleteAnchorAsync(*cloudAnchor);
		m_asyncOpInProgress = false;
		{
			auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
			m_cloudAnchors.erase(Data->CloudAnchorID);
		}

		Data->Result = AsyncResult::Success;
		{ std::wstringstream string; string << L"DeleteAnchor deleted cloud anchor " << Data->CloudAnchorID; Log(string); }
	}
	catch (winrt::hresult_error e)
	{
		m_asyncOpInProgress = false;
		Data->Result = AsyncResult::FailSeeErrorString;
		Data->OutError = e.message();
		{ std::wstringstream string; string << L"SaveCloudAnchor failed to delete cloud anchor " << Data->CloudAnchorID << L" message: " << e.message().c_str(); Log(string); }
	}

	Data->Complete();
	co_return;
}

bool AzureSpatialAnchorsInteropImpl::LoadCloudAnchorByID(LoadByIDAsyncDataPtr Data)
{
	Log(L"FindCloudAnchorByID");

	if (!CheckForSession(L"SaveCloudAnchor"))
	{
		Data->Result = AsyncResult::FailNoSession;
		Data->Complete();
		return false;
	}

	if (Data->CloudAnchorIdentifier.empty())
	{
		Data->Result = AsyncResult::FailBadAnchorIdentifier;
		Data->Complete();
		return false;
	}


	AnchorLocateCriteria criteria = AnchorLocateCriteria();
	criteria.Identifiers({ Data->CloudAnchorIdentifier.c_str() });

	Data->Result = AsyncResult::Started;

	{
		auto lock = std::unique_lock<std::mutex>(m_loadByIDAsyncDataMapMutex);

		try
		{
			auto& watcher = m_cloudSession.CreateWatcher(criteria);
			m_loadByIDAsyncDataMap.insert(std::make_pair(watcher.Identifier(), Data));
			{ std::wstringstream string; string << L"LoadCloudAnchorByID created watcher " << watcher.Identifier() << " for " << Data->CloudAnchorIdentifier; Log(string); }
			return true;
		}
		catch (winrt::hresult_error e)
		{
			Data->Result = AsyncResult::FailSeeErrorString;
			Data->OutError = e.message().c_str();
			Data->Complete();
			{ std::wstringstream string; string << L"LoadCloudAnchorByID failed to load anchor.  message: " << e.message().c_str(); Log(string); }
			return false;
		}
	}

}

bool AzureSpatialAnchorsInteropImpl::LoadCloudAnchorByID_Located(const winrt::Microsoft::Azure::SpatialAnchors::AnchorLocatedEventArgs& args)
{
	auto lock = std::unique_lock<std::mutex>(m_loadByIDAsyncDataMapMutex);
	auto itr = m_loadByIDAsyncDataMap.find(args.Watcher().Identifier());
	if (itr == m_loadByIDAsyncDataMap.end())
	{
		return false;
	}

	LoadByIDAsyncDataPtr& Data = itr->second;

	switch (args.Status())
	{
	case LocateAnchorStatus::Located:
	{
		{ std::wstringstream string; string << L"LoadCloudAnchorByID_Located status Located Id: " << args.Anchor().Identifier().c_str(); Log(string); }

		winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor loadedCloudAnchor = args.Anchor();
		winrt::Windows::Perception::Spatial::SpatialAnchor localAnchor = loadedCloudAnchor.LocalAnchor();

		LoadByIDAsyncDataPtr& Data = itr->second;

		assert(Data->CloudAnchorIdentifier == loadedCloudAnchor.Identifier());

		std::shared_ptr<class SpatialAnchorHelper> spatialAnchorHelper = GetSpatialAnchorHelper();
		spatialAnchorHelper->StoreSpatialAnchor(Data->LocalAnchorId, localAnchor);

		Data->CloudAnchorID = GetNextCloudAnchorID();

		{
			auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
			m_cloudAnchors.insert(std::make_pair(Data->CloudAnchorID, loadedCloudAnchor));
		}
		Data->Result = AsyncResult::Success;
	}
	break;
	case LocateAnchorStatus::AlreadyTracked:
	{
		{ std::wstringstream string; string << L"LoadCloudAnchorByID_Located status AlreadyTracked"; Log(string); }

		Data->Result = AsyncResult::FailAnchorAlreadyTracked;
	}
	break;
	case LocateAnchorStatus::NotLocated:
	{
		{ std::wstringstream string; string << L"LoadCloudAnchorByID_Located status NotLocated"; Log(string); }

		// This gets called repeatedly for a while until something else happens.
		Data->Result = AsyncResult::NotLocated;
	}
	break;
	case LocateAnchorStatus::NotLocatedAnchorDoesNotExist:
	{
		{ std::wstringstream string; string << L"LoadCloudAnchorByID_Located status NotLocatedAnchorDoesNotExist"; Log(string); }

		Data->Result = AsyncResult::FailAnchorDoesNotExist;
	}
	break;
	default:
		assert(false);
	}

	return true;
}

bool AzureSpatialAnchorsInteropImpl::LoadCloudAnchorByID_Completed(const winrt::Microsoft::Azure::SpatialAnchors::LocateAnchorsCompletedEventArgs& args)
{
	auto lock = std::unique_lock<std::mutex>(m_loadByIDAsyncDataMapMutex);
	auto itr = m_loadByIDAsyncDataMap.find(args.Watcher().Identifier());
	if (itr == m_loadByIDAsyncDataMap.end())
	{
		return false;
	}

	{ std::wstringstream string; string << L"LoadCloudAnchorByID_Completed watcher " << args.Watcher().Identifier() << " has completed."; Log(string); }

	LoadByIDAsyncDataPtr& Data = itr->second;
	if (args.Cancelled())
	{
		Data->Result = AsyncResult::Canceled;
	}
	Data->Complete();
	m_loadByIDAsyncDataMap.erase(itr);
	return true;
}




bool AzureSpatialAnchorsInteropImpl::UpdateCloudAnchorProperties(UpdateCloudAnchorPropertiesAsyncDataPtr Data)
{
	assert(Data);

	{ std::wstringstream string; string << L"UpdateCloudAnchorProperties for cloud anchor " << Data->CloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(Data->CloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"UpdateCloudAnchorProperties failed because cloud anchor " << Data->CloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		Data->Result = AsyncResult::FailNoAnchor;
		Data->Complete();
		return false;
	}

	if (!CheckForSession(L"UpdateCloudAnchorProperties"))
	{
		Data->Result = AsyncResult::FailNoSession;
		Data->Complete();
		return false;
	}

	UpdateCloudAnchorProperties(Data, cloudAnchor);

	Data->Result = AsyncResult::Started;
	return true;
}

winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::UpdateCloudAnchorProperties(UpdateCloudAnchorPropertiesAsyncDataPtr Data, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor)
{
	assert(Data);
	assert(cloudAnchor);

	try
	{
		m_asyncOpInProgress = true;
		co_await m_cloudSession.UpdateAnchorPropertiesAsync(*cloudAnchor);
		m_asyncOpInProgress = false;

		Data->Result = AsyncResult::Success;
		{ std::wstringstream string; string << L"UpdateAnchor updated cloud anchor " << Data->CloudAnchorID; Log(string); }
	}
	catch (winrt::hresult_error e)
	{
		m_asyncOpInProgress = false;
		Data->Result = AsyncResult::FailSeeErrorString;
		Data->OutError = e.message();
		{ std::wstringstream string; string << L"SaveCloudAnchor failed to update cloud anchor " << Data->CloudAnchorID << L" message: " << e.message().c_str(); Log(string); }
	}

	Data->Complete();
	co_return;
}

bool AzureSpatialAnchorsInteropImpl::RefreshCloudAnchorProperties(RefreshCloudAnchorPropertiesAsyncDataPtr Data)
{
	assert(Data);

	{ std::wstringstream string; string << L"RefreshCloudAnchorProperties for cloud anchor " << Data->CloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(Data->CloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"RefreshCloudAnchorProperties failed because cloud anchor " << Data->CloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		Data->Result = AsyncResult::FailNoAnchor;
		Data->Complete();
		return false;
	}

	if (!CheckForSession(L"RefreshCloudAnchorProperties"))
	{
		Data->Result = AsyncResult::FailNoSession;
		Data->Complete();
		return false;
	}

	RefreshCloudAnchorProperties(Data, cloudAnchor);

	Data->Result = AsyncResult::Started;
	return true;
}

winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::RefreshCloudAnchorProperties(RefreshCloudAnchorPropertiesAsyncDataPtr Data, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor)
{
	assert(Data);
	assert(cloudAnchor);

	try
	{
		m_asyncOpInProgress = true;
		co_await m_cloudSession.RefreshAnchorPropertiesAsync(*cloudAnchor);
		m_asyncOpInProgress = false;

		Data->Result = AsyncResult::Success;
		{ std::wstringstream string; string << L"RefreshAnchor refreshed cloud anchor " << Data->CloudAnchorID; Log(string); }
	}
	catch (winrt::hresult_error e)
	{
		m_asyncOpInProgress = false;
		Data->Result = AsyncResult::FailSeeErrorString;
		Data->OutError = e.message();
		{ std::wstringstream string; string << L"SaveCloudAnchor failed to refreshed cloud anchor " << Data->CloudAnchorID << L" message: " << e.message().c_str(); Log(string); }
	}

	Data->Complete();
	co_return;
}

bool AzureSpatialAnchorsInteropImpl::GetCloudAnchorProperties(GetCloudAnchorPropertiesAsyncDataPtr Data)
{
	//TODO
	// Get the cloud anchor, even if not yet located.
	assert(false); // Not implemented yet
	return false;
}

bool AzureSpatialAnchorsInteropImpl::CreateWatcher(CreateWatcherData& Data)
{
	Log(L"CreateWatcher");

	if (!CheckForSession(L"CreateWatcher"))
	{
		{ std::wstringstream string; string << L"CreateWatcher failed because there is no session.  You must start the AzureSpatialAnchors session first."; Log(string); }
		Data.Result = AsyncResult::FailNoSession;
		return false;
	}

	AnchorLocateCriteria criteria = AnchorLocateCriteria();

	criteria.BypassCache(Data.bBypassCache);

	if (Data.NearCloudAnchorID != CloudAnchorID_Invalid)
	{
		winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* sourceAnchor = GetCloudAnchor(Data.NearCloudAnchorID);
		if (!sourceAnchor)
		{
			{ std::wstringstream string; string << L"CreateWatcher failed because cloud anchor with NearCloudAnchorID " << Data.NearCloudAnchorID << L" does not exist!"; Log(string); }
			Data.Result = AsyncResult::FailBadAnchorIdentifier;
			Data.OutError = L"CreateWatcher failed because cloud anchor with specified NearCloudAnchorID does not exist!";
			return false;
		}
		NearAnchorCriteria nearAnchorCriteria;
		nearAnchorCriteria.DistanceInMeters(Data.NearCloudAnchorDistance);
		nearAnchorCriteria.MaxResultCount(Data.NearCloudAnchorMaxResultCount);
		nearAnchorCriteria.SourceAnchor(*sourceAnchor);
		criteria.NearAnchor(nearAnchorCriteria);
	}
	
	if(Data.SearchNearDevice)
	{
		NearDeviceCriteria nearDeviceCriteria = NearDeviceCriteria();
		nearDeviceCriteria.DistanceInMeters(Data.NearDeviceDistance);
		nearDeviceCriteria.MaxResultCount(Data.NearDeviceMaxResultCount);
		criteria.NearDevice(nearDeviceCriteria);
	}

	if (Data.Identifiers.size() > 0)
	{
		std::vector<winrt::hstring> Identifiers;
		Identifiers.reserve(Data.Identifiers.size());
		for (const auto& itr : Data.Identifiers)
		{
			Identifiers.push_back(itr.c_str());
		}
		criteria.Identifiers(Identifiers);
	}

	assert(static_cast<AnchorDataCategory>(Data.AzureSpatialAnchorDataCategory) >= AnchorDataCategory::None && static_cast<AnchorDataCategory>(Data.AzureSpatialAnchorDataCategory) <= AnchorDataCategory::Spatial);
	criteria.RequestedCategories(static_cast<AnchorDataCategory>(Data.AzureSpatialAnchorDataCategory));
	assert(static_cast<LocateStrategy>(Data.AzureSptialAnchorsLocateStrategy) >= LocateStrategy::AnyStrategy && static_cast<LocateStrategy>(Data.AzureSptialAnchorsLocateStrategy) <= LocateStrategy::VisualInformation);
	criteria.Strategy(static_cast<LocateStrategy>(Data.AzureSptialAnchorsLocateStrategy));

	{
		try
		{
			auto& watcher = m_cloudSession.CreateWatcher(criteria);
			m_watcherMap.insert({ static_cast<WatcherID>(watcher.Identifier()), watcher });
			Data.OutWatcherIdentifier = watcher.Identifier();
			Data.Result = AsyncResult::Started;
			{ std::wstringstream string; string << L"CreateWatcher created watcher " << watcher.Identifier(); Log(string); }
			return true;
		}
		catch (winrt::hresult_error e)
		{
			Data.Result = AsyncResult::FailSeeErrorString;
			Data.OutError = e.message().c_str();
			{ std::wstringstream string; string << L"CreateWatcher failed to create watcher.  message: " << e.message().c_str(); Log(string); }
			return false;
		}
	}
}

bool AzureSpatialAnchorsInteropImpl::StopWatcher(int32 WatcherIdentifier)
{
	auto itr = m_watcherMap.find(WatcherIdentifier);
	if (itr == m_watcherMap.end())
	{
		{ std::wstringstream string; string << L"StopWatcher watcher: " << WatcherIdentifier << " does not exist!  Ignoring."; Log(string); }
		return false;
	}
	else
	{
		{ std::wstringstream string; string << L"StopWatcher stoppint watcher: " << WatcherIdentifier; Log(string); }
		winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchorWatcher& watcher = itr->second;
		watcher.Stop();
		return true;
	}
}

bool AzureSpatialAnchorsInteropImpl::CreateARPinAroundAzureCloudSpatialAnchor(const LocalAnchorID& localAnchorId, CloudAnchorID cloudAnchorID)
{
	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(cloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"CreateARPinAroundAzureCloudSpatialAnchor failed because cloud anchor " << cloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		return false;
	}

	if (!cloudAnchor->LocalAnchor())
	{
		{ std::wstringstream string; string << L"CreateARPinAroundAzureCloudSpatialAnchor failed because cloud anchor " << cloudAnchorID << L" does not have a local anchor!  Perhaps it has not localized yet?"; Log(string); }
		return false;
	}

	std::shared_ptr<class SpatialAnchorHelper> spatialAnchorHelper = GetSpatialAnchorHelper();
	spatialAnchorHelper->StoreSpatialAnchor(localAnchorId, cloudAnchor->LocalAnchor());
	return true;
}

void AzureSpatialAnchorsInteropImpl::AddEventListeners()
{
	if (m_cloudSession == nullptr)
	{
		return;
	}

	if (m_anchorLocatedToken)
	{
		// Event listeners have already been setup, possible if we 'configure' multiple times.
		return;
	}

	m_anchorLocatedToken = m_cloudSession.AnchorLocated(winrt::auto_revoke, [this](auto&&, auto&& args)
	{
		{ std::wstringstream string; string << L"AnchorLocated watcher " << args.Watcher().Identifier() << " has Located."; Log(string); }

		LoadCloudAnchorByID_Located(args);

		LocateAnchorStatus Status = args.Status();
		AzureSpatialAnchorsInterop::CloudAnchorID CloudAnchorID = CloudAnchorID_Invalid;

		switch (Status)
		{
		case LocateAnchorStatus::Located:
		{
			winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor loadedCloudAnchor = args.Anchor();
			CloudAnchorID = CloudAnchorIdentifierToID(loadedCloudAnchor.Identifier());
			if (CloudAnchorID == CloudAnchorID_Invalid)
			{
				CloudAnchorID = GetNextCloudAnchorID();
				{
					auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
					m_cloudAnchors.insert(std::make_pair(CloudAnchorID, loadedCloudAnchor));
				}

				{ std::wstringstream string; string << L"LocateAnchorStatus::Located Id: " << args.Anchor().Identifier().c_str() << L" Created CloudAnchor " << CloudAnchorID; Log(string); }
			}
			else
			{
				{ std::wstringstream string; string << L"LocateAnchorStatus::Located Id: " << args.Anchor().Identifier().c_str() << L" Cloud Anchor already existed."; Log(string); }
			}
		}
		break;
		case LocateAnchorStatus::AlreadyTracked:
		{
			winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor loadedCloudAnchor = args.Anchor();
			CloudAnchorID = CloudAnchorIdentifierToID(loadedCloudAnchor.Identifier());
			{ std::wstringstream string; string << L"LocateAnchorStatus::AlreadyTracked CloudAnchorID " << CloudAnchorID; Log(string); }
			assert(CloudAnchorID != CloudAnchorID_Invalid);
		}
		break;
		case LocateAnchorStatus::NotLocated:
		{
			// This gets called repeatedly for a while until something else happens.
			{ std::wstringstream string; string << L" LocateAnchorStatus::NotLocated"; Log(string); }
			winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor loadedCloudAnchor = args.Anchor();
			CloudAnchorID = CloudAnchorIdentifierToID(loadedCloudAnchor.Identifier());
			if (CloudAnchorID == CloudAnchorID_Invalid)
			{
				CloudAnchorID = GetNextCloudAnchorID();
				{
					auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
					m_cloudAnchors.insert(std::make_pair(CloudAnchorID, loadedCloudAnchor));
				}

				{ std::wstringstream string; string << L"LocateAnchorStatus::NotLocated Id: " << args.Anchor().Identifier().c_str() << L" Created CloudAnchor " << CloudAnchorID; Log(string); }
			}
			else
			{
				{ std::wstringstream string; string << L"LocateAnchorStatus::NotLocated Id: " << args.Anchor().Identifier().c_str() << L" Cloud Anchor already existed."; Log(string); }
			}
		}
		break;
		case LocateAnchorStatus::NotLocatedAnchorDoesNotExist:
		{
			{ std::wstringstream string; string << L"LocateAnchorStatus::NotLocatedAnchorDoesNotExist"; Log(string); }
		}
		break;
		default:
			assert(false);
		}

		AnchorLocatedCallback(args.Watcher().Identifier(), static_cast<int32>(Status), CloudAnchorID);
	});

	m_locateAnchorsCompletedToken = m_cloudSession.LocateAnchorsCompleted(winrt::auto_revoke, [this](auto&&, auto&& args)
	{
		{ std::wstringstream string; string << L"LocateAnchorsCompleted watcher " << args.Watcher().Identifier() << " has completed."; Log(string); }
		LoadCloudAnchorByID_Completed(args);
		LocateAnchorsCompletedCallback(args.Watcher().Identifier(), args.Cancelled());
	});

	m_sessionUpdatedToken = m_cloudSession.SessionUpdated(winrt::auto_revoke, [this](auto&&, auto&& args)
	{
		winrt::Microsoft::Azure::SpatialAnchors::SessionStatus status = args.Status();
		m_enoughDataForSaving = status.RecommendedForCreateProgress() >= 1.0f;
		SessionUpdatedCallback(status.ReadyForCreateProgress(), status.RecommendedForCreateProgress(), status.SessionCreateHash(), status.SessionLocateHash(), static_cast<int32>(status.UserFeedback()));
	});

	m_errorToken = m_cloudSession.Error(winrt::auto_revoke, [this](auto&&, auto&& args)
	{
		m_logText = L"CloudSession ErrorMessage: ";
		m_logText.append(args.ErrorMessage());
		Log(m_logText.c_str());
	});

	m_onLogDebugToken = m_cloudSession.OnLogDebug(winrt::auto_revoke, [this](auto&&, auto&& args)
	{
		m_logText = L"CloudSession LogDebug: ";
		m_logText.append(args.Message());
		Log(m_logText.c_str());
	});
}

void AzureSpatialAnchorsInteropImpl::RemoveEventListeners()
{
	if (m_cloudSession != nullptr)
	{
		m_anchorLocatedToken.revoke();
		m_locateAnchorsCompletedToken.revoke();
		m_sessionUpdatedToken.revoke();
		m_errorToken.revoke();
		m_onLogDebugToken.revoke();
	}
}

AzureSpatialAnchorsInterop::CloudAnchorID AzureSpatialAnchorsInteropImpl::GetNextCloudAnchorID()
{
	static std::atomic<int> NextCloudAnchorID(0);
	return NextCloudAnchorID++;
}

winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* AzureSpatialAnchorsInteropImpl::GetCloudAnchor(CloudAnchorID cloudAnchorID)
{
	auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
	auto& iterator = m_cloudAnchors.find(cloudAnchorID);
	if (iterator == m_cloudAnchors.end())
	{
		return nullptr;
	}
	return &(iterator->second);
}

AzureSpatialAnchorsInterop::CloudAnchorID AzureSpatialAnchorsInteropImpl::CloudAnchorIdentifierToID(const CloudAnchorIdentifier& CloudAnchorIdentifier) const
{
	auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
	for (auto& itr : m_cloudAnchors)
	{
		if (itr.second.Identifier() == CloudAnchorIdentifier)
		{
			return itr.first;
		}
	}
	return CloudAnchorID_Invalid;
}

AzureSpatialAnchorsInterop::CloudAnchorID AzureSpatialAnchorsInteropImpl::CloudAnchorIdentifierToID(const winrt::hstring& CloudAnchorIdentifier) const
{
	auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
	for (auto& itr : m_cloudAnchors)
	{
		{ std::wstringstream string; string << L"CloudAnchorIdentifierToID " << CloudAnchorIdentifier.c_str() << L" " << itr.second.Identifier().c_str(); Log(string); }

		if (itr.second.Identifier() == CloudAnchorIdentifier)
		{
			return itr.first;
			{ std::wstringstream string; string << L"CloudAnchorIdentifierToID returning " << itr.first; Log(string); }
		}
	}
	{ std::wstringstream string; string << L"CloudAnchorIdentifierToID returning invalid"; Log(string); }
	return CloudAnchorID_Invalid;
}
