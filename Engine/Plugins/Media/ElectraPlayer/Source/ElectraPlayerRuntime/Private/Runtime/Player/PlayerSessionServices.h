// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "ErrorDetail.h"
#include "InfoLog.h"
#include "StreamTypes.h"


namespace Electra
{
	class ISynchronizedUTCTime;
	class IElectraHttpManager;
	class IAdaptiveStreamSelector;
	class IPlayerStreamFilter;
	struct FAccessUnitBufferInfo;
	class IAdaptiveStreamingPlayerResourceProvider;


	class IPlayerMessage
	{
	public:
		virtual ~IPlayerMessage() = default;
		virtual const FString& GetType() const = 0;
	};


	class IPlayerSessionServices
	{
	public:
		virtual ~IPlayerSessionServices() = default;

		/**
		 * Post an error. Playback will be halted.
		 *
		 * @param Error  Error that occurred.
		 */
		virtual void PostError(const FErrorDetail& Error) = 0;


		/**
		 * Posts a message to the log.
		 *
		 * @param FromFacility
		 * @param LogLevel
		 * @param Message
		 */
		virtual void PostLog(Facility::EFacility FromFacility, IInfoLog::ELevel LogLevel, const FString& Message) = 0;


		/**
		 * Sends a message to the player worker thread.
		 *
		 * @param PlayerMessage
		 */
		virtual void SendMessageToPlayer(TSharedPtrTS<IPlayerMessage> PlayerMessage) = 0;

		/**
		 * Returns the external GUID identifying this player and its associated external texture.
		 */
		virtual void GetExternalGuid(FGuid& OutExternalGuid) = 0;

		/**
		 * Returns the synchronized UTC clock instance associated with this player instance.
		 *
		 * @return Pointer to the synchronized clock.
		 */
		virtual ISynchronizedUTCTime* GetSynchronizedUTCTime() = 0;

		/**
		 * Returns the static resource provider, if any.
		 *
		 * @return Pointer to the static resource provider.
		 */
		virtual TSharedPtr<IAdaptiveStreamingPlayerResourceProvider, ESPMode::ThreadSafe> GetStaticResourceProvider() = 0;

		/**
		 * Returns the HTTP manager instance serving all HTTP requests of this player instance.
		 *
		 * @return Pointer to the HTTP manager.
		 */
		virtual IElectraHttpManager* GetHTTPManager() = 0;

		/**
		 * Returns the ABR stream selector instance.
		 *
		 * @return Pointer to the ABR stream selector.
		 */
		virtual TSharedPtrTS<IAdaptiveStreamSelector> GetStreamSelector() = 0;

		/**
		 * Returns the current stream access unit buffer stats.
		 */
		virtual void GetStreamBufferStats(FAccessUnitBufferInfo& OutBufferStats, EStreamType ForStream) = 0;

		/**
		 * Returns the stream filter interface used by playlist readers to determine whether or not a stream
		 * can be used on the platform.
		 */
		virtual IPlayerStreamFilter* GetStreamFilter() = 0;
	};


} // namespace Electra



