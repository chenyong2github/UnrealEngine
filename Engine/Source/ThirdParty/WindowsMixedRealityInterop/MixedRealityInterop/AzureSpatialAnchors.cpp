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
	static void Create(WindowsMixedReality::MixedRealityInterop& interop, void(*LogFunctionPointer)(const wchar_t* LogMsg));
	static AzureSpatialAnchorsInteropImpl& Get();
	static void Release();

	virtual bool CreateSession() override;
	virtual bool ConfigSession(const wchar_t* accountId, const wchar_t* accountKey, int logVerbosity) override;
	virtual bool StartSession() override;
	virtual void StopSession() override;
	virtual void DestroySession() override;

	virtual bool HasEnoughDataForSaving() override;
	virtual bool CreateCloudAnchor(const wchar_t* anchorId) override;
	virtual bool SetCloudAnchorExpiration(const wchar_t* anchorId, int minutesFromNow) override;
	virtual bool SaveCloudAnchor(SaveAsyncDataPtr Data) override;
	virtual bool DeleteCloudAnchor(DeleteAsyncDataPtr Data) override;
	virtual bool LoadCloudAnchorByID(LoadByIDAsyncDataPtr Data) override;

protected:
	AzureSpatialAnchorsInteropImpl(WindowsMixedReality::MixedRealityInterop& interop, void(*FunctionPointer)(const wchar_t* LogMsgFunc));
	virtual ~AzureSpatialAnchorsInteropImpl();
	AzureSpatialAnchorsInteropImpl(const AzureSpatialAnchorsInteropImpl&) = delete;
	AzureSpatialAnchorsInteropImpl& operator=(const AzureSpatialAnchorsInteropImpl&) = delete;

private:
	bool CheckForSession(const wchar_t* contex) const;
	winrt::fire_and_forget SaveAnchor(SaveAsyncDataPtr Data, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor);
	winrt::fire_and_forget DeleteAnchor(DeleteAsyncDataPtr Data, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor);

	void AddEventListeners();
	void RemoveEventListeners();

	void Log(const wchar_t* LogMsg) const;
	void Log(std::wstringstream& stream) const;

	WindowsMixedReality::MixedRealityInterop& m_mixedRealityInterop;

	/** Function pointer for logging */
	void(*OnLog)(const wchar_t*);

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

	// map of local anchors ids to cloud anchors
	std::map<std::wstring, winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor> m_cloudAnchors;
	typedef int32_t WatcherID;
	std::map<WatcherID, LoadByIDAsyncDataPtr> m_loadCloudAnchorByIDMap;
	std::mutex m_loadCloudAnchorByIDMapMutex;

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* GetCloudAnchor(const wchar_t* localAnchorId);

	std::mutex m_mutex;
};

AzureSpatialAnchorsInteropImpl* AzureSpatialAnchorsInteropImpl::Instance = nullptr;

void AzureSpatialAnchorsInterop::Create(WindowsMixedReality::MixedRealityInterop& interop, void(*LogFunctionPointer)(const wchar_t* LogMsg))
{
	AzureSpatialAnchorsInteropImpl::Create(interop, LogFunctionPointer);
}

void AzureSpatialAnchorsInteropImpl::Create(WindowsMixedReality::MixedRealityInterop& interop, void(*LogFunctionPointer)(const wchar_t* LogMsg))
{
	assert(AzureSpatialAnchorsInteropImpl::Instance == nullptr);
	AzureSpatialAnchorsInteropImpl::Instance = new AzureSpatialAnchorsInteropImpl(interop, LogFunctionPointer);
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

AzureSpatialAnchorsInteropImpl::AzureSpatialAnchorsInteropImpl(WindowsMixedReality::MixedRealityInterop& interop, void(*LogFunctionPointer)(const wchar_t* LogMsg))
	: AzureSpatialAnchorsInterop(),
	m_mixedRealityInterop(interop),
	OnLog(LogFunctionPointer)
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

bool AzureSpatialAnchorsInteropImpl::ConfigSession(const wchar_t* accountId, const wchar_t* accountKey, int logVerbosity)
{
	Log(L"ConfigSession");

	if (m_cloudSession == nullptr)
	{
		Log(L"ConfigSession called, but session does not exist!  Ignoring.");
		return false;
	}

	if (accountId == nullptr || *accountId == 0)
	{
		Log(L"ConfigSession called, but accountId is null or empty!  This session should not be useable.");
	}

	if (accountKey == nullptr || *accountKey == 0)
	{
		Log(L"ConfigSession called, but accountId is null or empty!  This session should not be useable.");
	}
	
	// Session configuration takes effect on Start() according to documentation.

	//ConfigSession
	auto configuration = m_cloudSession.Configuration();
	configuration.AccountId(accountId);
	configuration.AccountKey(accountKey);

	SessionLogLevel logLevel = (SessionLogLevel)logVerbosity;
	if ((logLevel < SessionLogLevel::None) || (logLevel > SessionLogLevel::All))
	{
		logLevel = (logLevel < SessionLogLevel::None) ? SessionLogLevel::None : SessionLogLevel::All;
		{ std::wstringstream string; string << L"ConfigSession called with invalid log level " << logVerbosity << ".  Clamping the value to " << (int)logLevel; Log(string); }
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
	auto lock = std::unique_lock<std::mutex>(m_mutex);
	RemoveEventListeners();
	m_cloudAnchors.clear();
	m_sessionStarted = false;
	m_cloudSession = nullptr;
}
bool AzureSpatialAnchorsInteropImpl::HasEnoughDataForSaving()
{
	return m_enoughDataForSaving;
}

bool AzureSpatialAnchorsInteropImpl::CreateCloudAnchor(const wchar_t* localAnchorId)
{
	if (localAnchorId == nullptr)
	{
		Log(L"CreateCloudAnchor failed because localAnchorId is null!");
		return false;
	}

	{ std::wstringstream string; string << L"CreateCloudAnchor from a local anchor " << localAnchorId; Log(string); }

	auto lock = std::unique_lock<std::mutex>(m_mutex);

	std::shared_ptr<class SpatialAnchorHelper> spatialAnchorHelper = GetSpatialAnchorHelper();
	winrt::Windows::Perception::Spatial::SpatialAnchor* localAnchor = spatialAnchorHelper->GetSpatialAnchor(localAnchorId);
	if (localAnchor == nullptr)
	{
		{ std::wstringstream string; string << L"CreateCloudAnchor failed because localAnchorId " << localAnchorId << L" does not exist!  You must create the local anchor first."; Log(string); }
		return false;
	}
	else
	{
		winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor newCloudAnchor = CloudSpatialAnchor();
		newCloudAnchor.LocalAnchor(*localAnchor);
		m_cloudAnchors.insert(std::make_pair(localAnchorId, newCloudAnchor));
		return true;
	}
}

bool AzureSpatialAnchorsInteropImpl::SetCloudAnchorExpiration(const wchar_t* localAnchorId, int minutesFromNow)
{
	if (localAnchorId == nullptr)
	{
		Log(L"SetCloudAnchorExpiration failed because localAnchorId is null!");
		return false;
	}

	{ std::wstringstream string; string << L"SetCloudAnchorExpiration for anchor " << localAnchorId; Log(string); }

	if (minutesFromNow <= 0)
	{
		{ std::wstringstream string; string << L"SetCloudAnchorExpiration failed because minutesFromNow is " << minutesFromNow << L" which is <= 0."; Log(string); }
		return false;
	}

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(localAnchorId);
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SetCloudAnchorExpiration failed because localAnchorId " << localAnchorId << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		return false;
	}
	else
	{
		const winrt::Windows::Foundation::DateTime minutesFromNowDateTime = winrt::Windows::Foundation::DateTime::clock::now() + std::chrono::minutes(minutesFromNow);
		cloudAnchor->Expiration(minutesFromNowDateTime);
		return true;
	}
}

bool AzureSpatialAnchorsInteropImpl::SaveCloudAnchor(SaveAsyncDataPtr Data)
{
	assert(Data);

	{ std::wstringstream string; string << L"SaveCloudAnchor for anchor " << Data->LocalAnchorId; Log(string); }

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(Data->LocalAnchorId.c_str());
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SaveCloudAnchor failed because LocalAnchorId " << Data->LocalAnchorId << L" does not exist!  You must create the cloud anchor first."; Log(string); }
		Data->Result = AsyncResult::FailNoLocalAnchor;
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
		{ std::wstringstream string; string << L"SaveCloudAnchor saving anchor " << Data->LocalAnchorId; Log(string); }

		m_asyncOpInProgress = true;
		co_await m_cloudSession.CreateAnchorAsync(*cloudAnchor);
		m_asyncOpInProgress = false;
		
		Data->CloudAnchorIdentifier = cloudAnchor->Identifier().c_str();
		Data->Result = AsyncResult::Success;
		{ std::wstringstream string; string << L"SaveCloudAnchor saved anchor " << Data->LocalAnchorId << L" with cloud ID [" << Data->CloudAnchorIdentifier << "]"; Log(string); }
	}
	catch (winrt::hresult_error e)
	{
		m_asyncOpInProgress = false;
		Data->Result = AsyncResult::FailSeeErrorString;
		Data->OutError = e.message();
		{ std::wstringstream string; string << L"SaveCloudAnchor failed to save anchor " << Data->LocalAnchorId << L" message: " << e.message().c_str(); Log(string); }
	}

	Data->Complete();
}

bool AzureSpatialAnchorsInteropImpl::DeleteCloudAnchor(DeleteAsyncDataPtr Data)
{
	assert(Data);

	{ std::wstringstream string; string << L"SaveCloudAnchor for anchor " << Data->LocalAnchorId; Log(string); }

	if (Data->LocalAnchorId.empty())
	{
		Data->Result = AsyncResult::FailBadAnchorId;
		Data->Complete();
		return false;
	}

	winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* cloudAnchor = GetCloudAnchor(Data->LocalAnchorId.c_str());
	if (!cloudAnchor)
	{
		{ std::wstringstream string; string << L"SaveCloudAnchor failed because anchorId " << Data->LocalAnchorId << L" does not exist!  You must create the cloud anchor first."; Log(string); }
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
		m_cloudAnchors.erase(Data->LocalAnchorId);

		Data->Result = AsyncResult::Success;
		{ std::wstringstream string; string << L"DeleteAnchor deleted anchor " << Data->LocalAnchorId; Log(string); }
	}
	catch (winrt::hresult_error e)
	{
		m_asyncOpInProgress = false;
		Data->Result = AsyncResult::FailSeeErrorString;
		Data->OutError = e.message();
		{ std::wstringstream string; string << L"SaveCloudAnchor failed to save anchor " << Data->LocalAnchorId << L" message: " << e.message().c_str(); Log(string); }
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
		Data->Result = AsyncResult::FailBadAnchorId;
		Data->Complete();
		return false;
	}


	AnchorLocateCriteria criteria = AnchorLocateCriteria();
	criteria.Identifiers({ Data->CloudAnchorIdentifier.c_str() });

	Data->Result = AsyncResult::Started;

	{
		auto lock = std::unique_lock<std::mutex>(m_loadCloudAnchorByIDMapMutex);

		try
		{
			auto& watcher = m_cloudSession.CreateWatcher(criteria);
			m_loadCloudAnchorByIDMap.insert(std::make_pair(watcher.Identifier(), Data));
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
		switch (args.Status())
		{
		case LocateAnchorStatus::Located:
		{
			auto lock = std::unique_lock<std::mutex>(m_mutex);
			{ std::wstringstream string; string << L"AnchorLocated status Located Id: " << args.Anchor().Identifier().c_str(); Log(string); }

			{
				auto lock = std::unique_lock<std::mutex>(m_loadCloudAnchorByIDMapMutex);
				auto itr = m_loadCloudAnchorByIDMap.find(args.Watcher().Identifier());
				if (itr == m_loadCloudAnchorByIDMap.end())
				{
					{ std::wstringstream string; string << L"AnchorLocated error.  An anchor was located, but we are not currently waiting for one.  This is a bug!"; Log(string); }
					assert(false);
				}
				else
				{
					winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor loadedCloudAnchor = args.Anchor();
					winrt::Windows::Perception::Spatial::SpatialAnchor localAnchor = loadedCloudAnchor.LocalAnchor();

					LoadByIDAsyncDataPtr& Data = itr->second;

					assert(Data->CloudAnchorIdentifier == loadedCloudAnchor.Identifier());

					std::shared_ptr<class SpatialAnchorHelper> spatialAnchorHelper = GetSpatialAnchorHelper();
					spatialAnchorHelper->StoreSpatialAnchor(Data->LocalAnchorId, localAnchor);

					m_cloudAnchors.insert(std::make_pair(Data->LocalAnchorId, loadedCloudAnchor));
					Data->Result = AsyncResult::Success;

				}
			}
		}
		break;
		case LocateAnchorStatus::AlreadyTracked:
		{
			{ std::wstringstream string; string << L"AnchorLocated status AlreadyTracked"; Log(string); }
			{
				auto lock = std::unique_lock<std::mutex>(m_loadCloudAnchorByIDMapMutex);
				auto itr = m_loadCloudAnchorByIDMap.find(args.Watcher().Identifier());
				if (itr != m_loadCloudAnchorByIDMap.end())
				{
					LoadByIDAsyncDataPtr& Data = itr->second;
					Data->Result = AsyncResult::FailAnchorAlreadyTracked;
				}
			}
		}
		break;
		case LocateAnchorStatus::NotLocated:
		{
			{ std::wstringstream string; string << L"AnchorLocated status NotLocated"; Log(string); }
			// This gets called repeatedly for a while until something else happens.
			{
				auto lock = std::unique_lock<std::mutex>(m_loadCloudAnchorByIDMapMutex);
				auto itr = m_loadCloudAnchorByIDMap.find(args.Watcher().Identifier());
				if (itr != m_loadCloudAnchorByIDMap.end())
				{
					LoadByIDAsyncDataPtr& Data = itr->second;
					Data->Result = AsyncResult::NotLocated;
				}
			}
		}
		break;
		case LocateAnchorStatus::NotLocatedAnchorDoesNotExist:
		{
			{ std::wstringstream string; string << L"AnchorLocated status NotLocatedAnchorDoesNotExist"; Log(string); }
			{
				auto lock = std::unique_lock<std::mutex>(m_loadCloudAnchorByIDMapMutex);
				auto itr = m_loadCloudAnchorByIDMap.find(args.Watcher().Identifier());
				if (itr != m_loadCloudAnchorByIDMap.end())
				{
					LoadByIDAsyncDataPtr& Data = itr->second;
					Data->Result = AsyncResult::FailAnchorDoesNotExist;
				}
			}
		}
		break;
		}
	});

	m_locateAnchorsCompletedToken = m_cloudSession.LocateAnchorsCompleted(winrt::auto_revoke, [this](auto&&, auto&& args)
	{
		{ std::wstringstream string; string << L"LocateAnchorsCompleted watcher " << args.Watcher().Identifier() << " has completed."; Log(string); }
		{
			auto lock = std::unique_lock<std::mutex>(m_loadCloudAnchorByIDMapMutex);
			auto itr = m_loadCloudAnchorByIDMap.find(args.Watcher().Identifier());
			if (itr != m_loadCloudAnchorByIDMap.end())
			{
				LoadByIDAsyncDataPtr& Data = itr->second;
				if (args.Cancelled())
				{
					Data->Result = AsyncResult::Canceled;
				}
				Data->Complete();
				m_loadCloudAnchorByIDMap.erase(itr);
			}
		}
	});

	m_sessionUpdatedToken = m_cloudSession.SessionUpdated(winrt::auto_revoke, [this](auto&&, auto&& args)
	{
		auto status = args.Status();
		m_enoughDataForSaving = status.RecommendedForCreateProgress() >= 1.0f;
		//m_statusText = StatusToString(status);
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

winrt::Microsoft::Azure::SpatialAnchors::CloudSpatialAnchor* AzureSpatialAnchorsInteropImpl::GetCloudAnchor(const wchar_t* localAnchorId)
{
	auto& iterator = m_cloudAnchors.find(localAnchorId);
	if (iterator == m_cloudAnchors.end())
	{
		return nullptr;
	}
	return &(iterator->second);
}
