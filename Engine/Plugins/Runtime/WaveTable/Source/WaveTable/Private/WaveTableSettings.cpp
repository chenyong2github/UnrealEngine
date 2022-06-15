// Copyright Epic Games, Inc. All Rights Reserved.
#include "WaveTableSettings.h"


namespace WaveTable
{
	int32 ResolutionToInt32(EWaveTableResolution InResolution)
	{
		if (InResolution == EWaveTableResolution::None)
		{
			return 0;
		}

		return 1 << static_cast<int32>(InResolution);
	}

	int32 ResolutionToInt32(EWaveTableCurve InCurve, EWaveTableResolution FileResolution)
	{
		EWaveTableResolution Resolution = EWaveTableResolution::None;
		switch (InCurve)
		{
			// File uses the incoming resolution
			case EWaveTableCurve::File:
			{
				Resolution = FileResolution;
			}
			break;

			case EWaveTableCurve::Linear:
			{
				Resolution = EWaveTableResolution::Res_16;
			}
			break;

			case EWaveTableCurve::Exp:
			case EWaveTableCurve::Exp_Inverse:
			case EWaveTableCurve::Log:
			{
				Resolution = EWaveTableResolution::Res_256;
			}
			break;

			case EWaveTableCurve::Sin:
			case EWaveTableCurve::SCurve:
			{
				Resolution = EWaveTableResolution::Res_64;
			}
			break;


			default:
			{
				Resolution = EWaveTableResolution::Res_128;
			}
			break;
		}

		return ResolutionToInt32(Resolution);
	}
} // namespace WaveTable
