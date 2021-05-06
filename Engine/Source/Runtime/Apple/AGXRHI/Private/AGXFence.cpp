// Copyright Epic Games, Inc. All Rights Reserved.

#include "AGXRHIPrivate.h"

#include "AGXFence.h"
#include "AGXCommandBuffer.h"
#include "AGXCommandQueue.h"
#include "AGXContext.h"
#include "AGXProfiler.h"

@implementation FAGXDebugFence
@synthesize Inner;

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FAGXDebugFence)

-(instancetype)init
{
	id Self = [super init];
	if (Self)
	{
		Label = nil;
	}
	return Self;
}

-(void)dealloc
{
	[self validate];
	FAGXDebugCommandEncoder* Encoder = nil;
	while ((Encoder = UpdatingEncoders.Pop()))
	{
		[Encoder release];
	}
	Encoder = nil;
	while ((Encoder = WaitingEncoders.Pop()))
	{
		[Encoder release];
	}
	[Label release];
	[super dealloc];
}

-(id <MTLDevice>) device
{
	if (Inner)
	{
		return Inner.device;
	}
	else
	{
		return nil;
	}
}

-(NSString *_Nullable)label
{
	return Label;
}

-(void)setLabel:(NSString *_Nullable)Text
{
	[Text retain];
	[Label release];
	Label = Text;
	if(Inner)
	{
		Inner.label = Text;
	}
}

-(void)validate
{
	UE_CLOG(UpdatingEncoders.IsEmpty() != WaitingEncoders.IsEmpty(), LogAGX, Fatal, TEXT("Fence with unmatched updates/waits destructed - there's a gap in fence (%p) %s"), self, Label ? *FString(Label) : TEXT("Null"));
}

-(void)updatingEncoder:(FAGXDebugCommandEncoder*)Encoder
{
	check(Encoder);
	UpdatingEncoders.Push([Encoder retain]);
}

-(void)waitingEncoder:(FAGXDebugCommandEncoder*)Encoder
{
	check(Encoder);
	WaitingEncoders.Push([Encoder retain]);
}

-(TLockFreePointerListLIFO<FAGXDebugCommandEncoder>*)updatingEncoders
{
	return &UpdatingEncoders;
}

-(TLockFreePointerListLIFO<FAGXDebugCommandEncoder>*)waitingEncoders
{
	return &WaitingEncoders;
}
@end

#if METAL_DEBUG_OPTIONS
extern int32 GAGXRuntimeDebugLevel;
#endif

uint32 FAGXFence::Release() const
{
	uint32 Refs = uint32(FPlatformAtomics::InterlockedDecrement(&NumRefs));
	if(Refs == 0)
	{
#if METAL_DEBUG_OPTIONS // When using validation we need to use fences only once per-frame in order to make it tractable
		if (GAGXRuntimeDebugLevel >= EAGXDebugLevelValidation)
		{
			AGXSafeReleaseMetalFence(const_cast<FAGXFence*>(this));
		}
		else // However in a final game, we need to reuse fences aggressively so that we don't run out when loading into projects
#endif
		{
			FAGXFencePool::Get().ReleaseFence(const_cast<FAGXFence*>(this));
		}
	}
	return Refs;
}

#if METAL_DEBUG_OPTIONS
void FAGXFence::Validate(void) const
{
	if (GetAGXDeviceContext().GetCommandQueue().GetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation && Get(mtlpp::RenderStages::Vertex))
	{
		[(FAGXDebugFence*)Get(mtlpp::RenderStages::Vertex).GetPtr() validate];
		if (Get(mtlpp::RenderStages::Fragment))
		{
			[(FAGXDebugFence*)Get(mtlpp::RenderStages::Fragment).GetPtr() validate];
		}
	}
}
#endif

void FAGXFencePool::Initialise(mtlpp::Device const& InDevice)
{
	Device = InDevice;
	for (int32 i = 0; i < FAGXFencePool::NumFences; i++)
	{
#if METAL_DEBUG_OPTIONS
		if (GAGXRuntimeDebugLevel >= EAGXDebugLevelValidation)
		{
			FAGXDebugFence* VertexFence = [[FAGXDebugFence new] autorelease];
			VertexFence.Inner = Device.NewFence();
			FAGXDebugFence* FragmentFence = [[FAGXDebugFence new] autorelease];
			FragmentFence.Inner = Device.NewFence();
			FAGXFence* F = new FAGXFence;
			F->Set(mtlpp::RenderStages::Vertex, VertexFence);
			F->Set(mtlpp::RenderStages::Fragment, FragmentFence);
			Fences.Add(F);
			Lifo.Push(F);
		}
		else
#endif
		{
			FAGXFence* F = new FAGXFence;
			F->Set(mtlpp::RenderStages::Vertex, Device.NewFence());
			F->Set(mtlpp::RenderStages::Fragment, Device.NewFence());
#if METAL_DEBUG_OPTIONS
			if (GAGXRuntimeDebugLevel >= EAGXDebugLevelValidation)
			{
				Fences.Add(F);
			}
#endif
			Lifo.Push(F);
		}
	}
	Count = FAGXFencePool::NumFences;
    Allocated = 0;
}

FAGXFence* FAGXFencePool::AllocateFence()
{
	FAGXFence* Fence = Lifo.Pop();
	if (Fence)
	{
        INC_DWORD_STAT(STAT_AGXFenceCount);
        FPlatformAtomics::InterlockedDecrement(&Count);
        FPlatformAtomics::InterlockedIncrement(&Allocated);
#if METAL_DEBUG_OPTIONS
		if (GAGXRuntimeDebugLevel >= EAGXDebugLevelValidation)
		{
			FScopeLock Lock(&Mutex);
			check(Fences.Contains(Fence));
			Fences.Remove(Fence);
		}
#endif
	}
	check(Fence);
	Fence->Reset();
	return Fence;
}

void FAGXFence::ValidateUsage(FAGXFence* InFence)
{
	if (InFence)
	{
		if (InFence->NumWrites(mtlpp::RenderStages::Vertex) != InFence->NumWaits(mtlpp::RenderStages::Vertex))
		{
			UE_LOG(LogAGX, Warning, TEXT("%p (%s) writes %d waits %d"), InFence, *FString(InFence->Get(mtlpp::RenderStages::Vertex).GetLabel()), (uint32)InFence->NumWrites(mtlpp::RenderStages::Vertex), (uint32)InFence->NumWaits(mtlpp::RenderStages::Vertex));
		}
		if (InFence->NumWrites(mtlpp::RenderStages::Fragment) != InFence->NumWaits(mtlpp::RenderStages::Fragment))
		{
			UE_LOG(LogAGX, Warning, TEXT("%p (%s) writes %d waits %d"), InFence, *FString(InFence->Get(mtlpp::RenderStages::Fragment).GetLabel()), (uint32)InFence->NumWrites(mtlpp::RenderStages::Fragment), (uint32)InFence->NumWaits(mtlpp::RenderStages::Fragment));
		}
	}
}

void FAGXFencePool::ReleaseFence(FAGXFence* const InFence)
{
	if (InFence)
	{
        DEC_DWORD_STAT(STAT_AGXFenceCount);
        FPlatformAtomics::InterlockedDecrement(&Allocated);
#if METAL_DEBUG_OPTIONS
		if (GAGXRuntimeDebugLevel >= EAGXDebugLevelValidation)
		{
			FScopeLock Lock(&Mutex);
			FAGXFence::ValidateUsage(InFence);
			check(!Fences.Contains(InFence));
			Fences.Add(InFence);
		}
#endif
		FPlatformAtomics::InterlockedIncrement(&Count);
		check(Count <= FAGXFencePool::NumFences);
		Lifo.Push(InFence);
	}
}
