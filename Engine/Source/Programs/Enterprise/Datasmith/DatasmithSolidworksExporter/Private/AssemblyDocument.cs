// Copyright Epic Games, Inc. All Rights Reserved.

using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
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

		class FSyncState
		{
			public Dictionary<string, FPartDocument> PartsMap = new Dictionary<string, FPartDocument>();
			public ConcurrentDictionary<string, FObjectMaterials> ComponentsMaterialsMap = new ConcurrentDictionary<string, FObjectMaterials>();
			public Dictionary<string, string> ComponentToPartMap = new Dictionary<string, string>();
			public HashSet<string> CleanComponents = new HashSet<string>();
			public Dictionary<string, uint> DirtyComponents = new Dictionary<string, uint>();
			public HashSet<string> ComponentsToDelete = new HashSet<string>();
		}

		public AssemblyDoc SwAsmDoc { get; private set; } = null;

		private FSyncState SyncState = new FSyncState();

		public FAssemblyDocument(int InDocId, AssemblyDoc InSwDoc, FDatasmithExporter InExporter) : base(InDocId, InSwDoc as ModelDoc2, InExporter)
		{
			SwAsmDoc = InSwDoc;
		}

		public override void ExportToDatasmithScene()
		{
			FSyncState OldSyncState = SyncState;

			if (bFileExportInProgress)
			{
				SyncState = new FSyncState();
			}

			SetExportStatus("Actors");
			foreach (string CompName in SyncState.ComponentsToDelete)
			{
				string ActorName = FDatasmithExporter.SanitizeName(CompName);
				SyncState.ComponentToPartMap.Remove(CompName);
				Exporter.RemoveActor(ActorName);
			}

			Configuration CurrentConfig = SwDoc.GetActiveConfiguration() as Configuration;

			SetExportStatus("");
			Component2 Root = CurrentConfig.GetRootComponent3(true);

			// Track components that need their mesh exported: we want to do that in parallel after the 
			// actor hierarchy has been exported
			Dictionary<Component2, string> MeshesToExportMap = new Dictionary<Component2, string>();
		
			ExportComponentRecursive(Root, null, ref MeshesToExportMap);

			// Export materials
			SetExportStatus($"Component Materials");

			HashSet<string> ComponentNamesToExportSet = new HashSet<string>();
			foreach (var KVP in MeshesToExportMap)
			{
				if (!ComponentNamesToExportSet.Contains(KVP.Key.Name))
				{
					ComponentNamesToExportSet.Add(KVP.Key.Name);
				}
				
			}
			SyncState.ComponentsMaterialsMap = FObjectMaterials.LoadAssemblyMaterials(this, ComponentNamesToExportSet, swDisplayStateOpts_e.swThisDisplayState, null);
			Exporter.ExportMaterials(ExportedMaterialsMap);

			// Export meshes
			SetExportStatus($"Component Meshes");
			ConcurrentBag<Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh>> CreatedMeshes = new ConcurrentBag<Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh>>();
			Parallel.ForEach(MeshesToExportMap, KVP =>
			{
				Component2 Comp = KVP.Key;

				FObjectMaterials ComponentMaterials = null;
				SyncState.ComponentsMaterialsMap?.TryGetValue(Comp.Name2, out ComponentMaterials);

				ConcurrentBag<FBody> Bodies = FBody.FetchBodies(Comp);
				FMeshData MeshData = FStripGeometry.CreateMeshData(Bodies, ComponentMaterials);

				if (MeshData != null)
				{
					Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh> NewMesh = null;
					Exporter.ExportMesh($"{KVP.Value}_Mesh", MeshData, KVP.Value, out NewMesh);

					if (NewMesh != null)
					{
						CreatedMeshes.Add(NewMesh);
					}
				}
			});
			// Adding stuff to a datasmith scene cannot be multithreaded!
			foreach (Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh> MeshPair in CreatedMeshes)
			{
				DatasmithScene.AddMesh(MeshPair.Item1);
			}

			SyncState.ComponentsToDelete.Clear();
			SyncState.DirtyComponents.Clear();

			SyncState = OldSyncState;
		}

		public override bool HasMaterialUpdates()
		{
			// Dig into part level materials (they wont be read by LoadDocumentMaterials)
			HashSet<string> AllExportedComponents = new HashSet<string>();
			AllExportedComponents.UnionWith(SyncState.CleanComponents);
			AllExportedComponents.UnionWith(SyncState.DirtyComponents.Keys);

			ConcurrentDictionary<string, FObjectMaterials> CurrentDocMaterialsMap = FObjectMaterials.LoadAssemblyMaterials(this, AllExportedComponents, swDisplayStateOpts_e.swThisDisplayState, null);

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
				if (CurrentDocMaterialsMap.Count != SyncState.ComponentsMaterialsMap.Count)
				{
					IEnumerable<string> Diff1 = CurrentDocMaterialsMap.Keys.Except(SyncState.ComponentsMaterialsMap.Keys);
					IEnumerable<string> Diff2 = SyncState.ComponentsMaterialsMap.Keys.Except(CurrentDocMaterialsMap.Keys);

					HashSet<string> DiffSet = new HashSet<string>();
					DiffSet.UnionWith(Diff1);
					DiffSet.UnionWith(Diff2);

					// Components in the DiffSet have their materials changed
					foreach (string CompName in DiffSet)
					{
						SetComponentDirty(CompName, EComponentDirtyState.Material);
					}

					return true;
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

		private void ExportComponentRecursive(Component2 InComponent, Component2 InParent, ref Dictionary<Component2, string> OutMeshesToExportMap)
		{
			if (!SyncState.CleanComponents.Contains(InComponent.Name2))
			{
				SetExportStatus(InComponent.Name2);

				MathTransform ComponentTransform = InComponent.GetTotalTransform(true);

				if (ComponentTransform == null)
				{
					ComponentTransform = InComponent.Transform2;
				}
			
				FDatasmithActorExportInfo ActorExportInfo = new FDatasmithActorExportInfo();

				string ComponentName = FDatasmithExporter.SanitizeName(InComponent.Name2);
				string[] NameComponents = ComponentName.Split('/');

				ActorExportInfo.Label = NameComponents.Last();
				ActorExportInfo.Name = ComponentName;
				ActorExportInfo.ParentName = InParent?.Name2;
				ActorExportInfo.bVisible = true;
				ActorExportInfo.Type = Exporter.GetExportedActorType(ComponentName) ?? EActorType.SimpleActor;

				if (ComponentTransform != null)
				{
					ActorExportInfo.Transform = MathUtils.ConvertFromSolidworksTransform(ComponentTransform, 100f/*GeomScale*/);
				}

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

				bool bNeedsGeometryExport = !InComponent.IsSuppressed() && (InComponent.GetModelDoc2() is PartDoc);

				if (bNeedsGeometryExport && SyncState.DirtyComponents.ContainsKey(InComponent.Name2))
				{
					uint DirtyState = SyncState.DirtyComponents[InComponent.Name2];
					bNeedsGeometryExport = 
						((DirtyState & (1u << (int)EComponentDirtyState.Material)) != 0) || 
						((DirtyState & (1u << (int)EComponentDirtyState.Geometry)) != 0) || 
						((DirtyState & (1u << (int)EComponentDirtyState.Delete)) != 0); 
				}

				if (bNeedsGeometryExport)
				{
					object ComponentDoc = (object)InComponent.GetModelDoc2();

					//TODO this will be null for new part, think of more solid solution
					string PartPath = (ComponentDoc as ModelDoc2).GetPathName();
					if (!SyncState.PartsMap.ContainsKey(InComponent.Name2))
					{
						// New part
						int PartDocId = Addin.Instance.GetDocumentId(ComponentDoc as ModelDoc2);
						SyncState.PartsMap[PartPath] = new FPartDocument(PartDocId, ComponentDoc as PartDoc, Exporter, this, InComponent.Name2);
						SyncState.PartsMap[PartPath].Init();
					}

					SyncState.ComponentToPartMap[InComponent.Name2] = PartPath;

					// This component has associated part document -- treat is as a mesh actor
					ActorExportInfo.Type = EActorType.MeshActor;
					
					OutMeshesToExportMap.Add(InComponent, ActorExportInfo.Name);
				}

				Exporter.ExportOrUpdateActor(ActorExportInfo);

				SyncState.CleanComponents.Add(InComponent.Name2);
			}

			// Export component children
			object[] Children = (object[])InComponent.GetChildren();

			foreach (object Obj in Children)
			{
				Component2 Child = (Component2)Obj;
				ExportComponentRecursive(Child, InComponent, ref OutMeshesToExportMap);
			}
		}

		public void SetComponentDirty(string InComponent, EComponentDirtyState InState)
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
				SetComponentDirty(Comp.Name2, EComponentDirtyState.Visibility);
			}
			return 0;
		}

		int OnComponentVisualPropertiesChangeNotify(object InCompObject)
		{
			if (InCompObject is Component2 Comp)
			{
				SetComponentDirty(Comp.Name2, EComponentDirtyState.Material);
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
			SetDirty(true);
			SyncState.CleanComponents?.Clear();
			SyncState.DirtyComponents?.Clear();
			return 0;
		}

		int OnDocumentDestroyNotify2(int DestroyType)
		{
			Addin.Instance.CloseDocument(DocId);
			return 0;
		}

		int OnComponentConfigurationChangeNotify(string componentName, string oldConfigurationName, string newConfigurationName)
		{
			SetComponentDirty(componentName, EComponentDirtyState.Geometry);
			return 0;
		}

		int OnUndoPostNotify()
		{
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
				SetComponentDirty(Comp.Name2, EComponentDirtyState.Visibility);
			}
			return 0;
		}

		int OnModifyNotify()
		{
			return 0;
		}

		int OnDeleteItemNotify(int InEntityType, string InItemName)
		{
			if (InEntityType == (int)swNotifyEntityType_e.swNotifyComponent && 
				SyncState.ComponentToPartMap.ContainsKey(InItemName) && 
				!SyncState.ComponentsToDelete.Contains(InItemName))
			{
				SyncState.ComponentsToDelete.Add(InItemName);
				SetComponentDirty(InItemName, EComponentDirtyState.Delete);
			}
			return 0;
		}

		int OnRenameItemNotify(int InEntityType, string InOldName, string InNewName)
		{
			if (InEntityType == (int)swNotifyEntityType_e.swNotifyComponent &&
				SyncState.ComponentToPartMap.ContainsKey(InOldName) &&
				!SyncState.ComponentsToDelete.Contains(InOldName))
			{
				SyncState.ComponentsToDelete.Add(InOldName);
				SetComponentDirty(InOldName, EComponentDirtyState.Delete);
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
					SetComponentDirty(Comp.Name2, EComponentDirtyState.Transform);
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