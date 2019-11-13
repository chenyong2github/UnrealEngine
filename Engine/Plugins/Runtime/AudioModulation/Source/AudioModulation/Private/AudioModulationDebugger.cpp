// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AudioModulationDebugger.h"

#if WITH_AUDIOMODULATION
#if !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
#include "AudioModulationLogging.h"
#include "AudioThread.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "IAudioExtensionPlugin.h"
#include "Misc/CoreDelegates.h"
#include "SoundModulationPatch.h"
#include "SoundModulationProxy.h"
#include "SoundModulationValue.h"


namespace AudioModulation
{
	namespace
	{
		const int32 MaxNameLength = 40;
		const int32 XIndent       = 36;

		FColor GetUnitRenderColor(const float Value, const FVector2D& Range)
		{
			return Value > Range.Y || Range.X < Value
				? FColor::Red
				: FColor::Green;
		}

		int32 RenderStatLFO(const TArray<FLFODebugInfo>& FilteredLFOs, FCanvas& Canvas, int32 X, int32 Y, const UFont& Font)
		{
			int32 Height = 12;
			int32 Width = 12;
			Font.GetStringHeightAndWidth(TEXT("@"), Height, Width);

			// Determine minimum width of cells
			static const FString NameHeader      = TEXT("Name");
			static const FString ValueHeader     = TEXT("Value");
			static const FString AmplitudeHeader = TEXT("Amplitude");
			static const FString FrequencyHeader = TEXT("Frequency");
			static const FString OffsetHeader    = TEXT("Offset");
			static const TArray<int32> StaticCellWidths =
			{
				NameHeader.Len(),
				ValueHeader.Len(),
				AmplitudeHeader.Len(),
				FrequencyHeader.Len(),
				OffsetHeader.Len(),
			};

			int32 CellWidth = FMath::Max(StaticCellWidths);
			for (const FLFODebugInfo& LFODebugInfo : FilteredLFOs)
			{
				CellWidth = FMath::Max(CellWidth, LFODebugInfo.Name.Len());
			}

			Canvas.DrawShadowedString(X + XIndent, Y, TEXT("Active LFOs:"), &Font, FColor::Red);
			Y += Height;
			X += XIndent;

			int32 RowX = X;
			Canvas.DrawShadowedString(RowX, Y, *NameHeader, &Font, FColor::White);
			RowX += Width * CellWidth;

			Canvas.DrawShadowedString(RowX, Y, *ValueHeader, &Font, FColor::White);
			RowX += Width * CellWidth;

			Canvas.DrawShadowedString(RowX, Y, *AmplitudeHeader, &Font, FColor::White);
			RowX += Width * CellWidth;

			Canvas.DrawShadowedString(RowX, Y, *FrequencyHeader, &Font, FColor::White);
			RowX += Width * CellWidth;

			Canvas.DrawShadowedString(RowX, Y, *OffsetHeader, &Font, FColor::White);
			RowX += Width * CellWidth;

			Y += Height;
			for (const FLFODebugInfo& LFODebugInfo : FilteredLFOs)
			{
				RowX = X;

				FString Name = LFODebugInfo.Name.Left(MaxNameLength);
				Name += FString::Printf(TEXT(" (%u)"), LFODebugInfo.RefCount);

				if (Name.Len() < MaxNameLength)
				{
					Name = Name.RightPad(MaxNameLength - Name.Len());
				}

				const FString ValueString  = FString::Printf(TEXT("%.6f"), LFODebugInfo.Value);

				Canvas.DrawShadowedString(RowX, Y, *Name, &Font, FColor::Green);
				RowX += Width * CellWidth;

				Canvas.DrawShadowedString(RowX, Y, *ValueString, &Font, FColor::Green);
				RowX += Width * CellWidth;

				Y += Height;
			}

			return Y;
		}

		int32 RenderStatMixMatrix(const TArray<FControlBusMixDebugInfo>& FilteredMixes, const TArray<FControlBusDebugInfo>& FilteredBuses, FCanvas& Canvas, int32 X, int32 Y, const UFont& Font)
		{
			int32 Height = 12;
			int32 Width = 12;
			Font.GetStringHeightAndWidth(TEXT("@"), Height, Width);
			X += XIndent;

			Canvas.DrawShadowedString(X, Y, TEXT("Bus Mix Matrix:"), &Font, FColor::Red);
			Y += Height;

			// Determine minimum width of cells
			static const FString MixSubTotalHeader = TEXT("Mix");
			static const FString LFOSubTotalHeader = TEXT("LFO");
			static const FString TotalHeader       = TEXT("Final");
			static const TArray<int32> StaticCellWidths =
			{
				LFOSubTotalHeader.Len(),
				MixSubTotalHeader.Len(),
				TotalHeader.Len(),
				14 /* Minimum width due to value strings X.XXXX(X.XXXX) */
			};

			int32 CellWidth = FMath::Max(StaticCellWidths);
			for (const FControlBusMixDebugInfo& BusMix : FilteredMixes)
			{
				CellWidth = FMath::Max(CellWidth, BusMix.Name.Len());
			}
			for (const FControlBusDebugInfo& Bus : FilteredBuses)
			{
				CellWidth = FMath::Max(CellWidth, Bus.Name.Len());
			}

			// Draw Column Headers
			int32 RowX = X;
			for (const FControlBusDebugInfo& Bus : FilteredBuses)
			{
				FString Name = Bus.Name.Left(MaxNameLength);
				Name += FString::Printf(TEXT(" (%u)"), Bus.RefCount);

				if (Name.Len() < MaxNameLength)
				{
					Name = Name.RightPad(MaxNameLength - Name.Len());
				}

				RowX += Width * CellWidth; // Add before to leave space for row headers
				Canvas.DrawShadowedString(RowX, Y, *Name, &Font, FColor::White);
			}

			// Draw Row Headers
			int32 ColumnY = Y;
			for (const FControlBusMixDebugInfo& BusMix : FilteredMixes)
			{
				FString Name = BusMix.Name.Left(MaxNameLength);
				Name += FString::Printf(TEXT(" (%u)"), BusMix.RefCount);

				if (Name.Len() < MaxNameLength)
				{
					Name = Name.RightPad(MaxNameLength - Name.Len());
				}

				ColumnY += Height; // Add before to leave space for column headers
				Canvas.DrawShadowedString(X, ColumnY, *Name, &Font, FColor::White);
			}

			// Reset Corner of Matrix & Draw Per Bus Data
			RowX = X;
			ColumnY = Y;
			for (const FControlBusMixDebugInfo& BusMix : FilteredMixes)
			{
				ColumnY += Height; // Add before to leave space for column headers
				RowX = X;

				for (const FControlBusDebugInfo& Bus : FilteredBuses)
				{
					RowX += Width * CellWidth; // Add before to leave space for row headers

					float Target = Bus.DefaultValue;
					float Value = Bus.DefaultValue;
					if (const FControlBusMixChannelDebugInfo* Channel = BusMix.Channels.Find(Bus.Id))
					{
						Target = Channel->TargetValue;
						Value  = Channel->CurrentValue;
					}

					if (Target != Value)
					{
						Canvas.DrawShadowedString(RowX, ColumnY, *FString::Printf(TEXT("%.4f(%.4f)"), Value, Target), &Font, FColor::Green);
					}
					else
					{
						Canvas.DrawShadowedString(RowX, ColumnY, *FString::Printf(TEXT("%.4f"), Value), &Font, FColor::Green);
					}
				}
			}

			Y += (FilteredMixes.Num() + 2) * Height; // Add 2, one for header and one for spacing row

			// Draw Sub-Totals & Totals
			Canvas.DrawShadowedString(X, Y, *MixSubTotalHeader, &Font, FColor::Yellow);
			RowX = X;
			for (const FControlBusDebugInfo& Bus : FilteredBuses)
			{
				RowX += Width * CellWidth; // Add before to leave space for row headers

				const float Value = Bus.MixValue;
				if (FMath::IsNaN(Value))
				{
					Canvas.DrawShadowedString(RowX, Y, TEXT("N/A"), &Font, FColor::Green);
				}
				else
				{
					const FColor Color = GetUnitRenderColor(Value, Bus.Range);
					Canvas.DrawShadowedString(RowX, Y, *FString::Printf(TEXT("%.4f"), Value), &Font, Color);
				}
			}
			Y += Height;

			Canvas.DrawShadowedString(X, Y, *LFOSubTotalHeader, &Font, FColor::Yellow);
			RowX = X;
			for (const FControlBusDebugInfo& Bus : FilteredBuses)
			{
				RowX += Width * CellWidth; // Add before to leave space for row headers
				const float Value = Bus.LFOValue;
				const FColor Color = GetUnitRenderColor(Value, Bus.Range);
				Canvas.DrawShadowedString(RowX, Y, *FString::Printf(TEXT("%.4f"), Value), &Font, Color);
			}

			Y += Height;
			Canvas.DrawShadowedString(X, Y, *TotalHeader, &Font, FColor::Yellow);
			RowX = X;
			for (const FControlBusDebugInfo& Bus : FilteredBuses)
			{
				RowX += Width * CellWidth; // Add before to leave space for row headers
				const FColor Color = GetUnitRenderColor(Bus.Value, Bus.Range);
				Canvas.DrawShadowedString(RowX, Y, *FString::Printf(TEXT("%.4f"), Bus.Value), &Font, Color);
			}
			Y += Height;

			return Y + Height;
		}

		template <typename T>
		bool CompareNames(const T& A, const T& B)
		{
			return A.Name < B.Name;
		}

		template <typename T, typename U>
		void FilterDebugArray(const T& Map, const FString& FilterString, int32 MaxCount, TArray<const U*>& FilteredArray)
		{
			int32 FilteredItemCount = 0;
			for (const auto& IdItemPair : Map)
			{
				const U& Item = IdItemPair.Value;
				const bool Filtered = !FilterString.IsEmpty()
					&& !Item.GetName().Contains(FilterString);
				if (!Filtered)
				{
					FilteredArray.Add(&Item);
					if (++FilteredItemCount >= MaxCount)
					{
						return;
					}
				}
			}
		}
	} // namespace <>

	FAudioModulationDebugger::FAudioModulationDebugger()
		: bActive(0)
		, bShowRenderStatLFO(1)
		, bShowRenderStatMix(1)
	{
	}

	void FAudioModulationDebugger::UpdateDebugData(const FReferencedProxies& RefProxies)
	{
		check(IsInAudioThread());

		if (!bActive)
		{
			return;
		}

		static const int32 MaxFilteredBuses = 8;
		TArray<const FControlBusProxy*> FilteredBusProxies;
		FilterDebugArray<FBusProxyMap, FControlBusProxy>(RefProxies.Buses, BusStringFilter, MaxFilteredBuses, FilteredBusProxies);
		TArray<FControlBusDebugInfo> RefreshedFilteredBuses;
		for (const FControlBusProxy* Proxy : FilteredBusProxies)
		{
			FControlBusDebugInfo DebugInfo;
			DebugInfo.DefaultValue = Proxy->GetDefaultValue();
			DebugInfo.Id = Proxy->GetId();
			DebugInfo.LFOValue = Proxy->GetLFOValue();
			DebugInfo.MixValue = Proxy->GetMixValue();
			DebugInfo.Name = Proxy->GetName();
			DebugInfo.Range = Proxy->GetRange();
			DebugInfo.RefCount = Proxy->GetRefCount();
			DebugInfo.Value = Proxy->GetValue();
			RefreshedFilteredBuses.Add(DebugInfo);
		}

		static const int32 MaxFilteredMixes = 16;
		TArray<const FModulatorBusMixProxy*> FilteredMixProxies;
		FilterDebugArray<FBusMixProxyMap, FModulatorBusMixProxy>(RefProxies.BusMixes, MixStringFilter, MaxFilteredMixes, FilteredMixProxies);
		TArray<FControlBusMixDebugInfo> RefreshedFilteredMixes;
		for (const FModulatorBusMixProxy* Proxy : FilteredMixProxies)
		{
			FControlBusMixDebugInfo DebugInfo;
			DebugInfo.Name = Proxy->GetName();
			DebugInfo.RefCount = Proxy->GetRefCount();
			for (const TPair< FBusId, FModulatorBusMixChannelProxy>& Channel : Proxy->Channels)
			{
				FControlBusMixChannelDebugInfo ChannelDebugInfo;
				ChannelDebugInfo.CurrentValue = Channel.Value.Value.GetCurrentValue();
				ChannelDebugInfo.TargetValue = Channel.Value.Value.TargetValue;
				DebugInfo.Channels.Add(Channel.Key, ChannelDebugInfo);
			}
			RefreshedFilteredMixes.Add(DebugInfo);
		}

		static const int32 MaxFilteredLFOs = 8;
		TArray<const FModulatorLFOProxy*> FilteredLFOProxies;
		FilterDebugArray<FLFOProxyMap, FModulatorLFOProxy>(RefProxies.LFOs, LFOStringFilter, MaxFilteredLFOs, FilteredLFOProxies);

		TArray<FLFODebugInfo> RefreshedFilteredLFOs;
		for (const FModulatorLFOProxy* Proxy : FilteredLFOProxies)
		{
			FLFODebugInfo DebugInfo;
			DebugInfo.Name = Proxy->GetName();
			DebugInfo.RefCount = Proxy->GetRefCount();
			DebugInfo.Value = Proxy->GetValue();
			RefreshedFilteredLFOs.Add(DebugInfo);
		}

		FAudioThread::RunCommandOnGameThread([this, RefreshedFilteredBuses, RefreshedFilteredMixes, RefreshedFilteredLFOs]()
		{
			FilteredBuses = RefreshedFilteredBuses;
			FilteredBuses.Sort(&CompareNames<FControlBusDebugInfo>);

			FilteredMixes = RefreshedFilteredMixes;
			FilteredMixes.Sort(&CompareNames<FControlBusMixDebugInfo>);

			FilteredLFOs = RefreshedFilteredLFOs;
			FilteredLFOs.Sort(&CompareNames<FLFODebugInfo>);
		});
	}

	bool FAudioModulationDebugger::OnPostHelp(FCommonViewportClient& ViewportClient, const TCHAR* Stream)
	{
		if (GEngine)
		{
			static const float TimeShowHelp = 10.0f;
			GEngine->AddOnScreenDebugMessage(-1, TimeShowHelp, FColor::Yellow, TEXT("-MixFilter: Substring that filters mixes shown in matrix view"));
			GEngine->AddOnScreenDebugMessage(-1, TimeShowHelp, FColor::Yellow, TEXT("-BusFilter: Substring that filters buses shown in matrix view"));
			GEngine->AddOnScreenDebugMessage(-1, TimeShowHelp, FColor::Yellow, TEXT("-Matrix: Show bus matrix"));
			GEngine->AddOnScreenDebugMessage(-1, TimeShowHelp, FColor::Yellow, TEXT("-LFOFilter: Substring that filters LFOs listed"));
			GEngine->AddOnScreenDebugMessage(-1, TimeShowHelp, FColor::Yellow, TEXT("-LFO: Show LFO debug data"));
			GEngine->AddOnScreenDebugMessage(-1, TimeShowHelp, FColor::Yellow, TEXT("stat SoundModulators:"));
		}

		return true;
	}

	int32 FAudioModulationDebugger::OnRenderStat(FCanvas& Canvas, int32 X, int32 Y, const UFont& Font)
	{
		check(IsInGameThread());

		// Render stats can get called when toggle did not update bActive, so force active
		// if called and update toggle state accordingly, utilizing the last set filters.
		if (!bActive)
		{
			bActive = true;
		}

		if (bShowRenderStatMix)
		{
			Y = RenderStatMixMatrix(FilteredMixes, FilteredBuses, Canvas, X, Y, Font);
		}

		if (bShowRenderStatLFO)
		{
			Y = RenderStatLFO(FilteredLFOs, Canvas, X, Y, Font);
		}

		return Y;
	}

	bool FAudioModulationDebugger::OnToggleStat(FCommonViewportClient& ViewportClient, const TCHAR* Stream)
	{
		check(IsInGameThread());

		if (bActive == ViewportClient.IsStatEnabled(TEXT("SoundModulators")))
		{
			return true;
		}

		if (!bActive)
		{
			if (Stream && Stream[0] != '\0')
			{
				const bool bUpdateLFO = FParse::Param(Stream, TEXT("LFO"));
				const bool bUpdateMix = FParse::Param(Stream, TEXT("Matrix"));

				if (bUpdateLFO || bUpdateMix)
				{
					bShowRenderStatLFO = bUpdateLFO;
					bShowRenderStatMix = bUpdateMix;
				}
				else
				{
					bShowRenderStatLFO = 1;
					bShowRenderStatMix = 1;
				}

				FParse::Value(Stream, TEXT("BusFilter"), BusStringFilter);
				FParse::Value(Stream, TEXT("LFOFilter"), LFOStringFilter);
				FParse::Value(Stream, TEXT("MixFilter"), MixStringFilter);
			}
			else
			{
				bShowRenderStatLFO = 1;
				bShowRenderStatMix = 1;

				BusStringFilter.Reset();
				LFOStringFilter.Reset();
				MixStringFilter.Reset();
			}
		}

		bActive = !bActive;
		return true;
	}
} // namespace AudioModulation
#endif // !UE_BUILD_SHIPPING
#endif // WITH_AUDIOMODULATION