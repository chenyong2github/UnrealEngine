// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHITextureReference.h"
#include "RHI.h"

FRHITextureReference::FRHITextureReference()
	: FRHITexture(RRT_TextureReference)
{
	check(DefaultTexture);
	ReferencedTexture = DefaultTexture;
}

FRHITextureReference::~FRHITextureReference() = default;

FRHITextureReference* FRHITextureReference::GetTextureReference()
{
	return this;
}

void* FRHITextureReference::GetNativeResource() const
{
	check(ReferencedTexture);
	return ReferencedTexture->GetNativeResource();
}

void* FRHITextureReference::GetNativeShaderResourceView() const
{
	check(ReferencedTexture);
	return ReferencedTexture->GetNativeShaderResourceView();
}

void* FRHITextureReference::GetTextureBaseRHI()
{
	check(ReferencedTexture);
	return ReferencedTexture->GetTextureBaseRHI();
}

void FRHITextureReference::GetWriteMaskProperties(void*& OutData, uint32& OutSize)
{
	check(ReferencedTexture);
	return ReferencedTexture->GetWriteMaskProperties(OutData, OutSize);
}

#if ENABLE_RHI_VALIDATION
RHIValidation::FResource* FRHITextureReference::GetTrackerResource()
{
	check(ReferencedTexture);
	return ReferencedTexture->GetTrackerResource();
}
#endif

const FRHITextureDesc& FRHITextureReference::GetDesc() const
{
	check(ReferencedTexture);
	return ReferencedTexture->GetDesc();
}
