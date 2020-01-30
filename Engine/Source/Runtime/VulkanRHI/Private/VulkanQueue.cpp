// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanQueue.cpp: Vulkan Queue implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanQueue.h"
#include "VulkanMemory.h"
#include "VulkanContext.h"

int32 GWaitForIdleOnSubmit = 0;
FAutoConsoleVariableRef CVarVulkanWaitForIdleOnSubmit(
	TEXT("r.Vulkan.WaitForIdleOnSubmit"),
	GWaitForIdleOnSubmit,
	TEXT("Waits for the GPU to be idle after submitting a command buffer. Useful for tracking GPU hangs.\n")
	TEXT(" 0: Do not wait(default)\n")
	TEXT(" 1: Wait on every submit\n")
	TEXT(" 2: Wait when submitting an upload buffer\n")
	TEXT(" 3: Wait when submitting an active buffer (one that has gfx commands)\n"),
	ECVF_Default
	);

FVulkanQueue::FVulkanQueue(FVulkanDevice* InDevice, uint32 InFamilyIndex)
	: Queue(VK_NULL_HANDLE)
	, FamilyIndex(InFamilyIndex)
	, QueueIndex(0)
	, Device(InDevice)
	, LastSubmittedCmdBuffer(nullptr)
	, LastSubmittedCmdBufferFenceCounter(0)
	, SubmitCounter(0)
{
	VulkanRHI::vkGetDeviceQueue(Device->GetInstanceHandle(), FamilyIndex, QueueIndex, &Queue);
}

FVulkanQueue::~FVulkanQueue()
{
	check(Device);
}

void FVulkanQueue::Submit(FVulkanCmdBuffer* CmdBuffer, uint32 NumSignalSemaphores, VkSemaphore* SignalSemaphores)
{
	check(CmdBuffer->HasEnded());

	VulkanRHI::FFence* Fence = CmdBuffer->Fence;
	check(!Fence->IsSignaled());

	const VkCommandBuffer CmdBuffers[] = { CmdBuffer->GetHandle() };

	VkSubmitInfo SubmitInfo;
	ZeroVulkanStruct(SubmitInfo, VK_STRUCTURE_TYPE_SUBMIT_INFO);
	SubmitInfo.commandBufferCount = 1;
	SubmitInfo.pCommandBuffers = CmdBuffers;
	SubmitInfo.signalSemaphoreCount = NumSignalSemaphores;
	SubmitInfo.pSignalSemaphores = SignalSemaphores;

	TArray<VkSemaphore> WaitSemaphores;
	if (CmdBuffer->WaitSemaphores.Num() > 0)
	{
		WaitSemaphores.Empty((uint32)CmdBuffer->WaitSemaphores.Num());
		for (VulkanRHI::FSemaphore* Semaphore : CmdBuffer->WaitSemaphores)
		{
			WaitSemaphores.Add(Semaphore->GetHandle());
		}
		SubmitInfo.waitSemaphoreCount = (uint32)CmdBuffer->WaitSemaphores.Num();
		SubmitInfo.pWaitSemaphores = WaitSemaphores.GetData();
		SubmitInfo.pWaitDstStageMask = CmdBuffer->WaitFlags.GetData();
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_VulkanQueueSubmit);
		VERIFYVULKANRESULT(VulkanRHI::vkQueueSubmit(Queue, 1, &SubmitInfo, Fence->GetHandle()));
	}

	CmdBuffer->State = FVulkanCmdBuffer::EState::Submitted;
	CmdBuffer->MarkSemaphoresAsSubmitted();
	CmdBuffer->SubmittedFenceCounter = CmdBuffer->FenceSignaledCounter;

	bool bShouldStall = false;

	if (GWaitForIdleOnSubmit != 0)
	{
		FVulkanCommandBufferManager* CmdBufferMgr = Device->GetImmediateContext().GetCommandBufferManager();

		switch(GWaitForIdleOnSubmit)
		{
			default:
				// intentional fall-through
			case 1:
				bShouldStall = true;
				break;

			case 2:
				bShouldStall = (CmdBufferMgr->HasPendingUploadCmdBuffer() && CmdBufferMgr->GetUploadCmdBuffer() == CmdBuffer);
				break;

			case 3:
				bShouldStall = (CmdBufferMgr->HasPendingActiveCmdBuffer() && CmdBufferMgr->GetActiveCmdBufferDirect() == CmdBuffer);
				break;
		}
	}

	if (bShouldStall)
	{
		// 200 ms timeout
		bool bSuccess = Device->GetFenceManager().WaitForFence(CmdBuffer->Fence, 200 * 1000 * 1000);
		ensure(bSuccess);
		ensure(Device->GetFenceManager().IsFenceSignaled(CmdBuffer->Fence));
		CmdBuffer->GetOwner()->RefreshFenceStatus();
	}

	UpdateLastSubmittedCommandBuffer(CmdBuffer);

	CmdBuffer->GetOwner()->RefreshFenceStatus(CmdBuffer);

	Device->GetStagingManager().ProcessPendingFree(false, false);
}

void FVulkanQueue::UpdateLastSubmittedCommandBuffer(FVulkanCmdBuffer* CmdBuffer)
{
	FScopeLock ScopeLock(&CS);
	LastSubmittedCmdBuffer = CmdBuffer;
	LastSubmittedCmdBufferFenceCounter = CmdBuffer->GetFenceSignaledCounterH();
	++SubmitCounter;
}
