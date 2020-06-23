// Copyright Epic Games, Inc. All Rights Reserved.
using Rhino.DocObjects;
using Rhino.Geometry;
using System.Collections.Generic;

namespace DatasmithRhino
{
	public static class FDatasmithRhinoMeshExporter
	{
		/// <summary>
		/// Center the given mesh on its pivot, either from a Gumball or from its bounding box center.
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

		public static void ParseMesh(FDatasmithFacadeMesh DatasmithMesh, Mesh RhinoMesh)
		{
			foreach (var Vertex in RhinoMesh.Vertices)
			{
				DatasmithMesh.AddVertex(Vertex.X, Vertex.Y, Vertex.Z);
			}

			if (RhinoMesh.Normals.Count == 0)
			{
				RhinoMesh.Normals.ComputeNormals();
			}

			bool bUseFaceNormals = RhinoMesh.Normals.Count != RhinoMesh.Vertices.Count && RhinoMesh.FaceNormals.Count == RhinoMesh.Faces.Count;
			int NumberOfVertices = DatasmithMesh.GetVertexCount();
			Vector3f[] NormalsByFaces = new Vector3f[NumberOfVertices];

			for (int FaceIndex = 0; FaceIndex < RhinoMesh.Faces.Count; ++FaceIndex)
			{
				MeshFace Face = RhinoMesh.Faces[FaceIndex];

				DatasmithMesh.AddTriangle(Face.A, Face.B, Face.C);

				if (Face.IsQuad)
				{
					DatasmithMesh.AddTriangle(Face.A, Face.C, Face.D);
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

			int NumberOfUVCoord = RhinoMesh.TextureCoordinates.Count;
			foreach (var UV in RhinoMesh.TextureCoordinates)
			{
				DatasmithMesh.AddUV(0, UV.X, UV.Y);
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