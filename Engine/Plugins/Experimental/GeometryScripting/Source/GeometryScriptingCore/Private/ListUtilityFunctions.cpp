// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/ListUtilityFunctions.h"


#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_ListUtilityFunctions"


int UGeometryScriptLibrary_ListUtilityFunctions::GetIndexListLength(FGeometryScriptIndexList IndexList)
{
	return (IndexList.List.IsValid()) ? IndexList.List->Num() : 0;
}

int UGeometryScriptLibrary_ListUtilityFunctions::GetIndexListLastIndex(FGeometryScriptIndexList IndexList)
{
	return (IndexList.List.IsValid()) ? FMath::Max(IndexList.List->Num()-1,0) : 0;
}

int UGeometryScriptLibrary_ListUtilityFunctions::GetIndexListItem(FGeometryScriptIndexList IndexList, int Index, bool& bIsValidIndex)
{
	bIsValidIndex = false;
	if (IndexList.List.IsValid() && Index >= 0 && Index < IndexList.List->Num())
	{
		bIsValidIndex = true;
		return (*IndexList.List)[Index];
	}
	return -1;
}

void UGeometryScriptLibrary_ListUtilityFunctions::ConvertIndexListToArray(FGeometryScriptIndexList IndexList, TArray<int>& IndexArray)
{
	IndexArray.Reset();
	if (IndexList.List.IsValid())
	{
		IndexArray.Append(*IndexList.List);
	}
}

void UGeometryScriptLibrary_ListUtilityFunctions::ConvertArrayToIndexList(const TArray<int>& IndexArray, FGeometryScriptIndexList& IndexList, EGeometryScriptIndexType IndexType)
{
	IndexList.Reset(IndexType);
	IndexList.List->Append(IndexArray);
}





int UGeometryScriptLibrary_ListUtilityFunctions::GetTriangleListLength(FGeometryScriptTriangleList TriangleList)
{
	return (TriangleList.List.IsValid()) ? TriangleList.List->Num() : 0;
}

int UGeometryScriptLibrary_ListUtilityFunctions::GetTriangleListLastTriangle(FGeometryScriptTriangleList TriangleList)
{
	return (TriangleList.List.IsValid()) ? FMath::Max(TriangleList.List->Num()-1,0) : 0;
}

FIntVector UGeometryScriptLibrary_ListUtilityFunctions::GetTriangleListItem(FGeometryScriptTriangleList TriangleList, int Triangle, bool& bIsValidTriangle)
{
	bIsValidTriangle = false;
	if (TriangleList.List.IsValid() && Triangle >= 0 && Triangle < TriangleList.List->Num())
	{
		bIsValidTriangle = true;
		return (*TriangleList.List)[Triangle];
	}
	return FIntVector::NoneValue;
}

void UGeometryScriptLibrary_ListUtilityFunctions::ConvertTriangleListToArray(FGeometryScriptTriangleList TriangleList, TArray<FIntVector>& TriangleArray)
{
	TriangleArray.Reset();
	if (TriangleList.List.IsValid())
	{
		TriangleArray.Append(*TriangleList.List);
	}
}

void UGeometryScriptLibrary_ListUtilityFunctions::ConvertArrayToTriangleList(const TArray<FIntVector>& TriangleArray, FGeometryScriptTriangleList& TriangleList)
{
	TriangleList.Reset();
	TriangleList.List->Append(TriangleArray);
}





int UGeometryScriptLibrary_ListUtilityFunctions::GetVectorListLength(FGeometryScriptVectorList VectorList)
{
	return (VectorList.List.IsValid()) ? VectorList.List->Num() : 0;
}

int UGeometryScriptLibrary_ListUtilityFunctions::GetVectorListLastIndex(FGeometryScriptVectorList VectorList)
{
	return (VectorList.List.IsValid()) ? FMath::Max(VectorList.List->Num()-1,0) : 0;
}

FVector UGeometryScriptLibrary_ListUtilityFunctions::GetVectorListItem(FGeometryScriptVectorList VectorList, int Index, bool& bIsValidIndex)
{
	bIsValidIndex = false;
	if (VectorList.List.IsValid() && Index >= 0 && Index < VectorList.List->Num())
	{
		bIsValidIndex = true;
		return (*VectorList.List)[Index];
	}
	return FVector::Zero();
}

void UGeometryScriptLibrary_ListUtilityFunctions::ConvertVectorListToArray(FGeometryScriptVectorList VectorList, TArray<FVector>& VectorArray)
{
	VectorArray.Reset();
	if (VectorList.List.IsValid())
	{
		VectorArray.Append(*VectorList.List);
	}
}

void UGeometryScriptLibrary_ListUtilityFunctions::ConvertArrayToVectorList(const TArray<FVector>& VectorArray, FGeometryScriptVectorList& VectorList)
{
	VectorList.Reset();
	VectorList.List->Append(VectorArray);
}





int UGeometryScriptLibrary_ListUtilityFunctions::GetUVListLength(FGeometryScriptUVList UVList)
{
	return (UVList.List.IsValid()) ? UVList.List->Num() : 0;
}

int UGeometryScriptLibrary_ListUtilityFunctions::GetUVListLastIndex(FGeometryScriptUVList UVList)
{
	return (UVList.List.IsValid()) ? FMath::Max(UVList.List->Num()-1,0) : 0;
}

FVector2D UGeometryScriptLibrary_ListUtilityFunctions::GetUVListItem(FGeometryScriptUVList UVList, int Index, bool& bIsValidIndex)
{
	bIsValidIndex = false;
	if (UVList.List.IsValid() && Index >= 0 && Index < UVList.List->Num())
	{
		bIsValidIndex = true;
		return (*UVList.List)[Index];
	}
	return FVector2D::ZeroVector;
}

void UGeometryScriptLibrary_ListUtilityFunctions::ConvertUVListToArray(FGeometryScriptUVList UVList, TArray<FVector2D>& UVArray)
{
	UVArray.Reset();
	if (UVList.List.IsValid())
	{
		UVArray.Append(*UVList.List);
	}
}

void UGeometryScriptLibrary_ListUtilityFunctions::ConvertArrayToUVList(const TArray<FVector2D>& UVArray, FGeometryScriptUVList& UVList)
{
	UVList.Reset();
	UVList.List->Append(UVArray);
}





int UGeometryScriptLibrary_ListUtilityFunctions::GetColorListLength(FGeometryScriptColorList ColorList)
{
	return (ColorList.List.IsValid()) ? ColorList.List->Num() : 0;
}

int UGeometryScriptLibrary_ListUtilityFunctions::GetColorListLastIndex(FGeometryScriptColorList ColorList)
{
	return (ColorList.List.IsValid()) ? FMath::Max(ColorList.List->Num()-1,0) : 0;
}

FLinearColor UGeometryScriptLibrary_ListUtilityFunctions::GetColorListItem(FGeometryScriptColorList ColorList, int Index, bool& bIsValidIndex)
{
	bIsValidIndex = false;
	if (ColorList.List.IsValid() && Index >= 0 && Index < ColorList.List->Num())
	{
		bIsValidIndex = true;
		return (*ColorList.List)[Index];
	}
	return FLinearColor::White;
}

void UGeometryScriptLibrary_ListUtilityFunctions::ConvertColorListToArray(FGeometryScriptColorList ColorList, TArray<FLinearColor>& ColorArray)
{
	ColorArray.Reset();
	if (ColorList.List.IsValid())
	{
		ColorArray.Append(*ColorList.List);
	}
}

void UGeometryScriptLibrary_ListUtilityFunctions::ConvertArrayToColorList(const TArray<FLinearColor>& ColorArray, FGeometryScriptColorList& ColorList)
{
	ColorList.Reset();
	ColorList.List->Append(ColorArray);
}




#undef LOCTEXT_NAMESPACE