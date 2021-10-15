// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using Autodesk.Revit.DB;
using Autodesk.Revit.ApplicationServices;
using Autodesk.Revit.DB.Events;
using System.Threading.Tasks;
using System.Threading;
using System.Linq;

namespace DatasmithRevitExporter
{
	public class FDirectLink
	{
		private class FCachedDocumentData
		{
			public Document															SourceDocument;
			public Dictionary<ElementId, FDocumentData.FBaseElementData>			CachedElements = new Dictionary<ElementId, FDocumentData.FBaseElementData>();
			public Queue<KeyValuePair<ElementId, FDocumentData.FBaseElementData>>	ElementsWithoutMetadata = new Queue<KeyValuePair<ElementId, FDocumentData.FBaseElementData>>();
			public HashSet<ElementId>												ExportedElements = new HashSet<ElementId>();
			public HashSet<ElementId>												ModifiedElements = new HashSet<ElementId>();
			public Dictionary<string, FDatasmithFacadeActor>						ExportedActorsMap = new Dictionary<string, FDatasmithFacadeActor>();

			public Dictionary<ElementId, FCachedDocumentData>						LinkedDocumentsCache = new Dictionary<ElementId, FCachedDocumentData>();
		
			public FCachedDocumentData(Document InDocument)
			{
				SourceDocument = InDocument;
			}

			public void SetAllElementsModified()
			{
				foreach (var Link in LinkedDocumentsCache.Values)
				{
					Link.SetAllElementsModified();
				}

				ModifiedElements.Clear();
				foreach (var ElemId in CachedElements.Keys)
				{
					ModifiedElements.Add(ElemId);
				}
			}

			public void ClearModified()
			{
				foreach (var Link in LinkedDocumentsCache.Values)
				{
					Link.ClearModified();
				}
				ModifiedElements.Clear();
			}

			// Intersect elements exported in this sync with cached elements.
			// Those out of the intersection set are either deleted or hidden 
			// and need to be removed from cache.
			public void Purge(FDatasmithFacadeScene DatasmithScene, bool bInRecursive)
			{
				if (bInRecursive)
				{
					// Call purge on linked docs first.
					foreach (var Link in LinkedDocumentsCache.Values)
					{
						Link.Purge(DatasmithScene, true);
					}
				}

				List<ElementId> ElementsToRemove = new List<ElementId>();
				foreach (var ElemId in CachedElements.Keys)
				{
					if (!ExportedElements.Contains(ElemId))
					{
						ElementsToRemove.Add(ElemId);
					}
				}

				// Apply deletions according to the accumulated sets of elements.
				foreach (var ElemId in ElementsToRemove)
				{
					if (!CachedElements.ContainsKey(ElemId))
					{
						continue;
					}
					FDocumentData.FBaseElementData ElementData = CachedElements[ElemId];
					CachedElements.Remove(ElemId);
					ElementData.Parent?.ChildElements.Remove(ElementData);
					DatasmithScene.RemoveActor(ElementData.ElementActor);
					ExportedActorsMap.Remove(ElementData.ElementActor.GetName());
				}
			}
		};

		struct SectionBoxInfo
		{
			public Element SectionBox;
			// Store bounding box because removed section boxes lose their bounding box 
			// and we can't query it anymore
			public BoundingBoxXYZ SectionBoxBounds;
		};

		public FDatasmithFacadeScene									DatasmithScene { get; private set; }

		private FCachedDocumentData										RootCache = null;
		private FCachedDocumentData										CurrentCache = null;
		
		private HashSet<Document>										ModifiedLinkedDocuments = new HashSet<Document>();
		private HashSet<ElementId>										ExportedLinkedDocuments = new HashSet<ElementId>();
		private Stack<FCachedDocumentData>								CacheStack = new Stack<FCachedDocumentData>();

		private IList<SectionBoxInfo>									PrevSectionBoxes = new List<SectionBoxInfo>();

		// Sets of elements related to current sync.
		private HashSet<ElementId>										DeletedElements = new HashSet<ElementId>();
		
		public HashSet<string>											UniqueTextureNameSet = new HashSet<string>();
		public Dictionary<string, FMaterialData>						MaterialDataMap = new Dictionary<string, FMaterialData>();

		private FDatasmithFacadeDirectLink								DatasmithDirectLink;
		private string													SceneName;

		private Dictionary<string, int>									ExportedActorNames = new Dictionary<string, int>();

		// The number of times this document was synced (sent to receiver)
		public int														SyncCount { get; private set; } = 0;

		private EventHandler<DocumentChangedEventArgs>					DocumentChangedHandler;

		// Metadata related
		private EventWaitHandle											MetadataEvent = new ManualResetEvent(false);
		private CancellationTokenSource									MetadataCancelToken = null;
		private Task													MetadataTask = null;

		private static FDirectLink										ActiveInstance = null;
		private static List<FDirectLink>								Instances = new List<FDirectLink>();

		public static FDirectLink Get()
		{
			return ActiveInstance;
		}

		public static void ActivateInstance(Document InDocument)
		{
			// Disable existing instance, if there's active one.
			ActiveInstance?.MakeActive(false);
			ActiveInstance = null;

			// Find out if we already have instance for this document and 
			// activate it if we do. Otherwise, create new one.

			FDirectLink InstanceToActivate = null;

			foreach (FDirectLink DL in Instances)
			{
				if (DL.RootCache.SourceDocument.Equals(InDocument))
				{
					InstanceToActivate = DL;
					break;
				}
			}

			if (InstanceToActivate == null)
			{
				InstanceToActivate = new FDirectLink(InDocument);
				Instances.Add(InstanceToActivate);
			}

			InstanceToActivate.MakeActive(true);
			ActiveInstance = InstanceToActivate;
		}

		public static FDirectLink FindInstance(Document InDocument)
		{
			foreach (var Inst in Instances)
			{
				if (Inst.RootCache.SourceDocument.Equals(InDocument))
				{
					return Inst;
				}
			}
			return null;
		}

		public static void DestroyInstance(FDirectLink Instance, Application InApp)
		{
			if (ActiveInstance == Instance)
			{
				ActiveInstance = null;
			}
			Instances.Remove(Instance);
			Instance?.Destroy(InApp);
		}

		public static void DestroyAllInstances(Application InApp) 
		{
			foreach (FDirectLink DL in Instances)
			{
				DestroyInstance(DL, InApp);
			}

			Instances.Clear();
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
				Element ModifiedElement = DirectLink.RootCache.SourceDocument.GetElement(ElemId);

				if (ModifiedElement.GetType() == typeof(RevitLinkInstance))
				{
					DirectLink.ModifiedLinkedDocuments.Add((ModifiedElement as RevitLinkInstance).GetLinkDocument());
				}
				else
				{
					// Handles a case where Revit won't notify us about modified mullions and their transform remains obsolte, thus wrong.
					ElementCategoryFilter Filter = new ElementCategoryFilter(BuiltInCategory.OST_CurtainWallMullions);
					IList<ElementId> DependentElements = ModifiedElement.GetDependentElements(Filter);
					if (DependentElements != null && DependentElements.Count > 0)
					{
						foreach (ElementId DepElemId in DependentElements)
						{
							DirectLink.RootCache.ModifiedElements.Add(DepElemId);
						}
					}
				}

				DirectLink.RootCache.ModifiedElements.Add(ElemId);
			}
		}

		private FDirectLink(Document InDocument)
		{
			RootCache = new FCachedDocumentData(InDocument);
			CurrentCache = RootCache;

			DatasmithScene = new FDatasmithFacadeScene(
				FDatasmithRevitExportContext.HOST_NAME,
				FDatasmithRevitExportContext.VENDOR_NAME,
				FDatasmithRevitExportContext.PRODUCT_NAME,
				InDocument.Application.VersionNumber);

			SceneName = Path.GetFileNameWithoutExtension(RootCache.SourceDocument.PathName);
			string OutputPath = Path.Combine(Path.GetTempPath(), SceneName);
			DatasmithScene.SetName(SceneName);
			DatasmithScene.SetLabel(SceneName);

			DocumentChangedHandler = new EventHandler<DocumentChangedEventArgs>(OnDocumentChanged);
			InDocument.Application.DocumentChanged += DocumentChangedHandler;
		}

		private void StopMetadataExport()
		{
			if (MetadataTask != null)
			{
				MetadataCancelToken.Cancel();
				MetadataEvent.Set();
				MetadataTask.Wait();
				MetadataEvent.Reset();
			}

			MetadataCancelToken?.Dispose();
			MetadataCancelToken = null;
			MetadataTask = null;
		}

		private void MakeActive(bool bInActive)
		{
			if (!bInActive)
			{
				DatasmithDirectLink = null;
			}
			else if (DatasmithDirectLink == null)
			{
				DatasmithDirectLink = new FDatasmithFacadeDirectLink();

				if (!DatasmithDirectLink.InitializeForScene(DatasmithScene))
				{
					throw new Exception("DirectLink: failed to initialize");
				}
			}
		}

		private void Destroy(Application InApp)
		{
			StopMetadataExport();

			InApp.DocumentChanged -= DocumentChangedHandler;
			DocumentChangedHandler = null;

			DatasmithDirectLink = null;
			DatasmithScene = null;
			RootCache = null;
			ModifiedLinkedDocuments.Clear();
		}

		public void MarkForExport(Element InElement)
		{
			if (InElement.GetType() == typeof(RevitLinkInstance))
			{
				// We want to track which links are exported and later removed the ones that 
				// were deleted from root document.
				if (!ExportedLinkedDocuments.Contains(InElement.Id))
				{
					ExportedLinkedDocuments.Add(InElement.Id);
				}
			}

			CurrentCache.ExportedElements.Add(InElement.Id);
		}

		public void ClearModified(Element InElement)
		{
			// Clear from modified set since we might get another element with same id and we dont want to skip it.
			CurrentCache.ModifiedElements.Remove(InElement.Id);
		}

		public void CacheElement(Document InDocument, Element InElement, FDocumentData.FBaseElementData InElementData)
		{
			if (!CurrentCache.CachedElements.ContainsKey(InElement.Id))
			{
				CurrentCache.CachedElements[InElement.Id] = InElementData;
				CurrentCache.ElementsWithoutMetadata.Enqueue(new KeyValuePair<ElementId, FDocumentData.FBaseElementData>(InElement.Id, InElementData));
			}
			CacheActorType(InElementData.ElementActor);
		}

		public void CacheActorType(FDatasmithFacadeActor InActor)
		{
			if (CurrentCache != null)
			{
				CurrentCache.ExportedActorsMap[InActor.GetName()] = InActor;
			}
		}

		public FDocumentData.FBaseElementData GetCachedElement(Element InElement)
		{
			FDocumentData.FBaseElementData Result = null;
			if (CurrentCache.CachedElements.TryGetValue(InElement.Id, out Result))
			{
				FDocumentData.FElementData ElementData = Result as FDocumentData.FElementData;
				if (ElementData != null)
				{
					// Re-init the element ref: in some cases (family instance update) it might become invalid.
					ElementData.CurrentElement = InElement; 
				}
			}
			return Result;
		}

		public FDatasmithFacadeActor GetCachedActor(string InActorName)
		{
			FDatasmithFacadeActor Actor = null;
			if (CurrentCache != null)
			{
				CurrentCache.ExportedActorsMap.TryGetValue(InActorName, out Actor);
			}
			return Actor;
		}

		public string EnsureUniqueActorName(string InActorName)
		{
			string UniqueName = InActorName;
			if (ExportedActorNames.ContainsKey(InActorName))
			{
				UniqueName = $"{InActorName}_{ExportedActorNames[InActorName]++}";
			}
			else
			{
				ExportedActorNames[InActorName] = 0;
			}

			return UniqueName;
		}

		public bool IsElementCached(Element InElement)
		{
			return CurrentCache.CachedElements.ContainsKey(InElement.Id);
		}

		public bool IsElementModified(Element InElement)
		{
			return CurrentCache.ModifiedElements.Contains(InElement.Id);
		}

		public void OnBeginLinkedDocument(Element InLinkElement)
		{
			Debug.Assert(InLinkElement.GetType() == typeof(RevitLinkInstance));

			Document LinkedDoc = (InLinkElement as RevitLinkInstance).GetLinkDocument();
			Debug.Assert(LinkedDoc != null);

			if (!CurrentCache.LinkedDocumentsCache.ContainsKey(InLinkElement.Id))
			{
				CurrentCache.LinkedDocumentsCache[InLinkElement.Id] = new FCachedDocumentData(LinkedDoc);
			}
			CacheStack.Push(CurrentCache.LinkedDocumentsCache[InLinkElement.Id]);
			CurrentCache = CurrentCache.LinkedDocumentsCache[InLinkElement.Id];
		}

		public void OnEndLinkedDocument()
		{
			CacheStack.Pop();
			CurrentCache = CacheStack.Count > 0 ? CacheStack.Peek() : RootCache;
		}

		public void OnBeginExport()
		{
			StopMetadataExport();

			SetSceneCachePath();

			foreach (var Link in RootCache.LinkedDocumentsCache.Values)
			{
				if (Link.SourceDocument.IsValidObject && ModifiedLinkedDocuments.Contains(Link.SourceDocument))
				{
					Link.SetAllElementsModified();
				}
			}

			// Handle section boxes.
			FilteredElementCollector Collector = new FilteredElementCollector(RootCache.SourceDocument, RootCache.SourceDocument.ActiveView.Id);
			List<SectionBoxInfo> CurrentSectionBoxes =  new List<SectionBoxInfo>();

			foreach (Element SectionBox in Collector.OfCategory(BuiltInCategory.OST_SectionBox).ToElements())
			{
				SectionBoxInfo Info = new SectionBoxInfo();
				Info.SectionBox = SectionBox;
				Info.SectionBoxBounds = SectionBox.get_BoundingBox(RootCache.SourceDocument.ActiveView);
				CurrentSectionBoxes.Add(Info);
			}

			List<SectionBoxInfo> ModifiedSectionBoxes = new List<SectionBoxInfo>();

			foreach(SectionBoxInfo CurrentSectionBoxInfo in CurrentSectionBoxes)
			{
				if (!RootCache.ModifiedElements.Contains(CurrentSectionBoxInfo.SectionBox.Id))
				{
					continue;
				}

				ModifiedSectionBoxes.Add(CurrentSectionBoxInfo);
			}

			// Check for old section boxes that were disabled since last sync.
			foreach (SectionBoxInfo PrevSectionBoxInfo in PrevSectionBoxes)
			{
				bool bSectionBoxWasDisabled = !CurrentSectionBoxes.Any(Info => Info.SectionBox.Id == PrevSectionBoxInfo.SectionBox.Id);

				if (bSectionBoxWasDisabled)
				{
					// Section box was removed, need to mark the elemets it intersected as modified
					ModifiedSectionBoxes.Add(PrevSectionBoxInfo);
				}
			}

			// Check all elements that need to be re-exported
			foreach(var SectionBoxInfo in ModifiedSectionBoxes)
			{
				MarkIntersectedElementsAsModified(RootCache, SectionBoxInfo.SectionBox, SectionBoxInfo.SectionBoxBounds);
			}

			PrevSectionBoxes = CurrentSectionBoxes;
		}

		void SetSceneCachePath()
		{
			string OutputPath = null;

			IDirectLinkUI DirectLinkUI = IDatasmithExporterUIModule.Get()?.GetDirectLinkExporterUI();
			if (DirectLinkUI != null)
			{
				OutputPath = Path.Combine(DirectLinkUI.GetDirectLinkCacheDirectory(), SceneName);
			}
			else
			{
				OutputPath = Path.Combine(Path.GetTempPath(), SceneName);
			}

			if (!Directory.Exists(OutputPath))
			{
				Directory.CreateDirectory(OutputPath);
			}

			DatasmithScene.SetOutputPath(OutputPath);
		}

		void MarkIntersectedElementsAsModified(FCachedDocumentData InData, Element InSectionBox, BoundingBoxXYZ InSectionBoxBounds)
		{
			ElementFilter IntersectFilter = new BoundingBoxIntersectsFilter(new Outline(InSectionBoxBounds.Min, InSectionBoxBounds.Max));
			ICollection<ElementId> IntersectedElements = new FilteredElementCollector(InData.SourceDocument).WherePasses(IntersectFilter).ToElementIds();

			ElementFilter InsideFilter = new BoundingBoxIsInsideFilter(new Outline(InSectionBoxBounds.Min, InSectionBoxBounds.Max));
			ICollection<ElementId> InsideElements = new FilteredElementCollector(InData.SourceDocument).WherePasses(InsideFilter).ToElementIds();

			// Elements that are fully inside the section box should not be marked modified to save export time
			foreach (ElementId InsideElement in InsideElements)
			{
				IntersectedElements.Remove(InsideElement);
			}

			foreach (var ElemId in IntersectedElements)
			{
				if (!InData.ModifiedElements.Contains(ElemId))
				{
					InData.ModifiedElements.Add(ElemId);
				}
			}

			// Run the linked documents
			foreach (var LinkedDoc in InData.LinkedDocumentsCache)
			{
				MarkIntersectedElementsAsModified(LinkedDoc.Value, InSectionBox, InSectionBoxBounds);

				if (LinkedDoc.Value.ModifiedElements.Count > 0)
				{
					InData.ModifiedElements.Add(LinkedDoc.Key);
					ModifiedLinkedDocuments.Add(LinkedDoc.Value.SourceDocument);
				}
			}
		}

		void ProcessLinkedDocuments()
		{
			List<ElementId> LinkedDocumentsToRemove = new List<ElementId>();

			// Check for modified linked documents.
			foreach (var LinkedDocEntry in RootCache.LinkedDocumentsCache)
			{
				// Check if the link was removed.
				if (!ExportedLinkedDocuments.Contains(LinkedDocEntry.Key))
				{
					LinkedDocumentsToRemove.Add(LinkedDocEntry.Key);
					continue;
				}

				// Check if the link was modified.
				FCachedDocumentData LinkedDocCache = LinkedDocEntry.Value;
				if (ModifiedLinkedDocuments.Contains(LinkedDocCache.SourceDocument))
				{
					LinkedDocCache.Purge(DatasmithScene, true);
				}
				LinkedDocCache.ExportedElements.Clear();
			}

			foreach (var LinkedDoc in LinkedDocumentsToRemove)
			{
				RootCache.LinkedDocumentsCache[LinkedDoc].Purge(DatasmithScene, true);
				RootCache.LinkedDocumentsCache.Remove(LinkedDoc);
			}
		}

		// Sync materials: DatasmithScene.CleanUp() might have deleted some materials that are not referenced by 
		// meshes anymore, so we need to update our map.
		void SyncMaterials()
		{
			HashSet<string> SceneMaterials = new HashSet<string>();
			for (int MaterialIndex = 0; MaterialIndex < DatasmithScene.GetMaterialsCount(); ++MaterialIndex)
			{
				FDatasmithFacadeBaseMaterial Material = DatasmithScene.GetMaterial(MaterialIndex);
				SceneMaterials.Add(Material.GetName());
			}

			List<string> MaterialsToDelete = new List<string>();

			foreach (var MaterialKV in MaterialDataMap)
			{
				string MaterialName = MaterialKV.Key;

				if (!SceneMaterials.Contains(MaterialName))
				{
					MaterialsToDelete.Add(MaterialName);
				}
			}
			foreach (string MaterialName in MaterialsToDelete)
			{
				MaterialDataMap.Remove(MaterialName);
			}
		}

		// Sync textures: DatasmithScene.CleanUp() might have deleted some textures that are not referenced by 
		// materials anymore, so we need to update our cache.
		void SyncTextures()
		{
			HashSet<string> SceneTextures = new HashSet<string>();
			for (int TextureIndex = 0; TextureIndex < DatasmithScene.GetTexturesCount(); ++TextureIndex)
			{
				FDatasmithFacadeTexture Texture = DatasmithScene.GetTexture(TextureIndex);
				SceneTextures.Add(Texture.GetName());
			}

			List<string> TexturesToDelete = new List<string>();

			foreach (var CachedTextureName in UniqueTextureNameSet)
			{
				if (!SceneTextures.Contains(CachedTextureName))
				{
					TexturesToDelete.Add(CachedTextureName);
				}
			}
			foreach (string TextureName in TexturesToDelete)
			{
				UniqueTextureNameSet.Remove(TextureName);
			}
		}

		public void OnEndExport()
		{
			if (RootCache.LinkedDocumentsCache.Count > 0)
			{
				ProcessLinkedDocuments();
			}

			RootCache.Purge(DatasmithScene, false);

			ModifiedLinkedDocuments.Clear();
			ExportedLinkedDocuments.Clear();
			RootCache.ClearModified();
			RootCache.ExportedElements.Clear();

			DatasmithScene.CleanUp();
			DatasmithDirectLink.UpdateScene(DatasmithScene);

			SyncMaterials();
			SyncTextures();

			SyncCount++;

			// Control metadata export via env var REVIT_DIRECTLINK_WITH_METADATA.
			// We are not interested of its value, just if it was set.
			if (null != Environment.GetEnvironmentVariable("REVIT_DIRECTLINK_WITH_METADATA"))
			{
				Debug.Assert(MetadataTask == null); // We cannot have metadata export running at this point (must be stopped in OnBeginExport)
				MetadataCancelToken = new CancellationTokenSource();
				MetadataTask = Task.Run(() => ExportMetadata());
			}
		}

		void ExportMetadata()
		{
			int DelayExport = 2000;		// milliseconds
			int ExportBatchSize = 1000;	// After each batch is exported, the process will wait for DelayExport and resume (unless cancelled)
			int CurrentBatchSize = 0;

			Func<FCachedDocumentData, bool> AddElements = (FCachedDocumentData CacheData) => 
			{
				while (CacheData.ElementsWithoutMetadata.Count > 0)
				{
					if (CurrentBatchSize == ExportBatchSize)
					{
						// Add some delay before exporting next batch.
						CurrentBatchSize = 0;

						// Send metadata to DirectLink.
						DatasmithScene.CleanUp();
						DatasmithDirectLink.UpdateScene(DatasmithScene);

						MetadataEvent.WaitOne(DelayExport);
					}

					if (MetadataCancelToken.IsCancellationRequested)
					{
						return false;
					}

					var Entry = CacheData.ElementsWithoutMetadata.Dequeue();

					// Handle the case where element might be deleted in the main export path.
					if (!CacheData.CachedElements.ContainsKey(Entry.Key))
					{
						continue;
					}

					Element RevitElement = CacheData.SourceDocument.GetElement(Entry.Key);

					if (RevitElement == null)
					{
						continue;
					}

					FDocumentData.FBaseElementData ElementData = Entry.Value;
					FDatasmithFacadeActor Actor = ElementData.ElementActor;

					ElementData.ElementMetaData = new FDatasmithFacadeMetaData(Actor.GetName() + "_DATA");
					ElementData.ElementMetaData.SetLabel(Actor.GetLabel());
					ElementData.ElementMetaData.SetAssociatedElement(Actor);

					FDocumentData.AddActorMetadata(RevitElement, ElementData.ElementMetaData);

					++CurrentBatchSize;

#if DEBUG
					Debug.WriteLine($"metadata batch element {CurrentBatchSize}, remain in Q {CacheData.ElementsWithoutMetadata.Count}");
#endif
				}

				return true;
			};

			List<FCachedDocumentData> CachesToExport = new List<FCachedDocumentData>();

			Action<FCachedDocumentData> GetLinkedDocuments = null;

			GetLinkedDocuments = (FCachedDocumentData InParent) =>
			{
				CachesToExport.Add(InParent);
				foreach (var Cache in InParent.LinkedDocumentsCache.Values) 
				{
					GetLinkedDocuments(Cache);
				}
			};

			GetLinkedDocuments(RootCache);

			foreach (var Cache in CachesToExport)
			{
				bool Success = AddElements(Cache);
				if (!Success)
				{
#if DEBUG
					Debug.WriteLine("metadata cancelled");
#endif
					return; // Metadata export was cancelled.
				}
			}

			if (CurrentBatchSize > 0)
			{
				// Send remaining chunk of metadata.
				DatasmithScene.CleanUp();
				DatasmithDirectLink.UpdateScene(DatasmithScene);
			}

#if DEBUG
			Debug.WriteLine("metadata exported");
#endif
		}
	}
}
