// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if ENABLE_RHI_VALIDATION

class FValidationRHIUtils
{
public:
	static void ValidateCopyTexture(
		FRHITexture*	SourceTexture,
		FRHITexture*	DestTexture,
		FIntVector			CopySize = FIntVector::ZeroValue,
		const FIntVector&	SourcePosition = FIntVector::ZeroValue,
		const FIntVector&	DestPosition = FIntVector::ZeroValue)
	{
		check(SourceTexture);
		check(DestTexture);

		const EPixelFormat SrcFormat = SourceTexture->GetFormat();
		const EPixelFormat DstFormat = DestTexture->GetFormat();
		const bool bIsSrcBlockCompressed = GPixelFormats[SrcFormat].BlockSizeX > 1;
		const bool bIsDstBlockCompressed = GPixelFormats[DstFormat].BlockSizeX > 1;
		const int32 SrcBlockBytes = GPixelFormats[SrcFormat].BlockBytes;
		const int32 DstBlockBytes = GPixelFormats[DstFormat].BlockBytes;
		const bool bValidCopyFormats = (SrcFormat == DstFormat) || (!bIsSrcBlockCompressed && bIsDstBlockCompressed && SrcBlockBytes == DstBlockBytes);

		checkf(bValidCopyFormats, TEXT("Some RHIs do not support this format conversion by the GPU for transfer operations!"));

		FIntVector SrcSize = SourceTexture->GetSizeXYZ();
		FIntVector DestSize = DestTexture->GetSizeXYZ();
		if (CopySize == FIntVector::ZeroValue)
		{
			CopySize = SrcSize;
		}

		checkf(CopySize.X <= DestSize.X && CopySize.Y <= DestSize.Y, TEXT("Some RHIs can't perform scaling operations [%dx%d to %dx%d] during copies!"), SrcSize.X, SrcSize.Y, DestSize.X, DestSize.Y);

		check(SourcePosition.X >= 0 && SourcePosition.Y >= 0 && SourcePosition.Z >= 0);
		check(SourcePosition.X + CopySize.X <= SrcSize.X && SourcePosition.Y + CopySize.Y <= SrcSize.Y);

		check(DestPosition.X >= 0 && DestPosition.Y >= 0 && DestPosition.Z >= 0);
		check(DestPosition.X + CopySize.X <= DestSize.X && DestPosition.Y + CopySize.Y <= DestSize.Y);

		if (SourceTexture->GetTexture3D() && DestTexture->GetTexture3D())
		{
			check(SourcePosition.Z + CopySize.Z <= SrcSize.Z);
			check(DestPosition.Z + CopySize.Z <= DestSize.Z);
		}
	}
};

#endif // ENABLE_RHI_VALIDATION
