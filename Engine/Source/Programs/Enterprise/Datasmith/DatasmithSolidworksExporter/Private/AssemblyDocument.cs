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
		enum EComponentDirtyState
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
			public Dictionary<string, FObjectMaterials> ComponentsMaterialsMap = new Dictionary<string, FObjectMaterials>();
			public Dictionary<string, string> ComponentToPartMap = new Dictionary<string, string>();
			public HashSet<string> CleanComponents = new HashSet<string>();
			public Dictionary<string, uint> DirtyComponents = new Dictionary<string, uint>();
			public HashSet<string> ComponentsToDelete = new HashSet<string>();
		}

		private AssemblyDoc SwAsmDoc = null;
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

			SetExportStatus("Materials");
			SyncState.ComponentsMaterialsMap = FObjectMaterials.LoadDocumentMaterials(this, swDisplayStateOpts_e.swThisDisplayState, null);
			Configuration CurrentConfig = SwDoc.GetActiveConfiguration() as Configuration;

			SetExportStatus("");
			Component2 Root = CurrentConfig.GetRootComponent3(true);

			// Track components that need their mesh exported: we want to do that in parallel after the 
			// actor hierarchy has been exported
			Dictionary<Component2, string> MeshesToExportMap = new Dictionary<Component2, string>();
		
			ExportComponentRecursive(Root, null, ref MeshesToExportMap);

			// Export materials
			ConcurrentDictionary<Component2, FObjectMaterials> ComponentMaterialsMap = new ConcurrentDictionary<Component2, FObjectMaterials>();

			SetExportStatus($"Component Materials");
			Parallel.ForEach(MeshesToExportMap, KVP => 
			{
				Component2 Comp = KVP.Key;

				FObjectMaterials ComponentMaterials = FObjectMaterials.LoadComponentMaterials(this, Comp, swDisplayStateOpts_e.swThisDisplayState, null);
				if (ComponentMaterials == null && Comp.GetModelDoc2() is PartDoc)
				{
					ComponentMaterials = FObjectMaterials.LoadPartMaterials(this, Comp.GetModelDoc2() as PartDoc, swDisplayStateOpts_e.swThisDisplayState, null);
				}
				if (ComponentMaterials != null)
				{
					ComponentMaterialsMap.TryAdd(Comp, ComponentMaterials);
				}
			});

			Exporter.ExportMaterials(ExportedMaterialsMap);

			// Export meshes
			SetExportStatus($"Component Meshes");
			ConcurrentBag<Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh>> CreatedMeshes = new ConcurrentBag<Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh>>();
			Parallel.ForEach(MeshesToExportMap, KVP =>
			{
				Component2 Comp = KVP.Key;

				FObjectMaterials ComponentMaterials = null;
				ComponentMaterialsMap.TryGetValue(Comp, out ComponentMaterials);

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
			Dictionary<string, FObjectMaterials> CurrentDocMaterialsMap = FObjectMaterials.LoadDocumentMaterials(this, swDisplayStateOpts_e.swThisDisplayState, null);

			// Dig into part level materials (they wont be read by LoadDocumentMaterials)
			HashSet<string> AllExportedComponents = new HashSet<string>();
			AllExportedComponents.UnionWith(SyncState.CleanComponents);
			AllExportedComponents.UnionWith(SyncState.DirtyComponents.Keys);

			foreach (string CompName in AllExportedComponents)
			{
				if (!CurrentDocMaterialsMap?.ContainsKey(CompName) ?? false)
				{
					Component2 Comp = SwAsmDoc.GetComponentByName(CompName);
					if (Comp == null)
					{
						continue;
					}

					object ComponentDoc = (object)Comp.GetModelDoc2();

					// Check for part level materials
					FObjectMaterials ComponentMaterials = FObjectMaterials.LoadComponentMaterials(this, Comp, swDisplayStateOpts_e.swThisDisplayState, null);
					if (ComponentMaterials == null && (object)Comp.GetModelDoc2() is PartDoc)
					{
						ComponentMaterials = FObjectMaterials.LoadPartMaterials(this, ComponentDoc as PartDoc, swDisplayStateOpts_e.swThisDisplayState, null);
					}

					if (ComponentMaterials != null)
					{
						if (CurrentDocMaterialsMap == null)
						{
							CurrentDocMaterialsMap = new Dictionary<string, FObjectMaterials>();
						}
						CurrentDocMaterialsMap[CompName] = ComponentMaterials;
					}
				}
			}

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
				ActorExportInfo.Type = EActorType.SimpleActor;

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
						SyncState.PartsMap[PartPath] = new FPartDocument(PartDocId, ComponentDoc as PartDoc, Exporter);
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

		private void SetComponentDirty(string InComponent, EComponentDirtyState InState)
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

			SwAsmDoc.RegenNotify += new DAssemblyDocEvents_RegenNotifyEventHandler(OnComponentRegenNotify);
			SwAsmDoc.ActiveDisplayStateChangePreNotify += new DAssemblyDocEvents_ActiveDisplayStateChangePreNotifyEventHandler(OnComponentActiveDisplayStateChangePreNotify);
			SwAsmDoc.ActiveViewChangeNotify += new DAssemblyDocEvents_ActiveViewChangeNotifyEventHandler(OnComponentActiveViewChangeNotify);
			SwAsmDoc.SuppressionStateChangeNotify += new DAssemblyDocEvents_SuppressionStateChangeNotifyEventHandler(OnComponentSuppressionStateChangeNotify);
			SwAsmDoc.ComponentReorganizeNotify += new DAssemblyDocEvents_ComponentReorganizeNotifyEventHandler(OnComponentReorganizeNotify);
			SwAsmDoc.ActiveDisplayStateChangePostNotify += new DAssemblyDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnComponentActiveDisplayStateChangePostNotify);
			SwAsmDoc.ConfigurationChangeNotify += new DAssemblyDocEvents_ConfigurationChangeNotifyEventHandler(OnConfigurationChangeNotify);
			SwAsmDoc.DestroyNotify2 += new DAssemblyDocEvents_DestroyNotify2EventHandler(OnDocumentDestroyNotify2);
			SwAsmDoc.ComponentConfigurationChangeNotify += new DAssemblyDocEvents_ComponentConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			SwAsmDoc.UndoPostNotify += new DAssemblyDocEvents_UndoPostNotifyEventHandler(OnComponentUndoPostNotify);
			SwAsmDoc.RenamedDocumentNotify += new DAssemblyDocEvents_RenamedDocumentNotifyEventHandler(OnComponentRenamedDocumentNotify);
			SwAsmDoc.DragStateChangeNotify += new DAssemblyDocEvents_DragStateChangeNotifyEventHandler(OnComponentDragStateChangeNotify);
			SwAsmDoc.RegenPostNotify2 += new DAssemblyDocEvents_RegenPostNotify2EventHandler(OnComponentRegenPostNotify2);
			SwAsmDoc.ComponentReferredDisplayStateChangeNotify += new DAssemblyDocEvents_ComponentReferredDisplayStateChangeNotifyEventHandler(OnComponentReferredDisplayStateChangeNotify);
			SwAsmDoc.RedoPostNotify += new DAssemblyDocEvents_RedoPostNotifyEventHandler(OnComponentRedoPostNotify);
			SwAsmDoc.ComponentStateChangeNotify3 += new DAssemblyDocEvents_ComponentStateChangeNotify3EventHandler(OnComponentStateChangeNotify3);
			SwAsmDoc.ComponentStateChangeNotify += new DAssemblyDocEvents_ComponentStateChangeNotifyEventHandler(OnComponentStateChangeNotify);
			SwAsmDoc.ModifyNotify += new DAssemblyDocEvents_ModifyNotifyEventHandler(OnComponentModifyNotify);
			SwAsmDoc.DeleteItemNotify += new DAssemblyDocEvents_DeleteItemNotifyEventHandler(OnComponentDeleteItemNotify);
			SwAsmDoc.RenameItemNotify += new DAssemblyDocEvents_RenameItemNotifyEventHandler(OnComponentRenameItemNotify);
			SwAsmDoc.AddItemNotify += new DAssemblyDocEvents_AddItemNotifyEventHandler(OnComponentAddItemNotify);
			SwAsmDoc.FileReloadNotify += new DAssemblyDocEvents_FileReloadNotifyEventHandler(OnComponentFileReloadNotify);
			SwAsmDoc.ActiveConfigChangeNotify += new DAssemblyDocEvents_ActiveConfigChangeNotifyEventHandler(OnComponentActiveConfigChangeNotify);
			SwAsmDoc.FileSaveAsNotify += new DAssemblyDocEvents_FileSaveAsNotifyEventHandler(OnComponentFileSaveAsNotify);
			SwAsmDoc.FileSaveNotify += new DAssemblyDocEvents_FileSaveNotifyEventHandler(OnComponentFileSaveNotify);
			SwAsmDoc.RegenPostNotify += new DAssemblyDocEvents_RegenPostNotifyEventHandler(OnComponentRegenPostNotify);
			SwAsmDoc.ActiveConfigChangePostNotify += new DAssemblyDocEvents_ActiveConfigChangePostNotifyEventHandler(OnComponentActiveConfigChangePostNotify);
			SwAsmDoc.ComponentStateChangeNotify2 += new DAssemblyDocEvents_ComponentStateChangeNotify2EventHandler(OnComponentStateChangeNotify2);
			SwAsmDoc.AddCustomPropertyNotify += new DAssemblyDocEvents_AddCustomPropertyNotifyEventHandler(OnComponentAddCustomPropertyNotify);
			SwAsmDoc.ChangeCustomPropertyNotify += new DAssemblyDocEvents_ChangeCustomPropertyNotifyEventHandler(OnComponentChangeCustomPropertyNotify);
			SwAsmDoc.DimensionChangeNotify += new DAssemblyDocEvents_DimensionChangeNotifyEventHandler(OnComponentDimensionChangeNotify);
			SwAsmDoc.ComponentDisplayStateChangeNotify += new DAssemblyDocEvents_ComponentDisplayStateChangeNotifyEventHandler(OnComponentDisplayStateChangeNotify);
			SwAsmDoc.ComponentVisualPropertiesChangeNotify += new DAssemblyDocEvents_ComponentVisualPropertiesChangeNotifyEventHandler(OnComponentVisualPropertiesChangeNotify);
			SwAsmDoc.ComponentMoveNotify2 += new DAssemblyDocEvents_ComponentMoveNotify2EventHandler(OnComponentMoveNotify2);
			SwAsmDoc.BodyVisibleChangeNotify += new DAssemblyDocEvents_BodyVisibleChangeNotifyEventHandler(OnComponentBodyVisibleChangeNotify);
			SwAsmDoc.ComponentVisibleChangeNotify += new DAssemblyDocEvents_ComponentVisibleChangeNotifyEventHandler(OnComponentVisibleChangeNotify);
			SwAsmDoc.ComponentMoveNotify += new DAssemblyDocEvents_ComponentMoveNotifyEventHandler(OnComponentMoveNotify);
			SwAsmDoc.FileReloadPreNotify += new DAssemblyDocEvents_FileReloadPreNotifyEventHandler(OnComponentFileReloadPreNotify);
			SwAsmDoc.DeleteSelectionPreNotify += new DAssemblyDocEvents_DeleteSelectionPreNotifyEventHandler(OnComponentDeleteSelectionPreNotify);
			SwAsmDoc.FileSaveAsNotify2 += new DAssemblyDocEvents_FileSaveAsNotify2EventHandler(OnComponentFileSaveAsNotify2);

			SwAsmDoc.UserSelectionPostNotify += new DAssemblyDocEvents_UserSelectionPostNotifyEventHandler(OnUserSelectionPostNotify);
		}

		public override void Destroy()
		{
			base.Destroy();

			SwAsmDoc.RegenNotify -= new DAssemblyDocEvents_RegenNotifyEventHandler(OnComponentRegenNotify);
			SwAsmDoc.ActiveDisplayStateChangePreNotify -= new DAssemblyDocEvents_ActiveDisplayStateChangePreNotifyEventHandler(OnComponentActiveDisplayStateChangePreNotify);
			SwAsmDoc.ActiveViewChangeNotify -= new DAssemblyDocEvents_ActiveViewChangeNotifyEventHandler(OnComponentActiveViewChangeNotify);
			SwAsmDoc.SuppressionStateChangeNotify -= new DAssemblyDocEvents_SuppressionStateChangeNotifyEventHandler(OnComponentSuppressionStateChangeNotify);
			SwAsmDoc.ComponentReorganizeNotify -= new DAssemblyDocEvents_ComponentReorganizeNotifyEventHandler(OnComponentReorganizeNotify);
			SwAsmDoc.ActiveDisplayStateChangePostNotify -= new DAssemblyDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnComponentActiveDisplayStateChangePostNotify);
			SwAsmDoc.ConfigurationChangeNotify -= new DAssemblyDocEvents_ConfigurationChangeNotifyEventHandler(OnConfigurationChangeNotify);
			SwAsmDoc.DestroyNotify2 -= new DAssemblyDocEvents_DestroyNotify2EventHandler(OnDocumentDestroyNotify2);
			SwAsmDoc.ComponentConfigurationChangeNotify -= new DAssemblyDocEvents_ComponentConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			SwAsmDoc.UndoPostNotify -= new DAssemblyDocEvents_UndoPostNotifyEventHandler(OnComponentUndoPostNotify);
			SwAsmDoc.RenamedDocumentNotify -= new DAssemblyDocEvents_RenamedDocumentNotifyEventHandler(OnComponentRenamedDocumentNotify);
			SwAsmDoc.ComponentDisplayModeChangePostNotify -= new DAssemblyDocEvents_ComponentDisplayModeChangePostNotifyEventHandler(OnComponentDisplayModeChangePostNotify);
			SwAsmDoc.DragStateChangeNotify -= new DAssemblyDocEvents_DragStateChangeNotifyEventHandler(OnComponentDragStateChangeNotify);
			SwAsmDoc.RegenPostNotify2 -= new DAssemblyDocEvents_RegenPostNotify2EventHandler(OnComponentRegenPostNotify2);
			SwAsmDoc.ComponentReferredDisplayStateChangeNotify -= new DAssemblyDocEvents_ComponentReferredDisplayStateChangeNotifyEventHandler(OnComponentReferredDisplayStateChangeNotify);
			SwAsmDoc.RedoPostNotify -= new DAssemblyDocEvents_RedoPostNotifyEventHandler(OnComponentRedoPostNotify);
			SwAsmDoc.ComponentStateChangeNotify3 -= new DAssemblyDocEvents_ComponentStateChangeNotify3EventHandler(OnComponentStateChangeNotify3);
			SwAsmDoc.ComponentStateChangeNotify -= new DAssemblyDocEvents_ComponentStateChangeNotifyEventHandler(OnComponentStateChangeNotify);
			SwAsmDoc.ModifyNotify -= new DAssemblyDocEvents_ModifyNotifyEventHandler(OnComponentModifyNotify);
			SwAsmDoc.DeleteItemNotify -= new DAssemblyDocEvents_DeleteItemNotifyEventHandler(OnComponentDeleteItemNotify);
			SwAsmDoc.RenameItemNotify -= new DAssemblyDocEvents_RenameItemNotifyEventHandler(OnComponentRenameItemNotify);
			SwAsmDoc.AddItemNotify -= new DAssemblyDocEvents_AddItemNotifyEventHandler(OnComponentAddItemNotify);
			SwAsmDoc.FileReloadNotify -= new DAssemblyDocEvents_FileReloadNotifyEventHandler(OnComponentFileReloadNotify);
			SwAsmDoc.ActiveConfigChangeNotify -= new DAssemblyDocEvents_ActiveConfigChangeNotifyEventHandler(OnComponentActiveConfigChangeNotify);
			SwAsmDoc.FileSaveAsNotify -= new DAssemblyDocEvents_FileSaveAsNotifyEventHandler(OnComponentFileSaveAsNotify);
			SwAsmDoc.FileSaveNotify -= new DAssemblyDocEvents_FileSaveNotifyEventHandler(OnComponentFileSaveNotify);
			SwAsmDoc.RegenPostNotify -= new DAssemblyDocEvents_RegenPostNotifyEventHandler(OnComponentRegenPostNotify);
			SwAsmDoc.ActiveConfigChangePostNotify -= new DAssemblyDocEvents_ActiveConfigChangePostNotifyEventHandler(OnComponentActiveConfigChangePostNotify);
			SwAsmDoc.ComponentStateChangeNotify2 -= new DAssemblyDocEvents_ComponentStateChangeNotify2EventHandler(OnComponentStateChangeNotify2);
			SwAsmDoc.AddCustomPropertyNotify -= new DAssemblyDocEvents_AddCustomPropertyNotifyEventHandler(OnComponentAddCustomPropertyNotify);
			SwAsmDoc.ChangeCustomPropertyNotify -= new DAssemblyDocEvents_ChangeCustomPropertyNotifyEventHandler(OnComponentChangeCustomPropertyNotify);
			SwAsmDoc.DimensionChangeNotify -= new DAssemblyDocEvents_DimensionChangeNotifyEventHandler(OnComponentDimensionChangeNotify);
			SwAsmDoc.ComponentDisplayStateChangeNotify -= new DAssemblyDocEvents_ComponentDisplayStateChangeNotifyEventHandler(OnComponentDisplayStateChangeNotify);
			SwAsmDoc.ComponentVisualPropertiesChangeNotify -= new DAssemblyDocEvents_ComponentVisualPropertiesChangeNotifyEventHandler(OnComponentVisualPropertiesChangeNotify);
			SwAsmDoc.ComponentMoveNotify2 -= new DAssemblyDocEvents_ComponentMoveNotify2EventHandler(OnComponentMoveNotify2);
			SwAsmDoc.BodyVisibleChangeNotify -= new DAssemblyDocEvents_BodyVisibleChangeNotifyEventHandler(OnComponentBodyVisibleChangeNotify);
			SwAsmDoc.ComponentVisibleChangeNotify -= new DAssemblyDocEvents_ComponentVisibleChangeNotifyEventHandler(OnComponentVisibleChangeNotify);
			SwAsmDoc.ComponentMoveNotify -= new DAssemblyDocEvents_ComponentMoveNotifyEventHandler(OnComponentMoveNotify);
			SwAsmDoc.FileReloadPreNotify -= new DAssemblyDocEvents_FileReloadPreNotifyEventHandler(OnComponentFileReloadPreNotify);
			SwAsmDoc.DeleteSelectionPreNotify -= new DAssemblyDocEvents_DeleteSelectionPreNotifyEventHandler(OnComponentDeleteSelectionPreNotify);
			SwAsmDoc.FileSaveAsNotify2 -= new DAssemblyDocEvents_FileSaveAsNotify2EventHandler(OnComponentFileSaveAsNotify2);
		}

		int OnUserSelectionPostNotify()
		{
			return 0;
		}

		//attach events to a component if it becomes resolved
		int OnComponentStateChangeNotify(object componentModel, short newCompState)
		{
			//ModelDoc2 modDoc = (ModelDoc2)componentModel;
			//swComponentSuppressionState_e newState = (swComponentSuppressionState_e)newCompState;

			//switch (newState)
			//{

			//	case swComponentSuppressionState_e.swComponentFullyResolved:
			//	{
			//		if ((modDoc != null) & !this.swAddin.OpenDocs.Contains(modDoc))
			//		{
			//			this.swAddin.AttachModelDocEventHandler(modDoc);
			//		}
			//		break;
			//	}

			//	case swComponentSuppressionState_e.swComponentResolved:
			//	{
			//		if ((modDoc != null) & !this.swAddin.OpenDocs.Contains(modDoc))
			//		{
			//			this.swAddin.AttachModelDocEventHandler(modDoc);
			//		}
			//		break;
			//	}

			//}
			return 0;
		}

		int OnComponentStateChangeNotify(object componentModel)
		{
			OnComponentStateChangeNotify(componentModel, (short)swComponentSuppressionState_e.swComponentResolved);
			return 0;
		}

		int OnComponentStateChange2Notify(object componentModel, string CompName, short oldCompState, short newCompState)
		{
			return OnComponentStateChangeNotify(componentModel, newCompState);
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

		int OnComponentRegenNotify()
		{
			return 0;
		}

		int OnComponentActiveDisplayStateChangePreNotify()
		{
			return 0;
		}

		int OnComponentActiveViewChangeNotify()
		{
			return 0;
		}

		int OnComponentSuppressionStateChangeNotify(Feature InFeature, int InNewSuppressionState, int InPreviousSuppressionState, int InConfigurationOption, ref object InConfigurationNames)
		{
			return 0;
		}

		int OnComponentReorganizeNotify(string sourceName, string targetName)
		{
			return 0;
		}

		int OnComponentActiveDisplayStateChangePostNotify(string DisplayStateName)
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

		int OnComponentUndoPostNotify()
		{
			return 0;
		}

		int OnComponentRenamedDocumentNotify(ref object RenamedDocumentInterface)
		{
			return 0;
		}

		int OnComponentDisplayModeChangePostNotify(object Component)
		{
			return 0;
		}

		int OnComponentDragStateChangeNotify(Boolean State)
		{
			return 0;
		}

		int OnComponentRegenPostNotify2(object stopFeature)
		{
			return 0;
		}

		int OnComponentReferredDisplayStateChangeNotify(object componentModel, string CompName, int oldDSId, string oldDSName, int newDSId, string newDSName)
		{
			return 0;
		}

		int OnComponentRedoPostNotify()
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

		int OnComponentModifyNotify()
		{
			return 0;
		}

		int OnComponentDeleteItemNotify(int InEntityType, string InItemName)
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

		int OnComponentRenameItemNotify(int InEntityType, string InOldName, string InNewName)
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

		int OnComponentAddItemNotify(int InEntityType, string InItemName)
		{
			if (InEntityType == (int)swNotifyEntityType_e.swNotifyComponent)
			{
				SetDirty(true);
			}
			return 0;
		}

		int OnComponentFileReloadNotify()
		{
			return 0;
		}

		int OnComponentActiveConfigChangeNotify()
		{
			return 0;
		}

		int OnComponentFileSaveAsNotify(string FileName)
		{
			return 0;
		}

		int OnComponentFileSaveNotify(string FileName)
		{
			return 0;
		}

		int OnComponentRegenPostNotify()
		{
			return 0;
		}

		int OnComponentActiveConfigChangePostNotify()
		{
			return 0;
		}

		int OnComponentStateChangeNotify2(object componentModel, string CompName, short oldCompState, short newCompState)
		{
			return 0;
		}

		int OnComponentAddCustomPropertyNotify(string propName, string Configuration, string Value, int valueType)
		{
			return 0;
		}

		int OnComponentChangeCustomPropertyNotify(string propName, string Configuration, string oldValue, string NewValue, int valueType)
		{
			return 0;
		}

		int OnComponentDimensionChangeNotify(object displayDim)
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

		int OnComponentBodyVisibleChangeNotify()
		{
			return 0;
		}

		int OnComponentVisibleChangeNotify()
		{
			return 0;
		}

		// called when the component moves because of Motion Manager or explode/collapse
		int OnComponentMoveNotify()
		{

			//if (SwSingleton.CurrentScene.bDirectLinkAutoSync)
			//{
			//	SwSingleton.CurrentScene.EvaluateSceneTransforms();
			//	SwSingleton.CurrentScene.bIsDirty = true;
			//}

			return 0;
		}

		int OnComponentFileReloadPreNotify()
		{
			return 0;
		}

		int OnComponentDeleteSelectionPreNotify()
		{
			return 0;
		}

		int OnComponentFileSaveAsNotify2(string FileName)
		{
			return 0;
		}
	}
}