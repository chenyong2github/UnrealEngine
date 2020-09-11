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
using Autodesk.Revit.DB.Events;

namespace DatasmithRevitExporter
{
	public class FExportData
	{
		public Dictionary<string, FDatasmithFacadeMesh>					MeshMap = new Dictionary<string, FDatasmithFacadeMesh>();
		public Dictionary<ElementId, FDocumentData.FBaseElementData>	ActorMap = new Dictionary<ElementId, FDocumentData.FBaseElementData>();
		public HashSet<ElementId>										ModifiedActorSet = new HashSet<ElementId>();
		public Dictionary<string, FMaterialData>						MaterialDataMap = new Dictionary<string, FMaterialData>();
	};

	public class FDirectLink
	{
		private static FDirectLink Instance;

		private FExportData CachedExportData = new FExportData();

		private FDatasmithFacadeDirectLink DatasmithDirectLink;

		public FDatasmithFacadeScene DatasmithScene { get; private set; }
		public Document RootDocument { get; private set; }

		private HashSet<ElementId> DeletedElements = new HashSet<ElementId>();
		private HashSet<ElementId> ModifiedElements = new HashSet<ElementId>();

		private EventHandler<DocumentChangedEventArgs> DocumentChangedHandler;

		private System.Timers.Timer MetadataExportTimer;

		private bool bInitialMetadataExport = false;

		private readonly CancellationTokenSource CTS = new CancellationTokenSource();

		public static void InitInstance()
		{
			Debug.Assert(Instance == null);
			Instance = new FDirectLink();
		}

		public static void DestroyInstance(Application InApp)
		{
			if (Instance == null)
			{
				return;
			}
			Instance.Destroy(InApp);
			Instance = null;
		}

		private void Destroy(Application InApp)
		{
			if (DocumentChangedHandler != null)
			{
				InApp.DocumentChanged -= DocumentChangedHandler;
				DocumentChangedHandler = null;
			}

			CachedExportData.MeshMap.Clear();
			CachedExportData.ActorMap.Clear();
			CachedExportData.MaterialDataMap.Clear();
			CachedExportData.ModifiedActorSet.Clear();

			// Make sure timer wont be running after DirectLink gets destroyed
			// (and thus referencing null refs)
 			if (MetadataExportTimer != null)
 			{
 				CTS.Cancel();
 				while (MetadataExportTimer.Enabled)
 				{
 					Thread.Sleep(10);
 				}
 				MetadataExportTimer = null;
 			}

			DatasmithDirectLink = null;
			DatasmithScene = null;
			RootDocument = null;
			DeletedElements = null;
		}

		public static FDirectLink Get()
		{
			return Instance;
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
				FDocumentData.FBaseElementData ElementData = CachedExportData.ActorMap[ElemId];
				CachedExportData.ActorMap.Remove(ElemId);
				ElementData.Parent?.ChildElements.Remove(ElementData);
				DatasmithScene.RemoveActor(ElementData.ElementActor);
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

				string SceneLabel = Path.GetFileNameWithoutExtension(RootDocument.PathName);
				DatasmithScene.SetLabel(SceneLabel);

				DatasmithDirectLink = new FDatasmithFacadeDirectLink();
				bool bInitOk = DatasmithDirectLink.InitializeForScene(DatasmithScene);

				DocumentChangedHandler = new EventHandler<DocumentChangedEventArgs>(OnDocumentChanged);
				InDocument.Application.DocumentChanged += DocumentChangedHandler;
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

						FDocumentData.FBaseElementData ElementData = Entry.Value;

						ElementData.ElementMetaData = new FDatasmithFacadeMetaData(Actor.GetName() + "_DATA");
						ElementData.ElementMetaData.SetLabel(Actor.GetLabel());
						ElementData.ElementMetaData.SetAssociatedElement(Actor);

						FDocumentData.AddActorMetadata(RevitElement, ElementData.ElementMetaData);
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
		}

		public static void OnDocumentChanged(
		  object InSender,
		  DocumentChangedEventArgs InArgs) 
		{
			FDirectLink DirectLink = FDirectLink.Get();

			Debug.Assert(DirectLink != null);

			// Handle modified elements
			foreach (ElementId ElemId in InArgs.GetModifiedElementIds())
			{
				DirectLink.ModifiedElements.Add(ElemId);
			}

			// Handle deleted elements
			foreach (ElementId ElemId in InArgs.GetDeletedElementIds())
			{
				DirectLink.DeletedElements.Add(ElemId);
			}

			// Handle new elements
			foreach (ElementId ElemId in InArgs.GetAddedElementIds())
			{
				if (DirectLink.DeletedElements.Contains(ElemId))
				{
					// Undo command
					DirectLink.DeletedElements.Remove(ElemId);
				}
			}
		}
	}
}
