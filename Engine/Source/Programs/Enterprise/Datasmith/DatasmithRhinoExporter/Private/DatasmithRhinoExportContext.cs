// Copyright Epic Games, Inc. All Rights Reserved.

using DatasmithRhino.Utils;
using Rhino;
using Rhino.Display;
using Rhino.DocObjects;
using Rhino.DocObjects.Tables;
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

	public abstract class DatasmithInfoBase<T> where T : DatasmithInfoBase<T>
	{
		public Rhino.Runtime.CommonObject RhinoCommonObject { get; protected set; }
		public string Name { get; private set; }
		public string Label { get; private set; }

		private DirectLinkSynchronizationStatus InternalDirectLinkStatus = DirectLinkSynchronizationStatus.Created;
		private DirectLinkSynchronizationStatus PreviousDirectLinkStatus { get; set; } = DirectLinkSynchronizationStatus.None;
		public virtual DirectLinkSynchronizationStatus DirectLinkStatus 
		{
			get => InternalDirectLinkStatus;
			set 
			{
				// We ignore the "modified" status if the current status is "created" or "deleted".
				// To "undelete" an element use RestorePreviousDirectLinkStatus().
				const DirectLinkSynchronizationStatus UnmodifiableStates = DirectLinkSynchronizationStatus.Created | DirectLinkSynchronizationStatus.Deleted;

				if (InternalDirectLinkStatus != value
					&& (value != DirectLinkSynchronizationStatus.Modified
					|| (InternalDirectLinkStatus & UnmodifiableStates) == DirectLinkSynchronizationStatus.None))
				{
					PreviousDirectLinkStatus = InternalDirectLinkStatus;
					InternalDirectLinkStatus = value;
				}
			}
		}
		public FDatasmithFacadeElement ExportedElement { get; private set; } = null;

		public DatasmithInfoBase(Rhino.Runtime.CommonObject InRhinoObject, string InName, string InLabel)
		{
			RhinoCommonObject = InRhinoObject;
			Name = InName;
			Label = InLabel;
		}

		/// <summary>
		/// Used to undo the last change to the DirectLinkStatus property. It is intended to be used mainly for "undeleted" objects for which the deletion was not synced.
		/// </summary>
		/// <returns></returns>
		public bool RestorePreviousDirectLinkStatus()
		{
			if (PreviousDirectLinkStatus != DirectLinkSynchronizationStatus.None)
			{
				InternalDirectLinkStatus = PreviousDirectLinkStatus;
				PreviousDirectLinkStatus = DirectLinkSynchronizationStatus.None;
				return true;
			}

			return false;
		}

		public void SetExportedElement(FDatasmithFacadeElement InExportedElement)
		{
			System.Diagnostics.Debug.Assert(ExportedElement == null, "Exported element cannot override existing exported element. A new DatasmithInfoBase must be created and the current one must be deleted");
			ExportedElement = InExportedElement;
		}

		public virtual void ApplyDiffs(T OtherInfo)
		{
			DirectLinkStatus = DirectLinkSynchronizationStatus.Modified;
			Label = OtherInfo.Label;
		}
	}

	public class DatasmithActorInfo : DatasmithInfoBase<DatasmithActorInfo>
	{
		public override DirectLinkSynchronizationStatus DirectLinkStatus {
			get 
			{
				DirectLinkSynchronizationStatus Status = base.DirectLinkStatus;

				if (DefinitionNode != null && Status == DirectLinkSynchronizationStatus.Synced)
				{
					return DefinitionNode.DirectLinkStatus;
				}
				return Status;
			}
		}

		public FDatasmithFacadeActor DatasmithActor { get { return ExportedElement as FDatasmithFacadeActor; } }
		public bool bHasRhinoObject { get { return RhinoModelComponent is RhinoObject; } }
		public bool bHasRhinoLayer { get { return RhinoModelComponent is Layer; } }
		public ModelComponent RhinoModelComponent { get { return RhinoCommonObject as ModelComponent; } }
		public Transform WorldTransform { get; private set; } = Transform.Identity;
		public int MaterialIndex { get; set; } = -1;
		public bool bOverrideMaterial { get; private set; } = false;

		public bool bIsRoot { get; private set; } = true;
		public DatasmithActorInfo Parent { get; private set; } = null;
		private LinkedList<DatasmithActorInfo> ChildrenInternal = new LinkedList<DatasmithActorInfo>();
		public ICollection<DatasmithActorInfo> Children { get => ChildrenInternal; }

		public bool bIsInstanceDefinition { get; private set; } = false;
		public DatasmithActorInfo DefinitionNode { get; private set; } = null;
		private LinkedList<DatasmithActorInfo> InstanceNodesInternal = new LinkedList<DatasmithActorInfo>();
		public ICollection<DatasmithActorInfo> InstanceNodes { get => InstanceNodesInternal; }

		public HashSet<int> LayerIndices { get; private set; } = new HashSet<int>();
		/// <summary>
		/// Used to determine variation occurring in the Layer hierarchy for that node.
		/// </summary>
		private List<int> RelativeLayerIndices = new List<int>();
		public Layer VisibilityLayer { get; private set; } = null;

		public bool bIsVisible
		{
			get
			{
				if (bHasRhinoLayer)
				{
					// This recursion ensure that only layer actors with an actual exported object under them are considered visible.
					// ie. Layers containing no mesh are not visible.
					return (RhinoCommonObject as Layer).IsVisible
						&& ChildrenInternal.Any(Child => Child.bIsVisible);
				}
				else if (bIsRoot)
				{
					return true;
				}

				return (VisibilityLayer == null || VisibilityLayer.IsVisible)
					&& (DefinitionNode == null || DefinitionNode.bIsVisible);
			}
		}

		public DatasmithActorInfo(Transform NodeTransform, string InName, string InLabel)
			: base(null, InName, InLabel)
		{
			WorldTransform = NodeTransform;
		}

		public DatasmithActorInfo(ModelComponent InModelComponent, string InName, string InLabel, int InMaterialIndex, bool bInOverrideMaterial, Layer InVisibilityLayer, IEnumerable<int> InLayerIndices = null)
			: base(InModelComponent, InName, InLabel)
		{
			bIsInstanceDefinition = RhinoModelComponent is InstanceDefinition;
			MaterialIndex = InMaterialIndex;
			bOverrideMaterial = bInOverrideMaterial;
			VisibilityLayer = InVisibilityLayer;

			if (InLayerIndices != null)
			{
				LayerIndices.UnionWith(InLayerIndices);
				RelativeLayerIndices.AddRange(InLayerIndices);
			}

			WorldTransform = DatasmithRhinoUtilities.GetModelComponentTransform(RhinoModelComponent);
		}

		public override void ApplyDiffs(DatasmithActorInfo OtherInfo)
		{
			base.ApplyDiffs(OtherInfo);

			//Rhino "replaces" object instead of modifying them, we must update the our reference to the object.
			RhinoCommonObject = OtherInfo.RhinoCommonObject;

			MaterialIndex = OtherInfo.MaterialIndex;
			bOverrideMaterial = OtherInfo.bOverrideMaterial;
			RelativeLayerIndices = OtherInfo.RelativeLayerIndices;
			LayerIndices = OtherInfo.LayerIndices;

			WorldTransform = OtherInfo.WorldTransform;
			MaterialIndex = OtherInfo.MaterialIndex;
			bOverrideMaterial = OtherInfo.bOverrideMaterial;
		}

		public List<string> GetTags(DatasmithRhinoExportContext ExportContext)
		{
			List<string> Tags = new List<string>();

			Tags.Add(string.Format("Rhino.ID: {0}", RhinoModelComponent.Id));
			string ComponentTypeString = DatasmithRhinoUniqueNameGenerator.GetDefaultTypeName(RhinoModelComponent);
			Tags.Add(string.Format("Rhino.Entity.Type: {0}", ComponentTypeString));

			//Add the groups this object belongs to.
			RhinoObject InRhinoObject = RhinoCommonObject as RhinoObject;
			if (InRhinoObject != null && InRhinoObject.GroupCount > 0)
			{
				int[] GroupIndices = InRhinoObject.GetGroupList();
				for (int GroupArrayIndex = 0; GroupArrayIndex < GroupIndices.Length; ++GroupArrayIndex)
				{
					string GroupName = ExportContext.GroupIndexToName[GroupIndices[GroupArrayIndex]];
					if (GroupName != null)
					{
						Tags.Add(GroupName);
					}
				}
			}

			return Tags;
		}

		private void SetParent(DatasmithActorInfo InParent)
		{
			if (Parent != null)
			{
				// Reset the absolute transform and layer to their relative values.
				WorldTransform = DatasmithRhinoUtilities.GetModelComponentTransform(RhinoModelComponent);
				LayerIndices = new HashSet<int>(RelativeLayerIndices);
			}

			bIsRoot = false;
			Parent = InParent;

			if (InParent != null)
			{
				bIsInstanceDefinition = InParent.bIsInstanceDefinition;
				WorldTransform = Transform.Multiply(InParent.WorldTransform, WorldTransform);
				LayerIndices.UnionWith(InParent.LayerIndices);
			}
		}

		public void ApplyTransform(Transform InTransform)
		{
			foreach (DatasmithActorInfo ActorInfo in GetEnumerator(/*bIncludeHidden=*/true))
			{
				ActorInfo.WorldTransform = InTransform * ActorInfo.WorldTransform;
			}
		}

		public void SetDefinitionNode(DatasmithActorInfo InDefinitionNode)
		{
			System.Diagnostics.Debug.Assert(InDefinitionNode.bIsInstanceDefinition, "Trying to create an instance from a node belonging in the root tree");
			DefinitionNode = InDefinitionNode;
			InDefinitionNode.InstanceNodesInternal.AddLast(this);
			LayerIndices.UnionWith(InDefinitionNode.LayerIndices);
		}

		public void AddChild(DatasmithActorInfo ChildHierarchyNodeInfo)
		{
			ChildHierarchyNodeInfo.SetParent(this);
			ChildrenInternal.AddLast(ChildHierarchyNodeInfo);
		}

		/// <summary>
		/// Returns the number of hierarchical descendants this nodes has.
		/// </summary>
		/// <returns></returns>
		public int GetDescendantsCount()
		{
			int DescendantCount = ChildrenInternal.Count;

			foreach (DatasmithActorInfo CurrentChild in ChildrenInternal)
			{
				DescendantCount += CurrentChild.GetDescendantsCount();
			}

			return DescendantCount;
		}

		public void Reparent(DatasmithActorInfo NewParent)
		{
			Parent.ChildrenInternal.Remove(this);
			NewParent.AddChild(this);
		}

		/// <summary>
		/// Remove all actor infos with the Deleted DirectLinkStatus from the hierarchy.
		/// </summary>
		public void PurgeDeleted()
		{
			DirectLinkStatus = DirectLinkSynchronizationStatus.Synced;

			LinkedListNode<DatasmithActorInfo> CurrentNode = ChildrenInternal.First;
			while (CurrentNode != null)
			{
				if (CurrentNode.Value.DirectLinkStatus != DirectLinkSynchronizationStatus.Deleted)
				{
					CurrentNode.Value.PurgeDeleted();
					CurrentNode = CurrentNode.Next;
				}
				else
				{
					LinkedListNode<DatasmithActorInfo> NodeToDelete = CurrentNode;
					CurrentNode = CurrentNode.Next;
					ChildrenInternal.Remove(NodeToDelete);
				}
			}

			CurrentNode = InstanceNodesInternal.First;
			while (CurrentNode != null)
			{
				if (CurrentNode.Value.DirectLinkStatus == DirectLinkSynchronizationStatus.Deleted)
				{
					LinkedListNode<DatasmithActorInfo> NodeToDelete = CurrentNode;
					CurrentNode = CurrentNode.Next;
					InstanceNodesInternal.Remove(NodeToDelete);
				}
				else
				{
					CurrentNode = CurrentNode.Next;
				}
			}
		}

		/// <summary>
		/// Custom enumerator implementation returning this Actor and all its descendant.
		/// </summary>
		/// <param name="bIncludeHidden">Enumerate with or without hidden actors</param>
		/// <returns></returns>
		public IEnumerable<DatasmithActorInfo> GetEnumerator(bool bIncludeHidden)
		{
			if (bIncludeHidden || bIsVisible)
			{
				yield return this;

				foreach (var Child in ChildrenInternal)
				{
					foreach (var ChildEnumValue in Child.GetEnumerator(bIncludeHidden))
					{
						yield return ChildEnumValue;
					}
				}
			}
		}

		/// <summary>
		/// Custom enumerator implementation for returning all descendants of this Actor.
		/// </summary>
		/// <param name="bIncludeHidden">Enumerate with or without hidden actors</param>
		/// <returns></returns>
		public IEnumerable<DatasmithActorInfo> GetDescendantEnumerator(bool bIncludeHidden)
		{
			foreach (var Child in ChildrenInternal)
			{
				foreach (var ChildEnumValue in Child.GetEnumerator(bIncludeHidden))
				{
					yield return ChildEnumValue;
				}
			}
		}
	}

	public class DatasmithMaterialInfo : DatasmithInfoBase<DatasmithMaterialInfo>
	{
		public Material RhinoMaterial { get { return RhinoCommonObject as Material; } }
		public FDatasmithFacadeUEPbrMaterial ExportedMaterial { get { return ExportedElement as FDatasmithFacadeUEPbrMaterial; } }

		public DatasmithMaterialInfo(Material InRhinoMaterial, string InName, string InLabel)
			: base(InRhinoMaterial, InName, InLabel)
		{
		}
	}

	public class DatasmithTextureInfo : DatasmithInfoBase<DatasmithTextureInfo>
	{
		public Texture RhinoTexture { get { return RhinoCommonObject as Texture; } }
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

	public class DatasmithMeshInfo : DatasmithInfoBase<DatasmithMeshInfo>
	{
		public FDatasmithFacadeMeshElement ExportedMesh { get { return ExportedElement as FDatasmithFacadeMeshElement; } }

		public List<Mesh> RhinoMeshes { get; private set; }
		public Transform OffsetTransform { get; set; }
		public List<int> MaterialIndices { get; private set; }

		public DatasmithMeshInfo(IEnumerable<Mesh> InRhinoMeshes, Transform InOffset, List<int> InMaterialIndexes, string InName, string InLabel)
			: base(null, InName, InLabel)
		{
			RhinoMeshes = new List<Mesh>(InRhinoMeshes);
			OffsetTransform = InOffset;
			MaterialIndices = InMaterialIndexes;
		}

		public DatasmithMeshInfo(Mesh InRhinoMesh, Transform InOffset, int InMaterialIndex, string InName, string InLabel)
			: this(new List<Mesh> { InRhinoMesh }, InOffset, new List<int> { InMaterialIndex }, InName, InLabel)
		{
		}

		public override void ApplyDiffs(DatasmithMeshInfo OtherMeshInfo)
		{
			base.ApplyDiffs(OtherMeshInfo);

			RhinoMeshes = OtherMeshInfo.RhinoMeshes;
			OffsetTransform = OtherMeshInfo.OffsetTransform;
			MaterialIndices = OtherMeshInfo.MaterialIndices;
		}
	}


	public class DatasmithRhinoExportContext
	{
		public RhinoDoc RhinoDocument { get => ExportOptions.RhinoDocument; }
		public DatasmithRhinoExportOptions ExportOptions { get; private set; }
		public bool bIsParsed { get; private set; } = false;
		private bool bExportedOnce = false;
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
		public Dictionary<Guid, DatasmithActorInfo> ObjectIdToHierarchyActorNodeDictionary = new Dictionary<Guid, DatasmithActorInfo>();
		public Dictionary<Guid, DatasmithMeshInfo> ObjectIdToMeshInfoDictionary = new Dictionary<Guid, DatasmithMeshInfo>();
		public Dictionary<string, DatasmithMaterialInfo> MaterialHashToMaterialInfo = new Dictionary<string, DatasmithMaterialInfo>();
		public Dictionary<string, DatasmithTextureInfo> TextureHashToTextureInfo = new Dictionary<string, DatasmithTextureInfo>();
		public Dictionary<int, string> GroupIndexToName = new Dictionary<int, string>();

		private Dictionary<int, string> MaterialIndexToMaterialHashDictionary = new Dictionary<int, string>();
		private Dictionary<Guid, string> TextureIdToTextureHash = new Dictionary<Guid, string>();
		private Dictionary<int, string> LayerIndexToLayerString = new Dictionary<int, string>();
		private Dictionary<int, HashSet<int>> LayerIndexToLayerIndexHierarchy = new Dictionary<int, HashSet<int>>();
		private DatasmithRhinoUniqueNameGenerator ActorLabelGenerator = new DatasmithRhinoUniqueNameGenerator();
		private DatasmithRhinoUniqueNameGenerator MaterialLabelGenerator = new DatasmithRhinoUniqueNameGenerator();
		private DatasmithRhinoUniqueNameGenerator TextureLabelGenerator = new DatasmithRhinoUniqueNameGenerator();
		private ViewportInfo ActiveViewportInfo;
		private int DummyLayerIndex = -1;

		public DatasmithRhinoExportContext(DatasmithRhinoExportOptions InOptions)
		{
			ExportOptions = InOptions;
			SceneRoot = new DatasmithActorInfo(ExportOptions.Xform, "SceneRoot", "SceneRoot");
		}

		public void ParseDocument(bool bForceParse = false)
		{
			if (!bIsParsed || bForceParse)
			{
				RhinoViewport ActiveViewport = RhinoDocument.Views.ActiveView?.ActiveViewport;
				//Update current active viewport.
				ActiveViewportInfo = ActiveViewport == null ? null : new ViewportInfo(ActiveViewport);

				DatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress(0.33f);
				ParseGroupNames();
				DatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress(0.66f);
				ParseRhinoHierarchy();
				DatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress(1f);
				ParseAllRhinoMeshes();

				bIsParsed = true;
			}
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

		/// <summary>
		/// This function should be called after each export to update the context tracking data.
		/// </summary>
		public void OnPostExport()
		{
			if (bExportedOnce)
			{
				Cleanup();
			}
			else
			{
				// Block definitions must be set to synced state after the first sync.
				foreach (DatasmithActorInfo InstanceDefinitionInfo in InstanceDefinitionHierarchyNodeDictionary.Values)
				{
					const bool bIncludeHidden = true;
					foreach(DatasmithActorInfo DefinitionNode in InstanceDefinitionInfo.GetEnumerator(bIncludeHidden))
					{
						DefinitionNode.DirectLinkStatus = DirectLinkSynchronizationStatus.Synced;
					}
				}
			}

			bExportedOnce = true;
		}

		/// <summary>
		/// Remove cached data for deleted object no longer needed.
		/// </summary>
		private void Cleanup()
		{
			CleanCacheDictionary(InstanceDefinitionHierarchyNodeDictionary);
			CleanCacheDictionary(ObjectIdToHierarchyActorNodeDictionary);
			CleanCacheDictionary(ObjectIdToMeshInfoDictionary);
			CleanCacheDictionary(MaterialHashToMaterialInfo);
			CleanCacheDictionary(TextureHashToTextureInfo);

			SceneRoot.PurgeDeleted();
			foreach (DatasmithActorInfo InstanceDefinitionInfo in InstanceDefinitionHierarchyNodeDictionary.Values)
			{
				InstanceDefinitionInfo.PurgeDeleted();
			}
		}

		private void CleanCacheDictionary<KeyType, InfoType>(Dictionary<KeyType, InfoType> InDictionary) where InfoType : DatasmithInfoBase<InfoType>
		{
			List<KeyType> KeysOfElementsToDelete = new List<KeyType>();

			foreach (KeyValuePair<KeyType, InfoType> CurrentKeyValuePair in InDictionary)
			{
				if (CurrentKeyValuePair.Value.DirectLinkStatus == DirectLinkSynchronizationStatus.Deleted)
				{
					KeysOfElementsToDelete.Add(CurrentKeyValuePair.Key);
				}
			}

			foreach (KeyType CurrentKey in KeysOfElementsToDelete)
			{
				InDictionary.Remove(CurrentKey);
			}
		}

		/// <summary>
		/// Actor creation event used when a RhinoObject is created.
		/// </summary>
		/// <param name="InModelComponent"></param>
		public void AddActor(ModelComponent InModelComponent)
		{
			if (ObjectIdToHierarchyActorNodeDictionary.TryGetValue(InModelComponent.Id, out DatasmithActorInfo CachedActorInfo))
			{
				// The actor already exists in the cache, it means that no direct link synchronization was done since its deletion.
				// In that case, we can simply undelete it.
				UndeleteActor(CachedActorInfo);
				return;
			}

			DatasmithActorInfo ActorParentInfo = GetActorParentInfo(InModelComponent);
			if (InModelComponent is Layer InLayer)
			{
				RecursivelyParseLayerHierarchy(InLayer, ActorParentInfo);
			}
			else if (InModelComponent is RhinoObject InRhinoObject)
			{
				RecursivelyParseObjectInstance(new[] { InRhinoObject }, ActorParentInfo);
				RegisterActorsToContext(ActorParentInfo.GetDescendantEnumerator(/*bIncludeHidden=*/true));

				HashSet<RhinoObject> CollectedMeshObjects = new HashSet<RhinoObject>();
				if (InRhinoObject is InstanceObject InRhinoInstance 
					&& InstanceDefinitionHierarchyNodeDictionary.TryGetValue(InRhinoInstance.InstanceDefinition, out DatasmithActorInfo InstanceDefinitionInfo))
				{
					CollectExportedRhinoObjectsFromInstanceDefinition(CollectedMeshObjects, InstanceDefinitionInfo);
				}
				else
				{
					CollectedMeshObjects.Add(InRhinoObject);
				}
				ParseRhinoMeshesFromRhinoObjects(CollectedMeshObjects);
			}
		}

		private void UndeleteActor(DatasmithActorInfo ActorInfo)
		{
			foreach (DatasmithActorInfo ActorInfoValue in ActorInfo.GetEnumerator(/*bIncludeHidden=*/true))
			{
				System.Diagnostics.Debug.Assert(ActorInfoValue.DirectLinkStatus == DirectLinkSynchronizationStatus.Deleted);
				if (ActorInfoValue.RestorePreviousDirectLinkStatus())
				{
					if (ObjectIdToMeshInfoDictionary.TryGetValue(ActorInfoValue.RhinoModelComponent.Id, out DatasmithMeshInfo MeshInfo))
					{
						MeshInfo.RestorePreviousDirectLinkStatus();
					}
				}
				else
				{
					System.Diagnostics.Debug.Fail("The previous direct link state was lost on an undeleted actor");
					break;
				}
			}

			//Undelete is always used during an undo, and undo often use "undelete" to restore an actor state (instead of calling modify).
			ModifyActor(ActorInfo.RhinoModelComponent, /*bReparent=*/false);
		}

		/// <summary>
		/// Specific Actor modification event used when the only change is the RhinoObject transform. We avoid reprocessing the Mesh and simply update the actor position.
		/// </summary>
		/// <param name="InRhinoObject"></param>
		/// <param name="InTransform"></param>
		public void MoveActor(RhinoObject InRhinoObject, Transform InTransform)
		{
			bool bHasMesh = false;
			if (ObjectIdToMeshInfoDictionary.TryGetValue(InRhinoObject.Id, out DatasmithMeshInfo MeshInfo))
			{
				// #ueent-todo	The MeshInfo is the one holding the OffsetTransform but changing it actually affects the actor and not the mesh. 
				//				It is weird that the actor is the one modified here, we should move the OffsetTransform into it's associated DatasmithActorInfo.
				MeshInfo.OffsetTransform = InTransform * MeshInfo.OffsetTransform;
				bHasMesh = true;
			}

			bool bIsInstance = InRhinoObject is InstanceObject;
			if ((bHasMesh || bIsInstance)
				&& ObjectIdToHierarchyActorNodeDictionary.TryGetValue(InRhinoObject.Id, out DatasmithActorInfo ActorInfo))
			{
				ActorInfo.DirectLinkStatus = DirectLinkSynchronizationStatus.Modified;

				if (bIsInstance)
				{
					ActorInfo.ApplyTransform(InTransform);
				}
			}
		}

		/// <summary>
		/// Generic Actor modification event used when the RhinoObject may have multiple property modifications, both the associated DatasmithActor and DatasmithMesh will be synced.
		/// </summary>
		/// <param name="InModelComponent"></param>
		/// <param name="bReparent"></param>
		public void ModifyActor(ModelComponent InModelComponent, bool bReparent)
		{
			RhinoObject InRhinoObject = InModelComponent as RhinoObject;
			if (ObjectIdToHierarchyActorNodeDictionary.TryGetValue(InModelComponent.Id, out DatasmithActorInfo ActorInfo))
			{
				Layer InLayer = InModelComponent as Layer;
				DatasmithActorInfo ActorParentInfo = bReparent 
					? GetActorParentInfo(InModelComponent)
					: ActorInfo.Parent;

				DatasmithActorInfo DiffActorInfo = null;
				if (InRhinoObject != null)
				{
					DiffActorInfo = TryParseObjectInstance(InRhinoObject, ActorParentInfo, out DatasmithActorInfo DefinitionRootNode);
				}
				else if (InLayer != null)
				{
					DiffActorInfo = TryParseLayer(InLayer, ActorParentInfo);
				}

				if (DiffActorInfo != null)
				{
					bool bMaterialChanged = ActorInfo.MaterialIndex != DiffActorInfo.MaterialIndex;
					if (bMaterialChanged)
					{
						if (InRhinoObject != null)
						{
							AddObjectMaterialReference(InRhinoObject, DiffActorInfo.MaterialIndex);
						}
						else
						{
							AddMaterialIndexMapping(DiffActorInfo.MaterialIndex);
						}
					}

					ActorInfo.ApplyDiffs(DiffActorInfo);
					if (bReparent)
					{
						ActorInfo.Reparent(ActorParentInfo);
					}

					if (bReparent || bMaterialChanged)
					{
						UpdateChildActorsMaterialIndex(ActorInfo);
					}

					ActorInfo.DirectLinkStatus = DirectLinkSynchronizationStatus.Modified;
				}
				else
				{
					DeleteActor(InModelComponent);
					return;
				}
			}
			else
			{
				AddActor(InModelComponent);
				return;
			}

			ModifyMesh(InRhinoObject);
		}

		private void ModifyMesh(RhinoObject InRhinoObject)
		{
			if (InRhinoObject != null && ObjectIdToMeshInfoDictionary.TryGetValue(InRhinoObject.Id, out DatasmithMeshInfo MeshInfo))
			{
				// Make sure all render meshes are generated before attempting to export them.
				RhinoObject[] ObjectArray = { InRhinoObject };
				RhinoObject.GetRenderMeshes(ObjectArray, /*okToCreate=*/true, /*returnAllObjects*/false);

				if (TryGenerateMeshInfoFromRhinoObjects(InRhinoObject) is DatasmithMeshInfo DiffMeshInfo)
				{
					MeshInfo.ApplyDiffs(DiffMeshInfo);
					MeshInfo.DirectLinkStatus = DirectLinkSynchronizationStatus.Modified;
				}
			}
		}

		private DatasmithActorInfo GetActorParentInfo(ModelComponent InModelComponent)
		{
			Layer ParentLayer = null;
			if (InModelComponent is RhinoObject InRhinoObject)
			{
				ParentLayer = RhinoDocument.Layers.FindIndex(InRhinoObject.Attributes.LayerIndex);
			}
			else if (InModelComponent is Layer InLayer)
			{
				ParentLayer = RhinoDocument.Layers.FindId(InLayer.ParentLayerId);
			}

			DatasmithActorInfo ActorParentInfo;
			if (ParentLayer != null)
			{
				ObjectIdToHierarchyActorNodeDictionary.TryGetValue(ParentLayer.Id, out ActorParentInfo);
			}
			else
			{
				ActorParentInfo = SceneRoot;
			}

			return ActorParentInfo;
		}

		/// <summary>
		/// Actor deletion event used when a RhinoObject is deleted, calling this will remove the associated DatasmithActorElement on the next sync.
		/// </summary>
		/// <param name="InModelComponent"></param>
		public void DeleteActor(ModelComponent InModelComponent)
		{
			if (ObjectIdToHierarchyActorNodeDictionary.TryGetValue(InModelComponent.Id, out DatasmithActorInfo ActorInfo))
			{
				foreach (DatasmithActorInfo ActorInfoValue in ActorInfo.GetEnumerator(/*bIncludeHidden=*/true))
				{
					ActorInfoValue.DirectLinkStatus = DirectLinkSynchronizationStatus.Deleted;

					// If this actor is not a block instance, we can delete its associated mesh.
					if (ActorInfoValue.DefinitionNode == null && ObjectIdToMeshInfoDictionary.TryGetValue(ActorInfoValue.RhinoModelComponent.Id, out DatasmithMeshInfo MeshInfo))
					{
						MeshInfo.DirectLinkStatus = DirectLinkSynchronizationStatus.Deleted;
					}
				}
			}
		}

		private void UpdateChildActorsMaterialIndex(DatasmithActorInfo ActorInfo)
		{
			foreach (DatasmithActorInfo ChildActorInfo in ActorInfo.Children)
			{
				ChildActorInfo.DirectLinkStatus = DirectLinkSynchronizationStatus.Modified;
				bool bMaterialAffectedByParent = ChildActorInfo.RhinoCommonObject is RhinoObject ChildRhinoObject && ChildRhinoObject.Attributes.MaterialSource != ObjectMaterialSource.MaterialFromObject;

				if (bMaterialAffectedByParent)
				{
					RhinoObject TargetObject = (ChildActorInfo.DefinitionNode != null ? ChildActorInfo.DefinitionNode.RhinoModelComponent : ChildActorInfo.RhinoModelComponent) as RhinoObject;

					if (TargetObject != null)
					{
						ChildActorInfo.MaterialIndex = GetObjectMaterialIndex(TargetObject, ActorInfo.Parent);
					}
				}
			}
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
				const int DefaultMaterialIndex = -1;
				//Decrement the dummy layer index for each dummy layer added to ensure unique id for each document.
				int[] DummyLayerIndices = { DummyLayerIndex-- };

				DummyDocumentNode = GenerateDummyNodeInfo(DummyLayerName, DummyLayerLabel, DefaultMaterialIndex, DummyLayerIndices);
				SceneRoot.AddChild(DummyDocumentNode);
				LayerIndexToLayerString.Add(DummyLayerIndices[0], BuildLayerString(DummyDocumentNode.Label, SceneRoot));
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
			if (TryParseLayer(CurrentLayer, ParentNode) is DatasmithActorInfo ActorNodeInfo)
			{
				ParentNode.AddChild(ActorNodeInfo);
				ObjectIdToHierarchyActorNodeDictionary.Add(CurrentLayer.Id, ActorNodeInfo);
				LayerIndexToLayerString.Add(CurrentLayer.Index, BuildLayerString(CurrentLayer, ParentNode));
				AddMaterialIndexMapping(CurrentLayer.RenderMaterialIndex);

				RhinoObject[] ObjectsInLayer = RhinoDocument.Objects.FindByLayer(CurrentLayer);
				RecursivelyParseObjectInstance(ObjectsInLayer, ActorNodeInfo);
				RegisterActorsToContext(ActorNodeInfo.GetDescendantEnumerator(/*bIncludeHidden=*/true));

				Layer[] ChildrenLayer = CurrentLayer.GetChildren();
				if (ChildrenLayer != null)
				{
					foreach (var ChildLayer in ChildrenLayer)
					{
						RecursivelyParseLayerHierarchy(ChildLayer, ActorNodeInfo);
					}
				}
			}
		}

		private DatasmithActorInfo TryParseLayer(Layer CurrentLayer, DatasmithActorInfo ParentNode)
		{
			if ((ExportOptions.bSkipHidden && !CurrentLayer.IsVisible) || CurrentLayer.IsDeleted)
			{
				return null;
			}

			int MaterialIndex = CurrentLayer.RenderMaterialIndex;
			return GenerateNodeInfo(CurrentLayer, ParentNode.bIsInstanceDefinition, MaterialIndex, CurrentLayer, new[] { CurrentLayer.Index });
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
				if (TryParseObjectInstance(CurrentObject, ParentNode, out DatasmithActorInfo DefinitionRootNode) is DatasmithActorInfo ObjectNodeInfo)
				{
					ParentNode.AddChild(ObjectNodeInfo);

					if (DefinitionRootNode != null)
					{
						InstanciateDefinition(ObjectNodeInfo, DefinitionRootNode);
					}
				}
			}
		}

		private void RegisterActorsToContext(IEnumerable<DatasmithActorInfo> ActorInfos)
		{
			foreach (DatasmithActorInfo CurrentInfo in ActorInfos)
			{
				if (CurrentInfo.DirectLinkStatus != DirectLinkSynchronizationStatus.Deleted)
				{
					ObjectIdToHierarchyActorNodeDictionary[CurrentInfo.RhinoModelComponent.Id] = CurrentInfo;

					if (CurrentInfo.RhinoModelComponent is RhinoObject CurrentRhinoObject)
					{
						AddObjectMaterialReference(CurrentRhinoObject, CurrentInfo.MaterialIndex);
					}
				}
			}
		}

		private DatasmithActorInfo TryParseObjectInstance(RhinoObject InObject, DatasmithActorInfo ParentNode, out DatasmithActorInfo DefinitionRootNode)
		{
			DefinitionRootNode = null;
			if (InObject == null
				|| IsObjectIgnoredBySelection(InObject, ParentNode)
				|| IsUnsupportedObject(InObject))
			{
				// Skip the object.
				return null;
			}

			if (InObject.ObjectType == ObjectType.InstanceReference)
			{
				InstanceObject CurrentInstance = InObject as InstanceObject;
				DefinitionRootNode = GetOrCreateDefinitionRootNode(CurrentInstance.InstanceDefinition);

				if (DefinitionRootNode.Children.Count == 0)
				{
					// Don't instantiate empty definitions.
					return null;
				}
			}

			int MaterialIndex = GetObjectMaterialIndex(InObject, ParentNode);
			Layer VisibilityLayer;
			IEnumerable<int> LayerIndices;
			if (ParentNode.bIsRoot && ParentNode.bIsInstanceDefinition)
			{
				VisibilityLayer = RhinoDocument.Layers.FindIndex(InObject.Attributes.LayerIndex);
				// The objects inside a Block definitions may be defined in a different layer than the one we are currently in.
				LayerIndices = GetOrCreateLayerIndexHierarchy(InObject.Attributes.LayerIndex);
			}
			else
			{
				VisibilityLayer = ParentNode.VisibilityLayer;
				LayerIndices = null;
			}

			return GenerateNodeInfo(InObject, ParentNode.bIsInstanceDefinition, MaterialIndex, VisibilityLayer, LayerIndices);
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
			ParentNode.SetDefinitionNode(DefinitionNode);

			foreach (DatasmithActorInfo DefinitionChildNode in DefinitionNode.Children)
			{
				AddChildInstance(ParentNode, DefinitionChildNode);
			}
		}

		private DatasmithActorInfo AddChildInstance(DatasmithActorInfo ParentNode, DatasmithActorInfo ChildDefinitionNode)
		{
			int MaterialIndex = GetObjectMaterialIndex(ChildDefinitionNode.RhinoModelComponent as RhinoObject, ParentNode);
			DatasmithActorInfo InstanceChildNode = GenerateInstanceNodeInfo(ParentNode, ChildDefinitionNode, MaterialIndex, ParentNode.VisibilityLayer);
			ParentNode.AddChild(InstanceChildNode);

			InstanciateDefinition(InstanceChildNode, ChildDefinitionNode);

			return InstanceChildNode;
		}

		private DatasmithActorInfo GetOrCreateDefinitionRootNode(InstanceDefinition InInstanceDefinition)
		{
			DatasmithActorInfo InstanceRootNode;

			//If a hierarchy node does not exist for this instance definition, create one.
			if (!InstanceDefinitionHierarchyNodeDictionary.TryGetValue(InInstanceDefinition, out InstanceRootNode))
			{
				InstanceRootNode = ParseInstanceDefinition(InInstanceDefinition);

				const bool bIncludeHidden = false;
				RegisterActorsToContext(InstanceRootNode.GetEnumerator(bIncludeHidden));
				InstanceDefinitionHierarchyNodeDictionary.Add(InInstanceDefinition, InstanceRootNode);
			}

			return InstanceRootNode;
		}

		private DatasmithActorInfo ParseInstanceDefinition(InstanceDefinition InInstanceDefinition)
		{
			const bool bIsInstanceDefinition = true;
			const int MaterialIndex = -1;
			const Layer NullVisibilityLayer = null;

			DatasmithActorInfo InstanceRootNode = GenerateNodeInfo(InInstanceDefinition, bIsInstanceDefinition, MaterialIndex, NullVisibilityLayer);

			RhinoObject[] InstanceObjects = InInstanceDefinition.GetObjects();
			RecursivelyParseObjectInstance(InstanceObjects, InstanceRootNode);

			return InstanceRootNode;
		}

		public void UpdateDefinitionNode(InstanceDefinition InInstanceDefinition)
		{
			if (InstanceDefinitionHierarchyNodeDictionary.TryGetValue(InInstanceDefinition, out DatasmithActorInfo DefinitionRootNode))
			{
				if (ParseInstanceDefinition(InInstanceDefinition) is DatasmithActorInfo DiffActorInfo)
				{
					// Apply changes on InstanceDefinition Actors.
					{
						DefinitionRootNode.ApplyDiffs(DiffActorInfo);

						// To avoid costly O(n^2) lookups, create HashSet and Dictionary to accelerate the search.
						HashSet<DatasmithActorInfo> DeletedChildren = new HashSet<DatasmithActorInfo>(DefinitionRootNode.Children);
						Dictionary<string, DatasmithActorInfo> ChildNameToIndex = new Dictionary<string, DatasmithActorInfo>(DefinitionRootNode.Children.Count);
						foreach (DatasmithActorInfo CurrentChild in DefinitionRootNode.Children)
						{
							ChildNameToIndex.Add(CurrentChild.Name, CurrentChild);
						}

						foreach (DatasmithActorInfo OtherChildInfo in DiffActorInfo.Children)
						{
							if (ChildNameToIndex.TryGetValue(OtherChildInfo.Name, out DatasmithActorInfo CurrentChild))
							{
								DeletedChildren.Remove(CurrentChild);
								CurrentChild.ApplyDiffs(OtherChildInfo);
							}
							else
							{
								//A new child is present, we must add it.
								//We're not calling Reparent() here because that would change the size of the array we are iterating upon.
								DefinitionRootNode.AddChild(OtherChildInfo);

								RecursivelyAddChildInstance(DefinitionRootNode, OtherChildInfo);
							}
						}

						foreach (DatasmithActorInfo DeletedChild in DeletedChildren)
						{
							DeletedChild.DirectLinkStatus = DirectLinkSynchronizationStatus.Deleted;
						}

						RegisterActorsToContext(DefinitionRootNode.GetEnumerator(true));
					}

					// Apply changes on InstanceDefinition Meshes
					{
						List<RhinoObject> ObjectNeedingMeshParsing = new List<RhinoObject>();

						const bool bIncludeHidden = true;
						foreach (DatasmithActorInfo ActorInfo in DefinitionRootNode.GetEnumerator(bIncludeHidden))
						{
							if (ActorInfo.DirectLinkStatus == DirectLinkSynchronizationStatus.Modified)
							{
								ModifyMesh(ActorInfo.RhinoModelComponent as RhinoObject);
							}
							else if (ActorInfo.DirectLinkStatus == DirectLinkSynchronizationStatus.Created)
							{
								if (ActorInfo.RhinoModelComponent is RhinoObject CreatedRhinoObject)
								{
									ObjectNeedingMeshParsing.Add(CreatedRhinoObject);
								}
							}
							else if (ActorInfo.DirectLinkStatus == DirectLinkSynchronizationStatus.Deleted)
							{
								if (ObjectIdToMeshInfoDictionary.TryGetValue(ActorInfo.RhinoModelComponent.Id, out DatasmithMeshInfo MeshInfo))
								{
									MeshInfo.DirectLinkStatus = DirectLinkSynchronizationStatus.Deleted;
								}
							}
						}

						// Add newly created meshes.
						ParseRhinoMeshesFromRhinoObjects(ObjectNeedingMeshParsing);
					}
				}
			}
		}


		private void RecursivelyAddChildInstance(DatasmithActorInfo DefinitionParent, DatasmithActorInfo ChildDefinition)
		{
			foreach (DatasmithActorInfo ActorInstance in DefinitionParent.InstanceNodes)
			{
				if (ActorInstance.DirectLinkStatus != DirectLinkSynchronizationStatus.Deleted)
				{
					DatasmithActorInfo ChildInstance = AddChildInstance(ActorInstance, ChildDefinition);
					RecursivelyAddChildInstance(ActorInstance, ChildInstance);
				}
			}
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
			bool bOverrideMaterial = DefinitionNodeInfo.MaterialIndex != MaterialIndex;

			return new DatasmithActorInfo(DefinitionNodeInfo.RhinoModelComponent, Name, Label, MaterialIndex, bOverrideMaterial, VisibilityLayer);
		}

		/// <summary>
		/// Creates a new hierarchy node info that is not represented by any ModelComponent, used to add an empty actor to the scene. 
		/// </summary>
		/// <param name="UniqueID"></param>
		/// <param name="TargetLabel"></param>
		/// <param name="MaterialIndex"></param>
		/// <returns></returns>
		private DatasmithActorInfo GenerateDummyNodeInfo(string UniqueID, string TargetLabel, int MaterialIndex, IEnumerable<int> LayerIndices)
		{
			string UniqueLabel = ActorLabelGenerator.GenerateUniqueNameFromBaseName(TargetLabel);
			const ModelComponent NullModelComponent = null;
			const bool bOverrideMaterial = false;
			const Layer NullVisiblityLayer = null;

			return new DatasmithActorInfo(NullModelComponent, UniqueID, UniqueLabel, MaterialIndex, bOverrideMaterial, NullVisiblityLayer, LayerIndices);
		}

		/// <summary>
		/// Creates a new hierarchy node info for a given Rhino Model Component, used to determine names and labels as well as linking.
		/// </summary>
		/// <param name="InModelComponent"></param>
		/// <param name="ParentNode"></param>
		/// <param name="MaterialIndex"></param>
		/// <returns></returns>
		private DatasmithActorInfo GenerateNodeInfo(ModelComponent InModelComponent, bool bIsInstanceDefinition, int MaterialIndex, Layer VisibilityLayer, IEnumerable<int> LayerIndices = null)
		{
			string Name = GetModelComponentName(InModelComponent);
			string Label = bIsInstanceDefinition
				? DatasmithRhinoUniqueNameGenerator.GetTargetName(InModelComponent)
				: ActorLabelGenerator.GenerateUniqueName(InModelComponent);
			const bool bOverrideMaterial = false;

			return new DatasmithActorInfo(InModelComponent, Name, Label, MaterialIndex, bOverrideMaterial, VisibilityLayer, LayerIndices);
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

		public void UpdateGroups(GroupTableEventType UpdateType, Group UpdatedGroup)
		{
			switch (UpdateType)
			{
				case GroupTableEventType.Added:
				case GroupTableEventType.Undeleted:
					GroupIndexToName.Add(UpdatedGroup.Index, GetGroupName(UpdatedGroup));
					break;
				case GroupTableEventType.Deleted:
					GroupIndexToName.Remove(UpdatedGroup.Index);
					break;
				case GroupTableEventType.Modified:
					GroupIndexToName[UpdatedGroup.Index] = GetGroupName(UpdatedGroup);
					break;
				default:
					break;
			}
		}

		private void ParseGroupNames()
		{
			foreach (Group CurrentGroup in RhinoDocument.Groups)
			{
				int GroupIndex = CurrentGroup.Index;
				string GroupName = GetGroupName(CurrentGroup);
				GroupIndexToName.Add(GroupIndex, GroupName);
			}
		}

		private string GetGroupName(Group InGroup)
		{
			return InGroup.Name == null || InGroup.Name == ""
					? string.Format("Group{0}", InGroup.Index)
					: InGroup.Name;
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

		private void ParseAllRhinoMeshes()
		{
			HashSet<RhinoObject> DocObjects = CollectExportedRhinoObjects();
			ParseRhinoMeshesFromRhinoObjects(DocObjects);
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
				CollectExportedRhinoObjectsFromInstanceDefinition(ExportedObjects, CurrentInstanceHierarchy);
			}

			return ExportedObjects;
		}

		private void CollectExportedRhinoObjectsFromInstanceDefinition(HashSet<RhinoObject> CollectedObjects, DatasmithActorInfo InstanceDefinitionHierarchy)
		{
			const bool bIncludeHidden = true;
			foreach (DatasmithActorInfo HierarchyNode in InstanceDefinitionHierarchy.GetEnumerator(bIncludeHidden))
			{
				if (!HierarchyNode.bIsRoot && HierarchyNode.bHasRhinoObject && !(HierarchyNode.RhinoModelComponent is InstanceObject))
				{
					CollectedObjects.Add(HierarchyNode.RhinoModelComponent as RhinoObject);
				}
			}
		}

		private void ParseRhinoMeshesFromRhinoObjects(IEnumerable<RhinoObject> RhinoObjects)
		{
			// Make sure all render meshes are generated before attempting to export them.
			RhinoObject.GetRenderMeshes(RhinoObjects, /*okToCreate=*/true, /*returnAllObjects*/false);

			foreach (RhinoObject CurrentObject in RhinoObjects)
			{
				if (TryGenerateMeshInfoFromRhinoObjects(CurrentObject) is DatasmithMeshInfo MeshInfo)
				{
					ObjectIdToMeshInfoDictionary[CurrentObject.Id] = MeshInfo;
				}
			}
		}

		private DatasmithMeshInfo TryGenerateMeshInfoFromRhinoObjects(RhinoObject InRhinoObject)
		{
			DatasmithMeshInfo MeshInfo = null;
			Mesh[] RenderMeshes = null;

			if (ActiveViewportInfo != null)
			{
				//Disabling obsolete warning as GetRenderPrimitiveList() is deprecated since Rhino5 but as of Rhino7 no alternative exists.
#pragma warning disable CS0612
				RenderMeshes = InRhinoObject.GetRenderPrimitiveList(ActiveViewportInfo, false)?.ToMeshArray();
#pragma warning restore CS0612
			}
			if (RenderMeshes == null)
			{
				RenderMeshes = InRhinoObject.GetMeshes(MeshType.Render);
			}

			if (RenderMeshes != null && RenderMeshes.Length > 0)
			{
				Dictionary<Mesh, ObjectAttributes> MeshAttributePairs = new Dictionary<Mesh, ObjectAttributes>(RenderMeshes.Length);

				BrepObject CurrentBrep = (InRhinoObject as BrepObject);
				if (CurrentBrep != null && CurrentBrep.HasSubobjectMaterials)
				{
					RhinoObject[] SubObjects = CurrentBrep.GetSubObjects();

					for (int Index = 0, MaxLength = Math.Min(RenderMeshes.Length, SubObjects.Length); Index < MaxLength; ++Index)
					{
						if (RenderMeshes[Index] != null)
						{
							MeshAttributePairs[RenderMeshes[Index]] = SubObjects[Index].Attributes;
						}
					}
				}
				else
				{
					for (int RenderMeshIndex = 0; RenderMeshIndex < RenderMeshes.Length; ++RenderMeshIndex)
					{
						if (RenderMeshes[RenderMeshIndex] != null)
						{
							MeshAttributePairs[RenderMeshes[RenderMeshIndex]] = InRhinoObject.Attributes;
						}
					}
				}

				if (MeshAttributePairs.Count > 0)
				{
					MeshInfo = GenerateMeshInfo(InRhinoObject.Id, MeshAttributePairs);
				}
			}

			return MeshInfo;
		}

		private DatasmithMeshInfo GenerateMeshInfo(Guid ObjectID, IReadOnlyDictionary<Mesh, ObjectAttributes> MeshAttributePairs)
		{
			DatasmithMeshInfo MeshInfo = null;

			if (ObjectIdToHierarchyActorNodeDictionary.TryGetValue(ObjectID, out DatasmithActorInfo HierarchyActorNode))
			{
				Transform OffsetTransform = Transform.Translation(DatasmithRhinoUtilities.CenterMeshesOnPivot(MeshAttributePairs.Keys));
				List<int> MaterialIndices = new List<int>(MeshAttributePairs.Count);
				foreach (ObjectAttributes CurrentAttributes in MeshAttributePairs.Values)
				{
					int MaterialIndex = GetMaterialIndexFromAttributes(HierarchyActorNode, CurrentAttributes);
					MaterialIndices.Add(MaterialIndex);
				}

				string Name = FDatasmithFacadeElement.GetStringHash("M:" + HierarchyActorNode.Name);
				string Label = HierarchyActorNode.Label;

				MeshInfo = new DatasmithMeshInfo(MeshAttributePairs.Keys, OffsetTransform, MaterialIndices, Name, Label);
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