// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

inline void FRDGSubresourceState::Finalize()
{
	ensureMsgf(Pipeline == ERHIPipeline::Graphics, TEXT("Resource should be on the graphics pipeline!"));

	const ERHIAccess LocalAccess = Access;
	*this = {};
	Access = LocalAccess;
}

inline FPooledRenderTargetDesc Translate(const FRDGTextureDesc& InDesc)
{
	check(InDesc.IsValid());

	const ETextureCreateFlags ShaderResourceOnlyFlags = TexCreate_Transient | TexCreate_FastVRAM | TexCreate_ResolveTargetable | TexCreate_DepthStencilResolveTarget;
	const ETextureCreateFlags ShaderResourceFlags = TexCreate_ShaderResource;

	FPooledRenderTargetDesc OutDesc;
	OutDesc.ClearValue = InDesc.ClearValue;
	OutDesc.Flags = (InDesc.Flags & ShaderResourceOnlyFlags) | (InDesc.Flags & ShaderResourceFlags);
	OutDesc.TargetableFlags = (InDesc.Flags & ~ShaderResourceOnlyFlags);
	OutDesc.Format = InDesc.Format;
	OutDesc.Extent.X = InDesc.Extent.X;
	OutDesc.Extent.Y = InDesc.Extent.Y;
	OutDesc.Depth = InDesc.Dimension == ETextureDimension::Texture3D ? InDesc.Depth : 0;
	OutDesc.ArraySize = InDesc.ArraySize;
	OutDesc.NumMips = InDesc.NumMips;
	OutDesc.NumSamples = InDesc.NumSamples;
	OutDesc.bIsArray = InDesc.IsTextureArray();
	OutDesc.bIsCubemap = InDesc.IsTextureCube();
	OutDesc.bForceSeparateTargetAndShaderResource = false;
	OutDesc.bForceSharedTargetAndShaderResource = InDesc.IsMultisample(); // Don't set this unless actually necessary to avoid creating separate pool buckets.
	OutDesc.AutoWritable = false;

	check(OutDesc.IsValid());
	return OutDesc;
}

FRDGTextureDesc Translate(const FPooledRenderTargetDesc& InDesc, ERenderTargetTexture InTexture)
{
	check(InDesc.IsValid());

	FRDGTextureDesc OutDesc;
	OutDesc.ClearValue = InDesc.ClearValue;
	OutDesc.Format = InDesc.Format;
	OutDesc.Extent = InDesc.Extent;
	OutDesc.ArraySize = InDesc.ArraySize;
	OutDesc.NumMips = InDesc.NumMips;

	if (InDesc.Depth > 0)
	{
		OutDesc.Depth = InDesc.Depth;
		OutDesc.Dimension = ETextureDimension::Texture3D;
	}
	else if (InDesc.bIsCubemap)
	{
		OutDesc.Dimension = InDesc.bIsArray ? ETextureDimension::TextureCubeArray : ETextureDimension::TextureCube;
	}
	else if (InDesc.bIsArray)
	{
		OutDesc.Dimension = ETextureDimension::Texture2DArray;
	}

	// Matches logic in RHIUtilities.h for compatibility.
	const ETextureCreateFlags TargetableFlags = ETextureCreateFlags(InDesc.TargetableFlags) | TexCreate_ShaderResource;
	const ETextureCreateFlags ShaderResourceFlags = ETextureCreateFlags(InDesc.Flags) | TexCreate_ShaderResource;

	OutDesc.Flags = TargetableFlags | ShaderResourceFlags;

	bool bUseSeparateTextures = InDesc.bForceSeparateTargetAndShaderResource;

	if (InDesc.NumSamples > 1 && !InDesc.bForceSharedTargetAndShaderResource)
	{
		bUseSeparateTextures = RHISupportsSeparateMSAAAndResolveTextures(GMaxRHIShaderPlatform);
	}

	if (bUseSeparateTextures)
	{
		if (InTexture == ERenderTargetTexture::Targetable)
		{
			OutDesc.NumSamples = InDesc.NumSamples;
			OutDesc.Flags = TargetableFlags;
		}
		else
		{
			OutDesc.Flags = ShaderResourceFlags;

			if (EnumHasAnyFlags(TargetableFlags, TexCreate_RenderTargetable))
			{
				OutDesc.Flags |= TexCreate_ResolveTargetable;
			}

			if (EnumHasAnyFlags(TargetableFlags, TexCreate_DepthStencilTargetable))
			{
				OutDesc.Flags |= TexCreate_DepthStencilResolveTarget;
			}
		}
	}

	check(OutDesc.IsValid());
	return OutDesc;
}

inline FRDGTextureDesc FRDGTextureDesc::Create2DDesc(
	FIntPoint InExtent,
	EPixelFormat InFormat,
	const FClearValueBinding& InClearValue,
	ETextureCreateFlags InFlags,
	ETextureCreateFlags InTargetableFlags,
	bool bInForceSeparateTargetAndShaderResource,
	uint16 InNumMips)
{
	return Translate(FPooledRenderTargetDesc::Create2DDesc(InExtent, InFormat, InClearValue, InFlags, InTargetableFlags, bInForceSeparateTargetAndShaderResource, InNumMips));
}

inline FRDGTextureDesc FRDGTextureDesc::CreateVolumeDesc(
	uint32 InSizeX,
	uint32 InSizeY,
	uint32 InSizeZ,
	EPixelFormat InFormat,
	const FClearValueBinding& InClearValue,
	ETextureCreateFlags InFlags,
	ETextureCreateFlags InTargetableFlags,
	bool bInForceSeparateTargetAndShaderResource,
	uint16 InNumMips)
{
	return Translate(FPooledRenderTargetDesc::CreateVolumeDesc(InSizeX, InSizeY, InSizeZ, InFormat, InClearValue, InFlags, InTargetableFlags, bInForceSeparateTargetAndShaderResource, InNumMips));
}

inline FRDGTextureDesc FRDGTextureDesc::CreateCubemapDesc(
	uint32 InExtent,
	EPixelFormat InFormat,
	const FClearValueBinding& InClearValue,
	ETextureCreateFlags InFlags,
	ETextureCreateFlags InTargetableFlags,
	bool bInForceSeparateTargetAndShaderResource,
	uint32 InArraySize,
	uint16 InNumMips)
{
	return Translate(FPooledRenderTargetDesc::CreateCubemapDesc(InExtent, InFormat, InClearValue, InFlags, InTargetableFlags, bInForceSeparateTargetAndShaderResource, InArraySize, InNumMips));
}

inline FRDGTextureDesc FRDGTextureDesc::CreateCubemapArrayDesc(
	uint32 InExtent,
	EPixelFormat InFormat,
	const FClearValueBinding& InClearValue,
	ETextureCreateFlags InFlags,
	ETextureCreateFlags InTargetableFlags,
	bool bInForceSeparateTargetAndShaderResource,
	uint32 InArraySize,
	uint16 InNumMips)
{
	return Translate(FPooledRenderTargetDesc::CreateCubemapArrayDesc(InExtent, InFormat, InClearValue, InFlags, InTargetableFlags, bInForceSeparateTargetAndShaderResource, InArraySize, InNumMips));
}

inline FRDGTextureSubresourceRange FRDGTextureSRV::GetSubresourceRange() const
{
	FRDGTextureSubresourceRange Range = GetParent()->GetSubresourceRange();
	Range.MipIndex = Desc.MipLevel;
	Range.PlaneSlice = GetResourceTransitionPlaneForMetadataAccess(Desc.MetaData);

	if (Desc.MetaData == ERDGTextureMetaDataAccess::None && Desc.Texture && Desc.Texture->Desc.Format == PF_DepthStencil)
	{
		// PF_X24_G8 is used to indicate that this is a view on the stencil plane. Otherwise, it is a view on the depth plane
		Range.PlaneSlice = Desc.Format == PF_X24_G8 ? FRHITransitionInfo::kStencilPlaneSlice : FRHITransitionInfo::kDepthPlaneSlice;
		Range.NumPlaneSlices = 1;
	}

	if (Desc.NumMipLevels != 0)
	{
		Range.NumMips = Desc.NumMipLevels;
	}

	if (Desc.NumArraySlices != 0)
	{
		Range.NumArraySlices = Desc.NumArraySlices;
	}

	if (Desc.MetaData != ERDGTextureMetaDataAccess::None)
	{
		Range.NumPlaneSlices = 1;
	}

	return Range;
}

inline FRDGTextureSubresourceRange FRDGTextureUAV::GetSubresourceRange() const
{
	FRDGTextureSubresourceRange Range = GetParent()->GetSubresourceRange();
	Range.MipIndex = Desc.MipLevel;
	Range.NumMips = 1;
	Range.PlaneSlice = GetResourceTransitionPlaneForMetadataAccess(Desc.MetaData);

	if (Desc.MetaData != ERDGTextureMetaDataAccess::None)
	{
		Range.NumPlaneSlices = 1;
	}

	return Range;
}

inline FRDGBufferSRVDesc::FRDGBufferSRVDesc(FRDGBufferRef InBuffer)
	: Buffer(InBuffer)
{
	if (Buffer->Desc.Usage & BUF_DrawIndirect)
	{
		BytesPerElement = 4;
		Format = PF_R32_UINT;
	}
	else
	{
		checkf(Buffer->Desc.UnderlyingType != FRDGBufferDesc::EUnderlyingType::VertexBuffer, TEXT("VertexBuffer %s requires a type when creating a SRV."), Buffer->Name);
	}
}

inline FRDGBufferUAVDesc::FRDGBufferUAVDesc(FRDGBufferRef InBuffer)
	: Buffer(InBuffer)
{
	if (Buffer->Desc.Usage & BUF_DrawIndirect)
	{
		Format = PF_R32_UINT;
	}
	else
	{
		checkf(Buffer->Desc.UnderlyingType != FRDGBufferDesc::EUnderlyingType::VertexBuffer, TEXT("VertexBuffer %s requires a type when creating a UAV."), Buffer->Name);
	}
}