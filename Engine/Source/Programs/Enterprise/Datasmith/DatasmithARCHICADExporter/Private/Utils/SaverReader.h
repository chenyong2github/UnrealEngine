// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

BEGIN_NAMESPACE_UE_AC

// Class to save data in memory
class CSaver
{
  public:
	// Contructor (if dest is NULL and size is 0 then just needed size is compute)
	CSaver(size_t inDestSize = 0, void* dest = nullptr)
		: Buffer(reinterpret_cast< char* >(dest))
		, Position(0)
		, BufferSize(inDestSize)
		, Allocated(nullptr)
	{
		if (Buffer == nullptr && BufferSize != 0)
		{
			Allocated = new char[BufferSize];
			Buffer = Allocated;
		}
	}

	~CSaver() { delete[] Allocated; }

	// Template save function
	template < class T > void SaveTo(const T& InValue) { Save(&InValue, sizeof(T)); }

	// Return the allocated buffer
	void* GetBuffer() const { return Allocated; }

	// Return cumulated saved size
	size_t GetPos() const { return Position; }

  private:
	// Untyped save function used by template one
	void Save(const void* InData, size_t DataSize)
	{
		if (Buffer != nullptr)
		{
			if (Position + DataSize > BufferSize)
			{
				if (Allocated)
				{
					size_t newSize = Position + DataSize + BufferSize;
					Buffer = new char[newSize];
					memcpy(Buffer, Allocated, Position);
					delete[] Allocated;
					Allocated = Buffer;
					BufferSize = newSize;
				}
				UE_AC_Assert(Position + DataSize <= BufferSize);
			}
			memcpy(Buffer + Position, InData, DataSize);
		}
		Position += DataSize;
	}

	char*  Buffer;
	size_t Position;
	size_t BufferSize;
	char*  Allocated;
};

// Specialization for std::string
template <> inline void CSaver::SaveTo(const std::string& DataSize)
{
	Save(DataSize.c_str(), DataSize.size() + 1);
}

// Class to read data from memory
class CReader
{
  public:
	// Contructor
	CReader(size_t InFromSize, const void* From = nullptr)
		: Buffer(reinterpret_cast< const char* >(From))
		, Position(0)
		, BufferSize(InFromSize)
		, Allocated(nullptr)
	{
		if (Buffer == nullptr)
		{
			Allocated = new char[InFromSize];
			Buffer = Allocated;
		}
	}

	~CReader() { delete[] Allocated; }

	// Template read function
	template < class T > void ReadFrom(T* t) { Read(t, sizeof(T)); }

	void* GetBuffer() { return Allocated; }

	// Return cumulated readed size
	size_t GetPos() const { return Position; }

	// Return buffer size
	size_t GetSize() const { return BufferSize; }

  private:
	// Untyped read function used by template one
	void Read(void* OutData, size_t DataSize)
	{
		UE_AC_Assert(Position + DataSize <= BufferSize);
		memcpy(OutData, Buffer + Position, DataSize);
		Position += DataSize;
	}

	const char* Buffer;
	size_t		Position;
	size_t		BufferSize;
	char*		Allocated;
};

// Specialization for std::string
template <> inline void CReader::ReadFrom(std::string* OutString)
{
	size_t NbChars = strnlen(Buffer + Position, BufferSize - Position) + 1;
	UE_AC_Assert(Position + NbChars <= BufferSize);
	*OutString = Buffer + Position;
	Position += NbChars;
}

END_NAMESPACE_UE_AC
