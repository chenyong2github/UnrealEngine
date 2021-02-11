// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraFlipbookSettings.h"

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

UNiagaraFlipbookSettings::UNiagaraFlipbookSettings(const FObjectInitializer& Init)
	: Super(Init)
{
	bRenderComponentOnly = true;
	OutputTextures.AddDefaulted();

	for ( int i=0; i < (int)ENiagaraFlipbookViewMode::Num; ++i )
	{
		CameraViewportLocation[i] = FVector::ZeroVector;
		CameraViewportRotation[i] = FRotator::ZeroRotator;
	}
	CameraViewportLocation[(int)ENiagaraFlipbookViewMode::Perspective] = FVector(0.0f, -200.0f, 0.0f);
	CameraViewportRotation[(int)ENiagaraFlipbookViewMode::Perspective] = FRotator(0.0f, 0.0f, 90.0f);
}

FVector UNiagaraFlipbookSettings::GetCameraLocation() const
{
	return CameraViewportLocation[(int)CameraViewportMode];
}

FMatrix UNiagaraFlipbookSettings::GetViewMatrix() const
{
	FMatrix ViewportMatrix;
	switch (CameraViewportMode)
	{
		case ENiagaraFlipbookViewMode::OrthoFront:	ViewportMatrix = FMatrix(-FVector::ZAxisVector, -FVector::XAxisVector,  FVector::YAxisVector, FVector::ZeroVector); break;
		case ENiagaraFlipbookViewMode::OrthoBack:	ViewportMatrix = FMatrix( FVector::ZAxisVector,  FVector::XAxisVector,  FVector::YAxisVector, FVector::ZeroVector); break;
		case ENiagaraFlipbookViewMode::OrthoLeft:	ViewportMatrix = FMatrix(-FVector::XAxisVector,  FVector::ZAxisVector,  FVector::YAxisVector, FVector::ZeroVector); break;
		case ENiagaraFlipbookViewMode::OrthoRight:	ViewportMatrix = FMatrix( FVector::XAxisVector, -FVector::ZAxisVector,  FVector::YAxisVector, FVector::ZeroVector); break;
		case ENiagaraFlipbookViewMode::OrthoTop:	ViewportMatrix = FMatrix( FVector::XAxisVector, -FVector::YAxisVector, -FVector::ZAxisVector, FVector::ZeroVector); break;
		case ENiagaraFlipbookViewMode::OrthoBottom:	ViewportMatrix = FMatrix(-FVector::XAxisVector, -FVector::YAxisVector,  FVector::ZAxisVector, FVector::ZeroVector); break;

		default: ViewportMatrix = FMatrix::Identity; break;
	}

	return FInverseRotationMatrix(CameraViewportRotation[(int)CameraViewportMode]) * ViewportMatrix;
}

FMatrix UNiagaraFlipbookSettings::GetProjectionMatrixForTexture(int32 iOutputTextureIndex) const
{
	if (CameraViewportMode == ENiagaraFlipbookViewMode::Perspective)
	{
		return FReversedZPerspectiveMatrix(CameraFOV, OutputTextures[iOutputTextureIndex].FrameSize.X, OutputTextures[iOutputTextureIndex].FrameSize.Y, 1.0f);
	}
	else
	{
		const float ZRange = WORLD_MAX;
		return FReversedZOrthoMatrix(CameraOrthoSize.X / 2.0f, CameraOrthoSize.Y / 2.0f, 0.5f / ZRange, ZRange);
	}
}

void UNiagaraFlipbookSettings::PostLoad()
{
	Super::PostLoad();

	for (FNiagaraFlipbookTextureSettings& Texture : OutputTextures)
	{
		if ( Texture.GeneratedTexture )
		{
			Texture.GeneratedTexture->PostLoad();
		}
	}
}

#if WITH_EDITORONLY_DATA
void UNiagaraFlipbookSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	bool bComputeOutputTextureSizes = false;
	if ( PropertyChangedEvent.MemberProperty != nullptr )
	{
		//if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FNiagaraFlipbookTextureSettings))
		if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraFlipbookSettings, OutputTextures))
		{
			bComputeOutputTextureSizes = true;
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraFlipbookSettings, FramesPerDimension))
		{
			FramesPerDimension.X = FMath::Max(FramesPerDimension.X, 1);
			FramesPerDimension.Y = FMath::Max(FramesPerDimension.Y, 1);
			bComputeOutputTextureSizes = true;
		}
	}

	// Recompute output texture sizes as something was modified which could impact it
	if (bComputeOutputTextureSizes)
	{
		for (FNiagaraFlipbookTextureSettings& OutputTexture : OutputTextures)
		{
			if (OutputTexture.bUseFrameSize)
			{
				OutputTexture.FrameSize.X = FMath::Max(OutputTexture.FrameSize.X, 1);
				OutputTexture.FrameSize.Y = FMath::Max(OutputTexture.FrameSize.Y, 1);
				OutputTexture.TextureSize.X = OutputTexture.FrameSize.X * FramesPerDimension.X;
				OutputTexture.TextureSize.Y = OutputTexture.FrameSize.Y * FramesPerDimension.Y;
			}
			else
			{
				OutputTexture.TextureSize.X = FMath::Max(OutputTexture.TextureSize.X, 1);
				OutputTexture.TextureSize.Y = FMath::Max(OutputTexture.TextureSize.Y, 1);
				OutputTexture.FrameSize.X = FMath::DivideAndRoundDown(OutputTexture.TextureSize.X, FramesPerDimension.X);
				OutputTexture.FrameSize.Y = FMath::DivideAndRoundDown(OutputTexture.TextureSize.Y, FramesPerDimension.Y);
			}
		}
	}
}
#endif
