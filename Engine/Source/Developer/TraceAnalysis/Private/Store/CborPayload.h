// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"
#include "CborReader.h"
#include "CborWriter.h"
#include "Containers/Array.h"
#include "Containers/ContainerAllocationPolicies.h"
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

	typedef TArray<uint8, TInlineAllocator<BufferSize>> BufferType;

					TInlineMemoryWriter();
	virtual void	Serialize(void* Data, int64 Num) override;
	BufferType		Buffer;
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
	Buffer.Append((const uint8*)Data, Num);
}



////////////////////////////////////////////////////////////////////////////////
struct FPayload
{
	const uint8*	Data;
	uint32			Size;
};



////////////////////////////////////////////////////////////////////////////////
enum class EStatusCode
{
	Unknown				= 0,
	Success				= 200,
	BadRequest			= 400,
	MethodNotAllowed	= 405,
	InternalError		= 500,
};



////////////////////////////////////////////////////////////////////////////////
template <int Size=128>
class TPayloadBuilder
{
public:
								TPayloadBuilder(EStatusCode StatusCode);
	template <int N>			TPayloadBuilder(const char (&MethodName)[N]);
	template <int N> void		AddInteger(const char (&Name)[N], int64 Value);
	template <int N> void		AddString(const char (&Name)[N], const char* Value, int32 Length=-1);
	FPayload					Done();

private:
	TInlineMemoryWriter<Size>	MemoryWriter;
	FCborWriter					CborWriter = { &MemoryWriter, ECborEndianness::StandardCompliant };
};

////////////////////////////////////////////////////////////////////////////////
template <int Size>
inline TPayloadBuilder<Size>::TPayloadBuilder(EStatusCode StatusCode)
{
	CborWriter.WriteContainerStart(ECborCode::Map, -1);
	AddInteger("$status", int32(StatusCode));
}

////////////////////////////////////////////////////////////////////////////////
template <int Size>
template <int N>
inline TPayloadBuilder<Size>::TPayloadBuilder(const char (&MethodName)[N])
{
	CborWriter.WriteContainerStart(ECborCode::Map, -1);
	AddString("$method", MethodName, N - 1);
}

////////////////////////////////////////////////////////////////////////////////
template <int Size>
template <int N>
inline void TPayloadBuilder<Size>::AddInteger(const char (&Name)[N], int64 Value)
{
	CborWriter.WriteValue(Name, N - 1);
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
	CborWriter.WriteValue(Name, N - 1);
	CborWriter.WriteValue(Value, Length);
}

////////////////////////////////////////////////////////////////////////////////
template <int Size>
inline FPayload TPayloadBuilder<Size>::Done()
{
	CborWriter.WriteContainerEnd();
	return { MemoryWriter.Buffer.GetData(), uint32(MemoryWriter.Buffer.Num()) };
}



////////////////////////////////////////////////////////////////////////////////
class FResponse
{
public:
	EStatusCode		GetStatusCode() const;
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
inline EStatusCode FResponse::GetStatusCode() const
{
	int32 Code = GetInteger("$status", 0);
	return Code ? EStatusCode(Code) : EStatusCode::Unknown;
}

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
	FCborReader CborReader(&MemoryReader, ECborEndianness::StandardCompliant);
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

		uint32 Length = Context.AsLength();
		uint32 Offset = uint32(MemoryReader.Tell());
		auto* String = (const char*)(Buffer.GetData() + Offset - Length);
		bool bIsTarget = (FCStringAnsi::Strncmp(Key, String, Length) == 0);

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
			return Context.AsInt();
		}
	);
}

////////////////////////////////////////////////////////////////////////////////
template <int N>
inline FAnsiStringView FResponse::GetString(const char* Key, const char (&Default)[N]) const
{
	FAnsiStringView DefaultView(Default, N - 1);
	return GetValue(
		Key,
		DefaultView,
		[this, DefaultView] (const FCborContext& Context, uint32 Offset)
		{
			if (Context.IsString())
			{
				int32 Length = Context.AsLength();
				const char* Data = (const char*)(Buffer.GetData() + Offset - Length);
				return FAnsiStringView(Data, Length);
			}

			return DefaultView;
		}
	);
}

} // namespace Trace
