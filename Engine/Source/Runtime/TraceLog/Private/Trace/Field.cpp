// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Detail/Field.h" // :(

#if UE_TRACE_ENABLED

#include "Trace/Detail/Writer.inl"

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
void Field_WriteAuxData(uint32 Index, const uint8* Data, int32 Size)
{
	static_assert(sizeof(Private::FWriteBuffer::Overflow) >= sizeof(FAuxHeader), "FWriteBuffer::Overflow is not large enough");

	// Header
	const int bMaybeHasAux = true;
	FWriteBuffer* Buffer = Writer_GetBuffer();
	Buffer->Cursor += sizeof(FAuxHeader) - bMaybeHasAux;

	auto* Header = (FAuxHeader*)(Buffer->Cursor - sizeof(FAuxHeader));
	Header->Size = Size << 8;
	Header->FieldIndex = uint8(0x80 | (Index & int(EIndexPack::FieldCountMask)));

	bool bCommit = ((uint8*)Header + bMaybeHasAux == Buffer->Committed);

	// Array data
	while (true)
	{
		if (Buffer->Cursor >= (uint8*)Buffer)
		{
			if (bCommit)
			{
				AtomicStoreRelease(&(uint8* volatile&)(Buffer->Committed), Buffer->Cursor);
			}

			Buffer = Writer_NextBuffer(0);
			bCommit = true;
		}

		int32 Remaining = int32((uint8*)Buffer - Buffer->Cursor);
		int32 SegmentSize = (Remaining < Size) ? Remaining : Size;
		memcpy(Buffer->Cursor, Data, SegmentSize);
		Buffer->Cursor += SegmentSize;

		Size -= SegmentSize;
		if (Size <= 0)
		{
			break;
		}

		Data += SegmentSize;
	}

	// The auxilary data null terminator.
	Buffer->Cursor[0] = 0;
	Buffer->Cursor++;

	if (bCommit)
	{
		AtomicStoreRelease(&(uint8* volatile&)(Buffer->Committed), Buffer->Cursor);
	}
}

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
