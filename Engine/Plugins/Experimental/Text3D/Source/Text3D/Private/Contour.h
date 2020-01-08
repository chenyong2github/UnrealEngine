// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util.h"

struct FPart;
class FData;

class FContour final : public FContourBase
{
public:
	struct FPathEntry
	{
		int32 Prev;
		int32 Next;
	};


	FContour(const TSharedPtr<FData> DataIn);
	~FContour();

	int32 GetPrev(const int32 Index) const;
	int32 GetNext(const int32 Index) const;

	bool HasIntersections() const;
	void ResetContour();
	void DisableIntersections();

	/**
	 * Compute expand value at which point's normal will intersect next point's normal. Result is written to FPart::AvailableExpandNear.
	 * @param Point - Point for which available expand should be computed.
	 */
	void ComputeAvailableExpandNear(FPart* const Point);

	/**
	 * Call FContour::ComputeAvailableExpandFar for given point and all edges.
	 * @param Point - Point for which available expand should be computed.
	 */
	void ComputeAvailableExpandsFarFrom(FPart* const Point);
	/**
	 * Call FContour::ComputeAvailableExpandFar for all points and given edge.
	 * @param Edge - Edge for which available expand should be computed.
	 */
	void ComputeAvailableExpandsFarTo(const FPart* const Edge);

	/**
	 * Remove multiple parts of contour.
	 * @param Start - Start part, included in removal.
	 * @param End - End part, not included in removal.
	 */
	void RemoveRange(const FPart* const Start, const FPart* const End);

private:
	const TSharedPtr<FData> Data;
	bool bHasIntersections;


	/**
	 * Compute expand value at which point's normal will intersect edge (with same expansion). Value is stored in Point->AvailableExpandsFar[Edge].
	 * @param Point - Point for which available expand should be computed.
	 * @param Edge - Edge for which available expand should be computed.
	 */
	void ComputeAvailableExpandFar(FPart* const Point, const FPart* const Edge);
};
