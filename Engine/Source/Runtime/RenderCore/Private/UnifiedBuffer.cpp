// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnifiedBuffer.h"
#include "RHI.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "Shader.h"
#include "ShaderParameterUtils.h"
#include "GlobalShader.h"
#include "RenderUtils.h"
#include "RenderGraphUtils.h"

enum class EByteBufferResourceType
{
	Float4_Buffer,
	Float4_StructuredBuffer,
	Uint_Buffer,
	Uint4Aligned_Buffer,
	Float4_Texture,
	Count
};

class FByteBufferShader : public FGlobalShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FByteBufferShader, NonVirtual);

	FByteBufferShader() {}
	FByteBufferShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}

	class ResourceTypeDim : SHADER_PERMUTATION_INT("RESOURCE_TYPE", (int)EByteBufferResourceType::Count);

	using FPermutationDomain = TShaderPermutationDomain<ResourceTypeDim>;

	static bool ShouldCompilePermutation( const FGlobalShaderPermutationParameters& Parameters )
	{
		FPermutationDomain PermutationVector( Parameters.PermutationId );

		EByteBufferResourceType ResourceType = (EByteBufferResourceType)PermutationVector.Get<ResourceTypeDim>();

		if (ResourceType == EByteBufferResourceType::Uint_Buffer || ResourceType == EByteBufferResourceType::Uint4Aligned_Buffer)
		{
			return FDataDrivenShaderPlatformInfo::GetSupportsByteBufferComputeShaders(Parameters.Platform);
		}
		else
		{
			return RHISupportsComputeShaders(Parameters.Platform);
		}
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER(uint32, Value)
		SHADER_PARAMETER(uint32, Size)
		SHADER_PARAMETER(uint32, SrcOffset)
		SHADER_PARAMETER(uint32, DstOffset)
		SHADER_PARAMETER(uint32, Float4sPerLine)
		SHADER_PARAMETER_UAV(RWBuffer<float4>, DstBuffer)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, DstStructuredBuffer)
		SHADER_PARAMETER_UAV(RWByteAddressBuffer, DstByteAddressBuffer)
		SHADER_PARAMETER_UAV(RWTexture2D<float4>, DstTexture)
	END_SHADER_PARAMETER_STRUCT()
};

class FMemsetBufferCS : public FByteBufferShader
{
	DECLARE_GLOBAL_SHADER( FMemsetBufferCS );
	SHADER_USE_PARAMETER_STRUCT( FMemsetBufferCS, FByteBufferShader );
};
IMPLEMENT_GLOBAL_SHADER( FMemsetBufferCS, "/Engine/Private/ByteBuffer.usf", "MemsetBufferCS", SF_Compute );

class FMemcpyCS : public FByteBufferShader
{
	DECLARE_GLOBAL_SHADER( FMemcpyCS );
	SHADER_USE_PARAMETER_STRUCT( FMemcpyCS, FByteBufferShader );

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FByteBufferShader::FParameters, Common)
		SHADER_PARAMETER_SRV(Buffer<float4>, SrcBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SrcStructuredBuffer)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, SrcByteAddressBuffer)
		SHADER_PARAMETER_SRV(Texture2D<float4>, SrcTexture)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER( FMemcpyCS, "/Engine/Private/ByteBuffer.usf", "MemcpyCS", SF_Compute );

class FScatterCopyCS : public FByteBufferShader
{
	DECLARE_GLOBAL_SHADER( FScatterCopyCS );
	SHADER_USE_PARAMETER_STRUCT( FScatterCopyCS, FByteBufferShader );

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FByteBufferShader::FParameters, Common)
		SHADER_PARAMETER(uint32, NumScatters)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, UploadByteAddressBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, UploadStructuredBuffer)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ScatterByteAddressBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, ScatterStructuredBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER( FScatterCopyCS, "/Engine/Private/ByteBuffer.usf", "ScatterCopyCS", SF_Compute );

enum class EResourceType
{
	BUFFER,
	STRUCTURED_BUFFER,
	BYTEBUFFER,
	TEXTURE
};

template<typename ResourceType>
struct ResourceTypeTraits;

template<>
struct ResourceTypeTraits<FRWBuffer>
{
	static const EResourceType Type = EResourceType::BUFFER;
};

template<>
struct ResourceTypeTraits<FRWBufferStructured>
{
	static const EResourceType Type = EResourceType::STRUCTURED_BUFFER;
};

template<>
struct ResourceTypeTraits<FTextureRWBuffer2D>
{
	static const EResourceType Type = EResourceType::TEXTURE;
};

template<>
struct ResourceTypeTraits<FRWByteAddressBuffer>
{
	static const EResourceType Type = EResourceType::BYTEBUFFER;
};

static uint32 CalculateFloat4sPerLine()
{
	uint16 PrimitivesPerTextureLine = (uint16)FMath::Min((int32)MAX_uint16, (int32)GMaxTextureDimensions) / FScatterUploadBuffer::PrimitiveDataStrideInFloat4s;
	return PrimitivesPerTextureLine * FScatterUploadBuffer::PrimitiveDataStrideInFloat4s;
}

template<typename ResourceType>
void MemsetResource(FRHICommandList& RHICmdList, const ResourceType& DstBuffer, const FMemsetResourceParams& Params)
{
	EByteBufferResourceType ResourceTypeEnum;

	FMemsetBufferCS::FParameters Parameters;
	Parameters.Value = Params.Value;
	Parameters.Size = Params.Count;
	Parameters.DstOffset = Params.DstOffset;

	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER)
	{
		ResourceTypeEnum = EByteBufferResourceType::Uint_Buffer;

		Parameters.DstByteAddressBuffer = DstBuffer.UAV;
	}
	else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BUFFER)
	{
		ResourceTypeEnum = EByteBufferResourceType::Float4_Buffer;

		Parameters.DstBuffer = DstBuffer.UAV;
	}
	else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::STRUCTURED_BUFFER)
	{
		ResourceTypeEnum = EByteBufferResourceType::Float4_StructuredBuffer;

		Parameters.DstStructuredBuffer = DstBuffer.UAV;
	}
	else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::TEXTURE)
	{
		ResourceTypeEnum = EByteBufferResourceType::Float4_Texture;

		Parameters.DstTexture = DstBuffer.UAV;
		Parameters.Float4sPerLine = CalculateFloat4sPerLine();
	}

	FMemcpyCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FMemcpyCS::ResourceTypeDim >((int)ResourceTypeEnum);

	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FMemsetBufferCS>(PermutationVector);

	// each thread will set 4 floats / uints
	const uint32 Divisor = ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER ? 4 : 1;

	FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FIntVector(FMath::DivideAndRoundUp(Params.Count / Divisor, 64u), 1, 1));
}

template<typename ResourceType>
void MemcpyResource(FRHICommandList& RHICmdList, const ResourceType& DstBuffer, const ResourceType& SrcBuffer, const FMemcpyResourceParams& Params, bool bAlreadyInUAVOverlap)
{
	// each thread will copy 4 floats / uints
	const uint32 Divisor = ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER ? 4 : 1;

	if (!bAlreadyInUAVOverlap)	// TODO: Get rid of this check once BeginUAVOverlap/EndUAVOverlap supports nesting.
		RHICmdList.BeginUAVOverlap(DstBuffer.UAV);

	uint32 NumElementsProcessed = 0;

	while (NumElementsProcessed < Params.Count)
	{
		const uint32 NumWaves = FMath::Max(FMath::Min<uint32>(GRHIMaxDispatchThreadGroupsPerDimension.X, FMath::DivideAndRoundUp(Params.Count / Divisor, 64u)), 1u);
		const uint32 NumElementsPerDispatch = FMath::Min(FMath::Max(NumWaves, 1u) * Divisor * 64, Params.Count - NumElementsProcessed);

		EByteBufferResourceType ResourceTypeEnum;

		FMemcpyCS::FParameters Parameters;
		Parameters.Common.Size = NumElementsPerDispatch;
		Parameters.Common.SrcOffset = (Params.SrcOffset + NumElementsProcessed);
		Parameters.Common.DstOffset = (Params.DstOffset + NumElementsProcessed);

		if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER)
		{
			ResourceTypeEnum = EByteBufferResourceType::Uint_Buffer;

			Parameters.SrcByteAddressBuffer = SrcBuffer.SRV;
			Parameters.Common.DstByteAddressBuffer = DstBuffer.UAV;
		}
		else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::STRUCTURED_BUFFER)
		{
			ResourceTypeEnum = EByteBufferResourceType::Float4_StructuredBuffer;

			Parameters.SrcStructuredBuffer = SrcBuffer.SRV;
			Parameters.Common.DstStructuredBuffer = DstBuffer.UAV;
		}
		else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BUFFER)
		{
			ResourceTypeEnum = EByteBufferResourceType::Float4_Buffer;

			Parameters.SrcBuffer = SrcBuffer.SRV;
			Parameters.Common.DstBuffer = DstBuffer.UAV;
		}
		else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::TEXTURE)
		{
			ResourceTypeEnum = EByteBufferResourceType::Float4_Texture;

			Parameters.SrcTexture = SrcBuffer.SRV;
			Parameters.Common.DstTexture = DstBuffer.UAV;
			Parameters.Common.Float4sPerLine = CalculateFloat4sPerLine();
		}
		else
		{
			check(false);
		}

		FMemcpyCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FMemcpyCS::ResourceTypeDim >((int)ResourceTypeEnum);

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FMemcpyCS >(PermutationVector);

		FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FIntVector(NumWaves, 1, 1));

		NumElementsProcessed += NumElementsPerDispatch;
	}

	if(!bAlreadyInUAVOverlap)
		RHICmdList.EndUAVOverlap(DstBuffer.UAV);
}

template<>
RENDERCORE_API bool ResizeResourceIfNeeded<FTextureRWBuffer2D>(FRHICommandList& RHICmdList, FTextureRWBuffer2D& Texture, uint32 NumBytes, const TCHAR* DebugName)
{
	check((NumBytes & 15) == 0);
	uint32 Float4sPerLine = CalculateFloat4sPerLine();
	uint32 BytesPerLine = Float4sPerLine * 16;

	EPixelFormat BufferFormat = PF_A32B32G32R32F;
	uint32 BytesPerElement = GPixelFormats[BufferFormat].BlockBytes;

	uint32 NumLines = (NumBytes + BytesPerLine - 1) / BytesPerLine;

	if (Texture.NumBytes == 0)
	{
		Texture.Initialize(DebugName, BytesPerElement, Float4sPerLine, NumLines, PF_A32B32G32R32F, TexCreate_RenderTargetable | TexCreate_UAV);
		return true;
	}
	else if ((NumLines * Float4sPerLine * BytesPerElement) != Texture.NumBytes)
	{
		FTextureRWBuffer2D NewTexture;
		NewTexture.Initialize(DebugName, BytesPerElement, Float4sPerLine, NumLines, PF_A32B32G32R32F, TexCreate_RenderTargetable | TexCreate_UAV);

		FMemcpyResourceParams Params;
		Params.Count = NumBytes / BytesPerElement;
		Params.SrcOffset = 0;
		Params.DstOffset = 0;
		MemcpyResource(RHICmdList, NewTexture, Texture, Params);
		Texture = NewTexture;
		return true;
	}

	return false;
}

template<>
RENDERCORE_API bool ResizeResourceIfNeeded<FRWBufferStructured>(FRHICommandList& RHICmdList, FRWBufferStructured& Buffer, uint32 NumBytes, const TCHAR* DebugName)
{
	const uint32 BytesPerElement = 16;

	check((NumBytes & (BytesPerElement - 1)) == 0);

	uint32 NumElements = NumBytes / BytesPerElement;

	if (Buffer.NumBytes == 0)
	{
		Buffer.Initialize(DebugName, BytesPerElement, NumElements);
		return true;
	}
	else if (NumBytes != Buffer.NumBytes)
	{
		FRWBufferStructured NewBuffer;
		NewBuffer.Initialize(DebugName, BytesPerElement, NumElements);

		RHICmdList.Transition(FRHITransitionInfo(Buffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
		RHICmdList.Transition(FRHITransitionInfo(NewBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));

		// Copy data to new buffer
		FMemcpyResourceParams Params;
		Params.Count = FMath::Min(NumBytes, Buffer.NumBytes) / BytesPerElement;
		Params.SrcOffset = 0;
		Params.DstOffset = 0;
		MemcpyResource(RHICmdList, NewBuffer, Buffer, Params);

		Buffer = NewBuffer;
		return true;
	}

	return false;
}

template<>
RENDERCORE_API bool ResizeResourceIfNeeded<FRWByteAddressBuffer>(FRHICommandList& RHICmdList, FRWByteAddressBuffer& Buffer, uint32 NumBytes, const TCHAR* DebugName)
{
	const uint32 BytesPerElement = 4;

	// Needs to be aligned to 16 bytes to MemcpyResource to work correctly (otherwise it skips last unaligned elements of the buffer during resize)
	check((NumBytes & 15) == 0);

	if (Buffer.NumBytes == 0)
	{
		Buffer.Initialize(DebugName, NumBytes);
		return true;
	}
	else if (NumBytes != Buffer.NumBytes)
	{
		FRWByteAddressBuffer NewBuffer;
		NewBuffer.Initialize(DebugName, NumBytes);

		RHICmdList.Transition({
			FRHITransitionInfo(Buffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
			FRHITransitionInfo(NewBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
		});

		// Copy data to new buffer
		FMemcpyResourceParams Params;
		Params.Count = FMath::Min(NumBytes, Buffer.NumBytes) / BytesPerElement;
		Params.SrcOffset = 0;
		Params.DstOffset = 0;
		MemcpyResource(RHICmdList, NewBuffer, Buffer, Params);

		Buffer = NewBuffer;
		return true;
	}

	return false;
}

RENDERCORE_API bool ResizeResourceIfNeeded(FRHICommandList& RHICmdList, FRWBuffer& Buffer, EPixelFormat Format, uint32 NumElements, const TCHAR* DebugName)
{
	const uint32 BytesPerElement = GPixelFormats[Format].BlockBytes;
	const uint32 NumBytes = BytesPerElement * NumElements;

	if (Buffer.NumBytes == 0)
	{
		Buffer.Initialize(DebugName, BytesPerElement, NumElements, Format);
		return true;
	}
	else if (NumBytes != Buffer.NumBytes)
	{
		FRWBuffer NewBuffer;
		NewBuffer.Initialize(DebugName, BytesPerElement, NumElements, Format);

		RHICmdList.Transition(FRHITransitionInfo(Buffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
		RHICmdList.Transition(FRHITransitionInfo(NewBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));

		// Copy data to new buffer
		FMemcpyResourceParams MemcpyParams;
		MemcpyParams.Count = FMath::Min(NumBytes, Buffer.NumBytes) / BytesPerElement;
		MemcpyParams.SrcOffset = 0;
		MemcpyParams.DstOffset = 0;
		MemcpyResource(RHICmdList, NewBuffer, Buffer, MemcpyParams);

		Buffer = NewBuffer;
		return true;
	}

	return false;
}

template<>
RENDERCORE_API bool ResizeResourceSOAIfNeeded<FRWBufferStructured>(FRHICommandList& RHICmdList, FRWBufferStructured& Buffer, const FResizeResourceSOAParams& Params, const TCHAR* DebugName)
{
	const uint32 BytesPerElement = 16;

	checkf(Params.NumBytes % BytesPerElement == 0, TEXT("NumBytes (%s) must be a multiple of BytesPerElement (%s)"), Params.NumBytes, BytesPerElement);
	checkf(Buffer.NumBytes % BytesPerElement == 0, TEXT("NumBytes (%s) must be a multiple of BytesPerElement (%s)"), Buffer.NumBytes, BytesPerElement);

	uint32 NumElements = Params.NumBytes / BytesPerElement;
	uint32 NumElementsOld = Buffer.NumBytes / BytesPerElement;

	checkf(NumElements % Params.NumArrays == 0, TEXT("NumElements (%s) must be a multiple of NumArrays (%s)"), NumElements, Params.NumArrays);
	checkf(NumElementsOld % Params.NumArrays == 0, TEXT("NumElements (%s) must be a multiple of NumArrays (%s)"), NumElementsOld, Params.NumArrays);

	if (Buffer.NumBytes == 0)
	{
		Buffer.Initialize(DebugName, BytesPerElement, NumElements);
		return true;
	}
	else if (Params.NumBytes != Buffer.NumBytes)
	{
		FRWBufferStructured NewBuffer;
		NewBuffer.Initialize(DebugName, BytesPerElement, NumElements);

		RHICmdList.Transition({
			FRHITransitionInfo(Buffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
			FRHITransitionInfo(NewBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
		});

		// Copy data to new buffer
		uint32 OldArraySize = NumElementsOld / Params.NumArrays;
		uint32 NewArraySize = NumElements / Params.NumArrays;

		RHICmdList.BeginUAVOverlap(NewBuffer.UAV);

		FMemcpyResourceParams MemcpyParams;
		MemcpyParams.Count = FMath::Min(NewArraySize, OldArraySize);

		for( uint32 i = 0; i < Params.NumArrays; i++ )
		{
			MemcpyParams.SrcOffset = i * OldArraySize;
			MemcpyParams.DstOffset = i * NewArraySize;
			MemcpyResource( RHICmdList, NewBuffer, Buffer, MemcpyParams, true );
		}

		RHICmdList.EndUAVOverlap(NewBuffer.UAV);

		Buffer = NewBuffer;
		return true;
	}

	return false;
}

RENDERCORE_API bool ResizeResourceSOAIfNeeded(FRDGBuilder& GraphBuilder, FRWBufferStructured& Buffer, const FResizeResourceSOAParams& Params, const TCHAR* DebugName)
{
	const uint32 BytesPerElement = 16;

	checkf(Params.NumBytes % BytesPerElement == 0, TEXT("NumBytes (%s) must be a multiple of BytesPerElement (%s)"), Params.NumBytes, BytesPerElement);
	checkf(Buffer.NumBytes % BytesPerElement == 0, TEXT("NumBytes (%s) must be a multiple of BytesPerElement (%s)"), Buffer.NumBytes, BytesPerElement);

	uint32 NumElements = Params.NumBytes / BytesPerElement;
	uint32 NumElementsOld = Buffer.NumBytes / BytesPerElement;

	checkf(NumElements % Params.NumArrays == 0, TEXT("NumElements (%s) must be a multiple of NumArrays (%s)"), NumElements, Params.NumArrays);
	checkf(NumElementsOld % Params.NumArrays == 0, TEXT("NumElements (%s) must be a multiple of NumArrays (%s)"), NumElementsOld, Params.NumArrays);

	if (Buffer.NumBytes == 0)
	{
		Buffer.Initialize(DebugName, BytesPerElement, NumElements);
		return true;
	}
	else if (Params.NumBytes != Buffer.NumBytes)
	{
		FRWBufferStructured NewBuffer;
		FRWBufferStructured OldBuffer = Buffer;
		NewBuffer.Initialize(DebugName, BytesPerElement, NumElements);

		AddPass(GraphBuilder, RDG_EVENT_NAME("ResizeResourceSOAIfNeeded"), 
			[OldBuffer, NewBuffer, NumElements, NumElementsOld, Params](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.Transition({
				FRHITransitionInfo(OldBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
				FRHITransitionInfo(NewBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
			});

			// Copy data to new buffer
			uint32 OldArraySize = NumElementsOld / Params.NumArrays;
			uint32 NewArraySize = NumElements / Params.NumArrays;

			RHICmdList.BeginUAVOverlap(NewBuffer.UAV);

			FMemcpyResourceParams MemcpyParams;
			MemcpyParams.Count = FMath::Min(NewArraySize, OldArraySize);

			for (uint32 i = 0; i < Params.NumArrays; i++)
			{
				MemcpyParams.SrcOffset = i * OldArraySize;
				MemcpyParams.DstOffset = i * NewArraySize;
				MemcpyResource(RHICmdList, NewBuffer, OldBuffer, MemcpyParams, true);
			}
			RHICmdList.EndUAVOverlap(NewBuffer.UAV);
		});

		Buffer = NewBuffer;
		return true;
	}

	return false;
}

template <typename FBufferType>
void AddCopyBufferPass(FRDGBuilder& GraphBuilder, const FBufferType &NewBuffer, const FBufferType &OldBuffer, uint32 ElementSize)
{
	AddPass(GraphBuilder, RDG_EVENT_NAME("ResizeResourceIfNeeded-Copy"), 
		[OldBuffer, NewBuffer, ElementSize](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.Transition({
			FRHITransitionInfo(OldBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
			FRHITransitionInfo(NewBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
		});

		// Copy data to new buffer
		FMemcpyResourceParams MemcpyParams;
		MemcpyParams.Count = FMath::Min(NewBuffer.NumBytes, OldBuffer.NumBytes) / ElementSize;
		MemcpyParams.SrcOffset = 0;
		MemcpyParams.DstOffset = 0;

		MemcpyResource(RHICmdList, NewBuffer, OldBuffer, MemcpyParams);
	});
}

RENDERCORE_API bool ResizeResourceIfNeeded(FRDGBuilder& GraphBuilder, FRWBufferStructured& Buffer, uint32 NumBytes, const TCHAR* DebugName)
{
	const uint32 BytesPerElement = 16;

	checkf((NumBytes % BytesPerElement) == 0, TEXT("NumBytes (%s) must be a multiple of BytesPerElement (%s)"), NumBytes, BytesPerElement);

	uint32 NumElements = NumBytes / BytesPerElement;

	if (Buffer.NumBytes == 0)
	{
		Buffer.Initialize(DebugName, BytesPerElement, NumElements);
		return true;
	}
	else if (NumBytes != Buffer.NumBytes)
	{
		FRWBufferStructured NewBuffer;
		NewBuffer.Initialize(DebugName, BytesPerElement, NumElements);

		AddCopyBufferPass(GraphBuilder, NewBuffer, Buffer, BytesPerElement);

		Buffer = NewBuffer;
		return true;
	}

	return false;
}

RENDERCORE_API bool ResizeResourceIfNeeded(FRDGBuilder& GraphBuilder, FRWByteAddressBuffer& Buffer, uint32 NumBytes, const TCHAR* DebugName)
{
	// Needs to be aligned to 16 bytes to MemcpyResource to work correctly (otherwise it skips last unaligned elements of the buffer during resize)
	check((NumBytes & 15) == 0);

	if (Buffer.NumBytes == 0)
	{
		Buffer.Initialize(DebugName, NumBytes);
		return true;
	}
	else if (NumBytes != Buffer.NumBytes)
	{
		FRWByteAddressBuffer NewBuffer;
		NewBuffer.Initialize(DebugName, NumBytes);

		AddCopyBufferPass(GraphBuilder, NewBuffer, Buffer, 4);

		Buffer = NewBuffer;
		return true;
	}

	return false;
}

RENDERCORE_API bool ResizeResourceIfNeeded(FRDGBuilder& GraphBuilder, FRWBuffer& Buffer, EPixelFormat Format, uint32 NumElements, const TCHAR* DebugName)
{
	const uint32 BytesPerElement = GPixelFormats[Format].BlockBytes;
	const uint32 NumBytes = BytesPerElement * NumElements;

	if (Buffer.NumBytes == 0)
	{
		Buffer.Initialize(DebugName, BytesPerElement, NumElements, Format);
		return true;
	}
	else if (NumBytes != Buffer.NumBytes)
	{
		FRWBuffer NewBuffer;
		NewBuffer.Initialize(DebugName, BytesPerElement, NumElements, Format);

		AddCopyBufferPass(GraphBuilder, NewBuffer, Buffer, BytesPerElement);

		Buffer = NewBuffer;
		return true;
	}

	return false;
}

void FScatterUploadBuffer::Init( uint32 NumElements, uint32 InNumBytesPerElement, bool bInFloat4Buffer, const TCHAR* DebugName )
{
	NumScatters = 0;
	MaxScatters = NumElements;
	NumBytesPerElement = InNumBytesPerElement;
	bFloat4Buffer = bInFloat4Buffer;

	const EBufferUsageFlags Usage = bInFloat4Buffer ? BUF_None : BUF_ByteAddressBuffer;
	const uint32 TypeSize = bInFloat4Buffer ? 16 : 4;

	uint32 ScatterBytes = NumElements * sizeof( uint32 );
	uint32 ScatterBufferSize = (uint32)FMath::Min( (uint64)FMath::RoundUpToPowerOfTwo( ScatterBytes ), GetMaxBufferDimension() * sizeof( uint32 ) );
	check( ScatterBufferSize >= ScatterBytes );

	uint32 UploadBytes = NumElements * NumBytesPerElement;
	uint32 UploadBufferSize = (uint32)FMath::Min( (uint64)FMath::RoundUpToPowerOfTwo( UploadBytes ), GetMaxBufferDimension() * TypeSize );
	check( UploadBufferSize >= UploadBytes );

	if (bUploadViaCreate)
	{
		if (ScatterBytes > ScatterDataSize || ScatterBufferSize < ScatterDataSize / 2)
		{
			FMemory::Free(ScatterData);
			ScatterData = (uint32*)FMemory::Malloc(ScatterBufferSize);
			ScatterDataSize = ScatterBufferSize;
		}

		if (UploadBytes > UploadDataSize || UploadBufferSize < UploadDataSize / 2)
		{
			FMemory::Free(UploadData);
			UploadData = (uint8*)FMemory::Malloc(UploadBufferSize);
			UploadDataSize = UploadBufferSize;
		}
	}
	else
	{
		check(ScatterData == nullptr);
		check(UploadData == nullptr);

		if (ScatterBytes > ScatterBuffer.NumBytes || ScatterBufferSize < ScatterBuffer.NumBytes / 2)
		{
			// Resize Scatter Buffer
			ScatterBuffer.Release();
			ScatterBuffer.NumBytes = ScatterBufferSize;

			FRHIResourceCreateInfo CreateInfo(DebugName);
			ScatterBuffer.Buffer = RHICreateStructuredBuffer(sizeof(uint32), ScatterBuffer.NumBytes, BUF_ShaderResource | BUF_Volatile | Usage, CreateInfo);
			ScatterBuffer.SRV = RHICreateShaderResourceView(ScatterBuffer.Buffer);
		}

		if (UploadBytes > UploadBuffer.NumBytes || UploadBufferSize < UploadBuffer.NumBytes / 2)
		{
			// Resize Upload Buffer
			UploadBuffer.Release();
			UploadBuffer.NumBytes = UploadBufferSize;

			FRHIResourceCreateInfo CreateInfo(DebugName);
			UploadBuffer.Buffer = RHICreateStructuredBuffer(TypeSize, UploadBuffer.NumBytes, BUF_ShaderResource | BUF_Volatile | Usage, CreateInfo);
			UploadBuffer.SRV = RHICreateShaderResourceView(UploadBuffer.Buffer);
		}

		ScatterData = (uint32*)RHILockBuffer(ScatterBuffer.Buffer, 0, ScatterBytes, RLM_WriteOnly);
		UploadData = (uint8*)RHILockBuffer(UploadBuffer.Buffer, 0, UploadBytes, RLM_WriteOnly);
	}
}

void FScatterUploadBuffer::Init(TArrayView<const uint32> ElementScatterOffsets, uint32 InNumBytesPerElement, bool bInFloat4Buffer, const TCHAR* DebugName)
{
	Init(ElementScatterOffsets.Num(), InNumBytesPerElement, bInFloat4Buffer, DebugName);
	FMemory::ParallelMemcpy(ScatterData, ElementScatterOffsets.GetData(), ElementScatterOffsets.Num() * ElementScatterOffsets.GetTypeSize(), EMemcpyCachePolicy::StoreUncached);
	NumScatters = ElementScatterOffsets.Num();
}

void FScatterUploadBuffer::InitPreSized(uint32 NumElements, uint32 InNumBytesPerElement, bool bInFloat4Buffer, const TCHAR* DebugName)
{
	Init(NumElements, InNumBytesPerElement, bInFloat4Buffer, DebugName);
	NumScatters = NumElements;
}

// Helper type used to initialize the buffer data on creation
struct FScatterUploadBufferResourceArray : public FResourceArrayInterface
{
	const void* const DataPtr;
	const int32 DataSize;

	FScatterUploadBufferResourceArray(void* InDataPtr, int32 InDataSize)
		: DataPtr(InDataPtr)
		, DataSize(InDataSize)
	{
	}

	const void* GetResourceData() const override { return DataPtr; }
	uint32 GetResourceDataSize() const override { return DataSize; }

	// Not necessary for our purposes
	void Discard() override { }
	bool IsStatic() const override { return false; }
	bool GetAllowCPUAccess() const override { return true; }
	void SetAllowCPUAccess(bool bInNeedsCPUAccess) override { }
};

template<typename ResourceType>
void FScatterUploadBuffer::ResourceUploadTo(FRHICommandList& RHICmdList, const ResourceType& DstBuffer, bool bFlush)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FScatterUploadBuffer::ResourceUploadTo);

	if (bUploadViaCreate)
	{
		ScatterBuffer.Release();
		UploadBuffer.Release();

		ScatterBuffer.NumBytes = ScatterDataSize;
		UploadBuffer.NumBytes = UploadDataSize;

		const uint32 TypeSize = bFloat4Buffer ? 16 : 4;
		const EBufferUsageFlags Usage = bFloat4Buffer ? BUF_None : BUF_ByteAddressBuffer;

		{
			FScatterUploadBufferResourceArray ScatterResourceArray(ScatterData, ScatterDataSize);
			FRHIResourceCreateInfo CreateInfo(TEXT("ScatterResourceArray"), &ScatterResourceArray);
			ScatterBuffer.Buffer = RHICreateStructuredBuffer(sizeof(uint32), ScatterDataSize, BUF_ShaderResource | BUF_Volatile | Usage, CreateInfo);
			ScatterBuffer.SRV = RHICreateShaderResourceView(ScatterBuffer.Buffer);
		}
		{
			FScatterUploadBufferResourceArray UploadResourceArray(UploadData, UploadDataSize);
			FRHIResourceCreateInfo CreateInfo(TEXT("ScatterUploadBuffer"), &UploadResourceArray);
			UploadBuffer.Buffer = RHICreateStructuredBuffer(TypeSize, UploadDataSize, BUF_ShaderResource | BUF_Volatile | Usage, CreateInfo);
			UploadBuffer.SRV = RHICreateShaderResourceView(UploadBuffer.Buffer);
		}
	}
	else
	{
		RHIUnlockBuffer(ScatterBuffer.Buffer);
		RHIUnlockBuffer(UploadBuffer.Buffer);

		ScatterData = nullptr;
		UploadData = nullptr;
	}

	if (NumScatters == 0)
		return;

	constexpr uint32 ThreadGroupSize = 64u;
	uint32 NumBytesPerThread = (NumBytesPerElement & 15) == 0 ? 16 : 4;
	uint32 NumThreadsPerScatter = NumBytesPerElement / NumBytesPerThread;
	uint32 NumThreads = NumScatters * NumThreadsPerScatter;
	uint32 NumDispatches = FMath::DivideAndRoundUp(NumThreads, ThreadGroupSize);
	uint32 NumLoops = FMath::DivideAndRoundUp(NumDispatches, (uint32)GMaxComputeDispatchDimension);

	EByteBufferResourceType ResourceTypeEnum;

	FScatterCopyCS::FParameters Parameters;
	Parameters.Common.Size = NumThreadsPerScatter;
	Parameters.NumScatters = NumScatters;

	check(bFloat4Buffer || ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER);

	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER)
	{
		if (NumBytesPerThread == 16)
		{
			ResourceTypeEnum = EByteBufferResourceType::Uint4Aligned_Buffer;
		}
		else
		{
			ResourceTypeEnum = EByteBufferResourceType::Uint_Buffer;
		}
		Parameters.UploadByteAddressBuffer = UploadBuffer.SRV;
		Parameters.ScatterByteAddressBuffer = ScatterBuffer.SRV;
		Parameters.Common.DstByteAddressBuffer = DstBuffer.UAV;
	}
	else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::STRUCTURED_BUFFER)
	{
		ResourceTypeEnum = EByteBufferResourceType::Float4_StructuredBuffer;

		Parameters.UploadStructuredBuffer = UploadBuffer.SRV;
		Parameters.ScatterStructuredBuffer = ScatterBuffer.SRV;
		Parameters.Common.DstStructuredBuffer = DstBuffer.UAV;
	}
	else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BUFFER)
	{
		ResourceTypeEnum = EByteBufferResourceType::Float4_Buffer;

		Parameters.UploadStructuredBuffer = UploadBuffer.SRV;
		Parameters.ScatterStructuredBuffer = ScatterBuffer.SRV;
		Parameters.Common.DstBuffer = DstBuffer.UAV;
	}
	else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::TEXTURE)
	{
		ResourceTypeEnum = EByteBufferResourceType::Float4_Texture;

		Parameters.UploadStructuredBuffer = UploadBuffer.SRV;
		Parameters.ScatterStructuredBuffer = ScatterBuffer.SRV;
		Parameters.Common.DstTexture = DstBuffer.UAV;

		Parameters.Common.Float4sPerLine = CalculateFloat4sPerLine();
	}

	FByteBufferShader::FPermutationDomain PermutationVector;
	PermutationVector.Set<FByteBufferShader::ResourceTypeDim>((int)ResourceTypeEnum);

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FScatterCopyCS>(PermutationVector);

	RHICmdList.BeginUAVOverlap(DstBuffer.UAV);

	for (uint32 LoopIdx = 0; LoopIdx < NumLoops; ++LoopIdx)
	{
		Parameters.Common.SrcOffset = LoopIdx * (uint32)GMaxComputeDispatchDimension * ThreadGroupSize;

		uint32 LoopNumDispatch = FMath::Min(NumDispatches - LoopIdx * (uint32)GMaxComputeDispatchDimension, (uint32)GMaxComputeDispatchDimension);

		FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FIntVector(LoopNumDispatch, 1, 1));
	}

	RHICmdList.EndUAVOverlap(DstBuffer.UAV);

	// We need to unbind shader SRVs in this case, because scatter upload buffers are sometimes used more than once in a
	// frame, and this can cause rendering bugs on D3D12, where the driver fails to update the bound SRV with new data.
	UnsetShaderSRVs(RHICmdList, ComputeShader, ComputeShader.GetComputeShader());

	if (bFlush)
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}
}

template RENDERCORE_API void MemsetResource<FRWBufferStructured>(FRHICommandList& RHICmdList, const FRWBufferStructured& DstBuffer, const FMemsetResourceParams& Params);
template RENDERCORE_API void MemsetResource<FRWByteAddressBuffer>(FRHICommandList& RHICmdList, const FRWByteAddressBuffer& DstBuffer, const FMemsetResourceParams& Params);

template RENDERCORE_API void MemcpyResource<FTextureRWBuffer2D>(FRHICommandList& RHICmdList, const FTextureRWBuffer2D& DstBuffer, const FTextureRWBuffer2D& SrcBuffer, const FMemcpyResourceParams& Params, bool bAlreadyInUAVOverlap);
template RENDERCORE_API void MemcpyResource<FRWBuffer>(FRHICommandList& RHICmdList, const FRWBuffer& DstBuffer, const FRWBuffer& SrcBuffer, const FMemcpyResourceParams& Params, bool bAlreadyInUAVOverlap);
template RENDERCORE_API void MemcpyResource<FRWBufferStructured>(FRHICommandList& RHICmdList, const FRWBufferStructured& DstBuffer, const FRWBufferStructured& SrcBuffer, const FMemcpyResourceParams& Params, bool bAlreadyInUAVOverlap);
template RENDERCORE_API void MemcpyResource<FRWByteAddressBuffer>(FRHICommandList& RHICmdList, const FRWByteAddressBuffer& DstBuffer, const FRWByteAddressBuffer& SrcBuffer, const FMemcpyResourceParams& Params, bool bAlreadyInUAVOverlap);

template RENDERCORE_API void FScatterUploadBuffer::ResourceUploadTo<FTextureRWBuffer2D>(FRHICommandList& RHICmdList, const FTextureRWBuffer2D& DstBuffer, bool bFlush);
template RENDERCORE_API void FScatterUploadBuffer::ResourceUploadTo<FRWBuffer>(FRHICommandList& RHICmdList, const FRWBuffer& DstBuffer, bool bFlush);
template RENDERCORE_API void FScatterUploadBuffer::ResourceUploadTo<FRWBufferStructured>(FRHICommandList& RHICmdList, const FRWBufferStructured& DstBuffer, bool bFlush);
template RENDERCORE_API void FScatterUploadBuffer::ResourceUploadTo<FRWByteAddressBuffer>(FRHICommandList& RHICmdList, const FRWByteAddressBuffer& DstBuffer, bool bFlush);