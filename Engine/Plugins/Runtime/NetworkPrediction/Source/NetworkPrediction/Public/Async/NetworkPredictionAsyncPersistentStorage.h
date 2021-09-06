// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace UE_NP {


// Something like this is needed and should be managed by the async physics system for persistent PT storage
// Note that the way this is used now, is TPersistentStorage<TSortedMap<ObjKey, ObjState>> - e.g, the whole snapshot
// We could maybe provide a way to do sparse allocated per object storage.
template<typename T>
struct TPersistentStorage
{
	int32 GetTailFrame() const { return FMath::Max(0, HeadFrame - Buffer.Num() + 1); }
	int32 GetHeadFrame() const { return HeadFrame; }

	// Const function that returns the frame that is safe to write to.
	// (Something like) this is the only thing ISimCallbackObject should really be using
	T* GetWritable() const { return const_cast<T*>(&Buffer[HeadFrame % Buffer.Num()]); }

	T* GetFrame(int32 Frame)
	{
		if (Frame <= HeadFrame && Frame >= GetTailFrame())
		{
			return &Buffer[Frame % Buffer.Num()];
		}

		return nullptr;
	}

	void SetHeadFrame(int32 NewHeadFrame)
	{
		HeadFrame = NewHeadFrame;
	}

	void IncrementFrame()
	{
		T* Pre = GetWritable();
		HeadFrame++;
		T* Next = GetWritable();
		*Next = *Pre;
	}

	void RollbackToFrame(int32 Frame)
	{
		npEnsure(Frame >= GetTailFrame());
		HeadFrame = Frame;
	}

private:

	int32 HeadFrame = 0;
	TStaticArray<T, 64> Buffer;
};

template<typename T>
struct TFrameStorage
{
	T&       operator[](int32 Index) { return Buffer[Index % Buffer.Num()]; }
	const T& operator[](int32 Index) const { return Buffer[Index % Buffer.Num()]; }

	int32 Num() const { return Buffer.Num(); }

private:
	
	TStaticArray<T, UE_NP::NumFramesStorage> Buffer;
};


} // namespace UE_NP