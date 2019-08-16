// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "CoreTypes.h"
#include "LC_Platform.h"

class MemoryBlock
{
public:
	MemoryBlock(const void* data, size_t size);
	~MemoryBlock(void);

	inline const void* GetData(void) const
	{
		return m_data;
	}

	inline size_t GetSize(void) const
	{
		return m_size;
	}

private:
	void* m_data;
	size_t m_size;

	LC_DISABLE_COPY(MemoryBlock);
	LC_DISABLE_MOVE(MemoryBlock);
	LC_DISABLE_ASSIGNMENT(MemoryBlock);
	LC_DISABLE_MOVE_ASSIGNMENT(MemoryBlock);
};
