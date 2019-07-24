// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#include "LC_MemoryBlock.h"


MemoryBlock::MemoryBlock(const void* data, size_t size)
	: m_data(malloc(size))
	, m_size(size)
{
	memcpy(m_data, data, size);
}


MemoryBlock::~MemoryBlock(void)
{
	free(m_data);
}
