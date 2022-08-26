// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "D3D12RHI.h"
#include "RayTracingBuiltInResources.h"

namespace D3D12ShaderUtils
{
	struct FStaticRayTracingRootSignatureDesc
	{
		CD3DX12_ROOT_PARAMETER1 TableSlots[7];
		CD3DX12_DESCRIPTOR_RANGE1 DescriptorRanges[4];
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC RootDesc;
	};

	void InitStaticRayTracingRootSignatureDesc(FStaticRayTracingRootSignatureDesc& Desc, bool bLocalRootSignature, D3D12_ROOT_SIGNATURE_FLAGS BaseFlags, bool bBindlessResources, bool bBindlessSamplers)
	{
		uint32 SlotIndex = 0;

		if (bLocalRootSignature)
		{
			uint32 NumConstants = 4; // sizeof(FHitGroupSystemRootConstants) / sizeof(uint32);

			Desc.TableSlots[SlotIndex++].InitAsShaderResourceView(RAY_TRACING_SYSTEM_INDEXBUFFER_REGISTER, RAY_TRACING_REGISTER_SPACE_SYSTEM);
			Desc.TableSlots[SlotIndex++].InitAsShaderResourceView(RAY_TRACING_SYSTEM_VERTEXBUFFER_REGISTER, RAY_TRACING_REGISTER_SPACE_SYSTEM);
			Desc.TableSlots[SlotIndex++].InitAsConstants(NumConstants, RAY_TRACING_SYSTEM_ROOTCONSTANT_REGISTER, RAY_TRACING_REGISTER_SPACE_SYSTEM);
		}

		uint32 BindingSpace = bLocalRootSignature ? RAY_TRACING_REGISTER_SPACE_LOCAL : RAY_TRACING_REGISTER_SPACE_GLOBAL;

		// Table ranges

		Desc.DescriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MAX_SRVS, 0, BindingSpace, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		Desc.DescriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, MAX_CBS, 0, BindingSpace, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		Desc.DescriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, MAX_SAMPLERS, 0, BindingSpace, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		Desc.DescriptorRanges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, MAX_UAVS, 0, BindingSpace, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

		// Table slots

		Desc.TableSlots[SlotIndex++].InitAsDescriptorTable(1, &Desc.DescriptorRanges[0], D3D12_SHADER_VISIBILITY_ALL); // SRV
		Desc.TableSlots[SlotIndex++].InitAsDescriptorTable(1, &Desc.DescriptorRanges[1], D3D12_SHADER_VISIBILITY_ALL); // CBV
		Desc.TableSlots[SlotIndex++].InitAsDescriptorTable(1, &Desc.DescriptorRanges[2], D3D12_SHADER_VISIBILITY_ALL); // Sampler
		Desc.TableSlots[SlotIndex++].InitAsDescriptorTable(1, &Desc.DescriptorRanges[3], D3D12_SHADER_VISIBILITY_ALL); // UAV

		D3D12_ROOT_SIGNATURE_FLAGS Flags = BaseFlags;

		if (!bLocalRootSignature && bBindlessResources)
		{
			Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
		}

		if (!bLocalRootSignature && bBindlessSamplers)
		{
			Flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
		}

		Desc.RootDesc.Init_1_1(SlotIndex, Desc.TableSlots, 0, nullptr, Flags);
	}
}
