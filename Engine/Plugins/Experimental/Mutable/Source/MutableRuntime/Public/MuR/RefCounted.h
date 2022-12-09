// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/MutableMemory.h"
#include "MuR/Ptr.h"

#include "HAL/PlatformAtomics.h"
#include "HAL/LowLevelMemTracker.h"

#include <atomic>

namespace mu
{


	//! \brief %Base class for all reference counted objects.
	//!
	//! Any subclass of this class can be managed using smart pointers through the Ptr<T> template.
	//! \warning This base allow multi-threaded manipulation of smart pointers, since the count
	//! increments and decrements are atomic.
	//! \ingroup runtime
	class MUTABLERUNTIME_API RefCounted : public Base
	{
	public:

		inline void IncRef() const
		{
			m_refCount++;
		}

		inline void DecRef() const
		{
			LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));

			if (m_refCount.fetch_sub(1) == 1)
			{
				delete this;
			}
		}

		inline int32 GetRefCount() const
		{
			return m_refCount;
		}

		RefCounted(const RefCounted&) = delete;
		RefCounted(const RefCounted&&) = delete;
		RefCounted& operator=(const RefCounted&) = delete;
		RefCounted& operator=(const RefCounted&&) = delete;

	protected:

		RefCounted()
		{
			m_refCount = 0;
		}

		inline virtual ~RefCounted() = default;

	private:

		mutable std::atomic<int32> m_refCount;

	};


	inline void mutable_ptr_add_ref(const RefCounted* p)
	{
		if (p) p->IncRef();
	}


	inline void mutable_ptr_release(const RefCounted* p)
	{
		if (p)
		{
			p->DecRef();
		}
	}

}

