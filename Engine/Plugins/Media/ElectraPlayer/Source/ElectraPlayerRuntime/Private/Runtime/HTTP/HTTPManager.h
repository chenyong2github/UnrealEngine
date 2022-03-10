// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"

#include "StreamDataBuffer.h"
#include "OptionalValue.h"

#include "ParameterDictionary.h"
#include "ErrorDetail.h"
#include "ElectraHTTPStream.h"
#include "Utilities/HttpRangeHeader.h"


DECLARE_LOG_CATEGORY_EXTERN(LogElectraHTTPManager, Log, All);

namespace Electra
{
	class IHTTPResponseCache;

	namespace HTTP
	{

		struct FStatusInfo
		{
			void Empty()
			{
				ErrorDetail.Clear();
				OccurredAtUTC.SetToInvalid();
				HTTPStatus = 0;
				ErrorCode = 0;
				ConnectionTimeoutAfterMilliseconds = 0;
				NoDataTimeoutAfterMilliseconds = 0;
				bRedirectionError = false;
				bReadError = false;
			}
			FErrorDetail	ErrorDetail;
			FTimeValue		OccurredAtUTC;
			int32			HTTPStatus = 0;
			int32			ErrorCode = 0;
			int32			ConnectionTimeoutAfterMilliseconds = 0;
			int32			NoDataTimeoutAfterMilliseconds = 0;
			bool			bRedirectionError = false;
			bool			bReadError = false;
		};

		struct FRetryInfo
		{
			FRetryInfo()
			{
				AttemptNumber = 0;
				MaxAttempts = 0;
			}
			int32						AttemptNumber;
			int32						MaxAttempts;
			TArray<FStatusInfo>			PreviousFailureStates;
		};


		struct FHTTPHeader : public FElectraHTTPStreamHeader
		{
			FHTTPHeader() = default;
			FHTTPHeader(const FString& InHeader, const FString& InValue)
			{
				Header = InHeader;
				Value = InValue;
			}
			void SetFromString(const FString& InString)
			{
				int32 ColonPos;
				if (InString.FindChar(TCHAR(':'), ColonPos))
				{
					Header = InString.Left(ColonPos);
					Value = InString.Mid(ColonPos + 2);
				}
			}
			static void ParseFromString(FHTTPHeader& OutHeader, const FString& InString)
			{
				OutHeader.SetFromString(InString);
			}
		};

		struct FConnectionInfo
		{
			struct FThroughput
			{
				FThroughput()
				{
					Clear();
				}
				void Clear()
				{
					ActiveReadTime.SetToZero();
					LastCheckTime.SetToInvalid();
					LastReadCount = 0;
					AccumulatedBytesPerSec = 0;
					AccumulatedBytesPerSecWithoutFirst = 0;
					NumAccumulatedBytesPerSec = 0;
					bIsFirst = true;
				}
				int64 GetThroughput() const
				{
					if (NumAccumulatedBytesPerSec > 1)
					{
						return AccumulatedBytesPerSecWithoutFirst / (NumAccumulatedBytesPerSec - 1);
					}
					else if (NumAccumulatedBytesPerSec)
					{
						return AccumulatedBytesPerSec / NumAccumulatedBytesPerSec;
					}
					else
					{
						// Did not get to collect a throughput sample.
						return 0;
					}
				}
				FTimeValue				ActiveReadTime;
				FTimeValue				LastCheckTime;
				int64					LastReadCount;
				int64					AccumulatedBytesPerSec;
				int64					AccumulatedBytesPerSecWithoutFirst;
				int32					NumAccumulatedBytesPerSec;
				bool					bIsFirst;
			};

			TArray<FHTTPHeader>					ResponseHeaders;					//!< Response headers
			FString								EffectiveURL;						//!< Effective URL after all redirections
			FString								ContentType;						//!< Content-Type header
			FString  							ContentRangeHeader;
			FString  							ContentLengthHeader;
			FTimeValue							RequestStartTime;					//!< Time at which the request was started
			FTimeValue							RequestEndTime;						//!< Time at which the request ended
			double								TimeForDNSResolve;					//!< Time it took to resolve DNS
			double								TimeUntilConnected;					//!< Time it took until connected to the server
			double								TimeUntilFirstByte;					//!< Time it took until received the first response data byte
			int64								ContentLength;						//!< Content length, if known. Chunked transfers may have no length (set to -1). Compressed file will store compressed size here!
			int64								BytesReadSoFar;						//!< Number of bytes read so far.
			int32								NumberOfRedirections;				//!< Number of redirections performed
			int32								HTTPVersionReceived;				//!< Version of HTTP header received (10 = 1.0, 11=1.1, 20=2.0)
			bool								bIsConnected;						//!< true once connected to the server
			bool								bHaveResponseHeaders;				//!< true when response headers have been received
			bool								bIsChunked;							//!< true if response is received in chunks
			bool								bWasAborted;						//!< true if transfer was aborted.
			bool								bHasFinished;						//!< true once the connection is closed regardless of state.
			bool								bResponseNotRanged;					//!< true if the response is not a range as was requested.
			bool								bIsCachedResponse;					//!< true if the response came from the cache.
			FStatusInfo							StatusInfo;
			TSharedPtrTS<FRetryInfo>			RetryInfo;

			FThroughput							Throughput;

			FConnectionInfo()
			{
				TimeForDNSResolve = 0.0;
				TimeUntilConnected = 0.0;
				TimeUntilFirstByte = 0.0;
				NumberOfRedirections = 0;
				HTTPVersionReceived = 0;
				ContentLength = -1;
				BytesReadSoFar = 0;
				bIsConnected = false;
				bHaveResponseHeaders = false;
				bIsChunked = false;
				bWasAborted = false;
				bHasFinished = false;
				bResponseNotRanged = false;
				bIsCachedResponse = false;
			}

			FConnectionInfo& CopyFrom(const FConnectionInfo& rhs)
			{
				ResponseHeaders = rhs.ResponseHeaders;
				EffectiveURL = rhs.EffectiveURL;
				ContentType = rhs.ContentType;
				ContentRangeHeader = rhs.ContentRangeHeader;
				ContentLengthHeader = rhs.ContentLengthHeader;
				RequestStartTime = rhs.RequestStartTime;
				RequestEndTime = rhs.RequestEndTime;
				TimeForDNSResolve = rhs.TimeForDNSResolve;
				TimeUntilConnected = rhs.TimeUntilConnected;
				TimeUntilFirstByte = rhs.TimeUntilFirstByte;
				ContentLength = rhs.ContentLength;
				BytesReadSoFar = rhs.BytesReadSoFar;
				NumberOfRedirections = rhs.NumberOfRedirections;
				HTTPVersionReceived = rhs.HTTPVersionReceived;
				bIsConnected = rhs.bIsConnected;
				bHaveResponseHeaders = rhs.bHaveResponseHeaders;
				bIsChunked = rhs.bIsChunked;
				bWasAborted = rhs.bWasAborted;
				bHasFinished = rhs.bHasFinished;
				bResponseNotRanged = rhs.bResponseNotRanged;
				bIsCachedResponse = rhs.bIsCachedResponse;
				StatusInfo = rhs.StatusInfo;
				Throughput = rhs.Throughput;
				if (rhs.RetryInfo.IsValid())
				{
					RetryInfo = MakeSharedTS<FRetryInfo>(*rhs.RetryInfo);
				}
				return *this;
			}
		};

	} // namespace HTTP


	class IElectraHttpManager
	{
	public:
		static TSharedPtrTS<IElectraHttpManager> Create();

		virtual ~IElectraHttpManager() = default;

		struct FRequest;

		typedef Electra::FastDelegate1<const FRequest*, int32> FProgressDelegate;
		typedef Electra::FastDelegate1<const FRequest*> FCompletionDelegate;
		struct FProgressListener
		{
			~FProgressListener()
			{
				Clear();
			}
			void Clear()
			{
				FMediaCriticalSection::ScopedLock lock(Lock);
				ProgressDelegate.clear();
				CompletionDelegate.clear();
			}
			int32 CallProgressDelegate(const FRequest* Request)
			{
				FMediaCriticalSection::ScopedLock lock(Lock);
				if (!ProgressDelegate.empty())
				{
					return ProgressDelegate(Request);
				}
				return 0;
			}
			void CallCompletionDelegate(const FRequest* Request)
			{
				FMediaCriticalSection::ScopedLock lock(Lock);
				if (!CompletionDelegate.empty())
				{
					CompletionDelegate(Request);
				}
			}
			FMediaCriticalSection	Lock;
			FProgressDelegate		ProgressDelegate;
			FCompletionDelegate		CompletionDelegate;
		};

		struct FReceiveBuffer
		{
			FPODRingbuffer		Buffer;
			bool				bEnableRingbuffer = false;
		};

		struct FParams
		{
			void AddFromHeaderList(const TArray<FString>& InHeaderList)
			{
				for(int32 i=0; i<InHeaderList.Num(); ++i)
				{
					HTTP::FHTTPHeader h;
					h.SetFromString(InHeaderList[i]);
					RequestHeaders.Emplace(MoveTemp(h));
				}
			}

			FString								URL;							//!< URL
			FString								Verb;							//!< GET (default if not set), HEAD, OPTIONS,....
			ElectraHTTPStream::FHttpRange		Range;							//!< Optional request range
			TArray<HTTP::FHTTPHeader>			RequestHeaders;					//!< Request headers
			TMediaOptionalValue<FString>		AcceptEncoding;					//!< Optional accepted encoding
			FTimeValue							ConnectTimeout;					//!< Optional timeout for connecting to the server
			FTimeValue							NoDataTimeout;					//!< Optional timeout when no data is being received
			TArray<uint8>						PostData;						//!< Data for POST
		};


		struct FRequest
		{
			FParams								Parameters;
			FParamDict							Options;
			HTTP::FConnectionInfo				ConnectionInfo;
			TWeakPtrTS<FReceiveBuffer>			ReceiveBuffer;
			TWeakPtrTS<FProgressListener>		ProgressListener;
			TSharedPtrTS<IHTTPResponseCache>	ResponseCache;
			bool								bAutoRemoveWhenComplete = false;
		};

		virtual void AddRequest(TSharedPtrTS<FRequest> Request, bool bAutoRemoveWhenComplete) = 0;
		virtual void RemoveRequest(TSharedPtrTS<FRequest> Request, bool bDoNotWaitForRemoval) = 0;

	};


} // namespace Electra


