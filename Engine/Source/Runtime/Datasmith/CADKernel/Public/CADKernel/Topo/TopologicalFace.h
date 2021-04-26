// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/MetadataDictionary.h"
#include "CADKernel/Geo/GeoPoint.h"
#include "CADKernel/Geo/Surfaces/Surface.h"
#include "CADKernel/Math/Aabb.h"
#include "CADKernel/Math/Curvature.h"
#include "CADKernel/Topo/TopologicalEntity.h"
#include "CADKernel/Topo/TopologicalLoop.h"

namespace CADKernel
{
	enum class EStatut : uint8
	{
		Interior = 0,
		Exterior,
		Border
	};

	enum class EQuadType : uint8
	{
		Unset = 0,
		Quadrangular,
		Triangular,
		Other
	};

	class FGrid;
	class FFaceMesh;
	class FThinZone;
	class FThinZoneFinder;
	class FBezierSurface;
	class FSegmentCurve;
	
	class CADKERNEL_API FTopologicalFace : public FTopologicalEntity, public FMetadataDictionary
	{
		friend class FEntity;

	protected:

		TSharedPtr<FSurface> CarrierSurface;
		TArray<TSharedPtr<FTopologicalLoop>> Loops;

		mutable TCache<FSurfacicBoundary> Boundary;

		TSharedPtr<FFaceMesh> Mesh;

		/**
		 * Final U&V coordinates of the surface's mesh grid
		 */
		FCoordinateGrid MeshCuttingCoordinates;

		/**
		 * Temporary discretization of the surface used to compute the mesh of the edge
		 */
		FCoordinateGrid CrossingCoordinates;
		
		/**
		 * Min delta U at the crossing u coordinate to respect meshing criteria
		 */
		FCoordinateGrid CrossingPointDeltaMins;

		/**
		 * Max delta U at the crossing u coordinate to respect meshing criteria
		 */
		FCoordinateGrid CrossingPointDeltaMaxs;

		/**
		 * Build a non-trimmed trimmed surface 
		 * This constructor has to be completed with one of the three "AddBoundaries" methods to be finalized. 
		 */
		FTopologicalFace(const TSharedPtr<FSurface>& InCarrierSurface)
			: FTopologicalEntity()
			, CarrierSurface(InCarrierSurface)
			, Mesh(TSharedPtr<FFaceMesh>())
		{
			ResetElementStatus();
		}

		FTopologicalFace(FCADKernelArchive& Archive)
			: FTopologicalEntity()
			, Mesh(TSharedPtr<FFaceMesh>())
		{
			Serialize(Archive);
		}

		/**
		 * Compute the bounds of the topological surface according to the Loops
		 */
		void ComputeBoundary() const;

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FTopologicalEntity::Serialize(Ar);
			SerializeIdent(Ar, CarrierSurface);
			SerializeIdents(Ar, (TArray<TSharedPtr<FEntity>>&) Loops);
		}

		virtual void SpawnIdent(FDatabase& Database) override;

		virtual void ResetMarkersRecursively() override
		{
			ResetMarkers();
			ResetMarkersRecursivelyOnEntities(Loops);
			CarrierSurface->ResetMarkersRecursively();
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual EEntity GetEntityType() const override
		{
			return EEntity::TopologicalFace;
		}

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override;

		const FSurfacicTolerance& GetIsoTolerances() const
		{
			return CarrierSurface->GetIsoTolerances();
		}

		const double& GetIsoTolerance(EIso Iso) const
		{
			return CarrierSurface->GetIsoTolerance(Iso);
		}

		const FSurfacicBoundary& GetBoundary() const
		{
			if (!Boundary.IsValid())
			{
				ComputeBoundary();
			}
			return Boundary;
		};

		virtual int32 FaceCount() const override
		{
			return 1;
		}

		virtual void GetFaces(TArray<TSharedPtr<FTopologicalFace>>& OutFaces) override		
		{
			if (!HasMarker1())
			{
				OutFaces.Emplace(StaticCastSharedRef<FTopologicalFace>(AsShared()));
				SetMarker1();
			}
		}

		// ======   Loop Functions   ======

		void RemoveLoop(const TSharedPtr<FTopologicalLoop>& Loop);
		void AddLoop(const TSharedPtr<FTopologicalLoop>& Loop);

		/**
		 * Trimmed the face with an outer boundary (first boundary of the array) and inners boundaries
		 */
		void AddLoops(const TArray<TSharedPtr<FTopologicalLoop>>& Loops);

		/**
		 * Trimmed the face with curves i.e. Edges will be build from the cures to make boundaries.
		 */
		void AddLoops(const TArray<TSharedPtr<FCurve>>& Restrictions);

		/**
		 * Trimmed the face with its natural limit curves (Iso UMin,  ...). This function is called to trim untrimmed topological face.
		 * This function should not be called if the topological face already has a loop. 
		 */
		void ApplyNaturalLoops();

		int32 LoopCount() const 
		{
			return Loops.Num();
		}

		const TArray<TSharedPtr<FTopologicalLoop>>& GetLoops() const
		{
			return Loops;
		}

		/**
		 * Get a sampling of each loop of the face 
		 * @param OutLoopSamplings an array of 2d points 
		 * @param OutAABBs an array of 2d axis aligned bounding box of each boundary
		 */
		const void Get2DLoopSampling(TArray<TArray<FPoint2D>>& OutLoopSamplings) const;


		// ======   Loop edge Functions   ======

		/**
		 * @return the twin edge of linked edge belonging this topological face
		 */
		TSharedPtr<FTopologicalEdge> GetLinkedEdge(const TSharedPtr<FTopologicalEdge>& LinkedEdge) const;

		/**
		 * Finds the boundary containing the twin edge of Edge
		 * @param Edge
		 * @param OutBoundaryIndex: the index of the boundary containing the twin edge
		 * @param OutEdgeIndex: the index in the boundary of the twin edge
		 */
		void GetEdgeIndex(const TSharedPtr<FTopologicalEdge>& Edge, int32& OutBoundaryIndex, int32& OutEdgeIndex) const;

		// ======   Carrier Surface Functions   ======

		TSharedRef<FSurface> GetCarrierSurface() const
		{
			return CarrierSurface.ToSharedRef();
		}

		// ======   Point evaluation Functions   ======

		void EvaluatePointGrid(const FCoordinateGrid& Coordinates, FSurfacicSampling& OutPoints, bool bComputeNormals = false) const
		{
			CarrierSurface->EvaluatePointGrid(Coordinates, OutPoints, bComputeNormals);
		}

		void EvaluateGrid(FGrid& Grid) const;

		// ======   Sample Functions   ======

		/**
		 * Generate a pre-sampling of the surface saved in CrossingCoordinate.
		 * This sampling is light enough to allow a fast computation of the grid, precise enough to compute accurately meshing criteria
		 */
		void Presample();

		// ======   Topo Functions   ======

		/**
		 * Checks if the face and the other face have the same boundaries i.e. each non degenerated edge is linked to an edge of the other face
		 */
		bool HasSameBoundariesAs(const TSharedPtr<FTopologicalFace>& OtherFace) const;

		/**
		 * Disconnects the face of its neighbors
		 */
		void RemoveLinksWithNeighbours();

		// ======   Meshing Function   ======

		TSharedRef<FFaceMesh> GetOrCreateMesh(const TSharedRef<FModelMesh>& ShellMesh);

		const bool HasTesselation() const
		{
			return Mesh.IsValid();
		}

		const TSharedRef<FFaceMesh> GetMesh() const
		{
			ensureCADKernel(Mesh.IsValid());
			return Mesh.ToSharedRef();
		}
		
		void InitDeltaUs();

		void ChooseFinalDeltaUs();


		const TArray<double>& GetCuttingCoordinatesAlongIso(EIso Iso) const
		{
			return MeshCuttingCoordinates[Iso];
		}

		TArray<double>& GetCuttingCoordinatesAlongIso(EIso Iso)
		{
			return MeshCuttingCoordinates[Iso];
		}

		const FCoordinateGrid& GetCuttingPointCoordinates() const
		{
			return MeshCuttingCoordinates;
		}

		FCoordinateGrid& GetCuttingPointCoordinates()
		{
			return MeshCuttingCoordinates;
		}

		const FCoordinateGrid& GetCrossingPointCoordinates() const
		{
			return CrossingCoordinates;
		}

		const TArray<double>& GetCrossingPointCoordinates(EIso Iso) const
		{
			return CrossingCoordinates[Iso];
		}

		TArray<double>& GetCrossingPointCoordinates(EIso Iso)
		{
			return CrossingCoordinates[Iso];
		}


		const TArray<double>& GetCrossingPointDeltaMins(EIso Iso) const
		{
			return CrossingPointDeltaMins[Iso];
		}

		TArray<double>& GetCrossingPointDeltaMins(EIso Iso)
		{
			return CrossingPointDeltaMins[Iso];
		}

		const TArray<double>& GetCrossingPointDeltaMaxs(EIso Iso) const
		{
			return CrossingPointDeltaMaxs[Iso];
		}

		TArray<double>& GetCrossingPointDeltaMaxs(EIso Iso)
		{
			return CrossingPointDeltaMaxs[Iso];
		}

		// ======   State, Type Functions   ======

		const EQuadType GetQuadType() const
		{
			return QuadType;
		}

		const bool HasThinZone() const
		{
			return ((States & EHaveStates::ThinZone) == EHaveStates::ThinZone);
		}

		void SetHasThinZone()
		{
			States |= EHaveStates::ThinZone;
		}

		void ResetHasThinSurface()
		{
			States &= ~EHaveStates::ThinZone;
		}

		bool IsBackOriented() const
		{
			return ((States & EHaveStates::IsBackOriented) == EHaveStates::IsBackOriented);
		}

		void SetBackOriented() const
		{
			States |= EHaveStates::IsBackOriented;
		}

		void ResetBackOriented() const
		{
			States &= ~EHaveStates::IsBackOriented;
		}


		// =========================================================================================================================================================================================================
		// =========================================================================================================================================================================================================
		// =========================================================================================================================================================================================================
		//
		//
		//                                                                            NOT YET REVIEWED
		//
		//
		// =========================================================================================================================================================================================================
		// =========================================================================================================================================================================================================
		// =========================================================================================================================================================================================================



		// ======================================================================================================================================================================================================================
		// Quad properties for meshing scheduling ===============================================================================================================================================================================
		// ======================================================================================================================================================================================================================
	private:
		TArray<TSharedPtr<FTopologicalVertex>> SurfaceCorners;
		TArray<int32> StartSideIndices;
		TArray<FEdge2DProperties> SideProperties;
		int32 NumOfMeshedSide = 0;
		double LoopLength;
		double LengthOfMeshedSide = 0;
		double QuadCriteria = 0;
		FSurfaceCurvature Curvatures;
		EQuadType QuadType = EQuadType::Unset;

	public:
		void ComputeQuadCriteria();
		double GetQuadCriteria();

		const FSurfaceCurvature& GetCurvatures() const
		{
			return Curvatures;
		}

		FSurfaceCurvature& GetCurvatures()
		{
			return Curvatures;
		}

		const FIsoCurvature& GetCurvature(EIso Iso) const
		{
			return Curvatures[Iso];
		}

		void ComputeSurfaceSideProperties();
		
		/**
		 * Defines if the surface is either EQuadType::QUAD, either EQuadType::TRIANGULAR or EQuadType::OTHER
		 */
		void DefineSurfaceType();

		const TArray<FEdge2DProperties>& GetSideProperties() const
		{
			return SideProperties;
		}

		FEdge2DProperties& GetSideProperty(int32 Index)
		{
			return SideProperties[Index];
		}

		const FEdge2DProperties& GetSideProperty(int32 Index) const
		{
			return SideProperties[Index];
		}

		int32& MeshedSideNum()
		{
			return NumOfMeshedSide;
		}

		const int32& MeshedSideNum() const
		{
			return NumOfMeshedSide;
		}

		void AddMeshedLength(double Length) 
		{
			LengthOfMeshedSide += Length;
		}

		double MeshedSideRatio() const 
		{
			return LengthOfMeshedSide / LoopLength;
		}

		int32 GetStartEdgeIndexOfSide(int32 Index) const
		{
			return StartSideIndices[Index];
		}

		const TArray<int32>& GetStartSideIndices() const
		{
			return StartSideIndices;
		}

		int32 GetSideIndex(TSharedPtr<FTopologicalEdge> Edge) const 
		{
			int32 EdgeIndex = Loops[0]->GetEdgeIndex(Edge);
			if (EdgeIndex < 0)
			{
				return -1;
			}
			return GetSideIndex(EdgeIndex);
		}

		int32 GetSideIndex(int32 EdgeIndex) const
		{
			if (StartSideIndices.Num() == 0)
			{
				return -1;
			}

			if (StartSideIndices[0] > EdgeIndex)
			{
				return (int32)StartSideIndices.Num() - 1;
			}
			else
			{
				for (int32 SideIndex = 0; SideIndex < StartSideIndices.Num() - 1; ++SideIndex)
				{
					if (StartSideIndices[SideIndex] <= EdgeIndex && EdgeIndex < StartSideIndices[SideIndex + 1])
					{
						return SideIndex;
					}
				}
				return (int32)StartSideIndices.Num() - 1;
			}
		}

	};

	struct FFaceSubset
	{
		TArray<TSharedPtr<FTopologicalFace>> Faces;
		int32 BorderEdgeCount = 0;
		int32 NonManifoldEdgeCount = 0;
	};


} // namespace CADKernel
