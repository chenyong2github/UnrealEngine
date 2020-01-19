// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

#include "CborReader.h"
#include "CborWriter.h"
#include "Containers/StringView.h"
#include "Serialization/MemoryArchive.h"
#include "Serialization/MemoryReader.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
template <int BufferSize>
struct TInlineMemoryWriter
	: public FMemoryArchive
{
					TInlineMemoryWriter();
	virtual void	Serialize(void* Data, int64 Num) override;
	uint32			Used = 0;
	uint8			Buffer[BufferSize];
};

////////////////////////////////////////////////////////////////////////////////
template <int BufferSize>
inline TInlineMemoryWriter<BufferSize>::TInlineMemoryWriter()
{
	SetIsSaving(true);
}

////////////////////////////////////////////////////////////////////////////////
template <int BufferSize>
inline void TInlineMemoryWriter<BufferSize>::Serialize(void* Data, int64 Num)
{
	uint32 NextUsed = Used + uint32(Num);
	if (NextUsed <= BufferSize)
	{
		memcpy(Buffer + Used, Data, Num);
	}
	Used = NextUsed;
}



////////////////////////////////////////////////////////////////////////////////
struct FPayload
{
	const uint8*	Data;
	uint32			Size;
};



////////////////////////////////////////////////////////////////////////////////
template <int Size=128>
class TPayloadBuilder
{
public:
								TPayloadBuilder(int32 StatusCode);
	template <int N>			TPayloadBuilder(const char (&MethodName)[N]);
	template <int N> void		AddInteger(const char (&Name)[N], int64 Value);
	template <int N> void		AddString(const char (&Name)[N], const char* Value, int32 Length=-1);
	FPayload					Done();

private:
	TInlineMemoryWriter<Size>	MemoryWriter;
	FCborWriter					CborWriter = &MemoryWriter;
};

////////////////////////////////////////////////////////////////////////////////
template <int Size>
inline TPayloadBuilder<Size>::TPayloadBuilder(int32 StatusCode)
{
	CborWriter.WriteContainerStart(ECborCode::Map, -1);
	AddInteger("$status", StatusCode);
}

////////////////////////////////////////////////////////////////////////////////
template <int Size>
template <int N>
inline TPayloadBuilder<Size>::TPayloadBuilder(const char (&MethodName)[N])
{
	CborWriter.WriteContainerStart(ECborCode::Map, -1);
	AddString("$method", MethodName, N);
}

////////////////////////////////////////////////////////////////////////////////
template <int Size>
template <int N>
inline void TPayloadBuilder<Size>::AddInteger(const char (&Name)[N], int64 Value)
{
	CborWriter.WriteValue(Name, N);
	CborWriter.WriteValue(Value);
}

////////////////////////////////////////////////////////////////////////////////
template <int Size>
template <int N>
inline void TPayloadBuilder<Size>::AddString(
	const char (&Name)[N],
	const char* Value,
	int Length)
{
	Length = (Length < 0) ? int32(strlen(Value)) : Length;
	CborWriter.WriteValue(Name, N);
	CborWriter.WriteValue(Value, Length);
}

////////////////////////////////////////////////////////////////////////////////
template <int Size>
inline FPayload TPayloadBuilder<Size>::Done()
{
	CborWriter.WriteContainerEnd();
	return { MemoryWriter.Buffer, MemoryWriter.Used };
}



////////////////////////////////////////////////////////////////////////////////
class FResponse
{
public:
	int64			GetInteger(const char* Key, int64 Default) const;
	template <int N>
	FAnsiStringView	GetString(const char* Key, const char (&Default)[N]) const;
	const uint8*	GetData() const;
	uint32			GetSize() const;
	uint8*			Reserve(uint32 Size);

private:
	template <typename Type, typename LambdaType>
	Type			GetValue(const char* Key, Type Default, LambdaType&& Lambda) const;
	TArray<uint8>	Buffer;
};

////////////////////////////////////////////////////////////////////////////////
inline const uint8* FResponse::GetData() const
{
	return Buffer.GetData();
};

////////////////////////////////////////////////////////////////////////////////
inline uint32 FResponse::GetSize() const
{
	return Buffer.Num();
}

////////////////////////////////////////////////////////////////////////////////
inline uint8* FResponse::Reserve(uint32 Size)
{
	Buffer.SetNumUninitialized(Size);
	return Buffer.GetData();
}

////////////////////////////////////////////////////////////////////////////////
template <typename Type, typename LambdaType>
inline Type FResponse::GetValue(const char* Key, Type Default, LambdaType&& Lambda) const
{
	FMemoryReader MemoryReader(Buffer);
	FCborReader CborReader(&MemoryReader);
	FCborContext Context;

	if (!CborReader.ReadNext(Context) || Context.MajorType() != ECborCode::Map)
	{
		return Default;
	}

	while (true)
	{
		// Read key
		if (!CborReader.ReadNext(Context) || !Context.IsString())
		{
			return Default;
		}

		const char* String = Context.AsCString();
		bool bIsTarget = (FCStringAnsi::Strcmp(Key, String) == 0);

		// Read value
		if (!CborReader.ReadNext(Context))
		{
			return Default;
		}

		if (bIsTarget)
		{
			return Lambda(Context, uint32(MemoryReader.Tell()));
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
inline int64 FResponse::GetInteger(const char* Key, int64 Default) const
{
	return GetValue(
		Key,
		Default,
		[this] (const FCborContext& Context, int64)
		{
			return int32(Context.AsInt());
		}
	);
}

////////////////////////////////////////////////////////////////////////////////
template <int N>
inline FAnsiStringView FResponse::GetString(const char* Key, const char (&Default)[N]) const
{
	FAnsiStringView DefaultView(Default, N);
	return GetValue(
		Key,
		DefaultView,
		[this, DefaultView] (const FCborContext& Context, uint32 Offset)
		{
			if (Context.IsString())
			{
				int32 Length = Context.AsLength();
				const char* Data = (const char*)(Buffer.GetData() + Offset - Length);
				return FAnsiStringView(Data, Length - 1);
			}

			return DefaultView;
		}
	);
}

} // namespace Trace

#endif // TRACE_WITH_ASIO
