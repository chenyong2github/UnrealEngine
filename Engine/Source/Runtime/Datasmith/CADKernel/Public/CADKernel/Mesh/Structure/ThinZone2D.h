// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Chrono.h"
#include "CADKernel/Core/HaveStates.h"
//#include "CADKernel/Core/Factory.h"
#include "CADKernel/Core/Types.h"
#include "CADKernel/Math/Point.h"

#include "CADKernel/Mesh/MeshEnum.h"
#include "CADKernel/Mesh/Structure/EdgeSegment.h"

namespace UE::CADKernel
{
	enum class EThinZone2DType : uint8
	{
		Undefined = 0,
		Global,			// a Surface globally thin
		PeakStart,		// an extremity of a Surface that is fine
		PeakEnd,		// an extremity of a Surface that is fine
		Butterfly,		// the outer loop that is like a bow tie (Butterfly)
		BetweenLoops,   // a bow tie between two different loops
		TooSmall        // -> to delete
	};

	class FThinZone2D;

	class FThinZoneSide : public FHaveStates
	{
		friend FThinZone2D;

	private:
		TArray<FEdgeSegment> Segments;
		FThinZoneSide& FrontSide;

		double SideLength;
		double MediumThickness;
		double MaxThickness;

	public:

		FThinZoneSide(FThinZoneSide* InFrontSide, const TArray<FEdgeSegment*>& InSegments);
		virtual ~FThinZoneSide() = default;

		void Empty()
		{
			Segments.Empty();
		}

		const FEdgeSegment& GetFirst() const
		{
			return Segments[0];
		}

		const FEdgeSegment& GetLast() const
		{
			return Segments.Last();
		}

		void SetEdgesAsThinZone();

		const TArray<FEdgeSegment>& GetSegments() const
		{
			return Segments;
		}

		TArray<FEdgeSegment>& GetSegments()
		{
			return Segments;
		}

		EMeshingState GetMeshingState() const;

		double Length() const
		{
			return SideLength;
		}

		double GetThickness() const
		{
			return MediumThickness;
		}

		double GetMaxThickness() const
		{
			return MaxThickness;
		}

		bool IsInner() const
		{
			if (Segments.Num() == 0)
			{
				return true;
			}
			return Segments[0].IsInner();
		}

	private:
		void ComputeThicknessAndLength();

	};

	class FThinZone2D : public FHaveStates
	{
	private:

		FThinZoneSide FirstSide;
		FThinZoneSide SecondSide;

		EThinZone2DType Category;

		double Thickness;
		double MaxThickness;

	public:

		/**
		 * FThinZone2D::FThinZoneSides are maded with a copy of TArray<FEdgeSegment*> into TArray<FEdgeSegment> to break the link with TFactory<FEdgeSegment> of FThinZone2DFinder
		 * So FThinZone2D can be transfered into the FTopologicalFace 
		 */
		FThinZone2D(const TArray<FEdgeSegment*>& InFirstSideSegments, const TArray<FEdgeSegment*>& InSecondSideSegments)
			: FirstSide(&SecondSide, InFirstSideSegments)
			, SecondSide(&FirstSide, InSecondSideSegments)
			, Category(EThinZone2DType::Undefined)
		{
			Finalize();
 		}

		virtual ~FThinZone2D() = default;

		void Empty()
		{
			FirstSide.Empty();
			SecondSide.Empty();
			Thickness = -1;
			SetRemoved();
		}

		double GetThickness() const
		{
			return Thickness;
		};

		double GetMaxThickness() const
		{
			return MaxThickness;
		};

		FThinZoneSide& GetFirstSide()
		{
			return FirstSide;
		}

		FThinZoneSide& GetSecondSide()
		{
			return SecondSide;
		}

		const FThinZoneSide& GetFirstSide() const
		{
			return FirstSide;
		}

		const FThinZoneSide& GetSecondSide() const 
		{
			return SecondSide;
		}

		EThinZone2DType GetCategory() const
		{
			return Category;
		}

		void SetEdgesAsThinZone();
		static void SetPeakEdgesMarker(const TArray<const FTopologicalEdge*>&);

		double Length() const
		{
			return FirstSide.Length() + SecondSide.Length();
		}

		double GetMaxSideLength() const
		{
			return FMath::Max(FirstSide.Length(), SecondSide.Length());
		}

		bool IsRemoved() const
		{
			return ((States & EHaveStates::IsRemoved) == EHaveStates::IsRemoved);
		}

		void SetRemoved() const
		{
			States |= EHaveStates::IsRemoved;
		}

		void ResetRemoved() const
		{
			States &= ~EHaveStates::IsRemoved;
		}

		void SetCategory(EThinZone2DType InType)
		{
			Category = InType;
		}

#ifdef CADKERNEL_DEV
		void Display(const FString& Title, EVisuProperty VisuProperty) const;
#endif

	private:
		void Finalize();
	};

}
