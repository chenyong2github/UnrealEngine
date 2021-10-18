// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino.Geometry;
using System.Collections.Generic;

namespace DatasmithRhino.ElementExporters
{
	public class DatasmithRhinoMeshExporter : IDatasmithRhinoElementExporter<DatasmithRhinoMeshExporter, DatasmithMeshInfo>
	{
		///// BEGIN IDatasmithRhinoElementExporter Interface /////

		protected override bool bShouldUseThreading => true;

		protected override int GetElementsToSynchronizeCount()
		{
			return ExportContext.ObjectIdToMeshInfoDictionary.Count;
		}

		protected override IEnumerable<DatasmithMeshInfo> GetElementsToSynchronize()
		{
			return ExportContext.ObjectIdToMeshInfoDictionary.Values;
		}

		protected override FDatasmithFacadeElement CreateElement(DatasmithMeshInfo ElementInfo)
		{
			string HashedName = FDatasmithFacadeElement.GetStringHash(ElementInfo.Name);
			FDatasmithFacadeMeshElement DatasmithMeshElement = new FDatasmithFacadeMeshElement(HashedName);

			// Parse and export the Mesh data to .udsmesh file. Free the DatasmithMesh after the export to reduce memory usage.
			using (FDatasmithFacadeMesh DatasmithMesh = new FDatasmithFacadeMesh())
			{
				ParseMeshElement(DatasmithMeshElement, DatasmithMesh, ElementInfo);
				if (DatasmithScene.ExportDatasmithMesh(DatasmithMeshElement, DatasmithMesh))
				{
					return DatasmithMeshElement;
				}
				else
				{
					return null;
				}
			}
		}

		protected override void AddElement(DatasmithMeshInfo ElementInfo)
		{
			DatasmithScene.AddMesh(ElementInfo.ExportedMesh);
		}

		protected override void ModifyElement(DatasmithMeshInfo ElementInfo)
		{
			FDatasmithFacadeMeshElement DatasmithMeshElement = ElementInfo.ExportedMesh;

			// Parse and export the Mesh data to .udsmesh file. Free the DatasmithMesh after the export to reduce memory usage.
			using (FDatasmithFacadeMesh DatasmithMesh = new FDatasmithFacadeMesh())
			{
				ParseMeshElement(DatasmithMeshElement, DatasmithMesh, ElementInfo);
				DatasmithScene.ExportDatasmithMesh(DatasmithMeshElement, DatasmithMesh);
			}
		}

		protected override void DeleteElement(DatasmithMeshInfo ElementInfo)
		{
			DatasmithScene.RemoveMesh(ElementInfo.ExportedMesh);
		}

		///// END IDatasmithRhinoElementExporter Interface /////

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

		private void ParseMeshElement(FDatasmithFacadeMeshElement DatasmithMeshElement, FDatasmithFacadeMesh DatasmithMesh, DatasmithMeshInfo ElementInfo)
		{
			List<DatasmithMaterialInfo> MaterialInfos = new List<DatasmithMaterialInfo>(ElementInfo.MaterialIndices.Count);
			ElementInfo.MaterialIndices.ForEach((MaterialIndex) => MaterialInfos.Add(ExportContext.GetMaterialInfoFromMaterialIndex(MaterialIndex)));

			DatasmithMeshElement.SetLabel(ElementInfo.UniqueLabel);
			ParseMesh(DatasmithMeshElement, DatasmithMesh, ElementInfo, MaterialInfos);
		}

		private static void ParseMesh(FDatasmithFacadeMeshElement DatasmithMeshElement, FDatasmithFacadeMesh DatasmithMesh, DatasmithMeshInfo MeshInfo, List<DatasmithMaterialInfo> MaterialInfos)
		{
			int VertexIndexOffset = 0;
			int FaceIndexOffset = 0;
			int UVIndexOffset = 0;
			List<Mesh> MeshSections = MeshInfo.RhinoMeshes;
			List<DatasmithMaterialInfo> UniqueMaterialInfo = new List<DatasmithMaterialInfo>();
			InitializeDatasmithMesh(DatasmithMesh, MeshSections, MeshInfo.TextureMappings.Count);

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

				for (int UVChannel = 0; UVChannel < MeshInfo.TextureMappings.Count; ++UVChannel)
				{
					// Since we are offsetting the meshes before exporting them, we must apply the same correction to the UV transform.
					Transform InverveOffsetTranform;
					MeshInfo.OffsetTransform.TryGetInverse(out InverveOffsetTranform);
					DatasmithTextureMappingData TextureMappingData = MeshInfo.TextureMappings[UVChannel];
					Transform CorrectedTransform = InverveOffsetTranform * TextureMappingData.ObjectTransform;

					// Rhino gives no guarantee on the state of the texture mapping in a given mesh.
					// We must make sure that UV is set to the channel we are exporting.
					const bool bLazyLoad = false;
					RhinoMesh.SetTextureCoordinates(TextureMappingData.RhinoTextureMapping, CorrectedTransform, bLazyLoad);

					// Add the UV coordinates for the triangles we just added.
					for (int UVIndex = 0; UVIndex < RhinoMesh.TextureCoordinates.Count; ++UVIndex)
					{
						Point2f UV = RhinoMesh.TextureCoordinates[UVIndex];
						DatasmithMesh.SetUV(UVChannel, UVIndex + UVIndexOffset, UV.X, 1 - UV.Y);
					}
				}

				VertexIndexOffset += RhinoMesh.Vertices.Count;
				FaceIndexOffset += RhinoMesh.Faces.Count + RhinoMesh.Faces.QuadCount;
				UVIndexOffset += RhinoMesh.TextureCoordinates.Count;
			}
		}

		private static void InitializeDatasmithMesh(FDatasmithFacadeMesh DatasmithMesh, List<Mesh> MeshSections, int NumberOfUVChannels)
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
			DatasmithMesh.SetUVChannelsCount(NumberOfUVChannels);

			for (int UVChannel = 0; UVChannel < NumberOfUVChannels; ++UVChannel)
			{
				DatasmithMesh.SetUVCount(UVChannel, TotalNumberOfTextureCoordinates);
			}
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