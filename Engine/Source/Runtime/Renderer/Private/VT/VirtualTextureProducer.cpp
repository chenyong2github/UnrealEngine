// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VirtualTextureProducer.h"
#include "VirtualTextureSystem.h"
#include "VirtualTexturePhysicalSpace.h"

void FVirtualTextureProducer::Release(FVirtualTextureSystem* System, const FVirtualTextureProducerHandle& HandleToSelf)
{
	if (Description.bPersistentHighestMip)
	{
		System->ForceUnlockAllTiles(HandleToSelf, this);
	}

	for (uint32 LayerIndex = 0u; LayerIndex < Description.NumLayers; ++LayerIndex)
	{
		FVirtualTexturePhysicalSpace* Space = PhysicalSpace[LayerIndex];
		PhysicalSpace[LayerIndex] = nullptr;
		Space->GetPagePool().EvictPages(System, HandleToSelf);
		System->ReleasePhysicalSpace(Space);
	}

	delete VirtualTexture;
	VirtualTexture = nullptr;
	Description = FVTProducerDescription();
}

FVirtualTextureProducerCollection::FVirtualTextureProducerCollection()
{
	Producers.AddDefaulted(1);
	Producers[0].Magic = 1u; // make sure FVirtualTextureProducerHandle(0) will not resolve to the dummy producer entry

	Callbacks.AddDefaulted(CallbackList_Count);
	for (uint32 CallbackIndex = 0u; CallbackIndex < CallbackList_Count; ++CallbackIndex)
	{
		FCallbackEntry& Callback = Callbacks[CallbackIndex];
		Callback.NextIndex = Callback.PrevIndex = CallbackIndex;
	}
}

FVirtualTextureProducerHandle FVirtualTextureProducerCollection::RegisterProducer(FVirtualTextureSystem* System, const FVTProducerDescription& InDesc, IVirtualTexture* InProducer)
{
	check(IsInRenderingThread());
	const uint32 ProducerWidth = InDesc.BlockWidthInTiles * InDesc.WidthInBlocks * InDesc.TileSize;
	const uint32 ProducerHeight = InDesc.BlockHeightInTiles * InDesc.HeightInBlocks * InDesc.TileSize;
	check(ProducerWidth > 0u);
	check(ProducerHeight > 0u);
	check(InDesc.MaxLevel <= FMath::CeilLogTwo(FMath::Max(ProducerWidth, ProducerHeight)));
	check(InProducer);

	const uint32 Index = AcquireEntry();
	FProducerEntry& Entry = Producers[Index];
	Entry.Producer.Description = InDesc;
	Entry.Producer.VirtualTexture = InProducer;
	Entry.DestroyedCallbacksIndex = AcquireCallback();

	check(InDesc.NumLayers <= VIRTUALTEXTURE_SPACE_MAXLAYERS);
	for (uint32 LayerIndex = 0u; LayerIndex < InDesc.NumLayers; ++LayerIndex)
	{
		FVTPhysicalSpaceDescription PhysicalSpaceDesc;
		PhysicalSpaceDesc.Dimensions = InDesc.Dimensions;
		PhysicalSpaceDesc.TileSize = InDesc.TileSize + InDesc.TileBorderSize * 2u;
		PhysicalSpaceDesc.Format = InDesc.LayerFormat[LayerIndex];
		PhysicalSpaceDesc.bContinuousUpdate = InDesc.bContinuousUpdate;
		PhysicalSpaceDesc.bCreateRenderTarget = InDesc.bCreateRenderTarget;
		Entry.Producer.PhysicalSpace[LayerIndex] = System->AcquirePhysicalSpace(PhysicalSpaceDesc);
	}

	const FVirtualTextureProducerHandle Handle(Index, Entry.Magic);
	return Handle;
}

void FVirtualTextureProducerCollection::ReleaseProducer(FVirtualTextureSystem* System, const FVirtualTextureProducerHandle& Handle)
{
	check(IsInRenderingThread());

	if (FProducerEntry* Entry = GetEntry(Handle))
	{
		uint32 CallbackIndex = Callbacks[Entry->DestroyedCallbacksIndex].NextIndex;
		while (CallbackIndex != Entry->DestroyedCallbacksIndex)
		{
			FCallbackEntry& Callback = Callbacks[CallbackIndex];
			const uint32 NextIndex = Callback.NextIndex;
			check(Callback.OwnerHandle == Handle);

			// Move callback to pending list
			RemoveCallbackFromList(CallbackIndex);
			AddCallbackToList(CallbackList_Pending, CallbackIndex);
			
			CallbackIndex = NextIndex;
		}

		ReleaseCallback(Entry->DestroyedCallbacksIndex);
		Entry->DestroyedCallbacksIndex = 0u;

		Entry->Magic = (Entry->Magic + 1u) & 1023u;
		Entry->Producer.Release(System, Handle);
		ReleaseEntry(Handle.Index);
	}
}

void FVirtualTextureProducerCollection::CallPendingCallbacks()
{
	uint32 CallbackIndex = Callbacks[CallbackList_Pending].NextIndex;
	while (CallbackIndex != CallbackList_Pending)
	{
		FCallbackEntry& Callback = Callbacks[CallbackIndex];
		check(Callback.DestroyedFunction);
		check(Callback.OwnerHandle.PackedValue != 0u);

		// Make a copy, then release the callback entry before calling the callback function
		// (The destroyed callback may try to remove this or other callbacks, so need to make sure state is valid before calling)
		const FCallbackEntry CallbackCopy(Callback);
		Callback.DestroyedFunction = nullptr;
		Callback.Baton = nullptr;
		Callback.OwnerHandle = FVirtualTextureProducerHandle();
		ReleaseCallback(CallbackIndex);

		CallbackCopy.DestroyedFunction(CallbackCopy.OwnerHandle, CallbackCopy.Baton);
		CallbackIndex = CallbackCopy.NextIndex;
	}
}

void FVirtualTextureProducerCollection::AddDestroyedCallback(const FVirtualTextureProducerHandle& Handle, FVTProducerDestroyedFunction* Function, void* Baton)
{
	check(IsInRenderingThread());
	check(Function);

	FProducerEntry* Entry = GetEntry(Handle);
	if (Entry)
	{
		const uint32 CallbackIndex = AcquireCallback();
		AddCallbackToList(Entry->DestroyedCallbacksIndex, CallbackIndex);
		FCallbackEntry& Callback = Callbacks[CallbackIndex];
		Callback.DestroyedFunction = Function;
		Callback.Baton = Baton;
		Callback.OwnerHandle = Handle;
	}
}

uint32 FVirtualTextureProducerCollection::RemoveAllCallbacks(const void* Baton)
{
	check(IsInRenderingThread());
	check(Baton);

	uint32 NumRemoved = 0u;
	for (int32 CallbackIndex = CallbackList_Count; CallbackIndex < Callbacks.Num(); ++CallbackIndex)
	{
		FCallbackEntry& Callback = Callbacks[CallbackIndex];
		if (Callback.Baton == Baton)
		{
			check(Callback.DestroyedFunction);
			Callback.DestroyedFunction = nullptr;
			Callback.Baton = nullptr;
			Callback.OwnerHandle = FVirtualTextureProducerHandle();
			ReleaseCallback(CallbackIndex);
			++NumRemoved;
		}
	}
	return NumRemoved;
}

FVirtualTextureProducer* FVirtualTextureProducerCollection::FindProducer(const FVirtualTextureProducerHandle& Handle)
{
	FProducerEntry* Entry = GetEntry(Handle);
	return Entry ? &Entry->Producer : nullptr;
}

FVirtualTextureProducer& FVirtualTextureProducerCollection::GetProducer(const FVirtualTextureProducerHandle& Handle)
{
	const uint32 Index = Handle.Index;
	check(Index < (uint32)Producers.Num());
	FProducerEntry& Entry = Producers[Index];
	check(Entry.Magic == Handle.Magic);
	return Entry.Producer;
}

FVirtualTextureProducerCollection::FProducerEntry* FVirtualTextureProducerCollection::GetEntry(const FVirtualTextureProducerHandle& Handle)
{
	const uint32 Index = Handle.Index;
	if (Index < (uint32)Producers.Num())
	{
		FProducerEntry& Entry = Producers[Index];
		if (Entry.Magic == Handle.Magic)
		{
			return &Entry;
		}
	}
	return nullptr;
}
