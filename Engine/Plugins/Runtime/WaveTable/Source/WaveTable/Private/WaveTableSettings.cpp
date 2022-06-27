// Copyright Epic Games, Inc. All Rights Reserved.
#include "WaveTableSettings.h"


namespace WaveTable
{
	int32 ResolutionToInt32(EWaveTableResolution InWaveTableResolution)
	{
		if (InWaveTableResolution == EWaveTableResolution::None)
		{
			return 0;
		}

		return 1 << static_cast<int32>(InWaveTableResolution);
	}

	int32 ResolutionToInt32(EWaveTableResolution InWaveTableResolution, EWaveTableCurve InCurve)
	{
		if (InWaveTableResolution != EWaveTableResolution::None)
		{
			return ResolutionToInt32(InWaveTableResolution);
		}

		switch (InCurve)
		{
			// File always uses the incoming resolution, even if 'None'
			case EWaveTableCurve::File:
			break;

			case EWaveTableCurve::Linear:
			case EWaveTableCurve::Linear_Inv:
			{
				InWaveTableResolution = EWaveTableResolution::Res_8;
			}
			break;

			case EWaveTableCurve::Exp:
			case EWaveTableCurve::Exp_Inverse:
			case EWaveTableCurve::Log:
			{
				InWaveTableResolution = EWaveTableResolution::Res_256;
			}
			break;

			case EWaveTableCurve::Sin:
			case EWaveTableCurve::SCurve:
			case EWaveTableCurve::Sin_Full:
			{
				InWaveTableResolution = EWaveTableResolution::Res_64;
			}
			break;


			default:
			{
				InWaveTableResolution = EWaveTableResolution::Res_128;
			}
			break;
		}

		return ResolutionToInt32(InWaveTableResolution);
	}
} // namespace WaveTable
