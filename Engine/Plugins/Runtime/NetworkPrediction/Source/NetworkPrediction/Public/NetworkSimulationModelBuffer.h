// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

template<typename TBuffer, typename ElementType>
struct TNetworkSimBufferIterator
{
	TNetworkSimBufferIterator(TBuffer& InBuffer) : Buffer(InBuffer)	{ CurrentPos = Buffer.IterStartPos(); }
	int32 Keyframe() const { return Buffer.IterKeyframe(CurrentPos); }
	ElementType* Element() const { return Buffer.IterElement(CurrentPos); }

	void operator++() { CurrentPos++; }
	operator bool()	{ return CurrentPos != INDEX_NONE && CurrentPos <= Buffer.IterEndPos(); }

private:
	TBuffer& Buffer;
	int32 CurrentPos = -1;
};

template<typename T>
struct TNetworkSimBufferBase
{
	FString GetBasicDebugStr() const
	{
		return FString::Printf(TEXT("Elements: [%d/%d]. Keyframes: [%d-%d]"), ((T*)this)->Num(), ((T*)this)->Max(), ((T*)this)->TailKeyframe(), ((T*)this)->HeadKeyframe());
	}

	bool IsValidKeyframe(int32 Keyframe) const 
	{ 
		return Keyframe >= ((T*)this)->TailKeyframe() && Keyframe <= ((T*)this)->HeadKeyframe(); 
	}

	// Copies each element of SourceBuffer into this buffer. This could break continuity so for TNetworkSimContiguousBuffer, it is possible to lose elements by doing this.
	template<typename TSource>
	void CopyAndMerge(const TSource& SourceBuffer)
	{
		for (auto It = SourceBuffer.CreateConstIterator(); It; ++It)
		{
			*((T*)this)->WriteKeyframe( It.Keyframe() ) = *It.Element();
		}
	}
protected:

	// Creates a new keyframe, but
	//	-If keyframe already exists, returns existing
	//	-If keyframe > head, contents of head are copied into new frame
	//	-If keyframe < head, contents are cleared to default value
	template<typename ElementType>
	ElementType* WriteKeyframeInitializedFromHeadImpl(int32 Keyframe)
	{
		if (((T*)this)->HeadKeyframe() > Keyframe)
		{
			ElementType* HeadElementPtr = ((T*)this)->HeadElement();
			ElementType* NewElement = ((T*)this)->WriteKeyframe(Keyframe);
			*NewElement = *HeadElementPtr;
			return NewElement;
		}
		else if (Keyframe < ((T*)this)->TailKeyframe())
		{
			ElementType* NewElement = ((T*)this)->WriteKeyframe(Keyframe);
			*NewElement = ElementType();
			return NewElement;
		}
		
		return ((T*)this)->WriteKeyframe(Keyframe);
	}
};

template<typename T, int32 NumElements=32>
struct TNetworkSimContiguousBuffer : public TNetworkSimBufferBase<TNetworkSimContiguousBuffer<T, NumElements>>
{
	using ElementType = T;
	TNetworkSimContiguousBuffer()
	{
		Data.SetNum(NumElements);
	}

	T* operator[](int32 Keyframe) {	return const_cast<T*>(GetImpl(Keyframe)); }
	const T* operator[](int32 Keyframe) const { return GetImpl(Keyframe); }

	T* Get(int32 Keyframe) { return const_cast<T*>(GetImpl(Keyframe)); }
	const T* Get(int32 Keyframe) const { return GetImpl(Keyframe); }

	const T* HeadElement() const { return HeadElementImpl(); }
	T* HeadElement() { return const_cast<T*>(HeadElementImpl()); }

	const T* TailElement() const { return TailElementImpl(); }
	T* TailElement() { return const_cast<T*>(TailElementImpl()); }

	int32 HeadKeyframe() const { return Head; }
	int32 TailKeyframe() const { return GetTail(); }

	int32 Num() const { return Head == INDEX_NONE ? 0 : (Head - GetTail() + 1); }
	int32 Max() const { return Data.Max(); }
	int32 GetDirtyCount() const { return DirtyCount; }

	T* WriteKeyframe(int32 Keyframe)
	{
		check(Keyframe >= 0);

		const int32 Tail = GetTail();
		if (Head == INDEX_NONE || Keyframe < Tail || Keyframe > Head+1)
		{
			// Writing outside the current range (+1) results in a full wipe of valid contents
			NumValidElements = 1;
		}
		else
		{
			// Writing inside the current range (+1) preserves valid elements up to the size of our buffer
			NumValidElements = FMath::Min<int32>(Keyframe - Tail + 1, Data.Num());
		}
		
		Head = Keyframe;
		++DirtyCount;
		return &Data[Keyframe % Data.Num()];
	}
	
	T* WriteKeyframeInitializedFromHead(int32 Keyframe)
	{
		return this->template WriteKeyframeInitializedFromHeadImpl<ElementType>(Keyframe);
	}

	TUniqueFunction<T*()> WriteKeyframeFunc(int32 Keyframe)
	{
		return [this, Keyframe]() { return WriteKeyframe(Keyframe); };
	}

	using IteratorType = TNetworkSimBufferIterator<TNetworkSimContiguousBuffer<ElementType, NumElements>, ElementType>;
	using ConstIteratorType = TNetworkSimBufferIterator<const TNetworkSimContiguousBuffer<ElementType, NumElements>, const ElementType>;

	IteratorType CreateIterator() { return IteratorType(*this); }
	ConstIteratorType CreateConstIterator() const { return ConstIteratorType(*this); }

private:

	const T* GetImpl(int32 Keyframe) const
	{
		const int32 RelativeToHead = Keyframe - Head;
		if (RelativeToHead > 0 || RelativeToHead <= -NumValidElements)
		{
			return nullptr;
		}

		return &Data[Keyframe % Data.Num()];
	}

	const T* HeadElementImpl() const
	{
		if (Head != INDEX_NONE)
		{
			return &Data[Head % Data.Num()];
		}
		return nullptr;
	}

	const T* TailElementImpl() const
	{
		const int32 Tail = GetTail();
		if (Tail != INDEX_NONE)
		{
			return &Data[Tail % Data.Num()];
		}
		return nullptr;
	}

	int32 GetTail() const { return Head == INDEX_NONE ? INDEX_NONE : (Head - NumValidElements + 1); };

	int32 DirtyCount = 0;
	int32 Head = INDEX_NONE;
	int32 NumValidElements = 0;
	TArray<ElementType, TInlineAllocator<NumElements>> Data;

	// Efficient position based accessing through TNetworkSimBufferIterator.
	template<typename, typename>
	friend struct TNetworkSimBufferIterator;
	int32 IterStartPos() const { return GetTail(); }
	int32 IterEndPos() const { return Head; }
	int32 IterKeyframe(int32 Pos) const { return Pos; }
	T* IterElement(int32 Pos) { return &Data[Pos % Data.Num()]; }
	const T* IterElement(int32 Pos) const { return &Data[Pos % Data.Num()]; }
};

// Keyframe: arbitrary identifier for data. Not contiguous or controlled by us (always passed in)
// Position/Pos: increasing counter for position in array. Mod with Data.Num to get Index.
// Index/Idx: actual index into Data Array
template<typename T, int32 NumElements=32>
struct TNetworkSimSparseBuffer : public TNetworkSimBufferBase<TNetworkSimSparseBuffer<T, NumElements>>
{
	using ElementType = T;
	TNetworkSimSparseBuffer()
	{
		Data.SetNum(NumElements);
	}

	T* operator[](int32 Keyframe) {	return const_cast<T*>(GetImpl(Keyframe)); }
	const T* operator[](int32 Keyframe) const { return GetImpl(Keyframe); }

	T* Get(int32 Keyframe) { return const_cast<T*>(GetImpl(Keyframe)); }
	const T* Get(int32 Keyframe) const { return GetImpl(Keyframe); }

	const T* HeadElement() const { return HeadElementImpl(); }
	T* HeadElement() { return const_cast<T*>(HeadElementImpl()); }

	const T* TailElement() const { return TailElementImpl(); }
	T* TailElement() { return const_cast<T*>(TailElementImpl()); }

	int32 HeadKeyframe() const { return Data[HeadPos % Data.Num()].Keyframe; }
	int32 TailKeyframe() const { return Data[GetTailPos() % Data.Num()].Keyframe; }

	int32 Num() const { return HeadPos == INDEX_NONE ? 0 : (HeadPos - GetTailPos() + 1); }
	int32 Max() const { return Data.Max(); }
	int32 GetDirtyCount() const { return DirtyCount; }

	// Returns the element @ keyframe for writing. The contents of this element are unknown (could be stale content!). Note the element returned is immediately considered "valid" by Num(), Iterators, etc!
	T* WriteKeyframe(int32 Keyframe)
	{
		check(Keyframe >= 0);
		++DirtyCount;

		int32 Pos = HeadPos;
		if (HeadPos != INDEX_NONE)
		{
			// If we have elements, find where this Keyframe would go
			const int32 Tail = GetTailPos();
			for (; Pos >= Tail; --Pos)
			{
				TInternal& InternalData = Data[Pos % Data.Num()];
				if (InternalData.Keyframe == Keyframe)
				{
					return &InternalData.Element;
				}
				if (InternalData.Keyframe <= Keyframe)
				{
					break;
				}
			}
		}

		// Write frame to next pos
		HeadPos = Pos+1;
		TInternal& WriteData = Data[HeadPos % Data.Num()];
		WriteData.Keyframe = Keyframe;
		return &WriteData.Element;
	}

	ElementType* WriteKeyframeInitializedFromHead(int32 Keyframe)
	{
		return this->template WriteKeyframeInitializedFromHeadImpl<ElementType>(Keyframe);
	}

	TUniqueFunction<T*()> WriteKeyframeFunc(int32 Keyframe)
	{
		return [this, Keyframe]() { return WriteKeyframe(Keyframe); };
	}

	using IteratorType = TNetworkSimBufferIterator<TNetworkSimSparseBuffer<ElementType, NumElements>, ElementType>;
	using ConstIteratorType = TNetworkSimBufferIterator<const TNetworkSimSparseBuffer<ElementType, NumElements>, const ElementType>;

	IteratorType CreateIterator() { return IteratorType(*this); }
	ConstIteratorType CreateConstIterator() const { return ConstIteratorType(*this); }

private:

	struct TInternal
	{
		int32 Keyframe = -1;
		T Element;
	};

	const T* GetImpl(int32 Keyframe) const
	{
		const int32 Pos = GetPosForKeyframe(Keyframe);
		if (Pos != INDEX_NONE)
		{		
			return &Data[Pos % Data.Num()].Element;
		}

		return nullptr;
	}

	const T* HeadElementImpl() const
	{
		if (HeadPos != INDEX_NONE)
		{
			return &Data[HeadPos % Data.Num()].Element;
		}
		return nullptr;
	}

	const T* TailElementImpl() const
	{
		const int32 TailPos = GetTailPos();
		if (TailPos != INDEX_NONE)
		{
			return &Data[TailPos % Data.Num()].Element;
		}
		return nullptr;
	}
	
	int32 GetPosForKeyframe(int32 Keyframe) const
	{
		const int32 TailPos = GetTailPos();
		for (int32 Pos=HeadPos; Pos >= TailPos; --Pos)
		{
			if (Data[Pos % Data.Num()].Keyframe <= Keyframe)
			{
				return Pos;
			}
		}
		return INDEX_NONE;
	}

	int32 GetTailPos() const { return HeadPos == INDEX_NONE ? INDEX_NONE : FMath::Max<int32>(0, HeadPos - Data.Num()); }

	int32 DirtyCount=0;
	int32 HeadPos=INDEX_NONE;
	TArray<TInternal, TInlineAllocator<NumElements>> Data;

	// Efficient position based accessing through TNetworkSimBufferIterator.
	template<typename, typename>
	friend struct TNetworkSimBufferIterator;
	int32 IterStartPos() const { return GetTailPos(); }
	int32 IterEndPos() const { return HeadPos; }
	int32 IterKeyframe(int32 Pos) const { return Data[Pos % Data.Num()].Keyframe; }
	T* IterElement(int32 Pos) { return &Data[Pos % Data.Num()].Element; }
	const T* IterElement(int32 Pos) const { return &Data[Pos % Data.Num()].Element; }
};