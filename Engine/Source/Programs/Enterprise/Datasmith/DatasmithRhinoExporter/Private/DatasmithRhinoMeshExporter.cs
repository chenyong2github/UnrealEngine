// Copyright Epic Games, Inc. All Rights Reserved.
using Rhino.DocObjects;
using Rhino.Geometry;
using System.Collections.Generic;

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
			int MeshIndex = 0;
			int MeshCount = SceneParser.ObjectIdToMeshInfoDictionary.Count;

			foreach (DatasmithMeshInfo CurrentMeshInfo in SceneParser.ObjectIdToMeshInfoDictionary.Values)
			{
				FDatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress((float)(MeshIndex++)/MeshCount);

				string HashedName = FDatasmithFacadeElement.GetStringHash(CurrentMeshInfo.Name);
				FDatasmithFacadeMesh DatasmithMesh = new FDatasmithFacadeMesh(HashedName);
				DatasmithMesh.SetLabel(CurrentMeshInfo.Label);				

				List<RhinoMaterialInfo> MaterialInfos = new List<RhinoMaterialInfo>(CurrentMeshInfo.MaterialIndices.Count);
				CurrentMeshInfo.MaterialIndices.ForEach((MaterialIndex) => MaterialInfos.Add(SceneParser.GetMaterialInfoFromMaterialIndex(MaterialIndex)));
				ParseMesh(DatasmithMesh, CurrentMeshInfo.RhinoMeshes, MaterialInfos);

				DatasmithScene.AddMesh(DatasmithMesh);
			}
		}

		public static void ParseMesh(FDatasmithFacadeMesh DatasmithMesh, List<Mesh> MeshSections, List<RhinoMaterialInfo> MaterialInfos)
		{
			int VertexIndexOffset = 0;
			List<RhinoMaterialInfo> UniqueMaterialInfo = new List<RhinoMaterialInfo>();

			for (int MeshIndex = 0; MeshIndex < MeshSections.Count; ++MeshIndex )
			{
				Mesh RhinoMesh = MeshSections[MeshIndex];

				// Get Material index for the current section.
				int MaterialIndex = UniqueMaterialInfo.FindIndex((CurrentInfo)=> CurrentInfo == MaterialInfos[MeshIndex]);
				if (MaterialIndex == -1)
				{
					MaterialIndex = UniqueMaterialInfo.Count;
					DatasmithMesh.AddMaterial(MaterialIndex, MaterialInfos[MeshIndex]?.Name);
					UniqueMaterialInfo.Add(MaterialInfos[MeshIndex]);
				}

				// Add all the section vertices to the mesh.
				for (int VertexIndex = 0; VertexIndex < RhinoMesh.Vertices.Count; ++VertexIndex)
				{
					Point3f Vertex = RhinoMesh.Vertices[VertexIndex];
					DatasmithMesh.AddVertex(Vertex.X, Vertex.Y, Vertex.Z);
				}

				// Try to compute normals if the section doesn't have them
				if (RhinoMesh.Normals.Count == 0)
				{
					RhinoMesh.Normals.ComputeNormals();
				}

				bool bUseFaceNormals = RhinoMesh.Normals.Count != RhinoMesh.Vertices.Count && RhinoMesh.FaceNormals.Count == RhinoMesh.Faces.Count;

				//Add triangles and normals to the mesh.
				for (int FaceIndex = 0; FaceIndex < RhinoMesh.Faces.Count; ++FaceIndex)
				{
					MeshFace Face = RhinoMesh.Faces[FaceIndex];

					DatasmithMesh.AddTriangle(VertexIndexOffset + Face.A, VertexIndexOffset + Face.B, VertexIndexOffset + Face.C, MaterialIndex);

					if (Face.IsQuad)
					{
						DatasmithMesh.AddTriangle(VertexIndexOffset + Face.A, VertexIndexOffset + Face.C, VertexIndexOffset + Face.D, MaterialIndex);
					}

					if (bUseFaceNormals)
					{
						Vector3f Normal = RhinoMesh.FaceNormals[FaceIndex];
						AddNormalsToMesh(DatasmithMesh, Normal);

						if (Face.IsQuad)
						{
							AddNormalsToMesh(DatasmithMesh, Normal);
						}
					}
					else
					{
						Vector3f[] Normals = new Vector3f[] { RhinoMesh.Normals[Face.A], RhinoMesh.Normals[Face.B], RhinoMesh.Normals[Face.C] };

						AddNormalsToMesh(DatasmithMesh, Normals[0], Normals[1], Normals[2]);
						if (Face.IsQuad)
						{
							Vector3f DNormal = RhinoMesh.Normals[Face.D];
							AddNormalsToMesh(DatasmithMesh, Normals[0], Normals[2], DNormal);
						}
					}
				}

				// Add the UV coordinates for the triangles we just added.
				int NumberOfUVCoord = RhinoMesh.TextureCoordinates.Count;
				foreach (var UV in RhinoMesh.TextureCoordinates)
				{
					DatasmithMesh.AddUV(0, UV.X, 1 - UV.Y);
				}

				VertexIndexOffset += RhinoMesh.Vertices.Count;
			}
		}

		private static void AddNormalsToMesh(FDatasmithFacadeMesh Mesh, Vector3f Normal)
		{
			AddNormalsToMesh(Mesh, Normal, Normal, Normal);
		}

		private static void AddNormalsToMesh(FDatasmithFacadeMesh Mesh, Vector3f NormalA, Vector3f NormalB, Vector3f NormalC)
		{
			Mesh.AddNormal(NormalA.X, NormalA.Y, NormalA.Z);
			Mesh.AddNormal(NormalB.X, NormalB.Y, NormalB.Z);
			Mesh.AddNormal(NormalC.X, NormalC.Y, NormalC.Z);
		}
	}
}