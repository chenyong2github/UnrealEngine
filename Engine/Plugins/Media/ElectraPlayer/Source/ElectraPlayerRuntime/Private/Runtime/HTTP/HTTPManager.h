// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"

#include "StreamDataBuffer.h"
#include "OptionalValue.h"

#include "ParameterDictionary.h"
#include "ErrorDetail.h"


DECLARE_LOG_CATEGORY_EXTERN(LogElectraHTTPManager, Log, All);

namespace Electra
{
	//
	class IPlayerSessionServices;


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


		struct FHTTPHeader
		{
			FString	Header;
			FString	Value;

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
			struct FRange
			{
				void Reset()
				{
					Start = -1;
					EndIncluding = -1;
					DocumentSize = -1;
				}
				bool IsSet() const
				{
					return Start != -1 || EndIncluding != -1;
				}
				//! Check if the range would result in "0-" for the entire file in which case we don't need to use range request.
				bool IsEverything() const
				{
					return Start <= 0 && EndIncluding < 0;
				}
				FString GetString() const
				{
					FString s, e, d(TEXT("-"));
					if (Start >= 0)
					{
						// An explicit range?
						if (EndIncluding >= Start)
						{
							return FString::Printf(TEXT("%lld-%lld"), (long long int)Start, (long long int)EndIncluding);
						}
						// Start to end, whereever that is.
						else
						{
							return FString::Printf(TEXT("%lld-"), (long long int)Start);
						}
					}
					// Everything
					return FString(TEXT("0-"));
				}
				// Returns the number of bytes in the range, which must be fully specified. An unset or partially open range will return -1.
				int64 GetNumberOfBytes() const
				{
					if (Start >= 0 && EndIncluding >= 0)
					{
						return EndIncluding - Start + 1;
					}
					return -1;
				}
				int64 GetStart() const
				{
					return Start;
				}
				int64 GetEndIncluding() const
				{
					return EndIncluding;
				}
				bool IsOpenEnded() const
				{
					return GetEndIncluding() < 0;
				}
				void Set(const FString& InString)
				{
					int32 DashPos = INDEX_NONE;
					if (InString.FindChar(TCHAR('-'), DashPos))
					{
						// -end
						if (DashPos == 0)
						{
							Start = 0;
							LexFromString(EndIncluding, *InString + 1);
						}
						// start-
						else if (DashPos == InString.Len()-1)
						{
							LexFromString(Start, *InString.Mid(0, DashPos));
							EndIncluding = -1;
						}
						// start-end
						else
						{
							LexFromString(Start, *InString.Mid(0, DashPos));
							LexFromString(EndIncluding, *InString + DashPos + 1);
						}
					}
				}
				void SetStart(int64 InStart)
				{
					Start = InStart;
				}
				void SetEndIncluding(int64 InEndIncluding)
				{
					EndIncluding = InEndIncluding;
				}
				int64 GetDocumentSize() const
				{
					return DocumentSize;
				}
				bool ParseFromContentRangeResponse(const FString& ContentRangeHeader)
				{
					// Examples: <unit> <range-start>-<range-end>/<size>
					//   Content-Range: bytes 26151-157222/7594984
					//   Content-Range: bytes 26151-157222/*
					//   Content-Range: bytes */7594984
					//
					Start = -1;
					EndIncluding = -1;
					DocumentSize = -1;
					FString rh = ContentRangeHeader;
					// In case the entire header is given, remove the header including the separating colon and space.
					rh.RemoveFromStart(TEXT("Content-Range: "), ESearchCase::CaseSensitive);
					// Split into parts
					TArray<FString> Parts;
					const TCHAR* const Delims[3] = {TEXT(" "),TEXT("-"),TEXT("/")};
					int32 NumParts = rh.ParseIntoArray(Parts, Delims, 3);
					if (NumParts)
					{
						if (Parts[0] == TEXT("bytes"))
						{
							Parts.RemoveAt(0);
						}
						// We should now be left with 3 remaining results, the start, end and document size.
						// The case where we get "*/<size>" we treat as invalid.
						if (Parts.Num() == 3)
						{
							if (Parts[0].IsNumeric())
							{
								LexFromString(Start, *Parts[0]);
								if (Parts[1].IsNumeric())
								{
									LexFromString(EndIncluding, *Parts[1]);
									if (Parts[2].IsNumeric())
									{
										LexFromString(DocumentSize, *Parts[2]);
										return true;
									}
									else if (Parts[2] == TEXT("*"))
									{
										DocumentSize = -1;
										return true;
									}
								}
							}
						}
					}
					UE_LOG(LogElectraHTTPManager, Error, TEXT("Failed to parse Content-Range HTTP response header \"%s\""), *ContentRangeHeader);
					return false;
				}
				int64			Start = -1;
				int64			EndIncluding = -1;
				int64			DocumentSize = -1;
			};

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
			FRange								Range;							//!< Optional request range
			int32								SubRangeRequestSize = 0;		//!< If not 0 the size to break the request into smaller range requests into.
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
			bool								bAutoRemoveWhenComplete = false;
		};

		virtual void AddRequest(TSharedPtrTS<FRequest> Request, bool bAutoRemoveWhenComplete) = 0;
		virtual void RemoveRequest(TSharedPtrTS<FRequest> Request, bool bDoNotWaitForRemoval) = 0;

	};


} // namespace Electra


