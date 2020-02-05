// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Text3DPrivate.h"
#include "Templates/SharedPointer.h"
#include "Math/Vector2D.h"
#include "Util.h"


class FGlyphLoader final
{
public:
	FGlyphLoader(const FT_GlyphSlot Glyph);

	TSharedPtr<class FContourList> GetContourList() const;


private:
	struct FContourNode;

	using FNodePtr = TSharedPtr<FContourNode>;

	class FLine final
	{
	public:
		FLine(const FVector2D PositionIn);
		void Add(FGlyphLoader* const Loader);

	private:
		const FVector2D Position;
	};

	class FCurve
	{
	public:
		FCurve(const bool bLineIn);
		virtual ~FCurve();

		void Add(FGlyphLoader* const LoaderIn);

	protected:
		struct FPointData;


		/**
		 * Check if 3 points are on one line.
		 * @param A - Point A.
		 * @param B - Point B.
		 * @param C - Point C.
		 * @return Are points on one line?
		 */
		static bool OnOneLine(const FVector2D A, const FVector2D B, const FVector2D C);


		/** Is curve a line? */
		const bool bLine;
		FGlyphLoader* Loader;

		const float StartT;
		const float EndT;

	private:
		int32 Depth;
		int32 MaxDepth;

		FPartConstPtr First;
		FPartConstPtr Last;

		/** Needed to make additional splits near start and end of curve */
		bool bFirstSplit;
		bool bLastSplit;


		/**
		 * Compute max depth (depends on curve length, step is fixed).
		 */
		void ComputeMaxDepth();
		void Split(const FPointData& Start, const FPointData& End);
		void CheckPart(const FPointData& Start, const FPointData& End);
		virtual void UpdateTangent(FPointData* const Middle);

		virtual FVector2D Position(const float T) const = 0;
		virtual FVector2D Tangent(const float T) = 0;
	};

	class FQuadraticCurve final : public FCurve
	{
	public:
		FQuadraticCurve(const FVector2D A, const FVector2D B, const FVector2D C);

	private:
		const FVector2D E;
		const FVector2D F;
		const FVector2D G;


		FVector2D Position(const float T) const override;
		FVector2D Tangent(const float T) override;
	};

	class FCubicCurve final : public FCurve
	{
	public:
		FCubicCurve(const FVector2D A, const FVector2D B, const FVector2D C, const FVector2D D);

	private:
		const FVector2D E;
		const FVector2D F;
		const FVector2D G;
		const FVector2D H;

		/** Sharp means that curve derivative has zero length, it's actually sharp only in middle case */
		bool bSharpStart;
		bool bSharpMiddle;
		bool bSharpEnd;


		void UpdateTangent(FPointData* const Middle) override;

		FVector2D Position(const float t) const override;
		FVector2D Tangent(const float t) override;
	};


	TSharedPtr<class FContourList> Contours;
	int32 EndIndex;
	class FContour* Contour;
	/** Initial parity */
	TMap<const class FContour*, bool> Clockwise;

	FVector2D FirstPosition;
	FPartPtr LastPoint;


	bool CreateContour(const FT_Outline Outline, const int32 ContourIndex);
	void ComputeInitialParity();
	/**
	 * Insert NodeA inside NodeB.
	 * @param NodeA
	 * @param NodeB
	 */
	void Insert(const FNodePtr NodeA, const FNodePtr NodeB);
	/**
	 * Reverse contour if it's initial parity differs from the one it should have.
	 * @param Node - Function is called recursively to fix all contours inside Node->Contour.
	 * @param ClockwiseIn - The parity that contours listed in Node->Nodes should have.
	 */
	void FixParity(const FNodePtr Node, const bool bClockwiseIn);

	bool ProcessFreetypeOutline(const FT_Outline Outline, const int32 ContourIndex);
	bool ComputeNormals();

	void RemoveContour();
	FPartPtr AddPoint(const FVector2D Position);
	void JoinWithLast(const FPartPtr Point);
	/**
	 * Check if ContourA is inside ContourB.
	 * @param ContourA
	 * @param ContourB
	 * @return Is ContourA inside ContourB?
	 */
	bool Inside(const class FContour* const ContourA, const class FContour* const ContourB);
};
