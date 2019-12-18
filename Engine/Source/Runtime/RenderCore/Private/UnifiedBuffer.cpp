// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	DECLARE_GLOBAL_SHADER( FByteBufferShader );

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
			return FDataDrivenShaderPlatformInfo::GetInfo(Parameters.Platform).bSupportsByteBufferComputeShaders || Parameters.Platform == SP_PS4 || Parameters.Platform == SP_PCD3D_SM5 || Parameters.Platform == SP_XBOXONE_D3D12;
		}
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER( uint32, Value )
		SHADER_PARAMETER( uint32, Size )
		SHADER_PARAMETER( uint32, SrcOffset )
		SHADER_PARAMETER( uint32, DstOffset )

		SHADER_PARAMETER_SRV( StructuredBuffer< float4 >,	SrcStructuredBuffer)
		SHADER_PARAMETER_UAV( RWStructuredBuffer< float4 >, DstStructuredBuffer )

		SHADER_PARAMETER_SRV( ByteAddressBuffer,			SrcByteAddressBuffer )
		SHADER_PARAMETER_UAV( RWByteAddressBuffer,			DstByteAddressBuffer )
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
};
IMPLEMENT_GLOBAL_SHADER( FMemcpyBufferCS, "/Engine/Private/ByteBuffer.usf", "MemcpyBufferCS", SF_Compute );

class FScatterCopyCS : public FByteBufferShader
{
	DECLARE_GLOBAL_SHADER( FScatterCopyCS );
	SHADER_USE_PARAMETER_STRUCT( FScatterCopyCS, FByteBufferShader );

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE( FByteBufferShader::FParameters, Common )
		
		SHADER_PARAMETER( uint32, NumScatters )
		SHADER_PARAMETER_SRV( StructureBuffer< float4 >,	ScatterStructuredBuffer )
		SHADER_PARAMETER_SRV( ByteAddressBuffer,			ScatterByteAddressBuffer )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER( FScatterCopyCS, "/Engine/Private/ByteBuffer.usf", "ScatterCopyCS", SF_Compute );

class FMemcpyTextureToTextureCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER( FMemcpyTextureToTextureCS );
	SHADER_USE_PARAMETER_STRUCT( FMemcpyTextureToTextureCS, FGlobalShader );

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER( uint32, Size )
		SHADER_PARAMETER( uint32, Float4sPerLine )
		SHADER_PARAMETER( uint32, SrcOffset )
		SHADER_PARAMETER( uint32, DstOffset )
		SHADER_PARAMETER( uint32, DstHeight )
		SHADER_PARAMETER_SRV( StructuredBuffer< float4 >,	SrcTexture )
		SHADER_PARAMETER_UAV( RWTexture2D<float4>,			DstTexture )
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation( const FGlobalShaderPermutationParameters& Parameters )
	{
		return RHISupportsComputeShaders( Parameters.Platform );
	}
};

IMPLEMENT_GLOBAL_SHADER( FMemcpyTextureToTextureCS, "/Engine/Private/ByteBuffer.usf", "MemcpyTextureToTextureCS", SF_Compute );


class FScatterCopyTextureCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER( FScatterCopyTextureCS );
	SHADER_USE_PARAMETER_STRUCT( FScatterCopyTextureCS, FGlobalShader );

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER( uint32, Size )
		SHADER_PARAMETER( uint32, Float4sPerLine )
		SHADER_PARAMETER( uint32, NumScatters )
		SHADER_PARAMETER( uint32, DstHeight )
		SHADER_PARAMETER_SRV( StructuredBuffer< float4 >, SrcStructuredBuffer )
		SHADER_PARAMETER_SRV( StructuredBuffer< uint >, ScatterStructuredBuffer )
		SHADER_PARAMETER_UAV( RWTexture2D<float4>, DstTexture )
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}
	
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("FLOAT4_BUFFER"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER( FScatterCopyTextureCS, "/Engine/Private/ByteBuffer.usf", "ScatterCopyTextureCS", SF_Compute );


// Must be aligned to 4 bytes
void MemsetBuffer( FRHICommandList& RHICmdList, const FRWByteAddressBuffer& DstBuffer, uint32 Value, uint32 NumBytes, uint32 DstOffset )
{
	check( (NumBytes & 3) == 0 );
	check( (DstOffset & 3) == 0 );

	FMemsetBufferCS::FParameters Parameters;
	Parameters.Value				= Value;
	Parameters.Size					= NumBytes / 4;
	Parameters.DstOffset			= DstOffset / 4;
	Parameters.DstByteAddressBuffer	= DstBuffer.UAV;

	FMemsetBufferCS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FMemsetBufferCS::FFloat4BufferDim >( false );

	auto ShaderMap = GetGlobalShaderMap( GMaxRHIFeatureLevel );
	auto ComputeShader = ShaderMap->GetShader< FMemsetBufferCS >( PermutationVector );

	FComputeShaderUtils::Dispatch( RHICmdList, ComputeShader, Parameters, FIntVector( FMath::DivideAndRoundUp( NumBytes / 4, 4 * 64u ), 1, 1 ) );
}

// Must be aligned to 4 bytes
void MemcpyBuffer( FRHICommandList& RHICmdList, const FRWByteAddressBuffer& DstBuffer, const FRWByteAddressBuffer& SrcBuffer, uint32 NumBytes, uint32 DstOffset, uint32 SrcOffset )
{
	check( (NumBytes & 3) == 0 );
	check( (SrcOffset & 3) == 0 );
	check( (DstOffset & 3) == 0 );

	FMemcpyBufferCS::FParameters Parameters;
	Parameters.Size			= NumBytes / 4;
	Parameters.SrcOffset	= SrcOffset / 4;
	Parameters.DstOffset	= DstOffset / 4;
	Parameters.SrcByteAddressBuffer	= SrcBuffer.SRV;
	Parameters.DstByteAddressBuffer	= DstBuffer.UAV;

	FMemcpyBufferCS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FMemcpyBufferCS::FFloat4BufferDim >( false );

	auto ShaderMap = GetGlobalShaderMap( GMaxRHIFeatureLevel );
	auto ComputeShader = ShaderMap->GetShader< FMemcpyBufferCS >( PermutationVector );

	FComputeShaderUtils::Dispatch( RHICmdList, ComputeShader, Parameters, FIntVector( FMath::DivideAndRoundUp( NumBytes / 4, 4 * 64u ), 1, 1 ) );
}

// Must be aligned to 4 bytes
bool ResizeBuffer( FRHICommandList& RHICmdList, FRWByteAddressBuffer& Buffer, uint32 NumBytes, const TCHAR* DebugName )
{
	check( (NumBytes & 3) == 0 );

	if( Buffer.NumBytes == 0 )
	{
		Buffer.Initialize( NumBytes,0, DebugName );
	}
	else if( NumBytes != Buffer.NumBytes )
	{
		FRWByteAddressBuffer NewBuffer;
		NewBuffer.Initialize( NumBytes, 0, DebugName );

		RHICmdList.TransitionResource( EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, Buffer.UAV );

		// Copy data to new buffer
		uint32 CopyBytes = FMath::Min( NumBytes, Buffer.NumBytes );
		MemcpyBuffer( RHICmdList, NewBuffer, Buffer, CopyBytes );

		Buffer = NewBuffer;
		return true;
	}

	return false;
}

// Must be aligned to 16 bytes
void MemsetBufferFloat4( FRHICommandList& RHICmdList, const FRWBufferStructured& DstBuffer, uint32 Value, uint32 NumBytes, uint32 DstOffset )
{
	check( (NumBytes & 15) == 0 );
	check( (DstOffset & 15) == 0 );

	FMemsetBufferCS::FParameters Parameters;
	Parameters.Value				= Value;
	Parameters.Size					= NumBytes / 16;
	Parameters.DstOffset			= DstOffset / 16;
	Parameters.DstStructuredBuffer	= DstBuffer.UAV;

	FMemsetBufferCS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FMemsetBufferCS::FFloat4BufferDim >( true );

	auto ShaderMap = GetGlobalShaderMap( GMaxRHIFeatureLevel );
	auto ComputeShader = ShaderMap->GetShader< FMemsetBufferCS >( PermutationVector );

	FComputeShaderUtils::Dispatch( RHICmdList, ComputeShader, Parameters, FIntVector( FMath::DivideAndRoundUp( NumBytes / 16, 64u ), 1, 1 ) );
}

// Must be aligned to 16 bytes
void MemcpyBufferFloat4( FRHICommandList& RHICmdList, const FRWBufferStructured& DstBuffer, const FRWBufferStructured& SrcBuffer, uint32 NumBytes, uint32 DstOffset, uint32 SrcOffset )
{
	check( (NumBytes & 15) == 0 );
	check( (SrcOffset & 15) == 0 );
	check( (DstOffset & 15) == 0 );

	FMemcpyBufferCS::FParameters Parameters;
	Parameters.Size			= NumBytes / 16;
	Parameters.SrcOffset	= SrcOffset / 16;
	Parameters.DstOffset	= DstOffset / 16;
	Parameters.SrcStructuredBuffer	= SrcBuffer.SRV;
	Parameters.DstStructuredBuffer	= DstBuffer.UAV;

	FMemcpyBufferCS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FMemcpyBufferCS::FFloat4BufferDim >( true );

	auto ShaderMap = GetGlobalShaderMap( GMaxRHIFeatureLevel );
	auto ComputeShader = ShaderMap->GetShader< FMemcpyBufferCS >( PermutationVector );

	FComputeShaderUtils::Dispatch( RHICmdList, ComputeShader, Parameters, FIntVector( FMath::DivideAndRoundUp( NumBytes / 16, 64u ), 1, 1 ) );
}

// Must be aligned to 16 bytes
bool ResizeBufferFloat4(FRHICommandList& RHICmdList, FRWBufferStructured& Buffer, uint32 NumBytes, const TCHAR* InDebugName)
{
	check( (NumBytes & 15) == 0 );

	if( Buffer.NumBytes == 0 )
	{
		Buffer.Initialize( 16, NumBytes / 16, 0, InDebugName );
	}
	else if( NumBytes != Buffer.NumBytes )
	{
		FRWBufferStructured NewBuffer;
		NewBuffer.Initialize( 16, NumBytes / 16, 0, InDebugName );

		// Copy data to new buffer
		uint32 CopyBytes = FMath::Min( NumBytes, Buffer.NumBytes );
		MemcpyBufferFloat4( RHICmdList, NewBuffer, Buffer, CopyBytes );

		Buffer = NewBuffer;
		return true;
	}

	return false;
}


void MemcpyTextureToTexture(FRHICommandList& RHICmdList, const FTextureRWBuffer2D& SrcTexture, const FTextureRWBuffer2D& DstTexture, uint32 SrcOffset, uint32 DstOffset, uint32 NumBytes, uint32 BytesPerLine)
{
	check( ( SrcOffset & 15 ) == 0 );
	check( ( DstOffset & 15 ) == 0 );
	check( ( NumBytes & 15 ) == 0 );

	uint32 NumFloat4s = NumBytes >> 4;
	uint32 Float4sPerLine = BytesPerLine >> 4;
	check( Float4sPerLine <= DstTexture.Buffer->GetSizeX() );

	FMemcpyTextureToTextureCS::FParameters Parameters;
	Parameters.Size				= NumFloat4s;
	Parameters.Float4sPerLine	= BytesPerLine >> 4;
	Parameters.SrcOffset		= SrcOffset >> 4;
	Parameters.DstOffset		= DstOffset >> 4;
	Parameters.DstHeight		= DstTexture.Buffer->GetSizeY();
	Parameters.SrcTexture		= SrcTexture.SRV;
	Parameters.DstTexture		= DstTexture.UAV;

	auto ComputeShader = GetGlobalShaderMap( GMaxRHIFeatureLevel )->GetShader< FMemcpyTextureToTextureCS >(  );
	FComputeShaderUtils::Dispatch( RHICmdList, ComputeShader, Parameters, FIntVector( FMath::DivideAndRoundUp( NumFloat4s, 64u ), 1, 1 ) );
}

bool ResizeTexture(FRHICommandList& RHICmdList, FTextureRWBuffer2D& Texture, uint32 NumBytes, uint32 BytesPerLine)
{
	check( ( NumBytes & 15 ) == 0 );
	check( ( BytesPerLine & 15 ) == 0 );
	uint32 Float4sPerLine = BytesPerLine >> 4;

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
		MemcpyTextureToTexture(RHICmdList, Texture, NewTexture, 0, 0, NumBytes, BytesPerLine);
		Texture = NewTexture;
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

void FScatterUploadBuffer::UploadToBuffer( FRHICommandList& RHICmdList, FRHIUnorderedAccessView* DstBufferUAV, bool bFlush )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( "FScatterUploadBuffer::UploadTo" );

	const uint32 TypeSize = bFloat4Buffer ? 16 : 4;
	const uint32 Usage = bFloat4Buffer ? 0 : BUF_ByteAddressBuffer;

	ScatterData = nullptr;
	UploadData = nullptr;

	RHIUnlockStructuredBuffer( ScatterBuffer.Buffer );
	RHIUnlockStructuredBuffer( UploadBuffer.Buffer );

	if( NumScatters == 0 )
	{
		return;
	}

	RHICmdList.TransitionResource( EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, DstBufferUAV );

	uint32 NumBytesPerThread = ( NumBytesPerElement & 15 ) == 0 ? 16 : 4;
	uint32 NumThreadsPerScatter = NumBytesPerElement / NumBytesPerThread;
	uint32 NumThreads = NumScatters * NumThreadsPerScatter;

	FScatterCopyCS::FParameters Parameters;
	Parameters.Common.Value = 0;
	Parameters.Common.Size = NumThreadsPerScatter;
	Parameters.Common.SrcOffset = 0;
	Parameters.Common.DstOffset = 0;
	Parameters.NumScatters = NumScatters;
	
	if (bFloat4Buffer)
	{
		Parameters.Common.SrcStructuredBuffer = UploadBuffer.SRV;
		Parameters.Common.DstStructuredBuffer = DstBufferUAV;
		Parameters.ScatterStructuredBuffer = ScatterBuffer.SRV;
	}
	else
	{
		Parameters.Common.SrcByteAddressBuffer = UploadBuffer.SRV;
		Parameters.Common.DstByteAddressBuffer = DstBufferUAV;
		Parameters.ScatterByteAddressBuffer = ScatterBuffer.SRV;
	}
	

	FScatterCopyCS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FScatterCopyCS::FFloat4BufferDim >( bFloat4Buffer );
	PermutationVector.Set< FScatterCopyCS::FUint4AlignedDim >( NumBytesPerThread == 16 );

	auto ComputeShader = GetGlobalShaderMap( GMaxRHIFeatureLevel )->GetShader< FScatterCopyCS >( PermutationVector );
	FComputeShaderUtils::Dispatch( RHICmdList, ComputeShader, Parameters, FIntVector( FMath::DivideAndRoundUp( NumThreads, 64u ), 1, 1 ) );
	
	if( bFlush )
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush( EImmediateFlushType::DispatchToRHIThread );
	}
}

void FScatterUploadBuffer::UploadToTexture(FRHICommandList& RHICmdList, FTextureRWBuffer2D& DstTexture, uint32 BytesPerLine, bool bFlush)
{
	RHIUnlockStructuredBuffer(ScatterBuffer.Buffer);
	RHIUnlockStructuredBuffer(UploadBuffer.Buffer);

	check( bFloat4Buffer );

	ScatterData = nullptr;
	UploadData = nullptr;

	uint32 NumThreadsPerScatter = NumBytesPerElement / 16;
	uint32 NumThreads = NumScatters * NumThreadsPerScatter;

	uint32 Float4sPerLine = BytesPerLine >> 4;
	check( Float4sPerLine <= DstTexture.Buffer->GetSizeX() );

	FScatterCopyTextureCS::FParameters Parameters;
	Parameters.Size				= NumThreadsPerScatter;
	Parameters.Float4sPerLine	= BytesPerLine >> 4;
	Parameters.NumScatters		= NumScatters;
	Parameters.DstHeight		= DstTexture.Buffer->GetSizeY();
	Parameters.SrcStructuredBuffer		= UploadBuffer.SRV;
	Parameters.ScatterStructuredBuffer	= ScatterBuffer.SRV;
	Parameters.DstTexture		= DstTexture.UAV;

	auto ComputeShader = GetGlobalShaderMap( GMaxRHIFeatureLevel )->GetShader< FScatterCopyTextureCS >();
	FComputeShaderUtils::Dispatch( RHICmdList, ComputeShader, Parameters, FIntVector( FMath::DivideAndRoundUp( NumThreads, 64u ), 1, 1 ) );

	if( bFlush )
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush( EImmediateFlushType::DispatchToRHIThread );
	}
}
