// Copyright Epic Games, Inc. All Rights Reserved.

#include "VulkanRHIPrivate.h"
#include "VulkanContext.h"
#include "Experimental/Containers/SherwoodHashTable.h"

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

static VkDeviceAddress GetDeviceAddress(VkDevice Device, VkBuffer Buffer)
{
	VkBufferDeviceAddressInfoKHR DeviceAddressInfo;
	ZeroVulkanStruct(DeviceAddressInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
	DeviceAddressInfo.buffer = Buffer;
	return vkGetBufferDeviceAddressKHR(Device, &DeviceAddressInfo);
}

// Temporary brute force allocation helper, this should be handled by the memory sub-allocator
static uint32 FindMemoryType(VkPhysicalDevice Gpu, uint32 Filter, VkMemoryPropertyFlags RequestedProperties)
{
	VkPhysicalDeviceMemoryProperties Properties = {};
	vkGetPhysicalDeviceMemoryProperties(Gpu, &Properties);

	uint32 Result = UINT32_MAX;
	for (uint32 i = 0; i < Properties.memoryTypeCount; ++i)
	{
		const bool bTypeFilter = Filter & (1 << i);
		const bool bPropFilter = (Properties.memoryTypes[i].propertyFlags & RequestedProperties) == RequestedProperties;
		if (bTypeFilter && bPropFilter)
		{
			Result = i;
			break;
		}
	}

	check(Result < UINT32_MAX);
	return Result;
}

// Temporary brute force allocation
static void Allocate(VkPhysicalDevice Gpu, VkDevice Device, VkDeviceSize Size, VkBufferUsageFlags UsageFlags, VkMemoryPropertyFlags MemoryFlags, FVkRtAllocation& Result)
{
	VkBufferCreateInfo CreateInfo;
	ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
	CreateInfo.size = Size;
	CreateInfo.usage = UsageFlags;
	VERIFYVULKANRESULT(vkCreateBuffer(Device, &CreateInfo, VULKAN_CPU_ALLOCATOR, &Result.Buffer));

	VkMemoryRequirements MemoryRequirements = {};
	vkGetBufferMemoryRequirements(Device, Result.Buffer, &MemoryRequirements);

	VkMemoryAllocateFlagsInfo MemoryAllocateFlagsInfo;
	ZeroVulkanStruct(MemoryAllocateFlagsInfo, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO);
	MemoryAllocateFlagsInfo.flags = MemoryFlags;

	VkMemoryAllocateInfo MemoryAllocateInfo;
	ZeroVulkanStruct(MemoryAllocateInfo, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
	MemoryAllocateInfo.pNext = &MemoryAllocateFlagsInfo;
	MemoryAllocateInfo.allocationSize = MemoryRequirements.size;
	MemoryAllocateInfo.memoryTypeIndex = FindMemoryType(Gpu, MemoryRequirements.memoryTypeBits, MemoryFlags);
	VERIFYVULKANRESULT(vkAllocateMemory(Device, &MemoryAllocateInfo, VULKAN_CPU_ALLOCATOR, &Result.Memory));
	VERIFYVULKANRESULT(vkBindBufferMemory(Device, Result.Buffer, Result.Memory, 0));

	Result.Device = Device;
}

// Temporary brute force deallocation
static void Free(FVkRtAllocation& Allocation)
{
	if (Allocation.Buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(Allocation.Device, Allocation.Buffer, VULKAN_CPU_ALLOCATOR);
		Allocation.Buffer = VK_NULL_HANDLE;
	}
	if (Allocation.Memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(Allocation.Device, Allocation.Memory, VULKAN_CPU_ALLOCATOR);
		Allocation.Memory = VK_NULL_HANDLE;
	}
}

FVulkanRayTracingGeometry::FVulkanRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer)
{
	// Only supporting triangles initially
	check(Initializer.GeometryType == ERayTracingGeometryType::RTGT_Triangles);

	DebugName = Initializer.DebugName;
	IndexOffsetInBytes = Initializer.IndexBufferOffset;
	TotalPrimitiveCount = Initializer.TotalPrimitiveCount;
	IndexBufferRHI = Initializer.IndexBuffer;
	bFastBuild = Initializer.bFastBuild;
	bAllowUpdate = Initializer.bAllowUpdate;
	Segments = TArray<FRayTracingGeometrySegment>(Initializer.Segments.GetData(), Initializer.Segments.Num());
}

FVulkanRayTracingGeometry::~FVulkanRayTracingGeometry()
{
	Free(Allocation);
	Free(Scratch);
}

void FVulkanRayTracingGeometry::BuildAccelerationStructure(FVulkanCommandListContext& CommandContext, EAccelerationStructureBuildMode BuildMode)
{
	check(BuildMode == EAccelerationStructureBuildMode::Build); // Temp

	checkf(!IndexBufferRHI || (IndexBufferRHI->GetStride() == 2 || IndexBufferRHI->GetStride() == 4), TEXT("Index buffer must be 16 or 32 bit if in use."));
	const VkDevice Device = CommandContext.GetDevice()->GetInstanceHandle();

	IndexStrideInBytes = IndexBufferRHI ? IndexBufferRHI->GetStride() : 0;

	FVulkanResourceMultiBuffer* const IndexBuffer = ResourceCast(IndexBufferRHI.GetReference());
	VkDeviceOrHostAddressConstKHR IndexBufferDeviceAddress = {};
	IndexBufferDeviceAddress.deviceAddress = IndexBufferRHI ? GetDeviceAddress(Device, IndexBuffer->GetHandle()) : 0;

	TArray<VkAccelerationStructureGeometryKHR, TInlineAllocator<1>> BuildSegments;
	TArray<VkAccelerationStructureBuildRangeInfoKHR, TInlineAllocator<1>> BuildRanges;
	TArray<uint32> VertexCounts;

	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FRayTracingGeometrySegment& Segment = Segments[SegmentIndex];

		FVulkanResourceMultiBuffer* const VertexBuffer = ResourceCast(Segment.VertexBuffer.GetReference());
		VkDeviceOrHostAddressConstKHR VertexBufferDeviceAddress = {};
		VertexBufferDeviceAddress.deviceAddress = GetDeviceAddress(Device, VertexBuffer->GetHandle());

		// #vkrt_todo: Add explicit vertex count to FRayTracingGeometrySegment instead of this estimate
		const uint32 MaxSegmentVertices = (VertexBuffer->GetSize() - Segment.VertexBufferOffset) / Segment.VertexBufferStride;
		VertexCounts.Add(MaxSegmentVertices);

		VkAccelerationStructureGeometryKHR SegmentGeometry;
		ZeroVulkanStruct(SegmentGeometry, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
		if (Segment.bForceOpaque)
		{
			SegmentGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		}

		// Only support triangles
		SegmentGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		
		SegmentGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		SegmentGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		SegmentGeometry.geometry.triangles.vertexData = VertexBufferDeviceAddress;
		SegmentGeometry.geometry.triangles.maxVertex = MaxSegmentVertices;
		SegmentGeometry.geometry.triangles.vertexStride = Segment.VertexBufferStride;
		SegmentGeometry.geometry.triangles.indexData = IndexBufferDeviceAddress;

		// No support for segment transform
		SegmentGeometry.geometry.triangles.transformData.deviceAddress = 0;
		SegmentGeometry.geometry.triangles.transformData.hostAddress = nullptr;

		if (IndexBufferRHI)
		{
			SegmentGeometry.geometry.triangles.indexType = (IndexStrideInBytes == 2) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
		}
		else
		{
			SegmentGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
		}

		BuildSegments.Add(SegmentGeometry);

		VkAccelerationStructureBuildRangeInfoKHR RangeInfo = {};
		RangeInfo.firstVertex = 0;

		// Disabled segments use an empty range. We still build them to keep the sbt valid.
		RangeInfo.primitiveCount = (Segment.bEnabled) ? MaxSegmentVertices : 0;

		RangeInfo.primitiveOffset = 0;
		RangeInfo.transformOffset = 0;

		BuildRanges.Add(RangeInfo);
	}

	VkAccelerationStructureBuildGeometryInfoKHR BuildGeometryInfo;
	ZeroVulkanStruct(BuildGeometryInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
	BuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	BuildGeometryInfo.flags = (bFastBuild) ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	if (bAllowUpdate)
	{
		BuildGeometryInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	}
	BuildGeometryInfo.mode = (BuildMode == EAccelerationStructureBuildMode::Build) ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
	BuildGeometryInfo.geometryCount = BuildSegments.Num();
	BuildGeometryInfo.pGeometries = BuildSegments.GetData();

	VkAccelerationStructureBuildSizesInfoKHR BuildSizesInfo;
	ZeroVulkanStruct(BuildSizesInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
	vkGetAccelerationStructureBuildSizesKHR(
		Device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&BuildGeometryInfo,
		VertexCounts.GetData(),
		&BuildSizesInfo);

	Allocate(
		CommandContext.GetDevice()->GetPhysicalHandle(), 
		Device, 
		BuildSizesInfo.accelerationStructureSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR, 
		Allocation);

	Allocate(
		CommandContext.GetDevice()->GetPhysicalHandle(),
		Device,
		BuildSizesInfo.buildScratchSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR, 
		Scratch);

	VkBufferDeviceAddressInfoKHR ScratchDeviceAddressInfo;
	ZeroVulkanStruct(ScratchDeviceAddressInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
	ScratchDeviceAddressInfo.buffer = Scratch.Buffer;
	Scratch.Address = vkGetBufferDeviceAddressKHR(Device, &ScratchDeviceAddressInfo);

	VkAccelerationStructureCreateInfoKHR CreateInfo;
	ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
	CreateInfo.buffer = Allocation.Buffer;
	CreateInfo.size = BuildSizesInfo.accelerationStructureSize;
	CreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	VERIFYVULKANRESULT(vkCreateAccelerationStructureKHR(Device, &CreateInfo, VULKAN_CPU_ALLOCATOR, &Handle));

	BuildGeometryInfo.dstAccelerationStructure = Handle;
	BuildGeometryInfo.scratchData.deviceAddress = Scratch.Address;

	VkAccelerationStructureBuildRangeInfoKHR* const pBuildRanges = BuildRanges.GetData();

	FVulkanCommandBufferManager& CommandBufferManager = *CommandContext.GetCommandBufferManager();
	FVulkanCmdBuffer* const CmdBuffer = CommandBufferManager.GetActiveCmdBuffer();
	vkCmdBuildAccelerationStructuresKHR(CmdBuffer->GetHandle(), 1, &BuildGeometryInfo, &pBuildRanges);

	CommandBufferManager.SubmitActiveCmdBuffer();
	CommandBufferManager.PrepareForNewActiveCommandBuffer();

	VkAccelerationStructureDeviceAddressInfoKHR DeviceAddressInfo;
	ZeroVulkanStruct(DeviceAddressInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
	DeviceAddressInfo.accelerationStructure = Handle;
	Allocation.Address = vkGetAccelerationStructureDeviceAddressKHR(Device, &DeviceAddressInfo);

	// No longer need scratch memory for a static build
	if (!bAllowUpdate)
	{
		Free(Scratch);
	}
}

FVulkanRayTracingScene::FVulkanRayTracingScene(const FRayTracingSceneInitializer& Initializer)
{
	DebugName = Initializer.DebugName;

	uint32 NumInstances = 0;
	Experimental::TSherwoodSet<const FVulkanRayTracingGeometry*> UniqueGeometryReferences;

	for (const FRayTracingGeometryInstance& SceneInstance : Initializer.Instances)
	{
		// Don't support GPU transforms yet.
		ensure(!SceneInstance.GPUTransformsSRV.IsValid());

		// Hold a reference to each unique geometry object in the scene
		const FVulkanRayTracingGeometry* const Geometry = ResourceCast(SceneInstance.GeometryRHI);
		bool bIsAlreadyInSet = false;
		UniqueGeometryReferences.Add(Geometry, &bIsAlreadyInSet);
		if (!bIsAlreadyInSet)
		{
			ReferencedGeometry.Add(Geometry);
		}

		NumInstances += SceneInstance.NumTransforms;
	}

	uint32 InstanceDescIndex = 0;
	InstanceDescs.SetNumUninitialized(NumInstances);
	InstanceGeometry.SetNumUninitialized(NumInstances);
	for (const FRayTracingGeometryInstance& SceneInstance : Initializer.Instances)
	{
		const bool bUseUniqueUserData = SceneInstance.UserData.Num() > 1;
		const uint32 CommonUserData = SceneInstance.UserData.Num() == 1 ? SceneInstance.UserData[0] : 0;

		TArrayView<const FMatrix> Transforms = SceneInstance.GetTransforms();
		for (int32 TransformIndex = 0; TransformIndex < Transforms.Num(); ++TransformIndex)
		{
			InstanceGeometry[InstanceDescIndex] = ResourceCast(SceneInstance.GeometryRHI);

			VkAccelerationStructureInstanceKHR& InstanceDesc = InstanceDescs[InstanceDescIndex];
			const FMatrix TransposedTransform = Transforms[TransformIndex].GetTransposed();
			FMemory::Memcpy(&InstanceDesc.transform, &TransposedTransform.M[0][0], sizeof(VkTransformMatrixKHR));

			InstanceDesc.accelerationStructureReference = 0; // Set during TLAS build after the BLAS for the referenced geometry is built.
			InstanceDesc.instanceCustomIndex = (bUseUniqueUserData) ? SceneInstance.UserData[TransformIndex] : CommonUserData;
			InstanceDesc.mask = SceneInstance.Mask;
			InstanceDesc.instanceShaderBindingTableRecordOffset = 0; // Todo
			InstanceDesc.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;

			if (SceneInstance.bForceOpaque)
			{
				InstanceDesc.flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
			}

			if (SceneInstance.bDoubleSided)
			{
				InstanceDesc.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
			}

			++InstanceDescIndex;
		}
	}
}

FVulkanRayTracingScene::~FVulkanRayTracingScene()
{
	Free(Allocation);
}

void FVulkanRayTracingScene::BuildAccelerationStructure(FVulkanCommandListContext& CommandContext)
{
	const VkDevice Device = CommandContext.GetDevice()->GetInstanceHandle();
	const uint32 NumInstances = static_cast<uint32>(InstanceDescs.Num());
	const uint32 InstanceBufferByteSize = NumInstances * sizeof(VkAccelerationStructureInstanceKHR);
	FVkRtAllocation InstanceBuffer;

	Allocate(
		CommandContext.GetDevice()->GetPhysicalHandle(),
		Device,
		InstanceBufferByteSize,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		InstanceBuffer);

	void* pMappedInstanceBufferMemory = nullptr;
	VERIFYVULKANRESULT(vkMapMemory(Device, InstanceBuffer.Memory, 0, InstanceBufferByteSize, 0, &pMappedInstanceBufferMemory));
	{
		VkAccelerationStructureInstanceKHR* const InstanceDescBuffer = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(pMappedInstanceBufferMemory);
		FMemory::Memcpy(InstanceDescBuffer, InstanceDescs.GetData(), InstanceBufferByteSize);

		for (int32 InstanceIndex = 0; InstanceIndex < InstanceDescs.Num(); ++InstanceIndex)
		{
			InstanceDescBuffer[InstanceIndex].accelerationStructureReference = InstanceGeometry[InstanceIndex]->GetAccelerationStructureAddress();
		}
	}
	vkUnmapMemory(Device, InstanceBuffer.Memory);

	InstanceDescs.Empty();
	InstanceGeometry.Empty();

	VkDeviceOrHostAddressConstKHR InstanceBufferDeviceAddress = {};
	InstanceBufferDeviceAddress.deviceAddress = GetDeviceAddress(Device, InstanceBuffer.Buffer);

	VkAccelerationStructureGeometryKHR TLASGeometry;
	ZeroVulkanStruct(TLASGeometry, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
	TLASGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	TLASGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	TLASGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
	TLASGeometry.geometry.instances.data = InstanceBufferDeviceAddress;

	VkAccelerationStructureBuildGeometryInfoKHR TLASGeometryInfo;
	ZeroVulkanStruct(TLASGeometryInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
	TLASGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	TLASGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	TLASGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	TLASGeometryInfo.geometryCount = 1;
	TLASGeometryInfo.pGeometries = &TLASGeometry;

	VkAccelerationStructureBuildSizesInfoKHR TLASBuildSizesInfo;
	ZeroVulkanStruct(TLASBuildSizesInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
	vkGetAccelerationStructureBuildSizesKHR(
		Device, 
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, 
		&TLASGeometryInfo, 
		&NumInstances, 
		&TLASBuildSizesInfo);

	Allocate(
		CommandContext.GetDevice()->GetPhysicalHandle(),
		Device,
		TLASBuildSizesInfo.accelerationStructureSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
		Allocation);

	Allocate(
		CommandContext.GetDevice()->GetPhysicalHandle(),
		Device,
		TLASBuildSizesInfo.buildScratchSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
		Scratch);

	VkBufferDeviceAddressInfoKHR ScratchDeviceAddressInfo;
	ZeroVulkanStruct(ScratchDeviceAddressInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
	ScratchDeviceAddressInfo.buffer = Scratch.Buffer;
	Scratch.Address = vkGetBufferDeviceAddressKHR(Device, &ScratchDeviceAddressInfo);

	VkAccelerationStructureCreateInfoKHR TLASCreateInfo;
	ZeroVulkanStruct(TLASCreateInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
	TLASCreateInfo.buffer = Allocation.Buffer;
	TLASCreateInfo.size = TLASBuildSizesInfo.accelerationStructureSize;
	TLASCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	VERIFYVULKANRESULT(vkCreateAccelerationStructureKHR(Device, &TLASCreateInfo, VULKAN_CPU_ALLOCATOR, &Handle));

	TLASGeometryInfo.dstAccelerationStructure = Handle;
	TLASGeometryInfo.scratchData.deviceAddress = Scratch.Address;

	VkAccelerationStructureBuildRangeInfoKHR TLASBuildRangeInfo;
	TLASBuildRangeInfo.primitiveCount = NumInstances;
	TLASBuildRangeInfo.primitiveOffset = 0;
	TLASBuildRangeInfo.transformOffset = 0;
	TLASBuildRangeInfo.firstVertex = 0;

	VkAccelerationStructureBuildRangeInfoKHR* const pBuildRanges = &TLASBuildRangeInfo;

	FVulkanCommandBufferManager& CommandBufferManager = *CommandContext.GetCommandBufferManager();
	FVulkanCmdBuffer* const CmdBuffer = CommandBufferManager.GetActiveCmdBuffer();
	vkCmdBuildAccelerationStructuresKHR(CmdBuffer->GetHandle(), 1, &TLASGeometryInfo, &pBuildRanges);

	CommandBufferManager.SubmitActiveCmdBuffer();
	CommandBufferManager.PrepareForNewActiveCommandBuffer();

	Free(InstanceBuffer);
	Free(Scratch);
}

FRayTracingSceneRHIRef FVulkanDynamicRHI::RHICreateRayTracingScene(const FRayTracingSceneInitializer& Initializer)
{
	return new FVulkanRayTracingScene(Initializer);
}

FRayTracingGeometryRHIRef FVulkanDynamicRHI::RHICreateRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer)
{
	return new FVulkanRayTracingGeometry(Initializer);
}

// Todo: High level rhi call should have transitioned and verified vb and ib to read for each segment
void FVulkanCommandListContext::RHIBuildAccelerationStructures(const TArrayView<const FAccelerationStructureBuildParams> Params)
{
	for (const FAccelerationStructureBuildParams& P : Params)
	{
		FVulkanRayTracingGeometry* const Geometry = ResourceCast(P.Geometry.GetReference());

		// Todo: Update geometry from params for each segment
		// Todo: Can we do this only for an update?

		// Build as for each segment
		Geometry->BuildAccelerationStructure(*this, P.BuildMode);
	}
}

void FVulkanCommandListContext::RHIBuildAccelerationStructure(FRHIRayTracingScene* InScene)
{
	FVulkanRayTracingScene* const Scene = ResourceCast(InScene);
	Scene->BuildAccelerationStructure(*this);
}

#endif // #if VULKAN_RHI_RAYTRACING
