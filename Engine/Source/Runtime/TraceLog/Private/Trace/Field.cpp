// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Detail/Field.h" // :(

#if UE_TRACE_ENABLED

#include "Trace/Detail/Writer.inl"

namespace UE {
namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
template <typename CallbackType>
static void Field_WriteAuxData(uint32 Index, int32 Size, CallbackType&& Callback)
{
	static_assert(
		sizeof(Private::FWriteBuffer::Overflow) >= sizeof(FAuxHeader) + sizeof(uint8 /*AuxDataTerminal*/),
		"FWriteBuffer::Overflow is not large enough"
	);

	// Early-out if there would be nothing to write
	if (Size == 0)
	{
		return;
	}

	// Header
	FWriteBuffer* Buffer = Writer_GetBuffer();
	auto* Header = (FAuxHeader*)(Buffer->Cursor);
	Header->Pack = Size << FAuxHeader::SizeShift;
	Header->Pack |= Index << FAuxHeader::FieldShift;
	Header->Uid = uint8(EKnownEventUids::AuxData) << EKnownEventUids::_UidShift;
	Buffer->Cursor += sizeof(FAuxHeader);

	// Array data
	bool bCommit = ((uint8*)Header == Buffer->Committed);
	while (true)
	{
		if (Buffer->Cursor >= (uint8*)Buffer)
		{
			if (bCommit)
			{
				AtomicStoreRelease(&(uint8* volatile&)(Buffer->Committed), Buffer->Cursor);
			}

			Buffer = Writer_NextBuffer(0);
			Buffer->Partial = 1;

			bCommit = true;
		}

		int32 Remaining = int32((uint8*)Buffer - Buffer->Cursor);
		int32 SegmentSize = (Remaining < Size) ? Remaining : Size;
		Callback(Buffer->Cursor, SegmentSize);
		Buffer->Cursor += SegmentSize;

		Size -= SegmentSize;
		if (Size <= 0)
		{
			break;
		}
	}

	if (bCommit)
	{
		AtomicStoreRelease(&(uint8* volatile&)(Buffer->Committed), Buffer->Cursor);
	}
}

////////////////////////////////////////////////////////////////////////////////
void Field_WriteAuxData(uint32 Index, const uint8* Data, int32 Size)
{
	auto MemcpyLambda = [&Data] (uint8* Cursor, int32 NumBytes)
	{
		memcpy(Cursor, Data, NumBytes);
		Data += NumBytes;
	};
	return Field_WriteAuxData(Index, Size, MemcpyLambda);
}

////////////////////////////////////////////////////////////////////////////////
void Field_WriteStringAnsi(uint32 Index, const WIDECHAR* String, int32 Length)
{
	int32 Size = Length;
	Size &= (FAuxHeader::SizeLimit - 1);

	auto WriteLambda = [&String] (uint8* Cursor, int32 NumBytes)
	{
		for (int32 i = 0; i < NumBytes; ++i)
		{
			*Cursor = uint8(*String & 0x7f);
			Cursor++;
			String++;
		}
	};

	return Field_WriteAuxData(Index, Size, WriteLambda);
}

////////////////////////////////////////////////////////////////////////////////
void Field_WriteStringAnsi(uint32 Index, const ANSICHAR* String, int32 Length)
{
	int32 Size = Length * sizeof(String[0]);
	Size &= (FAuxHeader::SizeLimit - 1); // a very crude "clamp"
	return Field_WriteAuxData(Index, (const uint8*)String, Size);
}

////////////////////////////////////////////////////////////////////////////////
void Field_WriteStringWide(uint32 Index, const WIDECHAR* String, int32 Length)
{
	int32 Size = Length * sizeof(String[0]);
	Size &= (FAuxHeader::SizeLimit - 1); // (see above)
	return Field_WriteAuxData(Index, (const uint8*)String, Size);
}

} // namespace Private
} // namespace Trace
} // namespace UE

#endif // UE_TRACE_ENABLED
