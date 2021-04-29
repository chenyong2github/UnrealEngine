// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraBakerSettings.h"

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

bool FNiagaraBakerTextureSettings::Equals(const FNiagaraBakerTextureSettings& Other) const
{
	return
		OutputName == Other.OutputName &&
		SourceBinding.SourceName == Other.SourceBinding.SourceName &&
		bUseFrameSize == Other.bUseFrameSize &&
		FrameSize == Other.FrameSize &&
		TextureSize == Other.TextureSize;
}

UNiagaraBakerSettings::UNiagaraBakerSettings(const FObjectInitializer& Init)
	: Super(Init)
{
	bRenderComponentOnly = true;
	OutputTextures.AddDefaulted();

	for ( int i=0; i < (int)ENiagaraBakerViewMode::Num; ++i )
	{
		CameraViewportLocation[i] = FVector::ZeroVector;
		CameraViewportRotation[i] = FRotator::ZeroRotator;
	}
	CameraViewportLocation[(int)ENiagaraBakerViewMode::Perspective] = FVector(0.0f, -200.0f, 0.0f);
	CameraViewportRotation[(int)ENiagaraBakerViewMode::Perspective] = FRotator(180.0f, 0.0f, 90.0f);
}

bool UNiagaraBakerSettings::Equals(const UNiagaraBakerSettings& Other) const
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

	for ( int i=0; i < (int)ENiagaraBakerViewMode::Num; ++i )
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

float UNiagaraBakerSettings::GetAspectRatio(int32 iOutputTextureIndex) const
{
	if (OutputTextures.IsValidIndex(iOutputTextureIndex))
	{
		const float TextureAspectRatio = float(OutputTextures[iOutputTextureIndex].FrameSize.Y) / float(OutputTextures[iOutputTextureIndex].FrameSize.X);
		return bUseCameraAspectRatio ? CameraAspectRatio : TextureAspectRatio;
	}
	return 1.0f;
}

FVector2D UNiagaraBakerSettings::GetOrthoSize(int32 iOutputTextureIndex) const
{
	return FVector2D(CameraOrthoWidth, CameraOrthoWidth * GetAspectRatio(iOutputTextureIndex));
}

FVector UNiagaraBakerSettings::GetCameraLocation() const
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

FMatrix UNiagaraBakerSettings::GetViewMatrix() const
{
	FMatrix ViewportMatrix;
	switch (CameraViewportMode)
	{
		case ENiagaraBakerViewMode::OrthoFront:	ViewportMatrix = FMatrix(-FVector::ZAxisVector, -FVector::XAxisVector,  FVector::YAxisVector, FVector::ZeroVector); break;
		case ENiagaraBakerViewMode::OrthoBack:	ViewportMatrix = FMatrix( FVector::ZAxisVector,  FVector::XAxisVector,  FVector::YAxisVector, FVector::ZeroVector); break;
		case ENiagaraBakerViewMode::OrthoLeft:	ViewportMatrix = FMatrix(-FVector::XAxisVector,  FVector::ZAxisVector,  FVector::YAxisVector, FVector::ZeroVector); break;
		case ENiagaraBakerViewMode::OrthoRight:	ViewportMatrix = FMatrix( FVector::XAxisVector, -FVector::ZAxisVector,  FVector::YAxisVector, FVector::ZeroVector); break;
		case ENiagaraBakerViewMode::OrthoTop:	ViewportMatrix = FMatrix( FVector::XAxisVector, -FVector::YAxisVector, -FVector::ZAxisVector, FVector::ZeroVector); break;
		case ENiagaraBakerViewMode::OrthoBottom:	ViewportMatrix = FMatrix(-FVector::XAxisVector, -FVector::YAxisVector,  FVector::ZAxisVector, FVector::ZeroVector); break;

		default: ViewportMatrix = FMatrix::Identity; break;
	}

	return FInverseRotationMatrix(CameraViewportRotation[(int)CameraViewportMode]) * ViewportMatrix;
}

FMatrix UNiagaraBakerSettings::GetProjectionMatrixForTexture(int32 iOutputTextureIndex) const
{
	if (CameraViewportMode == ENiagaraBakerViewMode::Perspective)
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

UNiagaraBakerSettings::FDisplayInfo UNiagaraBakerSettings::GetDisplayInfo(float Time, bool bLooping) const
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

void UNiagaraBakerSettings::PostLoad()
{
	Super::PostLoad();

	for (FNiagaraBakerTextureSettings& Texture : OutputTextures)
	{
		if ( Texture.GeneratedTexture )
		{
			Texture.GeneratedTexture->PostLoad();
		}
	}
}

#if WITH_EDITORONLY_DATA
void UNiagaraBakerSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	bool bComputeOutputTextureSizes = false;
	if ( PropertyChangedEvent.MemberProperty != nullptr )
	{
		//if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FNiagaraBakerTextureSettings))
		if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, OutputTextures))
		{
			bComputeOutputTextureSizes = true;
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, FramesPerDimension))
		{
			FramesPerDimension.X = FMath::Max(FramesPerDimension.X, 1);
			FramesPerDimension.Y = FMath::Max(FramesPerDimension.Y, 1);
			bComputeOutputTextureSizes = true;
		}
	}

	// Recompute output texture sizes as something was modified which could impact it
	if (bComputeOutputTextureSizes)
	{
		for (FNiagaraBakerTextureSettings& OutputTexture : OutputTextures)
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
