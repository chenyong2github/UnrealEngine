// Copyright Epic Games, Inc. All Rights Reserved.

#include "VulkanRHIPrivate.h"

#if VULKAN_RHI_RAYTRACING

// Define ray tracing entry points
#define DEFINE_VK_ENTRYPOINTS(Type,Func) VULKANRHI_API Type VulkanDynamicAPI::Func = NULL;
ENUM_VK_ENTRYPOINTS_RAYTRACING(DEFINE_VK_ENTRYPOINTS)

void FVulkanRayTracingPlatform::GetDeviceExtensions(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutExtensions)
{
	// Primary extensions
	OutExtensions.Add(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

	// VK_KHR_acceleration_structure dependencies 
	OutExtensions.Add(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME); // Note: Promoted to Vulkan 1.2
	OutExtensions.Add(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME); // Note: Promoted to Vulkan 1.2
	OutExtensions.Add(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	
	// VK_KHR_ray_tracing_pipeline dependency
	OutExtensions.Add(VK_KHR_SPIRV_1_4_EXTENSION_NAME); // Note: Promoted to Vulkan 1.2

	// VK_KHR_spirv_1_4 dependency
	OutExtensions.Add(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME); // Note: Promoted to Vulkan 1.2
}

void FVulkanRayTracingPlatform::EnablePhysicalDeviceFeatureExtensions(VkDeviceCreateInfo& DeviceInfo, FVulkanDevice& Device)
{
	if (Device.GetOptionalExtensions().HasRaytracingExtensions())
	{
		FOptionalVulkanDeviceFeatures& Features = Device.GetOptionalFeatures();

		ZeroVulkanStruct(Features.BufferDeviceAddressFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
		Features.BufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
		Features.BufferDeviceAddressFeatures.pNext = const_cast<void*>(DeviceInfo.pNext);

		ZeroVulkanStruct(Features.AccelerationStructureFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);;
		Features.AccelerationStructureFeatures.accelerationStructure = VK_TRUE;
		Features.AccelerationStructureFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;
		Features.AccelerationStructureFeatures.pNext = &Features.BufferDeviceAddressFeatures;
		
		ZeroVulkanStruct(Features.RayTracingPipelineFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
		Features.RayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
		Features.RayTracingPipelineFeatures.pNext = &Features.AccelerationStructureFeatures;

		ZeroVulkanStruct(Features.DescriptorIndexingFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT);
		Features.DescriptorIndexingFeatures.pNext = &Features.RayTracingPipelineFeatures;

		DeviceInfo.pNext = &Features.DescriptorIndexingFeatures;
	}
}

#if PLATFORM_WINDOWS
#pragma warning(push)
#pragma warning(disable : 4191) // warning C4191: 'type cast': unsafe conversion
#endif // PLATFORM_WINDOWS
bool FVulkanRayTracingPlatform::LoadVulkanInstanceFunctions(VkInstance inInstance)
{
	bool bFoundAllEntryPoints = true;
#define CHECK_VK_ENTRYPOINTS(Type,Func) if (VulkanDynamicAPI::Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogRHI, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }
#define GETINSTANCE_VK_ENTRYPOINTS(Type, Func) VulkanDynamicAPI::Func = (Type)VulkanDynamicAPI::vkGetInstanceProcAddr(inInstance, #Func);
	ENUM_VK_ENTRYPOINTS_RAYTRACING(GETINSTANCE_VK_ENTRYPOINTS);
#if UE_BUILD_DEBUG
	ENUM_VK_ENTRYPOINTS_RAYTRACING(CHECK_VK_ENTRYPOINTS);
#endif
	return bFoundAllEntryPoints;
}
#if PLATFORM_WINDOWS
#pragma warning(pop) // restore 4191
#endif
#endif // #if VULKAN_RHI_RAYTRACING
