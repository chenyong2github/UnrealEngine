// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino;
using Rhino.DocObjects;
using Rhino.Geometry;
using System;
using System.Collections;
using System.Collections.Generic;

namespace DatasmithRhino
{
	// Immutable class defining an actor in the hierarchy.
	public class RhinoSceneHierarchyNodeInfo
	{
		public bool bHasRhinoObject { get { return RhinoModelComponent is RhinoObject; } }
		public bool bHasRhinoLayer { get { return RhinoModelComponent is Layer; } }
		public ModelComponent RhinoModelComponent { get; private set; }
		public string Name { get; private set; }
		public string Label { get; private set; }
		public Transform WorldTransform { get; private set; }
		public int MaterialIndex { get; private set; }
		public bool bOverrideMaterial { get; private set; }
		public List<string> Tags { get; private set; } = new List<string>();

		public RhinoSceneHierarchyNodeInfo(ModelComponent InModelComponent, string InName, string InLabel, List<string> InTags, int InMaterialIndex, bool bInOverrideMaterial, Transform InTransform)
		{
			RhinoModelComponent = InModelComponent;
			Name = InName;
			Label = InLabel;
			Tags = InTags;
			MaterialIndex = InMaterialIndex;
			bOverrideMaterial = bInOverrideMaterial;

			Transform ModelComponentTransform = FDatasmithRhinoUtilities.GetModelComponentTransform(RhinoModelComponent);
			WorldTransform = Transform.Multiply(InTransform, ModelComponentTransform);
		}
	}

	public class RhinoSceneHierarchyNode : IEnumerable<RhinoSceneHierarchyNode>
	{
		public bool bIsRoot { get; private set; } = true;
		public bool bIsInstanceDefinition { get; private set; }
		public RhinoSceneHierarchyNode Parent { get; private set; }
		public RhinoSceneHierarchyNode LinkedNode { get; private set; }
		public FDatasmithFacadeActor DatasmithActor { get; private set; }
		private List<RhinoSceneHierarchyNode> Children;

		public RhinoSceneHierarchyNodeInfo Info { get; private set; }

		//No Parent, this is a root node.
		public RhinoSceneHierarchyNode(bool bInIsInstanceDefinition, RhinoSceneHierarchyNodeInfo InNodeInfo = null)
		{
			Children = new List<RhinoSceneHierarchyNode>();
			bIsInstanceDefinition = bInIsInstanceDefinition;
			Info = InNodeInfo;
		}

		private RhinoSceneHierarchyNode(RhinoSceneHierarchyNode InParent, RhinoSceneHierarchyNodeInfo InNodeInfo) : this(InParent.bIsInstanceDefinition)
		{
			bIsRoot = false;
			Parent = InParent;
			Info = InNodeInfo;
		}

		public void SetDatasmithActor(FDatasmithFacadeActor InActor)
		{
			if(bIsRoot || DatasmithActor != null)
			{
				RhinoApp.WriteLine(string.Format("Error: Generating a datasmith actor for a Hierarchy node that doesn't need one!"));
				return;
			}

			DatasmithActor = InActor;
		}

		public void LinkToNode(RhinoSceneHierarchyNode InLinkedNode)
		{
			LinkedNode = InLinkedNode;
			InLinkedNode.bIsInstanceDefinition = true;
		}

		public RhinoSceneHierarchyNode AddChild(RhinoSceneHierarchyNodeInfo InNodeInfo)
		{
			RhinoSceneHierarchyNode ChildNode = new RhinoSceneHierarchyNode(this, InNodeInfo);
			Children.Add(ChildNode);

			return ChildNode;
		}

		public int GetChildrenCount(bool bImmediateChildrenOnly = true)
		{
			int DescendantCount = 0;

			if(!bImmediateChildrenOnly)
			{
				for(int ChildIndex = 0, ChildrenCount = Children.Count; ChildIndex < ChildrenCount; ++ChildIndex)
				{
					DescendantCount += Children[ChildIndex].GetChildrenCount(/*bImmediateChildrenOnly=*/false);
				}
			}

			return DescendantCount + Children.Count;
		}

		public RhinoSceneHierarchyNode GetChild(int ChildIndex)
		{
			return Children[ChildIndex];
		}

		public bool RemoveChild(RhinoSceneHierarchyNode ChildNode)
		{
			return Children.Remove(ChildNode);
		}

		//IEnumerable interface begin
		public IEnumerator<RhinoSceneHierarchyNode> GetEnumerator()
		{
			yield return this;

			foreach (var Child in Children)
			{
				var ChildEnumerator = Child.GetEnumerator();
				while(ChildEnumerator.MoveNext())
				{
					yield return ChildEnumerator.Current;
				}
			}
		}

		IEnumerator IEnumerable.GetEnumerator()
		{
			return GetEnumerator();
		}
		//IEnumerable interface end
	}

	public class RhinoMaterialInfo
	{
		public Material RhinoMaterial { get; private set; }
		public string Name { get; private set; }
		public string Label { get; private set; }

		public RhinoMaterialInfo(Material InRhinoMaterial, string InName, string InLabel)
		{
			RhinoMaterial = InRhinoMaterial;
			Name = InName;
			Label = InLabel;
		}
	}

	public class RhinoTextureInfo
	{
		public Texture RhinoTexture { get; private set; }
		public string Name { get; private set; }
		public string Label { get { return Name; } }
		public string FilePath { get; private set; }

		public RhinoTextureInfo(Texture InRhinoTexture, string InName, string InFilePath)
		{
			RhinoTexture = InRhinoTexture;
			Name = InName;
			FilePath = InFilePath;
		}

		public bool IsSupported()
		{
			return (RhinoTexture.Enabled 
				&& (RhinoTexture.TextureType == TextureType.Bitmap 
					|| RhinoTexture.TextureType == TextureType.Bump 
					|| RhinoTexture.TextureType == TextureType.Transparency));
		}
	}

	public class DatasmithMeshInfo
	{
		public List<Mesh> RhinoMeshes { get; private set; }
		public Vector3d PivotOffset { get; private set; }
		public List<int> MaterialIndices { get; private set; }
		public string Name { get; private set; }
		public string Label { get; private set; }

		public DatasmithMeshInfo(List<Mesh> InRhinoMeshes, Vector3d InPivotOffset, List<int> InMaterialIndexes, string InName, string InLabel)
		{
			RhinoMeshes = InRhinoMeshes;
			PivotOffset = InPivotOffset;
			MaterialIndices = InMaterialIndexes;
			Name = InName;
			Label = InLabel;
		}

		public DatasmithMeshInfo(Mesh InRhinoMesh, Vector3d InPivotOffset, int InMaterialIndex, string InName, string InLabel)
			: this(new List<Mesh> { InRhinoMesh }, InPivotOffset, new List<int> { InMaterialIndex }, InName, InLabel)
		{
		}
	}


	public class DatasmithRhinoSceneParser
	{
		public RhinoDoc RhinoDocument { get; private set; }
		public Rhino.FileIO.FileWriteOptions ExportOptions { get; private set; }

		public RhinoSceneHierarchyNode SceneRoot = new RhinoSceneHierarchyNode(/*bInIsInstanceDefinition=*/false);
		public Dictionary<InstanceDefinition, RhinoSceneHierarchyNode> InstanceDefinitionHierarchyNodeDictionary = new Dictionary<InstanceDefinition, RhinoSceneHierarchyNode>();
		public Dictionary<Guid, RhinoSceneHierarchyNode> GuidToHierarchyNodeDictionary = new Dictionary<Guid, RhinoSceneHierarchyNode>();
		public Dictionary<Guid, DatasmithMeshInfo> ObjectIdToMeshInfoDictionary = new Dictionary<Guid, DatasmithMeshInfo>();
		public Dictionary<string, RhinoMaterialInfo> MaterialHashToMaterialInfo = new Dictionary<string, RhinoMaterialInfo>();
		public Dictionary<string, RhinoTextureInfo> TextureHashToTextureInfo = new Dictionary<string, RhinoTextureInfo>();

		private Dictionary<int, string> MaterialIndexToMaterialHashDictionary = new Dictionary<int, string>();
		private Dictionary<Guid, string> TextureIdToTextureHash = new Dictionary<Guid, string>();
		private List<string> GroupNameList = new List<string>();
		private FUniqueNameGenerator ActorLabelGenerator = new FUniqueNameGenerator();
		private FUniqueNameGenerator MaterialLabelGenerator = new FUniqueNameGenerator();
		private FUniqueNameGenerator TextureLabelGenerator = new FUniqueNameGenerator();

		public DatasmithRhinoSceneParser(RhinoDoc InDoc, Rhino.FileIO.FileWriteOptions InOptions)
		{
			RhinoDocument = InDoc;
			ExportOptions = InOptions;
		}

		public void ParseDocument()
		{
			FDatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress(0.33f);
			ParseGroupNames();
			FDatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress(0.66f);
			ParseRhinoHierarchy();
			FDatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress(1f);
			ParseRhinoMeshes();
		}

		public RhinoMaterialInfo GetMaterialInfoFromMaterialIndex(int MaterialIndex)
		{
			if(MaterialIndexToMaterialHashDictionary.TryGetValue(MaterialIndex, out string MaterialHash))
			{
				if(MaterialHashToMaterialInfo.TryGetValue(MaterialHash, out RhinoMaterialInfo MaterialInfo))
				{
					return MaterialInfo;
				}
			}

			return null;
		}

		public RhinoTextureInfo GetTextureInfoFromRhinoTexture(Guid TextureId)
		{
			if (TextureIdToTextureHash.TryGetValue(TextureId, out string TextureHash))
			{
				if (TextureHashToTextureInfo.TryGetValue(TextureHash, out RhinoTextureInfo TextureInfo))
				{
					return TextureInfo;
				}
			}

			return null;
		}

		private void ParseRhinoHierarchy()
		{
			foreach(var CurrentLayer in RhinoDocument.Layers)
			{
				//Only add Layers directly under root, the recursion will do the rest.
				if (CurrentLayer.ParentLayerId == Guid.Empty)
				{
					RecursivelyParseLayerHierarchy(CurrentLayer, SceneRoot);
				}
			}
		}

		private void RecursivelyParseLayerHierarchy(Layer CurrentLayer, RhinoSceneHierarchyNode ParentNode)
		{
			if(!CurrentLayer.IsVisible)
			{
				return;
			}

			int MaterialIndex = CurrentLayer.RenderMaterialIndex;
			Transform ParentTransform = ParentNode.bIsRoot ? ExportOptions.Xform : ParentNode.Info.WorldTransform;
			RhinoSceneHierarchyNodeInfo NodeInfo = GenerateNodeInfo(CurrentLayer, ParentNode.bIsInstanceDefinition, MaterialIndex, ParentTransform);
			RhinoSceneHierarchyNode CurrentNode = ParentNode.AddChild(NodeInfo);
			GuidToHierarchyNodeDictionary.Add(CurrentLayer.Id, CurrentNode);
			AddMaterialIndexMapping(CurrentLayer.RenderMaterialIndex);

			RhinoObject[] ObjectsInLayer = RhinoDocument.Objects.FindByLayer(CurrentLayer);
			RecursivelyParseObjectInstance(ObjectsInLayer, CurrentNode);

			Layer[] ChildrenLayer = CurrentLayer.GetChildren();
			if(ChildrenLayer != null)
			{
				foreach (var ChildLayer in ChildrenLayer)
				{
					RecursivelyParseLayerHierarchy(ChildLayer, CurrentNode);
				}
			}

			if (CurrentNode.GetChildrenCount() == 0) 
			{
				// This layer is empty, remove it.
				ParentNode.RemoveChild(CurrentNode);
			}
		}

		private void RecursivelyParseObjectInstance(RhinoObject[] InObjects, RhinoSceneHierarchyNode ParentNode)
		{
			foreach (RhinoObject CurrentObject in InObjects)
			{
				if (CurrentObject == null
					|| IsObjectIgnoredBySelection(CurrentObject, ParentNode)
					|| IsUnsupportedObject(CurrentObject))
				{
					// Skip the object.
					continue;
				}

				RhinoSceneHierarchyNode DefinitionRootNode = null;
				if (CurrentObject.ObjectType == ObjectType.InstanceReference)
				{
					InstanceObject CurrentInstance = CurrentObject as InstanceObject;
					DefinitionRootNode = GetOrCreateDefinitionRootNode(CurrentInstance.InstanceDefinition);

					if (DefinitionRootNode.GetChildrenCount() == 0)
					{
						// Don't instantiate empty definitions.
						continue;
					}
				}

				int MaterialIndex = GetObjectMaterialIndex(CurrentObject, ParentNode.Info);
				RhinoSceneHierarchyNodeInfo ObjectNodeInfo = GenerateNodeInfo(CurrentObject, ParentNode.bIsInstanceDefinition, MaterialIndex, ParentNode.Info.WorldTransform);
				RhinoSceneHierarchyNode ObjectNode = ParentNode.AddChild(ObjectNodeInfo);
				GuidToHierarchyNodeDictionary.Add(CurrentObject.Id, ObjectNode);
				AddObjectMaterialReference(CurrentObject, MaterialIndex);

				if (DefinitionRootNode != null)
				{
					InstanciateDefinition(ObjectNode, DefinitionRootNode);
				}
			}
		}

		private bool IsObjectIgnoredBySelection(RhinoObject InObject, RhinoSceneHierarchyNode ParentNode)
		{
			return !ParentNode.bIsInstanceDefinition
					&& ExportOptions.WriteSelectedObjectsOnly
					&& InObject.IsSelected(/*checkSubObjects=*/true) == 0;
		}

		private static bool IsUnsupportedObject(RhinoObject InObject)
		{
			// Geometry objects without meshes are currently not supported.
			return InObject.ComponentType == ModelComponentType.ModelGeometry 
				&& !InObject.IsMeshable(MeshType.Render);
		}

		private void InstanciateDefinition(RhinoSceneHierarchyNode ParentNode, RhinoSceneHierarchyNode DefinitionNode)
		{
			ParentNode.LinkToNode(DefinitionNode);

			for (int ChildIndex = 0; ChildIndex < DefinitionNode.GetChildrenCount(); ++ChildIndex)
			{
				RhinoSceneHierarchyNode DefinitionChildNode = DefinitionNode.GetChild(ChildIndex);
				int MaterialIndex = GetObjectMaterialIndex(DefinitionChildNode.Info.RhinoModelComponent as RhinoObject, ParentNode.Info);
				RhinoSceneHierarchyNodeInfo ChildNodeInfo = GenerateInstanceNodeInfo(ParentNode.Info, DefinitionChildNode.Info, MaterialIndex);
				RhinoSceneHierarchyNode InstanceChildNode = ParentNode.AddChild(ChildNodeInfo);

				InstanciateDefinition(InstanceChildNode, DefinitionChildNode);
			}
		}

		private RhinoSceneHierarchyNode GetOrCreateDefinitionRootNode(InstanceDefinition InInstanceDefinition)
		{
			RhinoSceneHierarchyNode InstanceRootNode;

			//If a hierarchy node does not exist for this instance definition, create one.
			if (!InstanceDefinitionHierarchyNodeDictionary.TryGetValue(InInstanceDefinition, out InstanceRootNode))
			{
				const bool bIsInstanceDefinition = true;
				const int MaterialIndex = -1;

				RhinoSceneHierarchyNodeInfo DefinitionNodeInfo = GenerateNodeInfo(InInstanceDefinition, bIsInstanceDefinition, MaterialIndex, Transform.Identity);
				InstanceRootNode = new RhinoSceneHierarchyNode(/*bInIsInstanceDefinition=*/true, DefinitionNodeInfo);
				InstanceDefinitionHierarchyNodeDictionary.Add(InInstanceDefinition, InstanceRootNode);

				RhinoObject[] InstanceObjects = InInstanceDefinition.GetObjects();
				RecursivelyParseObjectInstance(InstanceObjects, InstanceRootNode);
			}

			return InstanceRootNode;
		}

		/// <summary>
		/// Creates a new hierarchy node info by using the DefinitionNodeInfo as a base, and using InstanceParentNodeInfo for creating a unique name. Used when instancing block definitions.
		/// </summary>
		/// <param name="InstanceParentNodeInfo"></param>
		/// <param name="DefinitionNodeInfo"></param>
		/// <param name="MaterialIndex"></param>
		/// <returns></returns>
		private RhinoSceneHierarchyNodeInfo GenerateInstanceNodeInfo(RhinoSceneHierarchyNodeInfo InstanceParentNodeInfo, RhinoSceneHierarchyNodeInfo DefinitionNodeInfo, int MaterialIndex)
		{
			string Name = string.Format("{0}_{1}", InstanceParentNodeInfo.Name, DefinitionNodeInfo.Name);
			string Label = ActorLabelGenerator.GenerateUniqueNameFromBaseName(DefinitionNodeInfo.Label);
			List<string> Tags = new List<string>(DefinitionNodeInfo.Tags);
			bool bOverrideMaterial = DefinitionNodeInfo.MaterialIndex != MaterialIndex;

			return new RhinoSceneHierarchyNodeInfo(DefinitionNodeInfo.RhinoModelComponent, Name, Label, Tags, MaterialIndex, bOverrideMaterial, InstanceParentNodeInfo.WorldTransform);
		}

		/// <summary>
		/// Creates a new hierarchy node info for a given Rhino Model Component, used to determine names and labels as well as linking.
		/// </summary>
		/// <param name="InModelComponent"></param>
		/// <param name="ParentNode"></param>
		/// <param name="MaterialIndex"></param>
		/// <param name="ParentTransform"></param>
		/// <returns></returns>
		private RhinoSceneHierarchyNodeInfo GenerateNodeInfo(ModelComponent InModelComponent, bool bIsInstanceDefinition, int MaterialIndex, Transform ParentTransform)
		{
			string Name = InModelComponent.Id.ToString();
			string Label = bIsInstanceDefinition
				? FUniqueNameGenerator.GetTargetName(InModelComponent)
				: ActorLabelGenerator.GenerateUniqueName(InModelComponent);
			List<string> Tags = GetTags(InModelComponent);
			const bool bOverrideMaterial = false;

			return new RhinoSceneHierarchyNodeInfo(InModelComponent, Name, Label, Tags, MaterialIndex, bOverrideMaterial, ParentTransform);
		}

		private int GetObjectMaterialIndex(RhinoObject InRhinoObject, RhinoSceneHierarchyNodeInfo ParentNodeInfo)
		{
			ObjectMaterialSource MaterialSource = InRhinoObject.Attributes.MaterialSource;

			if(MaterialSource == ObjectMaterialSource.MaterialFromParent)
			{
				// Special use-case for block instances with the source material set to "Parent".
				// The material source in this case is actually the layer in which the definition objects are.
				if(InRhinoObject is InstanceObject)
				{
					InstanceObject Instance = InRhinoObject as InstanceObject;
					RhinoObject[] DefinitionObjects = Instance.InstanceDefinition.GetObjects();
					if(DefinitionObjects != null && DefinitionObjects.Length > 0)
					{
						Layer ObjectLayer = RhinoDocument.Layers.FindIndex(InRhinoObject.Attributes.LayerIndex);
						return ObjectLayer.RenderMaterialIndex;
					}
				}
				else if(ParentNodeInfo == null)
				{
					MaterialSource = ObjectMaterialSource.MaterialFromLayer;
				}
			}

			switch(MaterialSource)
			{
				case ObjectMaterialSource.MaterialFromLayer:
					Layer ObjectLayer = RhinoDocument.Layers.FindIndex(InRhinoObject.Attributes.LayerIndex);
					return ObjectLayer.RenderMaterialIndex;
				case ObjectMaterialSource.MaterialFromParent:
					return ParentNodeInfo.MaterialIndex;
				case ObjectMaterialSource.MaterialFromObject:
				default:
					return InRhinoObject.Attributes.MaterialIndex;
			}
		}

		private void AddObjectMaterialReference(RhinoObject InObject, int  MaterialIndex)
		{
			if (InObject.ObjectType == ObjectType.Brep)
			{
				BrepObject InBrepObject = InObject as BrepObject;
				if(InBrepObject.HasSubobjectMaterials)
				{
					RhinoObject[] SubObjects = InBrepObject.GetSubObjects();
					foreach (ComponentIndex CurrentIndex in InBrepObject.SubobjectMaterialComponents)
					{
						int SubObjectMaterialIndex = SubObjects[CurrentIndex.Index].Attributes.MaterialIndex;
						AddMaterialIndexMapping(SubObjectMaterialIndex);
					}
				}
			}

			AddMaterialIndexMapping(MaterialIndex);
		}

		private void AddMaterialIndexMapping(int MaterialIndex)
		{
			if(!MaterialIndexToMaterialHashDictionary.ContainsKey(MaterialIndex))
			{
				Material IndexedMaterial = MaterialIndex == -1
					? IndexedMaterial = Material.DefaultMaterial
					: RhinoDocument.Materials.FindIndex(MaterialIndex);

				string MaterialHash = FDatasmithRhinoUtilities.GetMaterialHash(IndexedMaterial);
				MaterialIndexToMaterialHashDictionary.Add(MaterialIndex, MaterialHash);

				AddMaterialHashMapping(MaterialHash, IndexedMaterial);
			}
		}

		private void AddMaterialHashMapping(string MaterialHash, Material RhinoMaterial)
		{
			if (!MaterialHashToMaterialInfo.ContainsKey(MaterialHash))
			{
				string MaterialLabel = MaterialLabelGenerator.GenerateUniqueName(RhinoMaterial);
				string MaterialName = FDatasmithFacadeElement.GetStringHash(MaterialLabel);

				MaterialHashToMaterialInfo.Add(MaterialHash, new RhinoMaterialInfo(RhinoMaterial, MaterialName, MaterialLabel));

				Texture[] MaterialTextures = RhinoMaterial.GetTextures();
				for (int TextureIndex = 0; TextureIndex < MaterialTextures.Length; ++TextureIndex)
				{
					Texture RhinoTexture = MaterialTextures[TextureIndex];
					if(RhinoTexture != null)
					{
						string TextureHash = FDatasmithRhinoUtilities.GetTextureHash(RhinoTexture);
						AddTextureHashMapping(TextureHash, RhinoTexture);
					}
				}
			}
		}

		private void AddTextureHashMapping(string TextureHash, Texture RhinoTexture)
		{
			if (!TextureIdToTextureHash.ContainsKey(RhinoTexture.Id))
			{
				TextureIdToTextureHash.Add(RhinoTexture.Id, TextureHash);

				if (!TextureHashToTextureInfo.ContainsKey(TextureHash))
				{
					string TextureName, TexturePath;
					FDatasmithRhinoUtilities.GetRhinoTextureNameAndPath(RhinoTexture, out TextureName, out TexturePath);
					TextureName = TextureLabelGenerator.GenerateUniqueNameFromBaseName(TextureName);

					TextureHashToTextureInfo.Add(TextureHash, new RhinoTextureInfo(RhinoTexture, TextureName, TexturePath));
				}
			}
		}

		private List<string> GetTags(ModelComponent InModelComponent)
		{
			List<string> NodeTags = new List<string>();
			NodeTags.Add(string.Format("Rhino.ID: {0}", InModelComponent.Id));

			string ComponentTypeString = FUniqueNameGenerator.GetDefaultTypeName(InModelComponent);
			NodeTags.Add(string.Format("Rhino.Entity.Type: {0}", ComponentTypeString));

			//Add the groups this object belongs to.
			RhinoObject InRhinoObject = InModelComponent as RhinoObject;
			if(InRhinoObject != null && InRhinoObject.GroupCount > 0)
			{
				int[] GroupIndices = InRhinoObject.GetGroupList();
				for(int GroupArrayIndex = 0; GroupArrayIndex < GroupIndices.Length; ++GroupArrayIndex)
				{
					string GroupName = GroupNameList[GroupIndices[GroupArrayIndex]];
					if(GroupName != null)
					{
						NodeTags.Add(GroupName);
					}
				}
			}

			return NodeTags;
		}

		void ParseGroupNames()
		{
			GroupNameList.Capacity = RhinoDocument.Groups.Count;

			foreach (Group CurrentGroup in RhinoDocument.Groups)
			{
				int GroupIndex = CurrentGroup.Index;
				string GroupName = CurrentGroup.Name == null || CurrentGroup.Name == ""
					? string.Format("Group{0}", GroupIndex)
					: CurrentGroup.Name;

				GroupNameList.Insert(GroupIndex, GroupName);
			}
		}

		private void ParseRhinoMeshes()
		{
			HashSet<RhinoObject> DocObjects = CollectExportedRhinoObjects(RhinoDocument, InstanceDefinitionHierarchyNodeDictionary.Values);

			// Make sure all render meshes are generated before attempting to export them.
			RhinoObject.GetRenderMeshes(DocObjects, /*okToCreate=*/true, /*returnAllObjects*/false);

			foreach (RhinoObject CurrentObject in DocObjects)
			{
				Mesh[] RenderMeshes = CurrentObject.GetMeshes(MeshType.Render);

				if (RenderMeshes != null && RenderMeshes.Length > 0)
				{
					List<Mesh> ExportedMeshes = new List<Mesh>(RenderMeshes);
					List<ObjectAttributes> MeshesAttributes = new List<ObjectAttributes>(RenderMeshes.Length);

					BrepObject CurrentBrep = (CurrentObject as BrepObject);
					if (CurrentBrep != null && CurrentBrep.HasSubobjectMaterials)
					{
						RhinoObject[] SubObjects = CurrentBrep.GetSubObjects();

						for (int SubObjectIndex = 0; SubObjectIndex < SubObjects.Length; ++SubObjectIndex)
						{
							MeshesAttributes.Add(SubObjects[SubObjectIndex].Attributes);
						}
					}
					else
					{
						for (int RenderMeshIndex = 0; RenderMeshIndex < RenderMeshes.Length; ++RenderMeshIndex)
						{
							MeshesAttributes.Add(CurrentObject.Attributes);
						}
					}

					DatasmithMeshInfo MeshInfo = GenerateMeshInfo(CurrentObject.Id, ExportedMeshes, MeshesAttributes);
					if (MeshInfo != null)
					{
						ObjectIdToMeshInfoDictionary[CurrentObject.Id] = MeshInfo;
					}
				}
			}
		}

		private static HashSet<RhinoObject> CollectExportedRhinoObjects(RhinoDoc RhinoDocument, IEnumerable<RhinoSceneHierarchyNode> InstancesHierarchies)
		{
			ObjectEnumeratorSettings Settings = new ObjectEnumeratorSettings();
			Settings.HiddenObjects = false;
			//First get all non-instance directly in the scene
			Settings.ObjectTypeFilter = ObjectType.AnyObject ^ (ObjectType.InstanceDefinition | ObjectType.InstanceReference);
			Settings.ReferenceObjects = true;

			//Calling GetObjectList instead of directly iterating through RhinoDocument.Objects as it seems that the ObjectTable may sometimes contain uninitialized RhinoObjects.
			HashSet<RhinoObject> ExportedObjects = new HashSet<RhinoObject>(RhinoDocument.Objects.GetObjectList(Settings));

			// Collect objects that are referenced inside instance definitions(blocks).
			// We do this because if we were to call RhinoObject.MeshObjects() on an Instance object it would create a single mesh merging all the instance's children.
			foreach (var CurrentInstanceHierarchy in InstancesHierarchies)
			{
				foreach (RhinoSceneHierarchyNode HierarchyNode in CurrentInstanceHierarchy)
				{
					if (!HierarchyNode.bIsRoot && HierarchyNode.Info.bHasRhinoObject && !(HierarchyNode.Info.RhinoModelComponent is InstanceObject))
					{
						ExportedObjects.Add(HierarchyNode.Info.RhinoModelComponent as RhinoObject);
					}
				}
			}

			return ExportedObjects;
		}

		private DatasmithMeshInfo GenerateMeshInfo(Guid ObjectID, List<Mesh> Meshes, List<ObjectAttributes> Attributes)
		{
			DatasmithMeshInfo MeshInfo = null;

			if (GuidToHierarchyNodeDictionary.TryGetValue(ObjectID, out RhinoSceneHierarchyNode HierarchyNode))
			{
				Vector3d PivotOffset = FDatasmithRhinoMeshExporter.CenterMeshesOnPivot(Meshes);
				List<int> MaterialIndices = new List<int>(Attributes.Count);
				for (int AttributeIndex = 0; AttributeIndex < Attributes.Count; ++AttributeIndex)
				{
					int MaterialIndex = GetMaterialIndexFromAttributes(HierarchyNode, Attributes[AttributeIndex]);
					MaterialIndices.Add(MaterialIndex);
				}

				string Name = FDatasmithFacadeElement.GetStringHash("M:" + HierarchyNode.Info.Name);
				string Label = HierarchyNode.Info.Label;

				MeshInfo = new DatasmithMeshInfo(Meshes, PivotOffset, MaterialIndices, Name, Label);
			}
			else
			{
				RhinoApp.WriteLine("Could not find the corresponding hierarchy node for the object ID: {0}", ObjectID);
			}

			return MeshInfo;
		}

		private int GetMaterialIndexFromAttributes(RhinoSceneHierarchyNode HierarchyNode, ObjectAttributes Attributes)
		{
			if (Attributes.MaterialSource == ObjectMaterialSource.MaterialFromObject)
			{
				return Attributes.MaterialIndex;
			}
			else if (HierarchyNode.Info != null)
			{
				return HierarchyNode.Info.MaterialIndex;
			}

			// Return default material index
			return -1;
		}
	}
}