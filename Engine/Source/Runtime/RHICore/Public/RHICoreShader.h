// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHICore.h"
#include "ShaderCore.h"

namespace UE
{
namespace RHICore
{

/** Validates that the uniform buffer at the requested static slot. */
extern RHICORE_API void ValidateStaticUniformBuffer(FRHIUniformBuffer* UniformBuffer, FUniformBufferStaticSlot Slot, uint32 ExpectedHash);

template <typename TRHIContext, typename TRHIShader>
void ApplyStaticUniformBuffers(
	TRHIContext* CommandContext,
	TRHIShader* Shader,
	const TArray<FUniformBufferStaticSlot>& Slots,
	const TArray<uint32>& LayoutHashes,
	const TArray<FRHIUniformBuffer*>& UniformBuffers)
{
	checkf(LayoutHashes.Num() == Slots.Num(), TEXT("Shader %s, LayoutHashes %d, Slots %d"),
		Shader->GetShaderName(), LayoutHashes.Num(), Slots.Num());

	for (int32 BufferIndex = 0; BufferIndex < Slots.Num(); ++BufferIndex)
	{
		const FUniformBufferStaticSlot Slot = Slots[BufferIndex];

		if (IsUniformBufferStaticSlotValid(Slot))
		{
			FRHIUniformBuffer* Buffer = UniformBuffers[Slot];
			ValidateStaticUniformBuffer(Buffer, Slot, LayoutHashes[BufferIndex]);

			if (Buffer)
			{
				CommandContext->RHISetShaderUniformBuffer(Shader, BufferIndex, Buffer);
			}
		}
	}
}

} //! RHICore
} //! UE