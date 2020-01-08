// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Generic iterator for iterating over each frame from tail to head
template<typename TBuffer, typename ElementType>
struct TNetworkSimBufferIterator
{
	TNetworkSimBufferIterator(TBuffer& InBuffer) : Buffer(InBuffer)	{ CurrentPos = Buffer.IterStartPos(); }
	int32 Frame() const { return Buffer.IterFrame(CurrentPos); }
	ElementType* Element() const { return Buffer.IterElement(CurrentPos); }

	void operator++() { CurrentPos++; }
	operator bool()	{ return CurrentPos != INDEX_NONE && CurrentPos <= Buffer.IterEndPos(); }

private:
	TBuffer& Buffer;
	int32 CurrentPos = -1;
};

// Helper struct to encapsulate optional, delayed writing of new element to buffer
template<typename ElementType>
struct TNetSimLazyWriterFunc
{
	template<typename TBuffer>
	TNetSimLazyWriterFunc(TBuffer& Buffer, int32 PendingFrame)
	{
		GetFunc = [&Buffer, PendingFrame]() { return Buffer.WriteFrameInitializedFromHead(PendingFrame); };
	}

	ElementType* Get() const
	{
		check(GetFunc);
		return (ElementType*)GetFunc();
	}

	TFunction<void*()> GetFunc;
};

// Reference version of LazyWriter. This is what gets passed through chains of ::SimulationTick calls. This is to avoid copying the TFunction in TNetSimLazyWriterFunc around.
template<typename ElementType>
struct TNetSimLazyWriter
{
	template<typename TLazyWriter>
	TNetSimLazyWriter(const TLazyWriter& Parent)
		: GetFunc(Parent.GetFunc) { }

	ElementType* Get() const
	{
		return (ElementType*)GetFunc();
	}

	TFunctionRef<void*()> GetFunc;
};

// Common implementations shared by TNetworkSimContiguousBuffer/TNetworkSimSparseBuffer
template<typename T>
struct TNetworkSimBufferBase
{
	FString GetBasicDebugStr() const
	{
		return FString::Printf(TEXT("Elements: [%d/%d]. Frames: [%d-%d]"), ((T*)this)->Num(), ((T*)this)->Max(), ((T*)this)->TailFrame(), ((T*)this)->HeadFrame());
	}

	bool IsValidFrame(int32 Frame) const 
	{ 
		return Frame >= ((T*)this)->TailFrame() && Frame <= ((T*)this)->HeadFrame(); 
	}

	// Copies each element of SourceBuffer into this buffer. This could break continuity so for TNetworkSimContiguousBuffer, it is possible to lose elements by doing this.
	template<typename TSource>
	void CopyAndMerge(const TSource& SourceBuffer)
	{
		for (auto It = SourceBuffer.CreateConstIterator(); It; ++It)
		{
			*((T*)this)->WriteFrame( It.Frame() ) = *It.Element();
		}
	}

protected:

	// Creates a new frame, but
	//	-If frame already exists, returns existing
	//	-If frame > head, contents of head are copied into new frame
	//	-If frame < tail, contents are cleared to default value
	// (this function is sketchy and poorly named but is proving to be useful in a few places)
	template<typename ElementType>
	ElementType* WriteFrameInitializedFromHeadImpl(int32 Frame)
	{
		if (Frame > ((T*)this)->HeadFrame())
		{
			ElementType* HeadElementPtr = ((T*)this)->HeadElement();
			ElementType* NewElement = ((T*)this)->WriteFrame(Frame);
			if (HeadElementPtr)
			{
				*NewElement = *HeadElementPtr;
			}
			return NewElement;
		}
		else if (Frame < ((T*)this)->TailFrame())
		{
			ElementType* NewElement = ((T*)this)->WriteFrame(Frame);
			*NewElement = ElementType();
			return NewElement;
		}
		
		return ((T*)this)->WriteFrame(Frame);
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

	T* operator[](int32 Frame) {	return const_cast<T*>(GetImpl(Frame)); }
	const T* operator[](int32 Frame) const { return GetImpl(Frame); }

	T* Get(int32 Frame) { return const_cast<T*>(GetImpl(Frame)); }
	const T* Get(int32 Frame) const { return GetImpl(Frame); }

	const T* HeadElement() const { return HeadElementImpl(); }
	T* HeadElement() { return const_cast<T*>(HeadElementImpl()); }

	const T* TailElement() const { return TailElementImpl(); }
	T* TailElement() { return const_cast<T*>(TailElementImpl()); }

	int32 HeadFrame() const { return Head; }
	int32 TailFrame() const { return GetTail(); }

	int32 Num() const { return Head == INDEX_NONE ? 0 : (Head - GetTail() + 1); }
	int32 Max() const { return Data.Max(); }
	int32 GetDirtyCount() const { return DirtyCount; }

	// Returns the element @ frame for writing. The contents of this element are unknown (could be stale content!). Note the element returned is immediately considered "valid" by Num(), Iterators, etc!
	// The written frame becomes the Head element. Essentially invalidates any existing frames >= Frame
	T* WriteFrame(int32 Frame)
	{
		check(Frame >= 0);

		const int32 Tail = GetTail();
		if (Head == INDEX_NONE || Frame < Tail || Frame > Head+1)
		{
			// Writing outside the current range (+1) results in a full wipe of valid contents
			NumValidElements = 1;
		}
		else
		{
			// Writing inside the current range (+1) preserves valid elements up to the size of our buffer
			NumValidElements = FMath::Min<int32>(Frame - Tail + 1, Data.Num());
		}
		
		Head = Frame;
		++DirtyCount;
		return &Data[Frame % Data.Num()];
	}
	
	T* WriteFrameInitializedFromHead(int32 Frame)
	{
		return this->template WriteFrameInitializedFromHeadImpl<ElementType>(Frame);
	}

	TNetSimLazyWriterFunc<ElementType> LazyWriter(int32 Frame)
	{
		return TNetSimLazyWriterFunc<ElementType>(*this, Frame);
	}

	using IteratorType = TNetworkSimBufferIterator<TNetworkSimContiguousBuffer<ElementType, NumElements>, ElementType>;
	using ConstIteratorType = TNetworkSimBufferIterator<const TNetworkSimContiguousBuffer<ElementType, NumElements>, const ElementType>;

	IteratorType CreateIterator() { return IteratorType(*this); }
	ConstIteratorType CreateConstIterator() const { return ConstIteratorType(*this); }

private:

	const T* GetImpl(int32 Frame) const
	{
		const int32 RelativeToHead = Frame - Head;
		if (RelativeToHead > 0 || RelativeToHead <= -NumValidElements)
		{
			return nullptr;
		}

		return &Data[Frame % Data.Num()];
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
	int32 IterFrame(int32 Pos) const { return Pos; }
	T* IterElement(int32 Pos) { return &Data[Pos % Data.Num()]; }
	const T* IterElement(int32 Pos) const { return &Data[Pos % Data.Num()]; }
};

// Frame: arbitrary identifier for data. Not contiguous or controlled by us (always passed in)
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

	T* operator[](int32 Frame) {	return const_cast<T*>(GetImpl(Frame)); }
	const T* operator[](int32 Frame) const { return GetImpl(Frame); }

	T* Get(int32 Frame) { return const_cast<T*>(GetImpl(Frame)); }
	const T* Get(int32 Frame) const { return GetImpl(Frame); }

	const T* HeadElement() const { return HeadElementImpl(); }
	T* HeadElement() { return const_cast<T*>(HeadElementImpl()); }

	const T* TailElement() const { return TailElementImpl(); }
	T* TailElement() { return const_cast<T*>(TailElementImpl()); }

	int32 HeadFrame() const { return Data[HeadPos % Data.Num()].Frame; }
	int32 TailFrame() const { return Data[GetTailPos() % Data.Num()].Frame; }

	int32 Num() const { return HeadPos == INDEX_NONE ? 0 : (HeadPos - GetTailPos() + 1); }
	int32 Max() const { return Data.Max(); }
	int32 GetDirtyCount() const { return DirtyCount; }

	// Returns the element @ frame for writing. The contents of this element are unknown (could be stale content!). Note the element returned is immediately considered "valid" by Num(), Iterators, etc!
	// The written frame becomes the Head element. Essentially invalidates any existing frames >= Frame
	T* WriteFrame(int32 Frame)
	{
		check(Frame >= 0);
		++DirtyCount;

		int32 Pos = HeadPos;
		if (HeadPos != INDEX_NONE)
		{
			// If we have elements, find where this Frame would go
			const int32 Tail = GetTailPos();
			for (; Pos >= Tail; --Pos)
			{
				TInternal& InternalData = Data[Pos % Data.Num()];
				if (InternalData.Frame == Frame)
				{
					HeadPos = Pos;
					return &InternalData.Element;
				}
				if (InternalData.Frame <= Frame)
				{
					break;
				}
			}
		}

		// Write frame to next pos
		HeadPos = Pos+1;
		TInternal& WriteData = Data[HeadPos % Data.Num()];
		WriteData.Frame = Frame;
		return &WriteData.Element;
	}

	ElementType* WriteFrameInitializedFromHead(int32 Frame)
	{
		return this->template WriteFrameInitializedFromHeadImpl<ElementType>(Frame);
	}

	TNetSimLazyWriterFunc<ElementType> LazyWriter(int32 Frame)
	{
		return TNetSimLazyWriterFunc<ElementType>(*this, Frame);
	}

	using IteratorType = TNetworkSimBufferIterator<TNetworkSimSparseBuffer<ElementType, NumElements>, ElementType>;
	using ConstIteratorType = TNetworkSimBufferIterator<const TNetworkSimSparseBuffer<ElementType, NumElements>, const ElementType>;

	IteratorType CreateIterator() { return IteratorType(*this); }
	ConstIteratorType CreateConstIterator() const { return ConstIteratorType(*this); }

private:

	struct TInternal
	{
		int32 Frame = -1;
		T Element;
	};

	const T* GetImpl(int32 Frame) const
	{
		const int32 Pos = GetPosForFrame(Frame);
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
	
	int32 GetPosForFrame(int32 Frame) const
	{
		const int32 TailPos = GetTailPos();
		for (int32 Pos=HeadPos; Pos >= TailPos; --Pos)
		{
			if (Data[Pos % Data.Num()].Frame <= Frame)
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
	int32 IterFrame(int32 Pos) const { return Data[Pos % Data.Num()].Frame; }
	T* IterElement(int32 Pos) { return &Data[Pos % Data.Num()].Element; }
	const T* IterElement(int32 Pos) const { return &Data[Pos % Data.Num()].Element; }
};