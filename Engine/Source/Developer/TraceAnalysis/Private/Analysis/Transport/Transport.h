// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataStream.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FTransport
{
public:
	virtual					~FTransport() {}
	void					SetSource(FStreamReader::FData& InSource);
	template <typename RetType>
	RetType const*			GetPointer();
	template <typename RetType>
	RetType const*			GetPointer(uint32 BlockSize);
	virtual void			Advance(uint32 BlockSize);

protected:
	virtual const uint8*	GetPointerImpl(uint32 BlockSize);
	FStreamReader::FData*	Source;
};

////////////////////////////////////////////////////////////////////////////////
inline void FTransport::SetSource(FStreamReader::FData& InSource)
{
	Source = &InSource;
}

////////////////////////////////////////////////////////////////////////////////
template <typename RetType>
inline RetType const* FTransport::GetPointer()
{
	return GetPointer<RetType>(sizeof(RetType));
}

////////////////////////////////////////////////////////////////////////////////
template <typename RetType>
inline RetType const* FTransport::GetPointer(uint32 BlockSize)
{
	return (RetType const*)GetPointerImpl(BlockSize);
}

////////////////////////////////////////////////////////////////////////////////
inline void FTransport::Advance(uint32 BlockSize)
{
	Source->Advance(BlockSize);
}

////////////////////////////////////////////////////////////////////////////////
inline const uint8* FTransport::GetPointerImpl(uint32 BlockSize)
{
	return Source->GetPointer(BlockSize);
}

} // namespace Trace
