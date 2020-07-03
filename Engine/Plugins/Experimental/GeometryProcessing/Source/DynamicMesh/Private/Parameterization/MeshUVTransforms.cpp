// Copyright Epic Games, Inc. All Rights Reserved.


#include "Parameterization/MeshUVTransforms.h"


void UE::MeshUVTransforms::RecenterScale(FDynamicMeshUVOverlay* UVOverlay, const TArray<int32>& UVElementIDs,
	EIslandPositionType NewPosition, double UVScale)
{
	FAxisAlignedBox2d UVBounds(FAxisAlignedBox2d::Empty());
	FVector2d InitialTranslation = FVector2d::Zero();
	if (NewPosition != EIslandPositionType::CurrentPosition)
	{
		for (int32 elemid : UVElementIDs)
		{
			FVector2d UV = (FVector2d)UVOverlay->GetElement(elemid);
			UVBounds.Contain(UV);
		}

		if (NewPosition == EIslandPositionType::MinBoxCornerToOrigin)
		{
			InitialTranslation = -UVBounds.Min;
		}
		else if (NewPosition == EIslandPositionType::CenterToOrigin)
		{
			InitialTranslation = -UVBounds.Center();
		}
	}

	for (int32 elemid : UVElementIDs)
	{
		FVector2d UV = (FVector2d)UVOverlay->GetElement(elemid);
		FVector2d NewUV = (UV + InitialTranslation) * UVScale;
		UVOverlay->SetElement(elemid, (FVector2f)NewUV);
	}
}





template<typename EnumerableType>
void FitToBox_Internal(FDynamicMeshUVOverlay* UVOverlay, EnumerableType UVElementIDs, const FAxisAlignedBox2d& TargetBox, bool bUniformScale)
{
	FAxisAlignedBox2d UVBounds(FAxisAlignedBox2d::Empty());
	for (int32 elemid : UVElementIDs)
	{
		FVector2d UV = (FVector2d)UVOverlay->GetElement(elemid);
		UVBounds.Contain(UV);
	}

	FVector2d CurCenter = UVBounds.Center();
	FVector2d TargetCenter = TargetBox.Center();

	double ScaleX = TargetBox.Width() / UVBounds.Width();
	double ScaleY = TargetBox.Height() / UVBounds.Height();
	if (bUniformScale)
	{
		double RatioX = (ScaleX < 1) ? (1.0 / ScaleX) : ScaleX;
		double RatioY = (ScaleY < 1) ? (1.0 / ScaleY) : ScaleY;
		if (RatioY > RatioX)
		{
			ScaleX = ScaleY;
		}
		else
		{
			ScaleY = ScaleX;
		}
	}


	for (int32 elemid : UVElementIDs)
	{
		FVector2d UV = (FVector2d)UVOverlay->GetElement(elemid);
		double NewX = (UV.X - CurCenter.X) * ScaleX + TargetCenter.X;
		double NewY = (UV.Y - CurCenter.Y) * ScaleY + TargetCenter.Y;
		UVOverlay->SetElement(elemid, FVector2f((float)NewX, (float)NewY));
	}
}



void UE::MeshUVTransforms::FitToBox(FDynamicMeshUVOverlay* UVOverlay, const TArray<int32>& UVElementIDs, const FAxisAlignedBox2d& TargetBox, bool bUniformScale)
{
	FitToBox_Internal(UVOverlay, UVElementIDs, TargetBox, bUniformScale);
}



void UE::MeshUVTransforms::FitToBox(FDynamicMeshUVOverlay* UVOverlay, const FAxisAlignedBox2d& Box, bool bUniformScale)
{
	FitToBox_Internal(UVOverlay, UVOverlay->ElementIndicesItr(), Box, bUniformScale);
}
