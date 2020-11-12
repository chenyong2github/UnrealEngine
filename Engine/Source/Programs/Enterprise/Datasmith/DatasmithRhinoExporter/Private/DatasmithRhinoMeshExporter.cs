// Copyright Epic Games, Inc. All Rights Reserved.
using Rhino.DocObjects;
using Rhino.Geometry;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace DatasmithRhino
{
	public static class FDatasmithRhinoMeshExporter
	{
		/// <summary>
		/// Center the given mesh on its pivot, from the bounding box center. Returns the pivot point.
		/// </summary>
		/// <param name="RhinoMesh"></param>
		/// <returns>The pivot point on which the Mesh was centered</returns>
		public static Vector3d CenterMeshOnPivot(Mesh RhinoMesh)
		{
			BoundingBox MeshBoundingBox = RhinoMesh.GetBoundingBox(true);
			Vector3d PivotPoint = new Vector3d(MeshBoundingBox.Center.X, MeshBoundingBox.Center.Y, MeshBoundingBox.Center.Z);
			RhinoMesh.Translate(-PivotPoint);

			return PivotPoint;
		}

		/// <summary>
		/// Center the given meshes on the pivot determined from the union of their bounding boxes. Returns the pivot point.
		/// </summary>
		/// <param name="RhinoMeshes"></param>
		/// <returns>The pivot point on which the Mesh was centered</returns>
		public static Vector3d CenterMeshesOnPivot(List<Mesh> RhinoMeshes)
		{
			BoundingBox MeshesBoundingBox = RhinoMeshes[0].GetBoundingBox(true);

			for (int MeshIndex = 1; MeshIndex < RhinoMeshes.Count; ++MeshIndex)
			{
				MeshesBoundingBox.Union(RhinoMeshes[MeshIndex].GetBoundingBox(true));
			}

			Vector3d PivotPoint = new Vector3d(MeshesBoundingBox.Center.X, MeshesBoundingBox.Center.Y, MeshesBoundingBox.Center.Z);
			RhinoMeshes.ForEach((CurrentMesh) => CurrentMesh.Translate(-PivotPoint));

			return PivotPoint;
		}


		public static void ExportMeshes(FDatasmithFacadeScene DatasmithScene, DatasmithRhinoSceneParser SceneParser)
		{
			List<DatasmithMeshInfo> MeshesToExport = new List<DatasmithMeshInfo>(SceneParser.ObjectIdToMeshInfoDictionary.Values);
			int MeshCount = MeshesToExport.Count;

			bool[] ExportResult = new bool[MeshCount];
			FDatasmithFacadeMeshElement[] ExportedMeshElements = new FDatasmithFacadeMeshElement[MeshCount];

			int CompletedMeshes = 0;
			bool bExportCancelled = false;
			Thread RhinoThread = Thread.CurrentThread;
			Parallel.For(0, MeshCount, (MeshIndex, LoopState) => 
			{
				DatasmithMeshInfo CurrentMeshInfo = MeshesToExport[MeshIndex];
				List<RhinoMaterialInfo> MaterialInfos = new List<RhinoMaterialInfo>(CurrentMeshInfo.MaterialIndices.Count);
				CurrentMeshInfo.MaterialIndices.ForEach((MaterialIndex) => MaterialInfos.Add(SceneParser.GetMaterialInfoFromMaterialIndex(MaterialIndex)));

				string HashedName = FDatasmithFacadeElement.GetStringHash(CurrentMeshInfo.Name);
				FDatasmithFacadeMeshElement DatasmithMeshElement = new FDatasmithFacadeMeshElement(HashedName);
				DatasmithMeshElement.SetLabel(CurrentMeshInfo.Label);
				ExportedMeshElements[MeshIndex] = DatasmithMeshElement;

				// Parse and export the Mesh data to .udsmesh file. Free the DatasmithMesh after the export to reduce memory usage.
				using (FDatasmithFacadeMesh DatasmithMesh = new FDatasmithFacadeMesh())
				{
					ParseMesh(DatasmithMeshElement, DatasmithMesh, CurrentMeshInfo.RhinoMeshes, MaterialInfos);
					ExportResult[MeshIndex] = DatasmithScene.ExportDatasmithMesh(DatasmithMeshElement, DatasmithMesh);
				}

				// Update the progress, only on rhino's main thread.
				Interlocked.Increment(ref CompletedMeshes);
				if (Thread.CurrentThread == RhinoThread)
				{
					try
					{
						FDatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress((float)(CompletedMeshes) / MeshCount);
					}
					catch (DatasmithExportCancelledException)
					{
						bExportCancelled = true;
						LoopState.Break();
					}
				}
			});

			if(bExportCancelled)
			{
				throw new DatasmithExportCancelledException();
			}

			for (int ResultIndex = 0; ResultIndex < MeshCount; ++ResultIndex)
			{
				if (ExportResult[ResultIndex])
				{
					DatasmithScene.AddMesh(ExportedMeshElements[ResultIndex]);
				}
				else
				{
					//#ueent_todo Log mesh could not be exported in Datasmith logging API.
				}
			}
		}

		public static void ParseMesh(FDatasmithFacadeMeshElement DatasmithMeshElement, FDatasmithFacadeMesh DatasmithMesh, List<Mesh> MeshSections, List<RhinoMaterialInfo> MaterialInfos)
		{
			int VertexIndexOffset = 0;
			int FaceIndexOffset = 0;
			int UVIndexOffset = 0;
			List<RhinoMaterialInfo> UniqueMaterialInfo = new List<RhinoMaterialInfo>();

			InitializeDatasmithMesh(DatasmithMesh, MeshSections);

			for (int MeshIndex = 0; MeshIndex < MeshSections.Count; ++MeshIndex )
			{
				Mesh RhinoMesh = MeshSections[MeshIndex];

				// Get Material index for the current section.
				int MaterialIndex = UniqueMaterialInfo.FindIndex((CurrentInfo)=> CurrentInfo == MaterialInfos[MeshIndex]);
				if (MaterialIndex == -1)
				{
					MaterialIndex = UniqueMaterialInfo.Count;
					DatasmithMeshElement.SetMaterial(MaterialInfos[MeshIndex]?.Name, MaterialIndex);
					UniqueMaterialInfo.Add(MaterialInfos[MeshIndex]);
				}

				// Add all the section vertices to the mesh.
				for (int VertexIndex = 0; VertexIndex < RhinoMesh.Vertices.Count; ++VertexIndex)
				{
					Point3f Vertex = RhinoMesh.Vertices[VertexIndex];
					DatasmithMesh.SetVertex(VertexIndex + VertexIndexOffset, Vertex.X, Vertex.Y, Vertex.Z);
				}

				// Try to compute normals if the section doesn't have them
				if (RhinoMesh.Normals.Count == 0)
				{
					RhinoMesh.Normals.ComputeNormals();
				}

				bool bUseFaceNormals = RhinoMesh.Normals.Count != RhinoMesh.Vertices.Count && RhinoMesh.FaceNormals.Count == RhinoMesh.Faces.Count;

				//Add triangles and normals to the mesh.
				for (int FaceIndex = 0, FaceQuadOffset = 0; FaceIndex < RhinoMesh.Faces.Count; ++FaceIndex)
				{
					int DatasmithFaceIndex = FaceIndex + FaceQuadOffset + FaceIndexOffset;
					MeshFace Face = RhinoMesh.Faces[FaceIndex];

					DatasmithMesh.SetFaceSmoothingMask(DatasmithFaceIndex, 0);
					DatasmithMesh.SetFace(DatasmithFaceIndex, VertexIndexOffset + Face.A, VertexIndexOffset + Face.B, VertexIndexOffset + Face.C, MaterialIndex);
					DatasmithMesh.SetFaceUV(DatasmithFaceIndex, 0, VertexIndexOffset + Face.A, VertexIndexOffset + Face.B, VertexIndexOffset + Face.C);

					if (Face.IsQuad)
					{
						FaceQuadOffset++;
						DatasmithMesh.SetFaceSmoothingMask(DatasmithFaceIndex + 1, 0);
						DatasmithMesh.SetFace(DatasmithFaceIndex + 1, VertexIndexOffset + Face.A, VertexIndexOffset + Face.C, VertexIndexOffset + Face.D, MaterialIndex);
						DatasmithMesh.SetFaceUV(DatasmithFaceIndex + 1, 0, VertexIndexOffset + Face.A, VertexIndexOffset + Face.C, VertexIndexOffset + Face.D);
					}

					if (bUseFaceNormals)
					{
						Vector3f Normal = RhinoMesh.FaceNormals[FaceIndex];
						AddNormalsToMesh(DatasmithMesh, DatasmithFaceIndex, Normal);

						if (Face.IsQuad)
						{
							AddNormalsToMesh(DatasmithMesh, DatasmithFaceIndex + 1, Normal);
						}
					}
					else
					{
						Vector3f[] Normals = new Vector3f[] { RhinoMesh.Normals[Face.A], RhinoMesh.Normals[Face.B], RhinoMesh.Normals[Face.C] };

						AddNormalsToMesh(DatasmithMesh, DatasmithFaceIndex, Normals[0], Normals[1], Normals[2]);
						if (Face.IsQuad)
						{
							Vector3f DNormal = RhinoMesh.Normals[Face.D];
							AddNormalsToMesh(DatasmithMesh, DatasmithFaceIndex + 1, Normals[0], Normals[2], DNormal);
						}
					}
				}

				// Add the UV coordinates for the triangles we just added.
				for (int UVIndex = 0; UVIndex < RhinoMesh.TextureCoordinates.Count; ++UVIndex)
				{
					Point2f UV = RhinoMesh.TextureCoordinates[UVIndex];
					DatasmithMesh.SetUV(0, UVIndex + UVIndexOffset, UV.X, 1 - UV.Y);
				}

				VertexIndexOffset += RhinoMesh.Vertices.Count;
				FaceIndexOffset += RhinoMesh.Faces.Count + RhinoMesh.Faces.QuadCount;
				UVIndexOffset += RhinoMesh.TextureCoordinates.Count;
			}
		}

		private static void InitializeDatasmithMesh(FDatasmithFacadeMesh DatasmithMesh, List<Mesh> MeshSections)
		{
			int TotalNumberOfVertices = 0;
			int TotalNumberOfFaces = 0;
			int TotalNumberOfTextureCoordinates = 0;

			foreach (Mesh MeshSection in MeshSections)
			{
				TotalNumberOfVertices += MeshSection.Vertices.Count;
				TotalNumberOfFaces += MeshSection.Faces.Count + MeshSection.Faces.QuadCount;
				TotalNumberOfTextureCoordinates += MeshSection.TextureCoordinates.Count;
			}

			DatasmithMesh.SetVerticesCount(TotalNumberOfVertices);
			DatasmithMesh.SetFacesCount(TotalNumberOfFaces);
			DatasmithMesh.SetUVChannelsCount(1);
			DatasmithMesh.SetUVCount(0, TotalNumberOfTextureCoordinates);
		}

		private static void AddNormalsToMesh(FDatasmithFacadeMesh Mesh, int FaceIndex, Vector3f Normal)
		{
			AddNormalsToMesh(Mesh, FaceIndex, Normal, Normal, Normal);
		}

		private static void AddNormalsToMesh(FDatasmithFacadeMesh Mesh, int FaceIndex, Vector3f NormalA, Vector3f NormalB, Vector3f NormalC)
		{
			int NormalIndex = FaceIndex * 3;
			Mesh.SetNormal(NormalIndex, NormalA.X, NormalA.Y, NormalA.Z);
			Mesh.SetNormal(NormalIndex + 1, NormalB.X, NormalB.Y, NormalB.Z);
			Mesh.SetNormal(NormalIndex + 2, NormalC.X, NormalC.Y, NormalC.Z);
		}
	}
}