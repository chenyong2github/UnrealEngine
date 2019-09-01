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

		int32 RenderStatLFO(const TArray<FModulatorLFOProxy>& FilteredLFOs, FCanvas& Canvas, int32 X, int32 Y, const UFont& Font)
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
			for (const FModulatorLFOProxy& LFOProxy : FilteredLFOs)
			{
				CellWidth = FMath::Max(CellWidth, LFOProxy.GetName().Len());
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
			for (const FModulatorLFOProxy& LFOPair : FilteredLFOs)
			{
				RowX = X;

				FString Name = LFOPair.GetName().Left(MaxNameLength);
				if (Name.Len() < MaxNameLength)
				{
					Name = Name.RightPad(MaxNameLength - Name.Len());
				}

				const FString ValueString  = FString::Printf(TEXT("%.6f"), LFOPair.GetValue());
				const FString AmpString    = FString::Printf(TEXT("%.6f"), LFOPair.GetAmplitude());
				const FString FreqString   = FString::Printf(TEXT("%.6f"), LFOPair.GetFreq());
				const FString OffsetString = FString::Printf(TEXT("%.6f"), LFOPair.GetOffset());

				Canvas.DrawShadowedString(RowX, Y, *Name, &Font, FColor::Green);
				RowX += Width * CellWidth;

				Canvas.DrawShadowedString(RowX, Y, *ValueString, &Font, FColor::Green);
				RowX += Width * CellWidth;

				Canvas.DrawShadowedString(RowX, Y, *AmpString, &Font, FColor::Green);
				RowX += Width * CellWidth;

				Canvas.DrawShadowedString(RowX, Y, *FreqString, &Font, FColor::Green);
				RowX += Width * CellWidth;

				Canvas.DrawShadowedString(RowX, Y, *OffsetString, &Font, FColor::Green);
				RowX += Width * CellWidth;

				Y += Height;
			}

			return Y;
		}

		int32 RenderStatMixMatrix(const TArray<FModulatorBusMixProxy>& FilteredMixes, const TArray<FModulatorBusProxy>& FilteredBuses, FCanvas& Canvas, int32 X, int32 Y, const UFont& Font)
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
			for (const FModulatorBusMixProxy& BusMix : FilteredMixes)
			{
				CellWidth = FMath::Max(CellWidth, BusMix.GetName().Len());
			}
			for (const FModulatorBusProxy& BusPair : FilteredBuses)
			{
				CellWidth = FMath::Max(CellWidth, BusPair.GetName().Len());
			}

			// Draw Column Headers
			int32 RowX = X;
			for (const FModulatorBusProxy& BusPair : FilteredBuses)
			{
				FString Name = BusPair.GetName().Left(MaxNameLength);
				if (Name.Len() < MaxNameLength)
				{
					Name = Name.RightPad(MaxNameLength - Name.Len());
				}

				RowX += Width * CellWidth; // Add before to leave space for row headers
				Canvas.DrawShadowedString(RowX, Y, *Name, &Font, FColor::White);
			}

			// Draw Row Headers
			int32 ColumnY = Y;
			for (const FModulatorBusMixProxy& BusMix : FilteredMixes)
			{
				FString Name = BusMix.GetName().Left(MaxNameLength);
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
			for (const FModulatorBusMixProxy& BusMix : FilteredMixes)
			{
				ColumnY += Height; // Add before to leave space for column headers
				RowX = X;

				for (const FModulatorBusProxy& Bus : FilteredBuses)
				{
					RowX += Width * CellWidth; // Add before to leave space for row headers

					float Target = Bus.GetDefaultValue();
					float Value = Bus.GetDefaultValue();
					if (const FModulatorBusMixChannelProxy* ChannelProxy = BusMix.Channels.Find(Bus.GetBusId()))
					{
						Target = ChannelProxy->Value.TargetValue;
						Value  = ChannelProxy->Value.GetCurrentValue();
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
			for (const FModulatorBusProxy& BusPair : FilteredBuses)
			{
				RowX += Width * CellWidth; // Add before to leave space for row headers

				const float Value = BusPair.GetMixValue();
				if (FMath::IsNaN(Value))
				{
					Canvas.DrawShadowedString(RowX, Y, TEXT("N/A"), &Font, FColor::Green);
				}
				else
				{
					const FColor Color = GetUnitRenderColor(Value, BusPair.GetRange());
					Canvas.DrawShadowedString(RowX, Y, *FString::Printf(TEXT("%.4f"), Value), &Font, Color);
				}
			}
			Y += Height;

			Canvas.DrawShadowedString(X, Y, *LFOSubTotalHeader, &Font, FColor::Yellow);
			RowX = X;
			for (const FModulatorBusProxy& BusPair : FilteredBuses)
			{
				RowX += Width * CellWidth; // Add before to leave space for row headers
				const float Value = BusPair.GetLFOValue();
				const FColor Color = GetUnitRenderColor(Value, BusPair.GetRange());
				Canvas.DrawShadowedString(RowX, Y, *FString::Printf(TEXT("%.4f"), Value), &Font, Color);
			}

			Y += Height;
			Canvas.DrawShadowedString(X, Y, *TotalHeader, &Font, FColor::Yellow);
			RowX = X;
			for (const FModulatorBusProxy& BusPair : FilteredBuses)
			{
				RowX += Width * CellWidth; // Add before to leave space for row headers
				const float Value = BusPair.GetValue();
				const FColor Color = GetUnitRenderColor(Value, BusPair.GetRange());
				Canvas.DrawShadowedString(RowX, Y, *FString::Printf(TEXT("%.4f"), Value), &Font, Color);
			}
			Y += Height;

			return Y + Height;
		}

		template <typename T>
		bool CompareNames(const T& A, const T& B)
		{
			return A.GetName() < B.GetName();
		}

		template <typename T, typename U>
		void FilterDebugArray(const T& Map, const FString& FilterString, int32 MaxCount, TArray<U>& FilteredArray)
		{
			FilteredArray.Reset(MaxCount);

			int32 FilteredItemCount = 0;
			for (const auto& IdItemPair : Map)
			{
				const U& Item = IdItemPair.Value;
				const bool Filtered = !FilterString.IsEmpty()
					&& !Item.GetName().Contains(FilterString);
				if (!Filtered)
				{
					FilteredArray.Add(Item);
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

	void FAudioModulationDebugger::UpdateDebugData(
		const BusProxyMap&    ActiveBuses,
		const BusMixProxyMap& ActiveMixes,
		const LFOProxyMap&    ActiveLFOs)
	{
		check(IsInAudioThread());

		if (!bActive)
		{
			return;
		}

		static const int32 MaxFilteredBuses = 8;
		TArray<FModulatorBusProxy> InFilteredBuses;
		FilterDebugArray<BusProxyMap, FModulatorBusProxy>(ActiveBuses, BusStringFilter, MaxFilteredBuses, InFilteredBuses);

		static const int32 MaxFilteredMixes = 16;
		TArray<FModulatorBusMixProxy> InFilteredMixes;
		FilterDebugArray<BusMixProxyMap, FModulatorBusMixProxy>(ActiveMixes, MixStringFilter, MaxFilteredMixes, InFilteredMixes);

		static const int32 MaxFilteredLFOs = 8;
		TArray<FModulatorLFOProxy> InFilteredLFOs;
		FilterDebugArray<LFOProxyMap, FModulatorLFOProxy>(ActiveLFOs, LFOStringFilter, MaxFilteredLFOs, InFilteredLFOs);

		FAudioThread::RunCommandOnGameThread([this, InFilteredBuses, InFilteredMixes, InFilteredLFOs]()
		{
			FilteredBuses = InFilteredBuses;
			FilteredBuses.Sort(&CompareNames<FModulatorBusProxy>);

			FilteredMixes = InFilteredMixes;
			FilteredMixes.Sort(&CompareNames<FModulatorBusMixProxy>);

			FilteredLFOs = InFilteredLFOs;
			FilteredLFOs.Sort(&CompareNames<FModulatorLFOProxy>);
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