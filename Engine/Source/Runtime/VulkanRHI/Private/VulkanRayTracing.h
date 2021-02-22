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

struct FAccelerationStructureAllocation
{
	VkDevice Device = VK_NULL_HANDLE;
	VkDeviceMemory Memory = VK_NULL_HANDLE;
	VkBuffer Buffer = VK_NULL_HANDLE;
	VkDeviceAddress Address = {};
};

class FVulkanRayTracingGeometry : public FRHIRayTracingGeometry
{
public:
	FVulkanRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer);
	~FVulkanRayTracingGeometry();

	void BuildAccelerationStructure(FVulkanCommandListContext& CommandContext, EAccelerationStructureBuildMode BuildMode);

private:
	uint32 IndexStrideInBytes  = 0;
	uint32 IndexOffsetInBytes  = 0;
	uint32 TotalPrimitiveCount = 0;
	
	FBufferRHIRef IndexBufferRHI;
	
	bool bFastBuild   = false;
	bool bAllowUpdate = false;

	VkAccelerationStructureKHR Handle = VK_NULL_HANDLE;
	FAccelerationStructureAllocation Allocation;
	FAccelerationStructureAllocation Scratch;

	TArray<FRayTracingGeometrySegment> Segments;

	FName DebugName;
};

// Todo
class FVulkanRayTracingScene : public FRHIRayTracingScene
{
public:
	FVulkanRayTracingScene(/*FD3D12Adapter* Adapter,*/ const FRayTracingSceneInitializer& Initializer);
	~FVulkanRayTracingScene();

	void BuildAccelerationStructure();

	TArray<FRayTracingGeometryInstance> Instances;
	TArray<TRefCountPtr<FVulkanRayTracingGeometry>> Geometries;
	FName DebugName;
};

#endif // #if VULKAN_RHI_RAYTRACING
