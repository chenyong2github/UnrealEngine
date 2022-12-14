// Copyright Epic Games, Inc. All Rights Reserved.

using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading.Tasks;

namespace DatasmithSolidworks
{
	public class FAssemblyDocument : FDocument
	{
		public enum EComponentDirtyState
		{
			Geometry,
			Visibility,
			Material,
			Transform,
			Delete
		};

		public class FSyncState
		{
			public Dictionary<string, FPartDocument> PartsMap = new Dictionary<string, FPartDocument>();
			public ConcurrentDictionary<FComponentName, FObjectMaterials> ComponentsMaterialsMap = new ConcurrentDictionary<FComponentName, FObjectMaterials>();
			public Dictionary<FComponentName, string> ComponentToPartMap = new Dictionary<FComponentName, string>();
			public Dictionary<FComponentName, float[]> ComponentsTransformsMap = new Dictionary<FComponentName, float[]>();

			/// All updated(not dirty) components. Updated component will be removed from dirty on export end
			public HashSet<FComponentName> CleanComponents = new HashSet<FComponentName>();

			/** Flags for each component that needs update. Cleared when export completes*/
			public Dictionary<FComponentName, uint> DirtyComponents = new Dictionary<FComponentName, uint>();

			public HashSet<FComponentName> ComponentsToDelete = new HashSet<FComponentName>();
			public Dictionary<FComponentName, Component2> ExportedComponentsMap = new Dictionary<FComponentName, Component2>();
			/** Stores which mesh was exported for each component*/
			public Dictionary<FComponentName, string> ComponentNameToMeshNameMap = new Dictionary<FComponentName, string>();
		}

		public AssemblyDoc SwAsmDoc { get; private set; } = null;

		public FSyncState SyncState { get; private set; } = new FSyncState();

		public FAssemblyDocument(int InDocId, AssemblyDoc InSwDoc, FDatasmithExporter InExporter) : base(InDocId, InSwDoc as ModelDoc2, InExporter)
		{
			SwAsmDoc = InSwDoc;
		}

		public override void PreExport(FMeshes Meshes, bool bConfigurations)
		{
		}

		// Export to datasmith scene extracted configurations data
		public override void ExportToDatasmithScene(FConfigurationExporter ConfigurationExporter)
		{
			FSyncState OldSyncState = SyncState;

			if (bFileExportInProgress)
			{
				// todo: separate DL from file export more explicitly
				SyncState = new FSyncState();
			}

			SetExportStatus("Actors");

			ProcessComponentsPendingDelete();

			Configuration CurrentConfig = SwDoc.GetActiveConfiguration() as Configuration;

			// Configurations combined tree should have single child(root component of each config is the same)
			Debug.Assert(ConfigurationExporter.CombinedTree.Children.Count == 1);
			ExportComponentRecursive(CurrentConfig.Name, ConfigurationExporter, ConfigurationExporter.CombinedTree.Children[0], null);
			SyncState.DirtyComponents.Clear();

			SyncState = OldSyncState;

			// todo: check animation export for DL
			// Export animations (only allow when exporting to file)
			// if (bFileExportInProgress)
			{
				Component2 Root = CurrentConfig.GetRootComponent3(true);
				List<FAnimation> Animations = FAnimationExtractor.ExtractAnimations(SwAsmDoc, Root);
				if (Animations != null)
				{
					SetExportStatus($"Animations");

					foreach (FAnimation Anim in Animations)
					{
						Exporter.ExportAnimation(Anim);
					}
				}
			}
		}

		private void ProcessComponentsPendingDelete()
		{
			foreach (FComponentName CompName in SyncState.ComponentsToDelete)
			{
				FActorName ActorName = CompName.GetActorName();
				SyncState.ComponentToPartMap.Remove(CompName);
				SyncState.ExportedComponentsMap.Remove(CompName);

				if (SyncState.ComponentNameToMeshNameMap.TryGetValue(CompName, out string MeshName))
				{
					SyncState.ComponentNameToMeshNameMap.Remove(CompName);
					Exporter.RemoveMesh(MeshName);
				}

				SyncState.ComponentsMaterialsMap.TryRemove(CompName, out FObjectMaterials _);
				SyncState.ComponentsTransformsMap.Remove(CompName);
				Exporter.RemoveActor(ActorName);
			}
			SyncState.ComponentsToDelete.Clear();
		}

		public override ConcurrentDictionary<FComponentName, FObjectMaterials> LoadDocumentMaterials(HashSet<FComponentName> ComponentNamesToExportSet)
		{
			return FObjectMaterials.LoadAssemblyMaterials(this, ComponentNamesToExportSet, swDisplayStateOpts_e.swThisDisplayState, null);
		}

		public override void AddComponentMaterials(FComponentName ComponentName, FObjectMaterials Materials)
		{
			Addin.Instance.LogDebug($"AddComponentMaterials: {ComponentName} - {Materials}");
			SyncState.ComponentsMaterialsMap[ComponentName] = Materials;
		}

		public override void AddMeshForComponent(FComponentName ComponentName, string MeshName)
		{
			SyncState.ComponentNameToMeshNameMap[ComponentName] = MeshName;
		}

		public override FObjectMaterials GetComponentMaterials(Component2 Comp)
		{
			FObjectMaterials ComponentMaterials = null;
			SyncState.ComponentsMaterialsMap?.TryGetValue(new FComponentName(Comp), out ComponentMaterials);
			return ComponentMaterials;
		}

		// Export to datasmith scene current configuration only
		public override void ExportToDatasmithScene(FMeshes meshes)
		{
			FSyncState OldSyncState = SyncState;

			if (bFileExportInProgress)
			{
				SyncState = new FSyncState();
			}

			SetExportStatus("Actors");
			ProcessComponentsPendingDelete();

			Configuration CurrentConfig = SwDoc.GetActiveConfiguration() as Configuration;

			SetExportStatus("");
			Component2 Root = CurrentConfig.GetRootComponent3(true);

			// Track components that need their mesh exported: we want to do that in parallel after the 
			// actor hierarchy has been exported
			Dictionary<Component2, FActorName> MeshesToExportMap = new Dictionary<Component2, FActorName>();
		
			ExportComponentRecursive(Root, null, MeshesToExportMap);

			// Export materials
			SetExportStatus($"Component Materials");

			HashSet<FComponentName> ComponentNamesToExportSet = new HashSet<FComponentName>();
			foreach (var KVP in MeshesToExportMap)
			{
				FComponentName ComponentName = new FComponentName(KVP.Key);
				if (!ComponentNamesToExportSet.Contains(ComponentName))
				{
					ComponentNamesToExportSet.Add(ComponentName);
				}
			}

			ConcurrentDictionary<FComponentName, FObjectMaterials> ModifiedComponentsMaterials = FObjectMaterials.LoadAssemblyMaterials(this, ComponentNamesToExportSet, swDisplayStateOpts_e.swThisDisplayState, null);

			if (ModifiedComponentsMaterials != null)
			{
				foreach (var MatKVP in ModifiedComponentsMaterials)
				{
					SyncState.ComponentsMaterialsMap[MatKVP.Key] = MatKVP.Value;
				}
			}

			Exporter.ExportMaterials(ExportedMaterialsMap);

			// Export meshes
			SetExportStatus($"Component Meshes");
			ConcurrentDictionary<Component2, Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh>> CreatedMeshes = new ConcurrentDictionary<Component2, Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh>>();
			
			Parallel.ForEach(MeshesToExportMap, KVP =>
			{
				Component2 Comp = KVP.Key;

				FObjectMaterials ComponentMaterials = null;
				SyncState.ComponentsMaterialsMap?.TryGetValue(new FComponentName(Comp), out ComponentMaterials);

				ConcurrentBag<FBody> Bodies = FBody.FetchBodies(Comp);
				FMeshData MeshData = FStripGeometry.CreateMeshData(Bodies, ComponentMaterials);

				if (MeshData != null)
				{
					Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh> NewMesh = null;
					if (Exporter.ExportMesh($"{KVP.Value}_Mesh", MeshData, KVP.Value, out NewMesh))
					{
						CreatedMeshes[Comp] = NewMesh;
					}
				}
			});

			foreach (var KVP in CreatedMeshes)
			{
				string MeshName = Exporter.AddMesh(KVP.Value);
				SyncState.ComponentNameToMeshNameMap[new FComponentName(KVP.Key)] = MeshName;
			}
			SyncState.DirtyComponents.Clear();

			SyncState = OldSyncState;

			// Export animations (only allow when exporting to file)
		//	if (bFileExportInProgress)
			{
				List<FAnimation> Animations = FAnimationExtractor.ExtractAnimations(SwAsmDoc, Root);
				if (Animations != null)
				{
					SetExportStatus($"Animations");

					foreach (FAnimation Anim in Animations)
					{
						Exporter.ExportAnimation(Anim);
					}
				}
			}
		}

		public override bool HasMaterialUpdates()
		{
			// Dig into part level materials (they wont be read by LoadDocumentMaterials)
			HashSet<FComponentName> AllExportedComponents = new HashSet<FComponentName>();
			AllExportedComponents.UnionWith(SyncState.CleanComponents);
			AllExportedComponents.UnionWith(SyncState.DirtyComponents.Keys);

			ConcurrentDictionary<FComponentName, FObjectMaterials> CurrentDocMaterialsMap = FObjectMaterials.LoadAssemblyMaterials(this, AllExportedComponents, swDisplayStateOpts_e.swThisDisplayState, null);

			if (CurrentDocMaterialsMap == null && SyncState.ComponentsMaterialsMap == null)
			{
				return false;
			}
			else if (CurrentDocMaterialsMap == null && SyncState.ComponentsMaterialsMap != null)
			{
				foreach (var KVP in SyncState.ComponentsMaterialsMap)
				{
					SetComponentDirty(KVP.Key, EComponentDirtyState.Material);
				}
				return true;
			}
			else if (CurrentDocMaterialsMap != null && SyncState.ComponentsMaterialsMap == null)
			{
				foreach (var KVP in CurrentDocMaterialsMap)
				{
					SetComponentDirty(KVP.Key, EComponentDirtyState.Material);
				}
				return true;
			}
			else
			{
				bool bHasDirtyMaterials = false;

				if (CurrentDocMaterialsMap.Count != SyncState.ComponentsMaterialsMap.Count)
				{
					IEnumerable<FComponentName> Diff1 = CurrentDocMaterialsMap.Keys.Except(SyncState.ComponentsMaterialsMap.Keys);
					IEnumerable<FComponentName> Diff2 = SyncState.ComponentsMaterialsMap.Keys.Except(CurrentDocMaterialsMap.Keys);

					HashSet<FComponentName> DiffSet = new HashSet<FComponentName>();
					DiffSet.UnionWith(Diff1);
					DiffSet.UnionWith(Diff2);

					// Components in the DiffSet have their materials changed
					foreach (FComponentName CompName in DiffSet)
					{
						bool bShouldSyncComponentMaterial = false;

						if (SyncState.ExportedComponentsMap.ContainsKey(CompName))
						{
							try
							{
								Component2 Comp = SyncState.ExportedComponentsMap[CompName];
								bShouldSyncComponentMaterial = !Comp.IsSuppressed() && (Comp.Visible == (int)swComponentVisibilityState_e.swComponentVisible);
							}
							catch { }
						}

						if (bShouldSyncComponentMaterial)
						{
							bHasDirtyMaterials = true;
							SetComponentDirty(CompName, EComponentDirtyState.Material);
						}
					}

					return bHasDirtyMaterials;
				}

				bool bHasDirtyComponents = false;

				foreach (var KVP in SyncState.ComponentsMaterialsMap)
				{
					FObjectMaterials CurrentComponentMaterials;
					if (CurrentDocMaterialsMap.TryGetValue(KVP.Key, out CurrentComponentMaterials))
					{
						if (!CurrentComponentMaterials.EqualMaterials(KVP.Value))
						{
							SetComponentDirty(KVP.Key, EComponentDirtyState.Material);
							bHasDirtyComponents = true;
						}
					}
				}

				return bHasDirtyComponents;
			}
		}

		private float[] GetComponentDatasmithTransform(Component2 InComponent)
		{
			MathTransform ComponentTransform = InComponent.GetTotalTransform(true);
			if (ComponentTransform == null)
			{
				ComponentTransform = InComponent.Transform2;
			}

			float[] DatasmithTransform = null;
			if (ComponentTransform != null)
			{
				DatasmithTransform = MathUtils.ConvertFromSolidworksTransform(ComponentTransform, 100f/*GeomScale*/);
			}
			else
			{
				DatasmithTransform = MathUtils.TransformIdentity();
			}

			return DatasmithTransform;
		}

		/// <summary>
		/// Get component transform in specified configuration
		/// </summary>
		private float[] GetComponentDatasmithTransform(FConfigurationTree.FComponentTreeNode InNode,
			FConfigurationTree.FComponentConfig ComponentConfig)
		{
			return ComponentConfig?.Transform ?? InNode.CommonConfig.Transform ?? MathUtils.TransformIdentity();
		}

		private void ExportComponentRecursive(Component2 InComponent, Component2 InParent, Dictionary<Component2, FActorName> OutMeshesToExportMap)
		{
			bool bHasDirtyTransform = false;

			FComponentName ComponentName = new FComponentName(InComponent);
			if (SyncState.ComponentsTransformsMap.ContainsKey(ComponentName))
			{
				float[] ComponentTm = GetComponentDatasmithTransform(InComponent);
				bHasDirtyTransform = !MathUtils.TransformsAreEqual(SyncState.ComponentsTransformsMap[ComponentName], ComponentTm);
			}

			if (bHasDirtyTransform || !SyncState.CleanComponents.Contains(ComponentName))
			{
				SetExportStatus(ComponentName.GetString());

				FDatasmithActorExportInfo ActorExportInfo = new FDatasmithActorExportInfo();

				FActorName ActorName = ComponentName.GetActorName();

				ActorExportInfo.Label = ComponentName.GetLabel();
				ActorExportInfo.Name = ActorName;

				if (InParent != null)
				{
					ActorExportInfo.ParentName = new FComponentName(InParent).GetActorName();
				}
				
				ActorExportInfo.bVisible = true;
				ActorExportInfo.Type = Exporter.GetExportedActorType(ActorName) ?? EActorType.SimpleActor;
				ActorExportInfo.Transform = GetComponentDatasmithTransform(InComponent);

				SyncState.ComponentsTransformsMap[ComponentName] = ActorExportInfo.Transform;

				// When exporting configurations we need to load all components to make their data available for export
				// Suppressed or lightweight components don't have this

				if (!InComponent.IsSuppressed())
				{
					dynamic ComponentVisibility = InComponent.GetVisibilityInAsmDisplayStates((int)swDisplayStateOpts_e.swThisDisplayState, null);
					if (ComponentVisibility != null)
					{
						int Visible = ComponentVisibility[0];
						if (Visible == (int)swComponentVisibilityState_e.swComponentHidden)
						{
							ActorExportInfo.bVisible = false;
						}
					}
				}
				else
				{
					ActorExportInfo.bVisible = false;
				}

				bool bNeedsGeometryExport = false;

				// ComponentDoc is null if component is suppressed or lightweight
				ModelDoc2 ComponentDoc = InComponent.GetModelDoc2();
				
				if (ComponentDoc is PartDoc) // Also possible to test document type with swDocumentTypes_e.swDocPART
				{
					bool bFirstExport = !SyncState.ExportedComponentsMap.ContainsKey(ComponentName);
					bNeedsGeometryExport = bFirstExport;

					if (!bFirstExport && SyncState.DirtyComponents.ContainsKey(ComponentName))
					{
						uint DirtyState = SyncState.DirtyComponents[ComponentName];
						bNeedsGeometryExport =
							((DirtyState & (1u << (int)EComponentDirtyState.Material)) != 0) ||
							((DirtyState & (1u << (int)EComponentDirtyState.Geometry)) != 0) ||
							((DirtyState & (1u << (int)EComponentDirtyState.Delete)) != 0);
					}
				}

				if (bNeedsGeometryExport)
				{
					//TODO this will be null for new part, think of more solid solution
					string PartPath = ComponentDoc.GetPathName();
					if (!SyncState.PartsMap.ContainsKey(PartPath))
					{
						// New part
						int PartDocId = Addin.Instance.GetDocumentId(ComponentDoc as ModelDoc2);
						SyncState.PartsMap[PartPath] = new FPartDocument(PartDocId, ComponentDoc as PartDoc, Exporter, this, ComponentName);
						SyncState.PartsMap[PartPath].Init();
					}

					SyncState.ComponentToPartMap[ComponentName] = PartPath;

					// This component has associated part document -- treat is as a mesh actor
					ActorExportInfo.Type = EActorType.MeshActor;
					
					OutMeshesToExportMap.Add(InComponent, ActorExportInfo.Name);
				}

				if (ActorExportInfo.Type == EActorType.MeshActor )
				{
					// todo: deduplicate mesh name calculation
					ActorExportInfo.MeshName = FDatasmithExporter.SanitizeName($"{ActorExportInfo.Name}_Mesh");
				}

				Exporter.ExportOrUpdateActor(ActorExportInfo);

				SyncState.CleanComponents.Add(ComponentName);
			}

			SyncState.ExportedComponentsMap[ComponentName] = InComponent;

			// Export component children
			object[] Children = (object[])InComponent.GetChildren();

			if (Children != null)
			{
				foreach (object Obj in Children)
				{
					Component2 Child = (Component2)Obj;
					ExportComponentRecursive(Child, InComponent, OutMeshesToExportMap);
				}
			}
		}

		private void ExportComponentRecursive(string ActiveConfigName,
			FConfigurationExporter ConfigurationExporter,
			FConfigurationTree.FComponentTreeNode InNode, FConfigurationTree.FComponentTreeNode InParent)
		{

			FConfigurationTree.FComponentConfig ActiveComponentConfig = InNode.Configurations?.Find(Config => Config.ConfigName == ActiveConfigName);

			if (NeedExportComponent(InNode, ActiveComponentConfig))
			{
				SetExportStatus(InNode.ComponentName.GetString());

				FDatasmithActorExportInfo ActorExportInfo = new FDatasmithActorExportInfo();

				FActorName ActorName = InNode.ActorName;

				ActorExportInfo.Label = InNode.ComponentName.GetLabel();
				ActorExportInfo.Name = ActorName;

				if (InParent != null)
				{
					ActorExportInfo.ParentName = InParent.GetActorName();
				}
				
				ActorExportInfo.bVisible = true;
				ActorExportInfo.Type = Exporter.GetExportedActorType(ActorName) ?? EActorType.SimpleActor;
				ActorExportInfo.Transform = GetComponentDatasmithTransform(InNode, ActiveComponentConfig);

				SyncState.ComponentsTransformsMap[InNode.ComponentName] = ActorExportInfo.Transform;

				ActorExportInfo.bVisible = ActiveComponentConfig?.bVisible ?? InNode.CommonConfig.bVisible;

				if (InNode.IsPartComponent())
				{
					// This component has associated part document -- treat is as a mesh actor
					ActorExportInfo.Type = EActorType.MeshActor;
				}

				if (ActorExportInfo.Type == EActorType.MeshActor )
				{
					// todo: deduplicate mesh name calculation
					ActorExportInfo.MeshName = FDatasmithExporter.SanitizeName($"{ActorExportInfo.Name}_Mesh");
				}

				Exporter.ExportOrUpdateActor(ActorExportInfo);

				SyncState.CleanComponents.Add(InNode.ComponentName);
			}

			// AddExportedComponent(InNode);

			// Export component children
			foreach (FConfigurationTree.FComponentTreeNode Child in InNode.EnumChildren())
			{
				ExportComponentRecursive(ActiveConfigName, ConfigurationExporter, Child, InNode);
			}
		}

		public override void AddExportedComponent(FConfigurationTree.FComponentTreeNode InNode)
		{
			SyncState.ExportedComponentsMap[InNode.ComponentName] = InNode.Component;
		}

		public override bool NeedExportComponent(FConfigurationTree.FComponentTreeNode InComponent, FConfigurationTree.FComponentConfig ActiveComponentConfig)
		{
			bool bHasDirtyTransform = false;
			if (SyncState.ComponentsTransformsMap.ContainsKey(InComponent.ComponentName))
			{
				float[] ComponentTm = GetComponentDatasmithTransform(InComponent, ActiveComponentConfig);
				bHasDirtyTransform =
					!MathUtils.TransformsAreEqual(SyncState.ComponentsTransformsMap[InComponent.ComponentName], ComponentTm);
			}

			bool bNeedExportComponent = bHasDirtyTransform || !SyncState.CleanComponents.Contains(InComponent.ComponentName);
			return bNeedExportComponent;
		}

		public override bool NeedGeometryExport(FConfigurationTree.FComponentTreeNode InNode, FConfigurationTree.FComponentConfig ActiveComponentConfig)
		{
			bool bNeedsGeometryExport;
			bool bFirstExport = !SyncState.ExportedComponentsMap.ContainsKey(InNode.ComponentName);
			bNeedsGeometryExport = bFirstExport;

			if (!bFirstExport && SyncState.DirtyComponents.ContainsKey(InNode.ComponentName))
			{
				uint DirtyState = SyncState.DirtyComponents[InNode.ComponentName];
				bNeedsGeometryExport =
					((DirtyState & (1u << (int)EComponentDirtyState.Material)) != 0) ||
					((DirtyState & (1u << (int)EComponentDirtyState.Geometry)) != 0) ||
					((DirtyState & (1u << (int)EComponentDirtyState.Delete)) != 0);
			}

			return bNeedsGeometryExport;
		}

		public override void AddPartDocument(FConfigurationTree.FComponentTreeNode InNode)
		{
			//TODO this will be null for new part, think of more solid solution
			if (!SyncState.PartsMap.ContainsKey(InNode.PartPath))
			{
				ModelDoc2 ComponentDoc = InNode.Component.GetModelDoc2();
				// New part
				int PartDocId = Addin.Instance.GetDocumentId(ComponentDoc as ModelDoc2);
				SyncState.PartsMap[InNode.PartPath] = new FPartDocument(PartDocId, ComponentDoc as PartDoc, Exporter, this,
					InNode.ComponentName);
				SyncState.PartsMap[InNode.PartPath].Init();
			}

			SyncState.ComponentToPartMap[InNode.ComponentName] = InNode.PartPath;
		}

		public void SetComponentDirty(FComponentName InComponent, EComponentDirtyState InState)
		{
			if (SyncState.CleanComponents.Contains(InComponent))
			{
				SyncState.CleanComponents.Remove(InComponent);
			}

			uint DirtyFlags = 0u;
			SyncState.DirtyComponents.TryGetValue(InComponent, out DirtyFlags);
			DirtyFlags |= (1u << (int)InState);
			SyncState.DirtyComponents[InComponent] = DirtyFlags;

			SetDirty(true);
		}

		public void SetComponentDirty(Component2 InComponent, EComponentDirtyState InState)
		{
			SetComponentDirty(new FComponentName(InComponent), InState);
		}

		public override void Init()
		{
			base.Init();

			SwAsmDoc.RegenNotify += new DAssemblyDocEvents_RegenNotifyEventHandler(OnRegenNotify);
			SwAsmDoc.ActiveDisplayStateChangePreNotify += new DAssemblyDocEvents_ActiveDisplayStateChangePreNotifyEventHandler(OnActiveDisplayStateChangePreNotify);
			SwAsmDoc.ActiveViewChangeNotify += new DAssemblyDocEvents_ActiveViewChangeNotifyEventHandler(OnActiveViewChangeNotify);
			SwAsmDoc.SuppressionStateChangeNotify += new DAssemblyDocEvents_SuppressionStateChangeNotifyEventHandler(OnSuppressionStateChangeNotify);
			SwAsmDoc.ComponentReorganizeNotify += new DAssemblyDocEvents_ComponentReorganizeNotifyEventHandler(OnComponentReorganizeNotify);
			SwAsmDoc.ActiveDisplayStateChangePostNotify += new DAssemblyDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnActiveDisplayStateChangePostNotify);
			SwAsmDoc.ConfigurationChangeNotify += new DAssemblyDocEvents_ConfigurationChangeNotifyEventHandler(OnConfigurationChangeNotify);
			SwAsmDoc.DestroyNotify2 += new DAssemblyDocEvents_DestroyNotify2EventHandler(OnDocumentDestroyNotify2);
			SwAsmDoc.ComponentConfigurationChangeNotify += new DAssemblyDocEvents_ComponentConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			SwAsmDoc.UndoPostNotify += new DAssemblyDocEvents_UndoPostNotifyEventHandler(OnUndoPostNotify);
			SwAsmDoc.RenamedDocumentNotify += new DAssemblyDocEvents_RenamedDocumentNotifyEventHandler(OnRenamedDocumentNotify);
			SwAsmDoc.DragStateChangeNotify += new DAssemblyDocEvents_DragStateChangeNotifyEventHandler(OnDragStateChangeNotify);
			SwAsmDoc.RegenPostNotify2 += new DAssemblyDocEvents_RegenPostNotify2EventHandler(OnRegenPostNotify2);
			SwAsmDoc.ComponentReferredDisplayStateChangeNotify += new DAssemblyDocEvents_ComponentReferredDisplayStateChangeNotifyEventHandler(OnComponentReferredDisplayStateChangeNotify);
			SwAsmDoc.RedoPostNotify += new DAssemblyDocEvents_RedoPostNotifyEventHandler(OnRedoPostNotify);
			SwAsmDoc.ComponentStateChangeNotify3 += new DAssemblyDocEvents_ComponentStateChangeNotify3EventHandler(OnComponentStateChangeNotify3);
			SwAsmDoc.ComponentStateChangeNotify += new DAssemblyDocEvents_ComponentStateChangeNotifyEventHandler(OnComponentStateChangeNotify);
			SwAsmDoc.ModifyNotify += new DAssemblyDocEvents_ModifyNotifyEventHandler(OnModifyNotify);
			SwAsmDoc.DeleteItemNotify += new DAssemblyDocEvents_DeleteItemNotifyEventHandler(OnDeleteItemNotify);
			SwAsmDoc.RenameItemNotify += new DAssemblyDocEvents_RenameItemNotifyEventHandler(OnRenameItemNotify);
			SwAsmDoc.AddItemNotify += new DAssemblyDocEvents_AddItemNotifyEventHandler(OnAddItemNotify);
			SwAsmDoc.FileReloadNotify += new DAssemblyDocEvents_FileReloadNotifyEventHandler(OnFileReloadNotify);
			SwAsmDoc.ActiveConfigChangeNotify += new DAssemblyDocEvents_ActiveConfigChangeNotifyEventHandler(OnActiveConfigChangeNotify);
			SwAsmDoc.FileSaveAsNotify += new DAssemblyDocEvents_FileSaveAsNotifyEventHandler(OnFileSaveAsNotify);
			SwAsmDoc.FileSaveNotify += new DAssemblyDocEvents_FileSaveNotifyEventHandler(OnFileSaveNotify);
			SwAsmDoc.RegenPostNotify += new DAssemblyDocEvents_RegenPostNotifyEventHandler(OnRegenPostNotify);
			SwAsmDoc.ActiveConfigChangePostNotify += new DAssemblyDocEvents_ActiveConfigChangePostNotifyEventHandler(OnActiveConfigChangePostNotify);
			SwAsmDoc.ComponentStateChangeNotify2 += new DAssemblyDocEvents_ComponentStateChangeNotify2EventHandler(OnComponentStateChangeNotify2);
			SwAsmDoc.AddCustomPropertyNotify += new DAssemblyDocEvents_AddCustomPropertyNotifyEventHandler(OnAddCustomPropertyNotify);
			SwAsmDoc.ChangeCustomPropertyNotify += new DAssemblyDocEvents_ChangeCustomPropertyNotifyEventHandler(OnChangeCustomPropertyNotify);
			SwAsmDoc.DimensionChangeNotify += new DAssemblyDocEvents_DimensionChangeNotifyEventHandler(OnDimensionChangeNotify);
			SwAsmDoc.ComponentDisplayStateChangeNotify += new DAssemblyDocEvents_ComponentDisplayStateChangeNotifyEventHandler(OnComponentDisplayStateChangeNotify);
			SwAsmDoc.ComponentVisualPropertiesChangeNotify += new DAssemblyDocEvents_ComponentVisualPropertiesChangeNotifyEventHandler(OnComponentVisualPropertiesChangeNotify);
			SwAsmDoc.ComponentMoveNotify2 += new DAssemblyDocEvents_ComponentMoveNotify2EventHandler(OnComponentMoveNotify2);
			SwAsmDoc.BodyVisibleChangeNotify += new DAssemblyDocEvents_BodyVisibleChangeNotifyEventHandler(OnBodyVisibleChangeNotify);
			SwAsmDoc.ComponentVisibleChangeNotify += new DAssemblyDocEvents_ComponentVisibleChangeNotifyEventHandler(OnComponentVisibleChangeNotify);
			SwAsmDoc.ComponentMoveNotify += new DAssemblyDocEvents_ComponentMoveNotifyEventHandler(OnComponentMoveNotify);
			SwAsmDoc.FileReloadPreNotify += new DAssemblyDocEvents_FileReloadPreNotifyEventHandler(OnFileReloadPreNotify);
			SwAsmDoc.DeleteSelectionPreNotify += new DAssemblyDocEvents_DeleteSelectionPreNotifyEventHandler(OnDeleteSelectionPreNotify);
			SwAsmDoc.FileSaveAsNotify2 += new DAssemblyDocEvents_FileSaveAsNotify2EventHandler(OnFileSaveAsNotify2);
		}

		public override void Destroy()
		{
			base.Destroy();

			SwAsmDoc.RegenNotify -= new DAssemblyDocEvents_RegenNotifyEventHandler(OnRegenNotify);
			SwAsmDoc.ActiveDisplayStateChangePreNotify -= new DAssemblyDocEvents_ActiveDisplayStateChangePreNotifyEventHandler(OnActiveDisplayStateChangePreNotify);
			SwAsmDoc.ActiveViewChangeNotify -= new DAssemblyDocEvents_ActiveViewChangeNotifyEventHandler(OnActiveViewChangeNotify);
			SwAsmDoc.SuppressionStateChangeNotify -= new DAssemblyDocEvents_SuppressionStateChangeNotifyEventHandler(OnSuppressionStateChangeNotify);
			SwAsmDoc.ComponentReorganizeNotify -= new DAssemblyDocEvents_ComponentReorganizeNotifyEventHandler(OnComponentReorganizeNotify);
			SwAsmDoc.ActiveDisplayStateChangePostNotify -= new DAssemblyDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnActiveDisplayStateChangePostNotify);
			SwAsmDoc.ConfigurationChangeNotify -= new DAssemblyDocEvents_ConfigurationChangeNotifyEventHandler(OnConfigurationChangeNotify);
			SwAsmDoc.DestroyNotify2 -= new DAssemblyDocEvents_DestroyNotify2EventHandler(OnDocumentDestroyNotify2);
			SwAsmDoc.ComponentConfigurationChangeNotify -= new DAssemblyDocEvents_ComponentConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			SwAsmDoc.UndoPostNotify -= new DAssemblyDocEvents_UndoPostNotifyEventHandler(OnUndoPostNotify);
			SwAsmDoc.RenamedDocumentNotify -= new DAssemblyDocEvents_RenamedDocumentNotifyEventHandler(OnRenamedDocumentNotify);
			SwAsmDoc.DragStateChangeNotify -= new DAssemblyDocEvents_DragStateChangeNotifyEventHandler(OnDragStateChangeNotify);
			SwAsmDoc.RegenPostNotify2 -= new DAssemblyDocEvents_RegenPostNotify2EventHandler(OnRegenPostNotify2);
			SwAsmDoc.ComponentReferredDisplayStateChangeNotify -= new DAssemblyDocEvents_ComponentReferredDisplayStateChangeNotifyEventHandler(OnComponentReferredDisplayStateChangeNotify);
			SwAsmDoc.RedoPostNotify -= new DAssemblyDocEvents_RedoPostNotifyEventHandler(OnRedoPostNotify);
			SwAsmDoc.ComponentStateChangeNotify3 -= new DAssemblyDocEvents_ComponentStateChangeNotify3EventHandler(OnComponentStateChangeNotify3);
			SwAsmDoc.ComponentStateChangeNotify -= new DAssemblyDocEvents_ComponentStateChangeNotifyEventHandler(OnComponentStateChangeNotify);
			SwAsmDoc.ModifyNotify -= new DAssemblyDocEvents_ModifyNotifyEventHandler(OnModifyNotify);
			SwAsmDoc.DeleteItemNotify -= new DAssemblyDocEvents_DeleteItemNotifyEventHandler(OnDeleteItemNotify);
			SwAsmDoc.RenameItemNotify -= new DAssemblyDocEvents_RenameItemNotifyEventHandler(OnRenameItemNotify);
			SwAsmDoc.AddItemNotify -= new DAssemblyDocEvents_AddItemNotifyEventHandler(OnAddItemNotify);
			SwAsmDoc.FileReloadNotify -= new DAssemblyDocEvents_FileReloadNotifyEventHandler(OnFileReloadNotify);
			SwAsmDoc.ActiveConfigChangeNotify -= new DAssemblyDocEvents_ActiveConfigChangeNotifyEventHandler(OnActiveConfigChangeNotify);
			SwAsmDoc.FileSaveAsNotify -= new DAssemblyDocEvents_FileSaveAsNotifyEventHandler(OnFileSaveAsNotify);
			SwAsmDoc.FileSaveNotify -= new DAssemblyDocEvents_FileSaveNotifyEventHandler(OnFileSaveNotify);
			SwAsmDoc.RegenPostNotify -= new DAssemblyDocEvents_RegenPostNotifyEventHandler(OnRegenPostNotify);
			SwAsmDoc.ActiveConfigChangePostNotify -= new DAssemblyDocEvents_ActiveConfigChangePostNotifyEventHandler(OnActiveConfigChangePostNotify);
			SwAsmDoc.ComponentStateChangeNotify2 -= new DAssemblyDocEvents_ComponentStateChangeNotify2EventHandler(OnComponentStateChangeNotify2);
			SwAsmDoc.AddCustomPropertyNotify -= new DAssemblyDocEvents_AddCustomPropertyNotifyEventHandler(OnAddCustomPropertyNotify);
			SwAsmDoc.ChangeCustomPropertyNotify -= new DAssemblyDocEvents_ChangeCustomPropertyNotifyEventHandler(OnChangeCustomPropertyNotify);
			SwAsmDoc.DimensionChangeNotify -= new DAssemblyDocEvents_DimensionChangeNotifyEventHandler(OnDimensionChangeNotify);
			SwAsmDoc.ComponentDisplayStateChangeNotify -= new DAssemblyDocEvents_ComponentDisplayStateChangeNotifyEventHandler(OnComponentDisplayStateChangeNotify);
			SwAsmDoc.ComponentVisualPropertiesChangeNotify -= new DAssemblyDocEvents_ComponentVisualPropertiesChangeNotifyEventHandler(OnComponentVisualPropertiesChangeNotify);
			SwAsmDoc.ComponentMoveNotify2 -= new DAssemblyDocEvents_ComponentMoveNotify2EventHandler(OnComponentMoveNotify2);
			SwAsmDoc.BodyVisibleChangeNotify -= new DAssemblyDocEvents_BodyVisibleChangeNotifyEventHandler(OnBodyVisibleChangeNotify);
			SwAsmDoc.ComponentVisibleChangeNotify -= new DAssemblyDocEvents_ComponentVisibleChangeNotifyEventHandler(OnComponentVisibleChangeNotify);
			SwAsmDoc.ComponentMoveNotify -= new DAssemblyDocEvents_ComponentMoveNotifyEventHandler(OnComponentMoveNotify);
			SwAsmDoc.FileReloadPreNotify -= new DAssemblyDocEvents_FileReloadPreNotifyEventHandler(OnFileReloadPreNotify);
			SwAsmDoc.DeleteSelectionPreNotify -= new DAssemblyDocEvents_DeleteSelectionPreNotifyEventHandler(OnDeleteSelectionPreNotify);
			SwAsmDoc.FileSaveAsNotify2 -= new DAssemblyDocEvents_FileSaveAsNotify2EventHandler(OnFileSaveAsNotify2);
		}

		int OnComponentStateChangeNotify(object componentModel, short newCompState)
		{
			return 0;
		}

		int OnComponentStateChangeNotify(object componentModel)
		{
			OnComponentStateChangeNotify(componentModel, (short)swComponentSuppressionState_e.swComponentResolved);
			return 0;
		}

		int OnComponentStateChangeNotify(object componentModel, short oldCompState, short newCompState)
		{
			return OnComponentStateChangeNotify(componentModel, newCompState);
		}

		int OnComponentDisplayStateChangeNotify(object InComponentObj)
		{
			if (InComponentObj is Component2 Comp)
			{
				SetComponentDirty(Comp, EComponentDirtyState.Visibility);
			}
			return 0;
		}

		int OnComponentVisualPropertiesChangeNotify(object InCompObject)
		{
			if (InCompObject is Component2 Comp)
			{
				SetComponentDirty(Comp, EComponentDirtyState.Material);
			}
			return 0;
		}

		int OnRegenNotify()
		{
			return 0;
		}

		int OnActiveDisplayStateChangePreNotify()
		{
			return 0;
		}

		int OnActiveViewChangeNotify()
		{
			return 0;
		}

		int OnSuppressionStateChangeNotify(Feature InFeature, int InNewSuppressionState, int InPreviousSuppressionState, int InConfigurationOption, ref object InConfigurationNames)
		{
			return 0;
		}

		int OnComponentReorganizeNotify(string sourceName, string targetName)
		{
			return 0;
		}

		int OnActiveDisplayStateChangePostNotify(string DisplayStateName)
		{
			return 0;
		}

		int OnConfigurationChangeNotify(string ConfigurationName, object Object, int ObjectType, int changeType)
		{
			return 0;
		}

		int OnDocumentDestroyNotify2(int DestroyType)
		{
			Addin.Instance.CloseDocument(DocId);
			return 0;
		}

		int OnComponentConfigurationChangeNotify(string componentName, string oldConfigurationName, string newConfigurationName)
		{
			SetComponentDirty(FComponentName.FromApiString(componentName), EComponentDirtyState.Geometry);
			return 0;
		}

		int OnUndoPostNotify()
		{
			// Check each exported component's transform for changes, since 
			// this callback does not tell us what changed (and there's no other way to know that)!
			foreach (var KVP in SyncState.ExportedComponentsMap)
			{
				Component2 Comp = KVP.Value;
				float[] PrevCompTransform = null;
				if (SyncState.ComponentsTransformsMap.TryGetValue(new FComponentName(Comp), out PrevCompTransform))
				{
					float[] CompTransform = GetComponentDatasmithTransform(Comp);

					if (!MathUtils.TransformsAreEqual(CompTransform, PrevCompTransform))
					{
						SetDirty(true);
						break;
					}
				}
			}
			return 0;
		}

		int OnRenamedDocumentNotify(ref object RenamedDocumentInterface)
		{
			return 0;
		}

		int OnDisplayModeChangePostNotify(object Component)
		{
			return 0;
		}

		int OnDragStateChangeNotify(Boolean State)
		{
			return 0;
		}

		int OnRegenPostNotify2(object stopFeature)
		{
			return 0;
		}

		int OnComponentReferredDisplayStateChangeNotify(object componentModel, string CompName, int oldDSId, string oldDSName, int newDSId, string newDSName)
		{
			return 0;
		}

		int OnRedoPostNotify()
		{
			return 0;
		}

		int OnComponentStateChangeNotify3(object InComponentObj, string InCompName, short InOldCompState, short InNewCompState)
		{
			if (InComponentObj is Component2 Comp)
			{
				SetComponentDirty(new FComponentName(Comp), EComponentDirtyState.Visibility);
			}
			return 0;
		}

		int OnModifyNotify()
		{
			return 0;
		}

		int OnDeleteItemNotify(int InEntityType, string InItemName)
		{
			FComponentName ComponentName = FComponentName.FromApiString(InItemName);

			if (InEntityType == (int)swNotifyEntityType_e.swNotifyComponent && 
				SyncState.ComponentToPartMap.ContainsKey(ComponentName) && 
				!SyncState.ComponentsToDelete.Contains(ComponentName))
			{
				SyncState.ComponentsToDelete.Add(ComponentName);
				SetComponentDirty(ComponentName, EComponentDirtyState.Delete);
			}
			return 0;
		}

		int OnRenameItemNotify(int InEntityType, string InOldName, string InNewName)
		{
			FComponentName ComponentName = FComponentName.FromApiString(InOldName);

			if (InEntityType == (int)swNotifyEntityType_e.swNotifyComponent &&
			    SyncState.ComponentToPartMap.ContainsKey(ComponentName) &&
			    !SyncState.ComponentsToDelete.Contains(ComponentName))
			{
				SyncState.ComponentsToDelete.Add(ComponentName);
				SetComponentDirty(ComponentName, EComponentDirtyState.Delete);
			}
			return 0;
		}

		int OnAddItemNotify(int InEntityType, string InItemName)
		{
			if (InEntityType == (int)swNotifyEntityType_e.swNotifyComponent)
			{
				SetDirty(true);
			}
			return 0;
		}

		int OnFileReloadNotify()
		{
			return 0;
		}

		int OnActiveConfigChangeNotify()
		{
			SetDirty(true);
			SyncState.CleanComponents?.Clear();
			SyncState.DirtyComponents?.Clear();
			SyncState.ExportedComponentsMap?.Clear();
			return 0;
		}

		int OnFileSaveAsNotify(string FileName)
		{
			return 0;
		}

		int OnFileSaveNotify(string FileName)
		{
			return 0;
		}

		int OnRegenPostNotify()
		{
			return 0;
		}

		int OnActiveConfigChangePostNotify()
		{
			return 0;
		}

		int OnComponentStateChangeNotify2(object componentModel, string CompName, short oldCompState, short newCompState)
		{
			return 0;
		}

		int OnAddCustomPropertyNotify(string propName, string Configuration, string Value, int valueType)
		{
			return 0;
		}

		int OnChangeCustomPropertyNotify(string propName, string Configuration, string oldValue, string NewValue, int valueType)
		{
			return 0;
		}

		int OnDimensionChangeNotify(object displayDim)
		{
			return 0;
		}

		int OnComponentMoveNotify2(ref object InComponents)
		{
			object[] ObjComps = InComponents as object[];
			foreach (object ObjComp in ObjComps)
			{
				IComponent2 Comp = ObjComp as IComponent2;
				if (Comp != null)
				{
					SetComponentDirty(new FComponentName(Comp), EComponentDirtyState.Transform);
				}
			}
			return 0;
		}

		int OnBodyVisibleChangeNotify()
		{
			return 0;
		}

		int OnComponentVisibleChangeNotify()
		{
			return 0;
		}

		int OnComponentMoveNotify()
		{
			return 0;
		}

		int OnFileReloadPreNotify()
		{
			return 0;
		}

		int OnDeleteSelectionPreNotify()
		{
			return 0;
		}

		int OnFileSaveAsNotify2(string FileName)
		{
			return 0;
		}
	}
}