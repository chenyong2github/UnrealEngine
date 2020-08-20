// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using Autodesk.Revit.DB;
using Autodesk.Revit.ApplicationServices;
using Autodesk.Revit.DB.Architecture;
using Autodesk.Revit.DB.Mechanical;
using Autodesk.Revit.DB.Plumbing;
using Autodesk.Revit.DB.Structure;
using Autodesk.Revit.DB.Visual;
using System.Linq;
using System.Threading;

namespace DatasmithRevitExporter
{
	public class FExportData
	{
		public Dictionary<string, FDatasmithFacadeMesh>					MeshMap = new Dictionary<string, FDatasmithFacadeMesh>();
		public Dictionary<ElementId, FDocumentData.FBaseElementData>	ActorMap = new Dictionary<ElementId, FDocumentData.FBaseElementData>();
		public HashSet<ElementId>										ModifiedActorSet = new HashSet<ElementId>();
		public Dictionary<string, FMaterialData>						MaterialDataMap = new Dictionary<string, FMaterialData>();
	};

	public class FDirectLink : IUpdater
	{
		private FExportData CachedExportData = new FExportData();
		private UpdaterId RevitUpdaterId;
		private FDatasmithFacadeDirectLink DatasmithDirectLink;

		public FDatasmithFacadeScene DatasmithScene { get; private set; }
		public Document RootDocument { get; private set; }

		private HashSet<ElementId> DeletedElements = new HashSet<ElementId>();
		private HashSet<ElementId> ModifiedElements = new HashSet<ElementId>();

// 		private System.Timers.Timer MetadataExportTimer;

// 		private bool bInitialMetadataExport = false;

		private readonly CancellationTokenSource CTS = new CancellationTokenSource();

		public void Destroy()
		{
			if (RevitUpdaterId != null)
			{
				UpdaterRegistry.UnregisterUpdater(RevitUpdaterId);
			}
			CachedExportData.MeshMap.Clear();
			CachedExportData.ActorMap.Clear();
			CachedExportData.MaterialDataMap.Clear();
			CachedExportData.ModifiedActorSet.Clear();

			// Make sure timer wont be running after DirectLink gets destroyed
			// (and thus referencing null refs)
// 			if (MetadataExportTimer != null)
// 			{
// 				CTS.Cancel();
// 				while (MetadataExportTimer.Enabled)
// 				{
// 					Thread.Sleep(10);
// 				}
// 				MetadataExportTimer = null;
// 			}

			DatasmithDirectLink = null;
			DatasmithScene = null;
			RootDocument = null;
			DeletedElements = null;
		}

		// Apply accumulated modifications of revit document to the datasmith scene.
		public void SyncRevitDocument()
		{
			if (DatasmithScene == null)
			{
				return;
			}

			foreach(var ElemId in DeletedElements)
			{
				if (!CachedExportData.ActorMap.ContainsKey(ElemId))
				{
					continue;
				}
				FDocumentData.FBaseElementData ActorData = CachedExportData.ActorMap[ElemId];
				DatasmithScene.RemoveActor(ActorData.ElementActor);
				CachedExportData.ActorMap.Remove(ElemId);
			}

			DeletedElements.Clear();
		}

		public FExportData GetOrAddCache(Document InDocument)
		{
			// TODO add support for linked documents!
			Debug.Assert(InDocument.Equals(RootDocument));
			return CachedExportData;
		}

		public bool IsActorCached(ElementId ElemId)
		{
			if (CachedExportData != null)
			{
				return CachedExportData.ActorMap.ContainsKey(ElemId);
			}
			return false;
		}

		public void CacheActor(Document InDocument, ElementId ElemId, FDocumentData.FBaseElementData InActor)
		{
			FExportData Cache = GetOrAddCache(InDocument);
			Cache.ActorMap[ElemId] = InActor;
		}

		public void OnBeginExport(Document InDocument)
		{
			if (RootDocument == null)
			{
				RootDocument = InDocument;

				DatasmithScene = new FDatasmithFacadeScene(
					FDatasmithRevitExportContext.HOST_NAME,
					FDatasmithRevitExportContext.VENDOR_NAME,
					FDatasmithRevitExportContext.PRODUCT_NAME,
					InDocument.Application.VersionNumber);

				RevitUpdaterId = new UpdaterId(InDocument.Application.ActiveAddInId, new Guid("fafbf6b2-4c06-42d4-97c1-d1b4eb593eff"));
				UpdaterRegistry.RegisterUpdater(this);
				RegisterFilters();

				DatasmithDirectLink = new FDatasmithFacadeDirectLink();
				bool bInitOk = DatasmithDirectLink.InitializeForScene(DatasmithScene);
			}

			foreach (var Elem in ModifiedElements)
			{
				if (!CachedExportData.ActorMap.ContainsKey(Elem))
				{
					continue;
				}
				CachedExportData.ModifiedActorSet.Add(Elem);
			}

			// In case of existing direct link instance: make sure it is matching the current document.
			Debug.Assert(InDocument.Equals(RootDocument));

			ModifiedElements.Clear();
		}

		public void OnEndExport()
		{
			CachedExportData.ModifiedActorSet.Clear();

			string SceneName = Path.GetFileNameWithoutExtension(RootDocument.PathName);

			string OutputPath = Path.Combine(Path.GetTempPath(), SceneName);
			Directory.CreateDirectory(OutputPath);

			DatasmithScene.ExportAssets(OutputPath);
			DatasmithScene.BuildScene(SceneName);

			bool bUpdateOk = DatasmithDirectLink.UpdateScene(DatasmithScene);
/*
			// Schedule metadata export.
			int MetadataExportDelay = 3000;
			int MetadataExportBatchSize = 1000;
			int MetadataExportedSize = 0;

			if (bInitialMetadataExport)
			{
				MetadataExportTimer = new System.Timers.Timer(MetadataExportDelay);
				MetadataExportTimer.Elapsed += (s, e) => 
				{
					for (int i = 0;  i < MetadataExportBatchSize && MetadataExportedSize < CachedExportData.ActorMap.Count; ++i, ++MetadataExportedSize)
					{
						if (CTS.IsCancellationRequested) return;

						KeyValuePair<ElementId, FDocumentData.FBaseElementData> Entry = CachedExportData.ActorMap.ElementAt(MetadataExportedSize);
						Element RevitElement = RootDocument.GetElement(Entry.Key);
						FDatasmithFacadeActor Actor = Entry.Value.ElementActor;
						FDocumentData.AddActorMetadata(RevitElement, Entry.Value.ElementMetaData);
						Entry.Value.ModifiedFlags = FDocumentData.EActorModifiedFlags.ActorModifiedMetadata;
					}

					if (CTS.IsCancellationRequested) return;

					// Send metadata to DirectLink.
					DatasmithScene.BuildScene(SceneName);

					if (CTS.IsCancellationRequested) return;

					DatasmithDirectLink.UpdateScene(DatasmithScene);

					if (!CTS.IsCancellationRequested && MetadataExportedSize < CachedExportData.ActorMap.Count)
					{
						MetadataExportTimer.Start();
					}
				};

				MetadataExportTimer.AutoReset = false;
				MetadataExportTimer.Start();
			}

			bInitialMetadataExport = false;
*/
		}

		// IUpdater implementation

		public void Execute(UpdaterData InData)
		{
			if (RootDocument == null)
			{
				return; // Nothing to do, we don't have cached data for this document.
			}

			// Handle modified elements
			foreach (ElementId ElemId in InData.GetModifiedElementIds())
			{
				ModifiedElements.Add(ElemId);
			}

			// Handle deleted elements
			foreach (ElementId ElemId in InData.GetDeletedElementIds())
			{
				DeletedElements.Add(ElemId);
			}

			// Handle new elements
			foreach (ElementId ElemId in InData.GetAddedElementIds())
			{
				if (DeletedElements.Contains(ElemId))
				{
					// Undo command
					DeletedElements.Remove(ElemId);
				}
			}
		}

		public void RegisterFilters()
		{
			List<ElementClassFilter> Filters = new List<ElementClassFilter>();
			Filters.Add(new ElementClassFilter(typeof(Wall)));
			Filters.Add(new ElementClassFilter(typeof(Railing)));
			Filters.Add(new ElementClassFilter(typeof(FlexDuct)));
			Filters.Add(new ElementClassFilter(typeof(FlexPipe)));
			Filters.Add(new ElementClassFilter(typeof(FamilyInstance)));

			foreach (var Filter in Filters)
			{
				UpdaterRegistry.AddTrigger(RevitUpdaterId, Filter, Element.GetChangeTypeElementAddition());
				UpdaterRegistry.AddTrigger(RevitUpdaterId, Filter, Element.GetChangeTypeElementDeletion());
				UpdaterRegistry.AddTrigger(RevitUpdaterId, Filter, Element.GetChangeTypeGeometry());
			}

			// Change type = element addition
			//UpdaterRegistry.AddTrigger(RevitUpdaterId, wallFilter, Element.GetChangeTypeElementAddition());
			//UpdaterRegistry.AddTrigger(RevitUpdaterId, wallFilter, Element.GetChangeTypeElementDeletion());
			//UpdaterRegistry.AddTrigger(RevitUpdaterId, wallFilter, Element.GetChangeTypeGeometry());
			//ElementCategoryFilter f = new ElementCategoryFilter(BuiltInCategory.OST_ModelText);
			//UpdaterRegistry.AddTrigger(updater.GetUpdaterId(), f, Element.GetChangeTypeAny());
		}

		public string GetAdditionalInformation()
		{
			return "Datasmith Element Tracker";
		}

		public ChangePriority GetChangePriority()
		{
			return ChangePriority.FloorsRoofsStructuralWalls;
		}

		public UpdaterId GetUpdaterId()
		{
			return RevitUpdaterId;
		}

		public string GetUpdaterName()
		{
			return "DatasmithElementTracker";
		}

		// End IUpdater implementation

	}
}
