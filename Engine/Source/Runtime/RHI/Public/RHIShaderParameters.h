// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "RHIResources.h"

class FRHICommandList;
class FRHIComputeCommandList;

/** Compact representation of a bound shader parameter (read: value). Its offsets are for referencing their data in an associated blob. */
struct FRHIShaderParameter
{
	FRHIShaderParameter(uint16 InBufferIndex, uint16 InBaseIndex, uint16 InByteOffset, uint16 InByteSize)
		: BufferIndex(InBufferIndex)
		, BaseIndex(InBaseIndex)
		, ByteOffset(InByteOffset)
		, ByteSize(InByteSize)
	{
	}
	uint16 BufferIndex;
	uint16 BaseIndex;
	uint16 ByteOffset;
	uint16 ByteSize;
};

/** Compact representation of a bound resource parameter (Texture, SRV, UAV, SamplerState, or UniformBuffer) */
struct FRHIShaderParameterResource
{
	enum class EType : uint8
	{
		Texture,
		ResourceView,
		UnorderedAccessView,
		Sampler,
		UniformBuffer,
	};

	FRHIShaderParameterResource() = default;
	FRHIShaderParameterResource(EType InType, FRHIResource* InResource, uint16 InIndex)
		: Resource(InResource)
		, Index(InIndex)
		, Type(InType)
	{
	}
	FRHIShaderParameterResource(FRHITexture* InTexture, uint16 InIndex)
		: FRHIShaderParameterResource(FRHIShaderParameterResource::EType::Texture, InTexture, InIndex)
	{
	}
	FRHIShaderParameterResource(FRHIShaderResourceView* InView, uint16 InIndex)
		: FRHIShaderParameterResource(FRHIShaderParameterResource::EType::ResourceView, InView, InIndex)
	{
	}
	FRHIShaderParameterResource(FRHIUnorderedAccessView* InUAV, uint16 InIndex)
		: FRHIShaderParameterResource(FRHIShaderParameterResource::EType::UnorderedAccessView, InUAV, InIndex)
	{
	}
	FRHIShaderParameterResource(FRHISamplerState* InSamplerState, uint16 InIndex)
		: FRHIShaderParameterResource(FRHIShaderParameterResource::EType::Sampler, InSamplerState, InIndex)
	{
	}
	FRHIShaderParameterResource(FRHIUniformBuffer* InUniformBuffer, uint16 InIndex)
		: FRHIShaderParameterResource(FRHIShaderParameterResource::EType::UniformBuffer, InUniformBuffer, InIndex)
	{
	}

	FRHIResource* Resource = nullptr;
	uint16        Index = 0;
	EType         Type = EType::Texture;
};

/** Collection of parameters to set in the RHI. These parameters aren't bound to any specific shader until SetBatchedShaderParameters is called. */
struct FRHIBatchedShaderParameters
{
	TArray<uint8> ParametersData;
	TArray<FRHIShaderParameter> Parameters;
	TArray<FRHIShaderParameterResource> ResourceParameters;
	TArray<FRHIShaderParameterResource> BindlessParameters;

	inline bool HasParameters() const
	{
		return (Parameters.Num() + ResourceParameters.Num() + BindlessParameters.Num()) > 0;
	}

	void Reset()
	{
		ParametersData.Reset();
		Parameters.Reset();
		ResourceParameters.Reset();
		BindlessParameters.Reset();
	}

	FORCEINLINE_DEBUGGABLE void SetShaderParameter(uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
	{
		const int32 DestDataOffset = ParametersData.Num();
		ParametersData.Append((const uint8*)NewValue, NumBytes);
		Parameters.Emplace((uint16)BufferIndex, (uint16)BaseIndex, (uint16)DestDataOffset, (uint16)NumBytes);
	}

	FORCEINLINE_DEBUGGABLE void SetShaderUniformBuffer(uint32 Index, FRHIUniformBuffer* UniformBuffer)
	{
		ResourceParameters.Emplace(UniformBuffer, (uint16)Index);
	}

	FORCEINLINE_DEBUGGABLE void SetShaderTexture(uint32 Index, FRHITexture* Texture)
	{
		ResourceParameters.Emplace(Texture, (uint16)Index);
	}
	FORCEINLINE_DEBUGGABLE void SetShaderResourceViewParameter(uint32 Index, FRHIShaderResourceView* SRV)
	{
		ResourceParameters.Emplace(SRV, (uint16)Index);
	}
	FORCEINLINE_DEBUGGABLE void SetShaderSampler(uint32 Index, FRHISamplerState* State)
	{
		ResourceParameters.Emplace(State, (uint16)Index);
	}
	FORCEINLINE_DEBUGGABLE void SetUAVParameter(uint32 Index, FRHIUnorderedAccessView* UAV)
	{
		ResourceParameters.Emplace(UAV, (uint16)Index);
	}

	FORCEINLINE_DEBUGGABLE void SetBindlessTexture(uint32 Index, FRHITexture* Texture)
	{
		BindlessParameters.Emplace(Texture, (uint16)Index);
	}
	FORCEINLINE_DEBUGGABLE void SetBindlessResourceView(uint32 Index, FRHIShaderResourceView* SRV)
	{
		BindlessParameters.Emplace(SRV, (uint16)Index);
	}
	FORCEINLINE_DEBUGGABLE void SetBindlessSampler(uint32 Index, FRHISamplerState* State)
	{
		BindlessParameters.Emplace(State, (uint16)Index);
	}
	FORCEINLINE_DEBUGGABLE void SetBindlessUAV(uint32 Index, FRHIUnorderedAccessView* UAV)
	{
		BindlessParameters.Emplace(UAV, (uint16)Index);
	}
};

/** Class that automatically batches shader parameters on a per-stage basis.
*   NOTE: this class will be phased out in favor of using FRHIBatchedShaderParameters directly.
*/
class FRHIParameterBatcher
{
protected:
	FRHIShader* AllBatchedShaders[SF_NumStandardFrequencies]{};
	FRHIBatchedShaderParameters AllBatchedShaderParameters[SF_NumStandardFrequencies];
	bool bEnabled = false;

	FRHIGraphicsShader* GetBatchedGraphicsShader(int32 Index) { return static_cast<FRHIGraphicsShader*>(AllBatchedShaders[Index]); }
	FRHIComputeShader* GetBatchedComputeShader() { return static_cast<FRHIComputeShader*>(AllBatchedShaders[SF_Compute]); }

	FORCEINLINE_DEBUGGABLE FRHIBatchedShaderParameters& GetBatchedShaderParameters(FRHIShader* InShader)
	{
		checkSlow(InShader);
		const EShaderFrequency Frequency = InShader->GetFrequency();
		checkSlow(Frequency < SF_NumStandardFrequencies);
		check(AllBatchedShaders[Frequency] == InShader);
		return AllBatchedShaderParameters[Frequency];
	}

public:
	RHI_API FRHIParameterBatcher();
	RHI_API FRHIParameterBatcher(const FBoundShaderStateInput& InBoundShaderStateInput, FRHIComputeShader* InBoundComputeShaderRHI);
	RHI_API FRHIParameterBatcher(FRHIParameterBatcher&&);
	RHI_API ~FRHIParameterBatcher();

	FORCEINLINE bool IsEnabled() const { return bEnabled; }

	RHI_API void OnBoundShaderChanged(FRHICommandList& InCommandList, const FBoundShaderStateInput& InBoundShaderStateInput);
	RHI_API void OnBoundShaderChanged(FRHIComputeCommandList& InCommandList, FRHIComputeShader* InBoundComputeShaderRHI);

	RHI_API void PreDispatch(FRHIComputeCommandList& InCommandList);
	RHI_API void PreDraw(FRHICommandList& InCommandList);

	RHI_API void FlushAllParameters(FRHIComputeCommandList& InCommandList);
	RHI_API void FlushAllParameters(FRHICommandList& InCommandList);

	RHI_API void FlushPendingParameters(FRHIComputeCommandList& InCommandList, FRHIComputeShader* InShader);
	RHI_API void FlushPendingParameters(FRHICommandList& InCommandList, FRHIGraphicsShader* InShader);

	FORCEINLINE_DEBUGGABLE void SetShaderParameter(FRHIShader* Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
	{
		GetBatchedShaderParameters(Shader).SetShaderParameter(BufferIndex, BaseIndex, NumBytes, NewValue);
	}

	FORCEINLINE_DEBUGGABLE void SetShaderTexture(FRHIShader* Shader, uint32 Index, FRHITexture* Texture)
	{
		GetBatchedShaderParameters(Shader).SetShaderTexture(Index, Texture);
	}
	FORCEINLINE_DEBUGGABLE void SetShaderResourceViewParameter(FRHIShader* Shader, uint32 Index, FRHIShaderResourceView* SRV)
	{
		GetBatchedShaderParameters(Shader).SetShaderResourceViewParameter(Index, SRV);
	}
	FORCEINLINE_DEBUGGABLE void SetShaderSampler(FRHIShader* Shader, uint32 Index, FRHISamplerState* State)
	{
		GetBatchedShaderParameters(Shader).SetShaderSampler(Index, State);
	}
	FORCEINLINE_DEBUGGABLE void SetUAVParameter(FRHIShader* Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV)
	{
		GetBatchedShaderParameters(Shader).SetUAVParameter(UAVIndex, UAV);
	}

	FORCEINLINE_DEBUGGABLE void SetBindlessTexture(FRHIShader* Shader, uint32 Index, FRHITexture* Texture)
	{
		GetBatchedShaderParameters(Shader).SetBindlessTexture(Index, Texture);
	}
	FORCEINLINE_DEBUGGABLE void SetBindlessResourceView(FRHIShader* Shader, uint32 Index, FRHIShaderResourceView* SRV)
	{
		GetBatchedShaderParameters(Shader).SetBindlessResourceView(Index, SRV);
	}
	FORCEINLINE_DEBUGGABLE void SetBindlessSampler(FRHIShader* Shader, uint32 Index, FRHISamplerState* State)
	{
		GetBatchedShaderParameters(Shader).SetBindlessSampler(Index, State);
	}
	FORCEINLINE_DEBUGGABLE void SetBindlessUAV(FRHIShader* Shader, uint32 Index, FRHIUnorderedAccessView* UAV)
	{
		GetBatchedShaderParameters(Shader).SetBindlessUAV(Index, UAV);
	}

	FORCEINLINE_DEBUGGABLE void SetUAVParameter(FRHIShader* Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount)
	{
		checkNoEntry(); // @todo: support append/consume buffers
	}
};
