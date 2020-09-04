// Copyright Epic Games, Inc. All Rights Reserved.

#include "AppEventHandler.h"
#include "AppFramework.h"
#include "Engine/Engine.h"
#include "MagicLeapHMD.h"
#include "MagicLeapPrivilegeUtils.h"

namespace MagicLeap
{
	IAppEventHandler::IAppEventHandler(const TArray<EMagicLeapPrivilege>& InRequiredPrivilegeIDs)
	: OnAppStartHandler(nullptr)
	, OnAppShutDownHandler(nullptr)
	, OnAppTickHandler(nullptr)
	, OnAppPauseHandler(nullptr)
	, OnAppResumeHandler(nullptr)
	, bAllPrivilegesInSync(false)
	, bWasSystemEnabledOnPause(false)
	{
		RequiredPrivileges.Reserve(InRequiredPrivilegeIDs.Num());
		for (EMagicLeapPrivilege RequiredPrivilegeID : InRequiredPrivilegeIDs)
		{
			RequiredPrivileges.Add(RequiredPrivilegeID, FRequiredPrivilege(RequiredPrivilegeID));
		}

		FAppFramework::AddEventHandler(this);
	}

	IAppEventHandler::IAppEventHandler()
	: bAllPrivilegesInSync(true)
	, bWasSystemEnabledOnPause(false)
	{
		FAppFramework::AddEventHandler(this);
	}

	IAppEventHandler::~IAppEventHandler()
	{
		FAppFramework::RemoveEventHandler(this);
	}

	EPrivilegeState IAppEventHandler::GetPrivilegeStatus(EMagicLeapPrivilege PrivilegeID, bool bBlocking/* = true*/)
	{
#if WITH_MLSDK
		FRequiredPrivilege RequiredPrivilege(PrivilegeID);
		{
			FScopeLock Lock(&CriticalSection);
			if (!RequiredPrivileges.Contains(PrivilegeID))
			{
				UE_LOG(LogMagicLeap, Error, TEXT("Privilege %s was denied as it wasn't listed in the required privileges map."), *MLPrivilegeToString(PrivilegeID));
				return EPrivilegeState::Denied;
			}
			RequiredPrivilege = RequiredPrivileges[PrivilegeID];
		}

		if (RequiredPrivilege.State == EPrivilegeState::NotYetRequested || RequiredPrivilege.State == EPrivilegeState::Pending)
		{
			// Attempt a quick check first. Note that MLPrivilegesCheckPrivilege will return denied if the privilege has never been requested.
			MLResult Result = MLPrivilegesCheckPrivilege(MagicLeap::UnrealToMLPrivilege(PrivilegeID));
			if (Result == MLPrivilegesResult_Granted)
			{
				RequiredPrivilege.State = EPrivilegeState::Granted;
				UE_LOG(LogMagicLeap, Log, TEXT("Privilege '%s' was granted."), *MagicLeap::MLPrivilegeToString(RequiredPrivilege.PrivilegeID));
			}
			else if (bBlocking)
			{
				Result = MLPrivilegesRequestPrivilege(MagicLeap::UnrealToMLPrivilege(PrivilegeID));
				switch (Result)
				{
				case MLPrivilegesResult_Granted:
				{
					RequiredPrivilege.State = EPrivilegeState::Granted;
					UE_LOG(LogMagicLeap, Log, TEXT("Privilege '%s' was granted."), *MagicLeap::MLPrivilegeToString(RequiredPrivilege.PrivilegeID));
				}
				break;
				case MLPrivilegesResult_Denied:
				{
					RequiredPrivilege.State = EPrivilegeState::Denied;
					UE_LOG(LogMagicLeap, Log, TEXT("Privilege '%s' was denied."), *MagicLeap::MLPrivilegeToString(RequiredPrivilege.PrivilegeID));
				}
				break;
				default:
				{
					UE_LOG(LogMagicLeap, Error, TEXT("MLPrivilegesRequestPrivilege() failed with error %s"), UTF8_TO_TCHAR(MLPrivilegesGetResultString(Result)));
					RequiredPrivilege.State = EPrivilegeState::Error;
				}
				}
			}
			else
			{
				Result = MLPrivilegesRequestPrivilegeAsync(MagicLeap::UnrealToMLPrivilege(PrivilegeID), reinterpret_cast<MLPrivilegesAsyncRequest**>(&RequiredPrivilege.PrivilegeRequest));
				if (Result != MLResult_Ok)
				{
					UE_LOG(LogMagicLeap, Error, TEXT("MLPrivilegesRequestPrivilegeAsync() failed with error %s"), UTF8_TO_TCHAR(MLPrivilegesGetResultString(Result)));
					RequiredPrivilege.State = EPrivilegeState::Error;
				}
				else
				{
					RequiredPrivilege.State = EPrivilegeState::Pending;
				}
			}
		}

		{
			FScopeLock Lock(&CriticalSection);
			RequiredPrivileges[PrivilegeID] = RequiredPrivilege;
		}

		return RequiredPrivilege.State;
#else
		return EPrivilegeState::NotYetRequested;
#endif // WITH_MLSDK
	}

	FString IAppEventHandler::PrivilegeToString(EMagicLeapPrivilege PrivilegeID)
	{
#if WITH_MLSDK
		return MLPrivilegeToString(PrivilegeID);
#else
		return TEXT("MLPrivilegeID_Invalid");
#endif // WITH_MLSDK
	}

	const TCHAR* IAppEventHandler::PrivilegeStateToString(EPrivilegeState PrivilegeState)
	{
		const TCHAR* PrivilegeStateString = nullptr;

		switch (PrivilegeState)
		{
		case EPrivilegeState::NotYetRequested:	PrivilegeStateString = TEXT("NotYetRequested"); break;
		case EPrivilegeState::Pending:			PrivilegeStateString = TEXT("Pending"); break;
		case EPrivilegeState::Granted:			PrivilegeStateString = TEXT("Granted"); break;
		case EPrivilegeState::Denied:			PrivilegeStateString = TEXT("Denied"); break;
		case EPrivilegeState::Error:			PrivilegeStateString = TEXT("Error"); break;
		}

		return PrivilegeStateString;
	}

	bool IAppEventHandler::AddPrivilegeEventHandler(EMagicLeapPrivilege PrivilegeID, FRequiredPrivilege::FPrivilegeEventHandler&& InOnPrivilegeEvent)
	{
		FScopeLock Lock(&CriticalSection);
		FRequiredPrivilege* RequiredPrivilege = RequiredPrivileges.Find(PrivilegeID);
		if (RequiredPrivilege)
		{
			RequiredPrivilege->EventHandler = MoveTemp(InOnPrivilegeEvent);
			return true;
		}

#if WITH_MLSDK
		UE_LOG(LogMagicLeap, Error, TEXT("Failed to resolve Privilege '%s' in required privileges list!"), *MagicLeap::MLPrivilegeToString(PrivilegeID));
#endif // WITH_MLSDK
		return false;
	}

	void IAppEventHandler::OnAppStart()
	{
		if (OnAppStartHandler)
		{
			OnAppStartHandler();
		}
	}

	void IAppEventHandler::OnAppShutDown()
	{
		if (OnAppShutDownHandler)
		{
			OnAppShutDownHandler();
		}
	}

	void IAppEventHandler::OnAppTick()
	{
#if WITH_MLSDK
		FScopeLock Lock(&CriticalSection);
		if (bAllPrivilegesInSync)
		{
			return;
		}

		bAllPrivilegesInSync = true;

		for (auto& ItRequiredPrivilege : RequiredPrivileges)
		{
			FRequiredPrivilege& RequiredPrivilege = ItRequiredPrivilege.Value;
			if (RequiredPrivilege.State == EPrivilegeState::NotYetRequested)
			{
				bAllPrivilegesInSync = false;
				continue;
			}

			if (RequiredPrivilege.State == EPrivilegeState::Granted || RequiredPrivilege.State == EPrivilegeState::Denied)
			{
				continue;
			}

			if (RequiredPrivilege.State == EPrivilegeState::Pending)
			{
				MLResult Result = MLPrivilegesRequestPrivilegeTryGet(static_cast<MLPrivilegesAsyncRequest*>(RequiredPrivilege.PrivilegeRequest));
				switch (Result)
				{
				case MLPrivilegesResult_Granted:
				{
					RequiredPrivilege.State = EPrivilegeState::Granted;
					UE_LOG(LogMagicLeap, Log, TEXT("Privilege '%s' was granted."), *MagicLeap::MLPrivilegeToString(RequiredPrivilege.PrivilegeID));
				}
				break;
				case MLPrivilegesResult_Denied:
				{
					RequiredPrivilege.State = EPrivilegeState::Denied;
					UE_LOG(LogMagicLeap, Log, TEXT("Privilege '%s' was denied."), *MagicLeap::MLPrivilegeToString(RequiredPrivilege.PrivilegeID));
				}
				break;
				case MLResult_Pending:
				{
					bAllPrivilegesInSync = false;
				}
				break;
				default:
				{
					bAllPrivilegesInSync = false;
					UE_LOG(LogMagicLeap, Error, TEXT("MLPrivilegesRequestPrivilegeTryGet() failed with error %s."), UTF8_TO_TCHAR(MLPrivilegesGetResultString(Result)));
				}
				}

				if (Result != MLResult_Pending && RequiredPrivilege.EventHandler)
				{
					RequiredPrivilege.EventHandler(RequiredPrivilege);
				}
			}
		}
#endif // WITH_MLSDK

		if (OnAppTickHandler)
		{
			OnAppTickHandler();
		}
	}

	void IAppEventHandler::OnAppPause()
	{
		if (OnAppPauseHandler)
		{
			OnAppPauseHandler();
		}
	}

	void IAppEventHandler::OnAppResume()
	{
#if WITH_MLSDK
		FScopeLock Lock(&CriticalSection);
		bAllPrivilegesInSync = false;
		for (auto& ItRequiredPrivilege : RequiredPrivileges)
		{
			ItRequiredPrivilege.Value.State = EPrivilegeState::NotYetRequested;
		}
#endif // WITH_MLSDK

		if (OnAppResumeHandler)
		{
			OnAppResumeHandler();
		}
	}
}
