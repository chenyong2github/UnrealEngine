// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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

	class FMount
	{
	public:
						FMount(const fs::path& InDir);
		uint32			GetId() const;
		FString			GetDir() const;
		uint32			GetTraceCount() const;
		const FTrace*	GetTraceInfo(uint32 Index) const;
		bool			HasTrace(uint32 Id) const;

	private:
		friend			FStore;
		FTrace*			GetTrace(uint32 Id) const;
		FTrace*			AddTrace(const char* Path);
		uint32			Refresh();
		fs::path		Dir;
		TArray<FTrace*>	Traces;
		uint32			Id;
	};

						FStore(asio::io_context& IoContext, const fs::path& InStoreDir);
						~FStore();
	void				Close();
	bool				AddMount(const fs::path& Dir);
	bool				RemoveMount(uint32 Id);
	const FMount*		GetMount(uint32 Id) const;
	uint32				GetMountCount() const;
	const FMount*		GetMountInfo(uint32 Index) const;

	FString				GetStoreDir() const;
	uint32				GetChangeSerial() const;
	uint32				GetTraceCount() const;
	const FTrace*		GetTraceInfo(uint32 Index) const;
	bool				HasTrace(uint32 Id) const;
	FNewTrace			CreateTrace();
	FAsioReadable*		OpenTrace(uint32 Id);
	class				FDirWatcher;

private:
	FTrace*				GetTrace(uint32 Id, FMount** OutMount=nullptr) const;
	void				Refresh();
	void				WatchDir();
	asio::io_context&	IoContext;
	TArray<FMount*>		Mounts;
	uint32				ChangeSerial;
	FDirWatcher*		DirWatcher = nullptr;
};

/* vim: set noexpandtab : */
