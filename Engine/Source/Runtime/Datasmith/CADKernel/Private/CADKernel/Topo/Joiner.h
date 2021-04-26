// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"

namespace CADKernel
{
	class FTopologicalFace;
	class FTopologicalVertex;

	class FJoiner
	{
		friend class FThinZone;
		friend class FThinZoneFinder;

	protected:

		TArray<TSharedPtr<FTopologicalFace>>& Surfaces;
		TArray<TSharedPtr<FTopologicalVertex>> Vertices;

		double JoiningTolerance;
		double JoiningToleranceSqr;

	public:

		FJoiner(TArray<TSharedPtr<FTopologicalFace>>& surfaces, double Tolerance);

		void JoinSurfaces();
		void JoinSurfaces(bool bProcessOnlyBorderEdges, bool bProcessOnlyNonManifoldEdges);

		void FixCollapsedEdges();

	private:

		void GetVertices();

		void JoinVertices();
		void JoinBorderVertices(TArray<TSharedPtr<FTopologicalVertex>>& MergedVertices);
		void CheckSelfConnectedEdge();

		void JoinEdges(TArray<TSharedPtr<FTopologicalVertex>>& Vertices);
		void MergeUnconnectedAdjacentEdges(TArray<TSharedPtr<FTopologicalVertex>>& Vertices, bool bOnlyIfSameCurve);
	};

} // namespace CADKernel
