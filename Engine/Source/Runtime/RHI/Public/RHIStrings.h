// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "Misc/AssertionMacros.h"

inline const TCHAR* GetShaderFrequencyString(EShaderFrequency Frequency, bool bIncludePrefix = true)
{
	const TCHAR* String = TEXT("SF_NumFrequencies");
	switch (Frequency)
	{
	case SF_Vertex:			String = TEXT("SF_Vertex"); break;
	case SF_Mesh:			String = TEXT("SF_Mesh"); break;
	case SF_Amplification:	String = TEXT("SF_Amplification"); break;
	case SF_Geometry:		String = TEXT("SF_Geometry"); break;
	case SF_Pixel:			String = TEXT("SF_Pixel"); break;
	case SF_Compute:		String = TEXT("SF_Compute"); break;
	case SF_RayGen:			String = TEXT("SF_RayGen"); break;
	case SF_RayMiss:		String = TEXT("SF_RayMiss"); break;
	case SF_RayHitGroup:	String = TEXT("SF_RayHitGroup"); break;
	case SF_RayCallable:	String = TEXT("SF_RayCallable"); break;

	default:
		checkf(0, TEXT("Unknown ShaderFrequency %d"), (int32)Frequency);
		break;
	}

	// Skip SF_
	int32 Index = bIncludePrefix ? 0 : 3;
	String += Index;
	return String;
}

template<typename TRHIShaderType>
inline const TCHAR* GetShaderFrequencyString(bool bIncludePrefix = true)
{
	return GetShaderFrequencyString(static_cast<EShaderFrequency>(TRHIShaderToEnum<TRHIShaderType>::ShaderFrequency), bIncludePrefix);
}

inline const TCHAR* GetTextureDimensionString(ETextureDimension Dimension)
{
	switch (Dimension)
	{
	case ETextureDimension::Texture2D:
		return TEXT("Texture2D");
	case ETextureDimension::Texture2DArray:
		return TEXT("Texture2DArray");
	case ETextureDimension::Texture3D:
		return TEXT("Texture3D");
	case ETextureDimension::TextureCube:
		return TEXT("TextureCube");
	case ETextureDimension::TextureCubeArray:
		return TEXT("TextureCubeArray");
	}
	return TEXT("");
}

inline const TCHAR* GetTextureCreateFlagString(ETextureCreateFlags TextureCreateFlag)
{
	switch (TextureCreateFlag)
	{
	case ETextureCreateFlags::None:
		return TEXT("None");
	case ETextureCreateFlags::RenderTargetable:
		return TEXT("RenderTargetable");
	case ETextureCreateFlags::ResolveTargetable:
		return TEXT("ResolveTargetable");
	case ETextureCreateFlags::DepthStencilTargetable:
		return TEXT("DepthStencilTargetable");
	case ETextureCreateFlags::ShaderResource:
		return TEXT("ShaderResource");
	case ETextureCreateFlags::SRGB:
		return TEXT("SRGB");
	case ETextureCreateFlags::CPUWritable:
		return TEXT("CPUWritable");
	case ETextureCreateFlags::NoTiling:
		return TEXT("NoTiling");
	case ETextureCreateFlags::VideoDecode:
		return TEXT("VideoDecode");
	case ETextureCreateFlags::Dynamic:
		return TEXT("Dynamic");
	case ETextureCreateFlags::InputAttachmentRead:
		return TEXT("InputAttachmentRead");
	case ETextureCreateFlags::Foveation:
		return TEXT("Foveation");
	case ETextureCreateFlags::Tiling3D:
		return TEXT("Tiling3D");
	case ETextureCreateFlags::Memoryless:
		return TEXT("Memoryless");
	case ETextureCreateFlags::GenerateMipCapable:
		return TEXT("GenerateMipCapable");
	case ETextureCreateFlags::FastVRAMPartialAlloc:
		return TEXT("FastVRAMPartialAlloc");
	case ETextureCreateFlags::DisableSRVCreation:
		return TEXT("DisableSRVCreation");
	case ETextureCreateFlags::DisableDCC:
		return TEXT("DisableDCC");
	case ETextureCreateFlags::UAV:
		return TEXT("UAV");
	case ETextureCreateFlags::Presentable:
		return TEXT("Presentable");
	case ETextureCreateFlags::CPUReadback:
		return TEXT("CPUReadback");
	case ETextureCreateFlags::OfflineProcessed:
		return TEXT("OfflineProcessed");
	case ETextureCreateFlags::FastVRAM:
		return TEXT("FastVRAM");
	case ETextureCreateFlags::HideInVisualizeTexture:
		return TEXT("HideInVisualizeTexture");
	case ETextureCreateFlags::Virtual:
		return TEXT("Virtual");
	case ETextureCreateFlags::TargetArraySlicesIndependently:
		return TEXT("TargetArraySlicesIndependently");
	case ETextureCreateFlags::Shared:
		return TEXT("Shared");
	case ETextureCreateFlags::NoFastClear:
		return TEXT("NoFastClear");
	case ETextureCreateFlags::DepthStencilResolveTarget:
		return TEXT("DepthStencilResolveTarget");
	case ETextureCreateFlags::Streamable:
		return TEXT("Streamable");
	case ETextureCreateFlags::NoFastClearFinalize:
		return TEXT("NoFastClearFinalize");
	case ETextureCreateFlags::AFRManual:
		return TEXT("AFRManual");
	case ETextureCreateFlags::ReduceMemoryWithTilingMode:
		return TEXT("ReduceMemoryWithTilingMode");
	}
	return TEXT("");
}

inline const TCHAR* GetBufferUsageFlagString(EBufferUsageFlags BufferUsage)
{
	switch (BufferUsage)
	{
	case EBufferUsageFlags::None:
		return TEXT("None");
	case EBufferUsageFlags::Static:
		return TEXT("Static");
	case EBufferUsageFlags::Dynamic:
		return TEXT("Dynamic");
	case EBufferUsageFlags::Volatile:
		return TEXT("Volatile");
	case EBufferUsageFlags::UnorderedAccess:
		return TEXT("UnorderedAccess");
	case EBufferUsageFlags::ByteAddressBuffer:
		return TEXT("ByteAddressBuffer");
	case EBufferUsageFlags::SourceCopy:
		return TEXT("SourceCopy");
	case EBufferUsageFlags::StreamOutput:
		return TEXT("StreamOutput");
	case EBufferUsageFlags::DrawIndirect:
		return TEXT("DrawIndirect");
	case EBufferUsageFlags::ShaderResource:
		return TEXT("ShaderResource");
	case EBufferUsageFlags::KeepCPUAccessible:
		return TEXT("KeepCPUAccessible");
	case EBufferUsageFlags::FastVRAM:
		return TEXT("FastVRAM");
	case EBufferUsageFlags::Shared:
		return TEXT("Shared");
	case EBufferUsageFlags::AccelerationStructure:
		return TEXT("AccelerationStructure");
	case EBufferUsageFlags::VertexBuffer:
		return TEXT("VertexBuffer");
	case EBufferUsageFlags::IndexBuffer:
		return TEXT("IndexBuffer");
	case EBufferUsageFlags::StructuredBuffer:
		return TEXT("StructuredBuffer");
	}
	return TEXT("");
}

inline const TCHAR* GetUniformBufferBaseTypeString(EUniformBufferBaseType BaseType)
{
	switch (BaseType)
	{
	case UBMT_INVALID:
		return TEXT("UBMT_INVALID");
	case UBMT_BOOL:
		return TEXT("UBMT_BOOL");
	case UBMT_INT32:
		return TEXT("UBMT_INT32");
	case UBMT_UINT32:
		return TEXT("UBMT_UINT32");
	case UBMT_FLOAT32:
		return TEXT("UBMT_FLOAT32");
	case UBMT_TEXTURE:
		return TEXT("UBMT_TEXTURE");
	case UBMT_SRV:
		return TEXT("UBMT_SRV");
	case UBMT_UAV:
		return TEXT("UBMT_UAV");
	case UBMT_SAMPLER:
		return TEXT("UBMT_SAMPLER");
	case UBMT_RDG_TEXTURE:
		return TEXT("UBMT_RDG_TEXTURE");
	case UBMT_RDG_TEXTURE_ACCESS:
		return TEXT("UBMT_RDG_TEXTURE_ACCESS");
	case UBMT_RDG_TEXTURE_ACCESS_ARRAY:
		return TEXT("UBMT_RDG_TEXTURE_ACCESS_ARRAY");
	case UBMT_RDG_TEXTURE_SRV:
		return TEXT("UBMT_RDG_TEXTURE_SRV");
	case UBMT_RDG_TEXTURE_UAV:
		return TEXT("UBMT_RDG_TEXTURE_UAV");
	case UBMT_RDG_BUFFER_ACCESS:
		return TEXT("UBMT_RDG_BUFFER_ACCESS");
	case UBMT_RDG_BUFFER_ACCESS_ARRAY:
		return TEXT("UBMT_RDG_BUFFER_ACCESS_ARRAY");
	case UBMT_RDG_BUFFER_SRV:
		return TEXT("UBMT_RDG_BUFFER_SRV");
	case UBMT_RDG_BUFFER_UAV:
		return TEXT("UBMT_RDG_BUFFER_UAV");
	case UBMT_RDG_UNIFORM_BUFFER:
		return TEXT("UBMT_RDG_UNIFORM_BUFFER");
	case UBMT_NESTED_STRUCT:
		return TEXT("UBMT_NESTED_STRUCT");
	case UBMT_INCLUDED_STRUCT:
		return TEXT("UBMT_INCLUDED_STRUCT");
	case UBMT_REFERENCED_STRUCT:
		return TEXT("UBMT_REFERENCED_STRUCT");
	case UBMT_RENDER_TARGET_BINDING_SLOTS:
		return TEXT("UBMT_RENDER_TARGET_BINDING_SLOTS");
	}
	return TEXT("");
}
