// Copyright 1998-2019 Epic Game s, Inc. All Rights Reserved.

#pragma once

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	TReplicationBuffer
//
//	Generic Cyclic buffer. Has canonical head position. This is the "Client Frame" or "Keyframe" identifier used by the rest of the system.
//	Contract: Elements in buffer are contiguously valid. We don't allow "gaps" in the buffer!
//	Use ::GetWriteNext<T> to write to the buffer.
//	Use ::CreateIterator to iterate from Tail->Head
//
//	for (auto It = Buffer.CreateIterator(); It; ++It) {
//		FMyType* Element = It.Element();
//		int32 Keyframe = It.Keyframe();
//	}
//
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>
struct TReplicationBuffer
{
	// Set the buffer size. Resizing is supported but not optimized because we preserve head/tail iteration (not a simple memcopy since where buffer wraps around will change)
	// In general, avoid resizing outside of startup/initialization.
	void SetBufferSize(int32 NewMaxNumElements)
	{
		check(NewMaxNumElements >= 0);	
		if (Data.Num() == NewMaxNumElements)
		{
			return;
		}
		
		if (Data.Num() == 0)
		{
			Data.SetNum(NewMaxNumElements, false);
		}
		else
		{
			// Grow or shrink. This is far from optimal but this operation should be rare
			TReplicationBuffer<T> Old(MoveTemp(*this)); // Move old data to a copy
			Data.SetNum(NewMaxNumElements, false);
			ResetNextHeadKeyframe(Old.GetTailKeyframe(), true); // Reset so our next write is at the old copies tail keyframe

			for (auto It = Old.CreateIterator(); It; ++It)
			{
				T* WriteNext = GetWriteNext();
				T* ReadNext = It.Element();
				FMemory::Memcpy(WriteNext, ReadNext, sizeof(T));
			}

			// Blow away the temp copy so we don't run the dtor on it on scope end (because we memcpy'd it over, not copy assigned)
			Old.Data.Reset();
			Old.NumValidElements = 0;
		}
	}

	// Gets the oldest element in the buffer (or null)
	T* GetElementFromTail(int32 OffsetFromTail)
	{
		check(OffsetFromTail >= 0); // Must always pass in valid offset

		// Out of range. This is not fatal, just don't return the wrong element.
		if (NumValidElements <= OffsetFromTail)
		{
			return nullptr;
		}
		
		const int32 Tail = GetTailKeyframe();
		const int32 Position = Tail + OffsetFromTail;
		const int32 Offset = (Position % Data.Num());
		return &Data[Offset];
	}

	// Gets the newest element in the buffer (or null)
	T* GetElementFromHead(int32 OffsetFromHead)
	{
		check(OffsetFromHead >= 0); // Must always pass in valid offset

		// Out of range. This is not fatal, just don't return the wrong element.
		if (NumValidElements <= OffsetFromHead)
		{
			return nullptr;
		}
		
		const int32 Position = Head - OffsetFromHead;
		check(Position >= 0);
		const int32 Offset = (Position % Data.Num());
		return &Data[Offset];
	}

	const T* FindElementByKeyframe(int32 Key) const
	{
		return FindElementByKeyframeImpl(Key);
	}

	T* FindElementByKeyframe(int32 Key)
	{
		const T* Element = const_cast<TReplicationBuffer<T>*>(this)->FindElementByKeyframeImpl(Key);
		return const_cast<T*>(Element);
	}

	// Returns the next element for writing. The contents of this element are unknown (could be stale content!). This also advances the head. Note the element returned is immediately considered "valid"!
	T* GetWriteNext()
	{
		check(Data.Num() > 0); // Buffer must be initialized first
		
		++Head;
		++DirtyCount;
		NumValidElements = FMath::Min<int32>(NumValidElements+1, Data.Num());

		const int32 Offset = (Head % Data.Num());
		return &Data[Offset];
	}

	// Resets the head keyframen, so that NextHeadKeyframe will be the keyframe of the next GetWriteNext<>(). E.g, call this before you want to write to NextHeadKeyframe.
	// To remphasize: NextHeadKeyframe is where the NEXT write will go. NOT "where the current head is".
	// Contents will be preserved if possible. E.g, if NextHeadKeyframe-1 is a valid keyframe, it will remain valid. If its not (if this creates "gaps" in the buffer) then
	// the contents will effectively be cleared. bForceClearContents will force this behavior even if the previous keyframe was valid.
	void ResetNextHeadKeyframe(int32 NextHeadKeyframe=0, bool bForceClearContents=false)
	{
		const int32 NewHeadKeyframe = NextHeadKeyframe - 1;
		if (bForceClearContents || NewHeadKeyframe < GetTailKeyframe() || NewHeadKeyframe > GetHeadKeyframe())
		{
			NumValidElements = 0;
		}
		else
		{
			NumValidElements += (NewHeadKeyframe - GetHeadKeyframe());
			check(NumValidElements >= 0 && NumValidElements <= Data.Num());
		}

		Head =  NewHeadKeyframe;
		DirtyCount++;
	}

	// Copies the contents of the SourceBuffer into this buffer. This existing buffer's data will be preserved as much as possible, but the guarantee is that the SourceBuffer's
	// data will all be added to this buffer.
	
	// Example 1: Target={1...5}, Source={3...9}  --> Target={1...9}
	// Example 2: Target={1...5}, Source={7...9}  --> Target={7...9}
	// Exmaple 3: Target={6...9}, Source={1...4}  --> Target={1...4}
	void CopyAndMerge(const TReplicationBuffer<T>& SourceBuffer)
	{
		const int32 StartingHeadKeyframe = GetHeadKeyframe();

		ResetNextHeadKeyframe(SourceBuffer.GetTailKeyframe());
		for (int32 Keyframe = SourceBuffer.GetTailKeyframe(); Keyframe <= SourceBuffer.GetHeadKeyframe(); ++Keyframe)
		{
			T* TargetData = GetWriteNext();
			check(GetHeadKeyframe() == Keyframe);

			const T* SourceData = SourceBuffer.FindElementByKeyframe(Keyframe);
			check(SourceData != nullptr);

			*TargetData = *SourceData;
		}
	}

	FString GetBasicDebugStr() const
	{
		return FString::Printf(TEXT("Elements: [%d/%d]. Keyframes: [%d-%d]"), NumValidElements, Data.Num(), GetTailKeyframe(), GetHeadKeyframe());
	}

	int32 GetNumValidElements() const { return NumValidElements; }
	int32 GetMaxNumElements() const { return Data.Num(); }
	int32 GetHeadKeyframe() const { return Head; }
	int32 GetTailKeyframe() const { return Head - NumValidElements + 1; }
	bool IsValidKeyframe(int32 Keyframe) const { return Keyframe >= GetTailKeyframe() && Keyframe <= GetHeadKeyframe(); }
	int32 GetDirtyCount() const { return DirtyCount; }

	// Create an iterator from tail->head. Note that no matter what template class is, this WILL iterate correctly across the element. The templated type
	// is just for casting the return element type. E.g, it is fine to use <uint8> in generic code, this will still step by 'StructSize'.
	struct TGenericIterator
	{
		TGenericIterator(TReplicationBuffer<T>& InBuffer) : Buffer(InBuffer)
		{
			CurrentKeyframe = Buffer.GetTailKeyframe();
		}

		int32 Keyframe() const { return CurrentKeyframe; }
		T* Element() const { return Buffer.FindElementByKeyframe(CurrentKeyframe); }
		void operator++() { CurrentKeyframe++; }
		operator bool()	{ return CurrentKeyframe <= Buffer.GetHeadKeyframe(); }

	private:
		TReplicationBuffer<T>& Buffer;
		int32 CurrentKeyframe = -1;
	};
		
	TGenericIterator CreateIterator() { return TGenericIterator(*this); }

private:

	const T* FindElementByKeyframeImpl(int32 Key) const
	{
		const int32 RelativeToHead = Key - Head;
		if (RelativeToHead > 0 || RelativeToHead <= -NumValidElements)
		{
			return nullptr;
		}

		return &Data[Key % Data.Num()];
	}

	int32 Head = INDEX_NONE;
	int32 DirtyCount = 0;
	int32 NumValidElements = 0;

	TArray<T> Data;
};