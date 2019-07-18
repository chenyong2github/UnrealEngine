// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EngineUtils.h"

#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Config/DisplayClusterConfigTypes.h"
#include "Game/IDisplayClusterGameManager.h"

#include "DisplayClusterUtils/DisplayClusterTypesConverter.h"

class AActor;
class UDisplayClusterCameraComponent;


namespace DisplayClusterHelpers
{
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Common String helpers
	//////////////////////////////////////////////////////////////////////////////////////////////
	namespace str
	{
		static inline auto BoolToStr(bool bVal, bool bAsWord = true)
		{
			return (bVal ? 
				(bAsWord ? TEXT("true")  : TEXT("1")) :
				(bAsWord ? TEXT("false") : TEXT("0")));
		}

		static void TrimStringValue(FString& InLine, bool bTrimQuotes = true)
		{
			InLine.TrimStartAndEndInline();
			InLine.RemoveFromStart(DisplayClusterStrings::strKeyValSeparator);

			if (bTrimQuotes)
			{
				InLine = InLine.TrimQuotes();
			}

			InLine.TrimStartAndEndInline();
		}

		// Parses string items separated by specified separator into array
		// Example: item1, item2,item3  ,  item4 => {item1, item2, item3, item4}
		template<typename T>
		static TArray<T> StrToArray(const FString& InData, const FString& InSeparator)
		{
			TArray<T> OutData;

			FString TmpR = InData;
			FString TmpL;

			while (TmpR.Split(InSeparator, &TmpL, &TmpR, ESearchCase::IgnoreCase, ESearchDir::FromStart))
			{
				TrimStringValue(TmpL, false);
				OutData.Add(FDisplayClusterTypesConverter::FromString<T>(TmpL));
			}

			TrimStringValue(TmpR, false);
			OutData.Add(FDisplayClusterTypesConverter::FromString<T>(TmpR));

			return OutData;
		}

		// Exports array data to a string
		// Example: {item1, item2, item3, item4} => "item1,item2,item3,item4"
		template<typename T>
		static FString ArrayToStr(const TArray<T>& InData, const FString& InSeparator = DisplayClusterStrings::strArrayValSeparator, bool bAddQuotes = true)
		{
			static const auto Quotes = TEXT("\"");

			FString ResultStr;
			ResultStr.Reserve(255);

			if (bAddQuotes)
			{
				ResultStr = Quotes;
			}

			for (int i = 0; i < InData.Num(); ++i)
			{
				ResultStr += FString::Printf(TEXT("%s%s"), *FDisplayClusterTypesConverter::ToString(InData[i]), *InSeparator);
			}

			if (InSeparator.Len() > 0)
			{
				ResultStr.RemoveAt(ResultStr.Len() - InSeparator.Len());
			}

			if (bAddQuotes)
			{
				ResultStr += Quotes;
			}

			return ResultStr;
		}

		// Parses string of key-value pairs separated by specified separator into map
		// Example: "key1=val1 key2=val2 key3=val3" => {{key1, val2}, {key2, val2}, {key3, val3}}
		template<typename TKey, typename TVal>
		void StrToMap(const FString& InData, const FString& InPairSeparator, const FString& InKeyValSeparator, TMap<TKey, TVal>& OutData)
		{
			TArray<FString> StrPairs = StrToArray<FString>(InData, InPairSeparator);

			for (const FString& StrPair : StrPairs)
			{
				FString StrKey;
				FString StrVal;

				if (StrPair.Split(InKeyValSeparator, &StrKey, &StrVal, ESearchCase::IgnoreCase))
				{
					OutData.Emplace(FDisplayClusterTypesConverter::FromString<TKey>(StrKey), FDisplayClusterTypesConverter::FromString<TVal>(StrVal));
				}
			}
		}

		// Exports map data to a string
		// Example: {{key1,val1},{key2,val2},{key3,val3}} => "key1=val1 key2=val2 key3=var3"
		template<typename TKey, typename TVal>
		FString MapToStr(const TMap<TKey, TVal>& InData, const FString& InPairSeparator = DisplayClusterStrings::strPairSeparator, const FString& InKeyValSeparator = DisplayClusterStrings::strKeyValSeparator, bool bAddQuoutes = true)
		{
			static const auto Quotes = TEXT("\"");
			
			FString ResultStr;
			ResultStr.Reserve(255);

			if (bAddQuoutes)
			{
				ResultStr = Quotes;
			}

			for (const auto& Pair : InData)
			{
				ResultStr = FString::Printf(TEXT("%s%s%s%s%s"), *ResultStr, *FDisplayClusterTypesConverter::ToString(Pair.Key), *InKeyValSeparator, *FDisplayClusterTypesConverter::ToString(Pair.Value), *InPairSeparator);
			}

			if (InPairSeparator.Len() > 0)
			{
				ResultStr.RemoveAt(ResultStr.Len() - InPairSeparator.Len());
			}

			if (bAddQuoutes)
			{
				ResultStr += Quotes;
			}

			return ResultStr;
		}

		// Extracts value either from a command line string or any other line that matches the same format
		// Example: extracting value of param2
		// "param1=value1 param2=value2 param3=value3" => value2
		template<typename T>
		static bool ExtractValue(const FString& InLine, const FString& InParamName, T& OutValue, bool bInTrimQuotes = true)
		{
			FString TmpVal;

			const FString FullParamName = InParamName + DisplayClusterStrings::strKeyValSeparator;
			if (FParse::Value(*InLine, *FullParamName, TmpVal, false))
			{
				TrimStringValue(TmpVal, bInTrimQuotes);
				OutValue = FDisplayClusterTypesConverter::FromString<T>(TmpVal);
				return true;
			}

			return false;
		}

		// Extracts array value either from a command line string or any other line that matches the same format
		// Example: extracting array value of param2
		// "param1=value1 param2="a,b,c,d" param3=value3" => {a,b,c,d}
		template<typename T>
		static bool ExtractArray(const FString& InLine, const FString& InParamName, const FString& InSeparator, TArray<T>& OutValue)
		{
			FString TmpVal;

			if (ExtractValue(InLine, InParamName, TmpVal, false))
			{
				OutValue = StrToArray<T>(TmpVal, InSeparator);
				return true;
			}

			return false;
		}

		// Extracts map value either from a command line string or any other line that matches the same format
		// Example: extracting map value of param2
		// "param1=value1 param2="a:1,b:7,c:22" param3=value3" => {{a,1},{b,7}{c,22}}
		template<typename TKey, typename TVal>
		static bool ExtractMap(const FString& InLine, const FString& InParamName, const FString& InPairSeparator, const FString& InKeyValSeparator, TMap<TKey, TVal>& OutData)
		{
			TArray<FString> TmpPairs;
			if (!ExtractArray(InLine, InParamName, InPairSeparator, TmpPairs))
			{
				return false;
			}

			for (const FString& StrPair : TmpPairs)
			{
				StrToMap(StrPair, InPairSeparator, InKeyValSeparator, OutData);
			}

			return true;
		}
	};


	//////////////////////////////////////////////////////////////////////////////////////////////
	// Array helpers
	//////////////////////////////////////////////////////////////////////////////////////////////	struct str
	namespace arrays
	{
		// Max element in array
		template<typename T>
		T max(const T* data, int size)
		{
			T result = data[0];
			for (int i = 1; i < size; i++)
				if (result < data[i])
					result = data[i];
			return result;
		}

		// Max element's index in array
		template<typename T>
		size_t max_idx(const T* data, int size)
		{
			size_t idx = 0;
			T result = data[0];
			for (int i = 1; i < size; i++)
				if (result < data[i])
				{
					result = data[i];
					idx = i;
				}
			return idx;
		}

		// Min element in array
		template<typename T>
		T min(const T* data, int size)
		{
			T result = data[0];
			for (int i = 1; i < size; i++)
				if (result > data[i])
					result = data[i];
			return result;
		}

		// Min element's index in array
		template<typename T>
		size_t min_idx(const T* data, int size)
		{
			size_t idx = 0;
			T result = data[0];
			for (int i = 1; i < size; i++)
				if (result > data[i])
				{
					result = data[i];
					idx = i;
				}
			return idx;
		}

		// Helper for array size
		template <typename T, size_t n>
		constexpr size_t array_size(const T(&)[n])
		{
			return n;
		}
	}


	//////////////////////////////////////////////////////////////////////////////////////////////
	// Game helpers
	//////////////////////////////////////////////////////////////////////////////////////////////
	namespace game
	{
		template<typename T>
		static void FindAllActors(UWorld* World, TArray<T*>& Out)
		{
			for (TActorIterator<AActor> It(World, T::StaticClass()); It; ++It)
			{
				T* Actor = Cast<T>(*It);
				if (Actor && !Actor->IsPendingKill())
				{
					Out.Add(Actor);
				}
			}
		}

		static UDisplayClusterCameraComponent* GetCamera(const FString& CameraId)
		{
			static const IDisplayClusterGameManager* const GameMgr = IDisplayCluster::Get().GetGameMgr();
			if (GameMgr)
			{
				return GameMgr->GetCameraById(CameraId);
			}

			return nullptr;
		}
	}

	//////////////////////////////////////////////////////////////////////////////////////////////
	// Config helpers
	//////////////////////////////////////////////////////////////////////////////////////////////
	namespace config
	{
		static bool GetLocalClusterNode(FDisplayClusterConfigClusterNode& LocalClusterNode)
		{
			IDisplayCluster& DisplayCluster = IDisplayCluster::Get();

			if (DisplayCluster.GetOperationMode() == EDisplayClusterOperationMode::Disabled)
			{
				return false;
			}

			const IDisplayClusterClusterManager* const ClusterMgr = DisplayCluster.GetClusterMgr();
			if (!ClusterMgr)
			{
				return false;
			}

			const FString LocalNodeId = ClusterMgr->GetNodeId();
			const IDisplayClusterConfigManager* const ConfigMgr = DisplayCluster.GetConfigMgr();
			if (!ConfigMgr)
			{
				return false;
			}

			return ConfigMgr->GetClusterNode(LocalNodeId, LocalClusterNode);
		}

		static bool GetLocalWindow(FDisplayClusterConfigWindow& LocalWindow)
		{
			FDisplayClusterConfigClusterNode LocalClusterNode;
			if (!GetLocalClusterNode(LocalClusterNode))
			{
				return false;
			}

			static const IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
			if (!ConfigMgr)
			{
				return false;
			}

			return ConfigMgr->GetWindow(LocalClusterNode.WindowId, LocalWindow);
		}

		static TArray<FDisplayClusterConfigViewport> GetLocalViewports()
		{
			TArray<FDisplayClusterConfigViewport> LocalViewports;

			FDisplayClusterConfigWindow LocalWindow;
			if (!DisplayClusterHelpers::config::GetLocalWindow(LocalWindow))
			{
				return LocalViewports;
			}

			static const IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
			if (!ConfigMgr)
			{
				return LocalViewports;
			}

			LocalViewports = ConfigMgr->GetViewports().FilterByPredicate([&LocalWindow](const FDisplayClusterConfigViewport& ItemViewport)
			{
				return LocalWindow.ViewportIds.ContainsByPredicate([ItemViewport](const FString& ItemId)
				{
					return ItemViewport.Id.Compare(ItemId, ESearchCase::IgnoreCase) == 0;
				});
			});

			return LocalViewports;
		}

		static TArray<FDisplayClusterConfigPostprocess> GetLocalPostprocess()
		{
			TArray<FDisplayClusterConfigPostprocess> LocalPostprocess;

			FDisplayClusterConfigWindow LocalWindow;
			if (!DisplayClusterHelpers::config::GetLocalWindow(LocalWindow))
			{
				return LocalPostprocess;
			}

			static const IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
			if (!ConfigMgr)
			{
				return LocalPostprocess;
			}

			LocalPostprocess = ConfigMgr->GetPostprocess().FilterByPredicate([&LocalWindow](const FDisplayClusterConfigPostprocess& ItemPostprocess)
			{
				return LocalWindow.PostprocessIds.ContainsByPredicate([ItemPostprocess](const FString& ItemId)
				{
					return ItemPostprocess.Id.Compare(ItemId, ESearchCase::IgnoreCase) == 0;
				});
			});

			return LocalPostprocess;
		}

		static TArray<FDisplayClusterConfigProjection> GetLocalProjections()
		{
			TArray<FDisplayClusterConfigProjection> LocalProjections;

			static const IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
			if (!ConfigMgr)
			{
				return LocalProjections;
			}

			TArray<FDisplayClusterConfigViewport> LocalViewports = GetLocalViewports();

			LocalProjections = ConfigMgr->GetProjections().FilterByPredicate([&LocalViewports](const FDisplayClusterConfigProjection& ItemProjection)
			{
				return LocalViewports.ContainsByPredicate([ItemProjection](const FDisplayClusterConfigViewport& ItemViewport)
				{
					return ItemViewport.ProjectionId.Compare(ItemProjection.Id, ESearchCase::IgnoreCase) == 0;
				});
			});

			return LocalProjections;
		}

		static bool GetViewportProjection(const FString& ViewportId, FDisplayClusterConfigProjection& ViewportProjection)
		{
			static const IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
			if (ConfigMgr)
			{
				FDisplayClusterConfigViewport CfgViewport;
				if (ConfigMgr->GetViewport(ViewportId, CfgViewport))
				{
					if (ConfigMgr->GetProjection(CfgViewport.ProjectionId, ViewportProjection))
					{
						return true;
					}
				}
			}

			return false;
		}

		static FString GetFullPath(const FString& LocalPath)
		{
			static const IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
			if (ConfigMgr)
			{
				return ConfigMgr->GetFullPathToFile(LocalPath);
			}

			return LocalPath;
		}

#if 0
		static TArray<FDisplayClusterConfigScreen> GetLocalScreens()
		{
			TArray<FDisplayClusterConfigScreen> LocalScreens;

			static const IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
			if (!ConfigMgr)
			{
				return LocalScreens;
			}

			TArray<FDisplayClusterConfigViewport> LocalViewports = GetLocalViewports();

			TArray<FDisplayClusterConfigProjection> LocalSimpleProjections;
			LocalViewports.FilterByPredicate([&LocalSimpleProjections, &ConfigMgr](const FDisplayClusterConfigViewport& ItemViewport)
			{
				FDisplayClusterConfigProjection ItemProjection;
				if (ConfigMgr->GetProjection(ItemViewport.ProjectionId, ItemProjection))
				{
					if (ItemProjection.Type.Compare(DisplayClusterStrings::misc::ProjectionTypeSimple, ESearchCase::IgnoreCase) == 0)
					{
						LocalSimpleProjections.Add(ItemProjection);
					}
				}

				return false;
			});


			LocalScreens = ConfigMgr->GetScreens().FilterByPredicate([&LocalSimpleProjections](const FDisplayClusterConfigScreen& ItemScreen)
			{
				return LocalSimpleProjections.ContainsByPredicate([&ItemScreen](const FDisplayClusterConfigProjection& ItemProjection)
				{
					FString ProjScreenId;
					if (DisplayClusterHelpers::str::ExtractValue(ItemProjection.Params, DisplayClusterStrings::cfg::data::projection::Screen, ProjScreenId))
					{
						return ItemScreen.Id.Compare(ProjScreenId, ESearchCase::IgnoreCase) == 0;
					}

					return false;
				});
			});

			return LocalScreens;
		}

		static bool IsLocalScreen(const FString& ScreenId)
		{
			return nullptr != GetLocalScreens().FindByPredicate([ScreenId](const FDisplayClusterConfigScreen& Screen)
			{
				return Screen.Id == ScreenId;
			});
		}
#endif
	}

#if 0
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Threading helpers
	//////////////////////////////////////////////////////////////////////////////////////////////
	namespace thread
	{
		static bool InGameThread()
		{
			if (GIsGameThreadIdInitialized)
			{
				return FPlatformTLS::GetCurrentThreadId() == GGameThreadId;
			}
			else
			{
				return true;
			}
		}

		static bool InRenderThread()
		{
			if (GRenderingThread && !GIsRenderingThreadSuspended.Load(EMemoryOrder::Relaxed))
			{
				return FPlatformTLS::GetCurrentThreadId() == GRenderingThread->GetThreadID();
			}
			else
			{
				return InGameThread();
			}
		}
	}
#endif
};
