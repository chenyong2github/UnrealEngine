// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/BlockCompression/Miro/Miro.h"
#include "MuR/MemoryPrivate.h"
#include "MuR/MutableMemory.h"

#include <stdint.h>
#include <cstdlib>

namespace mu
{

	//! This would be nice, but apparently several compilers don't support it.
#define MUTABLE_USE_STD_ALIGNED_ALLOC   1


//! Static pointers to the user provided malloc and free methods.
//! If they are null (default) the versions on the standard library will be used.
	static void* (*s_custom_malloc)(size_t, uint32_t) = nullptr;
	static void(*s_custom_free)(void*) = nullptr;


	static int s_initialized = 0;
	static int s_finalized = 0;


	//---------------------------------------------------------------------------------------------
#if MUTABLE_USE_STD_ALIGNED_ALLOC

	inline void* mutable_aligned_alloc(size_t size, size_t align)
	{
#if defined(_WIN32) && _WIN32
		return _aligned_malloc(size, align);
#elif defined(MUTABLE_PLATFORM_ANDROID)
		return memalign(align, size);
#else
		return aligned_alloc(align, size);
#endif
	}

	inline void mutable_aligned_free(void* p)
	{
#if defined(_WIN32) && _WIN32
		_aligned_free(p);
#else
		free(p);
#endif
	}

#else

	typedef uint16_t offset_t;
#define PTR_OFFSET_SZ sizeof( offset_t ) 
#ifndef align_up
#define align_up( num, align ) ( ( ( num ) + ( (align)-1 ) ) & ~( (align)-1 ) )
#endif

	inline void* mutable_aligned_alloc(size_t size, size_t align)
	{
		void* ptr = nullptr;

		// We want it to be a power of two since align_up operates on powers of two
		assert((align & (align - 1)) == 0);

		if (align && size)
		{
			// We know we have to fit an offset value
			// We also allocate extra bytes to ensure we
			// can meet the alignment
			size_t hdr_size = PTR_OFFSET_SZ + (align - 1);
			void* p = malloc(size + hdr_size);

			if (p)
			{
				// Add the offset size to malloc's pointer (we will always store that)
				// Then align the resulting value to the
				// target alignment
				ptr = (void*)align_up(((uintptr_t)p + PTR_OFFSET_SZ), align);

				// Calculate the offset and store it
				// behind our aligned pointer
				*((offset_t*)ptr - 1) = (offset_t)((uintptr_t)ptr - (uintptr_t)p);

			} // else NULL, could not malloc
		}     // else NULL, invalid arguments

		return ptr;
	}


	void mutable_aligned_free(void* ptr)
	{
		assert(ptr);

		// Walk backwards from the passed-in pointer to get the pointer offset. We convert to an 
		// offset_t pointer and rely on pointer math to get the data
		offset_t offset = *((offset_t*)ptr - 1);

		// Once we have the offset, we can get our original pointer and call free
		void* p = (void*)((uint8_t*)ptr - offset);
		free(p);
	}

#endif


	//---------------------------------------------------------------------------------------------
	//! Call the system malloc or the user-provided replacement.
	//---------------------------------------------------------------------------------------------
	inline void* lowerlevel_malloc(size_t bytes)
	{
		void* pMem = 0;
		if (s_custom_malloc)
		{
			pMem = s_custom_malloc(bytes, 1);
		}
		else
		{
			pMem = malloc(bytes);
		}
		return pMem;
	}

	inline void* lowerlevel_malloc_aligned(size_t bytes, uint32_t alignment)
	{
		void* pMem = 0;
		if (s_custom_malloc)
		{
			pMem = s_custom_malloc(bytes, alignment);
		}
		else
		{
			size_t padding = bytes % alignment;
			bytes += padding ? alignment - padding : 0;
			pMem = mutable_aligned_alloc(bytes, alignment);
		}
		return pMem;
	}

	//-------------------------------------------------------------------------------------------------
	//! Call the system free or the user-provided replacement.
	//-------------------------------------------------------------------------------------------------
	inline void lowerlevel_free(void* ptr)
	{
		if (s_custom_free)
		{
			s_custom_free(ptr);
		}
		else
		{
			free(ptr);
		}
	}


	//-------------------------------------------------------------------------------------------------
	inline void lowerlevel_free_aligned(void* ptr)
	{
		if (s_custom_free)
		{
			s_custom_free(ptr);
		}
		else
		{
			mutable_aligned_free(ptr);
		}
	}


	//-------------------------------------------------------------------------------------------------
	//! Memory management functions to be used inside the library. No other memory allocation is
	//! allowed.
	//-------------------------------------------------------------------------------------------------
	void* mutable_malloc(size_t size)
	{
		if (!size)
		{
			return nullptr;
		}

		void* pMem = nullptr;

		pMem = lowerlevel_malloc(size);

		return pMem;
	}


	void* mutable_malloc_aligned(size_t size, uint32_t alignment)
	{
		if (!size)
		{
			return nullptr;
		}

		void* pMem = nullptr;

		pMem = lowerlevel_malloc_aligned(size, alignment);

		return pMem;
	}


	void mutable_free(void* ptr)
	{
		if (!ptr)
		{
			return;
		}

		lowerlevel_free(ptr);
	}


	void mutable_free(void* ptr, size_t size)
	{
		if (!ptr || !size)
		{
			return;
		}

		lowerlevel_free(ptr);
	}


	void mutable_free_aligned(void* ptr, size_t size)
	{
		if (!size || !ptr)
		{
			return;
		}

		lowerlevel_free_aligned(ptr);
	}


	//-------------------------------------------------------------------------------------------------
	void Initialize
	(
		void* (*customMalloc)(size_t, uint32_t),
		void (*customFree)(void*)
	)
	{
		if (!s_initialized)
		{
			s_initialized = 1;
			s_finalized = 0;

			s_custom_malloc = customMalloc;
			s_custom_free = customFree;

			miro::initialize();
		}
	}


	//-------------------------------------------------------------------------------------------------
	void Finalize()
	{
		if (s_initialized && !s_finalized)
		{
			miro::finalize();

			s_finalized = 1;
			s_initialized = 0;
			s_custom_malloc = nullptr;
			s_custom_free = nullptr;
		}
	}


}
