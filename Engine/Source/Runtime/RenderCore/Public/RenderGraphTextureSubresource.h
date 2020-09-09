// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"

struct FRDGTextureSubresource
{
	FRDGTextureSubresource()
		: MipIndex(0)
		, PlaneSlice(0)
		, ArraySlice(0)
	{}

	FRDGTextureSubresource(uint32 InMipIndex, uint32 InArraySlice, uint32 InPlaneSlice)
		: MipIndex(InMipIndex)
		, PlaneSlice(InPlaneSlice)
		, ArraySlice(InArraySlice)
	{}

	inline bool operator == (const FRDGTextureSubresource& RHS) const
	{
		return MipIndex == RHS.MipIndex
			&& PlaneSlice == RHS.PlaneSlice
			&& ArraySlice == RHS.ArraySlice;
	}

	inline bool operator != (const FRDGTextureSubresource& RHS) const
	{
		return !(*this == RHS);
	}

	inline bool operator < (const FRDGTextureSubresource& RHS) const
	{
		return MipIndex < RHS.MipIndex
			&& PlaneSlice < RHS.PlaneSlice
			&& ArraySlice < RHS.ArraySlice;
	}

	inline bool operator <= (const FRDGTextureSubresource& RHS) const
	{
		return MipIndex <= RHS.MipIndex
			&& PlaneSlice <= RHS.PlaneSlice
			&& ArraySlice <= RHS.ArraySlice;
	}

	inline bool operator > (const FRDGTextureSubresource& RHS) const
	{
		return MipIndex > RHS.MipIndex
			&& PlaneSlice > RHS.PlaneSlice
			&& ArraySlice > RHS.ArraySlice;
	}

	inline bool operator >= (const FRDGTextureSubresource& RHS) const
	{
		return MipIndex >= RHS.MipIndex
			&& PlaneSlice >= RHS.PlaneSlice
			&& ArraySlice >= RHS.ArraySlice;
	}

	uint32 MipIndex   : 8;
	uint32 PlaneSlice : 8;
	uint32 ArraySlice : 16;
};

struct FRDGTextureSubresourceLayout
{
	FRDGTextureSubresourceLayout()
		: NumMips(0)
		, NumPlaneSlices(0)
		, NumArraySlices(0)
	{}

	FRDGTextureSubresourceLayout(uint32 InNumMips, uint32 InNumArraySlices, uint32 InNumPlaneSlices)
		: NumMips(InNumMips)
		, NumPlaneSlices(InNumPlaneSlices)
		, NumArraySlices(InNumArraySlices)
	{}

	inline uint32 GetSubresourceCount() const
	{
		return NumMips * NumArraySlices * NumPlaneSlices;
	}

	inline uint32 GetSubresourceIndex(FRDGTextureSubresource Subresource) const
	{
		check(Subresource < GetMaxSubresource());
		return Subresource.MipIndex + (Subresource.ArraySlice * NumMips) + (Subresource.PlaneSlice * NumMips * NumArraySlices);
	}

	inline FRDGTextureSubresource GetMaxSubresource() const
	{
		return FRDGTextureSubresource(NumMips, NumArraySlices, NumPlaneSlices);
	}

	inline bool operator == (FRDGTextureSubresourceLayout const& RHS) const
	{
		return NumMips == RHS.NumMips
			&& NumPlaneSlices == RHS.NumPlaneSlices
			&& NumArraySlices == RHS.NumArraySlices;
	}

	inline bool operator != (FRDGTextureSubresourceLayout const& RHS) const
	{
		return !(*this == RHS);
	}

	uint32 NumMips        : 8;
	uint32 NumPlaneSlices : 8;
	uint32 NumArraySlices : 16;
};

struct FRDGTextureSubresourceRange
{
	FRDGTextureSubresourceRange()
		: MipIndex(0)
		, PlaneSlice(0)
		, ArraySlice(0)
		, NumMips(0)
		, NumPlaneSlices(0)
		, NumArraySlices(0)
	{}

	explicit FRDGTextureSubresourceRange(FRDGTextureSubresourceLayout Layout)
		: MipIndex(0)
		, PlaneSlice(0)
		, ArraySlice(0)
		, NumMips(Layout.NumMips)
		, NumPlaneSlices(Layout.NumPlaneSlices)
		, NumArraySlices(Layout.NumArraySlices)
	{}

	bool operator == (FRDGTextureSubresourceRange const& RHS) const
	{
		return MipIndex == RHS.MipIndex
			&& PlaneSlice == RHS.PlaneSlice
			&& ArraySlice == RHS.ArraySlice
			&& NumMips == RHS.NumMips
			&& NumPlaneSlices == RHS.NumPlaneSlices
			&& NumArraySlices == RHS.NumArraySlices;
	}

	bool operator != (FRDGTextureSubresourceRange const& RHS) const
	{
		return !(*this == RHS);
	}

	FRDGTextureSubresource GetMinSubresource() const
	{
		return FRDGTextureSubresource(MipIndex, ArraySlice, PlaneSlice);
	}

	FRDGTextureSubresource GetMaxSubresource() const
	{
		return FRDGTextureSubresource(MipIndex + NumMips, ArraySlice + NumArraySlices, PlaneSlice + NumPlaneSlices);
	}

	template <typename TFunction>
	void EnumerateSubresources(TFunction Function) const
	{
		const FRDGTextureSubresource MaxSubresource = GetMaxSubresource();
		const FRDGTextureSubresource MinSubresource = GetMinSubresource();

		for (uint32 LocalPlaneSlice = MinSubresource.PlaneSlice; LocalPlaneSlice < MaxSubresource.PlaneSlice; ++LocalPlaneSlice)
		{
			for (uint32 LocalArraySlice = MinSubresource.ArraySlice; LocalArraySlice < MaxSubresource.ArraySlice; ++LocalArraySlice)
			{
				for (uint32 LocalMipIndex = MinSubresource.MipIndex; LocalMipIndex < MaxSubresource.MipIndex; ++LocalMipIndex)
				{
					Function(FRDGTextureSubresource(LocalMipIndex, LocalArraySlice, LocalPlaneSlice));
				}
			}
		}
	}

	bool IsWholeResource(const FRDGTextureSubresourceLayout& Layout) const
	{
		return MipIndex == 0
			&& PlaneSlice == 0
			&& ArraySlice == 0
			&& NumMips == Layout.NumMips
			&& NumPlaneSlices == Layout.NumPlaneSlices
			&& NumArraySlices == Layout.NumArraySlices;
	}

	uint32 MipIndex       : 8;
	uint32 PlaneSlice     : 8;
	uint32 ArraySlice     : 16;
	uint32 NumMips        : 8;
	uint32 NumPlaneSlices : 8;
	uint32 NumArraySlices : 16;
};

template <typename ElementType, typename AllocatorType>
FORCEINLINE void VerifyLayout(const TRDGTextureSubresourceArray<ElementType, AllocatorType>& SubresourceArray, const FRDGTextureSubresourceLayout& Layout)
{
	checkf(Layout.GetSubresourceCount() > 0, TEXT("Subresource layout has no subresources."));
	checkf(SubresourceArray.Num() == 1 || SubresourceArray.Num() == Layout.GetSubresourceCount(), TEXT("Subresource array does not match the subresource layout."));
}

template <typename ElementType, typename AllocatorType>
FORCEINLINE void InitAsWholeResource(TRDGTextureSubresourceArray<ElementType, AllocatorType>& SubresourceArray, const ElementType& Element = {})
{
	SubresourceArray.SetNum(1, false);
	SubresourceArray[0] = Element;
}

template <typename ElementType, typename AllocatorType>
inline void InitAsSubresources(TRDGTextureSubresourceArray<ElementType, AllocatorType>& SubresourceArray, const FRDGTextureSubresourceLayout& Layout, const ElementType& Element = {})
{
	const uint32 SubresourceCount = Layout.GetSubresourceCount();
	checkf(SubresourceCount > 0, TEXT("Subresource layout has no subresources."));
	checkf(SubresourceCount > 1, TEXT("Subresource layout has only 1 resource. Use InitAsWholeResource instead."));
	SubresourceArray.SetNum(SubresourceCount, false);
	for (uint32 SubresourceIndex = 0; SubresourceIndex < SubresourceCount; ++SubresourceIndex)
	{
		SubresourceArray[SubresourceIndex] = Element;
	}
}

template <typename ElementType, typename AllocatorType>
FORCEINLINE bool IsWholeResource(const TRDGTextureSubresourceArray<ElementType, AllocatorType>& SubresourceArray)
{
	checkf(SubresourceArray.Num() > 0, TEXT("IsWholeResource is only valid on initialized arrays."));
	return SubresourceArray.Num() == 1;
}

template <typename ElementType, typename AllocatorType>
FORCEINLINE bool IsSubresources(const TRDGTextureSubresourceArray<ElementType, AllocatorType>& SubresourceArray)
{
	checkf(SubresourceArray.Num() > 0, TEXT("IsSubresources is only valid on initialized arrays."));
	return SubresourceArray.Num() > 1;
}

template <typename ElementType, typename AllocatorType>
FORCEINLINE const ElementType& GetWholeResource(const TRDGTextureSubresourceArray<ElementType, AllocatorType>& SubresourceArray)
{
	check(IsWholeResource(SubresourceArray));
	return SubresourceArray[0];
}

template <typename ElementType, typename AllocatorType>
FORCEINLINE ElementType& GetWholeResource(TRDGTextureSubresourceArray<ElementType, AllocatorType>& SubresourceArray)
{
	checkf(IsWholeResource(SubresourceArray), TEXT("GetWholeResource with a may only be called an array initialized with InitAsWholeResource."));
	return SubresourceArray[0];
}

template <typename ElementType, typename AllocatorType>
FORCEINLINE const ElementType& GetSubresource(const TRDGTextureSubresourceArray<ElementType, AllocatorType>& SubresourceArray, const FRDGTextureSubresourceLayout& Layout, FRDGTextureSubresource Subresource)
{
	VerifyLayout(SubresourceArray, Layout);
	checkf(IsSubresources(SubresourceArray), TEXT("GetSubresource with a may only be called an array initialized InitAsSubresources."));
	return SubresourceArray[Layout.GetSubresourceIndex(Subresource)];
}

template <typename ElementType, typename AllocatorType>
FORCEINLINE ElementType& GetSubresource(TRDGTextureSubresourceArray<ElementType, AllocatorType>& SubresourceArray, const FRDGTextureSubresourceLayout& Layout, FRDGTextureSubresource Subresource)
{
	VerifyLayout(SubresourceArray, Layout);
	checkf(IsSubresources(SubresourceArray), TEXT("GetSubresource with a may only be called an array initialized InitAsSubresources."));
	return SubresourceArray[Layout.GetSubresourceIndex(Subresource)];
}

template <typename ElementType, typename AllocatorType, typename FunctionType>
inline void EnumerateSubresourceRange(TRDGTextureSubresourceArray<ElementType, AllocatorType>& SubresourceArray, const FRDGTextureSubresourceLayout& Layout, const FRDGTextureSubresourceRange& Range, FunctionType Function)
{
	VerifyLayout(SubresourceArray, Layout);
	checkf(IsSubresources(SubresourceArray), TEXT("EnumerateSubresources with a range may only be called an array initialized as subresources."));
	Range.EnumerateSubresources([&](FRDGTextureSubresource Subresource)
	{
		Function(GetSubresource(SubresourceArray, Layout, Subresource));
	});
}

template <typename ElementType, typename AllocatorType, typename FunctionType>
inline void EnumerateSubresourceRange(const TRDGTextureSubresourceArray<ElementType, AllocatorType>& SubresourceArray, const FRDGTextureSubresourceLayout& Layout, const FRDGTextureSubresourceRange& Range, FunctionType Function)
{
	VerifyLayout(SubresourceArray, Layout);
	checkf(IsSubresources(SubresourceArray), TEXT("EnumerateSubresources with a range may only be called an array initialized as subresources."));
	Range.EnumerateSubresources([&](FRDGTextureSubresource Subresource)
	{
		Function(GetSubresource(SubresourceArray, Layout, Subresource));
	});
}