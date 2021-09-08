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
			doc.UndoPostNotify += new DPartDocEvents_UndoPostNotifyEventHandler(OnPartUndoPostNotify);
			doc.SuppressionStateChangeNotify += new DPartDocEvents_SuppressionStateChangeNotifyEventHandler(OnPartSuppressionStateChangeNotify);
			doc.DestroyNotify2 += new DPartDocEvents_DestroyNotify2EventHandler(OnPartDestroyNotify2);
			doc.EquationEditorPostNotify += new DPartDocEvents_EquationEditorPostNotifyEventHandler(OnPartEquationEditorPostNotify);
			doc.ConfigurationChangeNotify += new DPartDocEvents_ConfigurationChangeNotifyEventHandler(OnPartConfigurationChangeNotify);
			doc.ActiveDisplayStateChangePostNotify += new DPartDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnPartActiveDisplayStateChangePostNotify);
			doc.DragStateChangeNotify += new DPartDocEvents_DragStateChangeNotifyEventHandler(OnPartDragStateChangeNotify);
			doc.UndoPreNotify += new DPartDocEvents_UndoPreNotifyEventHandler(OnPartUndoPreNotify);
			doc.RedoPreNotify += new DPartDocEvents_RedoPreNotifyEventHandler(OnPartRedoPreNotify);
			doc.RedoPostNotify += new DPartDocEvents_RedoPostNotifyEventHandler(OnPartRedoPostNotify);
			doc.ModifyNotify += new DPartDocEvents_ModifyNotifyEventHandler(OnPartModifyNotify);
			doc.AddItemNotify += new DPartDocEvents_AddItemNotifyEventHandler(OnPartAddItemNotify);
			doc.ActiveConfigChangePostNotify += new DPartDocEvents_ActiveConfigChangePostNotifyEventHandler(OnPartActiveConfigChangePostNotify);
			doc.FileReloadNotify += new DPartDocEvents_FileReloadNotifyEventHandler(OnPartFileReloadNotify);
			doc.ActiveConfigChangeNotify += new DPartDocEvents_ActiveConfigChangeNotifyEventHandler(OnPartActiveConfigChangeNotify);
			doc.FileSaveAsNotify += new DPartDocEvents_FileSaveAsNotifyEventHandler(OnPartFileSaveAsNotify);
			doc.FileSaveNotify += new DPartDocEvents_FileSaveNotifyEventHandler(OnPartFileSaveNotify);
			doc.ViewNewNotify += new DPartDocEvents_ViewNewNotifyEventHandler(OnPartViewNewNotify);
			doc.RegenPostNotify += new DPartDocEvents_RegenPostNotifyEventHandler(OnPartRegenPostNotify);
			doc.DestroyNotify += new DPartDocEvents_DestroyNotifyEventHandler(OnPartDestroyNotify);
			doc.DimensionChangeNotify += new DPartDocEvents_DimensionChangeNotifyEventHandler(OnPartDimensionChangeNotify);
			doc.RegenPostNotify2 += new DPartDocEvents_RegenPostNotify2EventHandler(OnPartRegenPostNotify2);
			doc.BodyVisibleChangeNotify += new DPartDocEvents_BodyVisibleChangeNotifyEventHandler(OnPartBodyVisibleChangeNotify);
			doc.FileSaveAsNotify2 += new DPartDocEvents_FileSaveAsNotify2EventHandler(OnPartFileSaveAsNotify2);
			doc.FileSavePostNotify += new DPartDocEvents_FileSavePostNotifyEventHandler(OnPartFileSavePostNotify);
			
			ConnectModelViews();

			return true;
		}

		override public bool DetachEventHandlers()
		{
			doc.RegenNotify -= new DPartDocEvents_RegenNotifyEventHandler(OnPartRegenNotify);
			doc.UndoPostNotify -= new DPartDocEvents_UndoPostNotifyEventHandler(OnPartUndoPostNotify);
			doc.SuppressionStateChangeNotify -= new DPartDocEvents_SuppressionStateChangeNotifyEventHandler(OnPartSuppressionStateChangeNotify);
			doc.DestroyNotify2 -= new DPartDocEvents_DestroyNotify2EventHandler(OnPartDestroyNotify2);
			doc.EquationEditorPostNotify -= new DPartDocEvents_EquationEditorPostNotifyEventHandler(OnPartEquationEditorPostNotify);
			doc.ConfigurationChangeNotify -= new DPartDocEvents_ConfigurationChangeNotifyEventHandler(OnPartConfigurationChangeNotify);
			doc.ActiveDisplayStateChangePostNotify -= new DPartDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnPartActiveDisplayStateChangePostNotify);
			doc.DragStateChangeNotify -= new DPartDocEvents_DragStateChangeNotifyEventHandler(OnPartDragStateChangeNotify);
			doc.UndoPreNotify -= new DPartDocEvents_UndoPreNotifyEventHandler(OnPartUndoPreNotify);
			doc.RedoPreNotify -= new DPartDocEvents_RedoPreNotifyEventHandler(OnPartRedoPreNotify);
			doc.RedoPostNotify -= new DPartDocEvents_RedoPostNotifyEventHandler(OnPartRedoPostNotify);
			doc.ModifyNotify -= new DPartDocEvents_ModifyNotifyEventHandler(OnPartModifyNotify);
			doc.AddItemNotify -= new DPartDocEvents_AddItemNotifyEventHandler(OnPartAddItemNotify);
			doc.ActiveConfigChangePostNotify -= new DPartDocEvents_ActiveConfigChangePostNotifyEventHandler(OnPartActiveConfigChangePostNotify);
			doc.FileReloadNotify -= new DPartDocEvents_FileReloadNotifyEventHandler(OnPartFileReloadNotify);
			doc.ActiveConfigChangeNotify -= new DPartDocEvents_ActiveConfigChangeNotifyEventHandler(OnPartActiveConfigChangeNotify);
			doc.FileSaveAsNotify -= new DPartDocEvents_FileSaveAsNotifyEventHandler(OnPartFileSaveAsNotify);
			doc.FileSaveNotify -= new DPartDocEvents_FileSaveNotifyEventHandler(OnPartFileSaveNotify);
			doc.ViewNewNotify -= new DPartDocEvents_ViewNewNotifyEventHandler(OnPartViewNewNotify);
			doc.RegenPostNotify -= new DPartDocEvents_RegenPostNotifyEventHandler(OnPartRegenPostNotify);
			doc.DestroyNotify -= new DPartDocEvents_DestroyNotifyEventHandler(OnPartDestroyNotify);
			doc.DimensionChangeNotify -= new DPartDocEvents_DimensionChangeNotifyEventHandler(OnPartDimensionChangeNotify);
			doc.RegenPostNotify2 -= new DPartDocEvents_RegenPostNotify2EventHandler(OnPartRegenPostNotify2);
			doc.BodyVisibleChangeNotify -= new DPartDocEvents_BodyVisibleChangeNotifyEventHandler(OnPartBodyVisibleChangeNotify);
			doc.FileSaveAsNotify2 -= new DPartDocEvents_FileSaveAsNotify2EventHandler(OnPartFileSaveAsNotify2);
			doc.FileSavePostNotify -= new DPartDocEvents_FileSavePostNotifyEventHandler(OnPartFileSavePostNotify);
			
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

		int OnPartRegenNotify()
		{
			return 0;
		}

		int OnPartUndoPostNotify()
		{
			SwSingleton.CurrentScene.bIsDirty = true;
			return 0;
		}

		// part has become resolved or lightweight or suppressed
		int OnPartSuppressionStateChangeNotify(Feature Feature, int NewSuppressionState, int PreviousSuppressionState, int ConfigurationOption, ref object ConfigurationNames)
		{
			return 0;
		}

		int OnPartEquationEditorPostNotify(Boolean Changed)
		{
			return 0;
		}

		int OnPartConfigurationChangeNotify(string ConfigurationName, object Object, int ObjectType, int changeType)
		{
			return 0;
		}

		int OnPartActiveDisplayStateChangePostNotify(string DisplayStateName)
		{
			return 0;
		}

		int OnPartDragStateChangeNotify(Boolean State)
		{
			SwSingleton.CurrentScene.bIsDirty = !State;
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
			return 0;
		}

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

		int OnPartRegenPostNotify()
		{
			return 0;
		}

		int OnPartDimensionChangeNotify(object displayDim)
		{
			return 0;
		}

		int OnPartRegenPostNotify2(object stopFeature)
		{
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
			doc.ActiveViewChangeNotify += new DAssemblyDocEvents_ActiveViewChangeNotifyEventHandler(OnComponentActiveViewChangeNotify);
			doc.SuppressionStateChangeNotify += new DAssemblyDocEvents_SuppressionStateChangeNotifyEventHandler(OnComponentSuppressionStateChangeNotify);
			doc.ComponentReorganizeNotify += new DAssemblyDocEvents_ComponentReorganizeNotifyEventHandler(OnComponentReorganizeNotify);
			doc.ActiveDisplayStateChangePostNotify += new DAssemblyDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnComponentActiveDisplayStateChangePostNotify);
			doc.ConfigurationChangeNotify += new DAssemblyDocEvents_ConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			doc.DestroyNotify2 += new DAssemblyDocEvents_DestroyNotify2EventHandler(OnComponentDestroyNotify2);
			doc.ComponentConfigurationChangeNotify += new DAssemblyDocEvents_ComponentConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			doc.UndoPostNotify += new DAssemblyDocEvents_UndoPostNotifyEventHandler(OnComponentUndoPostNotify);
			doc.RenamedDocumentNotify += new DAssemblyDocEvents_RenamedDocumentNotifyEventHandler(OnComponentRenamedDocumentNotify);
			doc.DragStateChangeNotify += new DAssemblyDocEvents_DragStateChangeNotifyEventHandler(OnComponentDragStateChangeNotify);
			doc.RegenPostNotify2 += new DAssemblyDocEvents_RegenPostNotify2EventHandler(OnComponentRegenPostNotify2);
			doc.ComponentReferredDisplayStateChangeNotify += new DAssemblyDocEvents_ComponentReferredDisplayStateChangeNotifyEventHandler(OnComponentReferredDisplayStateChangeNotify);
			doc.RedoPostNotify += new DAssemblyDocEvents_RedoPostNotifyEventHandler(OnComponentRedoPostNotify);
			doc.DeleteItemPreNotify += new DAssemblyDocEvents_DeleteItemPreNotifyEventHandler(OnComponentDeleteItemPreNotify);
			doc.ComponentStateChangeNotify3 += new DAssemblyDocEvents_ComponentStateChangeNotify3EventHandler(OnComponentStateChangeNotify3);
			doc.ComponentStateChangeNotify += new DAssemblyDocEvents_ComponentStateChangeNotifyEventHandler(OnComponentStateChangeNotify);
			doc.ModifyNotify += new DAssemblyDocEvents_ModifyNotifyEventHandler(OnComponentModifyNotify);
			doc.DeleteItemNotify += new DAssemblyDocEvents_DeleteItemNotifyEventHandler(OnComponentDeleteItemNotify);
			doc.RenameItemNotify += new DAssemblyDocEvents_RenameItemNotifyEventHandler(OnComponentRenameItemNotify);
			doc.AddItemNotify += new DAssemblyDocEvents_AddItemNotifyEventHandler(OnComponentAddItemNotify);
			doc.FileReloadNotify += new DAssemblyDocEvents_FileReloadNotifyEventHandler(OnComponentFileReloadNotify);
			doc.ActiveConfigChangeNotify += new DAssemblyDocEvents_ActiveConfigChangeNotifyEventHandler(OnComponentActiveConfigChangeNotify);
			doc.FileSaveAsNotify += new DAssemblyDocEvents_FileSaveAsNotifyEventHandler(OnComponentFileSaveAsNotify);
			doc.FileSaveNotify += new DAssemblyDocEvents_FileSaveNotifyEventHandler(OnComponentFileSaveNotify);
			doc.RegenPostNotify += new DAssemblyDocEvents_RegenPostNotifyEventHandler(OnComponentRegenPostNotify);
			doc.DestroyNotify += new DAssemblyDocEvents_DestroyNotifyEventHandler(OnComponentDestroyNotify);
			doc.ActiveConfigChangePostNotify += new DAssemblyDocEvents_ActiveConfigChangePostNotifyEventHandler(OnComponentActiveConfigChangePostNotify);
			doc.ComponentStateChangeNotify2 += new DAssemblyDocEvents_ComponentStateChangeNotify2EventHandler(OnComponentStateChangeNotify2);
			doc.AddCustomPropertyNotify += new DAssemblyDocEvents_AddCustomPropertyNotifyEventHandler(OnComponentAddCustomPropertyNotify);
			doc.ChangeCustomPropertyNotify += new DAssemblyDocEvents_ChangeCustomPropertyNotifyEventHandler(OnComponentChangeCustomPropertyNotify);
			doc.DimensionChangeNotify += new DAssemblyDocEvents_DimensionChangeNotifyEventHandler(OnComponentDimensionChangeNotify);
			doc.ComponentDisplayStateChangeNotify += new DAssemblyDocEvents_ComponentDisplayStateChangeNotifyEventHandler(OnComponentDisplayStateChangeNotify);
			doc.ComponentVisualPropertiesChangeNotify += new DAssemblyDocEvents_ComponentVisualPropertiesChangeNotifyEventHandler(OnComponentVisualPropertiesChangeNotify);
			doc.ComponentMoveNotify2 += new DAssemblyDocEvents_ComponentMoveNotify2EventHandler(OnComponentMoveNotify2);
			doc.BodyVisibleChangeNotify += new DAssemblyDocEvents_BodyVisibleChangeNotifyEventHandler(OnComponentBodyVisibleChangeNotify);
			doc.ComponentVisibleChangeNotify += new DAssemblyDocEvents_ComponentVisibleChangeNotifyEventHandler(OnComponentVisibleChangeNotify);
			doc.ComponentMoveNotify += new DAssemblyDocEvents_ComponentMoveNotifyEventHandler(OnComponentMoveNotify);
			doc.FileReloadPreNotify += new DAssemblyDocEvents_FileReloadPreNotifyEventHandler(OnComponentFileReloadPreNotify);
			doc.DeleteSelectionPreNotify += new DAssemblyDocEvents_DeleteSelectionPreNotifyEventHandler(OnComponentDeleteSelectionPreNotify);
			doc.FileSaveAsNotify2 += new DAssemblyDocEvents_FileSaveAsNotify2EventHandler(OnComponentFileSaveAsNotify2);


			ConnectModelViews();

			return true;
		}

		override public bool DetachEventHandlers()
		{
			doc.RegenNotify -= new DAssemblyDocEvents_RegenNotifyEventHandler(OnComponentRegenNotify);
			doc.ActiveDisplayStateChangePreNotify -= new DAssemblyDocEvents_ActiveDisplayStateChangePreNotifyEventHandler(OnComponentActiveDisplayStateChangePreNotify);
			doc.ActiveViewChangeNotify -= new DAssemblyDocEvents_ActiveViewChangeNotifyEventHandler(OnComponentActiveViewChangeNotify);
			doc.SuppressionStateChangeNotify -= new DAssemblyDocEvents_SuppressionStateChangeNotifyEventHandler(OnComponentSuppressionStateChangeNotify);
			doc.ComponentReorganizeNotify -= new DAssemblyDocEvents_ComponentReorganizeNotifyEventHandler(OnComponentReorganizeNotify);
			doc.ActiveDisplayStateChangePostNotify -= new DAssemblyDocEvents_ActiveDisplayStateChangePostNotifyEventHandler(OnComponentActiveDisplayStateChangePostNotify);
			doc.ConfigurationChangeNotify -= new DAssemblyDocEvents_ConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			doc.DestroyNotify2 -= new DAssemblyDocEvents_DestroyNotify2EventHandler(OnComponentDestroyNotify2);
			doc.ComponentConfigurationChangeNotify -= new DAssemblyDocEvents_ComponentConfigurationChangeNotifyEventHandler(OnComponentConfigurationChangeNotify);
			doc.UndoPostNotify -= new DAssemblyDocEvents_UndoPostNotifyEventHandler(OnComponentUndoPostNotify);
			doc.RenamedDocumentNotify -= new DAssemblyDocEvents_RenamedDocumentNotifyEventHandler(OnComponentRenamedDocumentNotify);
			doc.ComponentDisplayModeChangePostNotify -= new DAssemblyDocEvents_ComponentDisplayModeChangePostNotifyEventHandler(OnComponentDisplayModeChangePostNotify);
			doc.DragStateChangeNotify -= new DAssemblyDocEvents_DragStateChangeNotifyEventHandler(OnComponentDragStateChangeNotify);
			doc.RegenPostNotify2 -= new DAssemblyDocEvents_RegenPostNotify2EventHandler(OnComponentRegenPostNotify2);
			doc.ComponentReferredDisplayStateChangeNotify -= new DAssemblyDocEvents_ComponentReferredDisplayStateChangeNotifyEventHandler(OnComponentReferredDisplayStateChangeNotify);
			doc.RedoPostNotify -= new DAssemblyDocEvents_RedoPostNotifyEventHandler(OnComponentRedoPostNotify);
			doc.DeleteItemPreNotify -= new DAssemblyDocEvents_DeleteItemPreNotifyEventHandler(OnComponentDeleteItemPreNotify);
			doc.ComponentStateChangeNotify3 -= new DAssemblyDocEvents_ComponentStateChangeNotify3EventHandler(OnComponentStateChangeNotify3);
			doc.ComponentStateChangeNotify -= new DAssemblyDocEvents_ComponentStateChangeNotifyEventHandler(OnComponentStateChangeNotify);
			doc.ModifyNotify -= new DAssemblyDocEvents_ModifyNotifyEventHandler(OnComponentModifyNotify);
			doc.DeleteItemNotify -= new DAssemblyDocEvents_DeleteItemNotifyEventHandler(OnComponentDeleteItemNotify);
			doc.RenameItemNotify -= new DAssemblyDocEvents_RenameItemNotifyEventHandler(OnComponentRenameItemNotify);
			doc.AddItemNotify -= new DAssemblyDocEvents_AddItemNotifyEventHandler(OnComponentAddItemNotify);
			doc.FileReloadNotify -= new DAssemblyDocEvents_FileReloadNotifyEventHandler(OnComponentFileReloadNotify);
			doc.ActiveConfigChangeNotify -= new DAssemblyDocEvents_ActiveConfigChangeNotifyEventHandler(OnComponentActiveConfigChangeNotify);
			doc.FileSaveAsNotify -= new DAssemblyDocEvents_FileSaveAsNotifyEventHandler(OnComponentFileSaveAsNotify);
			doc.FileSaveNotify -= new DAssemblyDocEvents_FileSaveNotifyEventHandler(OnComponentFileSaveNotify);
			doc.RegenPostNotify -= new DAssemblyDocEvents_RegenPostNotifyEventHandler(OnComponentRegenPostNotify);
			doc.DestroyNotify -= new DAssemblyDocEvents_DestroyNotifyEventHandler(OnComponentDestroyNotify);
			doc.ActiveConfigChangePostNotify -= new DAssemblyDocEvents_ActiveConfigChangePostNotifyEventHandler(OnComponentActiveConfigChangePostNotify);
			doc.ComponentStateChangeNotify2 -= new DAssemblyDocEvents_ComponentStateChangeNotify2EventHandler(OnComponentStateChangeNotify2);
			doc.AddCustomPropertyNotify -= new DAssemblyDocEvents_AddCustomPropertyNotifyEventHandler(OnComponentAddCustomPropertyNotify);
			doc.ChangeCustomPropertyNotify -= new DAssemblyDocEvents_ChangeCustomPropertyNotifyEventHandler(OnComponentChangeCustomPropertyNotify);
			doc.DimensionChangeNotify -= new DAssemblyDocEvents_DimensionChangeNotifyEventHandler(OnComponentDimensionChangeNotify);
			doc.ComponentDisplayStateChangeNotify -= new DAssemblyDocEvents_ComponentDisplayStateChangeNotifyEventHandler(OnComponentDisplayStateChangeNotify);
			doc.ComponentVisualPropertiesChangeNotify -= new DAssemblyDocEvents_ComponentVisualPropertiesChangeNotifyEventHandler(OnComponentVisualPropertiesChangeNotify);
			doc.ComponentMoveNotify2 -= new DAssemblyDocEvents_ComponentMoveNotify2EventHandler(OnComponentMoveNotify2);
			doc.BodyVisibleChangeNotify -= new DAssemblyDocEvents_BodyVisibleChangeNotifyEventHandler(OnComponentBodyVisibleChangeNotify);
			doc.ComponentVisibleChangeNotify -= new DAssemblyDocEvents_ComponentVisibleChangeNotifyEventHandler(OnComponentVisibleChangeNotify);
			doc.ComponentMoveNotify -= new DAssemblyDocEvents_ComponentMoveNotifyEventHandler(OnComponentMoveNotify);
			doc.FileReloadPreNotify -= new DAssemblyDocEvents_FileReloadPreNotifyEventHandler(OnComponentFileReloadPreNotify);
			doc.DeleteSelectionPreNotify -= new DAssemblyDocEvents_DeleteSelectionPreNotifyEventHandler(OnComponentDeleteSelectionPreNotify);
			doc.FileSaveAsNotify2 -= new DAssemblyDocEvents_FileSaveAsNotify2EventHandler(OnComponentFileSaveAsNotify2);

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

		int OnComponentSuppressionStateChangeNotify(Feature Feature, int NewSuppressionState, int PreviousSuppressionState, int ConfigurationOption, ref object ConfigurationNames)
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

		int OnComponentConfigurationChangeNotify(string ConfigurationName, object Object, int ObjectType, int changeType)
		{
			return 0;
		}

		int OnComponentDestroyNotify2(int DestroyType)
		{
			DetachEventHandlers();
			return 0;
		}

		int OnComponentConfigurationChangeNotify(string componentName, string oldConfigurationName, string newConfigurationName)
		{
			return 0;
		}

		int OnComponentUndoPostNotify()
		{
			SwSingleton.CurrentScene.bIsDirty = true;
			return 0;
		}

		int OnComponentRenamedDocumentNotify(ref object RenamedDocumentInterface)
		{
			SwSingleton.Events.ComponentRenamedEvent.Fire(doc, 
				new SwEventArgs()
				.AddParameter<AssemblyDoc>("Doc", doc)
				.AddParameter<object>("RenamedDocumentInterface", RenamedDocumentInterface)
			);
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

		int OnComponentStateChangeNotify3(object Component, string CompName, short oldCompState, short newCompState)
		{
			return 0;
		}

		int OnComponentModifyNotify()
		{
			return 0;
		}

		int OnComponentDeleteItemNotify(int EntityType, string itemName)
		{
			return 0;
		}

		int OnComponentRenameItemNotify(int EntityType, string oldName, string NewName)
		{
			return 0;
		}

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

		int OnComponentDestroyNotify()
		{
			DetachEventHandlers();
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

		int OnComponentMoveNotify2(ref object Components)
		{
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
			if (SwSingleton.CurrentScene.bDirectLinkAutoSync)
			{
				SwSingleton.CurrentScene.EvaluateSceneTransforms();
				SwSingleton.CurrentScene.bIsDirty = true;
			}

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
