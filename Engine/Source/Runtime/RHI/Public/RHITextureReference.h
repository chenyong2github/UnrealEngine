// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIResources.h"

class RHI_API FRHITextureReference final : public FRHITexture
{
public:
	explicit FRHITextureReference();
	~FRHITextureReference();

	virtual class FRHITextureReference* GetTextureReference() override;

	virtual void* GetNativeResource() const override;
	virtual void* GetNativeShaderResourceView() const override;
	virtual void* GetTextureBaseRHI() override;
	virtual void GetWriteMaskProperties(void*& OutData, uint32& OutSize) override;
	virtual const FRHITextureDesc& GetDesc() const override;

#if ENABLE_RHI_VALIDATION
	// Implement RHIValidation::FTextureResource::GetTrackerResource to use the tracker info
	// for the referenced texture.
	virtual RHIValidation::FResource* GetTrackerResource() final override;
#endif

	inline FRHITexture * GetReferencedTexture() const
	{
		return ReferencedTexture.GetReference();
	}

private:
	friend class FRHICommandListImmediate;

	// Called only from FRHICommandListImmediate::UpdateTextureReference()
	void SetReferencedTexture(FRHITexture* InTexture)
	{
		ReferencedTexture = InTexture
			? InTexture
			: DefaultTexture.GetReference();
	}

	TRefCountPtr<FRHITexture> ReferencedTexture;

	// This pointer is set by the InitRHI() function on the FBlackTextureWithSRV global resource,
	// to allow FRHITextureReference to use the global black texture when the reference is nullptr.
	// A pointer is required since FBlackTextureWithSRV is defined in RenderCore.
	friend class FBlackTextureWithSRV;
	static TRefCountPtr<FRHITexture> DefaultTexture;
};
