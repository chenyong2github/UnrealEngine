// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
BurleyNormalizedSSS.cpp: Compute the transmition profile for Burley normalized SSS
=============================================================================*/


#include "Rendering/BurleyNormalizedSSS.h"

// estimated from the sampling interval, 1/TargetBufferSize(1/32) and MaxTransmissionProfileDistance. If any is changed, this parameter should be re-estimated.
const float ProfileRadiusOffset = 0.06;

inline float Burley_ScatteringProfile(float r, float A,float S, float L)
{   //2PIR(r)r
	float D = 1 / S;
	float R = r / L;
	const float Inv8Pi = 1.0 / (8 * PI);
	float NegRbyD = -R / D;
	float RrDotR = A*FMath::Max((exp(NegRbyD) + exp(NegRbyD / 3.0)) / (D*L)*Inv8Pi, 0.0);
	return RrDotR;
}

inline float Burley_TransmissionProfile(float r, float A, float S, float L)
{   
	//integration from t to infty
	return 0.25* A * (exp(-S * r/L) + 3 * exp(-S * r / (3*L)));
}


inline FVector Burley_ScatteringProfile(float r, FLinearColor SurfaceAlbedo, FVector ScalingFactor, FLinearColor DiffuseMeanFreePath)
{  
	return FVector(Burley_ScatteringProfile(r, SurfaceAlbedo.R, ScalingFactor.X, DiffuseMeanFreePath.R) ,
						Burley_ScatteringProfile(r, SurfaceAlbedo.G, ScalingFactor.Y, DiffuseMeanFreePath.G) ,
						Burley_ScatteringProfile(r, SurfaceAlbedo.B, ScalingFactor.Z, DiffuseMeanFreePath.B));
}

inline FLinearColor Burley_TransmissionProfile(float r, FLinearColor SurfaceAlbedo, FVector ScalingFactor, FLinearColor DiffuseMeanFreePath)
{
	return FLinearColor(Burley_TransmissionProfile(r, SurfaceAlbedo.R, ScalingFactor.X, DiffuseMeanFreePath.R),
						Burley_TransmissionProfile(r, SurfaceAlbedo.G, ScalingFactor.Y, DiffuseMeanFreePath.G),
						Burley_TransmissionProfile(r, SurfaceAlbedo.B, ScalingFactor.Z, DiffuseMeanFreePath.B));
}

//--------------------------------------------------------------------------
//Map burley ColorFallOff to Burley SurfaceAlbedo and diffuse mean free path.
void MapFallOffColor2SurfaceAlbedoAndDiffuseMeanFreePath(float FalloffColor, float& SurfaceAlbedo, float& DiffuseMeanFreePath)
{
	//@TODO, use picewise function to separate Falloffcolor to around (0,0.2) and (0.2, 1) to make it more correct
	//map Falloffcolor to SurfaceAlbedo with 4 polynomial, error < 2e-3.
	float X = FalloffColor;
	float X2 = X * X;
	float X4 = X2 * X2;
#if 0
	SurfaceAlbedo = 0.06698*X4 - 0.1405*X2*X + 0.09174*X2 + 0.1278*X - 0.0007324;
	//map FalOffColor to L with 6 polynomial, error < 6e-3  
	DiffuseMeanFreePath = 7.674*X4*X2 - 25.13*X4*X + 31.6*X4 - 18.77*X2*X + 4.954*X2 + 0.2557*X + 0.01;
#else
	// max error happens around 0.1, which is -4.8e-3. The others are less than 2.5e-3.
	SurfaceAlbedo = 5.883*X4*X2 - 19.88*X4*X + 26.08*X4 - 16.59 * X2*X + 5.143*X2 + 0.2636 *X + 0.01098;
	// max error happens around 0.1, which is -3.8e-3.
	DiffuseMeanFreePath = 4.78*X4*X2 - 5.178*X4*X + 5.2154 *X4 - 4.424 * X2*X + 1.636 * X2 + 0.4067 * X + 0.006853;
#endif
}

//-----------------------------------------------------------------
// Functions should be identical on both cpu and gpu
// Method 1: The light directly goes into the volume in a direction perpendicular to the surface.
// Average relative error: 5.5% (reference to MC)
float GetPerpendicularScalingFactor(float SurfaceAlbedo)
{
	return 1.85 - SurfaceAlbedo + 7 * FMath::Pow(SurfaceAlbedo - 0.8, 3);
}

FVector GetPerpendicularScalingFactor(FLinearColor SurfaceAlbedo)
{
	return FVector(GetPerpendicularScalingFactor(SurfaceAlbedo.R),
		GetPerpendicularScalingFactor(SurfaceAlbedo.G),
		GetPerpendicularScalingFactor(SurfaceAlbedo.B)
	);
}

// Method 2: Ideal diffuse transmission at the surface. More appropriate for rough surface.
// Average relative error: 3.9% (reference to MC)
float GetDiffuseSurfaceScalingFactor(float SurfaceAlbedo)
{
	return 1.9 - SurfaceAlbedo + 3.5 * FMath::Pow(SurfaceAlbedo - 0.8, 2);
}

FVector GetDiffuseSurfaceScalingFactor(FLinearColor SurfaceAlbedo)
{
	return FVector(GetDiffuseSurfaceScalingFactor(SurfaceAlbedo.R),
		GetDiffuseSurfaceScalingFactor(SurfaceAlbedo.G),
		GetDiffuseSurfaceScalingFactor(SurfaceAlbedo.B)
	);
}

// Method 3: The spectral of diffuse mean free path on the surface.
// Avergate relative error: 7.7% (reference to MC)
float GetSearchLightDiffuseScalingFactor(float SurfaceAlbedo)
{
	return 3.5 + 100 * FMath::Pow(SurfaceAlbedo - 0.33, 4);
}

FVector GetSearchLightDiffuseScalingFactor(FLinearColor SurfaceAlbedo)
{
	return FVector(GetSearchLightDiffuseScalingFactor(SurfaceAlbedo.R),
		GetSearchLightDiffuseScalingFactor(SurfaceAlbedo.G),
		GetSearchLightDiffuseScalingFactor(SurfaceAlbedo.B)
	);
}

void ComputeMirroredBSSSKernel(FLinearColor* TargetBuffer, uint32 TargetBufferSize,
	FLinearColor SurfaceAlbedo, FLinearColor DiffuseMeanFreePath, float WorldUnitScale, float ScatterRadius)
{
	check(TargetBuffer);
	check(TargetBufferSize > 0);

	uint32 nNonMirroredSamples = TargetBufferSize;
	int32 nTotalSamples = nNonMirroredSamples * 2 - 1;

	FVector ScalingFactor = GetSearchLightDiffuseScalingFactor(SurfaceAlbedo);
	// we could generate Out directly but the original code form SeparableSSS wasn't done like that so we convert it later
	// .a is in mm
	check(nTotalSamples < 64);

	FLinearColor kernel[64];
	{
		const float Range = (nTotalSamples > 20 ? 3.0f : 2.0f);
		// tweak constant
		const float Exponent = 2.0f;

		// Calculate the offsets:
		float step = 2.0f * Range / (nTotalSamples - 1);
		for (int i = 0; i < nTotalSamples; i++)
		{
			float o = -Range + float(i) * step;
			float sign = o < 0.0f ? -1.0f : 1.0f;
			kernel[i].A = Range * sign * FMath::Abs(FMath::Pow(o, Exponent)) / FMath::Pow(Range, Exponent);
		}

		//Scale the profile sampling radius.
		const float SpaceScale = ScatterRadius / WorldUnitScale;

		// Calculate the weights:
		for (int32 i = 0; i < nTotalSamples; i++)
		{
			float w0 = i > 0 ? FMath::Abs(kernel[i].A - kernel[i - 1].A) : 0.0f;
			float w1 = i < nTotalSamples - 1 ? FMath::Abs(kernel[i].A - kernel[i + 1].A) : 0.0f;
			float area = (w0 + w1) / 2.0f;
			FVector t = area * Burley_ScatteringProfile(FMath::Abs(kernel[i].A)*SpaceScale, SurfaceAlbedo, ScalingFactor,DiffuseMeanFreePath);
			kernel[i].R = t.X;
			kernel[i].G = t.Y;
			kernel[i].B = t.Z;
		}

		// We still need to do a small tweak to get the radius to visually match. Multiplying by 4.0 seems to fix it.
		const float StepScale = 4.0f;
		for (int32 i = 0; i < nTotalSamples; i++)
		{
			kernel[i].A *= StepScale;
		}

		// We want the offset 0.0 to come first:
		FLinearColor t = kernel[nTotalSamples / 2];

		for (int i = nTotalSamples / 2; i > 0; i--)
		{
			kernel[i] = kernel[i - 1];
		}
		kernel[0] = t;

		// Normalize the weights in RGB
		{
			FVector sum = FVector(0, 0, 0);

			for (int i = 0; i < nTotalSamples; i++)
			{
				sum.X += kernel[i].R;
				sum.Y += kernel[i].G;
				sum.Z += kernel[i].B;
			}

			for (int i = 0; i < nTotalSamples; i++)
			{
				kernel[i].R /= sum.X;
				kernel[i].G /= sum.Y;
				kernel[i].B /= sum.Z;
			}
		}

		/* we do that in the shader for better quality with half res

		// Tweak them using the desired strength. The first one is:
		//     lerp(1.0, kernel[0].rgb, strength)
		kernel[0].R = FMath::Lerp(1.0f, kernel[0].R, SubsurfaceColor.R);
		kernel[0].G = FMath::Lerp(1.0f, kernel[0].G, SubsurfaceColor.G);
		kernel[0].B = FMath::Lerp(1.0f, kernel[0].B, SubsurfaceColor.B);

		for (int i = 1; i < nTotalSamples; i++)
		{
			kernel[i].R *= SubsurfaceColor.R;
			kernel[i].G *= SubsurfaceColor.G;
			kernel[i].B *= SubsurfaceColor.B;
		}*/
	}

	// generate output (remove negative samples)
	{
		check(kernel[0].A == 0.0f);

		// center sample
		TargetBuffer[0] = kernel[0];

		// all positive samples
		for (uint32 i = 0; i < nNonMirroredSamples - 1; i++)
		{
			TargetBuffer[i + 1] = kernel[nNonMirroredSamples + i];
		}
	}
}



void ComputeTransmissionProfileBurley(FLinearColor* TargetBuffer, uint32 TargetBufferSize, FLinearColor SubsurfaceColor, 
																		FLinearColor FalloffColor, float ExtinctionScale, 
																		FLinearColor SurfaceAlbedo, FLinearColor DiffuseMeanFreePath,
																		float WorldUnitScale, FLinearColor TransmissionTintColor)
{
	check(TargetBuffer);
	check(TargetBufferSize > 0);

	static float MaxTransmissionProfileDistance = 5.0f; // See MAX_TRANSMISSION_PROFILE_DISTANCE in TransmissionCommon.ush

	//assuming that the volume albedo is the same to the surface albedo for transmission.
	FVector ScalingFactor = GetSearchLightDiffuseScalingFactor(SurfaceAlbedo);

	const float InvSize = 1.0f / TargetBufferSize;

	for (uint32 i = 0; i < TargetBufferSize; ++i)
	{

		float Distance = i * InvSize * MaxTransmissionProfileDistance/WorldUnitScale;

		FLinearColor TransmissionProfile = Burley_TransmissionProfile(Distance + ProfileRadiusOffset / WorldUnitScale, SurfaceAlbedo,ScalingFactor, DiffuseMeanFreePath);
		TargetBuffer[i] = TransmissionProfile * TransmissionTintColor; // Apply tint to the profile
		//Use Luminance of scattering as SSSS shadow.
		TargetBuffer[i].A = exp(-Distance * ExtinctionScale);
	}

	// Do this is because 5mm is not enough cool down the scattering to zero, although which is small number but after tone mapping still noticeable
	// so just Let last pixel be 0 which make sure thickness great than MaxRadius have no scattering
	static bool bMakeLastPixelBlack = true;
	if (bMakeLastPixelBlack)
	{
		TargetBuffer[TargetBufferSize - 1] = FLinearColor::Black;
	}
}
