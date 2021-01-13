// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "StreamTypes.h"
#include "Player/Manifest.h"
#include "Player/PlayerSessionServices.h"


namespace Electra
{
	class IAccessUnitMemoryProvider;
	struct FAccessUnit;
	struct FStreamSourceInfo;


	/**
	 * Base class for reading fragments from a stream.
	 * Specialized versions for particular container formats shall derive from this class.
	**/
	class IStreamReader : private TMediaNoncopyable<IStreamReader>
	{
	public:
		virtual ~IStreamReader() = default;

		class StreamReaderEventListener
		{
		public:
			virtual ~StreamReaderEventListener() = default;
			virtual bool OnFragmentAccessUnitReceived(FAccessUnit* pAccessUnit) = 0;
			virtual void OnFragmentReachedEOS(EStreamType InStreamType, TSharedPtr<const FStreamSourceInfo, ESPMode::ThreadSafe> InStreamSourceInfo) = 0;
			virtual void OnFragmentOpen(TSharedPtrTS<IStreamSegment> pRequest) = 0;
			virtual void OnFragmentClose(TSharedPtrTS<IStreamSegment> pRequest) = 0;
		};

		struct ReaderConfiguration
		{
			ReaderConfiguration()
			{
				ThreadParam.Priority = TPri_Normal;
				ThreadParam.StackSize = 65536;
				ThreadParam.CoreAffinity = -1;

				// No limit on download duration
				MaxDLDurationScale = 0.0;
			}

			FMediaRunnable::Param							ThreadParam;
			double											MaxDLDurationScale;
		};

		struct CreateParam
		{
			CreateParam()
			{
				MemoryProvider = nullptr;
				EventListener = nullptr;
				PlayerSessionService = nullptr;
			}

			ReaderConfiguration		   ReaderConfig;
			IAccessUnitMemoryProvider* MemoryProvider;
			StreamReaderEventListener* EventListener;

			FParamDict					Options;
			IPlayerSessionServices*		PlayerSessionService;
		};

		//! Creates the instance internally based on the creation parameters.
		virtual UEMediaError Create(IPlayerSessionServices* PlayerSessionService, const CreateParam& createParam) = 0;

		//! Cancels all pending requests and stops processing. The instance can no longer be used after that!
		virtual void Close() = 0;

		enum class EAddResult
		{
			Added,
			TryAgainLater,
			Error
		};
		//! Adds a request to read from a stream
		virtual EAddResult AddRequest(uint32 CurrentPlaybackSequenceID, TSharedPtrTS<IStreamSegment> Request) = 0;

		//! Cancels all pending requests.
		virtual void CancelRequests() = 0;

		//! Pauses all pending requests.
		virtual void PauseDownload() = 0;
		//! Resumes all pending requests.
		virtual void ResumeDownload() = 0;
	};

} // namespace Electra


