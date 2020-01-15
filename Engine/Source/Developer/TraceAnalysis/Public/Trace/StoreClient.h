// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class TRACEANALYSIS_API FStoreClient
{
public:
	struct TRACEANALYSIS_API FStatus
	{
		uint32			GetRecorderPort() const;
	};

	struct TRACEANALYSIS_API FTraceInfo
	{
		uint32			GetId() const;
		uint64			GetSize() const;
		//const TCHAR*	GetMetadata(const TCHAR* Key) const;
		//template <typename Lambda> uint32 ReadMetadata(Lambda&& Callback) const;
	};

	struct TRACEANALYSIS_API FTraceData
	{
						~FTraceData();
		int32			Read(void* Dest, uint32 DestSize);
		bool			IsValid() const;

	private:
		friend			FStoreClient;
		UPTRINT			Handle = 0;
	};
						FStoreClient(const TCHAR* Host, uint16 Port);
						~FStoreClient();
	bool				IsValid() const;
	const FStatus*		GetStatus();
	uint32				GetTraceCount();
	const FTraceInfo*	GetTraceInfo(uint32 Index);
	bool				ReadTrace(uint32 Id, FTraceData& Out);

#if 0
	template <typename Lambda> uint32 GetTraceInfos(uint32 StartIndex, uint32 Count, Lambda&& Callback) const;
#endif // 0

#if 0
	struct TRACEANALYSIS_API FSessionInfo
	{
		uint32			GetId() const;
		uint32			GetTraceId() const;
		uint32			GetIpAddress() const;
	};
	uint32				GetSessionCount() const;
	const FSessionInfo* GetSessionInfo(uint32 Index, Lambda&& Callback) const;
#endif // 0
#if 0
	template <typename Lambda> uint32	GetSessionInfos(uint32 StartIndex, uint32 Count, Lambda&& Callback) const;
#endif // 0

#if 0
	// -------
	class IStoreSubscriber
	{
		virtual void	OnStoreEvent() = 0;
	};
	bool				Subscribe(IStoreSubscriber* Subscriber);
	bool				Unsubscribe(IStoreSubscriber* Subscriber);
#endif // 0

private:
	struct				FImpl;
	FImpl*				Impl;

private:
						FStoreClient(const FStoreClient&) = delete;
						FStoreClient(const FStoreClient&&) = delete;
	void				operator = (const FStoreClient&) = delete;
	void				operator = (const FStoreClient&&) = delete;
};

} // namespace Trace
