// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "D3D12RHI.h"
#include "RayTracingBuiltInResources.h"

namespace D3D12ShaderUtils
{
	template <bool bLocalRootSignature, D3D12_ROOT_SIGNATURE_FLAGS Flags>
	const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& GetStaticRayTracingRootSignatureDesc()
	{
		static CD3DX12_ROOT_PARAMETER1 TableSlots[7];
		static CD3DX12_DESCRIPTOR_RANGE1 DescriptorRanges[4];
		static CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC RootDesc;

		uint32 SlotIndex = 0;

		if (bLocalRootSignature)
		{
			uint32 NumConstants = 4; // sizeof(FHitGroupSystemRootConstants) / sizeof(uint32);

			TableSlots[SlotIndex++].InitAsShaderResourceView(RAY_TRACING_SYSTEM_INDEXBUFFER_REGISTER, RAY_TRACING_REGISTER_SPACE_SYSTEM);
			TableSlots[SlotIndex++].InitAsShaderResourceView(RAY_TRACING_SYSTEM_VERTEXBUFFER_REGISTER, RAY_TRACING_REGISTER_SPACE_SYSTEM);
			TableSlots[SlotIndex++].InitAsConstants(NumConstants, RAY_TRACING_SYSTEM_ROOTCONSTANT_REGISTER, RAY_TRACING_REGISTER_SPACE_SYSTEM);
		}

		uint32 BindingSpace = bLocalRootSignature ? RAY_TRACING_REGISTER_SPACE_LOCAL : RAY_TRACING_REGISTER_SPACE_GLOBAL;

		// Table ranges

		DescriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MAX_SRVS, 0, BindingSpace, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		DescriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, MAX_CBS, 0, BindingSpace, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		DescriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, MAX_SAMPLERS, 0, BindingSpace, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		DescriptorRanges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, MAX_UAVS, 0, BindingSpace, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

		// Table slots

		TableSlots[SlotIndex++].InitAsDescriptorTable(1, &DescriptorRanges[0], D3D12_SHADER_VISIBILITY_ALL); // SRV
		TableSlots[SlotIndex++].InitAsDescriptorTable(1, &DescriptorRanges[1], D3D12_SHADER_VISIBILITY_ALL); // CBV
		TableSlots[SlotIndex++].InitAsDescriptorTable(1, &DescriptorRanges[2], D3D12_SHADER_VISIBILITY_ALL); // Sampler
		TableSlots[SlotIndex++].InitAsDescriptorTable(1, &DescriptorRanges[3], D3D12_SHADER_VISIBILITY_ALL); // UAV

		RootDesc.Init_1_1(SlotIndex, TableSlots, 0, nullptr, Flags);

		return RootDesc;
	}
}
