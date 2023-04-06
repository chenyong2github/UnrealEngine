// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoCache.h"

namespace UE::IO::Private
{
	FIoCacheRequestBase::FIoCacheRequestBase(FIoReadCallback&& ReadCallback)
		: Callback(MoveTemp(ReadCallback))
	{
	}

	void FIoCacheRequestBase::CompleteRequest(FIoBuffer&& Buffer)
	{
		if (EIoErrorCode::Unknown == ErrorCode.load(std::memory_order_seq_cst))
		{
			Callback(TIoStatusOr<FIoBuffer>(MoveTemp(Buffer)));
			ErrorCode = EIoErrorCode::Ok;
		}
	}

	void FIoCacheRequestBase::CompleteRequest(EIoErrorCode Error)
	{
		if (EIoErrorCode::Unknown == ErrorCode.load(std::memory_order_seq_cst))
		{
			Callback(FIoStatus(Error));
			ErrorCode = Error; 
		}
	}

} // namespace UE::IO::Private

FIoCacheRequest::FIoCacheRequest(UE::IO::Private::FIoCacheRequestBase* Base)
	: Pimpl(Base)
{
}

FIoCacheRequest::~FIoCacheRequest()
{
	if (IsValid())
	{
		if (!Status().IsCompleted())
		{
			Pimpl->Cancel();
			Pimpl->Wait();
		}
	}
}

void FIoCacheRequest::Cancel()
{
	check(IsValid());

	if (!Status().IsCompleted())
	{
		Pimpl->Cancel();
	}
}
