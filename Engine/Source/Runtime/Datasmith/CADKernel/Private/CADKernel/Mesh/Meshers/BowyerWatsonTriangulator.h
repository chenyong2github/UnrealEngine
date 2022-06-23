// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"
#include "CADKernel/Math/SlopeUtils.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IsoCell.h"
#include "CADKernel/UI/Display.h"

#ifdef CADKERNEL_DEV
#include "CADKernel/Mesh/Meshers/IsoTriangulator/DefineForDebug.h"
#endif

//#define DEBUG_BOWYERWATSON
namespace CADKernel
{



class FBowyerWatsonTriangulator
{
protected:
	struct FTriangle
	{
		int32 VertexIndices[3];
		double SquareRadius;
		FPoint Center;

		FTriangle(const int32& Index0, const int32& Index1, const int32& Index2, const TArray<TPair<int32, FPoint2D>>& InVertices)
		{
			Set(Index0, Index1, Index2, InVertices);
		}

		void Set(const int32& Index0, const int32& Index1, const int32& Index2, const TArray<TPair<int32, FPoint2D>>& InVertices)
		{
			VertexIndices[0] = Index0;
			VertexIndices[1] = Index1;
			VertexIndices[2] = Index2;
			FTriangle2D Triangle(InVertices[Index0].Value, InVertices[Index1].Value, InVertices[Index2].Value);
			Center = Triangle.CircumCircleCenterWithSquareRadius(SquareRadius);
		}
	};


public:

	/**
	 * @param Vertices the 2d point cloud to mesh
	 * @param OutEdgeVertices, the edges of the mesh. An edge is defined by the indices of its vertices
	 * So the ith edge is defined by the vertices EdgeVertexIndices[2 * ith] and EdgeVertexIndices[2 * ith + 1]
	 */
	FBowyerWatsonTriangulator(TArray<TPair<int32, FPoint2D>>& InVertices, TArray<int32>& OutEdgeVertices)
		: VerticesCount(InVertices.Num())
		, Vertices(InVertices)
		, EdgeVertexIndices(OutEdgeVertices)
	{
		Init();
	}

	void Triangulate()
	{
		FTimePoint StartTime = FChrono::Now();

		Vertices.Sort([](const TPair<int32, FPoint2D>& Vertex1, const TPair<int32, FPoint2D>& Vertex2) {return (Vertex1.Value.U + Vertex1.Value.V) > (Vertex2.Value.U + Vertex2.Value.V); });

		// initialization of Bowyer & Watson algorithm with a bounding mesh of the vertex cloud 
		// i.e. 2 triangles defined by the corners of the offset vertices bounding box
		MakeBoundingMesh();

		// insert each point in the mesh
		// The points are sorted on the diagonal of the bbox and are inserted from one end to the other  
		int32 VertexIndex = 0;
		for (int32 VIndex = 0; VIndex < VerticesCount; ++VIndex)
		{
			if (VIndex % 2 == 0)
			{
				VertexIndex = VIndex / 2;
			}
			else
			{
				VertexIndex = VerticesCount - 1 - VIndex / 2;
			}

			TriangleIndices.Empty(VerticesCount);
			AdditionalTriangleIndices.Empty(VerticesCount);

			const FPoint2D& NewVertex = Vertices[VertexIndex].Value;

			bool bHasAdditionalTriangles = false;
			// find all triangles whose circumcircles contain the new vertex
			{
				// The problem is for triangle with huge circumscribed circle radius (flat triangle)
				// in this case, if distance between the new vertex and the circumscribed circle center nearly equals its radius
				//  - So the idea is to check that the new vertex is not outside the triangle and could generate a flatten triangle 
				//    To check this, the slop between each side is evaluated (ComputeUnorientedSlope(NewVertex, Triangle.Vertex[a], Triangle.Vertex[b]))
				//    if the slop is nearly null this mean that the new vertex is outside the triangle and will generate a flat triangle

				constexpr double IncreaseFactor = 1.001; // to process the case of new vertex on the circumscribed circle
				constexpr double ReducingFactor = 0.9999 / IncreaseFactor; // to remove the case of new vertex on the circumscribed circle 
				// the case of 4 points nearly on the same circle is manage in a second time i.e. we check that the 

				for (int32 TriangleIndex = 0; TriangleIndex < TriangleSet.Num(); TriangleIndex++)
				{
					const FPoint2D Center = TriangleSet[TriangleIndex].Center;

					const double SquareDistanceToCenter = Center.SquareDistance(NewVertex);

					const double SquareRadiusMax = TriangleSet[TriangleIndex].SquareRadius * IncreaseFactor;
					const double SquareRadiusMin = SquareRadiusMax * ReducingFactor;

					if (SquareDistanceToCenter < SquareRadiusMin)
					{
						TriangleIndices.Add(TriangleIndex);
					}
					else
					{
						if (SquareDistanceToCenter < SquareRadiusMax)
						{
							bHasAdditionalTriangles = true;
							AdditionalTriangleIndices.Add(TriangleIndex);
						}
					}
				}
			}

			if (TriangleIndices.Num() == 0)
			{
				for (int32& TriangleIndex : AdditionalTriangleIndices)
				{
					const FPoint2D& Point0 = Vertices[TriangleSet[TriangleIndex].VertexIndices[0]].Value;
					const FPoint2D& Point1 = Vertices[TriangleSet[TriangleIndex].VertexIndices[1]].Value;
					const FPoint2D& Point2 = Vertices[TriangleSet[TriangleIndex].VertexIndices[2]].Value;
					const double Slop0 = ComputePositiveSlope(Point0, Point1, NewVertex);
					const double Slop1 = ComputePositiveSlope(Point1, Point2, NewVertex);
					const double Slop2 = ComputePositiveSlope(Point2, Point0, NewVertex);
					if (Slop0 < 4 && Slop1 < 4 && Slop2 < 4)
					{
						TriangleIndices.Add(TriangleIndex);
						TriangleIndex = -1;
					}
				}
			}

			if (bHasAdditionalTriangles)
			{

				TArray<int32> TmpVertexIndices;
				for (int32 TriangleIndex : TriangleIndices)
				{
					for (int32 Index = 0; Index < 3; Index++)
					{
						const int32 StartVertex = TriangleSet[TriangleIndex].VertexIndices[Index];
						TmpVertexIndices.AddUnique(StartVertex);
					}
				}

				TArray<TPair<int32, double>> VertexToSlop;
				for (int32 Index : TmpVertexIndices)
				{
					FPoint2D Vertex = Vertices[Index].Value;
					double Slop = ComputeSlope(NewVertex, Vertex);
					VertexToSlop.Emplace(Index, Slop);
				}

				VertexToSlop.Sort([](const TPair<int32, double>& Vertex1, const TPair<int32, double>& Vertex2) {return Vertex1.Value < Vertex2.Value; });

				//Wait();
				bool bTriangleHasBeenAdded = true;
				while (bTriangleHasBeenAdded)
				{
					bTriangleHasBeenAdded = false;
					for (int32& TriangleIndex : AdditionalTriangleIndices)
					{
						if (TriangleIndex < 0)
						{
							continue;
						}

						int32 CandidateVertexIndex = -1;
						for (int32 Index = 0; Index < 3; Index++)
						{
							int32 Candidate = TriangleSet[TriangleIndex].VertexIndices[Index];
							if (TmpVertexIndices.Find(Candidate) == INDEX_NONE)
							{
								if (CandidateVertexIndex == -1)
								{
									CandidateVertexIndex = Candidate;
								}
								else
								{
									CandidateVertexIndex = -1;
									break;
								}
							}
						}

						if (CandidateVertexIndex == -1)
						{
							continue;
						}

						FPoint2D CandidateVertexPoint = Vertices[CandidateVertexIndex].Value;

						int32 StartIndex = -1;
						int32 EndIndex = 0;
						{
							double Slope = ComputeSlope(NewVertex, CandidateVertexPoint);
							for (int32 AIndex = 0; AIndex < VertexToSlop.Num(); ++AIndex)
							{
								constexpr double SmallSlop = 0.01; // 0.5 deg
								if (FMath::IsNearlyEqual(Slope, VertexToSlop[AIndex].Value, SmallSlop))
								{
									EndIndex = -1;
									break;
								}

								if (Slope < VertexToSlop[AIndex].Value)
								{
									EndIndex = AIndex;
									break;
								}
							}
							if (EndIndex < 0)
							{
								continue;
							}
							StartIndex = EndIndex == 0 ? VertexToSlop.Num() - 1 : EndIndex - 1;
						}

						FPoint2D StartPoint = Vertices[VertexToSlop[StartIndex].Key].Value;
						FPoint2D Vect = StartPoint - NewVertex;
						FPoint2D Perp(Vect.V, -Vect.U);
						FPoint2D StartPoint2 = StartPoint - Perp;
						TSegment<FPoint2D> Segment1(StartPoint, StartPoint2);

						FPoint2D EndPoint = Vertices[VertexToSlop[EndIndex].Key].Value;
						FPoint2D Vect2 = EndPoint - NewVertex;
						FPoint2D Perp2(Vect2.V, -Vect2.U);
						FPoint2D EndPoint2 = EndPoint + Perp2;
						TSegment<FPoint2D> Segment2(EndPoint, EndPoint2);

						double SlopCandidate = 4;
						FPoint2D Intersection; 
						if(FindIntersectionOfLines2D(Segment1, Segment2, Intersection))
						{
							SlopCandidate = ComputePositiveSlope(CandidateVertexPoint, NewVertex, Intersection);
						}

						if (SlopCandidate > 2.00 && SlopCandidate < 6.00)
						{
							bTriangleHasBeenAdded = true;

							double SlopeAtNewVertex = ComputeSlope(NewVertex, CandidateVertexPoint);
							if (EndIndex == 0 && SlopeAtNewVertex > VertexToSlop.Last().Value)
							{
								VertexToSlop.Emplace(CandidateVertexIndex, SlopeAtNewVertex);
							}
							else
							{
								VertexToSlop.EmplaceAt(EndIndex, CandidateVertexIndex, SlopeAtNewVertex);
							}
							TmpVertexIndices.Add(CandidateVertexIndex);
							TriangleIndices.Add(TriangleIndex);
						}
						TriangleIndex = -1;
					}
				}
			}

			// Find the boundary edges of the selected triangles:
			// For all selected triangles, 
			//    For each triangle edges
			//       if the edge is not in EdgeVertexIndices: Add the edge i.e. add its vertex indices
			//       else (the edge is in EdgeVertexIndices), remove the edge of EdgeVertexIndices
			// As the triangles are oriented, the edge AB of a triangle is the edge BA of the adjacent triangle
			EdgeVertexIndices.Empty(VerticesCount);
			for (int32 TriangleIndex : TriangleIndices)
			{
				int32 EndVertex = TriangleSet[TriangleIndex].VertexIndices[2];
				for (int32 Index = 0; Index < 3; Index++)
				{
					int32 StartVertex = TriangleSet[TriangleIndex].VertexIndices[Index];
					int32 Endex = 0;
					// Does the edge exist
					for (; Endex < EdgeVertexIndices.Num(); Endex += 2)
					{
						if (EdgeVertexIndices[Endex] == EndVertex && EdgeVertexIndices[Endex + 1] == StartVertex)
						{
							EdgeVertexIndices[Endex] = -1;
							EdgeVertexIndices[Endex + 1] = -1;
							break;
						}
					}
					if (Endex == EdgeVertexIndices.Num())
					{   // No
						EdgeVertexIndices.Add(StartVertex);
						EdgeVertexIndices.Add(EndVertex);
					}
					EndVertex = StartVertex;
				}
			}

			// make the new triangles : Each new triangle is defined by an edge of the boundary and the new vertex 
			{
				// The deleted triangles are replaced by the new ones
				int32 EdgeIndex = 0;
				for (int32 TriangleIndex : TriangleIndices)
				{
					while (EdgeVertexIndices[EdgeIndex] < 0)
					{
						EdgeIndex += 2;
					}
					TriangleSet[TriangleIndex].Set(EdgeVertexIndices[EdgeIndex + 1], EdgeVertexIndices[EdgeIndex], VertexIndex, Vertices);
					EdgeIndex += 2;
				}

				// When all deleted triangles are replaced, the new triangles are added in the array
				for (; EdgeIndex < EdgeVertexIndices.Num(); EdgeIndex += 2)
				{
					if (EdgeVertexIndices[EdgeIndex] < 0)
					{
						continue;
					}
					TriangleSet.Emplace(EdgeVertexIndices[EdgeIndex + 1], EdgeVertexIndices[EdgeIndex], VertexIndex, Vertices);
				}
			}
		}

		// Find all Edges and their type (inner edge or boundary edge)
		EdgeVertexIndices.Empty(TriangleSet.Num() * 6);

		for (int32 TriangleIndex = 0; TriangleIndex < TriangleSet.Num(); TriangleIndex++)
		{
			int32 Index = 0;
			for (; Index < 3; Index++)
			{
				if (TriangleSet[TriangleIndex].VertexIndices[Index] >= VerticesCount)
				{
					// one of the point is a corner of the bounding mesh
					// At least, only one edge is added and this edge is necessarily an outer edge
					break;
				}
			}
			bool bIsOuter = Index < 3;

			int32 EndVertex = TriangleSet[TriangleIndex].VertexIndices[2];
			for (Index = 0; Index < 3; Index++)
			{
				int32 StartVertex = TriangleSet[TriangleIndex].VertexIndices[Index];
				if (StartVertex < VerticesCount && EndVertex < VerticesCount)
				{
					int32 Endex = 0;
					for (; Endex < EdgeVertexIndices.Num(); Endex += 2)
					{
						// Does the edge exist
						if (EdgeVertexIndices[Endex] == EndVertex && EdgeVertexIndices[Endex + 1] == StartVertex)
						{
							if (!bIsOuter)
							{
								EdgeInstanceCount[Endex / 2]++;
							}
							break;
						}
					}

					if (Endex == EdgeVertexIndices.Num())
					{
						// No
						EdgeVertexIndices.Add(StartVertex);
						EdgeVertexIndices.Add(EndVertex);
						EdgeInstanceCount.Add(bIsOuter ? 0 : 1);
					}
				}
				EndVertex = StartVertex;
			}
		}

		// the bounding mesh vertices are removed
		Vertices.SetNum(VerticesCount);

		for (int32& Indice : EdgeVertexIndices)
		{
			Indice = Vertices[Indice].Key;
		}
	}

	int32 OuterEdgeCount() const
	{
		int32 EdgeCount = 0;
		for (int32 Index = 0; Index < EdgeVertexIndices.Num() / 2; ++Index)
		{
			if (EdgeInstanceCount[Index] < 2)
			{
				EdgeCount++;
			}
		}
		return EdgeCount;
	}

	/**
	 * Return the edge connected to 0 or 1 triangle
	 */
	void GetOuterEdges(TArray<int32>& OuterEdgeIndices) const
	{
		int32 EdgeCount = OuterEdgeCount();
		OuterEdgeIndices.Reserve(EdgeCount);
		for (int32 Index = 0, EdgeIndex = 0; Index < EdgeVertexIndices.Num(); ++EdgeIndex)
		{
			if (EdgeInstanceCount[EdgeIndex] < 2)
			{
				OuterEdgeIndices.Add(EdgeVertexIndices[Index++]);
				OuterEdgeIndices.Add(EdgeVertexIndices[Index++]);
			}
			else
			{
				Index += 2;
			}
		}
	}

	void GetOuterVertices(TSet<int32>& OuterVertexIndices) const
	{
		int32 EdgeCount = OuterEdgeCount();
		OuterVertexIndices.Reserve(EdgeCount);
		for (int32 Index = 0, EdgeIndex = 0; Index < EdgeVertexIndices.Num(); ++EdgeIndex)
		{
			if (EdgeInstanceCount[EdgeIndex] < 2)
			{
				OuterVertexIndices.Add(EdgeVertexIndices[Index++]);
				OuterVertexIndices.Add(EdgeVertexIndices[Index++]);
			}
			else
			{
				Index += 2;
			}
		}
	}

	void GetMesh(TArray<int32>& Triangles)
	{
		Triangles.Empty(TriangleSet.Num() * 3);
		for (const FTriangle& Triangle : TriangleSet)
		{
			Triangles.Append(Triangle.VertexIndices, 3);
		}
	}

#ifdef DEBUG_BOWYERWATSON
	static bool bDisplay;
#endif

private:
	int32 VerticesCount;

	TArray<TPair<int32, FPoint2D>>& Vertices;

	/**
	 * An edge is defined by the indices of its vertices
	 * So the ith edge is defined by the vertices EdgeVertexIndices[2 * ith] and EdgeVertexIndices[2 * ith + 1]
	 */
	TArray<int32>& EdgeVertexIndices;

	TArray<FTriangle> TriangleSet;

	// It's use to mark all triangles whose circumcircles contain the next vertex
	TArray<int32> TriangleIndices;
	TArray<int32> AdditionalTriangleIndices;

	/**
	 * Use to determine if the edge is a border edge of inner edge
	 * If EdgeInstanceCount[ith] = 2, the edge is a inner edge
	 */
	TArray<int32> EdgeInstanceCount;

	void MakeBoundingMesh()
	{
		FAABB2D VerticesBBox;
		for (const TPair<int32, FPoint2D>& Vertex : Vertices)
		{
			VerticesBBox += Vertex.Value;
		}

		const double DiagonalLength = VerticesBBox.DiagonalLength();
		VerticesBBox.Offset(DiagonalLength);

		int32 VerticesId = Vertices.Num();
		Vertices.Emplace(VerticesId++, VerticesBBox.GetCorner(3));
		Vertices.Emplace(VerticesId++, VerticesBBox.GetCorner(2));
		Vertices.Emplace(VerticesId++, VerticesBBox.GetCorner(0));
		Vertices.Emplace(VerticesId++, VerticesBBox.GetCorner(1));

		TriangleSet.Emplace(VerticesCount, VerticesCount + 1, VerticesCount + 2, Vertices);
		TriangleSet.Emplace(VerticesCount + 2, VerticesCount + 3, VerticesCount, Vertices);
	}

	void Init()
	{
		VerticesCount = Vertices.Num();
		Vertices.Reserve(VerticesCount + 4);
		TriangleSet.Reserve(VerticesCount);
		TriangleIndices.Reserve(VerticesCount);
		AdditionalTriangleIndices.Reserve(VerticesCount);
		EdgeVertexIndices.Reserve(4 * VerticesCount);
		EdgeInstanceCount.Reserve(2 * VerticesCount);
	}

#ifdef DEBUG_BOWYERWATSON
	void DisplayEdges()
	{
		if (!bDisplay)
		{
			return;
		}

		F3DDebugSession _(TEXT("Edges"));
		for (int32 Index = 0; Index < EdgeVertexIndices.Num(); Index += 2)
		{
			if (EdgeInstanceCount[Index / 2] < 2)
			{
				DisplaySegment(Vertices[EdgeVertexIndices[Index]].Value * DisplayScale, Vertices[EdgeVertexIndices[Index + 1]].Value * DisplayScale, 0, EVisuProperty::YellowCurve);
			}
			else
			{
				DisplaySegment(Vertices[EdgeVertexIndices[Index]].Value * DisplayScale, Vertices[EdgeVertexIndices[Index + 1]].Value * DisplayScale, 0, EVisuProperty::PurpleCurve);
			}
		}
	};

	void DisplayTriangles()
	{
		if (!bDisplay)
		{
			return;
		}

		F3DDebugSession _(TEXT("Triangles"));
		for (int32 Index = 0; Index < TriangleSet.Num(); Index++)
		{
			DisplayTriangle(Index, EVisuProperty::BlueCurve);
		}
		//Wait();
	};

	void DisplaySelectedTriangles()
	{
		if (!bDisplay)
		{
			return;
		}

		F3DDebugSession _(TEXT("Selected Triangles"));
		for (int32 Index : TriangleIndices)
		{
			F3DDebugSession _(TEXT("Triangle"));
			DisplayTriangle(Index, EVisuProperty::BlueCurve);
		}
		//Wait();
	};

	void DisplayTriangle(int32 Index, EVisuProperty Property)
	{
		if (!bDisplay)
		{
			return;
		}

		DisplaySegment(Vertices[TriangleSet[Index].VertexIndices[0]].Value * DisplayScale, Vertices[TriangleSet[Index].VertexIndices[1]].Value * DisplayScale, 0, Property);
		DisplaySegment(Vertices[TriangleSet[Index].VertexIndices[1]].Value * DisplayScale, Vertices[TriangleSet[Index].VertexIndices[2]].Value * DisplayScale, 0, Property);
		DisplaySegment(Vertices[TriangleSet[Index].VertexIndices[2]].Value * DisplayScale, Vertices[TriangleSet[Index].VertexIndices[0]].Value * DisplayScale, 0, Property);
		//Wait();
	};

#endif

};

}