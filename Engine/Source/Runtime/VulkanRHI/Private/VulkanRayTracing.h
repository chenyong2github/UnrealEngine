// Copyright Epic Games, Inc. All Rights Reserved..

#pragma once

#if VULKAN_RHI_RAYTRACING

class FVulkanCommandListContext;

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
	VkDeviceAddress Address = 0;
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
	TArray<uint32, TInlineAllocator<1>> VertexCounts;
	VkAccelerationStructureBuildGeometryInfoKHR GeometryInfo;
	VkAccelerationStructureBuildSizesInfoKHR SizesInfo;
};

class FVulkanRayTracingGeometry : public FRHIRayTracingGeometry
{
public:
	FVulkanRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer, const FVulkanDevice* InDevice);
	~FVulkanRayTracingGeometry();

	void BuildAccelerationStructure(FVulkanCommandListContext& CommandContext, EAccelerationStructureBuildMode BuildMode);
	
	VkDeviceAddress GetAccelerationStructureAddress() const
	{
		return Allocation.Address;
	}

private:
	uint32 IndexStrideInBytes  = 0;
	uint32 IndexOffsetInBytes  = 0;
	uint32 TotalPrimitiveCount = 0;
	
	FBufferRHIRef IndexBufferRHI;
	
	bool bFastBuild   = false;
	bool bAllowUpdate = false;

	VkAccelerationStructureKHR Handle = VK_NULL_HANDLE;
	FVkRtAllocation Allocation;
	FVkRtAllocation Scratch;

	TArray<FRayTracingGeometrySegment> Segments;

	FName DebugName;
	const FVulkanDevice* Device = nullptr;
};

class FVulkanRayTracingScene : public FRHIRayTracingScene
{
public:
	FVulkanRayTracingScene(const FRayTracingSceneInitializer& Initializer, const FVulkanDevice* InDevice);
	~FVulkanRayTracingScene();

	void BuildAccelerationStructure(FVulkanCommandListContext& CommandContext);

	VkAccelerationStructureKHR Handle = VK_NULL_HANDLE;
	FVkRtAllocation Allocation;
	FVkRtAllocation Scratch;
	FVkRtAllocation InstanceBuffer;

	TArray<VkAccelerationStructureInstanceKHR> InstanceDescs;
	TArray<const FVulkanRayTracingGeometry*> InstanceGeometry;
	TArray<TRefCountPtr<const FVulkanRayTracingGeometry>> ReferencedGeometry;
	FName DebugName;
	uint32 NumInstances = 0;

private:
	const FVulkanDevice* Device = nullptr;
};

#endif // #if VULKAN_RHI_RAYTRACING
