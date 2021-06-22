// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith
{
	[ComVisible(false)]
	public class DocumentEventHandler
	{
		protected ISldWorks iSwApp;
		protected ModelDoc2 document;
		protected SwAddin userAddin;

		protected Hashtable openModelViews;

		public DocumentEventHandler(ModelDoc2 modDoc, SwAddin addin)
		{
			document = modDoc;
			userAddin = addin;
			iSwApp = (ISldWorks)userAddin.SwApp;
			openModelViews = new Hashtable();
		}

		virtual public bool AttachEventHandlers()
		{
			return true;
		}

		virtual public bool DetachEventHandlers()
		{
			return true;
		}

		public bool ConnectModelViews()
		{
			IModelView mView;
			mView = (IModelView)document.GetFirstModelView();

			while (mView != null)
			{
				if (!openModelViews.Contains(mView))
				{
					DocView dView = new DocView(userAddin, mView, this);
					dView.AttachEventHandlers();
					openModelViews.Add(mView, dView);
				}
				mView = (IModelView)mView.GetNext();
			}
			return true;
		}

		public bool DisconnectModelViews()
		{
			//Close events on all currently open docs
			DocView dView;
			int numKeys;
			numKeys = openModelViews.Count;

			if (numKeys == 0)
			{
				return false;
			}


			object[] keys = new object[numKeys];

			//Remove all ModelView event handlers
			openModelViews.Keys.CopyTo(keys, 0);
			foreach (ModelView key in keys)
			{
				dView = (DocView)openModelViews[key];
				dView.DetachEventHandlers();
				openModelViews.Remove(key);
				dView = null;
			}
			return true;
		}

		public bool DetachModelViewEventHandler(ModelView mView)
		{
			DocView dView;
			if (openModelViews.Contains(mView))
			{
				dView = (DocView)openModelViews[mView];
				openModelViews.Remove(mView);
				mView = null;
				dView = null;
			}
			return true;
		}
	}

	public class PartEventHandler : DocumentEventHandler
	{
		PartDoc doc;
		SwAddin swAddin;
		
		public PartEventHandler(ModelDoc2 modDoc, SwAddin addin)
			: base(modDoc, addin)
		{
			doc = (PartDoc)document;
			swAddin = addin;
			
			int units = modDoc.Extension.GetUserPreferenceInteger((int)swUserPreferenceIntegerValue_e.swUnitsLinear, (int)swUserPreferenceOption_e.swDetailingNoOptionSpecified);
			SwSingleton.CurrentScene.AddDocument(doc as PartDoc);
		}

		override public bool AttachEventHandlers()
		{
			doc.RegenNotify += new DPartDocEvents_RegenNotifyEventHandler(OnPartRegenNotify);
			doc.SensorAlertPreNotify += new DPartDocEvents_SensorAlertPreNotifyEventHandler(OnPartSensorAlertPreNotify);
			doc.FileDropPreNotify += new DPartDocEvents_FileDropPreNotifyEventHandler(OnPartFileDropPreNotify);
			doc.AutoSaveToStorageNotify += new DPartDocEvents_AutoSaveToStorageNotifyEventHandler(OnPartAutoSaveToStorageNotify);
			doc.AutoSaveNotify += new DPartDocEvents_AutoSaveNotifyEventHandler(OnPartAutoSaveNotify);
			doc.FlipLoopNotify += new DPartDocEvents_FlipLoopNotifyEventHandler(OnPartFlipLoopNotify);
			doc.FeatureManagerFilterStringChangeNotify += new DPartDocEvents_FeatureManagerFilterStringChangeNotifyEventHandler(OnPartFeatureManagerFilterStringChangeNotify);
			doc.ActiveViewChangeNotify += new DPartDocEvents_ActiveViewChangeNotifyEventHandler(OnPartActiveViewChangeNotify);
			doc.UndoPostNotify += new DPartDocEvents_UndoPostNotifyEventHandler(OnPartUndoPostNotify);
			doc.SuppressionStateChangeNotify += new DPartDocEvents_SuppressionStateChangeNotifyEventHandler(OnPartSuppressionStateChangeNotify);
			doc.DestroyNotify2 += new DPartDocEvents_DestroyNotify2EventHandler(OnPartDestroyNotify2);
			doc.UnitsChangeNotify += new DPartDocEvents_UnitsChangeNotifyEventHandler(OnPartUnitsChangeNotify);
			doc.AddDvePagePreNotify += new DPartDocEvents_AddDvePagePreNotifyEventHandler(OnPartAddDvePagePreNotify);
			doc.PromptBodiesToKeepNotify += new DPartDocEvents_PromptBodiesToKeepNotifyEventHandler(OnPartPromptBodiesToKeepNotify);
			doc.CloseDesignTableNotify += new DPartDocEvents_CloseDesignTableNotifyEventHandler(OnPartCloseDesignTableNotify);
			doc.OpenDesignTableNotify += new DPartDocEvents_OpenDesignTableNotifyEventHandler(OnPartOpenDesignTableNotify);
			doc.EquationEditorPostNotify += new DPartDocEvents_EquationEditorPostNotifyEventHandler(OnPartEquationEditorPostNotify);
			doc.ConfigurationChangeNotify += new DPartDocEvents_ConfigurationChangeNotifyEventHandler(OnPartConfigurationChangeNotify);
			doc.EquationEditorPreNotify += new DPartDocEvents_EquationEditorPreNotifyEventHandler(OnPartEquationEditorPreNotify);
			doc.UserSelectionPreNotify += new DPartDocEvents_UserSelectionPreNotifyEventHandler(OnPartUserSelectionPreNotify);
			doc.ActiveDisplayStateChangePostNotify += new DPartDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnPartActiveDisplayStateChangePostNotify);
			doc.ConvertToBodiesPreNotify += new DPartDocEvents_ConvertToBodiesPreNotifyEventHandler(OnPartConvertToBodiesPreNotify);
			doc.PublishTo3DPDFNotify += new DPartDocEvents_PublishTo3DPDFNotifyEventHandler(OnPartPublishTo3DPDFNotify);
			doc.FeatureManagerTabActivatedNotify += new DPartDocEvents_FeatureManagerTabActivatedNotifyEventHandler(OnPartFeatureManagerTabActivatedNotify);
			doc.FeatureManagerTabActivatedPreNotify += new DPartDocEvents_FeatureManagerTabActivatedPreNotifyEventHandler(OnPartFeatureManagerTabActivatedPreNotify);
			doc.RenamedDocumentNotify += new DPartDocEvents_RenamedDocumentNotifyEventHandler(OnPartRenamedDocumentNotify);
			doc.PreRenameItemNotify += new DPartDocEvents_PreRenameItemNotifyEventHandler(OnPartPreRenameItemNotify);
			doc.CommandManagerTabActivatedPreNotify += new DPartDocEvents_CommandManagerTabActivatedPreNotifyEventHandler(OnPartCommandManagerTabActivatedPreNotify);
			doc.ActiveDisplayStateChangePreNotify += new DPartDocEvents_ActiveDisplayStateChangePreNotifyEventHandler(OnPartActiveDisplayStateChangePreNotify);
			doc.UserSelectionPostNotify += new DPartDocEvents_UserSelectionPostNotifyEventHandler(OnPartUserSelectionPostNotify);
			doc.InsertTableNotify += new DPartDocEvents_InsertTableNotifyEventHandler(OnPartInsertTableNotify);
			doc.DragStateChangeNotify += new DPartDocEvents_DragStateChangeNotifyEventHandler(OnPartDragStateChangeNotify);
			doc.AutoSaveToStorageStoreNotify += new DPartDocEvents_AutoSaveToStorageStoreNotifyEventHandler(OnPartAutoSaveToStorageStoreNotify);
			doc.WeldmentCutListUpdatePostNotify += new DPartDocEvents_WeldmentCutListUpdatePostNotifyEventHandler(OnPartWeldmentCutListUpdatePostNotify);
			doc.UndoPreNotify += new DPartDocEvents_UndoPreNotifyEventHandler(OnPartUndoPreNotify);
			doc.RedoPreNotify += new DPartDocEvents_RedoPreNotifyEventHandler(OnPartRedoPreNotify);
			doc.RedoPostNotify += new DPartDocEvents_RedoPostNotifyEventHandler(OnPartRedoPostNotify);
			doc.ModifyTableNotify += new DPartDocEvents_ModifyTableNotifyEventHandler(OnPartModifyTableNotify);
			doc.ConvertToBodiesPostNotify += new DPartDocEvents_ConvertToBodiesPostNotifyEventHandler(OnPartConvertToBodiesPostNotify);
			doc.ClearSelectionsNotify += new DPartDocEvents_ClearSelectionsNotifyEventHandler(OnPartClearSelectionsNotify);
			doc.SketchSolveNotify += new DPartDocEvents_SketchSolveNotifyEventHandler(OnPartSketchSolveNotify);
			doc.ModifyNotify += new DPartDocEvents_ModifyNotifyEventHandler(OnPartModifyNotify);
			doc.DeleteItemNotify += new DPartDocEvents_DeleteItemNotifyEventHandler(OnPartDeleteItemNotify);
			doc.RenameItemNotify += new DPartDocEvents_RenameItemNotifyEventHandler(OnPartRenameItemNotify);
			doc.AddItemNotify += new DPartDocEvents_AddItemNotifyEventHandler(OnPartAddItemNotify);
			doc.LightingDialogCreateNotify += new DPartDocEvents_LightingDialogCreateNotifyEventHandler(OnPartLightingDialogCreateNotify);
			doc.ViewNewNotify2 += new DPartDocEvents_ViewNewNotify2EventHandler(OnPartViewNewNotify2);
			doc.ActiveConfigChangePostNotify += new DPartDocEvents_ActiveConfigChangePostNotifyEventHandler(OnPartActiveConfigChangePostNotify);
			doc.FileReloadNotify += new DPartDocEvents_FileReloadNotifyEventHandler(OnPartFileReloadNotify);
			doc.ActiveConfigChangeNotify += new DPartDocEvents_ActiveConfigChangeNotifyEventHandler(OnPartActiveConfigChangeNotify);
			doc.LoadFromStorageNotify += new DPartDocEvents_LoadFromStorageNotifyEventHandler(OnPartLoadFromStorageNotify);
			doc.FileSaveAsNotify += new DPartDocEvents_FileSaveAsNotifyEventHandler(OnPartFileSaveAsNotify);
			doc.FileSaveNotify += new DPartDocEvents_FileSaveNotifyEventHandler(OnPartFileSaveNotify);
			doc.NewSelectionNotify += new DPartDocEvents_NewSelectionNotifyEventHandler(OnPartNewSelectionNotify);
			doc.ViewNewNotify += new DPartDocEvents_ViewNewNotifyEventHandler(OnPartViewNewNotify);
			doc.RegenPostNotify += new DPartDocEvents_RegenPostNotifyEventHandler(OnPartRegenPostNotify);
			doc.DestroyNotify += new DPartDocEvents_DestroyNotifyEventHandler(OnPartDestroyNotify);
			doc.SaveToStorageNotify += new DPartDocEvents_SaveToStorageNotifyEventHandler(OnPartSaveToStorageNotify);
			doc.DeleteItemPreNotify += new DPartDocEvents_DeleteItemPreNotifyEventHandler(OnPartDeleteItemPreNotify);
			doc.AddCustomPropertyNotify += new DPartDocEvents_AddCustomPropertyNotifyEventHandler(OnPartAddCustomPropertyNotify);
			doc.DeleteCustomPropertyNotify += new DPartDocEvents_DeleteCustomPropertyNotifyEventHandler(OnPartDeleteCustomPropertyNotify);
			doc.FileSavePostCancelNotify += new DPartDocEvents_FileSavePostCancelNotifyEventHandler(OnPartFileSavePostCancelNotify);
			doc.FileReloadCancelNotify += new DPartDocEvents_FileReloadCancelNotifyEventHandler(OnPartFileReloadCancelNotify);
			doc.DimensionChangeNotify += new DPartDocEvents_DimensionChangeNotifyEventHandler(OnPartDimensionChangeNotify);
			doc.DynamicHighlightNotify += new DPartDocEvents_DynamicHighlightNotifyEventHandler(OnPartDynamicHighlightNotify);
			doc.FileDropPostNotify += new DPartDocEvents_FileDropPostNotifyEventHandler(OnPartFileDropPostNotify);
			doc.FeatureManagerTreeRebuildNotify += new DPartDocEvents_FeatureManagerTreeRebuildNotifyEventHandler(OnPartFeatureManagerTreeRebuildNotify);
			doc.SaveToStorageStoreNotify += new DPartDocEvents_SaveToStorageStoreNotifyEventHandler(OnPartSaveToStorageStoreNotify);
			doc.ChangeCustomPropertyNotify += new DPartDocEvents_ChangeCustomPropertyNotifyEventHandler(OnPartChangeCustomPropertyNotify);
			doc.LoadFromStorageStoreNotify += new DPartDocEvents_LoadFromStorageStoreNotifyEventHandler(OnPartLoadFromStorageStoreNotify);
			doc.RegenPostNotify2 += new DPartDocEvents_RegenPostNotify2EventHandler(OnPartRegenPostNotify2);
			doc.BodyVisibleChangeNotify += new DPartDocEvents_BodyVisibleChangeNotifyEventHandler(OnPartBodyVisibleChangeNotify);
			doc.FileReloadPreNotify += new DPartDocEvents_FileReloadPreNotifyEventHandler(OnPartFileReloadPreNotify);
			doc.DeleteSelectionPreNotify += new DPartDocEvents_DeleteSelectionPreNotifyEventHandler(OnPartDeleteSelectionPreNotify);
			doc.FileSaveAsNotify2 += new DPartDocEvents_FileSaveAsNotify2EventHandler(OnPartFileSaveAsNotify2);
			doc.FeatureSketchEditPreNotify += new DPartDocEvents_FeatureSketchEditPreNotifyEventHandler(OnPartFeatureSketchEditPreNotify);
			doc.FeatureEditPreNotify += new DPartDocEvents_FeatureEditPreNotifyEventHandler(OnPartFeatureEditPreNotify);
			doc.FileSavePostNotify += new DPartDocEvents_FileSavePostNotifyEventHandler(OnPartFileSavePostNotify);
			doc.RenameDisplayTitleNotify += new DPartDocEvents_RenameDisplayTitleNotifyEventHandler(OnPartRenameDisplayTitleNotify);
			
			ConnectModelViews();

			return true;
		}

		override public bool DetachEventHandlers()
		{
			doc.RegenNotify -= new DPartDocEvents_RegenNotifyEventHandler(OnPartRegenNotify);
			doc.SensorAlertPreNotify -= new DPartDocEvents_SensorAlertPreNotifyEventHandler(OnPartSensorAlertPreNotify);
			doc.FileDropPreNotify -= new DPartDocEvents_FileDropPreNotifyEventHandler(OnPartFileDropPreNotify);
			doc.AutoSaveToStorageNotify -= new DPartDocEvents_AutoSaveToStorageNotifyEventHandler(OnPartAutoSaveToStorageNotify);
			doc.AutoSaveNotify -= new DPartDocEvents_AutoSaveNotifyEventHandler(OnPartAutoSaveNotify);
			doc.FlipLoopNotify -= new DPartDocEvents_FlipLoopNotifyEventHandler(OnPartFlipLoopNotify);
			doc.FeatureManagerFilterStringChangeNotify -= new DPartDocEvents_FeatureManagerFilterStringChangeNotifyEventHandler(OnPartFeatureManagerFilterStringChangeNotify);
			doc.ActiveViewChangeNotify -= new DPartDocEvents_ActiveViewChangeNotifyEventHandler(OnPartActiveViewChangeNotify);
			doc.UndoPostNotify -= new DPartDocEvents_UndoPostNotifyEventHandler(OnPartUndoPostNotify);
			doc.SuppressionStateChangeNotify -= new DPartDocEvents_SuppressionStateChangeNotifyEventHandler(OnPartSuppressionStateChangeNotify);
			doc.DestroyNotify2 -= new DPartDocEvents_DestroyNotify2EventHandler(OnPartDestroyNotify2);
			doc.UnitsChangeNotify -= new DPartDocEvents_UnitsChangeNotifyEventHandler(OnPartUnitsChangeNotify);
			doc.AddDvePagePreNotify -= new DPartDocEvents_AddDvePagePreNotifyEventHandler(OnPartAddDvePagePreNotify);
			doc.PromptBodiesToKeepNotify -= new DPartDocEvents_PromptBodiesToKeepNotifyEventHandler(OnPartPromptBodiesToKeepNotify);
			doc.CloseDesignTableNotify -= new DPartDocEvents_CloseDesignTableNotifyEventHandler(OnPartCloseDesignTableNotify);
			doc.OpenDesignTableNotify -= new DPartDocEvents_OpenDesignTableNotifyEventHandler(OnPartOpenDesignTableNotify);
			doc.EquationEditorPostNotify -= new DPartDocEvents_EquationEditorPostNotifyEventHandler(OnPartEquationEditorPostNotify);
			doc.ConfigurationChangeNotify -= new DPartDocEvents_ConfigurationChangeNotifyEventHandler(OnPartConfigurationChangeNotify);
			doc.EquationEditorPreNotify -= new DPartDocEvents_EquationEditorPreNotifyEventHandler(OnPartEquationEditorPreNotify);
			doc.UserSelectionPreNotify -= new DPartDocEvents_UserSelectionPreNotifyEventHandler(OnPartUserSelectionPreNotify);
			doc.ActiveDisplayStateChangePostNotify -= new DPartDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnPartActiveDisplayStateChangePostNotify);
			doc.ConvertToBodiesPreNotify -= new DPartDocEvents_ConvertToBodiesPreNotifyEventHandler(OnPartConvertToBodiesPreNotify);
			doc.PublishTo3DPDFNotify -= new DPartDocEvents_PublishTo3DPDFNotifyEventHandler(OnPartPublishTo3DPDFNotify);
			doc.FeatureManagerTabActivatedNotify -= new DPartDocEvents_FeatureManagerTabActivatedNotifyEventHandler(OnPartFeatureManagerTabActivatedNotify);
			doc.FeatureManagerTabActivatedPreNotify -= new DPartDocEvents_FeatureManagerTabActivatedPreNotifyEventHandler(OnPartFeatureManagerTabActivatedPreNotify);
			doc.RenamedDocumentNotify -= new DPartDocEvents_RenamedDocumentNotifyEventHandler(OnPartRenamedDocumentNotify);
			doc.PreRenameItemNotify -= new DPartDocEvents_PreRenameItemNotifyEventHandler(OnPartPreRenameItemNotify);
			doc.CommandManagerTabActivatedPreNotify -= new DPartDocEvents_CommandManagerTabActivatedPreNotifyEventHandler(OnPartCommandManagerTabActivatedPreNotify);
			doc.ActiveDisplayStateChangePreNotify -= new DPartDocEvents_ActiveDisplayStateChangePreNotifyEventHandler(OnPartActiveDisplayStateChangePreNotify);
			doc.UserSelectionPostNotify -= new DPartDocEvents_UserSelectionPostNotifyEventHandler(OnPartUserSelectionPostNotify);
			doc.InsertTableNotify -= new DPartDocEvents_InsertTableNotifyEventHandler(OnPartInsertTableNotify);
			doc.DragStateChangeNotify -= new DPartDocEvents_DragStateChangeNotifyEventHandler(OnPartDragStateChangeNotify);
			doc.AutoSaveToStorageStoreNotify -= new DPartDocEvents_AutoSaveToStorageStoreNotifyEventHandler(OnPartAutoSaveToStorageStoreNotify);
			doc.WeldmentCutListUpdatePostNotify -= new DPartDocEvents_WeldmentCutListUpdatePostNotifyEventHandler(OnPartWeldmentCutListUpdatePostNotify);
			doc.UndoPreNotify -= new DPartDocEvents_UndoPreNotifyEventHandler(OnPartUndoPreNotify);
			doc.RedoPreNotify -= new DPartDocEvents_RedoPreNotifyEventHandler(OnPartRedoPreNotify);
			doc.RedoPostNotify -= new DPartDocEvents_RedoPostNotifyEventHandler(OnPartRedoPostNotify);
			doc.ModifyTableNotify -= new DPartDocEvents_ModifyTableNotifyEventHandler(OnPartModifyTableNotify);
			doc.ConvertToBodiesPostNotify -= new DPartDocEvents_ConvertToBodiesPostNotifyEventHandler(OnPartConvertToBodiesPostNotify);
			doc.ClearSelectionsNotify -= new DPartDocEvents_ClearSelectionsNotifyEventHandler(OnPartClearSelectionsNotify);
			doc.SketchSolveNotify -= new DPartDocEvents_SketchSolveNotifyEventHandler(OnPartSketchSolveNotify);
			doc.ModifyNotify -= new DPartDocEvents_ModifyNotifyEventHandler(OnPartModifyNotify);
			doc.DeleteItemNotify -= new DPartDocEvents_DeleteItemNotifyEventHandler(OnPartDeleteItemNotify);
			doc.RenameItemNotify -= new DPartDocEvents_RenameItemNotifyEventHandler(OnPartRenameItemNotify);
			doc.AddItemNotify -= new DPartDocEvents_AddItemNotifyEventHandler(OnPartAddItemNotify);
			doc.LightingDialogCreateNotify -= new DPartDocEvents_LightingDialogCreateNotifyEventHandler(OnPartLightingDialogCreateNotify);
			doc.ViewNewNotify2 -= new DPartDocEvents_ViewNewNotify2EventHandler(OnPartViewNewNotify2);
			doc.ActiveConfigChangePostNotify -= new DPartDocEvents_ActiveConfigChangePostNotifyEventHandler(OnPartActiveConfigChangePostNotify);
			doc.FileReloadNotify -= new DPartDocEvents_FileReloadNotifyEventHandler(OnPartFileReloadNotify);
			doc.ActiveConfigChangeNotify -= new DPartDocEvents_ActiveConfigChangeNotifyEventHandler(OnPartActiveConfigChangeNotify);
			doc.LoadFromStorageNotify -= new DPartDocEvents_LoadFromStorageNotifyEventHandler(OnPartLoadFromStorageNotify);
			doc.FileSaveAsNotify -= new DPartDocEvents_FileSaveAsNotifyEventHandler(OnPartFileSaveAsNotify);
			doc.FileSaveNotify -= new DPartDocEvents_FileSaveNotifyEventHandler(OnPartFileSaveNotify);
			doc.NewSelectionNotify -= new DPartDocEvents_NewSelectionNotifyEventHandler(OnPartNewSelectionNotify);
			doc.ViewNewNotify -= new DPartDocEvents_ViewNewNotifyEventHandler(OnPartViewNewNotify);
			doc.RegenPostNotify -= new DPartDocEvents_RegenPostNotifyEventHandler(OnPartRegenPostNotify);
			doc.DestroyNotify -= new DPartDocEvents_DestroyNotifyEventHandler(OnPartDestroyNotify);
			doc.SaveToStorageNotify -= new DPartDocEvents_SaveToStorageNotifyEventHandler(OnPartSaveToStorageNotify);
			doc.DeleteItemPreNotify -= new DPartDocEvents_DeleteItemPreNotifyEventHandler(OnPartDeleteItemPreNotify);
			doc.AddCustomPropertyNotify -= new DPartDocEvents_AddCustomPropertyNotifyEventHandler(OnPartAddCustomPropertyNotify);
			doc.DeleteCustomPropertyNotify -= new DPartDocEvents_DeleteCustomPropertyNotifyEventHandler(OnPartDeleteCustomPropertyNotify);
			doc.FileSavePostCancelNotify -= new DPartDocEvents_FileSavePostCancelNotifyEventHandler(OnPartFileSavePostCancelNotify);
			doc.FileReloadCancelNotify -= new DPartDocEvents_FileReloadCancelNotifyEventHandler(OnPartFileReloadCancelNotify);
			doc.DimensionChangeNotify -= new DPartDocEvents_DimensionChangeNotifyEventHandler(OnPartDimensionChangeNotify);
			doc.DynamicHighlightNotify -= new DPartDocEvents_DynamicHighlightNotifyEventHandler(OnPartDynamicHighlightNotify);
			doc.FileDropPostNotify -= new DPartDocEvents_FileDropPostNotifyEventHandler(OnPartFileDropPostNotify);
			doc.FeatureManagerTreeRebuildNotify -= new DPartDocEvents_FeatureManagerTreeRebuildNotifyEventHandler(OnPartFeatureManagerTreeRebuildNotify);
			doc.SaveToStorageStoreNotify -= new DPartDocEvents_SaveToStorageStoreNotifyEventHandler(OnPartSaveToStorageStoreNotify);
			doc.ChangeCustomPropertyNotify -= new DPartDocEvents_ChangeCustomPropertyNotifyEventHandler(OnPartChangeCustomPropertyNotify);
			doc.LoadFromStorageStoreNotify -= new DPartDocEvents_LoadFromStorageStoreNotifyEventHandler(OnPartLoadFromStorageStoreNotify);
			doc.RegenPostNotify2 -= new DPartDocEvents_RegenPostNotify2EventHandler(OnPartRegenPostNotify2);
			doc.BodyVisibleChangeNotify -= new DPartDocEvents_BodyVisibleChangeNotifyEventHandler(OnPartBodyVisibleChangeNotify);
			doc.FileReloadPreNotify -= new DPartDocEvents_FileReloadPreNotifyEventHandler(OnPartFileReloadPreNotify);
			doc.DeleteSelectionPreNotify -= new DPartDocEvents_DeleteSelectionPreNotifyEventHandler(OnPartDeleteSelectionPreNotify);
			doc.FileSaveAsNotify2 -= new DPartDocEvents_FileSaveAsNotify2EventHandler(OnPartFileSaveAsNotify2);
			doc.FeatureSketchEditPreNotify -= new DPartDocEvents_FeatureSketchEditPreNotifyEventHandler(OnPartFeatureSketchEditPreNotify);
			doc.FeatureEditPreNotify -= new DPartDocEvents_FeatureEditPreNotifyEventHandler(OnPartFeatureEditPreNotify);
			doc.FileSavePostNotify -= new DPartDocEvents_FileSavePostNotifyEventHandler(OnPartFileSavePostNotify);
			doc.RenameDisplayTitleNotify -= new DPartDocEvents_RenameDisplayTitleNotifyEventHandler(OnPartRenameDisplayTitleNotify);
			
			DisconnectModelViews();

			userAddin.DetachModelEventHandler(document);
			return true;
		}

		//Event Handlers
		int OnPartDestroyNotify()
		{
			DetachEventHandlers();
			return 0;
		}

		int OnPartDestroyNotify2(int DestroyType)
		{
			DetachEventHandlers();
			return 0;
		}

		int OnPartNewSelectionNotify() { return 0; }
		int OnPartRegenNotify() { return 0; }
		int OnPartSensorAlertPreNotify(object SensorIn, int SensorAlertType) { return 0; }
		int OnPartFileDropPreNotify(string FileName) { return 0; }
		int OnPartAutoSaveToStorageNotify() { return 0; }
		int OnPartAutoSaveNotify(string FileName) { return 0; }
		int OnPartFlipLoopNotify(object TheLoop, object TheEdge) { return 0; }
		int OnPartFeatureManagerFilterStringChangeNotify(string FilterString) { return 0; }
		int OnPartActiveViewChangeNotify() { return 0; }
		int OnPartUndoPostNotify() { return 0; }

		// part has become resolved or lightweight or suppressed
		int OnPartSuppressionStateChangeNotify(Feature Feature, int NewSuppressionState, int PreviousSuppressionState, int ConfigurationOption, ref object ConfigurationNames)
		{
			return 0;
		}

		int OnPartUnitsChangeNotify() { return 0; }
		int OnPartAddDvePagePreNotify(int Command, ref object PageToAdd) { return 0; }
		int OnPartPromptBodiesToKeepNotify(object Feature, ref object Bodies) { return 0; }
		int OnPartCloseDesignTableNotify(object DesignTable) { return 0; }
		int OnPartOpenDesignTableNotify(object DesignTable) { return 0; }
		int OnPartEquationEditorPostNotify(Boolean Changed) { return 0; }

		int OnPartConfigurationChangeNotify(string ConfigurationName, object Object, int ObjectType, int changeType)
		{
			return 0;
		}

		int OnPartEquationEditorPreNotify() { return 0; }
		int OnPartUserSelectionPreNotify(int SelType) { return 0; }

		int OnPartActiveDisplayStateChangePostNotify(string DisplayStateName)
		{
			return 0;
		}

		int OnPartConvertToBodiesPreNotify(string FileName) { return 0; }
		int OnPartPublishTo3DPDFNotify(string Path) { return 0; }
		int OnPartFeatureManagerTabActivatedNotify(int CommandIndex, string CommandTabName) { return 0; }
		int OnPartFeatureManagerTabActivatedPreNotify(int CommandIndex, string CommandTabName) { return 0; }
		int OnPartRenamedDocumentNotify(ref object RenamedDocumentInterface) { return 0; }
		int OnPartPreRenameItemNotify(int EntityType, string oldName, string NewName) { return 0; }
		int OnPartCommandManagerTabActivatedPreNotify(int CommandTabIndex, string CommandTabName) { return 0; }
		int OnPartActiveDisplayStateChangePreNotify() { return 0; }
		int OnPartUserSelectionPostNotify() { return 0; }
		int OnPartInsertTableNotify(TableAnnotation TableAnnotation, int TableType, string TemplatePath) { return 0; }
		int OnPartDragStateChangeNotify(Boolean State) { return 0; }
		int OnPartAutoSaveToStorageStoreNotify() { return 0; }
		int OnPartWeldmentCutListUpdatePostNotify() { return 0; }
		int OnPartUndoPreNotify() { return 0; }
		int OnPartRedoPreNotify() { return 0; }
		int OnPartRedoPostNotify() { return 0; }
		int OnPartModifyTableNotify(TableAnnotation TableAnnotation, int TableType, int reason, int RowInfo, int ColumnInfo, string DataInfo) { return 0; }
		int OnPartConvertToBodiesPostNotify(string FileName, int SaveOption, Boolean PreserveGeometryAndSketches) { return 0; }
		int OnPartClearSelectionsNotify() { return 0; }
		int OnPartSketchSolveNotify(string featName) { return 0; }

		// The problem with this event is that it only gets called when the document gets marked dirty.
		// Saving the document clears the dirty flag.
		// Any further modification while in dirty state, does NOT prompt the system to call this event.
		// It is only after the document is saved, hence cleared from dirty flag, the first modification
		// will trigger this event.
		// So basically this event is only called the moment the document becomes dirty, and not every
		// time it is modified.
		int OnPartModifyNotify()
		{
			return 0;
		}

		int OnPartDeleteItemNotify(int EntityType, string itemName)
		{
			return 0;
		}

		int OnPartRenameItemNotify(int EntityType, string oldName, string NewName) { return 0; }

		int OnPartAddItemNotify(int EntityType, string itemName)
		{
			SwSingleton.Events.PartAddItemEvent.Fire(doc,
				new SwEventArgs()
				.AddParameter<PartDoc>("Doc", doc)
				.AddParameter<int>("EntityType", EntityType)
				.AddParameter<string>("itemName", itemName)
			);
			return 0;
		}

		int OnPartLightingDialogCreateNotify(object dialog) { return 0; }
		int OnPartViewNewNotify2(object viewBeingAdded) { return 0; }
		int OnPartActiveConfigChangePostNotify() { return 0; }
		int OnPartFileReloadNotify() { return 0; }
		int OnPartActiveConfigChangeNotify() { return 0; }
		int OnPartLoadFromStorageNotify() { return 0; }
		int OnPartFileSaveAsNotify(string FileName) { return 0; }
		int OnPartFileSaveNotify(string FileName) { return 0; }
		int OnPartViewNewNotify() { return 0; }
		int OnPartRegenPostNotify() { return 0; }
		int OnPartSaveToStorageNotify() { return 0; }
		int OnPartDeleteItemPreNotify(int EntityType, string itemName) { return 0; }
		int OnPartAddCustomPropertyNotify(string propName, string Configuration, string Value, int valueType) { return 0; }
		int OnPartDeleteCustomPropertyNotify(string propName, string Configuration, string Value, int valueType) { return 0; }
		int OnPartFileSavePostCancelNotify() { return 0; }
		int OnPartFileReloadCancelNotify(int ErrorCode) { return 0; }
		int OnPartDimensionChangeNotify(object displayDim) { return 0; }
		int OnPartDynamicHighlightNotify(Boolean bHighlightState) { return 0; }
		int OnPartFileDropPostNotify(string FileName) { return 0; }
		int OnPartFeatureManagerTreeRebuildNotify() { return 0; }
		int OnPartSaveToStorageStoreNotify() { return 0; }
		int OnPartChangeCustomPropertyNotify(string propName, string Configuration, string oldValue, string NewValue, int valueType) { return 0; }
		int OnPartLoadFromStorageStoreNotify() { return 0; }
		int OnPartRegenPostNotify2(object stopFeature) { return 0; }

		int OnPartBodyVisibleChangeNotify()
		{
			return 0;
		}

		int OnPartFileReloadPreNotify() { return 0; }
		int OnPartDeleteSelectionPreNotify() { return 0; }
		int OnPartFileSaveAsNotify2(string FileName) { return 0; }
		int OnPartFeatureSketchEditPreNotify(object EditFeature, object featureSketch) { return 0; }
		int OnPartFeatureEditPreNotify(object EditFeature) { return 0; }
		int OnPartFileSavePostNotify(int saveType, string FileName) { return 0; }
		int OnPartRenameDisplayTitleNotify(string oldName, string NewName) { return 0; }
	}

	public class AssemblyEventHandler : DocumentEventHandler
	{
		AssemblyDoc doc;
		SwAddin swAddin;

		public AssemblyEventHandler(ModelDoc2 modDoc, SwAddin addin)
			: base(modDoc, addin)
		{
			doc = (AssemblyDoc)document;
			var name = modDoc.GetPathName();
			swAddin = addin;
			SwSingleton.CurrentScene.AddDocument(doc as AssemblyDoc);
		}

		override public bool AttachEventHandlers()
		{
			doc.RegenNotify += new DAssemblyDocEvents_RegenNotifyEventHandler(OnComponentRegenNotify);
			doc.ActiveDisplayStateChangePreNotify += new DAssemblyDocEvents_ActiveDisplayStateChangePreNotifyEventHandler(OnComponentActiveDisplayStateChangePreNotify);
			doc.SensorAlertPreNotify += new DAssemblyDocEvents_SensorAlertPreNotifyEventHandler(OnComponentSensorAlertPreNotify);
			doc.AutoSaveToStorageNotify += new DAssemblyDocEvents_AutoSaveToStorageNotifyEventHandler(OnComponentAutoSaveToStorageNotify);
			doc.AutoSaveNotify += new DAssemblyDocEvents_AutoSaveNotifyEventHandler(OnComponentAutoSaveNotify);
			doc.FlipLoopNotify += new DAssemblyDocEvents_FlipLoopNotifyEventHandler(OnComponentFlipLoopNotify);
			doc.FeatureManagerFilterStringChangeNotify += new DAssemblyDocEvents_FeatureManagerFilterStringChangeNotifyEventHandler(OnComponentFeatureManagerFilterStringChangeNotify);
			doc.ActiveViewChangeNotify += new DAssemblyDocEvents_ActiveViewChangeNotifyEventHandler(OnComponentActiveViewChangeNotify);
			doc.SuppressionStateChangeNotify += new DAssemblyDocEvents_SuppressionStateChangeNotifyEventHandler(OnComponentSuppressionStateChangeNotify);
			doc.ComponentReorganizeNotify += new DAssemblyDocEvents_ComponentReorganizeNotifyEventHandler(OnComponentReorganizeNotify);
			doc.ActiveDisplayStateChangePostNotify += new DAssemblyDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnComponentActiveDisplayStateChangePostNotify);
			doc.ConfigurationChangeNotify += new DAssemblyDocEvents_ConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			doc.UnitsChangeNotify += new DAssemblyDocEvents_UnitsChangeNotifyEventHandler(OnComponentUnitsChangeNotify);
			doc.AddDvePagePreNotify += new DAssemblyDocEvents_AddDvePagePreNotifyEventHandler(OnComponentAddDvePagePreNotify);
			doc.PromptBodiesToKeepNotify += new DAssemblyDocEvents_PromptBodiesToKeepNotifyEventHandler(OnComponentPromptBodiesToKeepNotify);
			doc.CloseDesignTableNotify += new DAssemblyDocEvents_CloseDesignTableNotifyEventHandler(OnComponentCloseDesignTableNotify);
			doc.OpenDesignTableNotify += new DAssemblyDocEvents_OpenDesignTableNotifyEventHandler(OnComponentOpenDesignTableNotify);
			doc.EquationEditorPostNotify += new DAssemblyDocEvents_EquationEditorPostNotifyEventHandler(OnComponentEquationEditorPostNotify);
			doc.EquationEditorPreNotify += new DAssemblyDocEvents_EquationEditorPreNotifyEventHandler(OnComponentEquationEditorPreNotify);
			doc.FileDropPostNotify += new DAssemblyDocEvents_FileDropPostNotifyEventHandler(OnComponentFileDropPostNotify);
			doc.ClearSelectionsNotify += new DAssemblyDocEvents_ClearSelectionsNotifyEventHandler(OnComponentClearSelectionsNotify);
			doc.DestroyNotify2 += new DAssemblyDocEvents_DestroyNotify2EventHandler(OnComponentDestroyNotify2);
			doc.AddMatePostNotify += new DAssemblyDocEvents_AddMatePostNotifyEventHandler(OnComponentAddMatePostNotify);
			doc.ComponentConfigurationChangeNotify += new DAssemblyDocEvents_ComponentConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			doc.UndoPostNotify += new DAssemblyDocEvents_UndoPostNotifyEventHandler(OnComponentUndoPostNotify);
			doc.AddMatePostNotify2 += new DAssemblyDocEvents_AddMatePostNotify2EventHandler(OnComponentAddMatePostNotify2);
			doc.PublishTo3DPDFNotify += new DAssemblyDocEvents_PublishTo3DPDFNotifyEventHandler(OnComponentPublishTo3DPDFNotify);
			doc.FeatureManagerTabActivatedNotify += new DAssemblyDocEvents_FeatureManagerTabActivatedNotifyEventHandler(OnComponentFeatureManagerTabActivatedNotify);
			doc.FeatureManagerTabActivatedPreNotify += new DAssemblyDocEvents_FeatureManagerTabActivatedPreNotifyEventHandler(OnComponentFeatureManagerTabActivatedPreNotify);
			doc.RenamedDocumentNotify += new DAssemblyDocEvents_RenamedDocumentNotifyEventHandler(OnComponentRenamedDocumentNotify);
			doc.PreRenameItemNotify += new DAssemblyDocEvents_PreRenameItemNotifyEventHandler(OnComponentPreRenameItemNotify);
			doc.CommandManagerTabActivatedPreNotify += new DAssemblyDocEvents_CommandManagerTabActivatedPreNotifyEventHandler(OnComponentCommandManagerTabActivatedPreNotify);
			doc.ComponentDisplayModeChangePostNotify += new DAssemblyDocEvents_ComponentDisplayModeChangePostNotifyEventHandler(OnComponentDisplayModeChangePostNotify);
			doc.ComponentDisplayModeChangePreNotify += new DAssemblyDocEvents_ComponentDisplayModeChangePreNotifyEventHandler(OnComponentDisplayModeChangePreNotify);
			doc.UserSelectionPostNotify += new DAssemblyDocEvents_UserSelectionPostNotifyEventHandler(OnComponentUserSelectionPostNotify);
			doc.ModifyTableNotify += new DAssemblyDocEvents_ModifyTableNotifyEventHandler(OnComponentModifyTableNotify);
			doc.InsertTableNotify += new DAssemblyDocEvents_InsertTableNotifyEventHandler(OnComponentInsertTableNotify);
			doc.DragStateChangeNotify += new DAssemblyDocEvents_DragStateChangeNotifyEventHandler(OnComponentDragStateChangeNotify);
			doc.AutoSaveToStorageStoreNotify += new DAssemblyDocEvents_AutoSaveToStorageStoreNotifyEventHandler(OnComponentAutoSaveToStorageStoreNotify);
			doc.RegenPostNotify2 += new DAssemblyDocEvents_RegenPostNotify2EventHandler(OnComponentRegenPostNotify2);
			doc.SelectiveOpenPostNotify += new DAssemblyDocEvents_SelectiveOpenPostNotifyEventHandler(OnComponentSelectiveOpenPostNotify);
			doc.ComponentReferredDisplayStateChangeNotify += new DAssemblyDocEvents_ComponentReferredDisplayStateChangeNotifyEventHandler(OnComponentReferredDisplayStateChangeNotify);
			doc.UndoPreNotify += new DAssemblyDocEvents_UndoPreNotifyEventHandler(OnComponentUndoPreNotify);
			doc.RedoPreNotify += new DAssemblyDocEvents_RedoPreNotifyEventHandler(OnComponentRedoPreNotify);
			doc.RedoPostNotify += new DAssemblyDocEvents_RedoPostNotifyEventHandler(OnComponentRedoPostNotify);
			doc.UserSelectionPreNotify += new DAssemblyDocEvents_UserSelectionPreNotifyEventHandler(OnComponentUserSelectionPreNotify);
			doc.DeleteItemPreNotify += new DAssemblyDocEvents_DeleteItemPreNotifyEventHandler(OnComponentDeleteItemPreNotify);
			doc.ComponentStateChangeNotify3 += new DAssemblyDocEvents_ComponentStateChangeNotify3EventHandler(OnComponentStateChangeNotify3);
			doc.SketchSolveNotify += new DAssemblyDocEvents_SketchSolveNotifyEventHandler(OnComponentSketchSolveNotify);
			doc.FileReloadCancelNotify += new DAssemblyDocEvents_FileReloadCancelNotifyEventHandler(OnComponentFileReloadCancelNotify);
			doc.FileDropNotify += new DAssemblyDocEvents_FileDropNotifyEventHandler(OnComponentFileDropNotify);
			doc.ComponentStateChangeNotify += new DAssemblyDocEvents_ComponentStateChangeNotifyEventHandler(OnComponentStateChangeNotify);
			doc.ModifyNotify += new DAssemblyDocEvents_ModifyNotifyEventHandler(OnComponentModifyNotify);
			doc.DeleteItemNotify += new DAssemblyDocEvents_DeleteItemNotifyEventHandler(OnComponentDeleteItemNotify);
			doc.RenameItemNotify += new DAssemblyDocEvents_RenameItemNotifyEventHandler(OnComponentRenameItemNotify);
			doc.AddItemNotify += new DAssemblyDocEvents_AddItemNotifyEventHandler(OnComponentAddItemNotify);
			doc.LightingDialogCreateNotify += new DAssemblyDocEvents_LightingDialogCreateNotifyEventHandler(OnComponentLightingDialogCreateNotify);
			doc.ViewNewNotify2 += new DAssemblyDocEvents_ViewNewNotify2EventHandler(OnComponentViewNewNotify2);
			doc.EndInContextEditNotify += new DAssemblyDocEvents_EndInContextEditNotifyEventHandler(OnComponentEndInContextEditNotify);
			doc.FileReloadNotify += new DAssemblyDocEvents_FileReloadNotifyEventHandler(OnComponentFileReloadNotify);
			doc.BeginInContextEditNotify += new DAssemblyDocEvents_BeginInContextEditNotifyEventHandler(OnComponentBeginInContextEditNotify);
			doc.ActiveConfigChangeNotify += new DAssemblyDocEvents_ActiveConfigChangeNotifyEventHandler(OnComponentActiveConfigChangeNotify);
			doc.SaveToStorageNotify += new DAssemblyDocEvents_SaveToStorageNotifyEventHandler(OnComponentSaveToStorageNotify);
			doc.LoadFromStorageNotify += new DAssemblyDocEvents_LoadFromStorageNotifyEventHandler(OnComponentLoadFromStorageNotify);
			doc.FileSaveAsNotify += new DAssemblyDocEvents_FileSaveAsNotifyEventHandler(OnComponentFileSaveAsNotify);
			doc.FileSaveNotify += new DAssemblyDocEvents_FileSaveNotifyEventHandler(OnComponentFileSaveNotify);
			doc.NewSelectionNotify += new DAssemblyDocEvents_NewSelectionNotifyEventHandler(OnComponentNewSelectionNotify);
			doc.ViewNewNotify += new DAssemblyDocEvents_ViewNewNotifyEventHandler(OnComponentViewNewNotify);
			doc.RegenPostNotify += new DAssemblyDocEvents_RegenPostNotifyEventHandler(OnComponentRegenPostNotify);
			doc.DestroyNotify += new DAssemblyDocEvents_DestroyNotifyEventHandler(OnComponentDestroyNotify);
			doc.ActiveConfigChangePostNotify += new DAssemblyDocEvents_ActiveConfigChangePostNotifyEventHandler(OnComponentActiveConfigChangePostNotify);
			doc.ComponentStateChangeNotify2 += new DAssemblyDocEvents_ComponentStateChangeNotify2EventHandler(OnComponentStateChangeNotify2);
			doc.AddCustomPropertyNotify += new DAssemblyDocEvents_AddCustomPropertyNotifyEventHandler(OnComponentAddCustomPropertyNotify);
			doc.ChangeCustomPropertyNotify += new DAssemblyDocEvents_ChangeCustomPropertyNotifyEventHandler(OnComponentChangeCustomPropertyNotify);
			doc.DimensionChangeNotify += new DAssemblyDocEvents_DimensionChangeNotifyEventHandler(OnComponentDimensionChangeNotify);
			doc.ComponentDisplayStateChangeNotify += new DAssemblyDocEvents_ComponentDisplayStateChangeNotifyEventHandler(OnComponentDisplayStateChangeNotify);
			doc.ComponentVisualPropertiesChangeNotify += new DAssemblyDocEvents_ComponentVisualPropertiesChangeNotifyEventHandler(OnComponentVisualPropertiesChangeNotify);
			doc.DynamicHighlightNotify += new DAssemblyDocEvents_DynamicHighlightNotifyEventHandler(OnComponentDynamicHighlightNotify);
			doc.ComponentMoveNotify2 += new DAssemblyDocEvents_ComponentMoveNotify2EventHandler(OnComponentMoveNotify2);
			doc.AssemblyElectricalDataUpdateNotify += new DAssemblyDocEvents_AssemblyElectricalDataUpdateNotifyEventHandler(OnComponentAssemblyElectricalDataUpdateNotify);
			doc.FeatureManagerTreeRebuildNotify += new DAssemblyDocEvents_FeatureManagerTreeRebuildNotifyEventHandler(OnComponentFeatureManagerTreeRebuildNotify);
			doc.SaveToStorageStoreNotify += new DAssemblyDocEvents_SaveToStorageStoreNotifyEventHandler(OnComponentSaveToStorageStoreNotify);
			doc.LoadFromStorageStoreNotify += new DAssemblyDocEvents_LoadFromStorageStoreNotifyEventHandler(OnComponentLoadFromStorageStoreNotify);
			doc.FileSavePostNotify += new DAssemblyDocEvents_FileSavePostNotifyEventHandler(OnComponentFileSavePostNotify);
			doc.FileDropPreNotify += new DAssemblyDocEvents_FileDropPreNotifyEventHandler(OnComponentFileDropPreNotify);
			doc.BodyVisibleChangeNotify += new DAssemblyDocEvents_BodyVisibleChangeNotifyEventHandler(OnComponentBodyVisibleChangeNotify);
			doc.ComponentVisibleChangeNotify += new DAssemblyDocEvents_ComponentVisibleChangeNotifyEventHandler(OnComponentVisibleChangeNotify);
			doc.ComponentMoveNotify += new DAssemblyDocEvents_ComponentMoveNotifyEventHandler(OnComponentMoveNotify);
			doc.FileReloadPreNotify += new DAssemblyDocEvents_FileReloadPreNotifyEventHandler(OnComponentFileReloadPreNotify);
			doc.DeleteSelectionPreNotify += new DAssemblyDocEvents_DeleteSelectionPreNotifyEventHandler(OnComponentDeleteSelectionPreNotify);
			doc.InterferenceNotify += new DAssemblyDocEvents_InterferenceNotifyEventHandler(OnComponentInterferenceNotify);
			doc.FileSaveAsNotify2 += new DAssemblyDocEvents_FileSaveAsNotify2EventHandler(OnComponentFileSaveAsNotify2);
			doc.FeatureSketchEditPreNotify += new DAssemblyDocEvents_FeatureSketchEditPreNotifyEventHandler(OnComponentFeatureSketchEditPreNotify);
			doc.FeatureEditPreNotify += new DAssemblyDocEvents_FeatureEditPreNotifyEventHandler(OnComponentFeatureEditPreNotify);
			doc.DeleteCustomPropertyNotify += new DAssemblyDocEvents_DeleteCustomPropertyNotifyEventHandler(OnComponentDeleteCustomPropertyNotify);
			doc.FileSavePostCancelNotify += new DAssemblyDocEvents_FileSavePostCancelNotifyEventHandler(OnComponentFileSavePostCancelNotify);
			doc.RenameDisplayTitleNotify += new DAssemblyDocEvents_RenameDisplayTitleNotifyEventHandler(OnComponentRenameDisplayTitleNotify);


			ConnectModelViews();

			return true;
		}

		override public bool DetachEventHandlers()
		{
			doc.RegenNotify -= new DAssemblyDocEvents_RegenNotifyEventHandler(OnComponentRegenNotify);
			doc.ActiveDisplayStateChangePreNotify -= new DAssemblyDocEvents_ActiveDisplayStateChangePreNotifyEventHandler(OnComponentActiveDisplayStateChangePreNotify);
			doc.SensorAlertPreNotify -= new DAssemblyDocEvents_SensorAlertPreNotifyEventHandler(OnComponentSensorAlertPreNotify);
			doc.AutoSaveToStorageNotify -= new DAssemblyDocEvents_AutoSaveToStorageNotifyEventHandler(OnComponentAutoSaveToStorageNotify);
			doc.AutoSaveNotify -= new DAssemblyDocEvents_AutoSaveNotifyEventHandler(OnComponentAutoSaveNotify);
			doc.FlipLoopNotify -= new DAssemblyDocEvents_FlipLoopNotifyEventHandler(OnComponentFlipLoopNotify);
			doc.FeatureManagerFilterStringChangeNotify -= new DAssemblyDocEvents_FeatureManagerFilterStringChangeNotifyEventHandler(OnComponentFeatureManagerFilterStringChangeNotify);
			doc.ActiveViewChangeNotify -= new DAssemblyDocEvents_ActiveViewChangeNotifyEventHandler(OnComponentActiveViewChangeNotify);
			doc.SuppressionStateChangeNotify -= new DAssemblyDocEvents_SuppressionStateChangeNotifyEventHandler(OnComponentSuppressionStateChangeNotify);
			doc.ComponentReorganizeNotify -= new DAssemblyDocEvents_ComponentReorganizeNotifyEventHandler(OnComponentReorganizeNotify);
			doc.ActiveDisplayStateChangePostNotify -= new DAssemblyDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnComponentActiveDisplayStateChangePostNotify);
			doc.ConfigurationChangeNotify -= new DAssemblyDocEvents_ConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			doc.UnitsChangeNotify -= new DAssemblyDocEvents_UnitsChangeNotifyEventHandler(OnComponentUnitsChangeNotify);
			doc.AddDvePagePreNotify -= new DAssemblyDocEvents_AddDvePagePreNotifyEventHandler(OnComponentAddDvePagePreNotify);
			doc.PromptBodiesToKeepNotify -= new DAssemblyDocEvents_PromptBodiesToKeepNotifyEventHandler(OnComponentPromptBodiesToKeepNotify);
			doc.CloseDesignTableNotify -= new DAssemblyDocEvents_CloseDesignTableNotifyEventHandler(OnComponentCloseDesignTableNotify);
			doc.OpenDesignTableNotify -= new DAssemblyDocEvents_OpenDesignTableNotifyEventHandler(OnComponentOpenDesignTableNotify);
			doc.EquationEditorPostNotify -= new DAssemblyDocEvents_EquationEditorPostNotifyEventHandler(OnComponentEquationEditorPostNotify);
			doc.EquationEditorPreNotify -= new DAssemblyDocEvents_EquationEditorPreNotifyEventHandler(OnComponentEquationEditorPreNotify);
			doc.FileDropPostNotify -= new DAssemblyDocEvents_FileDropPostNotifyEventHandler(OnComponentFileDropPostNotify);
			doc.ClearSelectionsNotify -= new DAssemblyDocEvents_ClearSelectionsNotifyEventHandler(OnComponentClearSelectionsNotify);
			doc.DestroyNotify2 -= new DAssemblyDocEvents_DestroyNotify2EventHandler(OnComponentDestroyNotify2);
			doc.AddMatePostNotify -= new DAssemblyDocEvents_AddMatePostNotifyEventHandler(OnComponentAddMatePostNotify);
			doc.ComponentConfigurationChangeNotify -= new DAssemblyDocEvents_ComponentConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			doc.UndoPostNotify -= new DAssemblyDocEvents_UndoPostNotifyEventHandler(OnComponentUndoPostNotify);
			doc.AddMatePostNotify2 -= new DAssemblyDocEvents_AddMatePostNotify2EventHandler(OnComponentAddMatePostNotify2);
			doc.PublishTo3DPDFNotify -= new DAssemblyDocEvents_PublishTo3DPDFNotifyEventHandler(OnComponentPublishTo3DPDFNotify);
			doc.FeatureManagerTabActivatedNotify -= new DAssemblyDocEvents_FeatureManagerTabActivatedNotifyEventHandler(OnComponentFeatureManagerTabActivatedNotify);
			doc.FeatureManagerTabActivatedPreNotify -= new DAssemblyDocEvents_FeatureManagerTabActivatedPreNotifyEventHandler(OnComponentFeatureManagerTabActivatedPreNotify);
			doc.RenamedDocumentNotify -= new DAssemblyDocEvents_RenamedDocumentNotifyEventHandler(OnComponentRenamedDocumentNotify);
			doc.PreRenameItemNotify -= new DAssemblyDocEvents_PreRenameItemNotifyEventHandler(OnComponentPreRenameItemNotify);
			doc.CommandManagerTabActivatedPreNotify -= new DAssemblyDocEvents_CommandManagerTabActivatedPreNotifyEventHandler(OnComponentCommandManagerTabActivatedPreNotify);
			doc.ComponentDisplayModeChangePostNotify -= new DAssemblyDocEvents_ComponentDisplayModeChangePostNotifyEventHandler(OnComponentDisplayModeChangePostNotify);
			doc.ComponentDisplayModeChangePreNotify -= new DAssemblyDocEvents_ComponentDisplayModeChangePreNotifyEventHandler(OnComponentDisplayModeChangePreNotify);
			doc.UserSelectionPostNotify -= new DAssemblyDocEvents_UserSelectionPostNotifyEventHandler(OnComponentUserSelectionPostNotify);
			doc.ModifyTableNotify -= new DAssemblyDocEvents_ModifyTableNotifyEventHandler(OnComponentModifyTableNotify);
			doc.InsertTableNotify -= new DAssemblyDocEvents_InsertTableNotifyEventHandler(OnComponentInsertTableNotify);
			doc.DragStateChangeNotify -= new DAssemblyDocEvents_DragStateChangeNotifyEventHandler(OnComponentDragStateChangeNotify);
			doc.AutoSaveToStorageStoreNotify -= new DAssemblyDocEvents_AutoSaveToStorageStoreNotifyEventHandler(OnComponentAutoSaveToStorageStoreNotify);
			doc.RegenPostNotify2 -= new DAssemblyDocEvents_RegenPostNotify2EventHandler(OnComponentRegenPostNotify2);
			doc.SelectiveOpenPostNotify -= new DAssemblyDocEvents_SelectiveOpenPostNotifyEventHandler(OnComponentSelectiveOpenPostNotify);
			doc.ComponentReferredDisplayStateChangeNotify -= new DAssemblyDocEvents_ComponentReferredDisplayStateChangeNotifyEventHandler(OnComponentReferredDisplayStateChangeNotify);
			doc.UndoPreNotify -= new DAssemblyDocEvents_UndoPreNotifyEventHandler(OnComponentUndoPreNotify);
			doc.RedoPreNotify -= new DAssemblyDocEvents_RedoPreNotifyEventHandler(OnComponentRedoPreNotify);
			doc.RedoPostNotify -= new DAssemblyDocEvents_RedoPostNotifyEventHandler(OnComponentRedoPostNotify);
			doc.UserSelectionPreNotify -= new DAssemblyDocEvents_UserSelectionPreNotifyEventHandler(OnComponentUserSelectionPreNotify);
			doc.DeleteItemPreNotify -= new DAssemblyDocEvents_DeleteItemPreNotifyEventHandler(OnComponentDeleteItemPreNotify);
			doc.ComponentStateChangeNotify3 -= new DAssemblyDocEvents_ComponentStateChangeNotify3EventHandler(OnComponentStateChangeNotify3);
			doc.SketchSolveNotify -= new DAssemblyDocEvents_SketchSolveNotifyEventHandler(OnComponentSketchSolveNotify);
			doc.FileReloadCancelNotify -= new DAssemblyDocEvents_FileReloadCancelNotifyEventHandler(OnComponentFileReloadCancelNotify);
			doc.FileDropNotify -= new DAssemblyDocEvents_FileDropNotifyEventHandler(OnComponentFileDropNotify);
			doc.ComponentStateChangeNotify -= new DAssemblyDocEvents_ComponentStateChangeNotifyEventHandler(OnComponentStateChangeNotify);
			doc.ModifyNotify -= new DAssemblyDocEvents_ModifyNotifyEventHandler(OnComponentModifyNotify);
			doc.DeleteItemNotify -= new DAssemblyDocEvents_DeleteItemNotifyEventHandler(OnComponentDeleteItemNotify);
			doc.RenameItemNotify -= new DAssemblyDocEvents_RenameItemNotifyEventHandler(OnComponentRenameItemNotify);
			doc.AddItemNotify -= new DAssemblyDocEvents_AddItemNotifyEventHandler(OnComponentAddItemNotify);
			doc.LightingDialogCreateNotify -= new DAssemblyDocEvents_LightingDialogCreateNotifyEventHandler(OnComponentLightingDialogCreateNotify);
			doc.ViewNewNotify2 -= new DAssemblyDocEvents_ViewNewNotify2EventHandler(OnComponentViewNewNotify2);
			doc.EndInContextEditNotify -= new DAssemblyDocEvents_EndInContextEditNotifyEventHandler(OnComponentEndInContextEditNotify);
			doc.FileReloadNotify -= new DAssemblyDocEvents_FileReloadNotifyEventHandler(OnComponentFileReloadNotify);
			doc.BeginInContextEditNotify -= new DAssemblyDocEvents_BeginInContextEditNotifyEventHandler(OnComponentBeginInContextEditNotify);
			doc.ActiveConfigChangeNotify -= new DAssemblyDocEvents_ActiveConfigChangeNotifyEventHandler(OnComponentActiveConfigChangeNotify);
			doc.SaveToStorageNotify -= new DAssemblyDocEvents_SaveToStorageNotifyEventHandler(OnComponentSaveToStorageNotify);
			doc.LoadFromStorageNotify -= new DAssemblyDocEvents_LoadFromStorageNotifyEventHandler(OnComponentLoadFromStorageNotify);
			doc.FileSaveAsNotify -= new DAssemblyDocEvents_FileSaveAsNotifyEventHandler(OnComponentFileSaveAsNotify);
			doc.FileSaveNotify -= new DAssemblyDocEvents_FileSaveNotifyEventHandler(OnComponentFileSaveNotify);
			doc.NewSelectionNotify -= new DAssemblyDocEvents_NewSelectionNotifyEventHandler(OnComponentNewSelectionNotify);
			doc.ViewNewNotify -= new DAssemblyDocEvents_ViewNewNotifyEventHandler(OnComponentViewNewNotify);
			doc.RegenPostNotify -= new DAssemblyDocEvents_RegenPostNotifyEventHandler(OnComponentRegenPostNotify);
			doc.DestroyNotify -= new DAssemblyDocEvents_DestroyNotifyEventHandler(OnComponentDestroyNotify);
			doc.ActiveConfigChangePostNotify -= new DAssemblyDocEvents_ActiveConfigChangePostNotifyEventHandler(OnComponentActiveConfigChangePostNotify);
			doc.ComponentStateChangeNotify2 -= new DAssemblyDocEvents_ComponentStateChangeNotify2EventHandler(OnComponentStateChangeNotify2);
			doc.AddCustomPropertyNotify -= new DAssemblyDocEvents_AddCustomPropertyNotifyEventHandler(OnComponentAddCustomPropertyNotify);
			doc.ChangeCustomPropertyNotify -= new DAssemblyDocEvents_ChangeCustomPropertyNotifyEventHandler(OnComponentChangeCustomPropertyNotify);
			doc.DimensionChangeNotify -= new DAssemblyDocEvents_DimensionChangeNotifyEventHandler(OnComponentDimensionChangeNotify);
			doc.ComponentDisplayStateChangeNotify -= new DAssemblyDocEvents_ComponentDisplayStateChangeNotifyEventHandler(OnComponentDisplayStateChangeNotify);
			doc.ComponentVisualPropertiesChangeNotify -= new DAssemblyDocEvents_ComponentVisualPropertiesChangeNotifyEventHandler(OnComponentVisualPropertiesChangeNotify);
			doc.DynamicHighlightNotify -= new DAssemblyDocEvents_DynamicHighlightNotifyEventHandler(OnComponentDynamicHighlightNotify);
			doc.ComponentMoveNotify2 -= new DAssemblyDocEvents_ComponentMoveNotify2EventHandler(OnComponentMoveNotify2);
			doc.AssemblyElectricalDataUpdateNotify -= new DAssemblyDocEvents_AssemblyElectricalDataUpdateNotifyEventHandler(OnComponentAssemblyElectricalDataUpdateNotify);
			doc.FeatureManagerTreeRebuildNotify -= new DAssemblyDocEvents_FeatureManagerTreeRebuildNotifyEventHandler(OnComponentFeatureManagerTreeRebuildNotify);
			doc.SaveToStorageStoreNotify -= new DAssemblyDocEvents_SaveToStorageStoreNotifyEventHandler(OnComponentSaveToStorageStoreNotify);
			doc.LoadFromStorageStoreNotify -= new DAssemblyDocEvents_LoadFromStorageStoreNotifyEventHandler(OnComponentLoadFromStorageStoreNotify);
			doc.FileSavePostNotify -= new DAssemblyDocEvents_FileSavePostNotifyEventHandler(OnComponentFileSavePostNotify);
			doc.FileDropPreNotify -= new DAssemblyDocEvents_FileDropPreNotifyEventHandler(OnComponentFileDropPreNotify);
			doc.BodyVisibleChangeNotify -= new DAssemblyDocEvents_BodyVisibleChangeNotifyEventHandler(OnComponentBodyVisibleChangeNotify);
			doc.ComponentVisibleChangeNotify -= new DAssemblyDocEvents_ComponentVisibleChangeNotifyEventHandler(OnComponentVisibleChangeNotify);
			doc.ComponentMoveNotify -= new DAssemblyDocEvents_ComponentMoveNotifyEventHandler(OnComponentMoveNotify);
			doc.FileReloadPreNotify -= new DAssemblyDocEvents_FileReloadPreNotifyEventHandler(OnComponentFileReloadPreNotify);
			doc.DeleteSelectionPreNotify -= new DAssemblyDocEvents_DeleteSelectionPreNotifyEventHandler(OnComponentDeleteSelectionPreNotify);
			doc.InterferenceNotify -= new DAssemblyDocEvents_InterferenceNotifyEventHandler(OnComponentInterferenceNotify);
			doc.FileSaveAsNotify2 -= new DAssemblyDocEvents_FileSaveAsNotify2EventHandler(OnComponentFileSaveAsNotify2);
			doc.FeatureSketchEditPreNotify -= new DAssemblyDocEvents_FeatureSketchEditPreNotifyEventHandler(OnComponentFeatureSketchEditPreNotify);
			doc.FeatureEditPreNotify -= new DAssemblyDocEvents_FeatureEditPreNotifyEventHandler(OnComponentFeatureEditPreNotify);
			doc.DeleteCustomPropertyNotify -= new DAssemblyDocEvents_DeleteCustomPropertyNotifyEventHandler(OnComponentDeleteCustomPropertyNotify);
			doc.FileSavePostCancelNotify -= new DAssemblyDocEvents_FileSavePostCancelNotifyEventHandler(OnComponentFileSavePostCancelNotify);
			doc.RenameDisplayTitleNotify -= new DAssemblyDocEvents_RenameDisplayTitleNotifyEventHandler(OnComponentRenameDisplayTitleNotify);

			DisconnectModelViews();

			userAddin.DetachModelEventHandler(document);
			return true;
		}

		//Event Handlers

		//attach events to a component if it becomes resolved
		int OnComponentStateChangeNotify(object componentModel, short newCompState)
		{
			ModelDoc2 modDoc = (ModelDoc2)componentModel;
			swComponentSuppressionState_e newState = (swComponentSuppressionState_e)newCompState;


			switch (newState)
			{

				case swComponentSuppressionState_e.swComponentFullyResolved:
					{
						if ((modDoc != null) & !this.swAddin.OpenDocs.Contains(modDoc))
						{
							this.swAddin.AttachModelDocEventHandler(modDoc);
						}
						break;
					}

				case swComponentSuppressionState_e.swComponentResolved:
					{
						if ((modDoc != null) & !this.swAddin.OpenDocs.Contains(modDoc))
						{
							this.swAddin.AttachModelDocEventHandler(modDoc);
						}
						break;
					}

			}
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

		int OnComponentDisplayStateChangeNotify(object swObject)
		{
			Component2 component = (Component2)swObject;
			ModelDoc2 modDoc = (ModelDoc2)component.GetModelDoc();

			return OnComponentStateChangeNotify(modDoc);
		}

		// called when material changes
		int OnComponentVisualPropertiesChangeNotify(object swObject)
		{
			Component2 component = (Component2)swObject;
			ModelDoc2 modDoc = (ModelDoc2)component.GetModelDoc();

			return OnComponentStateChangeNotify(modDoc);
		}

		int OnComponentRegenNotify() { return 0; }
		int OnComponentActiveDisplayStateChangePreNotify() { return 0; }
		int OnComponentSensorAlertPreNotify(object SensorIn, int SensorAlertType) { return 0; }
		int OnComponentAutoSaveToStorageNotify() { return 0; }
		int OnComponentAutoSaveNotify(string FileName) { return 0; }
		int OnComponentFlipLoopNotify(object TheLoop, object TheEdge) { return 0; }
		int OnComponentFeatureManagerFilterStringChangeNotify(string FilterString) { return 0; }
		int OnComponentActiveViewChangeNotify() { return 0; }
		int OnComponentSuppressionStateChangeNotify(Feature Feature, int NewSuppressionState, int PreviousSuppressionState, int ConfigurationOption, ref object ConfigurationNames) { return 0; }
		int OnComponentReorganizeNotify(string sourceName, string targetName) { return 0; }
		int OnComponentActiveDisplayStateChangePostNotify(string DisplayStateName) { return 0; }
		int OnComponentConfigurationChangeNotify(string ConfigurationName, object Object, int ObjectType, int changeType) { return 0; }
		int OnComponentUnitsChangeNotify() { return 0; }
		int OnComponentAddDvePagePreNotify(int Command, ref object PageToAdd) { return 0; }
		int OnComponentPromptBodiesToKeepNotify(object Feature, ref object Bodies) { return 0; }
		int OnComponentCloseDesignTableNotify(object DesignTable) { return 0; }
		int OnComponentOpenDesignTableNotify(object DesignTable) { return 0; }
		int OnComponentEquationEditorPostNotify(Boolean Changed) { return 0; }
		int OnComponentEquationEditorPreNotify() { return 0; }
		int OnComponentFileDropPostNotify() { return 0; }
		int OnComponentClearSelectionsNotify() { return 0; }
		int OnComponentDestroyNotify2(int DestroyType) { DetachEventHandlers(); return 0; }
		int OnComponentAddMatePostNotify() { return 0; }
		int OnComponentConfigurationChangeNotify(string componentName, string oldConfigurationName, string newConfigurationName) { return 0; }
		int OnComponentUndoPostNotify() { return 0; }
		int OnComponentAddMatePostNotify2(ref object mates) { return 0; }
		int OnComponentPublishTo3DPDFNotify(string Path) { return 0; }
		int OnComponentFeatureManagerTabActivatedNotify(int CommandIndex, string CommandTabName) { return 0; }
		int OnComponentFeatureManagerTabActivatedPreNotify(int CommandIndex, string CommandTabName) { return 0; }

		int OnComponentRenamedDocumentNotify(ref object RenamedDocumentInterface)
		{
			SwSingleton.Events.ComponentRenamedEvent.Fire(doc, 
				new SwEventArgs()
				.AddParameter<AssemblyDoc>("Doc", doc)
				.AddParameter<object>("RenamedDocumentInterface", RenamedDocumentInterface)
			);
			return 0;
		}

		int OnComponentPreRenameItemNotify(int EntityType, string oldName, string NewName) { return 0; }
		int OnComponentCommandManagerTabActivatedPreNotify(int CommandTabIndex, string CommandTabName) { return 0; }
		int OnComponentDisplayModeChangePostNotify(object Component) { return 0; }
		int OnComponentDisplayModeChangePreNotify(object Component) { return 0; }
		int OnComponentUserSelectionPostNotify() { return 0; }
		int OnComponentModifyTableNotify(TableAnnotation TableAnnotation, int TableType, int reason, int RowInfo, int ColumnInfo, string DataInfo) { return 0; }
		int OnComponentInsertTableNotify(TableAnnotation TableAnnotation, string TableType, string TemplatePath) { return 0; }
		int OnComponentDragStateChangeNotify(Boolean State) { return 0; }
		int OnComponentAutoSaveToStorageStoreNotify() { return 0; }
		int OnComponentRegenPostNotify2(object stopFeature) { return 0; }
		int OnComponentSelectiveOpenPostNotify(string NewAddedDisplayStateName, ref object SelectedComponentNames) { return 0; }
		int OnComponentReferredDisplayStateChangeNotify(object componentModel, string CompName, int oldDSId, string oldDSName, int newDSId, string newDSName) { return 0; }
		int OnComponentUndoPreNotify() { return 0; }
		int OnComponentRedoPreNotify() { return 0; }
		int OnComponentRedoPostNotify() { return 0; }
		int OnComponentUserSelectionPreNotify(int SelType) { return 0; }

		int OnComponentDeleteItemPreNotify(int EntityType, string itemName)
		{
			//swNotifyComponent   2 = Assembly component is being added, renamed, or deleted
			//swNotifyComponentInternal   8 = Assembly component is internal to the assembly
			//swNotifyConfiguration	1 = Configuration is being added, renamed, or deleted
			//swNotifyDerivedConfiguration	4 = Derived configuration is being added, renamed, or deleted
			//swNotifyDrawingSheet	5 = Drawing sheet is being added, renamed, or deleted
			//swNotifyDrawingView	6 = Drawing view is being added, renamed, or deleted
			//swNotifyFeature	3 = Feature is being added, renamed, or deleted
			if (EntityType == (int)swNotifyEntityType_e.swNotifyComponent)
			{
				SwSingleton.CurrentScene.DeleteComponent(itemName);
			}
			return 0;
		}

		int OnComponentStateChangeNotify3(object Component, string CompName, short oldCompState, short newCompState) { return 0; }
		int OnComponentSketchSolveNotify(string featName) { return 0; }
		int OnComponentFileReloadCancelNotify(int ErrorCode) { return 0; }
		int OnComponentFileDropNotify(string FileName) { return 0; }
		int OnComponentModifyNotify() { return 0; }

		int OnComponentDeleteItemNotify(int EntityType, string itemName)
		{
			return 0;
		}

		int OnComponentRenameItemNotify(int EntityType, string oldName, string NewName) { return 0; }

		int OnComponentAddItemNotify(int EntityType, string itemName)
		{
			SwSingleton.Events.ComponentAddItemEvent.Fire(doc,
				new SwEventArgs()
				.AddParameter<AssemblyDoc>("Doc", doc)
				.AddParameter<int>("EntityType", EntityType)
				.AddParameter<string>("itemName", itemName)
			);
			return 0;
		}

		int OnComponentLightingDialogCreateNotify(object dialog) { return 0; }
		int OnComponentViewNewNotify2(object viewBeingAdded) { return 0; }
		int OnComponentEndInContextEditNotify(object docBeingEdited, int DocType) { return 0; }
		int OnComponentFileReloadNotify() { return 0; }
		int OnComponentBeginInContextEditNotify(object docBeingEdited, int DocType) { return 0; }
		int OnComponentActiveConfigChangeNotify() { return 0; }
		int OnComponentSaveToStorageNotify() { return 0; }
		int OnComponentLoadFromStorageNotify() { return 0; }
		int OnComponentFileSaveAsNotify(string FileName) { return 0; }
		int OnComponentFileSaveNotify(string FileName) { return 0; }
		int OnComponentNewSelectionNotify() { return 0; }
		int OnComponentViewNewNotify() { return 0; }
		int OnComponentRegenPostNotify() { return 0; }
		int OnComponentDestroyNotify() { DetachEventHandlers(); return 0; }
		int OnComponentActiveConfigChangePostNotify() { return 0; }
		int OnComponentStateChangeNotify2(object componentModel, string CompName, short oldCompState, short newCompState) { return 0; }
		int OnComponentAddCustomPropertyNotify(string propName, string Configuration, string Value, int valueType) { return 0; }
		int OnComponentChangeCustomPropertyNotify(string propName, string Configuration, string oldValue, string NewValue, int valueType) { return 0; }
		int OnComponentDimensionChangeNotify(object displayDim) { return 0; }
		int OnComponentDynamicHighlightNotify(Boolean bHighlightState) { return 0; }
		int OnComponentMoveNotify2(ref object Components) { return 0; }
		int OnComponentAssemblyElectricalDataUpdateNotify(int saveType) { return 0; }
		int OnComponentFeatureManagerTreeRebuildNotify() { return 0; }
		int OnComponentSaveToStorageStoreNotify() { return 0; }
		int OnComponentLoadFromStorageStoreNotify() { return 0; }
		int OnComponentFileSavePostNotify(int saveType, string FileName) { return 0; }
		int OnComponentFileDropPreNotify(string FileName) { return 0; }
		int OnComponentBodyVisibleChangeNotify() { return 0; }
		int OnComponentVisibleChangeNotify() { return 0; }

		// called when the component moves because of Motion Manager or explode/collapse
		int OnComponentMoveNotify()
		{
			if (SwSingleton.CurrentScene.DirectLinkOn)
				SwSingleton.CurrentScene.EvaluateSceneTransforms();
			return 0;
		}

		int OnComponentFileReloadPreNotify() { return 0; }
		int OnComponentDeleteSelectionPreNotify() { return 0; }
		int OnComponentInterferenceNotify(ref object PComp, ref object PFace) { return 0; }
		int OnComponentFileSaveAsNotify2(string FileName) { return 0; }
		int OnComponentFeatureSketchEditPreNotify(object EditFeature, object featureSketch) { return 0; }
		int OnComponentFeatureEditPreNotify(object EditFeature) { return 0; }
		int OnComponentDeleteCustomPropertyNotify(string propName, string Configuration, string Value, int valueType) { return 0; }
		int OnComponentFileSavePostCancelNotify() { return 0; }
		int OnComponentRenameDisplayTitleNotify(string oldName, string NewName) { return 0; }
	}

	public class DrawingEventHandler : DocumentEventHandler
	{
		DrawingDoc doc;

		public DrawingEventHandler(ModelDoc2 modDoc, SwAddin addin)
			: base(modDoc, addin)
		{
			doc = (DrawingDoc)document;
		}

		override public bool AttachEventHandlers()
		{
			doc.DestroyNotify += new DDrawingDocEvents_DestroyNotifyEventHandler(OnDestroy);
			doc.NewSelectionNotify += new DDrawingDocEvents_NewSelectionNotifyEventHandler(OnNewSelection);
			doc.ModifyNotify += new DDrawingDocEvents_ModifyNotifyEventHandler(OnModify);

			ConnectModelViews();

			return true;
		}

		override public bool DetachEventHandlers()
		{
			doc.DestroyNotify -= new DDrawingDocEvents_DestroyNotifyEventHandler(OnDestroy);
			doc.NewSelectionNotify -= new DDrawingDocEvents_NewSelectionNotifyEventHandler(OnNewSelection);
			doc.ModifyNotify -= new DDrawingDocEvents_ModifyNotifyEventHandler(OnModify);

			DisconnectModelViews();

			userAddin.DetachModelEventHandler(document);
			return true;
		}

		//Event Handlers
		int OnDestroy()
		{
			DetachEventHandlers();
			return 0;
		}

		int OnNewSelection()
		{
			return 0;
		}

		int OnModify()
		{
			return 0;
		}
	}

	public class DocView
	{
		ISldWorks iSwApp;
		SwAddin userAddin;
		ModelView mView;
		DocumentEventHandler parent;

		public DocView(SwAddin addin, IModelView mv, DocumentEventHandler doc)
		{
			userAddin = addin;
			mView = (ModelView)mv;
			iSwApp = (ISldWorks)userAddin.SwApp;
			parent = doc;
		}

		public bool AttachEventHandlers()
		{
			mView.RepaintNotify += new DModelViewEvents_RepaintNotifyEventHandler(OnViewRepaintNotify);
			mView.ViewChangeNotify += new DModelViewEvents_ViewChangeNotifyEventHandler(OnViewViewChangeNotify);
			mView.DestroyNotify += new DModelViewEvents_DestroyNotifyEventHandler(OnViewDestroyNotify);
			mView.RepaintPostNotify += new DModelViewEvents_RepaintPostNotifyEventHandler(OnViewRepaintPostNotify);
			mView.BufferSwapNotify += new DModelViewEvents_BufferSwapNotifyEventHandler(OnViewBufferSwapNotify);
			mView.DestroyNotify2 += new DModelViewEvents_DestroyNotify2EventHandler(OnViewDestroyNotify2);
			mView.PerspectiveViewNotify += new DModelViewEvents_PerspectiveViewNotifyEventHandler(OnViewPerspectiveViewNotify);
			mView.RenderLayer0Notify += new DModelViewEvents_RenderLayer0NotifyEventHandler(OnViewRenderLayer0Notify);
			mView.UserClearSelectionsNotify += new DModelViewEvents_UserClearSelectionsNotifyEventHandler(OnViewUserClearSelectionsNotify);
			mView.PrintNotify += new DModelViewEvents_PrintNotifyEventHandler(OnViewPrintNotify);
			mView.GraphicsRenderPostNotify += new DModelViewEvents_GraphicsRenderPostNotifyEventHandler(OnViewGraphicsRenderPostNotify);
			mView.DisplayModeChangePreNotify += new DModelViewEvents_DisplayModeChangePreNotifyEventHandler(OnViewDisplayModeChangePreNotify);
			mView.DisplayModeChangePostNotify += new DModelViewEvents_DisplayModeChangePostNotifyEventHandler(OnViewDisplayModeChangePostNotify);
			mView.PrintNotify2 += new DModelViewEvents_PrintNotify2EventHandler(OnViewPrintNotify2);
			return true;
		}

		public bool DetachEventHandlers()
		{
			mView.RepaintNotify -= new DModelViewEvents_RepaintNotifyEventHandler(OnViewRepaintNotify);
			mView.ViewChangeNotify -= new DModelViewEvents_ViewChangeNotifyEventHandler(OnViewViewChangeNotify);
			mView.DestroyNotify -= new DModelViewEvents_DestroyNotifyEventHandler(OnViewDestroyNotify);
			mView.RepaintPostNotify -= new DModelViewEvents_RepaintPostNotifyEventHandler(OnViewRepaintPostNotify);
			mView.BufferSwapNotify -= new DModelViewEvents_BufferSwapNotifyEventHandler(OnViewBufferSwapNotify);
			mView.DestroyNotify2 -= new DModelViewEvents_DestroyNotify2EventHandler(OnViewDestroyNotify2);
			mView.PerspectiveViewNotify -= new DModelViewEvents_PerspectiveViewNotifyEventHandler(OnViewPerspectiveViewNotify);
			mView.RenderLayer0Notify -= new DModelViewEvents_RenderLayer0NotifyEventHandler(OnViewRenderLayer0Notify);
			mView.UserClearSelectionsNotify -= new DModelViewEvents_UserClearSelectionsNotifyEventHandler(OnViewUserClearSelectionsNotify);
			mView.PrintNotify -= new DModelViewEvents_PrintNotifyEventHandler(OnViewPrintNotify);
			mView.GraphicsRenderPostNotify -= new DModelViewEvents_GraphicsRenderPostNotifyEventHandler(OnViewGraphicsRenderPostNotify);
			mView.DisplayModeChangePreNotify -= new DModelViewEvents_DisplayModeChangePreNotifyEventHandler(OnViewDisplayModeChangePreNotify);
			mView.DisplayModeChangePostNotify -= new DModelViewEvents_DisplayModeChangePostNotifyEventHandler(OnViewDisplayModeChangePostNotify);
			mView.PrintNotify2 -= new DModelViewEvents_PrintNotify2EventHandler(OnViewPrintNotify2);
			parent.DetachModelViewEventHandler(mView);
			return true;
		}

		//EventHandlers
		int OnViewRepaintNotify(int paintType) { return 0; }
		int OnViewViewChangeNotify(object View) { return 0; }
		int OnViewDestroyNotify() { return 0; }
		int OnViewRepaintPostNotify() { return 0; }
		int OnViewBufferSwapNotify() { return 0; }
		int OnViewDestroyNotify2(int DestroyType) { return 0; }
		int OnViewPerspectiveViewNotify(Double Left, Double Right, Double bottom, Double Top, Double zNear, Double zFar) { return 0; }
		int OnViewRenderLayer0Notify() { return 0; }
		int OnViewUserClearSelectionsNotify() { return 0; }
		int OnViewPrintNotify(Int64 pDC) { return 0; }
		int OnViewGraphicsRenderPostNotify() { return 0; }
		int OnViewDisplayModeChangePreNotify() { return 0; }
		int OnViewDisplayModeChangePostNotify() { return 0; }
		int OnViewPrintNotify2(Int64 pDC, Boolean bPreview) { return 0; }
	}

}
