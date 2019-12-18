// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RenderGraphPass.h"

void FRDGPassParameterStruct::ClearUniformBuffers() const
{
	/** The pass parameter struct is mostly POD, with the exception of uniform buffers.
	 *  Since the explicit type of the struct is unknown, the method traverses and destructs
	 *  all uniform buffer references manually.
	 */
	const auto& Resources = Layout->Resources;

	for (int32 ResourceIndex = 0; ResourceIndex < Resources.Num(); ++ResourceIndex)
	{
		const EUniformBufferBaseType MemberType = Resources[ResourceIndex].MemberType;

		if (MemberType == UBMT_REFERENCED_STRUCT)
		{
			const uint16 MemberOffset = Resources[ResourceIndex].MemberOffset;

			FUniformBufferRHIRef* UniformBufferPtr = reinterpret_cast<FUniformBufferRHIRef*>(Contents + MemberOffset);
			*UniformBufferPtr = FUniformBufferRHIRef();
		}
	}
}

FUniformBufferStaticBindings FRDGPassParameterStruct::GetGlobalUniformBuffers() const
{
	FUniformBufferStaticBindings GlobalUniformBuffers;

	const auto& Resources = Layout->Resources;

	for (int32 ResourceIndex = 0; ResourceIndex < Resources.Num(); ++ResourceIndex)
	{
		const EUniformBufferBaseType MemberType = Resources[ResourceIndex].MemberType;

		if (MemberType == UBMT_REFERENCED_STRUCT)
		{
			const uint16 MemberOffset = Resources[ResourceIndex].MemberOffset;

			FRHIUniformBuffer* UniformBufferPtr = *reinterpret_cast<FUniformBufferRHIRef*>(Contents + MemberOffset);

			if (UniformBufferPtr)
			{
				const FUniformBufferStaticSlot StaticSlot = UniformBufferPtr->GetLayout().StaticSlot;

				if (IsUniformBufferStaticSlotValid(StaticSlot))
				{
					GlobalUniformBuffers.AddUniformBuffer(UniformBufferPtr);
				}
			}
		}
	}

	return GlobalUniformBuffers;
}

FRDGPass::FRDGPass(
	FRDGEventName&& InName,
	FRDGPassParameterStruct InParameterStruct,
	ERDGPassFlags InPassFlags)
	: Name(static_cast<FRDGEventName&&>(InName))
	, ParameterStruct(InParameterStruct)
	, PassFlags(InPassFlags)
{}

void FRDGPass::Execute(FRHICommandListImmediate& RHICmdList) const
{
	FUniformBufferStaticBindings GlobalUniformBuffers = ParameterStruct.GetGlobalUniformBuffers();
	SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, GlobalUniformBuffers);

	ExecuteImpl(RHICmdList);

	ParameterStruct.ClearUniformBuffers();
}