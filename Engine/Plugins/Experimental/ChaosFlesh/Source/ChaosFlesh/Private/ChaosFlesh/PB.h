// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Vector.h"

//DECLARE_LOG_CATEGORY_EXTERN(LogChaosFlesh, Verbose, All);

namespace ChaosFlesh
{
	namespace IO
	{
		/**
		* Read PhysBAM geometry structure - tet (d=4), tri (d=3), curve(d=2).
		*/
		template <int d>
		bool
		ReadStructure(const FString& filename, TArray<UE::Math::TVector<float>>& positions, TArray<Chaos::TVector<int32, d>>& mesh);

		inline bool ReadTet(const FString& filename, TArray<UE::Math::TVector<float>>& positions, TArray<Chaos::TVector<int32,4>>& mesh)
		{ return ReadStructure<4>(filename, positions, mesh); }
		inline bool ReadTri(const FString& filename, TArray<UE::Math::TVector<float>>& positions, TArray<Chaos::TVector<int32,3>>& mesh)
		{ return ReadStructure<3>(filename, positions, mesh); }
		inline bool ReadCurve(const FString& filename, TArray<UE::Math::TVector<float>>& positions, TArray<Chaos::TVector<int32,2>>& mesh)
		{ return ReadStructure<2>(filename, positions, mesh); }

		/**
		 * PhysBAM simulation directory structure:
		 *
		 * <BaseDir>/common
		 *		deformable_object_structures.gz					-> DEFORMABLE_GEOMETRY_COLLECTION
		 *		first_frame
		 *		last_frame
		 *		log.txt
		 *		muscle_densities.gz
		 *		muscle_fibers.gz
		 *		muscle_tetrahedra.gz
		 *		rigid_body_names
		 *		rigid_body_structures_key.gz
		 *		sim.param
		 * <BaseDir>/<FrameNum>
		 *		control_parameters.gz
		 *		deformable_object_particles.gz
		 *		frame_title
		 *		rigid_body_structure_active_ids.gz
		 *		rigid_geometry_particles.gz
		 *		time.gz
		 * <BaseDir>/face_control_parameters_configuration.gz	-> FACE_CONTROL_PARAMETERS
		 */
		class DeformableGeometryCollectionReader
		{
		public:
			struct Mesh
			{
				Mesh(const int Dimension)
					: Dimension(Dimension)
				{}
				const int32 Dimension;
				// Contains static points, not animated/simulated.
				//TArray<Chaos::FVec3> Points;
				//TArray<UE::Math::TVector<FReal>> Points; // double
				TArray<FVector> Points; // UE::Math::TVector<double>
			};

			struct TriMesh : public Mesh
			{
				TriMesh(const int Dimension = 3)
					: Mesh(3)
				{}
				TArray<Chaos::TVector<int32, 3>> SurfaceElements;
			};

			struct TetMesh : public Mesh
			{
				TetMesh()
					: Mesh(4)
				{}
				TArray<Chaos::TVector<int32, 4>> Elements;
			};

		public:
			DeformableGeometryCollectionReader(const FString& BaseDir, const FString& ColorFilePath=FString(), const FString& ColorGeometryFilePath=FString());
			~DeformableGeometryCollectionReader();

			bool
			ReadPBScene();

			TArray<Mesh*>
			GetMeshes() const
			{ return InputMeshes; }

			TArray<TetMesh*>
			GetTetMeshes() const
			{ return TetMeshes; }

			TArray<TriMesh*>
			GetTriMeshes() const
			{ return TriMeshes; }

			const TArray<Chaos::TVector<float, 3>> &
			GetVertexColors() const
			{ return PBVertexColors; }

			const TriMesh&
			GetVertexColorsTriSurf() const
			{ return PBVertexColorsTriSurf; }

			//! Reads the currently available frane range.
			TPair<int32, int32>
			ReadFrameRange();

			bool
			ReadPoints(const int32 Frame, TArray<UE::Math::TVector<float>>& Points);

			bool
			ReadPoints(const int32 Frame, TArray<UE::Math::TVector<double>>& Points)
			{
				TArray<UE::Math::TVector<float>> FPoints;
				if (!ReadPoints(Frame, FPoints))
					return false;
				Points.SetNum(FPoints.Num());
				for (int32 i = 0; i < FPoints.Num(); i++)
					for (int32 j = 0; j < 3; j++)
						Points[i][j] = FPoints[i][j];
				return true;
			}

		public:
			//! Reads the available frame range.
			//! \p CommonDir the 'BaseDir/common' directory to read from.
			//! \p InputFFrame the first available frame; only read if value is != -std::numeric_limits<int32>::max().
			//! \p InputLFrame the last available frame.
			void
			ReadFrameRange(const FString& CommonDir, int32& InputFFrame, int32& InputLFrame) const;

			int32
			ReadNumPoints(const int32 Frame, const FString& BaseDirIn) const;

			bool
			ReadDeformableGeometryCollection(const FString& FilePath);

			bool
			ReadFaceControlParameters(const FString& FilePath);

			bool
			ReadColorFile(const FString& FilePath);

			bool
			ReadColorGeometryFile(const FString& FilePath);

		private:
			FString BaseDir;
			FString CommonDir;
			FString ColorFilePath;
			FString ColorGeometryFilePath;

			TArray<Mesh*> InputMeshes;
			TArray<TriMesh*> TriMeshes;
			TArray<TetMesh*> TetMeshes;

			TArray<Chaos::TVector<float, 3>> PBVertexColors;
			TriMesh PBVertexColorsTriSurf;
		};


	}
} // namespace ChaosFlesh
