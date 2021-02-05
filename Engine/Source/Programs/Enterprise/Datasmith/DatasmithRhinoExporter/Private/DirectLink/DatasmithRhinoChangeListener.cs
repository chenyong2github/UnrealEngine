// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino;
using Rhino.DocObjects;
using Rhino.DocObjects.Tables;
using System;
using System.Collections.Generic;

namespace DatasmithRhino.DirectLink
{
	public class DatasmithRhinoChangeListener
	{
		private enum RhinoOngoingEventTypes
		{
			None,
			ReplacingActor,
			MovingActor,
		}

		public bool bIsListening { get => ExportContext != null; }
		private DatasmithRhinoExportContext ExportContext = null;

		/// <summary>
		/// The ModifyObjectAttributes event may fire recursively while parsing the info of a RhinoObject.
		/// We use this HashSet to avoid doing recursive parsing.
		/// </summary>
		private HashSet<Guid> RecursiveEventLocks = new HashSet<Guid>();

		/// <summary>
		/// Rhino rarely modify its object, instead it "replaces" them with a new object with the same ID.
		/// When that happens 3-4 events are fired in succession: (optional) BeforeTransformObjects, ReplaceRhinoObject, DeleteRhinoObject and AddRhinoObject (if undo is disabled UndeleteRhinoObject is called instead of AddRhinoObject).
		/// We use the event stack to determine if these events should be treated as a single "ongoing" event.
		/// </summary>
		private Stack<RhinoOngoingEventTypes> EventStack = new Stack<RhinoOngoingEventTypes>();
		
		/// <summary>
		/// Returns the current ongoing event in the event stack.
		/// If there is no ongoing event, the returned type is "None".
		/// </summary>
		private RhinoOngoingEventTypes OngoingEvent { get => EventStack.Count > 0 ? EventStack.Peek() : RhinoOngoingEventTypes.None; }

		public void StartListening(DatasmithRhinoExportContext Context)
		{
			if (Context != null)
			{
				if (!bIsListening)
				{
					RhinoDoc.BeforeTransformObjects += OnBeforeTransformObjects;
					RhinoDoc.ModifyObjectAttributes += OnModifyObjectAttributes;
					RhinoDoc.UndeleteRhinoObject += OnUndeleteRhinoObject;
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
			EventStack.Clear();

			RhinoDoc.BeforeTransformObjects -= OnBeforeTransformObjects;
			RhinoDoc.ModifyObjectAttributes -= OnModifyObjectAttributes;
			RhinoDoc.UndeleteRhinoObject -= OnUndeleteRhinoObject;
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
				System.Diagnostics.Debug.Assert(OngoingEvent == RhinoOngoingEventTypes.None, "Did not complete previous Object transform before starting a new one");

				for (int ObjectIndex = 0; ObjectIndex < RhinoEventArgs.ObjectCount; ++ObjectIndex)
				{
					EventStack.Push(RhinoOngoingEventTypes.MovingActor);
					TryCatchExecute(() => ExportContext.MoveActor(RhinoEventArgs.Objects[ObjectIndex], RhinoEventArgs.Transform));
				}
			}
		}

		private void OnModifyObjectAttributes(object Sender, RhinoModifyObjectAttributesEventArgs RhinoEventArgs)
		{
			bool bReparent = RhinoEventArgs.OldAttributes.LayerIndex != RhinoEventArgs.NewAttributes.LayerIndex;
			Guid ObjectId = RhinoEventArgs.RhinoObject.Id;

			if (RecursiveEventLocks.Add(ObjectId))
			{
				TryCatchExecute(() => ExportContext.ModifyActor(RhinoEventArgs.RhinoObject, bReparent));
				RecursiveEventLocks.Remove(ObjectId);
			}
		}

		private void OnUndeleteRhinoObject(object Sender, RhinoObjectEventArgs RhinoEventArgs)
		{
			AddActor(RhinoEventArgs.TheObject);
		}

		private void OnAddRhinoObject(object Sender, RhinoObjectEventArgs RhinoEventArgs)
		{
			AddActor(RhinoEventArgs.TheObject);
		}

		private void OnDeleteRhinoObject(object Sender, RhinoObjectEventArgs RhinoEventArgs)
		{
			// Replacing or moving an object (modifying it) involves deleting it first, then creating or "undeleting" a new object with the same ID.
			// Since with Datasmith we actually can (and want) to update the existing Elements, ignore the Deletion here.
			if (OngoingEvent == RhinoOngoingEventTypes.None)
			{
				TryCatchExecute(() => ExportContext.DeleteActor(RhinoEventArgs.TheObject));
			}
		}

		private void OnReplaceRhinoObject(object Sender, RhinoReplaceObjectEventArgs RhinoEventArgs)
		{
			if (OngoingEvent != RhinoOngoingEventTypes.MovingActor)
			{
				// Event will be completed at the end of the upcoming Delete-Add events.
				EventStack.Push(RhinoOngoingEventTypes.ReplacingActor);
			}
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
			if (OngoingEvent == RhinoOngoingEventTypes.MovingActor)
			{
				EventStack.Pop();
			}
			else if (OngoingEvent == RhinoOngoingEventTypes.ReplacingActor)
			{
				TryCatchExecute(() => ExportContext.ModifyActor(InObject, /*bReparent=*/false));
				EventStack.Pop();
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
