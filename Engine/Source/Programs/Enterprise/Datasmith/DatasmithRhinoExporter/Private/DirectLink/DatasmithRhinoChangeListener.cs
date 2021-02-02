// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino;
using Rhino.DocObjects;
using Rhino.DocObjects.Tables;
using System;

namespace DatasmithRhino.DirectLink
{
	public class DatasmithRhinoChangeListener
	{
		public bool bIsListening { get => ExportContext != null; }
		private DatasmithRhinoExportContext ExportContext = null;

		/// <summary>
		/// Rhino rarely modify its object, instead it "replaces" them with a new object with the same ID.
		/// When that happens 3 events are fired in succession: ReplaceRhinoObject, DeleteRhinoObject and AddRhinoObject (if undo is disabled UndeleteRhinoObject is called instead of AddRhinoObject).
		/// We use the bIsReplacingObject to determine that these 3 events should be treated as a single "Modify" event.
		/// </summary>
		private bool bIsReplacingObject = false;

		/// <summary>
		/// Rhino replaces moved objects instead of modifying them.
		/// That means that for every BeforeTransformObjects called, the Replace, Delete and Add events will be called for each object in the BeforeTransformObject.
		/// We use this Counter (and the associated bIsMovingObjects flag) to ignore the aforementioned events, allowing us to reduce the amount of data synced.
		/// </summary>
		private int MovingObjectCounter = 0;
		private bool bIsMovingObjects { get => MovingObjectCounter > 0; }

		public void StartListening(DatasmithRhinoExportContext Context)
		{
			if (Context != null)
			{
				if (!bIsListening)
				{
					RhinoDoc.BeforeTransformObjects += OnBeforeTransformObjects;
					RhinoDoc.ModifyObjectAttributes += OnModifyObjectAttributes;
					RhinoDoc.UndeleteRhinoObject += OnUndeleteRhinoObject;
					RhinoDoc.PurgeRhinoObject += OnPurgeRhinoObject;
					RhinoDoc.AddRhinoObject += OnAddRhinoObject;
					RhinoDoc.DeleteRhinoObject += OnDeleteRhinoObject;
					RhinoDoc.ReplaceRhinoObject += OnReplaceRhinoObject;
					RhinoDoc.LayerTableEvent += OnLayerTableEvent;
					RhinoDoc.GroupTableEvent += OnGroupTableEvent;

					//#ueent-todo Listen to the following events to update their associated DatasmithElement
					//RhinoDoc.InstanceDefinitionTableEvent += InstanceDefinitionTableEvent;
					//RhinoDoc.DimensionStyleTableEvent;
					//RhinoDoc.LightTableEvent;
					//RhinoDoc.MaterialTableEvent;
					//RhinoDoc.RenderMaterialsTableEvent;
					//RhinoDoc.RenderEnvironmentTableEvent;
					//RhinoDoc.RenderTextureTableEvent;
					//RhinoDoc.TextureMappingEvent;
					//RhinoDoc.DocumentPropertiesChanged;
				}

				ExportContext = Context;
			}
		}

		public void StopListening()
		{
			ExportContext = null;

			RhinoDoc.BeforeTransformObjects -= OnBeforeTransformObjects;
			RhinoDoc.ModifyObjectAttributes -= OnModifyObjectAttributes;
			RhinoDoc.UndeleteRhinoObject -= OnUndeleteRhinoObject;
			RhinoDoc.PurgeRhinoObject -= OnPurgeRhinoObject;
			RhinoDoc.AddRhinoObject -= OnAddRhinoObject;
			RhinoDoc.DeleteRhinoObject -= OnDeleteRhinoObject;
			RhinoDoc.ReplaceRhinoObject -= OnReplaceRhinoObject;
			RhinoDoc.LayerTableEvent -= OnLayerTableEvent;
			RhinoDoc.GroupTableEvent -= OnGroupTableEvent;
		}

		private void OnBeforeTransformObjects(object Sender, RhinoTransformObjectsEventArgs RhinoEventArgs)
		{
			//Copied object will call their own creation event.
			if (!RhinoEventArgs.ObjectsWillBeCopied)
			{
				System.Diagnostics.Debug.Assert(MovingObjectCounter == 0, "Did not complete previous Object transform before starting a new one");
				MovingObjectCounter = RhinoEventArgs.ObjectCount;

				for (int ObjectIndex = 0; ObjectIndex < RhinoEventArgs.ObjectCount; ++ObjectIndex)
				{
					TryCatchExecute(() => ExportContext.MoveActor(RhinoEventArgs.Objects[ObjectIndex], RhinoEventArgs.Transform));
				}
			}
		}

		private void OnModifyObjectAttributes(object Sender, RhinoModifyObjectAttributesEventArgs RhinoEventArgs)
		{
			bool bReparent = RhinoEventArgs.OldAttributes.LayerIndex != RhinoEventArgs.NewAttributes.LayerIndex;
			TryCatchExecute(() => ExportContext.ModifyActor(RhinoEventArgs.RhinoObject, bReparent));
		}

		private void OnUndeleteRhinoObject(object Sender, RhinoObjectEventArgs RhinoEventArgs)
		{
			AddActor(RhinoEventArgs.TheObject);
		}

		private void OnPurgeRhinoObject(object Sender, RhinoObjectEventArgs RhinoEventArgs)
		{
			TryCatchExecute(() => ExportContext.DeleteActor(RhinoEventArgs.TheObject));
		}

		private void OnAddRhinoObject(object Sender, RhinoObjectEventArgs RhinoEventArgs)
		{
			AddActor(RhinoEventArgs.TheObject);
		}

		private void OnDeleteRhinoObject(object Sender, RhinoObjectEventArgs RhinoEventArgs)
		{
			// Replacing an object (modifying it) involves deleting it first, then creating or "undeleting" a new object with the same ID.
			// Since with Datasmith we actually can (and want) to update the existing Elements, ignore the Deletion here.
			if (!bIsReplacingObject && !bIsMovingObjects)
			{
				TryCatchExecute(() => ExportContext.DeleteActor(RhinoEventArgs.TheObject));
			}
		}

		private void OnReplaceRhinoObject(object Sender, RhinoReplaceObjectEventArgs RhinoEventArgs)
		{
			if (bIsMovingObjects)
			{
				return;
			}

			System.Diagnostics.Debug.Assert(!bIsReplacingObject, "Did not complete object replacement before starting a new one");
			bIsReplacingObject = true;
		}

		private void OnLayerTableEvent(object Sender, LayerTableEventArgs RhinoEventArgs)
		{
			switch (RhinoEventArgs.EventType)
			{
				case LayerTableEventType.Added:
				case LayerTableEventType.Undeleted:
					TryCatchExecute(() => ExportContext.AddActor(RhinoEventArgs.NewState));
					break;
				case LayerTableEventType.Modified:
					bool bReparent = RhinoEventArgs?.OldState.ParentLayerId != RhinoEventArgs?.NewState.ParentLayerId;
					TryCatchExecute(() => ExportContext.ModifyActor(RhinoEventArgs.NewState, bReparent));
					break;
				case LayerTableEventType.Deleted:
					TryCatchExecute(() => ExportContext.DeleteActor(RhinoEventArgs.OldState));
					break;
				case LayerTableEventType.Sorted:
				case LayerTableEventType.Current:
				default:
					break;
			}
		}

		private void OnGroupTableEvent(object Sender, GroupTableEventArgs RhinoEventArgs)
		{
			TryCatchExecute(() =>
			{
				if (RhinoEventArgs.EventType == GroupTableEventType.Deleted)
				{
					ExportContext.UpdateGroups(RhinoEventArgs.EventType, RhinoEventArgs.NewState);
				}
				else if (RhinoEventArgs.EventType != GroupTableEventType.Sorted)
				{
					ExportContext.UpdateGroups(RhinoEventArgs.EventType, RhinoEventArgs.NewState);
				}
			});
		}

		public void AddActor(RhinoObject InObject)
		{
			if (bIsMovingObjects)
			{
				MovingObjectCounter--;
				return;
			}

			if (bIsReplacingObject)
			{
				TryCatchExecute(() => ExportContext.ModifyActor(InObject, /*bReparent=*/false));
				bIsReplacingObject = false;
			}
			else if (!InObject.IsInstanceDefinitionGeometry)
			{
				//Only add the object to the scene if it's not part of an instance definition. 
				//If it part of a definition it will be added when the actual instance will be created.
				TryCatchExecute(() => ExportContext.AddActor(InObject));
			}
		}

		/// <summary>
		/// Helper function used to catch any error the DatasmithExporter may trigger, otherwise it would fail silently.
		/// </summary>
		/// <param name="UpdateAction"></param>
		private void TryCatchExecute(Action UpdateAction)
		{
			try
			{
				UpdateAction();
			}
			catch (Exception e)
			{
				RhinoApp.WriteLine("An unexpected error has occurred:");
				RhinoApp.WriteLine(e.ToString());
			}
		}
	}
}