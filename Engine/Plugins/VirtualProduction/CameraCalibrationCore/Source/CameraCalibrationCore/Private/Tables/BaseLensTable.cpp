// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tables/BaseLensTable.h"
#include "LensFile.h"

FName FBaseLensTable::GetFriendlyPointName(ELensDataCategory InCategory)
{
	switch (InCategory)
	{
		case ELensDataCategory::Zoom: return TEXT("Focal Length Point");
		case ELensDataCategory::Distortion: return TEXT("Distorsion Point");
		case ELensDataCategory::ImageCenter: return TEXT("Image Center Point");
		case ELensDataCategory::STMap: return TEXT("ST Map Point");
		case ELensDataCategory::NodalOffset: return TEXT("Nodal Offset Point");
	}

	return TEXT("");
}

void FBaseLensTable::ForEachFocusPoint(FFocusPointCallback InCallback, const float InFocus,float InputTolerance) const
{
	ForEachPoint([this, InCallback, InFocus, InputTolerance](const FBaseFocusPoint& InFocusPoint)
	{
		if (!FMath::IsNearlyEqual(InFocusPoint.GetFocus(), InFocus, InputTolerance))
		{
			return;
		}

		InCallback(InFocusPoint);
	});
}

void FBaseLensTable::ForEachLinkedFocusPoint(FLinkedFocusPointCallback InCallback, const float InFocus, float InputTolerance) const
{
	if (!ensure(LensFile.IsValid()))
	{
		return;
	}
	
	const TMap<ELensDataCategory, FLinkPointMetadata> LinkedCategories = GetLinkedCategories();
	for (const TPair<ELensDataCategory, FLinkPointMetadata>& LinkedCategoryPair : LinkedCategories)
	{
		const FBaseLensTable& LinkDataTable = LensFile->GetDataTable(LinkedCategoryPair.Key);
		LinkDataTable.ForEachPoint([this, InCallback, InFocus, InputTolerance, LinkedCategoryPair](const FBaseFocusPoint& InFocusPoint)
		{
			if (!FMath::IsNearlyEqual(InFocusPoint.GetFocus(), InFocus, InputTolerance))
			{
				return;
			}

			InCallback(InFocusPoint, LinkedCategoryPair.Key, LinkedCategoryPair.Value);
		});
	}
}

bool FBaseLensTable::HasLinkedFocusValues(const float InFocus, float InputTolerance) const
{
	if (!ensure(LensFile.IsValid()))
	{
		return false;
	}
	
	const TMap<ELensDataCategory, FLinkPointMetadata> LinkedCategories = GetLinkedCategories();
	for (const TPair<ELensDataCategory, FLinkPointMetadata>& LinkedCategoryPair : LinkedCategories)
	{
		const FBaseLensTable& LinkDataTable = LensFile->GetDataTable(LinkedCategoryPair.Key);
		if (LinkDataTable.DoesFocusPointExists(InFocus))
		{
			return true;
		}
	}

	return false;
}

bool FBaseLensTable::HasLinkedZoomValues(const float InFocus, const float InZoomPoint, float InputTolerance) const
{
	if (!ensure(LensFile.IsValid()))
	{
		return false;
	}
	
	const TMap<ELensDataCategory, FLinkPointMetadata> LinkedCategories = GetLinkedCategories();
	for (const TPair<ELensDataCategory, FLinkPointMetadata>& LinkedCategoryPair : LinkedCategories)
	{
		const FBaseLensTable& LinkDataTable = LensFile->GetDataTable(LinkedCategoryPair.Key);
		if (LinkDataTable.DoesZoomPointExists(InFocus, InZoomPoint, InputTolerance))
		{
			return true;
		}
	}

	return false;
}

