// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CookTypes.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "HAL/CriticalSection.h"
#include "HAL/Platform.h"
#include "Templates/UniquePtr.h"
#include "UObject/NameTypes.h"

class ITargetPlatform;
class UCookOnTheFlyServer;

namespace UE
{
namespace Cook
{

	/*
	 * Struct to hold data about each platform we have encountered in the cooker.
	 * Fields in this struct persist across multiple CookByTheBook sessions.
	 * Fields on this struct are used on multiple threads; see variable comments for thread synchronization rules.
	 */
	struct FPlatformData
	{
		/* Name of the Platform, a cache of FName(ITargetPlatform->PlatformName()) */
		FName PlatformName;

		/* Pointer to the platform-specific RegistryGenerator for this platform.  If already constructed we can take a faster refresh path on future sessions.
		 * Read/Write on TickCookOnTheSide thread only
		 */
		TUniquePtr<FAssetRegistryGenerator> RegistryGenerator;

		/*
		 * Whether InitializeSandbox has been called for this platform.  If we have already initialized the sandbox we can take a faster refresh path on future sessions.
		 * Threadsafe due to write-once.  Written only once after construction on the game thread.
		 */
		bool bIsSandboxInitialized = false;

		/*
		 * The last FPlatformTime::GetSeconds() at which this platform was requested in a CookOnTheFly request.
		 * If equal to 0, the platform was not requested in a CookOnTheFly since the last clear.
		 * Written only when SessionLock critical section is held.
		 */
		double LastReferenceTime = 0.0;

		/*
		 * The count of how many active CookOnTheFly requests are currently using the Platform.
		 * Read/Write only when the SessionLock critical section is held.
		 */
		uint32 ReferenceCount = 0;
	};

	/* Information about the platforms (a) known and (b) active for the current cook session in the UCookOnTheFlyServer. */
	struct FPlatformManager
	{
	public:
		FPlatformManager(FCriticalSection& InSessionLock);

		/** Return the CriticalSection this PlatformManager is using to synchronize multithreaded access to Session Platforms */
		FCriticalSection& GetSessionLock();

		/**
		 * Returns the set of TargetPlatforms that is active for the CurrentCookByTheBook session or CookOnTheFly request.
		 * This function can be called and its return value referenced only from the TickCookOnTheSide thread or when SessionLock is held.
		 */
		const TArray<const ITargetPlatform*>& GetSessionPlatforms() const;

		/**
		 * Return whether the platforms have been selected for the current session (may be empty if current session is a null session).
		 * This function can be called from any thread, but is guaranteed thread-accurate only from the TickCookOnTheSide thread or when SessionLock is held.
		 */
		bool HasSelectedSessionPlatforms() const;

		/**
		 * Return whether the given platform is already in the list of platforms for the current session.
		 * This function can be called only from the TickCookOnTheSide thread or when SessionLock is held.
		 */
		bool HasSessionPlatform(const ITargetPlatform* TargetPlatform) const;

		/**
		 * Specify the set of TargetPlatforms to use for the currently-initializing CookByTheBook session or CookOnTheFly request.
		 * This function can be called only from the TickCookOnTheSide thread.
		 */
		void SelectSessionPlatforms(const TArrayView<const ITargetPlatform* const>& TargetPlatforms);

		/**
		 * Mark that the list of TargetPlatforms for the session has no longer been set; it will be an error to try to read them until SelectSessionPlatforms is called.
		 * This function can be called only from the TickCookOnTheSide thread.
		 */
		void ClearSessionPlatforms();

		/**
		 * Add @param TargetPlatform to the session platforms if not already present.
		 * This function can be called only from the TickCookOnTheSide thread.
		 */
		void AddSessionPlatform(const ITargetPlatform* TargetPlatform);

		/**
		 * Get The PlatformData for the given Platform.
		 * Guaranteed to return non-null for any Platform in the current list of SessionPlatforms.
		 * Can be called from any thread.
		 */
		FPlatformData* GetPlatformData(const ITargetPlatform* Platform);

		/**
		 * Create if not already created the necessary platform-specific data for the given platform.
		 * This function can be called with new platforms only before multithreading begins (e.g. in StartNetworkFileServer or StartCookByTheBook).
		 */
		FPlatformData& CreatePlatformData(const ITargetPlatform* Platform);

		/**
		 * Return whether platform-specific setup steps have been executed for the given platform in the current UCookOnTheFlyServer.
		 * This function can be called from any thread, but is guaranteed thread-accurate only from the TickCookOnTheSide thread.
		 */
		bool IsPlatformInitialized(const ITargetPlatform* Platform) const;

		/** If and only if bFrozen is set to true, it is invalid to call CreatePlatformData with a Platform that does not already exist. */
		void SetPlatformDataFrozen(bool bFrozen);

		/**
		 * Platforms requested in CookOnTheFly requests are added to the list of SessionPlatforms, and some packages (e.g. unsolicited packages) are cooked against all session packages.
		 * To have good performance in the case where multiple targetplatforms are being requested over time from the same CookOnTheFly server, we prune platforms from the list of
		 * active session platforms if they haven't been requested in a while.
		 * This function can be called only from the TickCookOnTheSide thread.
		 */
		void PruneUnreferencedSessionPlatforms(UCookOnTheFlyServer& CookOnTheFlyServer);

		/**
		 * Increment the counter indicating the current platform is requested in an active CookOnTheFly request.  Add it to the SessionPlatforms if not already present.
		 * This function can be called only when the SessionLock is held.
		 */
		void AddRefCookOnTheFlyPlatform(const ITargetPlatform* TargetPlatform, UCookOnTheFlyServer& CookOnTheFlyServer);

		/**
		 * Decrement the counter indicating the current platform is being used in a CookOnTheFly request.
		 * This function can be called only when the SessionLock is held.
		 */
		void ReleaseCookOnTheFlyPlatform(const ITargetPlatform* TargetPlatform);

	private:
		/** A collection of initialization flags and other data we maintain for each platform we have encountered in any session. */
		TFastPointerMap<const ITargetPlatform*, FPlatformData> PlatformDatas;

		/**
		 * A collection of Platforms that are active for the current CookByTheBook session or CookOnTheFly request.  Used so we can refer to "all platforms" without having to store a list on every FileRequest.
		 * Writing to the list of active session platforms requires a CriticalSection, because it is read (under critical section) on NetworkFileServer threads.
		 */
		TArray<const ITargetPlatform*> SessionPlatforms;

		/** A reference to the critical section used to guard SessionPlatforms. */
		FCriticalSection& SessionLock;

		/** If PlatformData is frozen, it is invalid to add new PlatformDatas. */
		bool bPlatformDataFrozen = false;

		/** It is invalid to attempt to cook if session platforms have not been selected. */
		bool bHasSelectedSessionPlatforms = false;
	};

}
}
