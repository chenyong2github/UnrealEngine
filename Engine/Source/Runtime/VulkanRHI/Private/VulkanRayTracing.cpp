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
void FVulkanRayTracingAllocator::Allocate(VkPhysicalDevice Gpu, VkDevice Device, VkDeviceSize Size, VkBufferUsageFlags UsageFlags, VkMemoryPropertyFlags MemoryFlags, FVkRtAllocation& Result)
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
	MemoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

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
void FVulkanRayTracingAllocator::Free(FVkRtAllocation& Allocation)
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

static void GetBLASBuildData(
	const VkDevice Device,
	const TArrayView<const FRayTracingGeometrySegment> Segments,
	const FBufferRHIRef IndexBufferRHI, 
	const bool bFastBuild,
	const bool bAllowUpdate,
	const uint32 IndexStrideInBytes,
	const EAccelerationStructureBuildMode BuildMode,
	FVkRtBLASBuildData& BuildData)
{
	FVulkanResourceMultiBuffer* const IndexBuffer = ResourceCast(IndexBufferRHI.GetReference());
	VkDeviceOrHostAddressConstKHR IndexBufferDeviceAddress = {};
	IndexBufferDeviceAddress.deviceAddress = IndexBufferRHI ? GetDeviceAddress(Device, IndexBuffer->GetHandle()) : 0;

	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FRayTracingGeometrySegment& Segment = Segments[SegmentIndex];

		FVulkanResourceMultiBuffer* const VertexBuffer = ResourceCast(Segment.VertexBuffer.GetReference());
		VkDeviceOrHostAddressConstKHR VertexBufferDeviceAddress = {};
		VertexBufferDeviceAddress.deviceAddress = GetDeviceAddress(Device, VertexBuffer->GetHandle());

		BuildData.VertexCounts.Add(Segment.MaxVertices);

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
		SegmentGeometry.geometry.triangles.maxVertex = Segment.MaxVertices;
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

		BuildData.Segments.Add(SegmentGeometry);

		VkAccelerationStructureBuildRangeInfoKHR RangeInfo = {};
		RangeInfo.firstVertex = 0;

		// Disabled segments use an empty range. We still build them to keep the sbt valid.
		RangeInfo.primitiveCount = (Segment.bEnabled) ? Segment.MaxVertices : 0;

		RangeInfo.primitiveOffset = 0;
		RangeInfo.transformOffset = 0;

		BuildData.Ranges.Add(RangeInfo);
	}

	BuildData.GeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	BuildData.GeometryInfo.flags = (bFastBuild) ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	if (bAllowUpdate)
	{
		BuildData.GeometryInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	}
	BuildData.GeometryInfo.mode = (BuildMode == EAccelerationStructureBuildMode::Build) ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
	BuildData.GeometryInfo.geometryCount = BuildData.Segments.Num();
	BuildData.GeometryInfo.pGeometries = BuildData.Segments.GetData();

	vkGetAccelerationStructureBuildSizesKHR(
		Device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&BuildData.GeometryInfo,
		BuildData.VertexCounts.GetData(),
		&BuildData.SizesInfo);
}

FVulkanRayTracingGeometry::FVulkanRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer, const FVulkanDevice* InDevice)
{
	// Only supporting triangles initially
	check(Initializer.GeometryType == ERayTracingGeometryType::RTGT_Triangles);

	Device = InDevice;
	DebugName = Initializer.DebugName;
	IndexOffsetInBytes = Initializer.IndexBufferOffset;
	TotalPrimitiveCount = Initializer.TotalPrimitiveCount;
	IndexBufferRHI = Initializer.IndexBuffer;
	bFastBuild = Initializer.bFastBuild;
	bAllowUpdate = Initializer.bAllowUpdate;
	Segments = TArray<FRayTracingGeometrySegment>(Initializer.Segments.GetData(), Initializer.Segments.Num());

	checkf(!IndexBufferRHI || (IndexBufferRHI->GetStride() == 2 || IndexBufferRHI->GetStride() == 4), TEXT("Index buffer must be 16 or 32 bit if in use."));
	IndexStrideInBytes = IndexBufferRHI ? IndexBufferRHI->GetStride() : 0;

	FVkRtBLASBuildData BuildData;
	GetBLASBuildData(
		Device->GetInstanceHandle(), 
		MakeArrayView(Segments),
		IndexBufferRHI, 
		bFastBuild, 
		bAllowUpdate, 
		IndexStrideInBytes, 
		EAccelerationStructureBuildMode::Build,
		BuildData);

	FVulkanRayTracingAllocator::Allocate(
		Device->GetPhysicalHandle(), 
		Device->GetInstanceHandle(),
		BuildData.SizesInfo.accelerationStructureSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Allocation);

	FVulkanRayTracingAllocator::Allocate(
		Device->GetPhysicalHandle(),
		Device->GetInstanceHandle(),
		BuildData.SizesInfo.buildScratchSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Scratch);

	VkBufferDeviceAddressInfoKHR ScratchDeviceAddressInfo;
	ZeroVulkanStruct(ScratchDeviceAddressInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
	ScratchDeviceAddressInfo.buffer = Scratch.Buffer;
	Scratch.Address = vkGetBufferDeviceAddressKHR(Device->GetInstanceHandle(), &ScratchDeviceAddressInfo);
}

FVulkanRayTracingGeometry::~FVulkanRayTracingGeometry()
{
	FVulkanRayTracingAllocator::Free(Allocation);
	FVulkanRayTracingAllocator::Free(Scratch);
}

void FVulkanRayTracingGeometry::BuildAccelerationStructure(FVulkanCommandListContext& CommandContext, EAccelerationStructureBuildMode BuildMode)
{
	check(BuildMode == EAccelerationStructureBuildMode::Build); // Temp

	FVkRtBLASBuildData BuildData;
	GetBLASBuildData(
		Device->GetInstanceHandle(),
		MakeArrayView(Segments),
		IndexBufferRHI,
		bFastBuild,
		bAllowUpdate,
		IndexStrideInBytes,
		BuildMode,
		BuildData);

	VkAccelerationStructureCreateInfoKHR CreateInfo;
	ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
	CreateInfo.buffer = Allocation.Buffer;
	CreateInfo.size = BuildData.SizesInfo.accelerationStructureSize;
	CreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	VERIFYVULKANRESULT(vkCreateAccelerationStructureKHR(Device->GetInstanceHandle(), &CreateInfo, VULKAN_CPU_ALLOCATOR, &Handle));

	BuildData.GeometryInfo.dstAccelerationStructure = Handle;
	BuildData.GeometryInfo.scratchData.deviceAddress = Scratch.Address;

	VkAccelerationStructureBuildRangeInfoKHR* const pBuildRanges = BuildData.Ranges.GetData();

	FVulkanCommandBufferManager& CommandBufferManager = *CommandContext.GetCommandBufferManager();
	FVulkanCmdBuffer* const CmdBuffer = CommandBufferManager.GetActiveCmdBuffer();
	vkCmdBuildAccelerationStructuresKHR(CmdBuffer->GetHandle(), 1, &BuildData.GeometryInfo, &pBuildRanges);

	CommandBufferManager.SubmitActiveCmdBuffer();
	CommandBufferManager.PrepareForNewActiveCommandBuffer();

	VkAccelerationStructureDeviceAddressInfoKHR DeviceAddressInfo;
	ZeroVulkanStruct(DeviceAddressInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
	DeviceAddressInfo.accelerationStructure = Handle;
	Allocation.Address = vkGetAccelerationStructureDeviceAddressKHR(Device->GetInstanceHandle(), &DeviceAddressInfo);

	// No longer need scratch memory for a static build
	if (!bAllowUpdate)
	{
		FVulkanRayTracingAllocator::Free(Scratch);
	}
}

static void GetTLASBuildData(
	const VkDevice Device,
	const uint32 NumInstances,
	const VkDeviceAddress InstanceBufferAddress,
	FVkRtTLASBuildData& BuildData)
{
	VkDeviceOrHostAddressConstKHR InstanceBufferDeviceAddress = {};
	InstanceBufferDeviceAddress.deviceAddress = InstanceBufferAddress;

	BuildData.Geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	BuildData.Geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	BuildData.Geometry.geometry.instances.arrayOfPointers = VK_FALSE;
	BuildData.Geometry.geometry.instances.data = InstanceBufferDeviceAddress;

	BuildData.GeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	BuildData.GeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	BuildData.GeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	BuildData.GeometryInfo.geometryCount = 1;
	BuildData.GeometryInfo.pGeometries = &BuildData.Geometry;

	vkGetAccelerationStructureBuildSizesKHR(
		Device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&BuildData.GeometryInfo,
		&NumInstances,
		&BuildData.SizesInfo);
}

static VkGeometryInstanceFlagsKHR TranslateRayTracingInstanceFlags(ERayTracingInstanceFlags InFlags)
{
	VkGeometryInstanceFlagsKHR Result = 0;

	if (EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::TriangleCullDisable))
	{
		Result |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	}

	if (!EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::TriangleCullReverse))
	{
		// Counterclockwise is the default for UE.
		Result |= VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;
	}

	if (EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::ForceOpaque))
	{
		Result |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
	}

	if (EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::ForceNonOpaque))
	{
		Result |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
	}

	return Result;
}

FVulkanRayTracingScene::FVulkanRayTracingScene(const FRayTracingSceneInitializer& Initializer, const FVulkanDevice* InDevice)
{
	Device = InDevice;
	DebugName = Initializer.DebugName;
	
	Experimental::TSherwoodSet<const FVulkanRayTracingGeometry*> UniqueGeometryReferences;

	for (const FRayTracingGeometryInstance& SceneInstance : Initializer.Instances)
	{
		// Don't support GPU transforms yet.
		ensure(SceneInstance.GPUTransformsSRV == nullptr);

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

		for (int32 TransformIndex = 0; TransformIndex < SceneInstance.Transforms.Num(); ++TransformIndex)
		{
			InstanceGeometry[InstanceDescIndex] = ResourceCast(SceneInstance.GeometryRHI);

			VkAccelerationStructureInstanceKHR& InstanceDesc = InstanceDescs[InstanceDescIndex];
			const FMatrix TransposedTransform = SceneInstance.Transforms[TransformIndex].GetTransposed();
			FMemory::Memcpy(&InstanceDesc.transform, &TransposedTransform.M[0][0], sizeof(VkTransformMatrixKHR));

			InstanceDesc.accelerationStructureReference = 0; // Set during TLAS build after the BLAS for the referenced geometry is built.
			InstanceDesc.instanceCustomIndex = (bUseUniqueUserData) ? SceneInstance.UserData[TransformIndex] : CommonUserData;
			InstanceDesc.mask = SceneInstance.Mask;
			InstanceDesc.instanceShaderBindingTableRecordOffset = 0; // Todo
			InstanceDesc.flags = TranslateRayTracingInstanceFlags(SceneInstance.Flags);

			++InstanceDescIndex;
		}
	}

	// Allocate instance buffer
	const uint32 InstanceBufferByteSize = static_cast<uint32>(NumInstances) * sizeof(VkAccelerationStructureInstanceKHR);

	FVulkanRayTracingAllocator::Allocate(
		Device->GetPhysicalHandle(),
		Device->GetInstanceHandle(),
		InstanceBufferByteSize,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		InstanceBuffer);

	InstanceBuffer.Address = GetDeviceAddress(Device->GetInstanceHandle(), InstanceBuffer.Buffer);

	// Copy instance data
	void* pMappedInstanceBufferMemory = nullptr;
	VERIFYVULKANRESULT(vkMapMemory(Device->GetInstanceHandle(), InstanceBuffer.Memory, 0, InstanceBufferByteSize, 0, &pMappedInstanceBufferMemory));
	{
		VkAccelerationStructureInstanceKHR* const InstanceDescBuffer = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(pMappedInstanceBufferMemory);
		FMemory::Memcpy(InstanceDescBuffer, InstanceDescs.GetData(), InstanceBufferByteSize);

		for (int32 InstanceIndex = 0; InstanceIndex < InstanceDescs.Num(); ++InstanceIndex)
		{
			InstanceDescBuffer[InstanceIndex].accelerationStructureReference = InstanceGeometry[InstanceIndex]->GetAccelerationStructureAddress();
		}
	}
	vkUnmapMemory(Device->GetInstanceHandle(), InstanceBuffer.Memory);

	InstanceDescs.Empty();
	InstanceGeometry.Empty();

	const ERayTracingAccelerationStructureFlags BuildFlags = ERayTracingAccelerationStructureFlags::FastTrace; // #yuriy_todo: pass this in
	SizeInfo = RHICalcRayTracingSceneSize(NumInstances, BuildFlags);
}

FVulkanRayTracingScene::~FVulkanRayTracingScene()
{}

void FVulkanRayTracingScene::BindBuffer(FRHIBuffer* InBuffer, uint32 InBufferOffset)
{
	check(SizeInfo.ResultSize + InBufferOffset <= InBuffer->GetSize());
	check(InBufferOffset % 256 == 0); // Spec requires offset to be a multiple of 256
	AccelerationStructureBuffer = static_cast<FVulkanAccelerationStructureBuffer*>(InBuffer);
	BufferOffset = InBufferOffset;
}

void FVulkanRayTracingScene::BuildAccelerationStructure(
	FVulkanCommandListContext& CommandContext,
	FVulkanResourceMultiBuffer* InScratchBuffer, uint32 InScratchOffset,
	FVulkanResourceMultiBuffer* InInstanceBuffer, uint32 InInstanceOffset,
	uint32 NumInstanceDescs)
{
	check(AccelerationStructureBuffer.IsValid());
	check(InInstanceBuffer == nullptr); // External instance buffer not supported yet
	const bool bExternalScratchBuffer = InScratchBuffer != nullptr;

	FVkRtTLASBuildData BuildData;
	GetTLASBuildData(Device->GetInstanceHandle(), NumInstances, InstanceBuffer.Address, BuildData);

	if (!bExternalScratchBuffer)
	{
		FVulkanRayTracingAllocator::Allocate(
			Device->GetPhysicalHandle(),
			Device->GetInstanceHandle(),
			BuildData.SizesInfo.buildScratchSize,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Scratch);

		Scratch.Address = GetDeviceAddress(Device->GetInstanceHandle(), Scratch.Buffer);
	}

	VkAccelerationStructureCreateInfoKHR TLASCreateInfo;
	ZeroVulkanStruct(TLASCreateInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
	TLASCreateInfo.buffer = AccelerationStructureBuffer->GetBuffer();
	TLASCreateInfo.offset = BufferOffset;
	TLASCreateInfo.size = BuildData.SizesInfo.accelerationStructureSize;
	TLASCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	VERIFYVULKANRESULT(vkCreateAccelerationStructureKHR(Device->GetInstanceHandle(), &TLASCreateInfo, VULKAN_CPU_ALLOCATOR, &Handle));

	BuildData.GeometryInfo.dstAccelerationStructure = Handle;

	if (bExternalScratchBuffer)
	{
		BuildData.GeometryInfo.scratchData.deviceAddress = GetDeviceAddress(Device->GetInstanceHandle(), InScratchBuffer->GetHandle()) + InScratchOffset;
	}
	else
	{
		BuildData.GeometryInfo.scratchData.deviceAddress = Scratch.Address;
	}

	VkAccelerationStructureBuildRangeInfoKHR TLASBuildRangeInfo;
	TLASBuildRangeInfo.primitiveCount = NumInstances;
	TLASBuildRangeInfo.primitiveOffset = 0;
	TLASBuildRangeInfo.transformOffset = 0;
	TLASBuildRangeInfo.firstVertex = 0;

	VkAccelerationStructureBuildRangeInfoKHR* const pBuildRanges = &TLASBuildRangeInfo;

	FVulkanCommandBufferManager& CommandBufferManager = *CommandContext.GetCommandBufferManager();
	FVulkanCmdBuffer* const CmdBuffer = CommandBufferManager.GetActiveCmdBuffer();
	vkCmdBuildAccelerationStructuresKHR(CmdBuffer->GetHandle(), 1, &BuildData.GeometryInfo, &pBuildRanges);

	CommandBufferManager.SubmitActiveCmdBuffer();
	CommandBufferManager.PrepareForNewActiveCommandBuffer();

	FVulkanRayTracingAllocator::Free(InstanceBuffer);

	if (!bExternalScratchBuffer)
	{
		FVulkanRayTracingAllocator::Free(Scratch);
	}
}

FRayTracingAccelerationStructureSize FVulkanDynamicRHI::RHICalcRayTracingSceneSize(uint32 MaxInstances, ERayTracingAccelerationStructureFlags Flags)
{
	FVkRtTLASBuildData BuildData;
	const VkDeviceAddress InstanceBufferAddress = 0; // No device address available when only querying TLAS size
	GetTLASBuildData(Device->GetInstanceHandle(), MaxInstances, InstanceBufferAddress, BuildData);

	FRayTracingAccelerationStructureSize Result;
	Result.ResultSize = BuildData.SizesInfo.accelerationStructureSize;
	Result.BuildScratchSize = BuildData.SizesInfo.buildScratchSize;
	Result.UpdateScratchSize = BuildData.SizesInfo.updateScratchSize;

	return Result;
}

FRayTracingAccelerationStructureSize FVulkanDynamicRHI::RHICalcRayTracingGeometrySize(const FRayTracingGeometryInitializer& Initializer)
{	
	const uint32 IndexStrideInBytes = Initializer.IndexBuffer ? Initializer.IndexBuffer->GetStride() : 0;

	FVkRtBLASBuildData BuildData;
	GetBLASBuildData(
		Device->GetInstanceHandle(),
		MakeArrayView(Initializer.Segments),
		Initializer.IndexBuffer,
		Initializer.bFastBuild,
		Initializer.bAllowUpdate,
		IndexStrideInBytes,
		EAccelerationStructureBuildMode::Build,
		BuildData);

	FRayTracingAccelerationStructureSize Result;
	Result.ResultSize = BuildData.SizesInfo.accelerationStructureSize;
	Result.BuildScratchSize = BuildData.SizesInfo.buildScratchSize;
	Result.UpdateScratchSize = BuildData.SizesInfo.updateScratchSize;
	
	return Result;
}

FRayTracingSceneRHIRef FVulkanDynamicRHI::RHICreateRayTracingScene(const FRayTracingSceneInitializer& Initializer)
{
	return new FVulkanRayTracingScene(Initializer, GetDevice());
}

FRayTracingGeometryRHIRef FVulkanDynamicRHI::RHICreateRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer)
{
	return new FVulkanRayTracingGeometry(Initializer, GetDevice());
}

void FVulkanCommandListContext::RHIBindAccelerationStructureMemory(FRHIRayTracingScene* Scene, FRHIBuffer* Buffer, uint32 BufferOffset)
{
	ResourceCast(Scene)->BindBuffer(Buffer, BufferOffset);
}

// Todo: High level rhi call should have transitioned and verified vb and ib to read for each segment
void FVulkanCommandListContext::RHIBuildAccelerationStructures(const TArrayView<const FRayTracingGeometryBuildParams> Params)
{
	for (const FRayTracingGeometryBuildParams& P : Params)
	{
		FVulkanRayTracingGeometry* const Geometry = ResourceCast(P.Geometry.GetReference());

		// Todo: Update geometry from params for each segment
		// Todo: Can we do this only for an update?

		// Build as for each segment
		Geometry->BuildAccelerationStructure(*this, P.BuildMode);
	}
}

void FVulkanCommandListContext::RHIBuildAccelerationStructure(const FRayTracingSceneBuildParams& SceneBuildParams)
{
	FVulkanRayTracingScene* const Scene = ResourceCast(SceneBuildParams.Scene);
	FVulkanResourceMultiBuffer* const ScratchBuffer = ResourceCast(SceneBuildParams.ScratchBuffer);
	FVulkanResourceMultiBuffer* const InstanceBuffer = ResourceCast(SceneBuildParams.InstanceBuffer);
	Scene->BuildAccelerationStructure(
		*this, 
		ScratchBuffer, SceneBuildParams.ScratchBufferOffset, 
		InstanceBuffer, SceneBuildParams.InstanceBufferOffset, 
		SceneBuildParams.NumInstances);
}

void FVulkanCommandListContext::RHIRayTraceOcclusion(FRHIRayTracingScene* Scene, FRHIShaderResourceView* Rays, FRHIUnorderedAccessView* Output, uint32 NumRays)
{
	// todo
	return;
}

void FVulkanDevice::InitializeRayTracing()
{
	// todo
	return;
}

void FVulkanDevice::CleanUpRayTracing()
{
	// todo
	return;
}
#endif // #if VULKAN_RHI_RAYTRACING
