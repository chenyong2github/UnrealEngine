// Copyright Epic Games, Inc. All Rights Reserved..

#pragma once

#include "VulkanRHIPrivate.h"

#if VULKAN_RHI_RAYTRACING

class FVulkanCommandListContext;
class FVulkanResourceMultiBuffer;
class FVulkanRayTracingLayout;

#define ENUM_VK_ENTRYPOINTS_RAYTRACING(EnumMacro) \
	EnumMacro(PFN_vkCreateAccelerationStructureKHR, vkCreateAccelerationStructureKHR) \
	EnumMacro(PFN_vkDestroyAccelerationStructureKHR, vkDestroyAccelerationStructureKHR) \
	EnumMacro(PFN_vkCmdBuildAccelerationStructuresKHR, vkCmdBuildAccelerationStructuresKHR) \
	EnumMacro(PFN_vkGetAccelerationStructureBuildSizesKHR, vkGetAccelerationStructureBuildSizesKHR) \
	EnumMacro(PFN_vkGetAccelerationStructureDeviceAddressKHR, vkGetAccelerationStructureDeviceAddressKHR) \
	EnumMacro(PFN_vkCmdTraceRaysKHR, vkCmdTraceRaysKHR) \
	EnumMacro(PFN_vkCreateRayTracingPipelinesKHR, vkCreateRayTracingPipelinesKHR) \
	EnumMacro(PFN_vkGetRayTracingShaderGroupHandlesKHR, vkGetRayTracingShaderGroupHandlesKHR) \
	EnumMacro(PFN_vkGetBufferDeviceAddressKHR, vkGetBufferDeviceAddressKHR)

// Declare ray tracing entry points
namespace VulkanDynamicAPI
{
	ENUM_VK_ENTRYPOINTS_RAYTRACING(DECLARE_VK_ENTRYPOINTS);
}

class FVulkanRayTracingPlatform
{
public:
	static void GetDeviceExtensions(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutExtensions);
	static void EnablePhysicalDeviceFeatureExtensions(VkDeviceCreateInfo& DeviceInfo, FVulkanDevice& Device);
	static bool LoadVulkanInstanceFunctions(VkInstance inInstance);
};

struct FVkRtAllocation
{
	VkDevice Device = VK_NULL_HANDLE;
	VkDeviceMemory Memory = VK_NULL_HANDLE;
	VkBuffer Buffer = VK_NULL_HANDLE;
};

class FVulkanRayTracingAllocator
{
public:
	static void Allocate(FVulkanDevice* Device, VkDeviceSize Size, VkBufferUsageFlags UsageFlags, VkMemoryPropertyFlags MemoryFlags, FVkRtAllocation& Result);
	static void Free(FVkRtAllocation& Allocation);
};

struct FVkRtTLASBuildData
{
	FVkRtTLASBuildData()
	{
		ZeroVulkanStruct(Geometry, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
		ZeroVulkanStruct(GeometryInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
		ZeroVulkanStruct(SizesInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
	}

	VkAccelerationStructureGeometryKHR Geometry;
	VkAccelerationStructureBuildGeometryInfoKHR GeometryInfo;
	VkAccelerationStructureBuildSizesInfoKHR SizesInfo;
};

struct FVkRtBLASBuildData
{
	FVkRtBLASBuildData()
	{
		ZeroVulkanStruct(GeometryInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
		ZeroVulkanStruct(SizesInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
	}

	TArray<VkAccelerationStructureGeometryKHR, TInlineAllocator<1>> Segments;
	TArray<VkAccelerationStructureBuildRangeInfoKHR, TInlineAllocator<1>> Ranges;
	VkAccelerationStructureBuildGeometryInfoKHR GeometryInfo;
	VkAccelerationStructureBuildSizesInfoKHR SizesInfo;
};

class FVulkanRayTracingGeometry : public FRHIRayTracingGeometry
{
public:
	FVulkanRayTracingGeometry(ENoInit);
	FVulkanRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer, FVulkanDevice* InDevice);
	~FVulkanRayTracingGeometry();

	virtual FRayTracingAccelerationStructureAddress GetAccelerationStructureAddress(uint64 GPUIndex) const final override { return Address; }	
	virtual void SetInitializer(const FRayTracingGeometryInitializer& Initializer) final override;

	void Swap(FVulkanRayTracingGeometry& Other);
	void BuildAccelerationStructure(FVulkanCommandListContext& CommandContext, EAccelerationStructureBuildMode BuildMode);

private:
	FVulkanDevice* const Device = nullptr;

	VkAccelerationStructureKHR Handle = VK_NULL_HANDLE;
	VkDeviceAddress Address = 0;
	TRefCountPtr<FVulkanResourceMultiBuffer> AccelerationStructureBuffer;
	TRefCountPtr<FVulkanResourceMultiBuffer> ScratchBuffer;
};

class FVulkanRayTracingScene : public FRHIRayTracingScene
{
public:
	FVulkanRayTracingScene(FRayTracingSceneInitializer2 Initializer, FVulkanDevice* InDevice, FVulkanResourceMultiBuffer* InInstanceBuffer);
	~FVulkanRayTracingScene();

	const FRayTracingSceneInitializer2& GetInitializer() const override final { return Initializer; }

	void BindBuffer(FRHIBuffer* InBuffer, uint32 InBufferOffset);
	void BuildAccelerationStructure(
		FVulkanCommandListContext& CommandContext, 
		FVulkanResourceMultiBuffer* ScratchBuffer, uint32 ScratchOffset, 
		FVulkanResourceMultiBuffer* InstanceBuffer, uint32 InstanceOffset);

	FRayTracingAccelerationStructureSize SizeInfo;

	virtual FRHIShaderResourceView* GetMetadataBufferSRV() const override final
	{
		return PerInstanceGeometryParameterSRV.GetReference();
	}

private:
	FVulkanDevice* const Device = nullptr;

	const FRayTracingSceneInitializer2 Initializer;

	TRefCountPtr<FVulkanResourceMultiBuffer> InstanceBuffer;

	// Native TLAS handles are owned by SRV objects in Vulkan RHI.
	// D3D12 and other RHIs allow creating TLAS SRVs from any GPU address at any point
	// and do not require them for operations such as build or update.
	// FVulkanRayTracingScene can't own the VkAccelerationStructureKHR directly because
	// we allow TLAS memory to be allocated using transient resource allocator and 
	// the lifetime of the scene object may be different from the lifetime of the buffer.
	// Many VkAccelerationStructureKHR-s may be created, pointing at the same buffer.
	TRefCountPtr<FVulkanShaderResourceView> AccelerationStructureView;
	
	TRefCountPtr<FVulkanResourceMultiBuffer> AccelerationStructureBuffer;

	// Buffer that contains per-instance index and vertex buffer binding data
	TRefCountPtr<FVulkanResourceMultiBuffer> PerInstanceGeometryParameterBuffer;
	TRefCountPtr<FVulkanShaderResourceView> PerInstanceGeometryParameterSRV;
	void BuildPerInstanceGeometryParameterBuffer();
};

class FVulkanRayTracingPipelineState : public FRHIRayTracingPipelineState
{
public:

	UE_NONCOPYABLE(FVulkanRayTracingPipelineState);
	FVulkanRayTracingPipelineState(FVulkanDevice* const InDevice, const FRayTracingPipelineStateInitializer& Initializer);
	~FVulkanRayTracingPipelineState();

private:

	FVulkanRayTracingLayout* Layout = nullptr;
	VkPipeline Pipeline = VK_NULL_HANDLE;
	FVkRtAllocation RayGenShaderBindingTable;
	FVkRtAllocation MissShaderBindingTable;
	FVkRtAllocation HitShaderBindingTable;
};

class FVulkanBasicRaytracingPipeline
{
public:

	UE_NONCOPYABLE(FVulkanBasicRaytracingPipeline);
	FVulkanBasicRaytracingPipeline(FVulkanDevice* const InDevice);
	~FVulkanBasicRaytracingPipeline();

private:

	FVulkanRayTracingPipelineState* Occlusion = nullptr;
};
#endif // VULKAN_RHI_RAYTRACING
