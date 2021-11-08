// Copyright Epic Games, Inc. All Rights Reserved.

using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System;
using System.Collections.Concurrent;
using System.IO;

namespace DatasmithSolidworks
{
	public class FPartDocument : FDocument
	{
		private PartDoc SwPartDoc = null;
		private FAssemblyDocument AsmDoc = null;
		private string ComponentName = null;

		public FObjectMaterials ExportedPartMaterials { get; set; }

		public string PathName { get; private set; }

		public FPartDocument(int InDocId, PartDoc InPartDoc, FDatasmithExporter InExporter, FAssemblyDocument InAsmDoc, string InComponentName) 
			: base(InDocId, InPartDoc as ModelDoc2, InExporter)
		{
			SwPartDoc = InPartDoc;
			AsmDoc = InAsmDoc;
			ComponentName = InComponentName;

			PathName = SwDoc?.GetPathName() ?? "";
			if (string.IsNullOrEmpty(PathName))
			{
				// Unsaved imported parts have no path or name
				PathName = SwDoc.GetExternalReferenceName();
			}
		}

		static public string GetBodyPath(PartDoc InDoc, IBody2 InBody)
		{
			ModelDoc2 SwDoc = InDoc as ModelDoc2;
			string Path = "";
			if (SwDoc != null && InBody != null)
			{
				try
				{
					string Title = SwDoc.GetPathName();
					string Name = InBody.Name;
					Path = Title + "\\\\" + Name;
				}
				catch { }
			}
			return Path;
		}

		static public string GetFeaturePath(PartDoc InDoc, IFeature InFeature)
		{
			ModelDoc2 SwDoc = InDoc as ModelDoc2;
			string Path = "";
			if (SwDoc != null && InFeature != null)
			{
				try
				{
					string Title = SwDoc.GetPathName();
					string Name = InFeature.Name;
					Path = Title + "\\\\" + Name;
				}
				catch { }
			}
			return Path;
		}

		protected override void SetDirty(bool bInDirty)
		{
			base.SetDirty(bInDirty);

			if (bInDirty && AsmDoc != null && !string.IsNullOrEmpty(ComponentName))
			{
				// Notify owner assembly that a component needs re-export
				AsmDoc.SetComponentDirty(ComponentName, FAssemblyDocument.EComponentDirtyState.Geometry);
			}
		}

		public override void ExportToDatasmithScene()
		{
			FDatasmithActorExportInfo ExportInfo = new FDatasmithActorExportInfo();
			ExportInfo.Name = Path.GetFileNameWithoutExtension(PathName);
			ExportInfo.bVisible = true;
			ExportInfo.Label = ExportInfo.Name;
			ExportInfo.Type = EActorType.MeshActor;

			Exporter.ExportOrUpdateActor(ExportInfo);

			SetExportStatus($"{ExportInfo.Name} Materials");

			ExportedPartMaterials = FObjectMaterials.LoadPartMaterials(this, SwPartDoc, swDisplayStateOpts_e.swThisDisplayState, null);

			Exporter.ExportMaterials(ExportedMaterialsMap);

			SetExportStatus($"{ExportInfo.Name} Meshes");

			ConcurrentBag<FBody> Bodies = FBody.FetchBodies(SwPartDoc);
			FMeshData MeshData = FStripGeometry.CreateMeshData(Bodies, ExportedPartMaterials);

			if (MeshData != null)
			{
				Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh> NewMesh = null;
				Exporter.ExportMesh($"{ExportInfo.Name}_Mesh", MeshData, ExportInfo.Name, out NewMesh);

				if (NewMesh != null)
				{
					DatasmithScene.AddMesh(NewMesh.Item1);
				}
			}
		}

		public override bool HasMaterialUpdates()
		{
			FObjectMaterials CurrentMaterials = FObjectMaterials.LoadPartMaterials(this, SwPartDoc, swDisplayStateOpts_e.swThisDisplayState, null);
			if (CurrentMaterials == null && ExportedPartMaterials == null)
			{
				return false;
			}
			else if ((CurrentMaterials != null && ExportedPartMaterials == null) ||
					 (CurrentMaterials == null && ExportedPartMaterials != null))
			{
				return true;
			}

			return !CurrentMaterials.EqualMaterials(ExportedPartMaterials);
		}

		public override void Init()
		{
			base.Init();

			SwPartDoc.UndoPostNotify += new DPartDocEvents_UndoPostNotifyEventHandler(OnPartUndoPostNotify);
			SwPartDoc.SuppressionStateChangeNotify += new DPartDocEvents_SuppressionStateChangeNotifyEventHandler(OnPartSuppressionStateChangeNotify);
			SwPartDoc.DestroyNotify2 += new DPartDocEvents_DestroyNotify2EventHandler(OnPartDocumentDestroyNotify2);
			SwPartDoc.EquationEditorPostNotify += new DPartDocEvents_EquationEditorPostNotifyEventHandler(OnPartEquationEditorPostNotify);
			SwPartDoc.ConfigurationChangeNotify += new DPartDocEvents_ConfigurationChangeNotifyEventHandler(OnPartConfigurationChangeNotify);
			SwPartDoc.ActiveDisplayStateChangePostNotify += new DPartDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnPartActiveDisplayStateChangePostNotify);
			SwPartDoc.DragStateChangeNotify += new DPartDocEvents_DragStateChangeNotifyEventHandler(OnPartDragStateChangeNotify);
			SwPartDoc.UndoPreNotify += new DPartDocEvents_UndoPreNotifyEventHandler(OnPartUndoPreNotify);
			SwPartDoc.RedoPreNotify += new DPartDocEvents_RedoPreNotifyEventHandler(OnPartRedoPreNotify);
			SwPartDoc.RedoPostNotify += new DPartDocEvents_RedoPostNotifyEventHandler(OnPartRedoPostNotify);
			SwPartDoc.AddItemNotify += new DPartDocEvents_AddItemNotifyEventHandler(OnPartAddItemNotify);
			SwPartDoc.ActiveConfigChangePostNotify += new DPartDocEvents_ActiveConfigChangePostNotifyEventHandler(OnPartActiveConfigChangePostNotify);
			SwPartDoc.FileReloadNotify += new DPartDocEvents_FileReloadNotifyEventHandler(OnPartFileReloadNotify);
			SwPartDoc.ActiveConfigChangeNotify += new DPartDocEvents_ActiveConfigChangeNotifyEventHandler(OnPartActiveConfigChangeNotify);
			SwPartDoc.FileSaveAsNotify += new DPartDocEvents_FileSaveAsNotifyEventHandler(OnPartFileSaveAsNotify);
			SwPartDoc.FileSaveNotify += new DPartDocEvents_FileSaveNotifyEventHandler(OnPartFileSaveNotify);
			SwPartDoc.ViewNewNotify += new DPartDocEvents_ViewNewNotifyEventHandler(OnPartViewNewNotify);
			SwPartDoc.DimensionChangeNotify += new DPartDocEvents_DimensionChangeNotifyEventHandler(OnPartDimensionChangeNotify);
			SwPartDoc.RegenPostNotify2 += new DPartDocEvents_RegenPostNotify2EventHandler(OnPartRegenPostNotify2);
			SwPartDoc.BodyVisibleChangeNotify += new DPartDocEvents_BodyVisibleChangeNotifyEventHandler(OnPartBodyVisibleChangeNotify);
			SwPartDoc.FileSaveAsNotify2 += new DPartDocEvents_FileSaveAsNotify2EventHandler(OnPartFileSaveAsNotify2);
			SwPartDoc.FileSavePostNotify += new DPartDocEvents_FileSavePostNotifyEventHandler(OnPartFileSavePostNotify);
		}

		public override void Destroy()
		{
			base.Destroy();

			SwPartDoc.UndoPostNotify -= new DPartDocEvents_UndoPostNotifyEventHandler(OnPartUndoPostNotify);
			SwPartDoc.SuppressionStateChangeNotify -= new DPartDocEvents_SuppressionStateChangeNotifyEventHandler(OnPartSuppressionStateChangeNotify);
			SwPartDoc.DestroyNotify2 -= new DPartDocEvents_DestroyNotify2EventHandler(OnPartDocumentDestroyNotify2);
			SwPartDoc.EquationEditorPostNotify -= new DPartDocEvents_EquationEditorPostNotifyEventHandler(OnPartEquationEditorPostNotify);
			SwPartDoc.ConfigurationChangeNotify -= new DPartDocEvents_ConfigurationChangeNotifyEventHandler(OnPartConfigurationChangeNotify);
			SwPartDoc.ActiveDisplayStateChangePostNotify -= new DPartDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnPartActiveDisplayStateChangePostNotify);
			SwPartDoc.DragStateChangeNotify -= new DPartDocEvents_DragStateChangeNotifyEventHandler(OnPartDragStateChangeNotify);
			SwPartDoc.UndoPreNotify -= new DPartDocEvents_UndoPreNotifyEventHandler(OnPartUndoPreNotify);
			SwPartDoc.RedoPreNotify -= new DPartDocEvents_RedoPreNotifyEventHandler(OnPartRedoPreNotify);
			SwPartDoc.RedoPostNotify -= new DPartDocEvents_RedoPostNotifyEventHandler(OnPartRedoPostNotify);
			SwPartDoc.AddItemNotify -= new DPartDocEvents_AddItemNotifyEventHandler(OnPartAddItemNotify);
			SwPartDoc.ActiveConfigChangePostNotify -= new DPartDocEvents_ActiveConfigChangePostNotifyEventHandler(OnPartActiveConfigChangePostNotify);
			SwPartDoc.FileReloadNotify -= new DPartDocEvents_FileReloadNotifyEventHandler(OnPartFileReloadNotify);
			SwPartDoc.ActiveConfigChangeNotify -= new DPartDocEvents_ActiveConfigChangeNotifyEventHandler(OnPartActiveConfigChangeNotify);
			SwPartDoc.FileSaveAsNotify -= new DPartDocEvents_FileSaveAsNotifyEventHandler(OnPartFileSaveAsNotify);
			SwPartDoc.FileSaveNotify -= new DPartDocEvents_FileSaveNotifyEventHandler(OnPartFileSaveNotify);
			SwPartDoc.ViewNewNotify -= new DPartDocEvents_ViewNewNotifyEventHandler(OnPartViewNewNotify);
			SwPartDoc.DimensionChangeNotify -= new DPartDocEvents_DimensionChangeNotifyEventHandler(OnPartDimensionChangeNotify);
			SwPartDoc.RegenPostNotify2 -= new DPartDocEvents_RegenPostNotify2EventHandler(OnPartRegenPostNotify2);
			SwPartDoc.BodyVisibleChangeNotify -= new DPartDocEvents_BodyVisibleChangeNotifyEventHandler(OnPartBodyVisibleChangeNotify);
			SwPartDoc.FileSaveAsNotify2 -= new DPartDocEvents_FileSaveAsNotify2EventHandler(OnPartFileSaveAsNotify2);
			SwPartDoc.FileSavePostNotify -= new DPartDocEvents_FileSavePostNotifyEventHandler(OnPartFileSavePostNotify);
		}

		int OnPartDocumentDestroyNotify2(int DestroyType)
		{
			Addin.Instance.CloseDocument(DocId);
			return 0;
		}

		int OnPartUndoPostNotify()
		{
			SetDirty(true);
			return 0;
		}

		// part has become resolved or lightweight or suppressed
		int OnPartSuppressionStateChangeNotify(Feature Feature, int NewSuppressionState, int PreviousSuppressionState, int ConfigurationOption, ref object ConfigurationNames)
		{
			SetDirty(true);
			return 0;
		}

		int OnPartEquationEditorPostNotify(Boolean Changed)
		{
			return 0;
		}

		int OnPartConfigurationChangeNotify(string ConfigurationName, object Object, int ObjectType, int changeType)
		{
			SetDirty(true);
			return 0;
		}

		int OnPartActiveDisplayStateChangePostNotify(string DisplayStateName)
		{
			return 0;
		}

		int OnPartDragStateChangeNotify(Boolean State)
		{
			SetDirty(true);
			return 0;
		}

		int OnPartUndoPreNotify()
		{
			return 0;
		}

		int OnPartRedoPreNotify()
		{
			return 0;
		}

		int OnPartRedoPostNotify()
		{
			SetDirty(true);
			return 0;
		}

		int OnPartAddItemNotify(int InEntityType, string InItemName)
		{
			SetDirty(true);
			return 0;
		}

		int OnPartActiveConfigChangePostNotify()
		{
			return 0;
		}

		int OnPartFileReloadNotify()
		{
			return 0;
		}

		int OnPartActiveConfigChangeNotify()
		{
			return 0;
		}

		int OnPartFileSaveAsNotify(string FileName)
		{
			return 0;
		}

		int OnPartFileSaveNotify(string FileName)
		{
			return 0;
		}

		int OnPartViewNewNotify()
		{
			return 0;
		}

		int OnPartDimensionChangeNotify(object displayDim)
		{
			return 0;
		}

		int OnPartRegenPostNotify2(object stopFeature)
		{
			SetDirty(true);
			return 0;
		}

		int OnPartBodyVisibleChangeNotify()
		{
			return 0;
		}

		int OnPartFileSaveAsNotify2(string FileName)
		{
			return 0;
		}

		int OnPartFileSavePostNotify(int saveType, string FileName)
		{
			return 0;
		}
	}
}