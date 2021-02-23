// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraFlipbookSettings.h"

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

bool FNiagaraFlipbookTextureSettings::Equals(const FNiagaraFlipbookTextureSettings& Other) const
{
	return
		OutputName == Other.OutputName &&
		SourceBinding.SourceName == Other.SourceBinding.SourceName &&
		bUseFrameSize == Other.bUseFrameSize &&
		FrameSize == Other.FrameSize &&
		TextureSize == Other.TextureSize;
}

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
	CameraViewportRotation[(int)ENiagaraFlipbookViewMode::Perspective] = FRotator(180.0f, 0.0f, 90.0f);
}

bool UNiagaraFlipbookSettings::Equals(const UNiagaraFlipbookSettings& Other) const
{
	if (OutputTextures.Num() != Other.OutputTextures.Num())
	{
		return false;
	}

	for ( int i=0; i < OutputTextures.Num(); ++i )
	{
		if ( !OutputTextures[i].Equals(Other.OutputTextures[i]) )
		{
			return false;
		}
	}

	for ( int i=0; i < (int)ENiagaraFlipbookViewMode::Num; ++i )
	{
		if ( !CameraViewportLocation[i].Equals(Other.CameraViewportLocation[i]) ||
			 !CameraViewportRotation[i].Equals(Other.CameraViewportRotation[i]) )
		{
			return false;
		}
	}

	return
		FMath::IsNearlyEqual(StartSeconds, Other.StartSeconds) &&
		FMath::IsNearlyEqual(DurationSeconds, Other.DurationSeconds) &&
		FramesPerSecond == Other.FramesPerSecond &&
		bPreviewLooping == Other.bPreviewLooping &&
		FramesPerDimension == Other.FramesPerDimension &&
		CameraViewportMode == Other.CameraViewportMode &&
		FMath::IsNearlyEqual(CameraOrbitDistance, Other.CameraOrbitDistance) &&
		FMath::IsNearlyEqual(CameraFOV, Other.CameraFOV) &&
		FMath::IsNearlyEqual(CameraOrthoWidth, Other.CameraOrthoWidth) &&
		bUseCameraAspectRatio == Other.bUseCameraAspectRatio &&
		FMath::IsNearlyEqual(CameraAspectRatio, Other.CameraAspectRatio) &&
		bRenderComponentOnly == Other.bRenderComponentOnly;
}

float UNiagaraFlipbookSettings::GetAspectRatio(int32 iOutputTextureIndex) const
{
	if (OutputTextures.IsValidIndex(iOutputTextureIndex))
	{
		const float TextureAspectRatio = float(OutputTextures[iOutputTextureIndex].FrameSize.Y) / float(OutputTextures[iOutputTextureIndex].FrameSize.X);
		return bUseCameraAspectRatio ? CameraAspectRatio : TextureAspectRatio;
	}
	return 1.0f;
}

FVector2D UNiagaraFlipbookSettings::GetOrthoSize(int32 iOutputTextureIndex) const
{
	return FVector2D(CameraOrthoWidth, CameraOrthoWidth * GetAspectRatio(iOutputTextureIndex));
}

FVector UNiagaraFlipbookSettings::GetCameraLocation() const
{
	if (IsPerspective())
	{
		const FVector OrbitOffset = CameraViewportRotation[(int)CameraViewportMode].RotateVector(FVector(0.0f, 0.0f, CameraOrbitDistance));
		return CameraViewportLocation[(int)CameraViewportMode] - OrbitOffset;
	}
	else
	{
		return CameraViewportLocation[(int)CameraViewportMode];
	}
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
		const float AspectRatio = GetAspectRatio(iOutputTextureIndex);
		const float HalfXFOV = FMath::DegreesToRadians(CameraFOV) * 0.5f;
		const float HalfYFOV = FMath::Atan(FMath::Tan(HalfXFOV) / AspectRatio);
		return FReversedZPerspectiveMatrix(HalfXFOV, HalfYFOV, 1.0f, 1.0, GNearClippingPlane, GNearClippingPlane);
	}
	else
	{
		const float ZRange = WORLD_MAX;
		const FVector2D OrthoSize = GetOrthoSize(iOutputTextureIndex);
		return FReversedZOrthoMatrix(OrthoSize.X / 2.0f, OrthoSize.Y / 2.0f, 0.5f / ZRange, ZRange);
	}
}

UNiagaraFlipbookSettings::FDisplayInfo UNiagaraFlipbookSettings::GetDisplayInfo(float Time, bool bLooping) const
{
	FDisplayInfo DisplayInfo;
	DisplayInfo.NormalizedTime = FMath::Max(Time / DurationSeconds, 0.0f);
	DisplayInfo.NormalizedTime = bLooping ? FMath::Fractional(DisplayInfo.NormalizedTime) : FMath::Min(DisplayInfo.NormalizedTime, 0.9999f);

	const int NumFrames = GetNumFrames();
	const float FrameTime = DisplayInfo.NormalizedTime * float(NumFrames);
	DisplayInfo.FrameIndexA = FMath::FloorToInt(FrameTime);
	DisplayInfo.FrameIndexB = bLooping ? (DisplayInfo.FrameIndexA + 1) % NumFrames : FMath::Min(DisplayInfo.FrameIndexA + 1, NumFrames - 1);
	DisplayInfo.Interp = FrameTime - float(DisplayInfo.FrameIndexA);

	return DisplayInfo;
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
