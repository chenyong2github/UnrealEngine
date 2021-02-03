// Copyright Epic Games, Inc. All Rights Reserved.

using DatasmithRhino.Utils;
using Rhino;
using Rhino.Display;
using Rhino.DocObjects;
using Rhino.Geometry;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace DatasmithRhino
{
	public enum DirectLinkSynchronizationStatus
	{
		None = 0,
		Created = 1 << 0,
		Modified = 1 << 1,
		Deleted = 1 << 2,
		Synced = 1 << 3,
	}

	public abstract class DatasmithInfoBase
	{
		public Rhino.Runtime.CommonObject RhinoObject { get; private set; }
		public string Name { get; private set; }
		public string Label { get; private set; }

		public DirectLinkSynchronizationStatus DirectLinkStatus { get; private set; } = DirectLinkSynchronizationStatus.Created;
		public FDatasmithFacadeElement ExportedElement { get; private set; } = null;

		public DatasmithInfoBase(Rhino.Runtime.CommonObject InRhinoObject, string InName, string InLabel)
		{
			RhinoObject = InRhinoObject;
			Name = InName;
			Label = InLabel;
		}

		public void SetExportedElement(FDatasmithFacadeElement InExportedElement)
		{
			System.Diagnostics.Debug.Assert(ExportedElement == null, "Exported element cannot override existing exported element. A new DatasmithInfoBase must be created and the current one must be deleted");
			ExportedElement = InExportedElement;
			DirectLinkStatus = DirectLinkSynchronizationStatus.Synced;
		}
	}

	public class DatasmithActorInfo : DatasmithInfoBase
	{
		public bool bHasRhinoObject { get { return RhinoModelComponent is RhinoObject; } }
		public bool bHasRhinoLayer { get { return RhinoModelComponent is Layer; } }
		public ModelComponent RhinoModelComponent { get { return RhinoObject as ModelComponent; } }
		public Transform WorldTransform { get; private set; } = Transform.Identity;
		public int MaterialIndex { get; private set; } = -1;
		public bool bOverrideMaterial { get; private set; } = false;
		public List<string> Tags { get; private set; } = new List<string>();

		public bool bIsRoot { get; private set; } = true;
		public bool bIsInstanceDefinition { get; private set; } = false;
		public DatasmithActorInfo Parent { get; private set; } = null;
		public DatasmithActorInfo LinkedNode { get; private set; } = null;
		public FDatasmithFacadeActor DatasmithActor { get { return ExportedElement as FDatasmithFacadeActor; } }
		private List<DatasmithActorInfo> Children = new List<DatasmithActorInfo>();
		public HashSet<int> LayerIndices { get; private set; } = new HashSet<int>();
		public Layer VisibilityLayer { get; private set; } = null;

		public bool bIsVisible {
			get {
				if (bHasRhinoLayer)
				{
					// This recursion ensure that only layer actors with an actual exported object under them are considered visible.
					// ie. Layers containing no mesh are not visible.
					return (RhinoObject as Layer).IsVisible
						&& Children.Any(Child => Child.bIsVisible);
				}
				else if (bIsRoot)
				{
					return true;
				}

				return (VisibilityLayer == null || VisibilityLayer.IsVisible)
					&& (LinkedNode == null || LinkedNode.bIsVisible);
			}
		}

		public DatasmithActorInfo(Transform NodeTransform, string InName, string InLabel)
			: base(null, InName, InLabel)
		{
			WorldTransform = NodeTransform;
		}

		public DatasmithActorInfo(ModelComponent InModelComponent, string InName, string InLabel, List<string> InTags, int InMaterialIndex, bool bInOverrideMaterial, Layer InVisibilityLayer)
			: base(InModelComponent, InName, InLabel)
		{
			bIsInstanceDefinition = RhinoModelComponent is InstanceDefinition;
			Tags = InTags;
			MaterialIndex = InMaterialIndex;
			bOverrideMaterial = bInOverrideMaterial;
			VisibilityLayer = InVisibilityLayer;

			WorldTransform = DatasmithRhinoUtilities.GetModelComponentTransform(RhinoModelComponent);
		}

		private void SetParent(DatasmithActorInfo InParent)
		{
			System.Diagnostics.Debug.Assert(Parent == null, "Overriding existing parent.");
			bIsRoot = false;
			Parent = InParent;
			bIsInstanceDefinition = InParent.bIsInstanceDefinition;
			WorldTransform = Transform.Multiply(InParent.WorldTransform, WorldTransform);
		}

		public void LinkToNode(DatasmithActorInfo InLinkedNode)
		{
			System.Diagnostics.Debug.Assert(InLinkedNode.bIsInstanceDefinition, "Trying to create an instance from a node belonging in the root tree");
			LinkedNode = InLinkedNode;
			LayerIndices.UnionWith(InLinkedNode.LayerIndices);
		}

		public void AddChild(DatasmithActorInfo ChildHierarchyNodeInfo)
		{
			ChildHierarchyNodeInfo.SetParent(this);
			ChildHierarchyNodeInfo.LayerIndices.UnionWith(LayerIndices);
			Children.Add(ChildHierarchyNodeInfo);
		}

		public void AddChild(DatasmithActorInfo ChildHierarchyNodeInfo, int LayerIndex)
		{
			AddChild(ChildHierarchyNodeInfo);
			ChildHierarchyNodeInfo.LayerIndices.Add(LayerIndex);
		}

		public void AddChild(DatasmithActorInfo ChildHierarchyNodeInfo, IEnumerable<int> LayerIndices)
		{
			AddChild(ChildHierarchyNodeInfo);
			ChildHierarchyNodeInfo.LayerIndices.UnionWith(LayerIndices);
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

		public DatasmithActorInfo GetChild(int ChildIndex)
		{
			System.Diagnostics.Debug.Assert(ChildIndex >= 0 && ChildIndex < Children.Count, "DatasmithActorInfo::GetChild() - Invalid child index");
			return Children[ChildIndex];
		}

		public bool RemoveChild(DatasmithActorInfo ChildNode)
		{
			return Children.Remove(ChildNode);
		}

		/// <summary>
		/// Custom enumerator implementation, we might want to enumerate with or without hidden actors.
		/// </summary>
		/// <param name="bIncludeHidden"></param>
		/// <returns></returns>
		public IEnumerable<DatasmithActorInfo> GetEnumerator(bool bIncludeHidden)
		{
			if (bIncludeHidden || bIsVisible)
			{
				yield return this;

				foreach (var Child in Children)
				{
					foreach (var ChildEnumValue in Child.GetEnumerator(bIncludeHidden))
					{
						yield return ChildEnumValue;
					}
				}
			}
		}
	}

	public class DatasmithMaterialInfo : DatasmithInfoBase
	{
		public Material RhinoMaterial { get { return RhinoObject as Material; } }
		public FDatasmithFacadeUEPbrMaterial ExportedMaterial { get { return ExportedElement as FDatasmithFacadeUEPbrMaterial; } }

		public DatasmithMaterialInfo(Material InRhinoMaterial, string InName, string InLabel)
			: base(InRhinoMaterial, InName, InLabel)
		{
		}
	}

	public class DatasmithTextureInfo : DatasmithInfoBase
	{
		public Texture RhinoTexture { get { return RhinoObject as Texture; } }
		public FDatasmithFacadeTexture ExportedTexture { get { return ExportedElement as FDatasmithFacadeTexture; } }
		public string FilePath { get; private set; }

		public DatasmithTextureInfo(Texture InRhinoTexture, string InName, string InFilePath)
			: base(InRhinoTexture, FDatasmithFacadeElement.GetStringHash(InName), InName)
		{
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

	public class DatasmithMeshInfo : DatasmithInfoBase
	{
		public FDatasmithFacadeMeshElement ExportedMesh { get { return ExportedElement as FDatasmithFacadeMeshElement; } }

		public List<Mesh> RhinoMeshes { get; private set; }
		public Vector3d PivotOffset { get; private set; }
		public List<int> MaterialIndices { get; private set; }

		public DatasmithMeshInfo(List<Mesh> InRhinoMeshes, Vector3d InPivotOffset, List<int> InMaterialIndexes, string InName, string InLabel)
			: base(null, InName, InLabel)
		{
			RhinoMeshes = InRhinoMeshes;
			PivotOffset = InPivotOffset;
			MaterialIndices = InMaterialIndexes;
		}

		public DatasmithMeshInfo(Mesh InRhinoMesh, Vector3d InPivotOffset, int InMaterialIndex, string InName, string InLabel)
			: this(new List<Mesh> { InRhinoMesh }, InPivotOffset, new List<int> { InMaterialIndex }, InName, InLabel)
		{
		}
	}


	public class DatasmithRhinoExportContext
	{
		public RhinoDoc RhinoDocument { get => ExportOptions.RhinoDocument; }
		public DatasmithRhinoExportOptions ExportOptions { get; private set; }
		public bool bIsInWorksession {
			get {
				//Only check for worksession on Windows, the feature is not implemented on Mac and calling the API throws exception.
				return Environment.OSVersion.Platform == PlatformID.Win32NT 
					&& RhinoDocument.Worksession != null
					&& RhinoDocument.Worksession.ModelCount > 1;
			}
		}

		public DatasmithActorInfo SceneRoot = null;
		public Dictionary<InstanceDefinition, DatasmithActorInfo> InstanceDefinitionHierarchyNodeDictionary = new Dictionary<InstanceDefinition, DatasmithActorInfo>();
		public Dictionary<Guid, DatasmithActorInfo> GuidToHierarchyActorNodeDictionary = new Dictionary<Guid, DatasmithActorInfo>();
		public Dictionary<Guid, DatasmithMeshInfo> ObjectIdToMeshInfoDictionary = new Dictionary<Guid, DatasmithMeshInfo>();
		public Dictionary<string, DatasmithMaterialInfo> MaterialHashToMaterialInfo = new Dictionary<string, DatasmithMaterialInfo>();
		public Dictionary<string, DatasmithTextureInfo> TextureHashToTextureInfo = new Dictionary<string, DatasmithTextureInfo>();

		private Dictionary<int, string> MaterialIndexToMaterialHashDictionary = new Dictionary<int, string>();
		private Dictionary<Guid, string> TextureIdToTextureHash = new Dictionary<Guid, string>();
		private Dictionary<int, string> LayerIndexToLayerString = new Dictionary<int, string>();
		private Dictionary<int, HashSet<int>> LayerIndexToLayerIndexHierarchy = new Dictionary<int, HashSet<int>>();
		private List<string> GroupNameList = new List<string>();
		private DatasmithRhinoUniqueNameGenerator ActorLabelGenerator = new DatasmithRhinoUniqueNameGenerator();
		private DatasmithRhinoUniqueNameGenerator MaterialLabelGenerator = new DatasmithRhinoUniqueNameGenerator();
		private DatasmithRhinoUniqueNameGenerator TextureLabelGenerator = new DatasmithRhinoUniqueNameGenerator();
		private ViewportInfo ActiveViewportInfo;

		public DatasmithRhinoExportContext(DatasmithRhinoExportOptions InOptions)
		{
			ExportOptions = InOptions;
			SceneRoot = new DatasmithActorInfo(ExportOptions.Xform, "SceneRoot", "SceneRoot");
		}

		public void ParseDocument()
		{
			RhinoViewport ActiveViewport = RhinoDocument.Views.ActiveView?.ActiveViewport;
			//Update current active viewport.
			ActiveViewportInfo = ActiveViewport == null ? null : new ViewportInfo(ActiveViewport);

			DatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress(0.33f);
			ParseGroupNames();
			DatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress(0.66f);
			ParseRhinoHierarchy();
			DatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress(1f);
			ParseRhinoMeshes();
		}

		public DatasmithMaterialInfo GetMaterialInfoFromMaterialIndex(int MaterialIndex)
		{
			if(MaterialIndexToMaterialHashDictionary.TryGetValue(MaterialIndex, out string MaterialHash))
			{
				if(MaterialHashToMaterialInfo.TryGetValue(MaterialHash, out DatasmithMaterialInfo MaterialInfo))
				{
					return MaterialInfo;
				}
			}

			return null;
		}

		public DatasmithTextureInfo GetTextureInfoFromRhinoTexture(Guid TextureId)
		{
			if (TextureIdToTextureHash.TryGetValue(TextureId, out string TextureHash))
			{
				if (TextureHashToTextureInfo.TryGetValue(TextureHash, out DatasmithTextureInfo TextureInfo))
				{
					return TextureInfo;
				}
			}

			return null;
		}

		public string GetNodeLayerString(DatasmithActorInfo Node)
		{
			StringBuilder Buider = new StringBuilder();
			foreach (int LayerIndex in Node.LayerIndices)
			{
				if (LayerIndexToLayerString.TryGetValue(LayerIndex, out string LayerString))
				{
					if (Buider.Length == 0)
					{
						Buider.Append(LayerString);
					}
					else
					{
						Buider.Append("," + LayerString);
					}
				}
			}

			return Buider.ToString();
		}

		private void ParseRhinoHierarchy()
		{
			DatasmithActorInfo DummyDocumentNode = SceneRoot;

			if (bIsInWorksession)
			{
				// Rhino worksession adds dummy layers (with non consistent IDs) to all of the linked documents except the active one.
				// This can cause reimport inconsistencies when the active document changes, as dummies are shuffled and some may be created while others destroyed.
				// To address this issue we add our own "Current Document Layer" dummy layer, and use the file path as ActorElement name, 
				// that way there are no actors deleted and the Datasmith IDs stay consistent.
				string DummyLayerName = RhinoDocument.Path;
				string DummyLayerLabel = RhinoDocument.Name;
				const int DummyLayerIndex = -1;

				DummyDocumentNode = GenerateDummyNodeInfo(DummyLayerName, DummyLayerLabel);
				SceneRoot.AddChild(DummyDocumentNode, DummyLayerIndex);
				LayerIndexToLayerString.Add(DummyLayerIndex, BuildLayerString(DummyDocumentNode.Label, SceneRoot));
			}

			foreach (var CurrentLayer in RhinoDocument.Layers)
			{
				//Only add Layers directly under root, the recursion will do the rest.
				if (CurrentLayer.ParentLayerId == Guid.Empty)
				{
					DatasmithActorInfo ParentNode = bIsInWorksession && !CurrentLayer.IsReference
						? DummyDocumentNode
						: SceneRoot;

					RecursivelyParseLayerHierarchy(CurrentLayer, ParentNode);
				}
			}
		}

		private void RecursivelyParseLayerHierarchy(Layer CurrentLayer, DatasmithActorInfo ParentNode)
		{
			if ((ExportOptions.bSkipHidden && !CurrentLayer.IsVisible) || CurrentLayer.IsDeleted)
			{
				return;
			}

			int MaterialIndex = CurrentLayer.RenderMaterialIndex;
			DatasmithActorInfo ActorNodeInfo = GenerateNodeInfo(CurrentLayer, ParentNode.bIsInstanceDefinition, MaterialIndex, CurrentLayer);
			ParentNode.AddChild(ActorNodeInfo, CurrentLayer.Index);
			GuidToHierarchyActorNodeDictionary.Add(CurrentLayer.Id, ActorNodeInfo);
			LayerIndexToLayerString.Add(CurrentLayer.Index, BuildLayerString(CurrentLayer, ParentNode));
			AddMaterialIndexMapping(CurrentLayer.RenderMaterialIndex);

			RhinoObject[] ObjectsInLayer = RhinoDocument.Objects.FindByLayer(CurrentLayer);
			RecursivelyParseObjectInstance(ObjectsInLayer, ActorNodeInfo);

			Layer[] ChildrenLayer = CurrentLayer.GetChildren();
			if(ChildrenLayer != null)
			{
				foreach (var ChildLayer in ChildrenLayer)
				{
					RecursivelyParseLayerHierarchy(ChildLayer, ActorNodeInfo);
				}
			}
		}

		private string BuildLayerString(Layer CurrentLayer, DatasmithActorInfo ParentNode)
		{
			return BuildLayerString(DatasmithRhinoUniqueNameGenerator.GetTargetName(CurrentLayer), ParentNode);
		}

		private string BuildLayerString(string LayerName, DatasmithActorInfo ParentNode)
		{
			string CurrentLayerName = LayerName.Replace(',', '_');

			if (!ParentNode.bIsRoot)
			{
				Layer ParentLayer = ParentNode.RhinoModelComponent as Layer;
				// If ParentLayer is null, then the parent node is a dummy document layer and we are in a worksession.
				int ParentLayerIndex = ParentLayer == null ? -1 : ParentLayer.Index;

				if (LayerIndexToLayerString.TryGetValue(ParentLayerIndex, out string ParentLayerString))
				{
					return string.Format("{0}_{1}", ParentLayerString, CurrentLayerName);
				}
			}

			return CurrentLayerName;
		}

		private void RecursivelyParseObjectInstance(RhinoObject[] InObjects, DatasmithActorInfo ParentNode)
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

				DatasmithActorInfo DefinitionRootNode = null;
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

				bool bIsActorInInstanceDefinition = ParentNode.bIsRoot && ParentNode.bIsInstanceDefinition;
				Layer VisiblityLayer = bIsActorInInstanceDefinition
					? RhinoDocument.Layers.FindIndex(CurrentObject.Attributes.LayerIndex)
					: ParentNode.VisibilityLayer;

				int MaterialIndex = GetObjectMaterialIndex(CurrentObject, ParentNode);
				DatasmithActorInfo ObjectNodeInfo = GenerateNodeInfo(CurrentObject, ParentNode.bIsInstanceDefinition, MaterialIndex, VisiblityLayer);
				if (bIsActorInInstanceDefinition)
				{
					// The objects inside a Block definitions may be defined in a different layer than the one we are currently in.
					ParentNode.AddChild(ObjectNodeInfo, GetOrCreateLayerIndexHierarchy(CurrentObject.Attributes.LayerIndex));
				}
				else
				{
					ParentNode.AddChild(ObjectNodeInfo);
				}
				GuidToHierarchyActorNodeDictionary.Add(CurrentObject.Id, ObjectNodeInfo);
				AddObjectMaterialReference(CurrentObject, MaterialIndex);

				if (DefinitionRootNode != null)
				{
					InstanciateDefinition(ObjectNodeInfo, DefinitionRootNode);
				}
			}
		}

		private bool IsObjectIgnoredBySelection(RhinoObject InObject, DatasmithActorInfo ParentNode)
		{
			return !ParentNode.bIsInstanceDefinition
					&& ExportOptions.bWriteSelectedObjectsOnly
					&& InObject.IsSelected(/*checkSubObjects=*/true) == 0;
		}

		private bool IsUnsupportedObject(RhinoObject InObject)
		{
//Disabling obsolete warning as GetRenderPrimitiveList() is deprecated since Rhino5 but as of Rhino7 no alternative exists.
#pragma warning disable CS0612
			// Geometry objects without meshes are currently not supported, unless they are points.
			return InObject.ComponentType == ModelComponentType.ModelGeometry 
				&& !InObject.IsMeshable(MeshType.Render)
				&& InObject.ObjectType != ObjectType.Point
				&& (InObject.ObjectType != ObjectType.Curve || null == InObject.GetRenderPrimitiveList(ActiveViewportInfo, true));
#pragma warning restore CS0612
		}

		private void InstanciateDefinition(DatasmithActorInfo ParentNode, DatasmithActorInfo DefinitionNode)
		{
			ParentNode.LinkToNode(DefinitionNode);

			for (int ChildIndex = 0; ChildIndex < DefinitionNode.GetChildrenCount(); ++ChildIndex)
			{
				DatasmithActorInfo DefinitionChildNode = DefinitionNode.GetChild(ChildIndex);
				int MaterialIndex = GetObjectMaterialIndex(DefinitionChildNode.RhinoModelComponent as RhinoObject, ParentNode);
				DatasmithActorInfo InstanceChildNode = GenerateInstanceNodeInfo(ParentNode, DefinitionChildNode, MaterialIndex, ParentNode.VisibilityLayer);
				ParentNode.AddChild(InstanceChildNode);

				InstanciateDefinition(InstanceChildNode, DefinitionChildNode);
			}
		}

		private DatasmithActorInfo GetOrCreateDefinitionRootNode(InstanceDefinition InInstanceDefinition)
		{
			DatasmithActorInfo InstanceRootNode;

			//If a hierarchy node does not exist for this instance definition, create one.
			if (!InstanceDefinitionHierarchyNodeDictionary.TryGetValue(InInstanceDefinition, out InstanceRootNode))
			{
				const bool bIsInstanceDefinition = true;
				const int MaterialIndex = -1;
				const Layer NullVisibilityLayer = null;

				InstanceRootNode = GenerateNodeInfo(InInstanceDefinition, bIsInstanceDefinition, MaterialIndex, NullVisibilityLayer);
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
		private DatasmithActorInfo GenerateInstanceNodeInfo(DatasmithActorInfo InstanceParentNodeInfo, DatasmithActorInfo DefinitionNodeInfo, int MaterialIndex, Layer VisibilityLayer)
		{
			string Name = string.Format("{0}_{1}", InstanceParentNodeInfo.Name, DefinitionNodeInfo.Name);
			string Label = ActorLabelGenerator.GenerateUniqueNameFromBaseName(DefinitionNodeInfo.Label);
			List<string> Tags = new List<string>(DefinitionNodeInfo.Tags);
			bool bOverrideMaterial = DefinitionNodeInfo.MaterialIndex != MaterialIndex;

			return new DatasmithActorInfo(DefinitionNodeInfo.RhinoModelComponent, Name, Label, Tags, MaterialIndex, bOverrideMaterial, VisibilityLayer);
		}

		/// <summary>
		/// Creates a new hierarchy node info that is not represented by any ModelComponent, used to add an empty actor to the scene. 
		/// </summary>
		/// <param name="UniqueID"></param>
		/// <param name="TargetLabel"></param>
		/// <returns></returns>
		private DatasmithActorInfo GenerateDummyNodeInfo(string UniqueID, string TargetLabel)
		{
			string UniqueLabel = ActorLabelGenerator.GenerateUniqueNameFromBaseName(TargetLabel);

			return new DatasmithActorInfo(Transform.Identity, UniqueID, UniqueLabel);
		}

		/// <summary>
		/// Creates a new hierarchy node info for a given Rhino Model Component, used to determine names and labels as well as linking.
		/// </summary>
		/// <param name="InModelComponent"></param>
		/// <param name="ParentNode"></param>
		/// <param name="MaterialIndex"></param>
		/// <param name="ParentTransform"></param>
		/// <returns></returns>
		private DatasmithActorInfo GenerateNodeInfo(ModelComponent InModelComponent, bool bIsInstanceDefinition, int MaterialIndex, Layer VisiblityLayer)
		{
			string Name = GetModelComponentName(InModelComponent);
			string Label = bIsInstanceDefinition
				? DatasmithRhinoUniqueNameGenerator.GetTargetName(InModelComponent)
				: ActorLabelGenerator.GenerateUniqueName(InModelComponent);
			List<string> Tags = GetTags(InModelComponent);
			const bool bOverrideMaterial = false;

			return new DatasmithActorInfo(InModelComponent, Name, Label, Tags, MaterialIndex, bOverrideMaterial, VisiblityLayer);
		}

		/// <summary>
		/// Gets the unique datasmith element name for the given ModelComponent object.
		/// Some ModelComponent may have volatile IDs, it's important to use this function to ensure the name is based on some other attributes in those cases.
		/// </summary>
		/// <param name="InModelComponent"></param>
		/// <returns></returns>
		private string GetModelComponentName(ModelComponent InModelComponent)
		{
			Layer RhinoLayer = InModelComponent as Layer;

			if (bIsInWorksession && RhinoLayer != null && RhinoLayer.IsReference && RhinoLayer.ParentLayerId == Guid.Empty)
			{
				// This is a dummy layer added by Rhino representing the linked document.
				// Use the document absolute filepath as unique name to ensure consistency across worksessions.
				string DocumentFilePath = GetRhinoDocumentPathFromLayerName(RhinoLayer.Name);
				if (DocumentFilePath != null)
				{
					return DocumentFilePath;
				}
			}

			return InModelComponent.Id.ToString();
		}

		private string GetRhinoDocumentPathFromLayerName(string LayerName)
		{
			string[] DocumentPaths = RhinoDocument.Worksession.ModelPaths;

			if (DocumentPaths != null)
			{
				foreach (string CurrentPath in DocumentPaths)
				{
					if (CurrentPath.Contains(LayerName))
					{
						return CurrentPath;
					}
				}
			}

			return null;
		}

		private int GetObjectMaterialIndex(RhinoObject InRhinoObject, DatasmithActorInfo ParentNodeInfo)
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

				string MaterialHash = DatasmithRhinoUtilities.GetMaterialHash(IndexedMaterial);
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

				MaterialHashToMaterialInfo.Add(MaterialHash, new DatasmithMaterialInfo(RhinoMaterial, MaterialName, MaterialLabel));

				Texture[] MaterialTextures = RhinoMaterial.GetTextures();
				for (int TextureIndex = 0; TextureIndex < MaterialTextures.Length; ++TextureIndex)
				{
					Texture RhinoTexture = MaterialTextures[TextureIndex];
					if(RhinoTexture != null)
					{
						string TextureHash = DatasmithRhinoUtilities.GetTextureHash(RhinoTexture);
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
					if (DatasmithRhinoUtilities.GetRhinoTextureNameAndPath(RhinoTexture, out string TextureName, out string TexturePath))
					{
						TextureName = TextureLabelGenerator.GenerateUniqueNameFromBaseName(TextureName);
						TextureHashToTextureInfo.Add(TextureHash, new DatasmithTextureInfo(RhinoTexture, TextureName, TexturePath));
					}
				}
			}
		}

		private List<string> GetTags(ModelComponent InModelComponent)
		{
			List<string> NodeTags = new List<string>();
			NodeTags.Add(string.Format("Rhino.ID: {0}", InModelComponent.Id));

			string ComponentTypeString = DatasmithRhinoUniqueNameGenerator.GetDefaultTypeName(InModelComponent);
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

		private HashSet<int> GetOrCreateLayerIndexHierarchy(int ChildLayerIndex)
		{
			HashSet<int> LayerIndexHierarchy; 
			if (!LayerIndexToLayerIndexHierarchy.TryGetValue(ChildLayerIndex, out LayerIndexHierarchy))
			{
				LayerIndexHierarchy = new HashSet<int>();
				Layer CurrentLayer = RhinoDocument.Layers.FindIndex(ChildLayerIndex);

				if (CurrentLayer != null)
				{
					LayerIndexHierarchy.Add(CurrentLayer.Index);

					//Add all parent layers to the hierarchy set
					while (CurrentLayer.ParentLayerId != Guid.Empty)
					{
						CurrentLayer = RhinoDocument.Layers.FindId(CurrentLayer.ParentLayerId);
						LayerIndexHierarchy.Add(CurrentLayer.Index);
					}
				}

				//The above search is costly, to avoid doing it for every exported object, cache the result.
				LayerIndexToLayerIndexHierarchy.Add(ChildLayerIndex, LayerIndexHierarchy);
			}

			return LayerIndexHierarchy;
		}

		private void ParseRhinoMeshes()
		{
			HashSet<RhinoObject> DocObjects = CollectExportedRhinoObjects();

			// Make sure all render meshes are generated before attempting to export them.
			RhinoObject.GetRenderMeshes(DocObjects, /*okToCreate=*/true, /*returnAllObjects*/false);

			foreach (RhinoObject CurrentObject in DocObjects)
			{
				Mesh[] RenderMeshes = null;
				if (ActiveViewportInfo != null)
				{
//Disabling obsolete warning as GetRenderPrimitiveList() is deprecated since Rhino5 but as of Rhino7 no alternative exists.
#pragma warning disable CS0612
					RenderMeshes = CurrentObject.GetRenderPrimitiveList(ActiveViewportInfo, false)?.ToMeshArray();
#pragma warning restore CS0612
				}
				if (RenderMeshes == null)
				{
					RenderMeshes = CurrentObject.GetMeshes(MeshType.Render);
				}

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

		private HashSet<RhinoObject> CollectExportedRhinoObjects()
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
			foreach (var CurrentInstanceHierarchy in InstanceDefinitionHierarchyNodeDictionary.Values)
			{
				foreach (DatasmithActorInfo HierarchyNode in CurrentInstanceHierarchy.GetEnumerator(/*bIncludeHidden=*/true))
				{
					if (!HierarchyNode.bIsRoot && HierarchyNode.bHasRhinoObject && !(HierarchyNode.RhinoModelComponent is InstanceObject))
					{
						ExportedObjects.Add(HierarchyNode.RhinoModelComponent as RhinoObject);
					}
				}
			}

			return ExportedObjects;
		}

		private DatasmithMeshInfo GenerateMeshInfo(Guid ObjectID, List<Mesh> Meshes, List<ObjectAttributes> Attributes)
		{
			DatasmithMeshInfo MeshInfo = null;

			if (GuidToHierarchyActorNodeDictionary.TryGetValue(ObjectID, out DatasmithActorInfo HierarchyActorNode))
			{
				Vector3d PivotOffset = DatasmithRhinoUtilities.CenterMeshesOnPivot(Meshes);
				List<int> MaterialIndices = new List<int>(Attributes.Count);
				for (int AttributeIndex = 0; AttributeIndex < Attributes.Count; ++AttributeIndex)
				{
					int MaterialIndex = GetMaterialIndexFromAttributes(HierarchyActorNode, Attributes[AttributeIndex]);
					MaterialIndices.Add(MaterialIndex);
				}

				string Name = FDatasmithFacadeElement.GetStringHash("M:" + HierarchyActorNode.Name);
				string Label = HierarchyActorNode.Label;

				MeshInfo = new DatasmithMeshInfo(Meshes, PivotOffset, MaterialIndices, Name, Label);
			}
			else
			{
				RhinoApp.WriteLine("Could not find the corresponding hierarchy node for the object ID: {0}", ObjectID);
			}

			return MeshInfo;
		}

		private int GetMaterialIndexFromAttributes(DatasmithActorInfo HierarchyActorNode, ObjectAttributes Attributes)
		{
			if (Attributes.MaterialSource == ObjectMaterialSource.MaterialFromObject)
			{
				return Attributes.MaterialIndex;
			}

			//The parent material and layer material is baked into the HierarchyActorNode.
			return HierarchyActorNode.MaterialIndex;
		}
	}
}