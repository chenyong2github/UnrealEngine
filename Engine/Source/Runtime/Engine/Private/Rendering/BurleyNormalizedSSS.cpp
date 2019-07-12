// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
BurleyNormalizedSSS.cpp: Compute the transmition profile for Burley normalized SSS
=============================================================================*/


#include "Rendering/BurleyNormalizedSSS.h"

inline float Burley_Profile(float r, float A,float S, float L)
{   //2PIR(r)r
	float D = 1 / S;
	float R = r / L;
	const float Inv8Pi = 1.0 / (8 * PI);
	float NegRbyD = -R / D;
	float RrDotR = A*FMath::Max((exp(NegRbyD) + exp(NegRbyD / 3.0)) / (D*L)*Inv8Pi, 0.0);
	return 2 * PI* RrDotR;
}

inline FVector Burley_Profile(float r, FLinearColor SurfaceAlbedo, FVector ScalingFactor, FLinearColor DiffuseMeanFreePath)
{  
	return FVector( Burley_Profile(r, SurfaceAlbedo.R, ScalingFactor.X, DiffuseMeanFreePath.R) ,
					Burley_Profile(r, SurfaceAlbedo.G, ScalingFactor.Y, DiffuseMeanFreePath.G) ,
					Burley_Profile(r, SurfaceAlbedo.B, ScalingFactor.Z, DiffuseMeanFreePath.B));
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
	SurfaceAlbedo = 0.06698*X4 - 0.1405*X2*X + 0.09174*X2 + 0.1278*X - 0.0007324;

	//map FalOffColor to L with 6 polynomial, error < 6e-3  
	DiffuseMeanFreePath = 7.674*X4*X2 - 25.13*X4*X + 31.6*X4 - 18.77*X2*X + 4.954*X2 + 0.2557*X + 0.01;
}

//map SurfaceAlbedo and  DiffuseMeanFreePath to Falloff, Actually, we only need to use SurfaceAlbedo, as Diffuse mean free path 
//is coupled with SurfaceAlbedo. @TODO: map arbitrary SurfaceAlbedo and DiffuseMeanFreePath to FallOffColor and lerp factor.
float MapAlbedoAndDiffuseMeanFreePath2FallOffColor(float SurfaceAlbedo, float&FallOffColor)
{
	float X = FallOffColor;
	float X2 = X * X;
	float X4 = X2 * X2;
	//split 0.04,
	if (SurfaceAlbedo < 0.04)
	{
		//6 polynomial, error <3e-3
		return 4.261e+09*X4*X2 + -4.932e+08*X4*X + 2.195e+07*X4 - 4.624e+05*X2*X + 4448 * X2 - 7.119*X + 0.00699;
	}
	else
	{
		//error <1e-3
		return 0.8498*X2 + 6.738*X + 0.007304;
	}
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

void ComputeTransmissionProfileBurley(FLinearColor* TargetBuffer, uint32 TargetBufferSize, FLinearColor SubsurfaceColor, 
																		FLinearColor FalloffColor, float ExtinctionScale, 
																		FLinearColor SurfaceAlbedo, FLinearColor DiffuseMeanFreePath)
{
	check(TargetBuffer);
	check(TargetBufferSize > 0);

	static float MaxTransmissionProfileDistance = 5.0f; // See MAX_TRANSMISSION_PROFILE_DISTANCE in TransmissionCommon.ush
	FVector ScalingFactor = GetSearchLightDiffuseScalingFactor(SurfaceAlbedo);
	for (uint32 i = 0; i < TargetBufferSize; ++i)
	{
		//10 mm
		const float InvSize = 1.0f / TargetBufferSize;
		float Distance = i * InvSize * MaxTransmissionProfileDistance;

		FVector TransmissionProfile = Burley_Profile(Distance, SurfaceAlbedo,ScalingFactor, DiffuseMeanFreePath);
		TargetBuffer[i] = TransmissionProfile;
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
