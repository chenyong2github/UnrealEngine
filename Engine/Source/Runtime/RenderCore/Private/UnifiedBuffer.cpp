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

class FByteBufferShader : public FGlobalShader
{
	DECLARE_INLINE_TYPE_LAYOUT( FByteBufferShader, NonVirtual );

	FByteBufferShader() {}
	FByteBufferShader( const ShaderMetaType::CompiledShaderInitializerType& Initializer )
		: FGlobalShader( Initializer )
	{}

	class FFloat4BufferDim : SHADER_PERMUTATION_BOOL("FLOAT4_BUFFER");
	class FUint4AlignedDim : SHADER_PERMUTATION_BOOL("UINT4_ALIGNED");

	using FPermutationDomain = TShaderPermutationDomain<
		FFloat4BufferDim,
		FUint4AlignedDim
	>;

	static bool ShouldCompilePermutation( const FGlobalShaderPermutationParameters& Parameters )
	{
		FPermutationDomain PermutationVector( Parameters.PermutationId );

		if( PermutationVector.Get< FFloat4BufferDim >() )
		{
			return RHISupportsComputeShaders(Parameters.Platform);
		}
		else
		{
			/*
			return RHISupportsComputeShaders(Parameters.Platform)
				&& FDataDrivenShaderPlatformInfo::GetInfo(Parameters.Platform).bSupportsByteBufferComputeShaders;
				*/
			// TODO: Workaround for FDataDrivenShaderPlatformInfo::GetInfo not being properly filled out yet.
			return FDataDrivenShaderPlatformInfo::GetSupportsByteBufferComputeShaders(Parameters.Platform) || Parameters.Platform == SP_PCD3D_SM5;
		}
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("FLOAT4_TEXTURE"), false);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER(uint32, Value)
		SHADER_PARAMETER(uint32, Size)
		SHADER_PARAMETER(uint32, SrcOffset)
		SHADER_PARAMETER(uint32, DstOffset)
		SHADER_PARAMETER(uint32, Float4sPerLine)
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

class FMemcpyBufferCS : public FByteBufferShader
{
	DECLARE_GLOBAL_SHADER( FMemcpyBufferCS );
	SHADER_USE_PARAMETER_STRUCT( FMemcpyBufferCS, FByteBufferShader );
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FByteBufferShader::FParameters, Common)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, SrcByteAddressBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SrcStructuredBuffer)
		SHADER_PARAMETER_SRV(Texture2D<float4>, SrcTexture)
	END_SHADER_PARAMETER_STRUCT()


	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FByteBufferShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("FLOAT4_TEXTURE"), false);
	}
};
IMPLEMENT_GLOBAL_SHADER( FMemcpyBufferCS, "/Engine/Private/ByteBuffer.usf", "MemcpyCS", SF_Compute );

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

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FByteBufferShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("FLOAT4_TEXTURE"), false);
	}
};
IMPLEMENT_GLOBAL_SHADER( FScatterCopyCS, "/Engine/Private/ByteBuffer.usf", "ScatterCopyCS", SF_Compute );

class FMemcpyTextureToTextureCS : public FMemcpyBufferCS
{
	DECLARE_GLOBAL_SHADER( FMemcpyTextureToTextureCS );

	SHADER_USE_PARAMETER_STRUCT( FMemcpyTextureToTextureCS, FMemcpyBufferCS);


	static bool ShouldCompilePermutation( const FGlobalShaderPermutationParameters& Parameters )
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		return RHISupportsComputeShaders( Parameters.Platform );
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FByteBufferShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("FLOAT4_BUFFER"), false);
		OutEnvironment.SetDefine(TEXT("UINT4_ALIGNED"), false);
		OutEnvironment.SetDefine(TEXT("FLOAT4_TEXTURE"), true);
	}
};

IMPLEMENT_GLOBAL_SHADER( FMemcpyTextureToTextureCS, "/Engine/Private/ByteBuffer.usf", "MemcpyCS", SF_Compute );


class FScatterCopyTextureCS : public FScatterCopyCS
{
	DECLARE_GLOBAL_SHADER( FScatterCopyTextureCS );

	SHADER_USE_PARAMETER_STRUCT( FScatterCopyTextureCS, FScatterCopyCS);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		return RHISupportsComputeShaders(Parameters.Platform);
	}
	
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FByteBufferShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("FLOAT4_BUFFER"), false);
		OutEnvironment.SetDefine(TEXT("UINT4_ALIGNED"), false);
		OutEnvironment.SetDefine(TEXT("FLOAT4_TEXTURE"), true);
	}
};

IMPLEMENT_GLOBAL_SHADER( FScatterCopyTextureCS, "/Engine/Private/ByteBuffer.usf", "ScatterCopyCS", SF_Compute );

enum class EResourceType
{
	BUFFER,
	BYTEBUFFER,
	TEXTURE
};

template<typename ResourceType>
struct ResourceTypeTraits;

template<>
struct ResourceTypeTraits<FRWBufferStructured>
{
	typedef FScatterCopyCS FScatterCS;
	typedef FMemcpyBufferCS FMemcypCS;
	typedef FMemsetBufferCS FMemsetCS;
	static const EResourceType Type = EResourceType::BUFFER;
};

template<>
struct ResourceTypeTraits<FTextureRWBuffer2D>
{
	typedef FScatterCopyTextureCS FScatterCS;
	typedef FMemcpyTextureToTextureCS FMemcypCS;
	static const EResourceType Type = EResourceType::TEXTURE;
};

template<>
struct ResourceTypeTraits<FRWByteAddressBuffer>
{
	typedef FScatterCopyCS FScatterCS;
	typedef FMemcpyBufferCS FMemcypCS;
	typedef FMemsetBufferCS FMemsetCS;
	static const EResourceType Type = EResourceType::BYTEBUFFER;
};

template<typename ResourceType>
void MemsetResource(FRHICommandList& RHICmdList, const ResourceType& DstBuffer, uint32 Value, uint32 NumBytes, uint32 DstOffset)
{
	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::TEXTURE)
	{
		check(false && TEXT("TEXTURE memset not yet implemented"));
		return;
	}

	uint32 DivisorAlignment = ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER ? 4 : 16;
	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER)
	{
		check((NumBytes & 3) == 0);
		check((DstOffset & 3) == 0);
	}
	else
	{
		check((DstOffset & 15) == 0);
		check((NumBytes & 15) == 0);
	}

	typename ResourceTypeTraits<ResourceType>::FMemsetCS::FParameters Parameters;
	Parameters.Value = Value;
	Parameters.Size = NumBytes / DivisorAlignment;
	Parameters.DstOffset = DstOffset / DivisorAlignment;
	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER)
	{
		Parameters.DstByteAddressBuffer = DstBuffer.UAV;
	}
	else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BUFFER)
	{
		Parameters.DstStructuredBuffer = DstBuffer.UAV;
	}

	typename ResourceTypeTraits<ResourceType>::FMemsetCS::FPermutationDomain PermutationVector;
	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER)
	{
		PermutationVector.template Set<typename ResourceTypeTraits<ResourceType>::FMemsetCS::FFloat4BufferDim >(false);
	}
	else
	{
		PermutationVector.template Set<typename ResourceTypeTraits<ResourceType>::FMemsetCS::FFloat4BufferDim >(true);
	}

	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<typename ResourceTypeTraits<ResourceType>::FMemsetCS >(PermutationVector);

	FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FIntVector(FMath::DivideAndRoundUp(NumBytes / 16, 64u), 1, 1));
}

template<typename ResourceType>
void MemcpyResource(FRHICommandList& RHICmdList, const ResourceType& DstBuffer, const ResourceType& SrcBuffer, uint32 NumBytes, uint32 DstOffset, uint32 SrcOffset)
{
	uint32 DivisorAlignment = ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER ? 4 : 16;
	if(ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER)
	{
		check((NumBytes & 3) == 0);
		check((SrcOffset & 3) == 0);
		check((DstOffset & 3) == 0);
	}
	else
	{
		check((SrcOffset & 15) == 0);
		check((DstOffset & 15) == 0);
		check((NumBytes & 15) == 0);
	}

	typename ResourceTypeTraits<ResourceType>::FMemcypCS::FParameters Parameters;
	Parameters.Common.Size = NumBytes / DivisorAlignment;
	Parameters.Common.SrcOffset = SrcOffset / DivisorAlignment;
	Parameters.Common.DstOffset = DstOffset / DivisorAlignment;
	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER)
	{
		Parameters.SrcByteAddressBuffer = SrcBuffer.SRV;
		Parameters.Common.DstByteAddressBuffer = DstBuffer.UAV;
	}
	else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BUFFER)
	{
		Parameters.SrcStructuredBuffer = SrcBuffer.SRV;
		Parameters.Common.DstStructuredBuffer = DstBuffer.UAV;
	}
	else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::TEXTURE)
	{
		Parameters.SrcTexture = SrcBuffer.SRV;
		Parameters.Common.DstTexture = DstBuffer.UAV;
	}

	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::TEXTURE)
	{
		uint16 PrimitivesPerTextureLine = FMath::Min((int32)MAX_uint16, (int32)GMaxTextureDimensions) / (FScatterUploadBuffer::PrimitiveDataStrideInFloat4s);
		Parameters.Common.Float4sPerLine = PrimitivesPerTextureLine * FScatterUploadBuffer::PrimitiveDataStrideInFloat4s;
	}

	typename ResourceTypeTraits<ResourceType>::FMemcypCS::FPermutationDomain PermutationVector;
	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER)
	{
		PermutationVector.template Set<typename ResourceTypeTraits<ResourceType>::FMemcypCS::FFloat4BufferDim >(false);
	}
	else
	{
		PermutationVector.template Set<typename ResourceTypeTraits<ResourceType>::FMemcypCS::FFloat4BufferDim >(true);
	}

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<typename ResourceTypeTraits<ResourceType>::FMemcypCS >(PermutationVector);

	FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FIntVector(FMath::DivideAndRoundUp(NumBytes / 16, 64u), 1, 1));
}

template<>
RENDERCORE_API bool ResizeResourceIfNeeded<FTextureRWBuffer2D>(FRHICommandList& RHICmdList, FTextureRWBuffer2D& Texture, uint32 NumBytes, const TCHAR* DebugName)
{
	check((NumBytes & 15) == 0);
	uint16 PrimitivesPerTextureLine = FMath::Min((int32)MAX_uint16, (int32)GMaxTextureDimensions) / (FScatterUploadBuffer::PrimitiveDataStrideInFloat4s);
	uint32 Float4sPerLine = PrimitivesPerTextureLine * FScatterUploadBuffer::PrimitiveDataStrideInFloat4s;
	uint32 BytesPerLine = Float4sPerLine * 16;

	EPixelFormat BufferFormat = PF_A32B32G32R32F;
	uint32 BytesPerElement = GPixelFormats[BufferFormat].BlockBytes;

	uint32 NumLines = (NumBytes + BytesPerLine - 1) / BytesPerLine;

	if (Texture.NumBytes == 0)
	{
		Texture.Initialize(BytesPerElement, Float4sPerLine, NumLines, PF_A32B32G32R32F, TexCreate_RenderTargetable | TexCreate_UAV);
	}
	else if ((NumLines * Float4sPerLine * BytesPerElement) != Texture.NumBytes)
	{
		FTextureRWBuffer2D NewTexture;
		NewTexture.Initialize(BytesPerElement, Float4sPerLine, NumLines, PF_A32B32G32R32F, TexCreate_RenderTargetable | TexCreate_UAV);
		MemcpyResource(RHICmdList, NewTexture, Texture, NumBytes, 0, 0);
		Texture = NewTexture;
		return true;
	}

	return false;
}

template<>
RENDERCORE_API bool ResizeResourceIfNeeded<FRWBufferStructured>(FRHICommandList& RHICmdList, FRWBufferStructured& Buffer, uint32 NumBytes, const TCHAR* DebugName)
{
	check((NumBytes & 15) == 0);

	if (Buffer.NumBytes == 0)
	{
		Buffer.Initialize(16, NumBytes / 16, 0, DebugName);
	}
	else if (NumBytes != Buffer.NumBytes)
	{
		FRWBufferStructured NewBuffer;
		NewBuffer.Initialize(16, NumBytes / 16, 0, DebugName);

		// Copy data to new buffer
		uint32 CopyBytes = FMath::Min(NumBytes, Buffer.NumBytes);
		MemcpyResource(RHICmdList, NewBuffer, Buffer, CopyBytes);

		Buffer = NewBuffer;
		return true;
	}

	return false;
}

template<>
RENDERCORE_API bool ResizeResourceIfNeeded<FRWByteAddressBuffer>(FRHICommandList& RHICmdList, FRWByteAddressBuffer& Buffer, uint32 NumBytes, const TCHAR* DebugName)
{
	check((NumBytes & 3) == 0);

	if (Buffer.NumBytes == 0)
	{
		Buffer.Initialize(NumBytes, 0, DebugName);
	}
	else if (NumBytes != Buffer.NumBytes)
	{
		FRWByteAddressBuffer NewBuffer;
		NewBuffer.Initialize(NumBytes, 0, DebugName);

		RHICmdList.Transition(FRHITransitionInfo(Buffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
		RHICmdList.Transition(FRHITransitionInfo(NewBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));

		// Copy data to new buffer
		uint32 CopyBytes = FMath::Min(NumBytes, Buffer.NumBytes);
		MemcpyResource(RHICmdList, NewBuffer, Buffer, CopyBytes);

		Buffer = NewBuffer;
		return true;
	}

	return false;
}

void FScatterUploadBuffer::Init( uint32 NumElements, uint32 InNumBytesPerElement, bool bInFloat4Buffer, const TCHAR* DebugName )
{
	check( ScatterData == nullptr );
	check( UploadData == nullptr );

	NumScatters = 0;
	MaxScatters = NumElements;
	NumScattersAllocated = FMath::RoundUpToPowerOfTwo( NumElements );
	NumBytesPerElement = InNumBytesPerElement;
	bFloat4Buffer = bInFloat4Buffer;

	const uint32 Usage = bInFloat4Buffer ? 0 : BUF_ByteAddressBuffer;
	const uint32 TypeSize = bInFloat4Buffer ? 16 : 4;

	uint32 ScatterBytes = NumElements * sizeof( uint32 );

	if( ScatterBytes > ScatterBuffer.NumBytes )
	{
		// Resize Scatter Buffer
		ScatterBuffer.Release();
		ScatterBuffer.NumBytes = NumScattersAllocated * sizeof( uint32 );

		FRHIResourceCreateInfo CreateInfo(DebugName);
		ScatterBuffer.Buffer = RHICreateStructuredBuffer( sizeof( uint32 ), ScatterBuffer.NumBytes, BUF_ShaderResource | BUF_Volatile | Usage, CreateInfo );
		ScatterBuffer.SRV = RHICreateShaderResourceView( ScatterBuffer.Buffer );
	}

	uint32 UploadBytes = NumElements * NumBytesPerElement;
	if( UploadBytes > UploadBuffer.NumBytes )
	{
		// Resize Upload Buffer
		UploadBuffer.Release();
		UploadBuffer.NumBytes = NumScattersAllocated * NumBytesPerElement;

		FRHIResourceCreateInfo CreateInfo(DebugName);
		UploadBuffer.Buffer = RHICreateStructuredBuffer( TypeSize, UploadBuffer.NumBytes, BUF_ShaderResource | BUF_Volatile | Usage, CreateInfo );
		UploadBuffer.SRV = RHICreateShaderResourceView( UploadBuffer.Buffer );
	}

	ScatterData = (uint32*)RHILockStructuredBuffer( ScatterBuffer.Buffer, 0, ScatterBytes, RLM_WriteOnly );
	UploadData = (uint8*)RHILockStructuredBuffer( UploadBuffer.Buffer, 0, UploadBytes, RLM_WriteOnly );
}

template<typename ResourceType>
void FScatterUploadBuffer::ResourceUploadTo(FRHICommandList& RHICmdList, ResourceType& DstBuffer, bool bFlush)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FScatterUploadBuffer::ResourceUploadTo);
	
	RHIUnlockStructuredBuffer(ScatterBuffer.Buffer);
	RHIUnlockStructuredBuffer(UploadBuffer.Buffer);

	ScatterData = nullptr;
	UploadData = nullptr;

	if (NumScatters == 0)
		return;

	constexpr uint32 ThreadGroupSize = 64u;
	uint32 NumBytesPerThread = (NumBytesPerElement & 15) == 0 ? 16 : 4;
	uint32 NumThreadsPerScatter = NumBytesPerElement / NumBytesPerThread;
	uint32 NumThreads = NumScatters * NumThreadsPerScatter;
	uint32 NumDispatches = FMath::DivideAndRoundUp(NumThreads, ThreadGroupSize);
	uint32 NumLoops = FMath::DivideAndRoundUp(NumDispatches, (uint32)GMaxComputeDispatchDimension);

	typename ResourceTypeTraits<ResourceType>::FScatterCS::FParameters Parameters;
	
	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::TEXTURE)
	{
		uint16 PrimitivesPerTextureLine = FMath::Min((int32)MAX_uint16, (int32)GMaxTextureDimensions) / (FScatterUploadBuffer::PrimitiveDataStrideInFloat4s);
		Parameters.Common.Float4sPerLine = PrimitivesPerTextureLine * FScatterUploadBuffer::PrimitiveDataStrideInFloat4s;;
	}

	Parameters.Common.Size = NumThreadsPerScatter;
	Parameters.NumScatters = NumScatters;

	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BYTEBUFFER)
	{
		Parameters.UploadByteAddressBuffer = UploadBuffer.SRV;
		Parameters.ScatterByteAddressBuffer = ScatterBuffer.SRV;
		Parameters.Common.DstByteAddressBuffer = DstBuffer.UAV;
	}
	else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::BUFFER)
	{
		Parameters.UploadStructuredBuffer = UploadBuffer.SRV;
		Parameters.ScatterStructuredBuffer = ScatterBuffer.SRV;
		Parameters.Common.DstStructuredBuffer = DstBuffer.UAV;
	}
	else if (ResourceTypeTraits<ResourceType>::Type == EResourceType::TEXTURE)
	{
		Parameters.UploadStructuredBuffer = UploadBuffer.SRV;
		Parameters.ScatterStructuredBuffer = ScatterBuffer.SRV;
		Parameters.Common.DstTexture = DstBuffer.UAV;
	}

	typename ResourceTypeTraits<ResourceType>::FMemcypCS::FPermutationDomain PermutationVector;
	if (ResourceTypeTraits<ResourceType>::Type == EResourceType::TEXTURE)
	{
		PermutationVector.template Set<typename ResourceTypeTraits<ResourceType>::FMemcypCS::FFloat4BufferDim >(false);
		PermutationVector.template Set<typename ResourceTypeTraits<ResourceType>::FMemcypCS::FUint4AlignedDim >(false);
	}
	else
	{
		PermutationVector.template Set< FScatterCopyCS::FFloat4BufferDim >(bFloat4Buffer);
		PermutationVector.template Set< FScatterCopyCS::FUint4AlignedDim >(NumBytesPerThread == 16);
	}

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<typename ResourceTypeTraits<ResourceType>::FScatterCS >(PermutationVector);
	for (uint32 LoopIdx = 0; LoopIdx < NumLoops; ++LoopIdx)
	{
		if (LoopIdx != 0)
		{
			RHICmdList.Transition(FRHITransitionInfo(DstBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::ERWNoBarrier));
		}

		Parameters.Common.SrcOffset = LoopIdx * (uint32)GMaxComputeDispatchDimension * ThreadGroupSize;

		uint32 LoopNumDispatch = FMath::Min(NumDispatches - LoopIdx * (uint32)GMaxComputeDispatchDimension, (uint32)GMaxComputeDispatchDimension);

		FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FIntVector(LoopNumDispatch, 1, 1));
	}

	if (bFlush)
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}
}

template RENDERCORE_API void MemsetResource< FRWBufferStructured>(FRHICommandList& RHICmdList, const FRWBufferStructured& DstBuffer, uint32 Value, uint32 NumBytes, uint32 DstOffset);
template RENDERCORE_API void MemsetResource< FRWByteAddressBuffer>(FRHICommandList& RHICmdList, const FRWByteAddressBuffer& DstBuffer, uint32 Value, uint32 NumBytes, uint32 DstOffset);
template RENDERCORE_API void MemcpyResource< FTextureRWBuffer2D>(FRHICommandList& RHICmdList, const FTextureRWBuffer2D& DstBuffer, const FTextureRWBuffer2D& SrcBuffer, uint32 NumBytes, uint32 DstOffset, uint32 SrcOffset);
template RENDERCORE_API void MemcpyResource< FRWBufferStructured>(FRHICommandList& RHICmdList, const FRWBufferStructured& DstBuffer, const FRWBufferStructured& SrcBuffer, uint32 NumBytes, uint32 DstOffset, uint32 SrcOffset);
template RENDERCORE_API void MemcpyResource< FRWByteAddressBuffer>(FRHICommandList& RHICmdList, const FRWByteAddressBuffer& DstBuffer, const FRWByteAddressBuffer& SrcBuffer, uint32 NumBytes, uint32 DstOffset, uint32 SrcOffset);
template RENDERCORE_API void FScatterUploadBuffer::ResourceUploadTo<FTextureRWBuffer2D>(FRHICommandList& RHICmdList, FTextureRWBuffer2D& DstBuffer, bool bFlush);
template RENDERCORE_API void FScatterUploadBuffer::ResourceUploadTo<FRWBufferStructured>(FRHICommandList& RHICmdList, FRWBufferStructured& DstBuffer, bool bFlush);
template RENDERCORE_API void FScatterUploadBuffer::ResourceUploadTo<FRWByteAddressBuffer>(FRHICommandList& RHICmdList, FRWByteAddressBuffer& DstBuffer, bool bFlush);