// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{
	class FImplicitObject;

	class CHAOS_API FTrackedGeometryManager
	{
	public:
		static FTrackedGeometryManager& Get()
		{
			static FTrackedGeometryManager Singleton;
			return Singleton;
		}

		void DumpMemoryUsage(FOutputDevice* Ar) const
		{
			struct FMemInfo
			{
				uint32 NumBytes;
				FString DebugInfo;

				bool operator<(const FMemInfo& Other) const { return NumBytes < Other.NumBytes; }
			};

			TArray<FMemInfo> MemEntries;
			uint32 TotalBytes = 0;
			for (const auto& Itr : SharedGeometry)
			{
				FMemInfo Info;
				Info.DebugInfo = Itr.Value;
				TArray<uint8> Data;
				FMemoryWriter MemAr(Data);
				FChaosArchive ChaosAr(MemAr);
				FImplicitObject* NonConst = const_cast<FImplicitObject*>(Itr.Key.Get());	//only doing this to write out, serialize is non const for read in
				NonConst->Serialize(ChaosAr);
				Info.NumBytes = Data.Num();
				MemEntries.Add(Info);
				TotalBytes += Info.NumBytes;
			}

			MemEntries.Sort();

			Ar->Logf(TEXT(""));
			Ar->Logf(TEXT("Chaos Tracked Geometry:"));
			Ar->Logf(TEXT(""));

			for (const FMemInfo& Info : MemEntries)
			{
				Ar->Logf(TEXT("%-10d %s"), Info.NumBytes, *Info.DebugInfo);
			}

			Ar->Logf(TEXT("%-10d Total"), TotalBytes);
		}

	private:
		TMap<TSerializablePtr<FImplicitObject>, FString> SharedGeometry;
		FCriticalSection CriticalSection;

		friend FImplicitObject;

		//These are private because of various threading considerations. ImplicitObject does the cleanup because it needs extra information
		void AddGeometry(TSerializablePtr<FImplicitObject> Geometry, const FString& DebugInfo)
		{
			FScopeLock Lock(&CriticalSection);
			SharedGeometry.Add(Geometry, DebugInfo);
		}

		void RemoveGeometry(const FImplicitObject* Geometry)
		{
			FScopeLock Lock(&CriticalSection);
			TSerializablePtr<FImplicitObject> Dummy;
			Dummy.SetFromRawLowLevel(Geometry);
			SharedGeometry.Remove(Dummy);
		}
	};
}
