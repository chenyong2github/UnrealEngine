// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeTexturePatchBase.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Landscape.h"

void ULandscapeTexturePatchBase::SetTextureAsset(UTexture* TextureIn)
{
	ensureMsgf(!TextureIn || TextureIn->VirtualTextureStreaming == 0, 
		TEXT("ULandscapeTexturePatchBase::SetTextureAsset: Virtual textures are not supported."));
	TextureAsset = TextureIn; 
}

FTransform ULandscapeTexturePatchBase::GetPatchToWorldTransform() const
{
	FTransform PatchToWorld = GetComponentTransform();

	if (Landscape.IsValid())
	{
		FRotator3d PatchRotator = PatchToWorld.GetRotation().Rotator();
		FRotator3d LandscapeRotator = Landscape->GetTransform().GetRotation().Rotator();
		PatchToWorld.SetRotation(FRotator3d(LandscapeRotator.Pitch, PatchRotator.Yaw, LandscapeRotator.Roll).Quaternion());
	}

	return PatchToWorld;
}

bool ULandscapeTexturePatchBase::GetTextureResolution(FVector2D& SizeOut) const
{
	switch (SourceMode)
	{
	case ELandscapeTexturePatchSourceMode::None:
		return false;
	case ELandscapeTexturePatchSourceMode::InternalTexture:
		if (IsValid(InternalTexture))
		{
			// Apparently direct GetSizeX/Y calls can return a default texture size in some cases
			// while the texture is compiling, hence us going through the resource here.
			FTextureResource* TextureResource = InternalTexture->GetResource();
			if (ensure(TextureResource))
			{
				SizeOut = FVector2D(TextureResource->GetSizeX(), TextureResource->GetSizeY());
			}
			else
			{
				SizeOut = FVector2D(InternalTexture->GetSizeX(), InternalTexture->GetSizeY());
			}
			return true;
		}
		break;
	case ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget:
		if (IsValid(InternalRenderTarget))
		{
			SizeOut = FVector2D(InternalRenderTarget->SizeX, InternalRenderTarget->SizeY);
			return true;
		}
		break;
	case ELandscapeTexturePatchSourceMode::TextureAsset:
		if (IsValid(TextureAsset))
		{
			FTextureResource* TextureResource = TextureAsset->GetResource();
			if (ensure(TextureResource))
			{
				SizeOut = FVector2D(TextureResource->GetSizeX(), TextureResource->GetSizeY());
			}
			else
			{
				SizeOut = FVector2D(TextureAsset->GetResource()->GetSizeX(), TextureAsset->GetResource()->GetSizeY());
			}
			return true;
		}
		break;
	default:
		ensure(false);
		break;
	}

	return false;
}

FVector2D ULandscapeTexturePatchBase::GetFullUnscaledWorldSize() const
{
	FVector2D Resolution;

	if (!GetTextureResolution(Resolution))
	{
		return UnscaledPatchCoverage;
	}

	return GetFullUnscaledWorldSizeForResolution(Resolution);
}

FVector2D ULandscapeTexturePatchBase::GetFullUnscaledWorldSizeForResolution(const FVector2D& ResolutionIn) const
{
	// UnscaledPatchCoverage is meant to represent the distance between the centers of the extremal pixels.
	// That distance in pixels is TextureSize-1.
	FVector2D TargetPixelSize(UnscaledPatchCoverage / FMath::Max(ResolutionIn - 1, FVector2D(1, 1)));
	return TargetPixelSize * ResolutionIn;
}

bool ULandscapeTexturePatchBase::GetInitResolutionFromLandscape(float ResolutionMultiplier, FVector2D& ResolutionOut) const
{
	if (!Landscape.IsValid())
	{
		return false;
	}

	ResolutionOut = FVector2D::One();

	FVector LandscapeScale = Landscape->GetTransform().GetScale3D();
	// We go off of the larger dimension so that our patch works in different rotations.
	double LandscapeQuadSize = FMath::Max(FMath::Abs(LandscapeScale.X), FMath::Abs(LandscapeScale.Y));

	if (LandscapeQuadSize > 0)
	{
		double PatchQuadSize = LandscapeQuadSize;
		PatchQuadSize /= (ResolutionMultiplier > 0 ? ResolutionMultiplier : 1);

		FVector PatchScale = GetComponentTransform().GetScale3D();
		double NumQuadsX = FMath::Abs(UnscaledPatchCoverage.X * PatchScale.X / PatchQuadSize);
		double NumQuadsY = FMath::Abs(UnscaledPatchCoverage.Y * PatchScale.Y / PatchQuadSize);

		ResolutionOut = FVector2D(
			FMath::Max(1, FMath::CeilToInt(NumQuadsX) + 1),
			FMath::Max(1, FMath::CeilToInt(NumQuadsY) + 1)
		);

		return true;
	}
	return false;
}
