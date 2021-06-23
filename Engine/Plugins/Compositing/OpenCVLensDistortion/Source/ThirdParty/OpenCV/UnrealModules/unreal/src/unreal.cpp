// Copyright Epic Games, Inc. All Rights Reserved.

#include "opencv2/unreal.hpp"


namespace cv 
{ 
	namespace unreal 
	{
		/** Keeps pointer to Unreal's FMemory::Malloc */
		static TUnrealMalloc UnrealMalloc = nullptr;

		/** Keeps pointer to Unreal's FMemory::Free */
		static TUnrealFree UnrealFree = nullptr;

		void SetMallocAndFree(TUnrealMalloc InUnrealMalloc, TUnrealFree InUnrealFree)
		{
			UnrealMalloc = InUnrealMalloc;
			UnrealFree = InUnrealFree;
		}
	}
}

/** operator new overrides */

void* operator new(std::size_t size)
{
	return cv::unreal::UnrealMalloc ? cv::unreal::UnrealMalloc(size, 0) : malloc(size);
}

void* operator new(std::size_t size, const std::nothrow_t&)
{
	return cv::unreal::UnrealMalloc ? cv::unreal::UnrealMalloc(size, 0) : malloc(size);
}

void* operator new[](std::size_t size)
{
	return cv::unreal::UnrealMalloc ? cv::unreal::UnrealMalloc(size, 0) : malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&)
{
	return cv::unreal::UnrealMalloc ? cv::unreal::UnrealMalloc(size, 0) : malloc(size);
}

/** operator delete overrides */

void  operator delete(void* p)
{
	cv::unreal::UnrealFree ? cv::unreal::UnrealFree(p) : free(p);
}

void  operator delete(void* p, const std::nothrow_t&)
{
	cv::unreal::UnrealFree ? cv::unreal::UnrealFree(p) : free(p);
}

void  operator delete[](void* p)
{
	cv::unreal::UnrealFree ? cv::unreal::UnrealFree(p) : free(p);
}

void  operator delete[](void* p, const std::nothrow_t&)
{
	cv::unreal::UnrealFree ? cv::unreal::UnrealFree(p) : free(p);
}
