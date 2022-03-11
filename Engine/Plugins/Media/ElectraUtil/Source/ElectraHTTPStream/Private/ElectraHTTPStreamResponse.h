// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Templates/SharedPointer.h"

#include "ElectraHTTPStream.h"
#include "ElectraHTTPStreamBuffer.h"

class FElectraHTTPStreamResponse : public IElectraHTTPStreamResponse
{
public:
	FElectraHTTPStreamResponse() = default;
	virtual ~FElectraHTTPStreamResponse() = default;

	virtual EStatus GetStatus() override
	{ return CurrentStatus; }

	virtual EState GetState() override
	{ return CurrentState; }

	virtual FString GetErrorMessage() override
	{ 
		FScopeLock lock(&ErrorLock);
		return ErrorMessage; 
	}

	virtual int32 GetHTTPResponseCode() override
	{ return HTTPResponseCode; }

	virtual int64 GetNumResponseBytesReceived() override
	{ return ResponseData.GetNumTotalBytesAdded(); }

	virtual int64 GetNumRequestBytesSent() override
	{ return 0;	}

	virtual FString GetEffectiveURL() override
	{ return EffectiveURL; }

	virtual void GetAllHeaders(TArray<FElectraHTTPStreamHeader>& OutHeaders) override
	{ OutHeaders = ResponseHeaders;	}

	virtual FString GetHTTPStatusLine() override
	{ return HTTPStatusLine; }
	virtual FString GetContentLengthHeader() override
	{ return ContentLength; }
	virtual FString GetContentRangeHeader() override
	{ return ContentRange; }
	virtual FString GetAcceptRangesHeader() override
	{ return AcceptRanges; }
	virtual FString GetTransferEncodingHeader() override
	{ return TransferEncoding; }
	virtual FString GetContentTypeHeader() override
	{ return ContentType; }

	virtual IElectraHTTPStreamBuffer& GetResponseData() override
	{ return ResponseData; }

	virtual double GetTimeElapsed() override
	{ return CurrentStatus == EStatus::NotRunning ? 0.0 : TimeUntilFinished > 0.0 ? TimeUntilFinished : FPlatformTime::Seconds() - StartTime; }

	virtual double GetTimeSinceLastDataArrived() override
	{ return CurrentStatus == EStatus::NotRunning || TimeOfMostRecentReceive == 0.0 || TimeUntilFinished > 0.0 ? 0.0 : FPlatformTime::Seconds() - TimeOfMostRecentReceive; }

	virtual double GetTimeUntilNameResolved() override
	{ return TimeUntilNameResolved; }
	virtual double GetTimeUntilConnected() override
	{ return TimeUntilConnected; }
	virtual double GetTimeUntilRequestSent() override
	{ return TimeUntilRequestSent; }
	virtual double GetTimeUntilHeadersAvailable() override
	{ return TimeUntilHeadersAvailable; }
	virtual double GetTimeUntilFirstByte() override
	{ return TimeUntilFirstByte; }
	virtual double GetTimeUntilFinished() override
	{ return TimeUntilFinished; }

	void AddResponseData(const TArray<uint8>& InNewData)
	{ ResponseData.AddData(InNewData); }
	void AddResponseData(TArray<uint8>&& InNewData)
	{ ResponseData.AddData(MoveTemp(InNewData)); }
	void AddResponseData(const TConstArrayView<const uint8>& InNewData)
	{ ResponseData.AddData(InNewData); }


	void SetEOS()
	{ ResponseData.SetEOS(); }

	void SetErrorMessage(const FString& InErrorMessage)
	{
		FScopeLock lock(&ErrorLock);
		ErrorMessage = InErrorMessage;
	}

	EStatus CurrentStatus = EStatus::NotRunning;
	EState CurrentState = EState::Connecting;

	TArray<FElectraHTTPStreamHeader> ResponseHeaders;
	FString ContentLength;
	FString ContentRange;
	FString AcceptRanges;
	FString TransferEncoding;
	FString ContentType;

	FString HTTPStatusLine;
	FString EffectiveURL;
	int32 HTTPResponseCode = 0;

	FElectraHTTPStreamBuffer ResponseData;

	// Timing
	double StartTime = 0.0;
	double TimeUntilNameResolved = 0.0;
	double TimeUntilConnected = 0.0;
	double TimeUntilRequestSent = 0.0;
	double TimeUntilHeadersAvailable = 0.0;
	double TimeUntilFirstByte = 0.0;
	double TimeUntilFinished = 0.0;
	double TimeOfMostRecentReceive = 0.0;

	// Error message.
	FCriticalSection ErrorLock;
	FString ErrorMessage;
};
