// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGTextureData.h"

#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGAsync.h"

namespace PCGTextureSampling
{
	template<typename ValueType>
	ValueType Sample(const FVector2D& InPosition, const FBox2D& InSurface, const UPCGBaseTextureData* InTextureData, int32 Width, int32 Height, TFunctionRef<ValueType(int32 Index)> SamplingFunction)
	{
		check(Width > 0 && Height > 0);
		if (Width <= 0 || Height <= 0)
		{
			return ValueType{};
		}

		check(InTextureData);

		FVector2D Pos = FVector2D::ZeroVector;
		if (InTextureData->bStretchToFit)
		{
			// TODO: There seems to be a bias issue here, as the bounds size are not in the same space as the texels.
			Pos = (InPosition - InSurface.Min) * FVector2D(Width, Height) / InSurface.GetSize();
		}
		else
		{
			const float TextureSizeX = InTextureData->TexelSize * Width;
			const float TextureSizeY = InTextureData->TexelSize * Height;

			const FRotator Rotation = FRotator(0.f, -InTextureData->Rotation, 0.f);
			const FVector Translation = FVector(-InTextureData->XOffset * TextureSizeX, -InTextureData->YOffset * TextureSizeY, 0.f);
			const FTransform Transform(Rotation, Translation);
			const FVector2D SamplePosition = FVector2D(Transform.TransformPosition(FVector(InPosition, 0.f)));

			float X = FMath::Clamp(static_cast<float>(FMath::Fmod(SamplePosition.X, TextureSizeX)) / TextureSizeX, -1.f, 1.f);
			if (X < 0.f)
			{
				X = 1.f + X;
			}

			float Y = FMath::Clamp(static_cast<float>(FMath::Fmod(SamplePosition.Y, TextureSizeY)) / TextureSizeY, -1.f, 1.f);
			if (Y < 0.f)
			{
				Y = 1.f + Y;
			}

			X *= Width;
			Y *= Height;

			Pos = FVector2D(X, Y);
		}

		// TODO: this isn't super robust, if that becomes an issue
		int32 X0 = FMath::FloorToInt(Pos.X);
		if (X0 < 0 || X0 >= Width)
		{
			X0 = 0;
		}

		int32 X1 = FMath::CeilToInt(Pos.X);
		if (X1 < 0 || X1 >= Width)
		{
			X1 = 0;
		}

		int32 Y0 = FMath::FloorToInt(Pos.Y);
		if (Y0 < 0 || Y0 >= Height)
		{
			Y0 = 0;
		}

		int32 Y1 = FMath::CeilToInt(Pos.Y);
		if (Y1 < 0 || Y1 >= Height)
		{
			Y1 = 0;
		}

		ValueType SampleX0Y0 = SamplingFunction(X0 + Y0 * Width);
		ValueType SampleX1Y0 = SamplingFunction(X1 + Y0 * Width);
		ValueType SampleX0Y1 = SamplingFunction(X0 + Y1 * Width);
		ValueType SampleX1Y1 = SamplingFunction(X1 + Y1 * Width);

		ValueType BilinearInterpolation = FMath::BiLerp(SampleX0Y0, SampleX1Y0, SampleX0Y1, SampleX1Y1, Pos.X - X0, Pos.Y - Y0);
		return BilinearInterpolation;
	}

	float SampleFloatChannel(const FLinearColor& InColor, EPCGTextureColorChannel ColorChannel)
	{
		switch (ColorChannel)
		{
		case EPCGTextureColorChannel::Red:
			return InColor.R;
		case EPCGTextureColorChannel::Green:
			return InColor.G;
		case EPCGTextureColorChannel::Blue:
			return InColor.B;
		case EPCGTextureColorChannel::Alpha:
		default:
			return InColor.A;
		}
	}
}

FBox UPCGBaseTextureData::GetBounds() const
{
	return Bounds;
}

FBox UPCGBaseTextureData::GetStrictBounds() const
{
	return Bounds;
}

bool UPCGBaseTextureData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	// TODO: add metadata support
	// TODO: add sampling along the bounds
	if (!IsValid())
	{
		return false;
	}

	// Compute transform
	// TODO: embed local bounds center offset at this time?
	OutPoint.Transform = InTransform;
	FVector PointPositionInLocalSpace = Transform.InverseTransformPosition(InTransform.GetLocation());
	PointPositionInLocalSpace.Z = 0;
	OutPoint.Transform.SetLocation(Transform.TransformPosition(PointPositionInLocalSpace));
	OutPoint.SetLocalBounds(InBounds); // TODO: should set Min.Z = Max.Z = 0;

	// Compute density & color (& metadata)
	// TODO: sample in the bounds given, not only on a single pixel
	FVector2D Position2D(PointPositionInLocalSpace.X, PointPositionInLocalSpace.Y);
	FBox2D Surface(FVector2D(-1.0f, -1.0f), FVector2D(1.0f, 1.0f));

	FLinearColor Color = PCGTextureSampling::Sample<FLinearColor>(Position2D, Surface, this, Width, Height, [this](int32 Index) { return ColorData[Index]; });
	OutPoint.Color = Color;
	OutPoint.Density = ((DensityFunction == EPCGTextureDensityFunction::Ignore) ? 1.0f : PCGTextureSampling::SampleFloatChannel(Color, ColorChannel));

	return OutPoint.Density > 0;
}

const UPCGPointData* UPCGBaseTextureData::CreatePointData(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGBaseTextureData::CreatePointData);
	// TODO: this is a trivial implementation
	// A better sampler would allow to sample a fixed number of points in either direction
	// or based on a given texel size
	FBox2D LocalSurfaceBounds(FVector2D(-1.0f, -1.0f), FVector2D(1.0f, 1.0f));

	UPCGPointData* Data = NewObject<UPCGPointData>(const_cast<UPCGBaseTextureData*>(this));
	Data->InitializeFromData(this);

	// Early out for invalid data
	if (!IsValid())
	{
		UE_LOG(LogPCG, Error, TEXT("Texture data does not have valid sizes - will return empty data"));
		return Data;
	}

	TArray<FPCGPoint>& Points = Data->GetMutablePoints();

	// TODO: There's a bias issue here where we should correct by a 0.5 unit...
	const FVector::FReal XScale = LocalSurfaceBounds.GetSize().X / Width;
	const FVector::FReal YScale = LocalSurfaceBounds.GetSize().Y / Height;
	const FVector2D Bias = LocalSurfaceBounds.Min;

	FPCGAsync::AsyncPointProcessing(Context, Width * Height, Points, [this, XScale, YScale, Bias](int32 Index, FPCGPoint& OutPoint)
	{
		const int X = Index % Width;
		const int Y = Index / Width;

		const float Density = PCGTextureSampling::SampleFloatChannel(ColorData[X + Y * Width], ColorChannel);

#if WITH_EDITORONLY_DATA
		if (Density > 0 || bKeepZeroDensityPoints)
#else
		if (Density > 0)
#endif
		{
			FVector LocalPosition(X * XScale + Bias.X, Y * YScale + Bias.Y, 0);
			OutPoint = FPCGPoint(FTransform(Transform.TransformPosition(LocalPosition)),
				Density,
				PCGHelpers::ComputeSeed(X, Y));

			const FVector TransformScale = Transform.GetScale3D();
			OutPoint.SetExtents(FVector(TransformScale.X * XScale / 2.0, TransformScale.Y * YScale / 2.0, 1.0));
			OutPoint.Color = ColorData[X + Y * Width];

			return true;
		}
		else
		{
			return false;
		}
	});

	return Data;
}

bool UPCGBaseTextureData::IsValid() const
{
	return Height > 0 && Width > 0;
}

void UPCGTextureData::Initialize(UTexture2D* InTexture, const FTransform& InTransform)
{
	Texture = InTexture;
	Transform = InTransform;
	Width = 0;
	Height = 0;

	if (Texture)
	{
#if WITH_EDITORONLY_DATA
		if (Texture->GetPlatformData()->Mips.Num() > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UPCGTextureData::Initialize::ReadData);

			if (Texture->GetPlatformData()->PixelFormat == PF_R8G8B8A8 || Texture->GetPlatformData()->PixelFormat == PF_G8)
			{
				Width = Texture->GetSizeX();
				Height = Texture->GetSizeY();
				const int32 PixelCount = Width * Height;
				ColorData.SetNum(PixelCount);

				// TODO: previously this code created a duplicate of InTexture and read the ColorData from that source 
				// however, there was a problem with the duplicate not having texture data in Mips[0], so we've removed the duplication for now
				// original code can be found in ProceduralTextureReader.h
				const uint8_t* BulkData = reinterpret_cast<const uint8_t*>(InTexture->GetPlatformData()->Mips[0].BulkData.LockReadOnly());

				if (Texture->GetPlatformData()->PixelFormat == PF_R8G8B8A8)
				{
					const FColor* FormattedImageData = reinterpret_cast<const FColor*>(BulkData);
					for (int32 D = 0; D < PixelCount; ++D)
					{
						ColorData[D] = FormattedImageData[D].ReinterpretAsLinear();
					}
				}
				else if (Texture->GetPlatformData()->PixelFormat == PF_G8)
				{
					for (int32 D = 0; D < PixelCount; ++D)
					{
						ColorData[D] = FColor(BulkData[D], BulkData[D], BulkData[D]).ReinterpretAsLinear();
					}
				}

				InTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
			}
			else
			{
				UE_LOG(LogPCG, Error, TEXT("PCGTextureData does not support the format of %s"), *Texture->GetFName().ToString());
			}
		}
#endif
	}

	Bounds = FBox(EForceInit::ForceInit);
	Bounds += FVector(-1.0f, -1.0f, 0.0f);
	Bounds += FVector(1.0f, 1.0f, 0.0f);
	Bounds = Bounds.TransformBy(Transform);
}