// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/PCGTestsCommon.h"

#include "Data/PCGTextureData.h"
#include "Data/PCGRenderTargetData.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"

#if WITH_EDITOR

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGTextureDataOffsetTilingRotation, FPCGTestBaseClass, "pcg.tests.Texture.OffsetTilingRotation", PCGTestsCommon::TestFlags)

bool FPCGTextureDataOffsetTilingRotation::RunTest(const FString& Parameters)
{
	const int32 TextureSize = 128;
	const int32 WhitePixelX = 50;
	const int32 WhitePixelY = 70;

	TArray<FColor> Pixels;
	Pixels.Init(FColor::Black, TextureSize * TextureSize);
	Pixels[WhitePixelY * TextureSize + WhitePixelX] = FColor::White;

	UTexture2D* Texture2D = UTexture2D::CreateTransient(TextureSize, TextureSize, EPixelFormat::PF_R8G8B8A8);
	Texture2D->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
	Texture2D->SRGB = 0;
	Texture2D->MipGenSettings = TMGS_NoMipmaps;
	Texture2D->UpdateResource();

	void* RawTextureData = Texture2D->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);

	FMemory::Memcpy(RawTextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));

	Texture2D->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture2D->UpdateResource();

	UPCGTextureData* TextureData = NewObject<UPCGTextureData>();
	TextureData->Initialize(Texture2D, FTransform());

	TextureData->bStretchToFit = false;

	FRandomStream RandomStream;
	FPCGPoint OutPoint;

	const float ScaledPixelX = static_cast<float>(WhitePixelX) / TextureSize;
	const float ScaledPixelY = static_cast<float>(WhitePixelY) / TextureSize;
	const float TexelScalar = 1.f / 3.f;

	for (int TexelSizeFactor = 0; TexelSizeFactor <= 3; ++TexelSizeFactor)
	{
		const float TexelSize = FMath::Pow(10.f, TexelSizeFactor);
		const float ScaledTexelSize = TexelSize * TexelScalar;

		TextureData->TexelSize = TexelSize;
		TextureData->Rotation = 0.f;
	
		// sampling with no offset
		TextureData->XOffset = 0.f;
		TextureData->YOffset = 0.f;
		TextureData->SamplePoint(FTransform(), FBox(), OutPoint, nullptr);
		TestEqual("Valid color sampled for no offset at (0, 0)", OutPoint.Color, static_cast<FVector>(FColor::Black));

		// sampling at position, with no offset
		TextureData->XOffset = 0.f;
		TextureData->YOffset = 0.f;
		TextureData->SamplePoint(FTransform(FVector(ScaledTexelSize, ScaledTexelSize, 0.f)), FBox(), OutPoint, nullptr);
		TestEqual("Valid color sampled for no offset at position", OutPoint.Color, static_cast<FVector>(FColor::Black));
	
		// sampling with offset
		TextureData->XOffset = 1 - ScaledPixelX;
		TextureData->YOffset = 1 - ScaledPixelY;
		TextureData->SamplePoint(FTransform(), FBox(), OutPoint, nullptr);
		TestEqual("Valid color sampled for offset from (0, 0)", OutPoint.Color, static_cast<FVector>(FColor::White));

		// sampling at position, with offset
		TextureData->XOffset = (TexelScalar - WhitePixelX) / TextureSize;
		TextureData->YOffset = (TexelScalar - WhitePixelY) / TextureSize;
		TextureData->SamplePoint(FTransform(FVector(ScaledTexelSize, ScaledTexelSize, 0.f)), FBox(), OutPoint, nullptr);
		TestEqual("Valid color sampled for offset from position", OutPoint.Color, static_cast<FVector>(FColor::White));
	
		for (float Rotation = 0.f; Rotation < 360.f; Rotation += 10.f)
		{
			const float Theta = FMath::DegreesToRadians(Rotation);
			const float CosTheta = FMath::Cos(Theta);
			const float SinTheta = FMath::Sin(Theta);
			const float X = ((WhitePixelX * CosTheta) - (WhitePixelY * SinTheta)) * TexelSize;
			const float Y = ((WhitePixelY * CosTheta) + (WhitePixelX * SinTheta)) * TexelSize;

			FTransform RotatedTransform(FVector(X, Y, 0.f));

			// sampling with rotation at black position
			TextureData->Rotation = Rotation;
			TextureData->XOffset = 0.f; 
			TextureData->YOffset = 0.f; 
			TextureData->SamplePoint(FTransform(FVector(ScaledTexelSize, ScaledTexelSize, 0.f)), FBox(), OutPoint, nullptr);
			TestEqual("Valid color sampled for non-rotated position", OutPoint.Color, static_cast<FVector>(FColor::Black));
	
			// sampling with rotation at white position
			TextureData->Rotation = Rotation;
			TextureData->XOffset = 0.f; 
			TextureData->YOffset = 0.f; 
			TextureData->SamplePoint(RotatedTransform, FBox(), OutPoint, nullptr);
			TestEqual("Valid color sampled for rotated position", OutPoint.Color, static_cast<FVector>(FColor::White));
			
			RotatedTransform.SetLocation(FVector(X / 2, Y / 2, 0.f));
	
			// sampling with rotation and offset
			TextureData->Rotation = Rotation;
			TextureData->XOffset = -ScaledPixelX / 2;
			TextureData->YOffset = -ScaledPixelY / 2;
			TextureData->SamplePoint(RotatedTransform, FBox(), OutPoint, nullptr);
			TestEqual("Valid color sampled for offset and rotation", OutPoint.Color, static_cast<FVector>(FColor::White));
		}
	}

	return true;
}
#endif
