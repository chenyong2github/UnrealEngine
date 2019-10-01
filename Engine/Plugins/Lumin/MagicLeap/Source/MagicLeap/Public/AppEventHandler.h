// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MagicLeapPrivilegeTypes.h"
#include "Lumin/CAPIShims/LuminAPI.h"

namespace MagicLeap
{
	enum EPrivilegeState
	{
		NotYetRequested,
		Pending,
		Granted,
		Denied,
		Error
	};

	struct FRequiredPrivilege
	{
		FRequiredPrivilege(EMagicLeapPrivilege InPrivilegeID)
		: PrivilegeID(InPrivilegeID)
		, PrivilegeRequest(nullptr)
		, State(NotYetRequested)
		{}

		EMagicLeapPrivilege PrivilegeID;
		void* PrivilegeRequest;
		EPrivilegeState State;
	};

	/**
	* Provides an interface between the AppFramework and any system that needs to be
	* notified of application events (such as pause/resume).
	*/
	class MAGICLEAP_API IAppEventHandler
	{
	public:
		typedef TFunction<void()> FEventHandler;
		typedef TFunction<void(const FRequiredPrivilege& RequiredPrivilege)> FPrivilegeEventHandler;

		/** 
			Adds the IAppEventHandler instance to the application's list of IAppEventHandler instances.
			Populates a RequiredPrivileges list based on the privilge ids passed via InRequiredPrivileges;
			@param InRequiredPrivileges The list of privilge ids required by the calling system.
		*/
		IAppEventHandler(const TArray<EMagicLeapPrivilege>& InRequiredPrivileges);

		IAppEventHandler();

		/** Removes the IAppEventHandler instance from the application's list of IAppEventHandler instances.*/
		virtual ~IAppEventHandler();

		/**
			Can be overridden by inheriting class that needs to destroy certain api interfaces before the perception stack is
			closed down.
		*/
		virtual void OnAppShutDown();

		/**
			Use to check status of privilege requests.
		*/
		virtual void OnAppTick();

		/**
			Can be overridden by inheriting class in order to pause its system.
		*/
		virtual void OnAppPause();

		/**
			Can be overridden by inheriting class in order to resume its system.
		*/
		virtual void OnAppResume();

		/**
			Returns the status of the specified privilege.
			@param PrivilegeID The privilege id to be queried.
			@param bBlocking Flags whether or not to use the blocking query internally.
		*/
		EPrivilegeState GetPrivilegeStatus(EMagicLeapPrivilege PrivilegeID, bool bBlocking = true);

		/**
			Converts the EMagicLeapPrivilege enum value to it's corresponding string representation.
			@param PrivilegeID The EMagicLeapPrivilege enum value to be converted to a string.
			@return An FString representing the string value of the supplied privilege enum value.
		*/
		FString PrivilegeToString(EMagicLeapPrivilege PrivilegeID);

		/**
			Converts the EPrivilegeState enum value to it's corresponding string representation.
			@param PrivilegeState The EPrivilegeState enum value to be converted to a string.
			@return A TCHAR pointer representing the string value of the supplied privilege state enum value.
		*/
		const TCHAR* PrivilegeStateToString(EPrivilegeState PrivilegeState);

		/**
			Pushes this object onto a worker thread so that it's blocking destructor can be called without locking up the update thread.
			@note This should only be called by objects that have a blocking destructor and are no longer referenced.
		*/
		bool AsyncDestroy();

		/**
			Triggered when a privilege request changes state.
		*/
		void SetPrivilegeEventHandler(FPrivilegeEventHandler&& InOnPrivilegeEvent)
		{
			OnPrivilegeEvent = MoveTemp(InOnPrivilegeEvent);
		}

		/**
			Use this as an alternative to overriding the OnAppShutDown function.  This allows you to use IAppEventHandler
			as and aggregate class rather than an ancestor. 
		*/
		void SetOnAppShutDownHandler(FEventHandler&& InOnAppShutDownHandler)
		{
			OnAppShutDownHandler = MoveTemp(InOnAppShutDownHandler);
		}

		/**
			Use this as an alternative to overriding the OnAppTick function.  This allows you to use IAppEventHandler
			as and aggregate class rather than an ancestor.
		*/
		void SetOnAppTickHandler(FEventHandler&& InOnAppTickHandler)
		{
			OnAppTickHandler = MoveTemp(InOnAppTickHandler);
		}

		/**
			Use this as an alternative to overriding the OnAppPause function.  This allows you to use IAppEventHandler
			as and aggregate class rather than an ancestor.
		*/
		void SetOnAppPauseHandler(FEventHandler&& InOnAppPauseHandler)
		{
			OnAppPauseHandler = MoveTemp(InOnAppPauseHandler);
		}

		/**
			Use this as an alternative to overriding the OnAppResume function.  This allows you to use IAppEventHandler
			as and aggregate class rather than an ancestor.
		*/
		void SetOnAppResumeHandler(FEventHandler&& InOnAppResumeHandler)
		{
			OnAppResumeHandler = MoveTemp(InOnAppResumeHandler);
		}

		void SetWasSystemEnabledOnPause(bool bInWasSystemEnabledOnPause)
		{
			bWasSystemEnabledOnPause = bInWasSystemEnabledOnPause;
		}

		bool WasSystemEnabledOnPause() const
		{
			return bWasSystemEnabledOnPause;
		}

	protected:
		TMap<EMagicLeapPrivilege, FRequiredPrivilege> RequiredPrivileges;
		FPrivilegeEventHandler OnPrivilegeEvent;
		FEventHandler OnAppShutDownHandler;
		FEventHandler OnAppTickHandler;
		FEventHandler OnAppPauseHandler;
		FEventHandler OnAppResumeHandler;
		bool bAllPrivilegesInSync;
		bool bWasSystemEnabledOnPause;
		FCriticalSection CriticalSection;
	};
} // MagicLeap
