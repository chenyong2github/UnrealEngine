// Copyright 2011-2020 Molecular Matters GmbH, all rights reserved.

// BEGIN EPIC MOD
//#include PCH_INCLUDE
// END EPIC MOD
#include "LC_VirtualMemoryRange.h"
#include "LC_PointerUtil.h"
// BEGIN EPIC MOD
#include "LC_Logging.h"
#include <cinttypes>
// END EPIC MOD

VirtualMemoryRange::VirtualMemoryRange(Process::Handle processHandle, const void* addressStart, const void* addressEnd, size_t alignment)
	: m_processHandle(processHandle)
	, m_addressStart(addressStart)
	, m_addressEnd(addressEnd)
	, m_alignment(alignment)
{
	m_pageData.reserve(32u);
}


void VirtualMemoryRange::ReservePages(void)
{
// BEGIN EPIC MOD
// If we have two modules in the same process, then they might overlap in the 2GB range. Thus when we try to update one module
// the other module might prevent us from finding address space.  Disable this feature for now.
#if 0
	// reserve all free pages in the virtual memory range.
	// pages must be aligned to the given alignment.
	for (const void* address = m_addressStart; address < m_addressEnd; /* nothing */)
	{
		// align address to be scanned
		address = pointer::AlignTop<const void*>(address, m_alignment);

		::MEMORY_BASIC_INFORMATION memoryInfo = {};
		const size_t bytesReturned = ::VirtualQueryEx(+m_processHandle, address, &memoryInfo, sizeof(::MEMORY_BASIC_INFORMATION));

		// we are only interested in free pages
		if ((bytesReturned > 0u) && (memoryInfo.State == MEM_FREE))
		{
			// work out the maximum size of the page allocation.
			// we should not allocate past the end of the range.
			const size_t bytesLeft = pointer::Displacement<size_t>(memoryInfo.BaseAddress, m_addressEnd);
			const size_t size = std::min<size_t>(memoryInfo.RegionSize, bytesLeft);

			// try to reserve this page.
			// if we are really unlucky, the process might have allocated this region in the meantime.
			void* baseAddress = ::VirtualAllocEx(+m_processHandle, memoryInfo.BaseAddress, size, MEM_RESERVE, PAGE_NOACCESS);
			if (baseAddress)
			{
				LC_LOG_DEV("Found virtual memory region at 0x%p with size 0x%" PRIX64, baseAddress, size);
				m_pageData.emplace_back(PageData { baseAddress });
			}
		}

		// keep on searching
		address = pointer::Offset<const void*>(memoryInfo.BaseAddress, memoryInfo.RegionSize);
	}
#endif
// END EPIC MOD
}


void VirtualMemoryRange::FreeReservedPages(void)
{
	for (const auto& it : m_pageData)
	{
		const bool success = ::VirtualFreeEx(+m_processHandle, it.address, 0u, MEM_RELEASE);
		if (!success)
		{
			LC_WARNING_USER("Cannot free virtual memory region at 0x%p", it.address);
		}
	}

	m_pageData.clear();
}
