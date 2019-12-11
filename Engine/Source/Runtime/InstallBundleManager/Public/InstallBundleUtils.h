// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InstallBundleTypes.h"
#include "Async/AsyncWork.h"
#include "Misc/EmbeddedCommunication.h"

namespace InstallBundleUtil
{
	// Returns the app version in the same format as BPS versions
	INSTALLBUNDLEMANAGER_API FString GetAppVersion();

	INSTALLBUNDLEMANAGER_API bool HasInternetConnection(ENetworkConnectionType ConnectionType);

	INSTALLBUNDLEMANAGER_API const TCHAR* GetInstallBundlePauseReason(EInstallBundlePauseFlags Flags);

	// It would really be nice to have these in core
	template<class EnumType>
	constexpr auto& CastAsUnderlying(EnumType &Type)
	{
		static_assert(TIsEnum<EnumType>::Value, "");
		using UnderType = __underlying_type(EnumType);
		return *reinterpret_cast<UnderType*>(&Type);
	}

	template<class EnumType>
	constexpr const auto& CastAsUnderlying(const EnumType &Type)
	{
		static_assert(TIsEnum<EnumType>::Value, "");
		using UnderType = __underlying_type(EnumType);
		return *reinterpret_cast<const UnderType*>(&Type);
	}

	template<class EnumType>
	constexpr auto CastToUnderlying(EnumType Type)
	{
		static_assert(TIsEnum<EnumType>::Value, "");
		using UnderType = __underlying_type(EnumType);
		return static_cast<UnderType>(Type);
	}

	// Keep the engine awake via RAII when running as an embedded app
	class INSTALLBUNDLEMANAGER_API FInstallBundleManagerKeepAwake : public FEmbeddedKeepAwake
	{
		static FName Tag;
		static FName TagWithRendering;
	public:
		FInstallBundleManagerKeepAwake(bool bNeedsRendering = false)
			: FEmbeddedKeepAwake(bNeedsRendering ? TagWithRendering : Tag, bNeedsRendering) {}
	};

	class INSTALLBUNDLEMANAGER_API FInstallBundleManagerScreenSaverControl
	{
		static bool bDidDisableScreensaver;
		static int DisableCount;

		static void IncDisable();
		static void DecDisable();

	public:
		FInstallBundleManagerScreenSaverControl()
		{
			IncDisable();
		}

		~FInstallBundleManagerScreenSaverControl()
		{
			DecDisable();
		}

		FInstallBundleManagerScreenSaverControl(const FInstallBundleManagerScreenSaverControl& Other)
		{
			IncDisable();
		}
		FInstallBundleManagerScreenSaverControl(FInstallBundleManagerScreenSaverControl&& Other) = default;

		FInstallBundleManagerScreenSaverControl& operator=(const FInstallBundleManagerScreenSaverControl& Other) = default;
		FInstallBundleManagerScreenSaverControl& operator=(FInstallBundleManagerScreenSaverControl&& Other)
		{
			DecDisable();
			return *this;
		}
	};

	class INSTALLBUNDLEMANAGER_API FInstallBundleWork : public FNonAbandonableTask
	{
	public:
		FInstallBundleWork() = default;

		FInstallBundleWork(TUniqueFunction<void()> InWork, TUniqueFunction<void()> InOnComplete)
			: WorkFunc(MoveTemp(InWork))
			, OnCompleteFunc(MoveTemp(InOnComplete))
		{}

		void DoWork()
		{
			if (WorkFunc)
			{
				WorkFunc();
			}
		}

		void CallOnComplete()
		{
			if (OnCompleteFunc)
			{
				OnCompleteFunc();
			}
		}

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FInstallBundleWork, STATGROUP_ThreadPoolAsyncTasks);
		}

	private:
		TUniqueFunction<void()> WorkFunc;
		TUniqueFunction<void()> OnCompleteFunc;
	};

	using FInstallBundleTask = FAsyncTask<FInstallBundleWork>;

	INSTALLBUNDLEMANAGER_API void StartInstallBundleAsyncIOTask(TArray<TUniquePtr<FInstallBundleTask>>& Tasks, TUniqueFunction<void()> WorkFunc, TUniqueFunction<void()> OnComplete);

	INSTALLBUNDLEMANAGER_API void FinishInstallBundleAsyncIOTasks(TArray<TUniquePtr<FInstallBundleTask>>& Tasks);

	INSTALLBUNDLEMANAGER_API void CleanupInstallBundleAsyncIOTasks(TArray<TUniquePtr<FInstallBundleTask>>& Tasks);

	struct INSTALLBUNDLEMANAGER_API FContentRequestStateStats
	{
		double StartTime = 0.0;
		double EndTime = 0.0;
		uint64 DataSize = 0;
		bool bOpen = true;

		double GetElapsedTime() const
		{
			return (EndTime > StartTime) ? (EndTime - StartTime) : 0.0;
		}
	};

	struct INSTALLBUNDLEMANAGER_API FContentRequestStats
	{
		double StartTime = 0.0;
		double EndTime = 0.0;
		bool bOpen = true;
		TMap<FString, FContentRequestStateStats> StateStats;

		double GetElapsedTime() const
		{
			return (EndTime > StartTime) ? (EndTime - StartTime) : 0.0;
		}
	};

	class INSTALLBUNDLEMANAGER_API FContentRequestStatsMap
	{
	private:
		TMap<FName, InstallBundleUtil::FContentRequestStats> StatsMap;

	public:
		void StatsBegin(FName BundleName);
		void StatsEnd(FName BundleName);
		void StatsBegin(FName BundleName, const TCHAR* State);
		void StatsEnd(FName BundleName, const TCHAR* State, uint64 DataSize = 0);

		const TMap<FName, InstallBundleUtil::FContentRequestStats>& GetMap() { return StatsMap; }
	};

	struct INSTALLBUNDLEMANAGER_API IBundleSourceContentRequestSharedContext
	{
		virtual ~IBundleSourceContentRequestSharedContext() {}
	};

	struct INSTALLBUNDLEMANAGER_API FContentRequestSharedContext
	{
		FContentRequestSharedContext() = default;
		FContentRequestSharedContext(const FContentRequestSharedContext& Other) = delete;
		FContentRequestSharedContext(FContentRequestSharedContext&& Other) = default;
		FContentRequestSharedContext& operator=(const FContentRequestSharedContext& Other) = delete;
		FContentRequestSharedContext& operator=(FContentRequestSharedContext&& Other) = default;

		TMap<EInstallBundleSourceType, TUniquePtr<IBundleSourceContentRequestSharedContext>> BundleSourceSharedContext;
	};
	using FContentRequestSharedContextPtr = TSharedPtr<FContentRequestSharedContext>;
}
