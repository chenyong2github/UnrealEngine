// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IsoCell.h"
#include "CADKernel/UI/Display.h"

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

			FTriangle(const int32& Index0, const int32& Index1, const int32& Index2, const TArray<FPoint2D>& InVertices)
			{
				Set(Index0, Index1, Index2, InVertices);
			}

			void Set(const int32& Index0, const int32& Index1, const int32& Index2, const TArray<FPoint2D>& InVertices)
			{
				VertexIndices[0] = Index0;
				VertexIndices[1] = Index1;
				VertexIndices[2] = Index2;
				FTriangle2D Triangle(InVertices[Index0], InVertices[Index1], InVertices[Index2]);
				Center = Triangle.CircumCircleCenterWithSquareRadius(SquareRadius);
			}
		};


	public:

		/**
		 * @param Vertices the 2d point cloud to mesh
		 * @param OutEdgeVertices, the edges of the mesh. An edge is defined by the indices of its vertices
		 * So the ith edge is defined by the vertices EdgeVertexIndices[2 * ith] and EdgeVertexIndices[2 * ith + 1]
		 */
		FBowyerWatsonTriangulator(TArray<FPoint2D>& InVertices, TArray<int32>& OutEdgeVertices)
			: VerticesCount(InVertices.Num())
			, Vertices(InVertices)
			, EdgeVertexIndices(OutEdgeVertices)
		{
			Init();
		}

		void Triangulate(bool bAddFirstVertex)
		{
			FTimePoint StartTime = FChrono::Now();
#ifdef DEBUG_BOWYERWATSON
			F3DDebugSession DelaunayDebugSession(TEXT("Delaunay Algo"));
#endif // DEBUG_DELAUNAY

			// initialization of Bowyer & Watson algorithm with a bounding mesh of the vertex cloud 
			// i.e. 2 triangles defined by the corners of the offsetted vertices bounding box
			MakeBoundingMesh();

#ifdef DEBUG_BOWYERWATSON
			F3DDebugSession _(TEXT("Vertices"));
			for (int32 LoopIndex = bAddFirstVertex ? 1 : 0; LoopIndex < VerticesCount; ++LoopIndex)
			{
				DisplayPoint(Vertices[LoopIndex], EVisuProperty::YellowPoint);
			}
			DisplayTriangles();
#endif

			// insert each point in the mesh
			for (int32 VertexIndex = bAddFirstVertex ? 1 : 0; VertexIndex < VerticesCount; ++VertexIndex)
			{
				TriangleIndices.Empty(VerticesCount);

				const FPoint2D& NewVertex = Vertices[VertexIndex];

				// find all triangles whose circumcircles contain the new vertex
				for (int32 TriangleIndex = 0; TriangleIndex < TriangleSet.Num(); TriangleIndex++)
				{
					const FPoint2D Center = TriangleSet[TriangleIndex].Center;
					double SquareRadius = TriangleSet[TriangleIndex].SquareRadius;
					double SquareDistanceToCenter = Center.SquareDistance(NewVertex);
					if (SquareDistanceToCenter < SquareRadius)
					{
						TriangleIndices.Add(TriangleIndex);
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
				int32 EdgeIndex = 0;

				// The deleted triangles are replaced by the new ones
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
#ifdef DEBUG_DELAUNAY
				//DisplayTriangles();
#endif
			}

#ifdef DEBUG_DELAUNAY
			// The final mesh
			DisplayTriangles();
#endif

			// Find all Edges and their type (inner edge or boundary edge)
			EdgeVertexIndices.Empty(TriangleSet.Num()*6);

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
#ifdef DEBUG_DELAUNAY
			DisplayEdges();
			//Wait();
#endif
			// the bounding mesh vertices are removed
			Vertices.SetNum(Vertices.Num() - 4);
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
			Triangles.Empty(TriangleSet.Num()*3);
			for (const FTriangle& Triangle : TriangleSet)
			{
				Triangles.Append(Triangle.VertexIndices, 3);
			}
		}

private:
		int32 VerticesCount;

		TArray<FPoint2D>& Vertices;

		/**
		 * An edge is defined by the indices of its vertices
		 * So the ith edge is defined by the vertices EdgeVertexIndices[2 * ith] and EdgeVertexIndices[2 * ith + 1]
		 */
		TArray<int32>& EdgeVertexIndices;

		TArray<FTriangle> TriangleSet;

		// It's use to mark all triangles whose circumcircles contain the next vertex
		TArray<int32> TriangleIndices;

		bool bDisplay = false;

		/**
		 * Use to determine if the edge is a border edge of inner edge
		 * If EdgeInstanceCount[ith] = 2, the edge is a inner edge
		 */
		TArray<int32> EdgeInstanceCount;

		void MakeBoundingMesh()
		{
			FAABB2D VerticesBBox;
			VerticesBBox += Vertices;
			VerticesBBox.Offset(VerticesBBox.DiagonalLength());

			Vertices.Emplace(VerticesBBox.GetCorner(3));
			Vertices.Emplace(VerticesBBox.GetCorner(2));
			Vertices.Emplace(VerticesBBox.GetCorner(0));
			Vertices.Emplace(VerticesBBox.GetCorner(1));

			TriangleSet.Emplace(VerticesCount, VerticesCount + 1, VerticesCount + 2, Vertices);
			TriangleSet.Emplace(VerticesCount + 2, VerticesCount + 3, VerticesCount, Vertices);
		}

		void Init()
		{
			VerticesCount = Vertices.Num();
			Vertices.Reserve(VerticesCount + 4);
			TriangleSet.Reserve(VerticesCount);
			TriangleIndices.Reserve(VerticesCount);
			EdgeVertexIndices.Reserve(4 * VerticesCount);
			EdgeInstanceCount.Reserve(2 * VerticesCount);
		}

#ifdef DEBUG_DELAUNAY
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
				DisplaySegment(Vertices[EdgeVertexIndices[Index]], Vertices[EdgeVertexIndices[Index + 1]], 0, EVisuProperty::YellowCurve);
			}
			else
			{
				DisplaySegment(Vertices[EdgeVertexIndices[Index]], Vertices[EdgeVertexIndices[Index + 1]]);
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
			DisplaySegment(Vertices[TriangleSet[Index].VertexIndices[0]], Vertices[TriangleSet[Index].VertexIndices[1]]);
			DisplaySegment(Vertices[TriangleSet[Index].VertexIndices[1]], Vertices[TriangleSet[Index].VertexIndices[2]]);
			DisplaySegment(Vertices[TriangleSet[Index].VertexIndices[2]], Vertices[TriangleSet[Index].VertexIndices[0]]);
		}
		//Wait();
	};
#endif

	};

}