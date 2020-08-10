// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulationDebugger.h"

#if WITH_AUDIOMODULATION
#if !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
#include "Async/TaskGraphInterfaces.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "AudioThread.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "IAudioModulation.h"
#include "Misc/CoreDelegates.h"
#include "SoundControlBusProxy.h"
#include "SoundModulationPatch.h"
#include "SoundModulationProxy.h"
#include "SoundModulationValue.h"
#include "HAL/IConsoleManager.h"



static float AudioModulationDebugUpdateRateCVar = 0.25f;
FAutoConsoleVariableRef CVarAudioModulationDebugUpdateRate(
	TEXT("au.Debug.SoundModulators.UpdateRate"),
	AudioModulationDebugUpdateRateCVar,
	TEXT("Sets update rate for modulation debug statistics (in seconds).\n")
	TEXT("Default: 0.25f"),
	ECVF_Default);

namespace AudioModulation
{
	namespace Debug
	{
		const int32 MaxNameLength = 40;
		const int32 XIndent       = 36;

		FColor GetUnitRenderColor(const float Value)
		{
			return Value > 1.0f || Value < 0.0f
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
			static const FString TypeHeader		= TEXT("Type");
			static const TArray<int32> StaticCellWidths =
			{
				NameHeader.Len(),
				ValueHeader.Len(),
				AmplitudeHeader.Len(),
				FrequencyHeader.Len(),
				OffsetHeader.Len(),
				TypeHeader.Len()
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

			Canvas.DrawShadowedString(RowX, Y, *TypeHeader, &Font, FColor::White);
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

				Canvas.DrawShadowedString(RowX, Y, *Name, &Font, FColor::Green);
				RowX += Width * CellWidth;

				const FString ValueString = FString::Printf(TEXT("%.6f"), LFODebugInfo.Value);
				Canvas.DrawShadowedString(RowX, Y, *ValueString, &Font, FColor::Green);
				RowX += Width * CellWidth;

				const FString AmpString = FString::Printf(TEXT("%.3f"), LFODebugInfo.Amplitude);
				Canvas.DrawShadowedString(RowX, Y, *AmpString, &Font, FColor::Green);
				RowX += Width * CellWidth;

				const FString FreqString = FString::Printf(TEXT("%.4f"), LFODebugInfo.Frequency);
				Canvas.DrawShadowedString(RowX, Y, *FreqString, &Font, FColor::Green);
				RowX += Width * CellWidth;

				const FString OffsetString = FString::Printf(TEXT("%.4f"), LFODebugInfo.Offset);
				Canvas.DrawShadowedString(RowX, Y, *OffsetString, &Font, FColor::Green);
				RowX += Width * CellWidth;

				Canvas.DrawShadowedString(RowX, Y, *LFODebugInfo.Type, &Font, FColor::Green);
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
					if (const FControlBusMixStageDebugInfo* Stage = BusMix.Stages.Find(Bus.Id))
					{
						Target = Stage->TargetValue;
						Value  = Stage->CurrentValue;
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
					const FColor Color = Debug::GetUnitRenderColor(Value);
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
				const FColor Color = Debug::GetUnitRenderColor(Value);
				Canvas.DrawShadowedString(RowX, Y, *FString::Printf(TEXT("%.4f"), Value), &Font, Color);
			}

			Y += Height;
			Canvas.DrawShadowedString(X, Y, *TotalHeader, &Font, FColor::Yellow);
			RowX = X;
			for (const FControlBusDebugInfo& Bus : FilteredBuses)
			{
				RowX += Width * CellWidth; // Add before to leave space for row headers
				const FColor Color = Debug::GetUnitRenderColor(Bus.Value);
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
	} // namespace Debug

	FAudioModulationDebugger::FAudioModulationDebugger()
		: bActive(0)
		, bShowRenderStatLFO(1)
		, bShowRenderStatMix(1)
		, ElapsedSinceLastUpdate(0.0f)
	{
	}

	void FAudioModulationDebugger::UpdateDebugData(double InElapsed, const FReferencedProxies& InRefProxies)
	{
		if (!bActive)
		{
			ElapsedSinceLastUpdate = 0.0f;
			return;
		}

		ElapsedSinceLastUpdate += InElapsed;
		if (AudioModulationDebugUpdateRateCVar > ElapsedSinceLastUpdate)
		{
			return;
		}
		ElapsedSinceLastUpdate = 0.0f;

		static const int32 MaxFilteredBuses = 8;
		TArray<const FControlBusProxy*> FilteredBusProxies;
		Debug::FilterDebugArray<FBusProxyMap, FControlBusProxy>(InRefProxies.Buses, BusStringFilter, MaxFilteredBuses, FilteredBusProxies);
		TArray<FControlBusDebugInfo> RefreshedFilteredBuses;
		for (const FControlBusProxy* Proxy : FilteredBusProxies)
		{
			FControlBusDebugInfo DebugInfo;
			DebugInfo.DefaultValue = Proxy->GetDefaultValue();
			DebugInfo.Id = Proxy->GetId();
			DebugInfo.LFOValue = Proxy->GetLFOValue();
			DebugInfo.MixValue = Proxy->GetMixValue();
			DebugInfo.Name = Proxy->GetName();
			DebugInfo.RefCount = Proxy->GetRefCount();
			DebugInfo.Value = Proxy->GetValue();
			RefreshedFilteredBuses.Add(DebugInfo);
		}

		static const int32 MaxFilteredMixes = 16;
		TArray<const FModulatorBusMixProxy*> FilteredMixProxies;
		Debug::FilterDebugArray<FBusMixProxyMap, FModulatorBusMixProxy>(InRefProxies.BusMixes, MixStringFilter, MaxFilteredMixes, FilteredMixProxies);
		TArray<FControlBusMixDebugInfo> RefreshedFilteredMixes;
		for (const FModulatorBusMixProxy* Proxy : FilteredMixProxies)
		{
			FControlBusMixDebugInfo DebugInfo;
			DebugInfo.Name = Proxy->GetName();
			DebugInfo.RefCount = Proxy->GetRefCount();
			for (const TPair<FBusId, FModulatorBusMixStageProxy>& Stage : Proxy->Stages)
			{
				FControlBusMixStageDebugInfo StageDebugInfo;
				StageDebugInfo.CurrentValue = Stage.Value.Value.GetCurrentValue();
				StageDebugInfo.TargetValue = Stage.Value.Value.TargetValue;
				DebugInfo.Stages.Add(Stage.Key, StageDebugInfo);
			}
			RefreshedFilteredMixes.Add(DebugInfo);
		}

		static const int32 MaxFilteredLFOs = 8;
		TArray<const FModulatorLFOProxy*> FilteredLFOProxies;
		Debug::FilterDebugArray<FLFOProxyMap, FModulatorLFOProxy>(InRefProxies.LFOs, LFOStringFilter, MaxFilteredLFOs, FilteredLFOProxies);

		TArray<FLFODebugInfo> RefreshedFilteredLFOs;
		for (const FModulatorLFOProxy* Proxy : FilteredLFOProxies)
		{
			FLFODebugInfo DebugInfo;
			DebugInfo.Name = Proxy->GetName();
			DebugInfo.RefCount = Proxy->GetRefCount();
			DebugInfo.Value = Proxy->GetValue();
			DebugInfo.Amplitude = Proxy->GetLFO().GetGain();
			DebugInfo.Frequency = Proxy->GetLFO().GetFrequency();
			DebugInfo.Offset = Proxy->GetOffset();

			switch (Proxy->GetLFO().GetType())
			{
				case Audio::ELFO::DownSaw:
				DebugInfo.Type = TEXT("DownSaw");
				break;

				case Audio::ELFO::Exponential:
				DebugInfo.Type = TEXT("Exponential");
				break;

				case Audio::ELFO::RandomSampleHold:
				DebugInfo.Type = TEXT("Random (Sample & Hold)");
				break;

				case Audio::ELFO::Sine:
				DebugInfo.Type = TEXT("Sine");
				break;

				case Audio::ELFO::Square:
				DebugInfo.Type = TEXT("Square");
				break;

				case Audio::ELFO::Triangle:
				DebugInfo.Type = TEXT("Triangle");
				break;

				case Audio::ELFO::UpSaw:
				DebugInfo.Type = TEXT("Up Saw");
				break;

				default:
				static_assert(static_cast<int32>(Audio::ELFO::NumLFOTypes) == 7, "Missing LFO type case coverage");
				break;
			}

			RefreshedFilteredLFOs.Add(DebugInfo);
		}

		FFunctionGraphTask::CreateAndDispatchWhenReady([this, RefreshedFilteredBuses, RefreshedFilteredMixes, RefreshedFilteredLFOs]()
		{
			FilteredBuses = RefreshedFilteredBuses;
			FilteredBuses.Sort(&Debug::CompareNames<FControlBusDebugInfo>);

			FilteredMixes = RefreshedFilteredMixes;
			FilteredMixes.Sort(&Debug::CompareNames<FControlBusMixDebugInfo>);

			FilteredLFOs = RefreshedFilteredLFOs;
			FilteredLFOs.Sort(&Debug::CompareNames<FLFODebugInfo>);
		}, TStatId(), nullptr, ENamedThreads::GameThread);
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
			Y = Debug::RenderStatMixMatrix(FilteredMixes, FilteredBuses, Canvas, X, Y, Font);
		}

		if (bShowRenderStatLFO)
		{
			Y = Debug::RenderStatLFO(FilteredLFOs, Canvas, X, Y, Font);
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