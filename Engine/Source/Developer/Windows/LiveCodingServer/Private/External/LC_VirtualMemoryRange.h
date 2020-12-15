// Copyright 2011-2020 Molecular Matters GmbH, all rights reserved.

#pragma once

// BEGIN EPIC MOD
#include "CoreTypes.h"
// END EPIC MOD
#include "LC_ProcessTypes.h"
// BEGIN EPIC MOD
#include "LC_Types.h"
// END EPIC MOD


class VirtualMemoryRange
{
public:
	VirtualMemoryRange(Process::Handle processHandle, const void* addressStart, const void* addressEnd, size_t alignment);

	void ReservePages(void);
	void FreeReservedPages(void);

private:
	struct PageData
	{
		void* address;
	};

	Process::Handle m_processHandle;
	const void* m_addressStart;
	const void* m_addressEnd;
	size_t m_alignment;

	types::vector<PageData> m_pageData;
};
