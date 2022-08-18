// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanState.cpp: Vulkan state implementation.
=============================================================================*/

#include "VulkanRenderpass.h"

extern int32 GVulkanInputAttachmentShaderRead;

template <typename TSubpassDescriptionClass, typename TSubpassDependencyClass, typename TAttachmentReferenceClass, typename TAttachmentDescriptionClass, typename TRenderPassCreateInfoClass>
class FVulkanRenderPassBuilder
{
public:
	FVulkanRenderPassBuilder(FVulkanDevice& InDevice)
		: Device(InDevice)
	{}

	VkRenderPass Create(const FVulkanRenderTargetLayout& RTLayout)
	{
		TRenderPassCreateInfoClass CreateInfo;

		uint32 NumSubpasses = 0;
		uint32 NumDependencies = 0;

		//0b11 for 2, 0b1111 for 4, and so on
		uint32 MultiviewMask = (0b1 << RTLayout.GetMultiViewCount()) - 1;

		const bool bDeferredShadingSubpass = RTLayout.GetSubpassHint() == ESubpassHint::DeferredShadingSubpass;
		const bool bDepthReadSubpass = RTLayout.GetSubpassHint() == ESubpassHint::DepthReadSubpass;
		const bool bApplyFragmentShadingRate =  GRHISupportsAttachmentVariableRateShading && GRHIVariableRateShadingEnabled && GRHIAttachmentVariableRateShadingEnabled && RTLayout.GetFragmentDensityAttachmentReference() != nullptr;

#if VULKAN_SUPPORTS_RENDERPASS2
		FVulkanAttachmentReference<VkAttachmentReference2> ShadingRateAttachmentReference;
		FVulkanFragmentShadingRateAttachmentInfo FragmentShadingRateAttachmentInfo;
		if (bApplyFragmentShadingRate)
		{
			ShadingRateAttachmentReference.SetAttachment(*RTLayout.GetFragmentDensityAttachmentReference(), VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT);
			FragmentShadingRateAttachmentInfo.SetReference(&ShadingRateAttachmentReference);
		}
#endif

		// Grab (and optionally convert) attachment references.
		for (uint32 ColorAttachment = 0; ColorAttachment < RTLayout.GetNumColorAttachments(); ++ColorAttachment)
		{
			ColorAttachmentReferences.Add(TAttachmentReferenceClass(RTLayout.GetColorAttachmentReferences()[ColorAttachment], 0));
			if (RTLayout.GetResolveAttachmentReferences() != nullptr)
			{
				ResolveAttachmentReferences.Add(TAttachmentReferenceClass(RTLayout.GetResolveAttachmentReferences()[ColorAttachment], 0));
			}
		}

		if (RTLayout.GetDepthStencilAttachmentReference() != nullptr)
		{
			DepthStencilAttachmentReference = TAttachmentReferenceClass(*RTLayout.GetDepthStencilAttachmentReference(), 0);
		}

		// main sub-pass
		{
			TSubpassDescriptionClass& SubpassDesc = SubpassDescriptions[NumSubpasses++];

			SubpassDesc.SetColorAttachments(ColorAttachmentReferences);
			if (!bDepthReadSubpass)
			{
				// only set resolve attachment on the last subpass
				SubpassDesc.SetResolveAttachments(ResolveAttachmentReferences);
			}
			if (RTLayout.GetDepthStencilAttachmentReference() != nullptr)
			{
				SubpassDesc.SetDepthStencilAttachment(&DepthStencilAttachmentReference);
			}

#if VULKAN_SUPPORTS_RENDERPASS2
			if (bApplyFragmentShadingRate)
			{
				SubpassDesc.SetShadingRateAttachment(&FragmentShadingRateAttachmentInfo);
			}
			SubpassDesc.SetMultiViewMask(MultiviewMask);
#endif
		}

		// Color write and depth read sub-pass
		static const uint32 InputAttachment1Count = 1;
		TAttachmentReferenceClass InputAttachments1[InputAttachment1Count];
		TAttachmentReferenceClass DepthStencilAttachmentOG;
		if (bDepthReadSubpass)
		{
			DepthStencilAttachmentOG.SetAttachment(*RTLayout.GetDepthStencilAttachmentReference(), VK_IMAGE_ASPECT_DEPTH_BIT);
			TSubpassDescriptionClass& SubpassDesc = SubpassDescriptions[NumSubpasses++];

			SubpassDesc.SetColorAttachments(ColorAttachmentReferences);
			SubpassDesc.SetResolveAttachments(ResolveAttachmentReferences);

			check(RTLayout.GetDepthStencilAttachmentReference());

			// Depth as Input0
			InputAttachments1[0].SetAttachment(DepthStencilAttachmentOG, VK_IMAGE_ASPECT_DEPTH_BIT);
			InputAttachments1[0].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

			SubpassDesc.SetInputAttachments(InputAttachments1, InputAttachment1Count);
			// depth attachment is same as input attachment
			SubpassDesc.SetDepthStencilAttachment(InputAttachments1);

#if VULKAN_SUPPORTS_RENDERPASS2
			if (bApplyFragmentShadingRate)
			{
				SubpassDesc.SetShadingRateAttachment(&FragmentShadingRateAttachmentInfo);
			}
			SubpassDesc.SetMultiViewMask(MultiviewMask);
#endif
			
			TSubpassDependencyClass& SubpassDep = SubpassDependencies[NumDependencies++];
			SubpassDep.srcSubpass = 0;
			SubpassDep.dstSubpass = 1;
			SubpassDep.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			SubpassDep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			SubpassDep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			SubpassDep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			SubpassDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		}

		// Two subpasses for deferred shading
		TAttachmentReferenceClass InputAttachments2[MaxSimultaneousRenderTargets + 1];
		TAttachmentReferenceClass DepthStencilAttachment;
		if (bDeferredShadingSubpass)
		{
			// both sub-passes only test DepthStencil
			DepthStencilAttachment.attachment = RTLayout.GetDepthStencilAttachmentReference()->attachment;
			DepthStencilAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			DepthStencilAttachment.SetAspect(VK_IMAGE_ASPECT_DEPTH_BIT);	// @todo?

			//const VkAttachmentReference* ColorRef = RTLayout.GetColorAttachmentReferences();
			//uint32 NumColorAttachments = RTLayout.GetNumColorAttachments();
			//check(RTLayout.GetNumColorAttachments() == 5); //current layout is SceneColor, GBufferA/B/C/D

			// 1. Write to SceneColor and GBuffer, input DepthStencil
			{
				TSubpassDescriptionClass& SubpassDesc = SubpassDescriptions[NumSubpasses++];
				SubpassDesc.SetColorAttachments(ColorAttachmentReferences);
				SubpassDesc.SetDepthStencilAttachment(&DepthStencilAttachment);
				SubpassDesc.SetInputAttachments(&DepthStencilAttachment, 1);

#if VULKAN_SUPPORTS_RENDERPASS2
				if (bApplyFragmentShadingRate)
				{
					SubpassDesc.SetShadingRateAttachment(&FragmentShadingRateAttachmentInfo);
				}
				SubpassDesc.SetMultiViewMask(MultiviewMask);
#endif
				
				// Depth as Input0
				TSubpassDependencyClass& SubpassDep = SubpassDependencies[NumDependencies++];
				SubpassDep.srcSubpass = 0;
				SubpassDep.dstSubpass = 1;
				SubpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				SubpassDep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				SubpassDep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				SubpassDep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
				SubpassDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			}

			// 2. Write to SceneColor, input GBuffer and DepthStencil
			{
				TSubpassDescriptionClass& SubpassDesc = SubpassDescriptions[NumSubpasses++];
				SubpassDesc.SetColorAttachments(ColorAttachmentReferences, 1); // SceneColor only
				SubpassDesc.SetDepthStencilAttachment(&DepthStencilAttachment);

				// Depth as Input0
				InputAttachments2[0].attachment = DepthStencilAttachment.attachment;
				InputAttachments2[0].layout = DepthStencilAttachment.layout;
				InputAttachments2[0].SetAspect(VK_IMAGE_ASPECT_DEPTH_BIT);

				// SceneColor write only
				InputAttachments2[1].attachment = VK_ATTACHMENT_UNUSED;
				InputAttachments2[1].layout = VK_IMAGE_LAYOUT_UNDEFINED;
				InputAttachments2[1].SetAspect(0);
				
				// GBufferA/B/C/D as Input2/3/4/5
				int32 NumColorInputs = ColorAttachmentReferences.Num() - 1;
				for (int32 i = 2; i < (NumColorInputs + 2); ++i)
				{
					InputAttachments2[i].attachment = ColorAttachmentReferences[i - 1].attachment;
					InputAttachments2[i].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					InputAttachments2[i].SetAspect(VK_IMAGE_ASPECT_COLOR_BIT);
				}

				SubpassDesc.SetInputAttachments(InputAttachments2, NumColorInputs + 2);
#if VULKAN_SUPPORTS_RENDERPASS2
				if (bApplyFragmentShadingRate)
				{
					SubpassDesc.SetShadingRateAttachment(&FragmentShadingRateAttachmentInfo);
				}
				SubpassDesc.SetMultiViewMask(MultiviewMask);
#endif

				TSubpassDependencyClass& SubpassDep = SubpassDependencies[NumDependencies++];
				SubpassDep.srcSubpass = 1;
				SubpassDep.dstSubpass = 2;
				SubpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				SubpassDep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				SubpassDep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				SubpassDep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
				if (GVulkanInputAttachmentShaderRead == 1)
				{
					// this is not required, but might flicker on some devices without
					SubpassDep.dstAccessMask |= VK_ACCESS_SHADER_READ_BIT;
				}
				SubpassDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			}
		}

		TArray<TAttachmentDescriptionClass> AttachmentDescriptions;
		for (uint32 Attachment = 0; Attachment < RTLayout.GetNumAttachmentDescriptions(); ++Attachment)
		{
			AttachmentDescriptions.Add(TAttachmentDescriptionClass(RTLayout.GetAttachmentDescriptions()[Attachment]));
		}

		CreateInfo.attachmentCount = AttachmentDescriptions.Num();
		CreateInfo.pAttachments = AttachmentDescriptions.GetData();
		CreateInfo.subpassCount = NumSubpasses;
		CreateInfo.pSubpasses = SubpassDescriptions;
		CreateInfo.dependencyCount = NumDependencies;
		CreateInfo.pDependencies = SubpassDependencies;

		/*
		Bit mask that specifies which view rendering is broadcast to
		0011 = Broadcast to first and second view (layer)
		*/
		const uint32_t ViewMask[2] = { MultiviewMask, MultiviewMask };

		/*
		Bit mask that specifices correlation between views
		An implementation may use this for optimizations (concurrent render)
		*/
		const uint32_t CorrelationMask = MultiviewMask;

		VkRenderPassMultiviewCreateInfo MultiviewInfo;
		if (RTLayout.GetIsMultiView())
		{
#if VULKAN_SUPPORTS_RENDERPASS2
			if (Device.GetOptionalExtensions().HasKHRRenderPass2)
			{
				CreateInfo.SetCorrelationMask(&CorrelationMask);
			}
			else
#endif
			{
				checkf(Device.GetOptionalExtensions().HasKHRMultiview, TEXT("Layout is multiview but extension is not supported!"))
				ZeroVulkanStruct(MultiviewInfo, VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO);
				MultiviewInfo.subpassCount = NumSubpasses;
				MultiviewInfo.pViewMasks = ViewMask;
				MultiviewInfo.dependencyCount = 0;
				MultiviewInfo.pViewOffsets = nullptr;
				MultiviewInfo.correlationMaskCount = 1;
				MultiviewInfo.pCorrelationMasks = &CorrelationMask;

				MultiviewInfo.pNext = CreateInfo.pNext;
				CreateInfo.pNext = &MultiviewInfo;
			}
		}


		VkRenderPassFragmentDensityMapCreateInfoEXT FragDensityCreateInfo;
		if (Device.GetOptionalExtensions().HasEXTFragmentDensityMap && RTLayout.GetHasFragmentDensityAttachment())
		{
			ZeroVulkanStruct(FragDensityCreateInfo, VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT);
			FragDensityCreateInfo.fragmentDensityMapAttachment = *RTLayout.GetFragmentDensityAttachmentReference();

			// Chain fragment density info onto create info and the rest of the pNexts
			// onto the fragment density info
			FragDensityCreateInfo.pNext = CreateInfo.pNext;
			CreateInfo.pNext = &FragDensityCreateInfo;
		}

#if VULKAN_SUPPORTS_QCOM_RENDERPASS_TRANSFORM
		if (RTLayout.GetQCOMRenderPassTransform() != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		{
			CreateInfo.flags = VK_RENDER_PASS_CREATE_TRANSFORM_BIT_QCOM;
		}
#endif

		return CreateInfo.Create(Device);
	}

private:
	TSubpassDescriptionClass SubpassDescriptions[8];
	TSubpassDependencyClass SubpassDependencies[8];

	TArray<TAttachmentReferenceClass> ColorAttachmentReferences;
	TArray<TAttachmentReferenceClass> ResolveAttachmentReferences;
	TAttachmentReferenceClass DepthStencilAttachmentReference;

	FVulkanDevice& Device;
};

VkRenderPass CreateVulkanRenderPass(FVulkanDevice& InDevice, const FVulkanRenderTargetLayout& RTLayout)
{
	VkRenderPass OutRenderpass;

#if VULKAN_SUPPORTS_RENDERPASS2
	if (InDevice.GetOptionalExtensions().HasKHRRenderPass2)
	{
		FVulkanRenderPassBuilder<FVulkanSubpassDescription<VkSubpassDescription2>, FVulkanSubpassDependency<VkSubpassDependency2>, FVulkanAttachmentReference<VkAttachmentReference2>, FVulkanAttachmentDescription<VkAttachmentDescription2>, FVulkanRenderPassCreateInfo<VkRenderPassCreateInfo2>> Creator(InDevice);
		OutRenderpass = Creator.Create(RTLayout);
	}
	else
#endif
	{
		FVulkanRenderPassBuilder<FVulkanSubpassDescription<VkSubpassDescription>, FVulkanSubpassDependency<VkSubpassDependency>, FVulkanAttachmentReference<VkAttachmentReference>, FVulkanAttachmentDescription<VkAttachmentDescription>, FVulkanRenderPassCreateInfo<VkRenderPassCreateInfo>> Creator(InDevice);
		OutRenderpass = Creator.Create(RTLayout);
	}

	return OutRenderpass;
}

