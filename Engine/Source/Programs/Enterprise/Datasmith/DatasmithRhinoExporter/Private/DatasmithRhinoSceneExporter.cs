// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using Rhino;
using Rhino.DocObjects;
using Rhino.DocObjects.Tables;
using System.Collections.Generic;
using Rhino.Geometry;

namespace DatasmithRhino
{
	public class DatasmithRhinoSceneExporter
	{
		struct DSMeshInstance
		{
			public FDatasmithFacadeMesh DSMesh;
			public Vector3d PivotOffset;
		}

		Dictionary<Guid, Mesh> ObjectIDMeshDictionary;
		Dictionary<Mesh, DSMeshInstance> MeshDSMeshDictionary = new Dictionary<Mesh, DSMeshInstance>();

		public bool Export(string filename, RhinoDoc RhinoDocument)
		{
			string RhinoAppName = Rhino.RhinoApp.Name;
			string RhinoVersion = Rhino.RhinoApp.ExeVersion.ToString();
			FDatasmithFacadeElement.SetCoordinateSystemType(FDatasmithFacadeElement.ECoordinateSystemType.RightHandedZup);
			FDatasmithFacadeElement.SetWorldUnitScale((float)Rhino.RhinoMath.UnitScale(RhinoDocument.ModelUnitSystem, UnitSystem.Centimeters));
			FDatasmithFacadeScene DatasmithScene = new FDatasmithFacadeScene("Rhino", "Robert McNeel & Associates", "Rhino3D", RhinoVersion);
			DatasmithScene.PreExport();

			Rhino.Input.Custom.GetObject go = new Rhino.Input.Custom.GetObject();

			using (var ProgressManager = new FDatasmithRhinoProgressManager(RhinoDocument.RuntimeSerialNumber))
			{
				DatasmithRhinoSceneParser SceneParser = new DatasmithRhinoSceneParser(RhinoDocument);
				SceneParser.ParseDocument();
				ExportScene(SceneParser, DatasmithScene);
				string SceneName = System.IO.Path.GetFileName(filename);
				DatasmithScene.Optimize();

				try
				{
					DatasmithScene.ExportScene(filename);
				}
				catch (Exception e)
				{
					Console.WriteLine(e);
					return false;
				}
			}

			return true;
		}

		private Dictionary<Guid, Mesh> GenerateObjectMeshMap(Mesh[] Meshes, ObjectAttributes[] Attributes)
		{
			Dictionary<Guid, Mesh> GeneratedDictionary = new Dictionary<Guid, Mesh>();

			for (int MeshIndex = 0; MeshIndex < Meshes.Length && MeshIndex < Attributes.Length; ++MeshIndex)
			{
				Guid ObjectID = Attributes[MeshIndex].ObjectId;

				if (GeneratedDictionary.TryGetValue(ObjectID, out Mesh GeneratedMesh))
				{
					GeneratedMesh.Append(Meshes[MeshIndex]);
				}
				else
				{
					GeneratedDictionary.Add(ObjectID, Meshes[MeshIndex]);
				}
			}

			return GeneratedDictionary;
		}

		public Rhino.Commands.Result ExportScene(DatasmithRhinoSceneParser SceneParser, FDatasmithFacadeScene DatasmithScene)
		{
			RhinoDoc RhinoDocument = SceneParser.RhinoDocument;

			MeshingParameters MeshingParams = RhinoDocument.GetMeshingParameters(RhinoDocument.MeshingParameterStyle);
			bool bSimpleDialog = true;
			Mesh[] Meshes;
			ObjectAttributes[] Attributes;
			Rhino.Commands.Result MeshingResult = Rhino.Commands.Result.Failure;
			HashSet<RhinoObject> DocObjects = CollectExportedRhinoObjects(SceneParser);


			MeshingResult = RhinoObject.MeshObjects(DocObjects, ref MeshingParams, ref bSimpleDialog, out Meshes, out Attributes);
			if (MeshingResult != Rhino.Commands.Result.Success)
			{
				return MeshingResult;
			}
			else
			{
				ObjectIDMeshDictionary = GenerateObjectMeshMap(Meshes, Attributes);
			}

			bool PreviousRedrawValue = RhinoDocument.Views.RedrawEnabled;
			RhinoDocument.Views.RedrawEnabled = false;

			ExportHierarchy(DatasmithScene, SceneParser.SceneRoot);

			RhinoDocument.Views.RedrawEnabled = PreviousRedrawValue;
			return Rhino.Commands.Result.Success;
		}

		private HashSet<RhinoObject> CollectExportedRhinoObjects(DatasmithRhinoSceneParser SceneParser)
		{
			//First get all non-instance objects directly in the scene
			ObjectType ObjectFilter = ObjectType.AnyObject ^ (ObjectType.InstanceDefinition | ObjectType.InstanceReference);
			HashSet<RhinoObject> DocObjects = new HashSet<RhinoObject>(SceneParser.RhinoDocument.Objects.GetObjectList(ObjectFilter));

			//Then collect objects that may live only inside instance definitions(blocks)
			foreach (var KeyValue in SceneParser.InstanceNodeMap)
			{
				foreach (RhinoSceneHierarchyNode HierarchyNode in KeyValue.Value)
				{
					if (!HierarchyNode.bIsRoot && HierarchyNode.Info.bHasRhinoObject && !(HierarchyNode.Info.RhinoModelComponent is InstanceObject))
					{
						DocObjects.Add(HierarchyNode.Info.RhinoModelComponent as RhinoObject);
					}
				}
			}

			return DocObjects;
		}

		private void ExportHierarchy(FDatasmithFacadeScene DatasmithScene, RhinoSceneHierarchyNode RootNode)
		{
			foreach (RhinoSceneHierarchyNode Node in RootNode)
			{
				if (Node.bIsRoot)
				{
					continue;
				}

				ExportHierarchyNode(DatasmithScene, Node);
			}
		}

		private void ExportHierarchyNode(FDatasmithFacadeScene DatasmithScene, RhinoSceneHierarchyNode Node)
		{
			if (Node.Info.bHasRhinoObject)
			{
				ExportObject(DatasmithScene, Node);
			}
			else
			{
				//This node has no RhinoObject, export an empty Actor.
				ExportEmptyNode(DatasmithScene, Node);
			}
		}

		private void ExportObject(FDatasmithFacadeScene InDatasmithScene, RhinoSceneHierarchyNode InNode)
		{
			RhinoObject CurrentObject = InNode.Info.RhinoModelComponent as RhinoObject;

			if(CurrentObject.ObjectType == ObjectType.InstanceReference)
			{
				//The Instance Reference node is exported as an empty actor under which we create the instanced block.
				ExportEmptyNode(InDatasmithScene, InNode);
			}
			else if (ObjectIDMeshDictionary.TryGetValue(CurrentObject.Id, out Mesh GeneratedMesh))
			{
				DSMeshInstance DatasmithMeshDefinition = GetOrCreateDatasmithMeshDefinition(GeneratedMesh, InNode, InDatasmithScene);

				FDatasmithFacadeActorMesh DatasmithActorMesh = new FDatasmithFacadeActorMesh("A:" + InNode.Info.Name, InNode.Info.Label);
				InNode.SetDatasmithActor(DatasmithActorMesh);
				DatasmithActorMesh.HashName();

				Transform OffsetTransform = Transform.Translation(DatasmithMeshDefinition.PivotOffset);
				Transform WorldTransform = Transform.Multiply(InNode.Info.InstanceWorldTransform, OffsetTransform);
				DatasmithActorMesh.SetWorldTransform(WorldTransform.ToFloatArray(false));

				string MeshName = DatasmithMeshDefinition.DSMesh.GetName();
				DatasmithActorMesh.SetMesh(MeshName);
				DatasmithActorMesh.KeepActor();

				AddActorToParent(DatasmithActorMesh, InNode, InDatasmithScene);
			}
			else
			{
				string ObjectName = CurrentObject.Name != null && CurrentObject.Name != "" ? CurrentObject.Name : CurrentObject.Id.ToString();
				RhinoApp.WriteLine(string.Format("RhinoObject \"{0}\" of type {1} has no mesh and will be ignored.", ObjectName, CurrentObject.ObjectType));
			}
		}

		private DSMeshInstance GetOrCreateDatasmithMeshDefinition(Mesh InRhinoMesh, RhinoSceneHierarchyNode InNode, FDatasmithFacadeScene InDatasmithScene)
		{
			DSMeshInstance DatasmithMeshDefinition;
			if (!MeshDSMeshDictionary.TryGetValue(InRhinoMesh, out DatasmithMeshDefinition))
			{
				DatasmithMeshDefinition.DSMesh = new FDatasmithFacadeMesh("M:" + InNode.Info.Name, InNode.Info.Label);
				DatasmithMeshDefinition.DSMesh.HashName();
				DatasmithMeshDefinition.PivotOffset = FDatasmithRhinoMeshExporter.CenterMeshOnPivot(InRhinoMesh);
				FDatasmithRhinoMeshExporter.ParseMesh(DatasmithMeshDefinition.DSMesh, InRhinoMesh);
				InDatasmithScene.AddElement(DatasmithMeshDefinition.DSMesh);

				MeshDSMeshDictionary.Add(InRhinoMesh, DatasmithMeshDefinition);
			}

			return DatasmithMeshDefinition;
		}

		private void ExportEmptyNode(FDatasmithFacadeScene InDatasmithScene, RhinoSceneHierarchyNode InNode)
		{
			FDatasmithFacadeActor DatasmithActor = new FDatasmithFacadeActor(InNode.Info.Name, InNode.Info.Label);
			InNode.SetDatasmithActor(DatasmithActor);
			DatasmithActor.HashName();
			DatasmithActor.KeepActor();

			float[] MatrixArray = InNode.Info.InstanceWorldTransform.ToFloatArray(false);
			DatasmithActor.SetWorldTransform(MatrixArray);

			AddActorToParent(DatasmithActor, InNode, InDatasmithScene);
		}

		private void AddActorToParent(FDatasmithFacadeActor InDatasmithActor, RhinoSceneHierarchyNode InNode, FDatasmithFacadeScene InDatasmithScene)
		{
			if (InNode.Parent.bIsRoot)
			{
				InDatasmithScene.AddElement(InDatasmithActor);
			}
			else
			{
				InNode.Parent.DatasmithActor.AddChild(InDatasmithActor);
			}
		}

		private static void AddTagsToDatasmithActors(FDatasmithFacadeActor InDatasmithActor, RhinoSceneHierarchyNode InNode)
		{
			if (!InNode.bIsRoot && InNode.Info.Tags != null)
			{
				foreach(string CurrentTag in InNode.Info.Tags)
				{
					InDatasmithActor.AddTag(CurrentTag);
				}
			}
		}

	}
}