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
	virtual void DestroySession() override;

	virtual bool HasEnoughDataForSaving() override;


	virtual bool CreateARPinAroundAzureCloudSpatialAnchor(const LocalAnchorID& localAnchorId, CloudAnchorID cloudAnchorID) override;


	virtual void GetAccessTokenWithAccountKeyAsync(const wchar_t* AccountKey, Callback_Result_String Callback) override;
	virtual void GetAccessTokenWithAuthenticationTokenAsync(const wchar_t* AuthenticationToken, Callback_Result_String Callback) override;
	virtual ASAResult StartSession() override;
	virtual void StopSession() override;
	virtual ASAResult ResetSession() override;
	virtual void DisposeSession() override;
	virtual void GetSessionStatusAsync(Callback_Result_SessionStatus Callback) override;
	virtual ASAResult ConstructAnchor(const LocalAnchorID& InLocalAnchorID, CloudAnchorID& OutCloudAnchorID) override;
	virtual void CreateAnchorAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback) override;
	virtual void DeleteAnchorAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback) override;
	virtual ASAResult CreateWatcher(const LocateCriteria& InLocateCriteria, WatcherID& OutWatcherID, StringOutParam& OutErrorString) override;
	virtual ASAResult GetActiveWatchers(IntArrayOutParam& OutWatcherIDs) override;
	virtual void GetAnchorPropertiesAsync(const wchar_t* InCloudAnchorIdentifier, Callback_Result_CloudAnchorID Callback) override;
	virtual void RefreshAnchorPropertiesAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback) override;
	virtual void UpdateAnchorPropertiesAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback) override;
	virtual ASAResult GetConfiguration(SessionConfig& OutConfig) override;
	virtual ASAResult SetConfiguration(const SessionConfig& InConfig) override;
	virtual ASAResult SetLocationProvider(const LocationProviderConfig& InConfig) override;
	virtual ASAResult GetLogLevel(int32_t& OutLogVerbosity) override;
	virtual ASAResult SetLogLevel(int32_t InLogVerbosity) override;
	//virtual ASAResult GetSession() override;
	//virtual ASAResult SetSession() override;
	virtual ASAResult GetSessionId(std::wstring& OutSessionID) override;

	virtual ASAResult StopWatcher(AzureSpatialAnchorsInterop::WatcherID WatcherIdentifier) override;

	virtual ASAResult GetCloudSpatialAnchorIdentifier(CloudAnchorID InCloudAnchorID, StringOutParam& OutCloudAnchorIdentifier) override;
	virtual ASAResult SetCloudAnchorExpiration(CloudAnchorID InCloudAnchorID, float InLifetimeInSeconds) override;
	virtual ASAResult GetCloudAnchorExpiration(CloudAnchorID InCloudAnchorID, float& OutLifetimeInSeconds) override;
	virtual ASAResult SetCloudAnchorAppProperties(CloudAnchorID InCloudAnchorID, int InNumAppProperties, const wchar_t** InAppProperties) override;
	virtual ASAResult GetCloudAnchorAppProperties(CloudAnchorID InCloudAnchorID, StringArrayOutParam& OutAppProperties) override;

	virtual ASAResult SetDiagnosticsConfig(DiagnosticsConfig& InConfig) override;
	virtual void CreateDiagnosticsManifestAsync(const wchar_t* Description, Callback_Result_String Callback) override;
	virtual void SubmitDiagnosticsManifestAsync(const wchar_t* ManifestPath, Callback_Result Callback) override;


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

	winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::GetAccessTokenWithAccountKey_Coroutine(const wchar_t* AccountKey, Callback_Result_String Callback);
	winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::GetAccessTokenWithAuthenticationToken_Coroutine(const wchar_t* AuthenticationToken, Callback_Result_String Callback);
	winrt::fire_and_forget GetSessionStatus_Coroutine(Callback_Result_SessionStatus Callback);
	winrt::fire_and_forget CreateAnchor_Coroutine(CloudAnchorID InCloudAnchorID, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* InCloudAnchor, Callback_Result Callback);
	winrt::fire_and_forget DeleteAnchor_Coroutine(CloudAnchorID InCloudAnchorID, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor, Callback_Result Callback);
	winrt::fire_and_forget UpdateCloudAnchorProperties_Coroutine(CloudAnchorID InCloudAnchorID, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor, Callback_Result Callback);
	winrt::fire_and_forget GetAnchorProperties_Coroutine(const wchar_t* CloudAnchorIdentifier, Callback_Result_CloudAnchorID Callback);
	winrt::fire_and_forget RefreshCloudAnchorProperties_Coroutine(CloudAnchorID InCloudAnchorID, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor, Callback_Result Callback);
	winrt::fire_and_forget CreateDiagnosticsManifest_Coroutine(const wchar_t* Description, Callback_Result_String Callback);
	winrt::fire_and_forget SubmitDiagnosticsManifest_Coroutine(const wchar_t* ManifestPath, Callback_Result Callback);


	CloudAnchorID GetNextCloudAnchorID();

	void AddEventListeners();
	void RemoveEventListeners();

	void Log(const wchar_t* LogMsg) const;
	void Log(std::wstringstream& stream) const;

	WindowsMixedReality::MixedRealityInterop& m_mixedRealityInterop;

	// Function pointer for logging
	LogFunctionPtr OnLog;

	// Function Pointers for asa event callbacks 
	AnchorLocatedCallbackPtr AnchorLocatedCallback;
	LocateAnchorsCompletedCallbackPtr LocateAnchorsCompletedCallback;
	SessionUpdatedCallbackPtr SessionUpdatedCallback;


	static AzureSpatialAnchorsInteropImpl* Instance;

	bool m_enoughDataForSaving{ false };
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
	mutable std::mutex m_watcherMapMutex;

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* GetCloudAnchor(CloudAnchorID cloudAnchorID);
	CloudAnchorID CloudAnchorIdentifierToID(const winrt::hstring& CloudAnchorIdentifier) const;
	
	static void Deleter(const void* ArrayPtr);
};

void AzureSpatialAnchorsInteropImpl::Deleter(const void* ArrayPtr) 
{
	delete[] ArrayPtr;
}

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

	AddEventListeners();

	return m_cloudSession != nullptr;
}

void AzureSpatialAnchorsInteropImpl::DestroySession()
{
	Log(L"DestroySession");

	if (m_cloudSession == nullptr)
	{
		Log(L"DestroySession called, but session does not exist!  Ignoring.");
		return;
	}

	//DestroySession
	RemoveEventListeners();
	{
		auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
		m_cloudAnchors.clear();
	}
	m_sessionStarted = false;
	m_enoughDataForSaving = false;
	m_cloudSession = nullptr;
}

bool AzureSpatialAnchorsInteropImpl::HasEnoughDataForSaving()
{
	return m_enoughDataForSaving;
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
	// Note: IDs must remain unique across the creation of multiple Interop's in a UE4 app lifetime (important for remoting which can run multiple interops).
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

AzureSpatialAnchorsInterop::CloudAnchorID AzureSpatialAnchorsInteropImpl::CloudAnchorIdentifierToID(const winrt::hstring& CloudAnchorIdentifier) const
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


void AzureSpatialAnchorsInteropImpl::GetAccessTokenWithAccountKeyAsync(const wchar_t* AccountKey, Callback_Result_String Callback)
{
	{ std::wstringstream string; string << L"GetSessionStatusAsync"; Log(string); }

	if (!CheckForSession(L"GetSessionStatusAsync"))
	{
		Callback(ASAResult::FailNoSession, L"", L"");
		return;
	}

	GetAccessTokenWithAccountKey_Coroutine(AccountKey, Callback);
}
winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::GetAccessTokenWithAccountKey_Coroutine(const wchar_t* InAccountKey, Callback_Result_String Callback)
{
	ASAResult Result = ASAResult::NotStarted;
	const wchar_t* Error = nullptr;
	winrt::hstring AccessToken;
	try
	{
		AccessToken = co_await m_cloudSession.GetAccessTokenWithAccountKeyAsync(InAccountKey);

		Result = ASAResult::Success;
	}
	catch (winrt::hresult_error e)
	{
		{ std::wstringstream string; string << L"GetAccessTokenWithAccountKey_Coroutine failed to get status. message: " << e.message().c_str(); Log(string); }
		Result = ASAResult::FailSeeErrorString;
		Error = e.message().c_str();
	}

	Callback(Result, Error, AccessToken.c_str());
}

void AzureSpatialAnchorsInteropImpl::GetAccessTokenWithAuthenticationTokenAsync(const wchar_t* AuthenticationToken, Callback_Result_String Callback)
{
	{ std::wstringstream string; string << L"GetSessionStatusAsync"; Log(string); }

	if (!CheckForSession(L"GetSessionStatusAsync"))
	{
		Callback(ASAResult::FailNoSession, L"", L"");
		return;
	}

	GetAccessTokenWithAuthenticationToken_Coroutine(AuthenticationToken, Callback);
}
winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::GetAccessTokenWithAuthenticationToken_Coroutine(const wchar_t* InAuthenticationToken, Callback_Result_String Callback)
{
	ASAResult Result = ASAResult::NotStarted;
	const wchar_t* Error = nullptr;
	winrt::hstring AccessToken;
	try
	{
		AccessToken = co_await m_cloudSession.GetAccessTokenWithAuthenticationTokenAsync(InAuthenticationToken);

		Result = ASAResult::Success;
	}
	catch (winrt::hresult_error e)
	{
		{ std::wstringstream string; string << L"GInAuthenticationToken_Coroutine failed to get status. message: " << e.message().c_str(); Log(string); }
		Result = ASAResult::FailSeeErrorString;
		Error = e.message().c_str();
	}

	Callback(Result, Error, AccessToken.c_str());
}

AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::StartSession()
{
	Log(L"StartSession");

	if (m_cloudSession == nullptr)
	{
		Log(L"StartSession called, but session does not exist!  Ignoring.");
		return ASAResult::FailNoSession;
	}

	if (m_sessionStarted == true)
	{
		Log(L"StartSession called, but session is already started.  Ignoring.");
		return ASAResult::FailAlreadyStarted;
	}

	m_cloudSession.Start();
	m_sessionStarted = true;

	return ASAResult::Success;
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

	m_sessionStarted = false;
	m_cloudSession.Stop();
}

AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::ResetSession()
{
	Log(L"ResetSession");

	if (m_cloudSession == nullptr)
	{
		Log(L"ResetSession called, but session has already been cleaned up.  Ignoring.");
		return ASAResult::FailNoSession;
	}

	m_cloudSession.Reset();
	return ASAResult::Success;
}

void AzureSpatialAnchorsInteropImpl::DisposeSession()
{
	Log(L"DisposeSession");

	if (m_cloudSession == nullptr)
	{
		Log(L"DisposeSession called, but no session exists.  Ignoring.");
		return;
	}

	m_cloudSession.Dispose();
	m_cloudSession = nullptr;
}

void AzureSpatialAnchorsInteropImpl::GetSessionStatusAsync(Callback_Result_SessionStatus Callback)
{
	{ std::wstringstream string; string << L"GetSessionStatusAsync"; Log(string); }

	if (!CheckForSession(L"GetSessionStatusAsync"))
	{
		SessionStatus EmptyStatus;
		Callback(ASAResult::FailNoSession, L"", EmptyStatus);
		return;
	}

	GetSessionStatus_Coroutine(Callback);
}
winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::GetSessionStatus_Coroutine(Callback_Result_SessionStatus Callback)
{
	ASAResult Result = ASAResult::NotStarted;
	const wchar_t* Error = nullptr;
	SessionStatus Status;
	try
	{
		{ std::wstringstream string; string << L"GetSessionStatus_Coroutine getting status."; Log(string); }

		winrt::Microsoft::Azure::SpatialAnchors::SessionStatus NativeStatus = co_await m_cloudSession.GetSessionStatusAsync();

		Status.ReadyForCreateProgress		= NativeStatus.ReadyForCreateProgress();
		Status.RecommendedForCreateProgress = NativeStatus.RecommendedForCreateProgress();
		Status.SessionCreateHash			= NativeStatus.SessionCreateHash();
		Status.SessionLocateHash			= NativeStatus.SessionLocateHash();
		Status.UserFeedback					= static_cast<int32_t>(NativeStatus.UserFeedback());

		{ std::wstringstream string; string << L"GetSessionStatus_Coroutine got status."; Log(string); }
		Result = ASAResult::Success;
	}
	catch (winrt::hresult_error e)
	{
		{ std::wstringstream string; string << L"GetSessionStatus_Coroutine failed to get status. message: " << e.message().c_str(); Log(string); }
		Result = ASAResult::FailSeeErrorString;
		Error = e.message().c_str();
	}

	Callback(Result, Error, Status);
}

AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::ConstructAnchor(const LocalAnchorID& LocalAnchorID, CloudAnchorID& OutCloudAnchorID)
{
	if (LocalAnchorID == nullptr || LocalAnchorID[0] == 0)
	{
		Log(L"ConstructAnchor failed because LocalAnchorId is null or empty!");
		return ASAResult::FailBadLocalAnchorID;
	}

	{ std::wstringstream string; string << L"ConstructAnchor from a local anchor " << LocalAnchorID; Log(string); }

	std::shared_ptr<class SpatialAnchorHelper> spatialAnchorHelper = GetSpatialAnchorHelper();
	winrt::Windows::Perception::Spatial::SpatialAnchor* localAnchor = spatialAnchorHelper->GetSpatialAnchor(LocalAnchorID);
	if (localAnchor == nullptr)
	{
		{ std::wstringstream string; string << L"ConstructAnchor failed because localAnchorId " << LocalAnchorID << L" does not exist!  You must create the local anchor first."; Log(string); }
		return ASAResult::FailNoAnchor;
	}
	else
	{
		winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor newCloudAnchor = CloudSpatialAnchor();
		newCloudAnchor.LocalAnchor(*localAnchor);
		OutCloudAnchorID = GetNextCloudAnchorID();
		auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
		m_cloudAnchors.insert(std::make_pair(OutCloudAnchorID, newCloudAnchor));
		return ASAResult::Success;
	}
}

void AzureSpatialAnchorsInteropImpl::CreateAnchorAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback)
{
	{ std::wstringstream string; string << L"CreateAnchorAsync for CloudAnchorID " << InCloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(InCloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"CreateAnchorAsync failed because cloud anchor for CloudAnchorID " << InCloudAnchorID << L" does not exist!"; Log(string); }
		Callback(ASAResult::FailNoCloudAnchor, nullptr);
		return;
	}

	//if (!m_enoughDataForSaving)
	//{
	//	Log(L"CreateAnchorAsync Cannot save AzureSpatialAnchor yet, not enough data. Look around more then try again.");
	//	Callback(ASAResult::FailNotEnoughData, nullptr);
	//	return;
	//}

	if (!CheckForSession(L"CreateAnchorAsync"))
	{
		Callback(ASAResult::FailNoSession, nullptr);
		return;
	}

	CreateAnchor_Coroutine(InCloudAnchorID, cloudAnchor, Callback);
}
winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::CreateAnchor_Coroutine(CloudAnchorID InCloudAnchorID, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* InCloudAnchor, Callback_Result Callback)
{
	assert(InCloudAnchor);

	ASAResult Result = ASAResult::NotStarted;
	const wchar_t* Error = nullptr;
	try
	{
		{ std::wstringstream string; string << L"CreateAnchor_Coroutine saving cloud anchor " << InCloudAnchorID; Log(string); }

		co_await m_cloudSession.CreateAnchorAsync(*InCloudAnchor);

		{ std::wstringstream string; string << L"CreateAnchor_Coroutine saved cloud anchor [" << InCloudAnchorID << L"] with cloud Identifier [" << InCloudAnchor->Identifier().c_str() << L"]"; Log(string); }
		Result = ASAResult::Success;
	}
	catch (winrt::hresult_error e)
	{
		{ std::wstringstream string; string << L"CreateAnchor_Coroutine failed to save cloud anchor [" << InCloudAnchorID << L" message: " << e.message().c_str(); Log(string); }
		Result = ASAResult::FailSeeErrorString;
		Error = e.message().c_str();
	}

	{ std::wstringstream string; string << L"CreateAnchor_Coroutine making callback"; Log(string); }
	Callback(Result, Error);
}

void AzureSpatialAnchorsInteropImpl::DeleteAnchorAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback)
{
	{ std::wstringstream string; string << L"DeleteAnchorAsync for CloudAnchorID " << InCloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(InCloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"DeleteAnchorAsync failed because cloud anchor for CloudAnchorID " << InCloudAnchorID << L" does not exist!"; Log(string); }
		Callback(ASAResult::FailNoCloudAnchor, nullptr);
		return;
	}

	if (!CheckForSession(L"DeleteAnchorAsync"))
	{
		Callback(ASAResult::FailNoSession, nullptr);
		return;
	}

	DeleteAnchor_Coroutine(InCloudAnchorID, cloudAnchor, Callback);
}
winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::DeleteAnchor_Coroutine(CloudAnchorID InCloudAnchorID, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* InCloudAnchor, Callback_Result Callback)
{
	assert(InCloudAnchor);

	ASAResult Result = ASAResult::NotStarted;
	const wchar_t* Error = nullptr;
	try
	{
		co_await m_cloudSession.DeleteAnchorAsync(*InCloudAnchor);
		{
			auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
			m_cloudAnchors.erase(InCloudAnchorID);
		}

		Result = ASAResult::Success;
		{ std::wstringstream string; string << L"DeleteAnchor deleted cloud anchor " << InCloudAnchorID; Log(string); }
	}
	catch (winrt::hresult_error e)
	{
		Result = ASAResult::FailSeeErrorString;
		Error = e.message().c_str();
		{ std::wstringstream string; string << L"SaveCloudAnchor failed to delete cloud anchor " << InCloudAnchorID << L" message: " << e.message().c_str(); Log(string); }
	}

	Callback(Result, Error);
}

AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::CreateWatcher(const LocateCriteria& InLocateCriteria, WatcherID& OutWatcherID, StringOutParam& OutErrorString)
{
	Log(L"CreateWatcher");

	if (m_cloudSession == nullptr)
	{
		{ std::wstringstream string; string << L"CreateWatcher failed because there is no session.  You must create the AzureSpatialAnchors session first."; Log(string); }
		return ASAResult::FailNoSession;
	}

	AnchorLocateCriteria criteria = AnchorLocateCriteria();

	criteria.BypassCache(InLocateCriteria.bBypassCache);

	if (InLocateCriteria.NearCloudAnchorID != CloudAnchorID_Invalid)
	{
		winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* sourceAnchor = GetCloudAnchor(InLocateCriteria.NearCloudAnchorID);
		if (!sourceAnchor)
		{
			{ std::wstringstream string; string << L"CreateWatcher failed because cloud anchor with NearCloudAnchorID " << InLocateCriteria.NearCloudAnchorID << L" does not exist!"; Log(string); }
			return ASAResult::FailNoCloudAnchor;
		}
		NearAnchorCriteria nearAnchorCriteria;
		nearAnchorCriteria.DistanceInMeters(InLocateCriteria.NearCloudAnchorDistance);
		nearAnchorCriteria.MaxResultCount(InLocateCriteria.NearCloudAnchorMaxResultCount);
		nearAnchorCriteria.SourceAnchor(*sourceAnchor);
		criteria.NearAnchor(nearAnchorCriteria);
	}

	if (InLocateCriteria.SearchNearDevice)
	{
		NearDeviceCriteria nearDeviceCriteria = NearDeviceCriteria();
		nearDeviceCriteria.DistanceInMeters(InLocateCriteria.NearDeviceDistance);
		nearDeviceCriteria.MaxResultCount(InLocateCriteria.NearDeviceMaxResultCount);
		criteria.NearDevice(nearDeviceCriteria);
	}

	if (InLocateCriteria.NumIdentifiers > 0)
	{
		std::vector<winrt::hstring> Identifiers;
		Identifiers.reserve(InLocateCriteria.NumIdentifiers);
		for (int32_t i = 0; i < InLocateCriteria.NumIdentifiers; ++i)
		{
			Identifiers.push_back(InLocateCriteria.Identifiers[i]);
		}
		criteria.Identifiers(Identifiers);
	}

	assert(static_cast<AnchorDataCategory>(InLocateCriteria.AzureSpatialAnchorDataCategory) >= AnchorDataCategory::None && static_cast<AnchorDataCategory>(InLocateCriteria.AzureSpatialAnchorDataCategory) <= AnchorDataCategory::Spatial);
	criteria.RequestedCategories(static_cast<AnchorDataCategory>(InLocateCriteria.AzureSpatialAnchorDataCategory));
	assert(static_cast<LocateStrategy>(InLocateCriteria.AzureSptialAnchorsLocateStrategy) >= LocateStrategy::AnyStrategy && static_cast<LocateStrategy>(InLocateCriteria.AzureSptialAnchorsLocateStrategy) <= LocateStrategy::VisualInformation);
	criteria.Strategy(static_cast<LocateStrategy>(InLocateCriteria.AzureSptialAnchorsLocateStrategy));

	{
		try
		{
			auto& watcher = m_cloudSession.CreateWatcher(criteria);
			auto lock = std::unique_lock<std::mutex>(m_watcherMapMutex);
			m_watcherMap.insert({ static_cast<WatcherID>(watcher.Identifier()), watcher });
			OutWatcherID = watcher.Identifier();
			{ std::wstringstream string; string << L"CreateWatcher created watcher " << watcher.Identifier(); Log(string); }
			return ASAResult::Success;
		}
		catch (winrt::hresult_error e)
		{
			OutErrorString.Set(Deleter, e.message().size(), e.message().c_str());
			{ std::wstringstream string; string << L"CreateWatcher failed to create watcher.  message: " << e.message().c_str(); Log(string); }
			return ASAResult::FailSeeErrorString;
		}
	}
}

AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::GetActiveWatchers(IntArrayOutParam& OutWatcherIDs)
{
	Log(L"ResetSession");

	if (m_cloudSession == nullptr)
	{
		Log(L"GetActiveWatchers called, but session has already been cleaned up.  Returning empty list.");
		return ASAResult::FailNoSession;
	}

	winrt::Windows::Foundation::Collections::IVectorView<winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchorWatcher> Watchers = m_cloudSession.GetActiveWatchers();

	OutWatcherIDs.SetArraySize(Deleter, Watchers.Size());
	int32_t Index = 0;
#if _DEBUG
	// only lock and test the assert below in debug.
	auto lock = std::unique_lock<std::mutex>(m_watcherMapMutex);
#endif
	for (auto& Watcher : Watchers)
	{
		int32_t ID = Watcher.Identifier();
		OutWatcherIDs.SetArrayElement(Index, ID);
		++Index;

		// All watchers should be in the map.
#if _DEBUG
		assert(m_watcherMap.find(ID) != m_watcherMap.end());
#endif
	}

	return ASAResult::Success;
}

void AzureSpatialAnchorsInteropImpl::GetAnchorPropertiesAsync(const wchar_t* InCloudAnchorIdentifier, Callback_Result_CloudAnchorID Callback)
{
	{ std::wstringstream string; string << L"GetAnchorPropertiesAsync for cloud identifier " << InCloudAnchorIdentifier; Log(string); }

	if (!InCloudAnchorIdentifier || InCloudAnchorIdentifier[0] == 0)
	{
		{ std::wstringstream string; string << L"GetAnchorPropertiesAsync failed because cloud anchor Identifier is null or empty!"; Log(string); }
		Callback(ASAResult::FailBadCloudAnchorIdentifier, nullptr, CloudAnchorID_Invalid);
		return;
	}

	GetAnchorProperties_Coroutine(InCloudAnchorIdentifier, Callback);
}
winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::GetAnchorProperties_Coroutine(const wchar_t* CloudAnchorIdentifier, Callback_Result_CloudAnchorID Callback)
{
	assert(CloudAnchorIdentifier);

	ASAResult Result = ASAResult::NotStarted;
	const wchar_t* Error = nullptr;
	CloudAnchorID CloudAnchorID = CloudAnchorID_Invalid;
	try
	{
		winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor FoundCloudAnchor = co_await m_cloudSession.GetAnchorPropertiesAsync(CloudAnchorIdentifier);

		// If we already have this CloudAnchor return it's ID.
		CloudAnchorID = CloudAnchorIdentifierToID(FoundCloudAnchor.Identifier());
		if (CloudAnchorID == CloudAnchorID_Invalid)
		{
			CloudAnchorID = GetNextCloudAnchorID();
			{
				auto lock = std::unique_lock<std::mutex>(m_cloudAnchorsMutex);
				m_cloudAnchors.insert(std::make_pair(CloudAnchorID, FoundCloudAnchor));
			}
		}

		Result = ASAResult::Success;
		{ std::wstringstream string; string << L"GetAnchorProperties found anchor " << CloudAnchorID << L"with identifier " << CloudAnchorIdentifier; Log(string); }
	}
	catch (winrt::hresult_error e)
	{
		Result = ASAResult::FailSeeErrorString;
		Error = e.message().c_str();
		{ std::wstringstream string; string << L"GetAnchorProperties failed to find cloud anchor with identifier " << CloudAnchorIdentifier << L" message: " << e.message().c_str(); Log(string); }
	}

	Callback(Result, Error, CloudAnchorID);
}

void AzureSpatialAnchorsInteropImpl::RefreshAnchorPropertiesAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback)
{
	{ std::wstringstream string; string << L"RefreshCloudAnchorProperties for cloud anchor " << InCloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(InCloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"RefreshCloudAnchorProperties failed because cloud anchor " << InCloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		Callback(ASAResult::FailNoAnchor, nullptr);
		return;
	}

	if (!CheckForSession(L"RefreshCloudAnchorProperties"))
	{
		Callback(ASAResult::FailNoSession, nullptr);
		return;
	}

	RefreshCloudAnchorProperties_Coroutine(InCloudAnchorID, cloudAnchor, Callback);
}
winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::RefreshCloudAnchorProperties_Coroutine(CloudAnchorID InCloudAnchorID, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* InCloudAnchor, Callback_Result Callback)
{
	assert(InCloudAnchor);

	ASAResult Result = ASAResult::NotStarted;
	const wchar_t* Error = nullptr;
	try
	{
		co_await m_cloudSession.RefreshAnchorPropertiesAsync(*InCloudAnchor);

		Result = ASAResult::Success;
		{ std::wstringstream string; string << L"RefreshCloudAnchorProperties refreshed cloud anchor " << InCloudAnchorID; Log(string); }
	}
	catch (winrt::hresult_error e)
	{
		Result = ASAResult::FailSeeErrorString;
		Error = e.message().c_str();
		{ std::wstringstream string; string << L"RefreshCloudAnchorProperties failed to refresh cloud anchor " << InCloudAnchorID << L" message: " << e.message().c_str(); Log(string); }
	}

	Callback(Result, Error);
}

void AzureSpatialAnchorsInteropImpl::UpdateAnchorPropertiesAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback)
{
	{ std::wstringstream string; string << L"UpdateCloudAnchorProperties for cloud anchor " << InCloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(InCloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"UpdateCloudAnchorProperties failed because cloud anchor " << InCloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		Callback(ASAResult::FailNoAnchor, nullptr);
		return;
	}

	if (!CheckForSession(L"UpdateCloudAnchorProperties"))
	{
		Callback(ASAResult::FailNoSession, nullptr);
		return;
	}

	UpdateCloudAnchorProperties_Coroutine(InCloudAnchorID, cloudAnchor, Callback);
}
winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::UpdateCloudAnchorProperties_Coroutine(CloudAnchorID InCloudAnchorID, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* InCloudAnchor, Callback_Result Callback)
{
	assert(InCloudAnchor);

	ASAResult Result = ASAResult::NotStarted;
	const wchar_t* Error = nullptr;
	try
	{
		co_await m_cloudSession.UpdateAnchorPropertiesAsync(*InCloudAnchor);

		Result = ASAResult::Success;
		{ std::wstringstream string; string << L"UpdateCloudAnchorProperties updated cloud anchor " << InCloudAnchorID; Log(string); }
	}
	catch (winrt::hresult_error e)
	{
		Result = ASAResult::FailSeeErrorString;
		Error = e.message().c_str();
		{ std::wstringstream string; string << L"UpdateCloudAnchorProperties failed to update cloud anchor " << InCloudAnchorID << L" message: " << e.message().c_str(); Log(string); }
	}

	Callback(Result, Error);
}

AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::GetConfiguration(SessionConfig& OutConfig)
{
	if (m_cloudSession == nullptr)
	{
		Log(L"GetConfiguration called, but no session exists.  Ignoring.");
		return ASAResult::FailNoSession;
	}

	const winrt::Microsoft::Azure::SpatialAnchors::SessionConfiguration& Config = m_cloudSession.Configuration();
	OutConfig.AccessToken			= Config.AccessToken().c_str();
	OutConfig.AccountDomain			= Config.AccountDomain().c_str();
	OutConfig.AccountId				= Config.AccountId().c_str();
	OutConfig.AccountKey			= Config.AccountKey().c_str();
	OutConfig.AuthenticationToken	= Config.AuthenticationToken().c_str();
	return ASAResult::Success;
}
AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::SetConfiguration(const SessionConfig& InConfig)
{
	if (m_cloudSession == nullptr)
	{
		Log(L"SetConfiguration called, but no session exists.  Ignoring.");
		return ASAResult::FailNoSession;
	}

	Log(L"SetConfiguration");

	const winrt::Microsoft::Azure::SpatialAnchors::SessionConfiguration& Config = m_cloudSession.Configuration();
	if (InConfig.AccessToken && InConfig.AccessToken[0] != 0) Config.AccessToken(InConfig.AccessToken);
	if (InConfig.AccountDomain && InConfig.AccountDomain[0] != 0) Config.AccountDomain(InConfig.AccountDomain);
	if (InConfig.AccountId && InConfig.AccountId[0] != 0) Config.AccountId(InConfig.AccountId);
	if (InConfig.AccountKey && InConfig.AccountKey[0] != 0) Config.AccountKey(InConfig.AccountKey);
	if (InConfig.AuthenticationToken && InConfig.AuthenticationToken[0] != 0) Config.AuthenticationToken(InConfig.AuthenticationToken);
	return ASAResult::Success;
}

AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::SetLocationProvider(const LocationProviderConfig& InConfig)
{
	if (m_cloudSession == nullptr)
	{
		Log(L"SetLocationProvider called, but no session exists.  Ignoring.");
		return ASAResult::FailNoSession;
	}

	Log(L"SetLocationProvider");

	// Do Coarse Localization Setup
	if (InConfig.bCoarseLocalizationEnabled)
	{
		// Create the sensor fingerprint provider
		PlatformLocationProvider sensorProvider = PlatformLocationProvider();
		SensorCapabilities sensors = sensorProvider.Sensors();

		// Allow GPS
		sensors.GeoLocationEnabled(InConfig.bEnableGPS);

		// Allow WiFi scanning
		// Note :if wifi scanning is enabled when remoting an exception will fire soon after session start, but it will be handled.  Localization does work, but perhaps wifi scanning is not working.
		sensors.WifiEnabled(InConfig.bEnableWifi);

		// Bluetooth beacons
		if (InConfig.NumBLEBeaconUUIDs > 0)
		{
			// Populate the set of known BLE beacons' UUIDs
			std::vector<winrt::hstring> uuids;
			uuids.reserve(InConfig.NumBLEBeaconUUIDs);
			for (int32_t i = 0; i < InConfig.NumBLEBeaconUUIDs; ++i)
			{
				uuids.emplace_back(InConfig.BLEBeaconUUIDs[i]);
			}

			// Allow the set of known BLE beacons
			sensors.BluetoothEnabled(true);
			sensors.KnownBeaconProximityUuids(uuids);
		}

		// Set the session's sensor fingerprint provider
		m_cloudSession.LocationProvider(sensorProvider);
	}

	return ASAResult::Success;
}

AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::GetLogLevel(int32_t& OutLogVerbosity)
{
	if (m_cloudSession == nullptr)
	{
		Log(L"GetLogLevel called, but no session exists.  Ignoring.");
		return ASAResult::FailNoSession;
	}

	OutLogVerbosity = static_cast<int32_t>(m_cloudSession.LogLevel());
	return ASAResult::Success;
}
AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::SetLogLevel(int32_t InLogVerbosity)
{
	if (m_cloudSession == nullptr)
	{
		Log(L"SetLogLevel called, but no session exists.  Ignoring.");
		return ASAResult::FailNoSession;
	}

	Log(L"SetLogLevel");

	SessionLogLevel logLevel = (SessionLogLevel)InLogVerbosity;
	if ((logLevel < SessionLogLevel::None) || (logLevel > SessionLogLevel::All))
	{
		logLevel = (logLevel < SessionLogLevel::None) ? SessionLogLevel::None : SessionLogLevel::All;
		{ std::wstringstream string; string << L"ConfigSession called with invalid log level " << InLogVerbosity << ".  Clamping the value to " << (int)logLevel; Log(string); }
	}

	m_cloudSession.LogLevel(logLevel);

	//AddEventListeners();
	return ASAResult::Success;
}

//AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::GetSession(){}
//AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::SetSession(){}

AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::GetSessionId(std::wstring& OutSessionID)
{
	if (m_cloudSession == nullptr)
	{
		Log(L"GetSessionId called, but no session exists.  Returning empty string.");
		OutSessionID = L"";
		return ASAResult::FailNoSession;
	}
	OutSessionID = m_cloudSession.SessionId();
	return ASAResult::Success;
};


AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::StopWatcher(AzureSpatialAnchorsInterop::WatcherID WatcherIdentifier)
{
	auto lock = std::unique_lock<std::mutex>(m_watcherMapMutex);
	auto itr = m_watcherMap.find(WatcherIdentifier);
	if (itr == m_watcherMap.end())
	{
		{ std::wstringstream string; string << L"StopWatcher watcher: " << WatcherIdentifier << " does not exist!  Ignoring."; Log(string); }
		return ASAResult::FailNoWatcher;
	}
	else
	{
		{ std::wstringstream string; string << L"StopWatcher stop watcher: " << WatcherIdentifier; Log(string); }
		winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchorWatcher& watcher = itr->second;
		watcher.Stop();
		return ASAResult::Success;
	}
}


AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::GetCloudSpatialAnchorIdentifier(CloudAnchorID InCloudAnchorID, StringOutParam& OutCloudAnchorIdentifier)
{
	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* CloudAnchor = GetCloudAnchor(InCloudAnchorID);
	if (CloudAnchor)
	{
		OutCloudAnchorIdentifier.Set(Deleter, CloudAnchor->Identifier().size(), CloudAnchor->Identifier().c_str());
		return ASAResult::Success;
	}
	else
	{
		OutCloudAnchorIdentifier.Set(Deleter, 0, L"");
		return ASAResult::FailAnchorDoesNotExist;
	}
}

AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::SetCloudAnchorExpiration(CloudAnchorID InCloudAnchorID, float InLifetimeInSeconds)
{
	{ std::wstringstream string; string << L"SetCloudAnchorExpiration for anchor " << InCloudAnchorID; Log(string); }

	if (InLifetimeInSeconds <= 0.0f)
	{
		{ std::wstringstream string; string << L"Warning: SetCloudAnchorExpiration setting with lifetime " << InLifetimeInSeconds << " which is invalid!  Expiration not set."; Log(string); }
		return ASAResult::FailBadLifetime;
	}
	int64 lifetimeInt = static_cast<int64>(std::ceil(InLifetimeInSeconds));
	const winrt::Windows::Foundation::TimeSpan future{ std::chrono::seconds{ lifetimeInt } };
	const winrt::Windows::Foundation::DateTime expiration = winrt::clock::now() + future;

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(InCloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SetCloudAnchorExpiration failed because cloudAnchorID " << InCloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		return ASAResult::FailNoCloudAnchor;
	}
	else
	{
		cloudAnchor->Expiration(expiration);
		return ASAResult::Success;
	}
}
AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::GetCloudAnchorExpiration(CloudAnchorID InCloudAnchorID, float& OutLifetimeInSeconds)
{
	{ std::wstringstream string; string << L"GetCloudAnchorExpiration for anchor " << InCloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(InCloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SetCloudAnchorExpiration failed because cloudAnchorID " << InCloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		return ASAResult::FailNoCloudAnchor;
	}
	else
	{
		const winrt::Windows::Foundation::TimeSpan lifetimeSpan = cloudAnchor->Expiration() - winrt::clock::now();
		typedef std::chrono::duration<float> floatseconds; // +- about 30 years is representable
		floatseconds seconds = std::chrono::duration_cast<floatseconds>(lifetimeSpan);
		OutLifetimeInSeconds = seconds.count();
		return ASAResult::Success;
	}
}
AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::SetCloudAnchorAppProperties(CloudAnchorID InCloudAnchorID, int InNumAppProperties, const wchar_t** InAppProperties)
{
	{ std::wstringstream string; string << L"SetCloudAnchorAppProperties for anchor " << InCloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(InCloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SetCloudAnchorAppProperties failed because cloudAnchorID " << InCloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		return ASAResult::FailNoCloudAnchor;
	}
	else
	{
		auto Properties = cloudAnchor->AppProperties();
		Properties.Clear();
		for (int i = 0; i < InNumAppProperties; i+=2)
		{
			Properties.Insert(InAppProperties[i], InAppProperties[i+1]);
		}

		return ASAResult::Success;
	}
}
AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::GetCloudAnchorAppProperties(CloudAnchorID InCloudAnchorID, StringArrayOutParam& OutAppProperties)
{
	{ std::wstringstream string; string << L"GetCloudAnchorAppProperties for anchor " << InCloudAnchorID; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(InCloudAnchorID);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SetCloudAnchorAppProperties failed because cloudAnchorID " << InCloudAnchorID << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		return ASAResult::FailNoCloudAnchor;
	}
	else
	{
		OutAppProperties.SetArraySize(Deleter, cloudAnchor->AppProperties().Size() * 2);
		int32_t Index = 0;
		for (auto itr : cloudAnchor->AppProperties())
		{
			OutAppProperties.SetArrayElement(Index, itr.Key().size(), itr.Key().c_str());
			OutAppProperties.SetArrayElement(Index + 1, itr.Value().size(), itr.Value().c_str());
			Index += 2;
		}

		return ASAResult::Success;
	}
}

AzureSpatialAnchorsInterop::ASAResult AzureSpatialAnchorsInteropImpl::SetDiagnosticsConfig(AzureSpatialAnchorsInterop::DiagnosticsConfig& InConfig)
{
	if (m_cloudSession == nullptr)
	{
		Log(L"SetDiagnosticsConfig called, but no session exists.  Ignoring.");
		return ASAResult::FailNoSession;
	}

	Log(L"SetDiagnosticsConfig");

	const winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchorSessionDiagnostics& Diagnostics = m_cloudSession.Diagnostics();
	Diagnostics.ImagesEnabled(InConfig.bImagesEnabled);
	Diagnostics.LogDirectory(InConfig.LogDirectory);
	Diagnostics.LogLevel(static_cast<winrt::Microsoft::Azure::SpatialAnchors::SessionLogLevel>(InConfig.LogLevel));
	Diagnostics.MaxDiskSizeInMB(InConfig.MaxDiskSizeInMB);
	return ASAResult::Success;
}
void AzureSpatialAnchorsInteropImpl::CreateDiagnosticsManifestAsync(const wchar_t* Description, Callback_Result_String Callback)
{
	{ std::wstringstream string; string << L"CreateDiagnosticsManifestAsync"; Log(string); }

	if (!CheckForSession(L"CreateDiagnosticsManifestAsync"))
	{
		Callback(ASAResult::FailNoSession, L"", L"");
		return;
	}

	CreateDiagnosticsManifest_Coroutine(Description, Callback);
}
winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::CreateDiagnosticsManifest_Coroutine(const wchar_t* Description, Callback_Result_String Callback)
{
	ASAResult Result = ASAResult::NotStarted;
	const wchar_t* Error = nullptr;
	winrt::hstring ReturnedString;
	try
	{
		ReturnedString = co_await m_cloudSession.Diagnostics().CreateManifestAsync(Description);

		Result = ASAResult::Success;
	}
	catch (winrt::hresult_error e)
	{
		{ std::wstringstream string; string << L"GetAccessTokenWithAccountKey_Coroutine failed to get status. message: " << e.message().c_str(); Log(string); }
		Result = ASAResult::FailSeeErrorString;
		Error = e.message().c_str();
	}

	Callback(Result, Error, ReturnedString.c_str());
}
void AzureSpatialAnchorsInteropImpl::SubmitDiagnosticsManifestAsync(const wchar_t* ManifestPath, Callback_Result Callback)
{
	{ std::wstringstream string; string << L"SubmitDiagnosticsManifestAsync"; Log(string); }

	if (!CheckForSession(L"SubmitDiagnosticsManifestAsync"))
	{
		Callback(ASAResult::FailNoSession, L"");
		return;
	}

	SubmitDiagnosticsManifest_Coroutine(ManifestPath, Callback);
}
winrt::fire_and_forget AzureSpatialAnchorsInteropImpl::SubmitDiagnosticsManifest_Coroutine(const wchar_t* ManifestPath, Callback_Result Callback)
{
	ASAResult Result = ASAResult::NotStarted;
	const wchar_t* Error = nullptr;
	try
	{
		co_await m_cloudSession.Diagnostics().SubmitManifestAsync(ManifestPath);

		Result = ASAResult::Success;
	}
	catch (winrt::hresult_error e)
	{
		{ std::wstringstream string; string << L"GetAccessTokenWithAccountKey_Coroutine failed to get status. message: " << e.message().c_str(); Log(string); }
		Result = ASAResult::FailSeeErrorString;
		Error = e.message().c_str();
	}

	Callback(Result, Error);
}
