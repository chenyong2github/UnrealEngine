// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanState.h: Vulkan state definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "VulkanRHIPrivate.h"
#include "VulkanResources.h"
#include "VulkanPendingState.h"
#include "VulkanContext.h"

template <typename TAttachmentReferenceType>
struct FVulkanAttachmentReference
	: public TAttachmentReferenceType
{
	FVulkanAttachmentReference()
	{
		ZeroStruct();
	}

	FVulkanAttachmentReference(const VkAttachmentReference& AttachmentReferenceIn, VkImageAspectFlags AspectMask)
	{
		SetAttachment(AttachmentReferenceIn, AspectMask);
	}

	inline void SetAttachment(const VkAttachmentReference& AttachmentReferenceIn, VkImageAspectFlags AspectMask) { check(false); }
	inline void SetAttachment(const FVulkanAttachmentReference<TAttachmentReferenceType>& AttachmentReferenceIn, VkImageAspectFlags AspectMask) { *this = AttachmentReferenceIn; }
	inline void ZeroStruct() {}
	inline void SetAspect(uint32 Aspect) {}
};

template <>
inline void FVulkanAttachmentReference<VkAttachmentReference>::SetAttachment(const VkAttachmentReference& AttachmentReferenceIn, VkImageAspectFlags AspectMask)
{
	attachment = AttachmentReferenceIn.attachment;
	layout = AttachmentReferenceIn.layout;
}

#if VULKAN_SUPPORTS_RENDERPASS2
template <>
inline void FVulkanAttachmentReference<VkAttachmentReference2>::SetAttachment(const VkAttachmentReference& AttachmentReferenceIn, VkImageAspectFlags AspectMask)
{
	sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
	pNext = nullptr;
	attachment = AttachmentReferenceIn.attachment;
	layout = AttachmentReferenceIn.layout;
	aspectMask = AspectMask;
}

template<>
inline void FVulkanAttachmentReference<VkAttachmentReference2>::SetAttachment(const FVulkanAttachmentReference<VkAttachmentReference2>& AttachmentReferenceIn, VkImageAspectFlags AspectMask)
{
	sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
	pNext = nullptr;
	attachment = AttachmentReferenceIn.attachment;
	layout = AttachmentReferenceIn.layout;
	aspectMask = AspectMask;
}
#endif

template<>
inline void FVulkanAttachmentReference<VkAttachmentReference>::ZeroStruct()
{
	attachment = 0;
	layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

#if VULKAN_SUPPORTS_RENDERPASS2
template<>
inline void FVulkanAttachmentReference<VkAttachmentReference2>::ZeroStruct()
{
	sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
	pNext = nullptr;
	attachment = 0;
	layout = VK_IMAGE_LAYOUT_UNDEFINED;
	aspectMask = 0;
}

template<>
inline void FVulkanAttachmentReference<VkAttachmentReference2>::SetAspect(uint32 Aspect)
{
	aspectMask = Aspect;
}
#endif

template <typename TSubpassDescriptionType>
class FVulkanSubpassDescription
{
};

template<>
struct FVulkanSubpassDescription<VkSubpassDescription>
	: public VkSubpassDescription
{
	FVulkanSubpassDescription()
	{
		FMemory::Memzero(this, sizeof(VkSubpassDescription));
		pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	}

	void SetColorAttachments(const TArray<FVulkanAttachmentReference<VkAttachmentReference>>& ColorAttachmentReferences, int OverrideCount = -1)
	{
		colorAttachmentCount = (OverrideCount == -1) ? ColorAttachmentReferences.Num() : OverrideCount;
		pColorAttachments = ColorAttachmentReferences.GetData();
	}

	void SetResolveAttachments(const TArray<FVulkanAttachmentReference<VkAttachmentReference>>& ResolveAttachmentReferences)
	{
		if (ResolveAttachmentReferences.Num() > 0)
		{
			check(colorAttachmentCount == ResolveAttachmentReferences.Num());
			pResolveAttachments = ResolveAttachmentReferences.GetData();
		}
	}

	void SetDepthStencilAttachment(FVulkanAttachmentReference<VkAttachmentReference>* DepthStencilAttachmentReference)
	{
		pDepthStencilAttachment = static_cast<VkAttachmentReference*>(DepthStencilAttachmentReference);
	}

	void SetInputAttachments(FVulkanAttachmentReference<VkAttachmentReference>* InputAttachmentReferences, uint32 NumInputAttachmentReferences)
	{
		pInputAttachments = static_cast<VkAttachmentReference*>(InputAttachmentReferences);
		inputAttachmentCount = NumInputAttachmentReferences;
	}

	void SetShadingRateAttachment(void* /* ShadingRateAttachmentInfo */)
	{
		// No-op without VK_KHR_create_renderpass2
	}

	void SetMultiViewMask(uint32_t Mask)
	{
		// No-op without VK_KHR_create_renderpass2
	}
};

#if VULKAN_SUPPORTS_RENDERPASS2
template<>
struct FVulkanSubpassDescription<VkSubpassDescription2>
	: public VkSubpassDescription2
{
	FVulkanSubpassDescription()
	{
		ZeroVulkanStruct(*this, VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2);
		pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		viewMask = 0;
	}

	void SetColorAttachments(const TArray<FVulkanAttachmentReference<VkAttachmentReference2>>& ColorAttachmentReferences, int OverrideCount = -1)
	{
		colorAttachmentCount = OverrideCount == -1 ? ColorAttachmentReferences.Num() : OverrideCount;
		pColorAttachments = ColorAttachmentReferences.GetData();
	}

	void SetResolveAttachments(const TArray<FVulkanAttachmentReference<VkAttachmentReference2>>& ResolveAttachmentReferences)
	{
		if (ResolveAttachmentReferences.Num() > 0)
		{
			check(colorAttachmentCount == ResolveAttachmentReferences.Num());
			pResolveAttachments = ResolveAttachmentReferences.GetData();
		}
	}

	void SetDepthStencilAttachment(FVulkanAttachmentReference<VkAttachmentReference2>* DepthStencilAttachmentReference)
	{
		pDepthStencilAttachment = static_cast<VkAttachmentReference2*>(DepthStencilAttachmentReference);
	}

	void SetInputAttachments(FVulkanAttachmentReference<VkAttachmentReference2>* InputAttachmentReferences, uint32 NumInputAttachmentReferences)
	{
		pInputAttachments = static_cast<VkAttachmentReference2*>(InputAttachmentReferences);
		inputAttachmentCount = NumInputAttachmentReferences;
	}

	void SetShadingRateAttachment(void* ShadingRateAttachmentInfo)
	{
		pNext = ShadingRateAttachmentInfo;
	}

	void SetMultiViewMask(uint32_t Mask)
	{
		viewMask = Mask;
	}
};
#endif

template <typename TSubpassDependencyType>
struct FVulkanSubpassDependency
	: public TSubpassDependencyType
{
};

template<>
struct FVulkanSubpassDependency<VkSubpassDependency>
	: public VkSubpassDependency
{
	FVulkanSubpassDependency()
	{
		FMemory::Memzero(this, sizeof(VkSubpassDependency));
	}
};

#if VULKAN_SUPPORTS_RENDERPASS2
template<>
struct FVulkanSubpassDependency<VkSubpassDependency2>
	: public VkSubpassDependency2
{
	FVulkanSubpassDependency()
	{
		ZeroVulkanStruct(*this, VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2);
		viewOffset = 0;		// According to the Vulkan spec: "If dependencyFlags does not include VK_DEPENDENCY_VIEW_LOCAL_BIT, viewOffset must be 0"
	}
};
#endif

template<typename TAttachmentDescriptionType>
struct FVulkanAttachmentDescription
{
};

template<>
struct FVulkanAttachmentDescription<VkAttachmentDescription>
	: public VkAttachmentDescription
{
	FVulkanAttachmentDescription()
	{
		FMemory::Memzero(this, sizeof(VkAttachmentDescription));
	}

	FVulkanAttachmentDescription(const VkAttachmentDescription& InDesc)
	{
		flags = InDesc.flags;
		format = InDesc.format;
		samples = InDesc.samples;
		loadOp = InDesc.loadOp;
		storeOp = InDesc.storeOp;
		stencilLoadOp = InDesc.stencilLoadOp;
		stencilStoreOp = InDesc.stencilStoreOp;
		initialLayout = InDesc.initialLayout;
		finalLayout = InDesc.finalLayout;
	}
};

#if VULKAN_SUPPORTS_RENDERPASS2
template<>
struct FVulkanAttachmentDescription<VkAttachmentDescription2>
	: public VkAttachmentDescription2
{
	FVulkanAttachmentDescription()
	{
		ZeroVulkanStruct(*this, VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2);
	}

	FVulkanAttachmentDescription(const VkAttachmentDescription& InDesc)
	{
		sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
		pNext = nullptr;
		flags = InDesc.flags;
		format = InDesc.format;
		samples = InDesc.samples;
		loadOp = InDesc.loadOp;
		storeOp = InDesc.storeOp;
		stencilLoadOp = InDesc.stencilLoadOp;
		stencilStoreOp = InDesc.stencilStoreOp;
		initialLayout = InDesc.initialLayout;
		finalLayout = InDesc.finalLayout;
	}
};
#endif

template <typename T>
struct FVulkanRenderPassCreateInfo
{};

template<>
struct FVulkanRenderPassCreateInfo<VkRenderPassCreateInfo>
	: public VkRenderPassCreateInfo
{
	FVulkanRenderPassCreateInfo()
	{
		ZeroVulkanStruct(*this, VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
	}

	void SetCorrelationMask(const uint32_t* MaskPtr)
	{
		// No-op without VK_KHR_create_renderpass2
	}

	VkRenderPass Create(FVulkanDevice& Device)
	{
		VkRenderPass Handle = VK_NULL_HANDLE;
		VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkCreateRenderPass(Device.GetInstanceHandle(), this, VULKAN_CPU_ALLOCATOR, &Handle));
		return Handle;
	}
};

#if VULKAN_SUPPORTS_RENDERPASS2
template<>
struct FVulkanRenderPassCreateInfo<VkRenderPassCreateInfo2>
	: public VkRenderPassCreateInfo2
{
	FVulkanRenderPassCreateInfo()
	{
		ZeroVulkanStruct(*this, VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2);
	}

	void SetCorrelationMask(const uint32_t* MaskPtr)
	{
		correlatedViewMaskCount = 1;
		pCorrelatedViewMasks = MaskPtr;
	}

	VkRenderPass Create(FVulkanDevice& Device)
	{
		VkRenderPass Handle = VK_NULL_HANDLE;
		VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkCreateRenderPass2KHR(Device.GetInstanceHandle(), this, VULKAN_CPU_ALLOCATOR, &Handle));
		return Handle;
	}
};

struct FVulkanFragmentShadingRateAttachmentInfo
	: public VkFragmentShadingRateAttachmentInfoKHR
{
	FVulkanFragmentShadingRateAttachmentInfo()
	{
		ZeroVulkanStruct(*this, VK_STRUCTURE_TYPE_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR);
		// For now, just use the smallest tile-size available. TODO: Add a setting to allow prioritizing either higher resolution/larger shading rate attachment targets 
		// or lower-resolution/smaller attachments.
		shadingRateAttachmentTexelSize = { (uint32)GRHIVariableRateShadingImageTileMinWidth, (uint32)GRHIVariableRateShadingImageTileMinHeight };
	}

	void SetReference(FVulkanAttachmentReference<VkAttachmentReference2>* AttachmentReference)
	{
		pFragmentShadingRateAttachment = AttachmentReference;
	}
};
#endif

VkRenderPass CreateVulkanRenderPass(FVulkanDevice& Device, const FVulkanRenderTargetLayout& RTLayout);

