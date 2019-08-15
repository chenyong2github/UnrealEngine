// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VirtualTextureShared.h"
#include "RendererInterface.h"
#include "VirtualTexturing.h"

class FVirtualTextureSystem;
class FVirtualTexturePhysicalSpace;

class FVirtualTextureProducer
{
public:
	FVirtualTextureProducer() : VirtualTexture(nullptr) {}

	void Release(FVirtualTextureSystem* System, const FVirtualTextureProducerHandle& HandleToSelf);

	inline const FVTProducerDescription& GetDescription() const { return Description; }
	inline IVirtualTexture* GetVirtualTexture() const { return VirtualTexture; }
	inline const FName& GetName() const { return Description.Name; }
	inline uint32 GetNumLayers() const { return Description.NumLayers; }
	inline uint32 GetWidthInTiles() const { return Description.BlockWidthInTiles * Description.WidthInBlocks; }
	inline uint32 GetHeightInTiles() const { return Description.BlockHeightInTiles * Description.HeightInBlocks; }
	inline uint32 GetDepthInTiles() const { return Description.DepthInTiles; }
	inline EPixelFormat GetLayerFormat(uint32 LayerIndex) const { check(LayerIndex < Description.NumLayers); return Description.LayerFormat[LayerIndex]; }
	inline FVirtualTexturePhysicalSpace* GetPhysicalSpace(uint32 LayerIndex) const { check(LayerIndex < Description.NumLayers); return PhysicalSpace[LayerIndex]; }
	inline uint32 GetMaxLevel() const { return Description.MaxLevel; }

private:
	friend class FVirtualTextureProducerCollection;
	~FVirtualTextureProducer() {}

	IVirtualTexture* VirtualTexture;
	TRefCountPtr<FVirtualTexturePhysicalSpace> PhysicalSpace[VIRTUALTEXTURE_SPACE_MAXLAYERS];
	FVTProducerDescription Description;
};

class FVirtualTextureProducerCollection
{
public:
	FVirtualTextureProducerCollection();
	FVirtualTextureProducerHandle RegisterProducer(FVirtualTextureSystem* System, const FVTProducerDescription& InDesc, IVirtualTexture* InProducer);
	void ReleaseProducer(FVirtualTextureSystem* System, const FVirtualTextureProducerHandle& Handle);
	
	void AddDestroyedCallback(const FVirtualTextureProducerHandle& Handle, FVTProducerDestroyedFunction* Function, void* Baton);
	uint32 RemoveAllCallbacks(const void* Baton);
	void CallPendingCallbacks();

	/**
	 * Gets the producer associated with the given handle, or nullptr if handle is invalid
	 * Returned pointer is only valid until the next call to RegisterProducer, so should not be stored beyond scope of a function
	 */
	FVirtualTextureProducer* FindProducer(const FVirtualTextureProducerHandle& Handle);

	/** Like FindProducer, but fails check/crashes if handle is not valid.  Returns a reference, since never needs to return nullptr */
	FVirtualTextureProducer& GetProducer(const FVirtualTextureProducerHandle& Handle);

private:
	struct FProducerEntry
	{
		FVirtualTextureProducer Producer;
		uint32 DestroyedCallbacksIndex = 0u;
		uint32 NextIndex = 0u;
		uint32 PrevIndex = 0u;
		uint16 Magic = 0u;
	};

	struct FCallbackEntry
	{
		FVTProducerDestroyedFunction* DestroyedFunction = nullptr;
		void* Baton = nullptr;
		FVirtualTextureProducerHandle OwnerHandle;
		uint32 NextIndex = 0u;
		uint32 PrevIndex = 0u;
		union
		{
			uint32 PackedFlags = 0u;
			struct
			{
				uint32 bPending : 1;
				uint32 Pad : 31;
			};
		};
	};

	enum ECallbackListType
	{
		CallbackList_Free,
		CallbackList_Pending,
		CallbackList_Count,
	};

	FProducerEntry* GetEntry(const FVirtualTextureProducerHandle& Handle);

	void RemoveEntryFromList(uint32 Index)
	{
		FProducerEntry& Entry = Producers[Index];
		Producers[Entry.PrevIndex].NextIndex = Entry.NextIndex;
		Producers[Entry.NextIndex].PrevIndex = Entry.PrevIndex;
		Entry.NextIndex = Entry.PrevIndex = Index;
	}

	void AddEntryToList(uint32 HeadIndex, uint32 Index)
	{
		FProducerEntry& Head = Producers[HeadIndex];
		FProducerEntry& Entry = Producers[Index];
		check(Index > 0u); // make sure we're not trying to add a list head to another list

		// make sure we're not currently in any list
		check(Entry.NextIndex == Index);
		check(Entry.PrevIndex == Index);

		Entry.NextIndex = HeadIndex;
		Entry.PrevIndex = Head.PrevIndex;
		Producers[Head.PrevIndex].NextIndex = Index;
		Head.PrevIndex = Index;
	}

	uint32 AcquireEntry()
	{
		FProducerEntry& FreeHead = Producers[0u];
		uint32 Index = FreeHead.NextIndex;
		if (Index != 0u)
		{
			RemoveEntryFromList(Index);
			return Index;
		}

		Index = Producers.AddDefaulted();
		FProducerEntry& Entry = Producers[Index];
		Entry.NextIndex = Entry.PrevIndex = Index;
		return Index;
	}

	void ReleaseEntry(uint32 Index)
	{
		RemoveEntryFromList(Index);
		AddEntryToList(0u, Index);
	}

	void RemoveCallbackFromList(uint32 Index)
	{
		FCallbackEntry& Entry = Callbacks[Index];
		Callbacks[Entry.PrevIndex].NextIndex = Entry.NextIndex;
		Callbacks[Entry.NextIndex].PrevIndex = Entry.PrevIndex;
		Entry.NextIndex = Entry.PrevIndex = Index;
	}

	void AddCallbackToList(uint32 HeadIndex, uint32 Index)
	{
		FCallbackEntry& Head = Callbacks[HeadIndex];
		FCallbackEntry& Entry = Callbacks[Index];
		check(Index >= CallbackList_Count); // make sure we're not trying to add a list head to another list

		// make sure we're not currently in any list
		check(Entry.NextIndex == Index);
		check(Entry.PrevIndex == Index);

		Entry.NextIndex = HeadIndex;
		Entry.PrevIndex = Head.PrevIndex;
		Callbacks[Head.PrevIndex].NextIndex = Index;
		Head.PrevIndex = Index;
	}

	uint32 AcquireCallback()
	{
		FCallbackEntry& FreeHead = Callbacks[CallbackList_Free];
		uint32 Index = FreeHead.NextIndex;
		if (Index != CallbackList_Free)
		{
			RemoveCallbackFromList(Index);
			return Index;
		}

		Index = Callbacks.AddDefaulted();
		FCallbackEntry& Entry = Callbacks[Index];
		Entry.NextIndex = Entry.PrevIndex = Index;
		return Index;
	}

	void ReleaseCallback(uint32 Index)
	{
		RemoveCallbackFromList(Index);
		AddCallbackToList(CallbackList_Free, Index);
	}

	TArray<FProducerEntry> Producers;
	TArray<FCallbackEntry> Callbacks;
	uint32 NumPendingCallbacks;
};
