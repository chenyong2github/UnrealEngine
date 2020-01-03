// Copyright Epic Games, Inc. All Rights Reserved.

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
		static inline FString BoolToStr(bool bVal, bool bAsWord = true)
		{
			return (bVal ? 
				(bAsWord ? FString(TEXT("true"))  : FString(TEXT("1"))) :
				(bAsWord ? FString(TEXT("false")) : FString(TEXT("0"))));
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
		static TArray<T> StrToArray(const FString& InData, const FString& InSeparator, bool bCullEmpty = true)
		{
			TArray<T> OutData;
			TArray<FString> TempData;
			
			InData.ParseIntoArray(TempData, *InSeparator, bCullEmpty);

			for (FString& Item : TempData)
			{
				TrimStringValue(Item, false);
			}

			for (const FString& Item : TempData)
			{
				if (!bCullEmpty && Item.IsEmpty())
				{
					OutData.Add(T());
				}
				else
				{
					OutData.Add(FDisplayClusterTypesConverter::template FromString<T>(Item));
				}
			}

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

			if (InSeparator.Len() > 0 && InData.Num() > 0)
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
					OutData.Emplace(FDisplayClusterTypesConverter::template FromString<TKey>(StrKey), FDisplayClusterTypesConverter::template FromString<TVal>(StrVal));
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
				OutValue = FDisplayClusterTypesConverter::template FromString<T>(TmpVal);
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
	}

	//////////////////////////////////////////////////////////////////////////////////////////////
	// Math helpers
	//////////////////////////////////////////////////////////////////////////////////////////////
	namespace math
	{
		static FMatrix GetProjectionMatrixFromOffsets(float l, float r, float t, float b, float n, float f)
		{
			const float LeftFOVRad   = FMath::Atan(l / n);
			const float RightFOVRad  = FMath::Atan(r / n);
			const float TopFOVRad    = FMath::Atan(t / n);
			const float BottomFOVRad = FMath::Atan(b / n);

			{
				const float MinFOVRad = FMath::DegreesToRadians(1.0f);   // min FOV = 1 degree
				const float MaxFOVRad = FMath::DegreesToRadians(179.0f); // max FOV = 179 degree

				// clamp FOV values:
				const float FOVRadH  = (RightFOVRad - LeftFOVRad);
				float NewRightFOVRad = (FOVRadH < MinFOVRad) ? (LeftFOVRad + MinFOVRad) : RightFOVRad;
				NewRightFOVRad       = (FOVRadH > MaxFOVRad) ? (LeftFOVRad + MaxFOVRad) : NewRightFOVRad;

				const float FOVRadV = (TopFOVRad - BottomFOVRad);
				float NewTopFOVRad  = (FOVRadV < MinFOVRad) ? (BottomFOVRad + MinFOVRad) : TopFOVRad;
				NewTopFOVRad        = (FOVRadV > MaxFOVRad) ? (BottomFOVRad + MaxFOVRad) : NewTopFOVRad;

				if (RightFOVRad != NewRightFOVRad)
				{
					r = float(n * FMath::Tan(NewRightFOVRad));
					//! add LOG warning
				}
				if (TopFOVRad != NewTopFOVRad)
				{
					t = float(n * FMath::Tan(NewTopFOVRad));
					//! add LOG warning
				}
			}

			const float mx = 2.f * n / (r - l);
			const float my = 2.f * n / (t - b);
			const float ma = -(r + l) / (r - l);
			const float mb = -(t + b) / (t - b);

			// Support unlimited far plane (f==n)
			const float mc = (f == n) ? (1.0f - Z_PRECISION) : (f / (f - n));
			const float md = (f == n) ? (-n * (1.0f - Z_PRECISION)) : (-(f * n) / (f - n));

			const float me = 1.f;

			// Normal LHS
			const FMatrix ProjectionMatrix = FMatrix(
				FPlane(mx,  0,  0,  0),
				FPlane(0,  my,  0,  0),
				FPlane(ma, mb, mc, me),
				FPlane(0,  0,  md,  0));

			// Invert Z-axis (UE4 uses Z-inverted LHS)
			static const FMatrix flipZ = FMatrix(
				FPlane(1, 0, 0, 0),
				FPlane(0, 1, 0, 0),
				FPlane(0, 0, -1, 0),
				FPlane(0, 0, 1, 1));

			const FMatrix ResultMatrix(ProjectionMatrix * flipZ);

			return ResultMatrix;
		}

		static FMatrix GetProjectionMatrixFromAngles(float LeftAngle, float RightAngle, float TopAngle, float BottomAngle, float ZNear, float ZFar)
		{
			const float t = ZNear * FMath::Tan(FMath::DegreesToRadians(TopAngle));
			const float b = ZNear * FMath::Tan(FMath::DegreesToRadians(BottomAngle));
			const float l = ZNear * FMath::Tan(FMath::DegreesToRadians(LeftAngle));
			const float r = ZNear * FMath::Tan(FMath::DegreesToRadians(RightAngle));

			return DisplayClusterHelpers::math::GetProjectionMatrixFromOffsets(l, r, t, b, ZNear, ZFar);
		}
	}
};
