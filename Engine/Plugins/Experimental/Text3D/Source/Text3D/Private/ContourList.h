// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Text3DPrivate.h"
#include "Contour.h"
#include "Containers/List.h"


class FData;

class FContourList final : public TDoubleLinkedList<FContour>
{
public:
	FContourList(const FT_GlyphSlot Glyph, const TSharedPtr<FData> DataIn);

	/**
	 * Create contour.
	 * @return Reference to created contour.
	 */
	FContour& Add();
	/**
	 * Remove contour.
	 * @param Contour - Const reference to contour that should be removed.
	 */
	void Remove(const FContour& Contour);

	void Reset();

private:
	const TSharedPtr<FData> Data;


	void Init();
};
