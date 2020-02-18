// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util.h"


class FContour final : public FContourBase
{
public:
	struct FPathEntry
	{
		int32 Prev;
		int32 Next;
	};


	FContour();
	~FContour();

	int32 GetPrev(const int32 Index) const;
	int32 GetNext(const int32 Index) const;

private:

	/**
	 * Compute expand value at which point's normal will intersect edge (with same expansion). Value is stored in Point->AvailableExpandsFar[Edge].
	 * @param Point - Point for which available expand should be computed.
	 * @param Edge - Edge for which available expand should be computed.
	 */
	void ComputeAvailableExpandFar(const FPartPtr Point, const FPartConstPtr Edge);
};
