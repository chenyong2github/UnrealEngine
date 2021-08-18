// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#define TS_WITH_DIR_WATCHER TS_ON

#include "Asio.h"
#include "AsioFile.h"
#include "Foundation.h"
#include "Utils.h"

class FAsioReadable;
class FAsioWriteable;

////////////////////////////////////////////////////////////////////////////////
enum class EStoreVersion
{
	Value = 0x0100, // 0xMMmm MM=major, mm=minor
};

////////////////////////////////////////////////////////////////////////////////
class FStore
{
public:
	class FTrace
	{
	public:
							FTrace(const char* InPath);
		const FStringView&	GetName() const;
		uint32				GetId() const;
		uint64				GetSize() const;
		uint64				GetTimestamp() const;

	private:
		friend				FStore;
		FString				Path;
		FStringView			Name;
		uint64				Timestamp;
		uint32				Id = 0;
	};

	struct FNewTrace
	{
		uint32			Id;
		FAsioWriteable* Writeable;
	};

						FStore(asio::io_context& IoContext, const char* InStoreDir);
						~FStore();
	void				Close();
	const char*			GetStoreDir() const;
	uint32				GetChangeSerial() const;
	uint32				GetTraceCount() const;
	const FTrace*		GetTraceInfo(uint32 Index) const;
	bool				HasTrace(uint32 Id) const;
	FNewTrace			CreateTrace();
	FAsioReadable*		OpenTrace(uint32 Id);

private:
	FTrace*				GetTrace(uint32 Id) const;
	FTrace*				AddTrace(const char* Path);
	void				ClearTraces();
	void				Refresh();
	asio::io_context&	IoContext;
	FString				StoreDir;
	TArray<FTrace*>		Traces;
	uint32				ChangeSerial;
#if TS_USING(TS_WITH_DIR_WATCHER)
	class				FDirWatcher;
	void				WatchDir();
	FDirWatcher*		DirWatcher = nullptr;
#endif
#if 0
	int32				LastTraceId = -1;
#endif // 0
};

/* vim: set noexpandtab : */
