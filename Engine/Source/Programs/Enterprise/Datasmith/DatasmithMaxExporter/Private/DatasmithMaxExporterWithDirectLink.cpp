// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NEW_DIRECTLINK_PLUGIN

#include "DatasmithMaxDirectLink.h"

#include "DatasmithMaxExporter.h"

#include "DatasmithMaxSceneExporter.h"
#include "DatasmithMaxHelper.h"
#include "DatasmithMaxWriter.h"
#include "DatasmithMaxClassIDs.h"

#include "DatasmithMaxLogger.h"
#include "DatasmithMaxSceneParser.h"
#include "DatasmithMaxCameraExporter.h"
#include "DatasmithMaxAttributes.h"
#include "DatasmithMaxProgressManager.h"
#include "DatasmithMaxMeshExporter.h"
#include "DatasmithMaxExporterUtils.h"

#include "Modules/ModuleManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

#include "DatasmithExporterManager.h"
#include "DatasmithExportOptions.h"
#include "DatasmithSceneExporter.h"

#include "DatasmithMesh.h"
#include "DatasmithMeshExporter.h"

#include "IDatasmithExporterUIModule.h"
#include "IDirectLinkUI.h"



#include "DatasmithSceneFactory.h"

#include "DatasmithDirectLink.h"

#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"

#include "Async/Async.h"

#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "Max.h"
	#include "bitmap.h"
	#include "gamma.h"

	#include "notify.h"

	#include "ilayer.h"
	#include "ilayermanager.h"

	#include "ISceneEventManager.h"

	#include "IFileResolutionManager.h" // for GetActualPath
MAX_INCLUDES_END

namespace DatasmithMaxDirectLink
{

typedef Texmap* FTexmapKey;

class FDatasmith3dsMaxScene
{
public:
	FDatasmith3dsMaxScene() 
	{
		ResetScene();
	}

	void ResetScene()
	{
		DatasmithSceneRef.Reset();
		SceneExporterRef.Reset();
	}

	void SetupScene()
	{
		DatasmithSceneRef = FDatasmithSceneFactory::CreateScene(TEXT(""));
		SceneExporterRef = MakeShared<FDatasmithSceneExporter>();

		MSTR Renderer;
		FString Host;
		Host = TEXT("Autodesk 3dsmax ") + FString::FromInt(MAX_VERSION_MAJOR) + TEXT(".") + FString::FromInt(MAX_VERSION_MINOR) + TEXT(".") + FString::FromInt(MAX_VERSION_POINT);
		GetCOREInterface()->GetCurrentRenderer()->GetClassName(Renderer);

		DatasmithSceneRef->SetProductName(TEXT("3dsmax"));
		DatasmithSceneRef->SetHost( *( Host + Renderer ) );

		// Set the vendor name of the application used to build the scene.
		DatasmithSceneRef->SetVendor(TEXT("Autodesk"));

		FString Version = FString::FromInt(MAX_VERSION_MAJOR) + TEXT(".") + FString::FromInt(MAX_VERSION_MINOR) + TEXT(".") + FString::FromInt(MAX_VERSION_POINT);
		DatasmithSceneRef->SetProductVersion(*Version);
	}

	TSharedRef<IDatasmithScene> GetDatasmithScene()
	{
		return DatasmithSceneRef.ToSharedRef();
	}

	FDatasmithSceneExporter& GetSceneExporter()
	{
		return *SceneExporterRef;
	}

	void SetName(const TCHAR* InName)
	{
		SceneExporterRef->SetName(InName);
		DatasmithSceneRef->SetName(InName);
		DatasmithSceneRef->SetLabel(InName);
	}

	void SetOutputPath(const TCHAR* InOutputPath)
	{
		// Set the output folder where this scene will be exported.
		SceneExporterRef->SetOutputPath(InOutputPath);
		DatasmithSceneRef->SetResourcePath(SceneExporterRef->GetOutputPath());
	}

	void PreExport()
	{
		// Create a Datasmith scene exporter.
		SceneExporterRef->Reset();

		// Start measuring the time taken to export the scene.
		SceneExporterRef->PreExport();
	}

	TSharedPtr<IDatasmithScene> DatasmithSceneRef;
	TSharedPtr<FDatasmithSceneExporter> SceneExporterRef;
};

class FNodeTrackerHandle
{
public:
	explicit FNodeTrackerHandle(FNodeKey InNodeKey, INode* InNode) : Impl(MakeShared<FNodeTracker>(InNodeKey, InNode)) {}

	FNodeTracker* GetNodeTracker() const
	{
		return Impl.Get();
	}

private:
	TSharedPtr<FNodeTracker> Impl;
};

// Every node which is resolved to the same object is considered an instance
// This class holds all this nodes and the object they resolve to
struct FInstances
{
	Object* EvaluatedObj = nullptr;
	Mtl* Material = nullptr; // Material assigned to Datasmith StaticMesh, used to check if a particular instance needs to verride it

	TSet<class FNodeTracker*> NodeTrackers;

	// Mesh conversion results
	TSet<uint16> SupportedChannels;
	TSharedPtr<IDatasmithMeshElement> DatasmithMeshElement;
};

class FLayerTracker
{
public:
	FLayerTracker(const FString& InName, bool bInIsHidden): Name(InName), bIsHidden(bInIsHidden)
	{
	}

	void SetName(const FString& InName)
	{
		if (Name == InName)
		{
			return;
		}
		bIsInvalidated = true;
		Name = InName;
	}

	void SetIsHidden(bool bInIsHidden)
	{
		if (bIsHidden == bInIsHidden)
		{
			return;
		}
		bIsInvalidated = true;
		bIsHidden = bInIsHidden;
	}

	FString Name;
	bool bIsHidden = false;

	bool bIsInvalidated = true;
};



class FUpdateProgress
{
	TUniquePtr<FDatasmithMaxProgressManager> ProgressManager;
	int32 StageIndex = 0;
	int32 StageCount;
public:
	FUpdateProgress(bool bShowProgressBar, int32 InStageCount) : StageCount(InStageCount)
	{
		if (bShowProgressBar)
		{
			ProgressManager = MakeUnique<FDatasmithMaxProgressManager>();
		}
	}

	void ProgressStage(const TCHAR* Name)
	{
		LogDebug(Name);
		if (ProgressManager)
		{
			StageIndex++;
			ProgressManager->SetMainMessage(*FString::Printf(TEXT("%s (%d of %d)"), Name, StageIndex, StageCount));
			ProgressManager->ProgressEvent(0, TEXT(""));
		}
	}

	void ProgressEvent(float Progress, const TCHAR* Message)
	{
		LogDebug(FString::Printf(TEXT("%f %s"), Progress, Message));
		if (ProgressManager)
		{
			ProgressManager->ProgressEvent(Progress, Message);
		}
	}
};

class FProgressCounter
{
public:

	FProgressCounter(FUpdateProgress& InProgressManager, int32 InCount)
		: ProgressManager(InProgressManager)
		, Count(InCount)
		, SecondsOfLastUpdate(FPlatformTime::Seconds())
	{
	}

	void Next()
	{
		double CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime - SecondsOfLastUpdate > UpdateIntervalMin) // Don't span progress bar
		{
			ProgressManager.ProgressEvent(float(Index) / Count, *FString::Printf(TEXT("%d of %d"), Index, Count) );
			SecondsOfLastUpdate = CurrentTime;
		}
		Index++;
	}
private:
	FUpdateProgress& ProgressManager;
	int32 Count;
	int32 Index = 0;
	const double UpdateIntervalMin = 0.05; // Don't update progress it last update was just recently
	double SecondsOfLastUpdate;
};

// Convert various node data to Datasmith tags
class FTagsConverter
{
public:
	void ConvertNodeTags(FNodeTracker& NodeTracker)
	{
		INode* Node = NodeTracker.Node;
		INode* ParentNode = Node->GetParentNode();
		DatasmithMaxExporterUtils::ExportMaxTagsForDatasmithActor( NodeTracker.DatasmithActorElement, Node, ParentNode, KnownMaxDesc, KnownMaxSuperClass );
	}

private:
	// We don't know how the 3ds max lookup_MaxClass is implemented so we use this map to skip it when we can
	TMap<TPair<uint32, TPair<uint32, uint32>>, MAXClass*> KnownMaxDesc;
	// Same for the lookup_MAXSuperClass.
	TMap<uint32, MAXSuperClass*> KnownMaxSuperClass;
	
};


// In order to retrieve Render geometry rather than Viewport geometry
// RenderBegin need to be called for all RefMakers to be exported (and RenderEnd afterwards)
// e.g. When using Optimize modifier on a geometry it has separate LODs for Render and Viewport and
// GetRenderMesh would return Viewport lod if called without RenderBegin first. Consequently
// without RenderEnd it would display Render LOD in viewport.
class FNodesPreparer
{
public:

	class FBeginRefEnumProc : public RefEnumProc
	{
	public:
		void SetTime(TimeValue StartTime)
		{
			Time = StartTime;
		}

		virtual int proc(ReferenceMaker* RefMaker) override
		{
			RefMaker->RenderBegin(Time);
			return REF_ENUM_CONTINUE;
		}

	private:
		TimeValue Time;
	};

	class FEndRefEnumProc : public RefEnumProc
	{
	public:
		void SetTime(TimeValue EndTime)
		{
			Time = EndTime;
		}

		virtual int proc(ReferenceMaker* RefMaker) override
		{
			RefMaker->RenderEnd(Time);
			return REF_ENUM_CONTINUE;
		}

	private:
		TimeValue Time;
	};

	void Start(TimeValue Time, bool bInRenderQuality)
	{
		bRenderQuality = bInRenderQuality;

		BeginProc.SetTime(Time);
		EndProc.SetTime(Time);

		if (bRenderQuality)
		{
			BeginProc.BeginEnumeration();
		}
	}

	void Finish()
	{
		if (bRenderQuality)
		{
			BeginProc.EndEnumeration();

			// Call RenderEnd on every node that had RenderBegin called
			EndProc.BeginEnumeration();
			for(INode* Node: NodesPrepared)
			{
				Node->EnumRefHierarchy(EndProc);
			}
			EndProc.EndEnumeration();
			NodesPrepared.Reset();
		}
	}

	void PrepareNode(INode* Node)
	{
		if (bRenderQuality)
		{
			// Skip if node was already Prepared
			bool bIsAlreadyPrepared;
			NodesPrepared.FindOrAdd(Node, &bIsAlreadyPrepared);
			if (bIsAlreadyPrepared)
			{
				return;
			}

			Node->EnumRefHierarchy(BeginProc);
		}
	}

	bool bRenderQuality = false; // If need to call RenderBegin on all nodes to make them return Render-quality mesh

	FBeginRefEnumProc BeginProc;
	FEndRefEnumProc EndProc;
	
	TSet<INode*> NodesPrepared;
};



struct FExportOptions
{
	// Default options for DirectLink change-tracking
	bool bSelectedOnly = false;
	bool bAnimatedTransforms = false;
};

// Global export options, stored in preferences
class FPersistentExportOptions: public IPersistentExportOptions
{
public:

	void Load()
	{
		if (bLoaded)
		{
			return;
		}
		GetBool(TEXT("SelectedOnly"), Options.bSelectedOnly);
		GetBool(TEXT("AnimatedTransforms"), Options.bAnimatedTransforms);
		bLoaded = true;
	}

	virtual void SetSelectedOnly(bool bValue) override
	{
		Options.bSelectedOnly = bValue;
		SetBool(TEXT("SelectedOnly"), bValue);
	}
	virtual bool GetSelectedOnly() override
	{
		return Options.bSelectedOnly;
	}

	virtual void SetAnimatedTransforms(bool bValue) override
	{
		Options.bAnimatedTransforms = bValue;
		SetBool(TEXT("AnimatedTransforms"), bValue);
	}

	virtual bool GetAnimatedTransforms() override
	{
		return Options.bAnimatedTransforms;
	}

	void GetBool(const TCHAR* Name, bool& bValue)
	{
		if (!GConfig)
		{
			return;
		}
		FString ConfigPath = GetConfigPath();
		GConfig->GetBool(TEXT("Export"), Name, bValue, ConfigPath);
	}

	void SetBool(const TCHAR* Name, bool bValue)
	{
		if (!GConfig)
		{
			return;
		}
		FString ConfigPath = GetConfigPath();
		GConfig->SetBool(TEXT("Export"), Name, bValue, ConfigPath);
		GConfig->Flush(false, ConfigPath);
	}

	FString GetConfigPath()
	{
		FString PlugCfgPath = GetCOREInterface()->GetDir(APP_PLUGCFG_DIR);
		return FPaths::Combine(PlugCfgPath, TEXT("UnrealDatasmithMax.ini"));
	}


	FExportOptions Options;
	bool bLoaded = false;
};

// Holds states of entities for syncronization and handles change events
class FSceneTracker: public ISceneTracker
{
public:
	FSceneTracker(const FExportOptions& InOptions, FDatasmith3dsMaxScene& InExportedScene, FNotifications* InNotificationsHandler)
		: Options(InOptions)
		, ExportedScene(InExportedScene)
		, NotificationsHandler(InNotificationsHandler)
		, MaterialsCollectionTracker(*this) {}

	bool ParseScene()
	{
		INode* Node = GetCOREInterface()->GetRootNode();
		bSceneParsed = ParseScene(Node);
		return bSceneParsed;
	}

	// Parse scene or XRef scene(in this case attach to parent datasmith actor)
	bool ParseScene(INode* SceneRootNode, FXRefScene XRefScene=FXRefScene())
	{
		LogDebugNode(TEXT("ParseScene"), SceneRootNode);
		// todo: do we need Root Datasmith node of scene/XRefScene in the hierarchy?
		// is there anything we need to handle for main file root node?
		// for XRefScene? Maybe addition/removal? Do we need one node to consolidate XRefScene under?

		// nodes comming from XRef Scenes/Objects could be null
		if (!SceneRootNode)
		{
			return false;
		}

		// Parse XRefScenes
		for (int XRefChild = 0; XRefChild < SceneRootNode->GetXRefFileCount(); ++XRefChild)
		{
			DWORD XRefFlags = SceneRootNode->GetXRefFlags(XRefChild);

			// XRef is disabled - not shown in viewport/render. Not loaded.
			if (XRefFlags & XREF_DISABLED)
			{
				// todo: baseline - doesn't check this - exports even disabled and XREF_HIDDEN scenes
				continue;
			}

			FString Path = FDatasmithMaxSceneExporter::GetActualPath(SceneRootNode->GetXRefFile(XRefChild).GetFileName());
			if (FPaths::FileExists(Path) == false)
			{
				FString Error = FString("XRefScene file \"") + FPaths::GetCleanFilename(*Path) + FString("\" cannot be found");
				// todo: logging
				// DatasmithMaxLogger::Get().AddMissingAssetError(*Error);
			}
			else
			{
				ParseScene(SceneRootNode->GetXRefTree(XRefChild), FXRefScene{SceneRootNode, XRefChild});
			}
		}

		int32 ChildNum = SceneRootNode->NumberOfChildren();
		for (int32 ChildIndex = 0; ChildIndex < ChildNum; ++ChildIndex)
		{
			if (FNodeTracker* NodeTracker = ParseNode(SceneRootNode->GetChildNode(ChildIndex)))
			{
				NodeTracker->SetXRefIndex(XRefScene);
			}
		}
		return true;
	}

	FNodeTracker* ParseNode(INode* Node)
	{
		LogDebugNode(TEXT("ParseNode"), Node);

		BOOL bIsNodeHidden = Node->IsNodeHidden(TRUE);

		if (bIsNodeHidden == false)
		{
			//todo: when a refernced file is not found XRefObj is not resolved and it's kept as XREFOBJ_CLASS_ID
			// instead of resolved class that it references
//			if (ObjState.obj->ClassID() == XREFOBJ_CLASS_ID)
//			{
//				//max2017 and newer versions allow developers to get the file, with previous versions you only can retrieve the active one
//#ifdef MAX_RELEASE_R19
//				FString Path = FDatasmithMaxSceneExporter::GetActualPath(((IXRefObject8*)ObjState.obj)->GetFile(FALSE).GetFileName());
//#else
//				FString Path = FDatasmithMaxSceneExporter::GetActualPath(((IXRefObject8*)ObjState.obj)->GetActiveFile().GetFileName());
//#endif
//				if (FPaths::FileExists(Path) == false)
//				{
//					FString Error = FString("XRefObj file \"") + FPaths::GetCleanFilename(*Path) + FString("\" cannot be found");
//					DatasmithMaxLogger::Get().AddMissingAssetError(*Error);
//					bIsNodeHidden = true;
//				}
//			}
		}

		FNodeKey NodeKey = NodeEventNamespace::GetKeyByNode(Node);

		if (FNodeTrackerHandle* NodeTrackerHandle = NodeTrackers.Find(NodeKey))
		{
			// Node being added might already be tracked(e.g. if it was deleted before but Update wasn't called to SceneTracker yet)
			FNodeTracker* NodeTracker = NodeTrackerHandle->GetNodeTracker();
			ensure(NodeTracker->bDeleted);
			NodeTracker->bDeleted = false;
			InvalidateNode(*NodeTracker);
			return NodeTracker;
		}
		else
		{
			FNodeTrackerHandle& NodeTracker = AddNode(NodeKey, Node);

			// Parse children
			int32 ChildNum = Node->NumberOfChildren();
			for (int32 ChildIndex = 0; ChildIndex < ChildNum; ++ChildIndex)
			{
				ParseNode(Node->GetChildNode(ChildIndex));
			}
			return NodeTracker.GetNodeTracker();
		}
	}

	void Reset()
	{
		bSceneParsed = false;
		NodeTrackers.Reset();
		NodeTrackersNames.Reset();
		CollisionNodes.Reset();
		InvalidatedNodeTrackers.Reset();
		InvalidatedInstances.Reset();
		MaterialsCollectionTracker.Reset();
		LayersForAnimHandle.Reset();
		NodesPerLayer.Reset();
		NodeDatasmithMetadata.Reset();

		InstancesForAnimHandle.Reset();
	}

	// Check every layer and if it's modified invalidate nodes assigned to it
	// 3ds Max doesn't have events for all Layer changes(e.g. Name seems to be just an UI thing and has no notifications) so
	// we need to go through all layers every update to see what's changed
	bool UpdateLayers()
	{
		bool bChangeEncountered = false;

		ILayerManager* LayerManager = GetCOREInterface13()->GetLayerManager();
		int LayerCount = LayerManager->GetLayerCount();

		for (int LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
		{
			ILayer* Layer = LayerManager->GetLayer(LayerIndex);

			AnimHandle Handle = Animatable::GetHandleByAnim(Layer);

			TUniquePtr<FLayerTracker>& LayerTracker = LayersForAnimHandle.FindOrAdd(Handle);

			BOOL bIsHidden = Layer->IsHidden(TRUE);
			FString Name = Layer->GetName().data();

			if (!LayerTracker)
			{
				LayerTracker = MakeUnique<FLayerTracker>(Name, bIsHidden);
			}

			LayerTracker->SetName(Name);
			LayerTracker->SetIsHidden(bIsHidden);

			if (LayerTracker->bIsInvalidated)
			{
				bChangeEncountered = true;
				if (TSet<FNodeTracker*>* NodeTrackersPtr = NodesPerLayer.Find(LayerTracker.Get()))
				{
					for(FNodeTracker* NodeTracker:* NodeTrackersPtr)
					{
						InvalidateNode(*NodeTracker);
					}
				}
				LayerTracker->bIsInvalidated = false;
			}
		}
		return bChangeEncountered;
	}

	// Applies all recorded changes to Datasmith scene
	bool Update(bool bQuiet, bool bRenderQuality)
	{
		// Disable Undo, editing, redraw, messages during export/sync so that nothing changes the scene
		GetCOREInterface()->EnableUndo(false);
		GetCOREInterface()->DisableSceneRedraw();
		SuspendAll UberSuspend(TRUE, TRUE, TRUE, TRUE, TRUE, TRUE);

		// Flush all updates for SceneEventManager - so they are not received in mid of Update
		// When ProgressBar is updated it calls internal max event loop which can send unprocessed events to the callback
		if (NotificationsHandler)
		{
			NotificationsHandler->PrepareForUpdate();
		}

		DatasmithMaxLogger::Get().Purge();

		NodesPreparer.Start(GetCOREInterface()->GetTime(), bRenderQuality);

		bUpdateInProgress = true;
		bool bResult = UpdateInternalSafe(bQuiet);
		bUpdateInProgress = false;

		NodesPreparer.Finish();

		UberSuspend.Resume();
		GetCOREInterface()->EnableSceneRedraw();
		GetCOREInterface()->EnableUndo(true);

		return bResult;
	}

	bool UpdateInternalSafe(bool bQuiet)
	{
		__try
		{
			return UpdateInternal(bQuiet);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			LogInfo(TEXT("Update finished with exception"));
		}
		return false;
	}

	bool UpdateInternal(bool bQuiet)
	{
		FUpdateProgress ProgressManager(!bQuiet, 6); // Will shutdown on end of Update

		bool bChangeEncountered = false;

		if (!bSceneParsed) // Parse whole scene only once
		{
			ParseScene();
		}

		ProgressManager.ProgressStage(TEXT("Refresh layers"));
		{
			bChangeEncountered = UpdateLayers() && bChangeEncountered;
		}

		// Changes present only when there are modified layers(changes checked manually), nodes(notified by Max) or materials(notified by Max with all changes in dependencies)
		bChangeEncountered |= !InvalidatedNodeTrackers.IsEmpty();
		bChangeEncountered |= !MaterialsCollectionTracker.GetInvalidatedMaterials().IsEmpty();


		ProgressManager.ProgressStage(TEXT("Remove deleted nodes"));
		{
			TArray<FNodeTracker*> DeletedNodeTrackers;
			for (FNodeTracker* NodeTracker : InvalidatedNodeTrackers)
			{
				if (NodeTracker->bDeleted)
				{
					DeletedNodeTrackers.Add(NodeTracker);
				}
			}

			for (FNodeTracker* NodeTrackerPtr : DeletedNodeTrackers)
			{
				RemoveNodeTracker(*NodeTrackerPtr);
			}
		}

		ProgressManager.ProgressStage(TEXT("Update node names"));
		// todo: move to NameChanged and NodeAdded?
		for (FNodeTracker* NodeTracker : InvalidatedNodeTrackers)
		{
			FString Name = NodeTracker->Node->GetName();
			if (Name != NodeTracker->Name)
			{
				NodeTrackersNames[NodeTracker->Name].Remove(NodeTracker);
				NodeTracker->Name = Name;
				NodeTrackersNames.FindOrAdd(NodeTracker->Name).Add(NodeTracker);
			}
		}

		ProgressManager.ProgressStage(TEXT("Refresh collisions")); // Update set of nodes used for collision 
		{
			FProgressCounter ProgressCounter(ProgressManager, InvalidatedNodeTrackers.Num());
			TSet<FNodeTracker*> NodesWithChangedCollisionStatus; // Need to invalidate these nodes to make them renderable or to keep them from renderable depending on collision status
			for (FNodeTracker* NodeTracker : InvalidatedNodeTrackers)
			{
				ProgressCounter.Next();
				UpdateCollisionStatus(NodeTracker, NodesWithChangedCollisionStatus);
			}
			InvalidatedNodeTrackers.Append(NodesWithChangedCollisionStatus);
		}

		ProgressManager.ProgressStage(TEXT("Process invalidated nodes"));
		{
			FProgressCounter ProgressCounter(ProgressManager, InvalidatedNodeTrackers.Num());
			for (FNodeTracker* NodeTracker : InvalidatedNodeTrackers)
			{
				ProgressCounter.Next();
				UpdateNode(*NodeTracker);
			}
		}
		
		ProgressManager.ProgressStage(TEXT("Process invalidated instances"));
		{
			FProgressCounter ProgressCounter(ProgressManager, InvalidatedInstances.Num());
			for (FInstances* Instances : InvalidatedInstances)
			{
				ProgressCounter.Next();
				UpdateInstances(*Instances);
				InvalidatedNodeTrackers.Append(Instances->NodeTrackers);
			}
			InvalidatedInstances.Reset();
		}

		ProgressManager.ProgressStage(TEXT("Reparent Datasmith Actors"));
		for (FNodeTracker* NodeTracker : InvalidatedNodeTrackers)
		{
			AttachNodeToDatasmithScene(*NodeTracker);
		}
		InvalidatedNodeTrackers.Reset();

		TSet<Mtl*> ActualMaterialToUpdate;
		TSet<Texmap*> ActualTexmapsToUpdate;

		ProgressManager.ProgressStage(TEXT("Process invalidated materials"));
		{
			FProgressCounter ProgressCounter(ProgressManager, MaterialsCollectionTracker.GetInvalidatedMaterials().Num());
			for (FMaterialTracker* MaterialTracker : MaterialsCollectionTracker.GetInvalidatedMaterials())
			{
				ProgressCounter.Next();

				MaterialsCollectionTracker.UpdateMaterial(MaterialTracker);

				for (Mtl* ActualMaterial : MaterialTracker->GetActualMaterials())
				{
					ActualMaterialToUpdate.Add(ActualMaterial);
				}
				MaterialTracker->bInvalidated = false;
				for (Texmap* Texture: MaterialTracker->Textures)
				{
					ActualTexmapsToUpdate.Add(Texture);
				}
			}
			MaterialsCollectionTracker.ResetInvalidatedMaterials();
		}

		ProgressManager.ProgressStage(TEXT("Update textures"));
		{
			FProgressCounter ProgressCounter(ProgressManager, ActualTexmapsToUpdate.Num());
			for (Texmap* Texture : ActualTexmapsToUpdate)
			{
				ProgressCounter.Next();
				FDatasmithMaxMatExport::GetXMLTexture(ExportedScene.GetDatasmithScene(), Texture, ExportedScene.GetSceneExporter().GetAssetsOutputPath());
			}
		}

		ProgressManager.ProgressStage(TEXT("Update materials"));
		{
			FProgressCounter ProgressCounter(ProgressManager, ActualMaterialToUpdate.Num());
			for (Mtl* ActualMaterial : ActualMaterialToUpdate)
			{
				ProgressCounter.Next();

				// todo: make sure not reexport submaterial more than once - i.e. when a submaterial is used in two composite material
				FDatasmithMaxMatExport::bForceReexport = true;
				TSharedPtr<IDatasmithBaseMaterialElement> DatastmihMaterial = FDatasmithMaxMatExport::ExportUniqueMaterial(ExportedScene.GetDatasmithScene(), ActualMaterial, ExportedScene.GetSceneExporter().GetAssetsOutputPath());

				MaterialsCollectionTracker.SetDatasmithMaterial(ActualMaterial, DatastmihMaterial);
			}
		}

		// todo: removes textures that were added again(materials were updated). Need to fix this by identifying exactly which textures are being updated and removing ahead
		//TMap<FString, TSharedPtr<IDatasmithTextureElement>> TexturesAdded;
		//TArray<TSharedPtr<IDatasmithTextureElement>> TexturesToRemove;
		//for(int32 TextureIndex = 0; TextureIndex < ExportedScene.GetDatasmithScene()->GetTexturesCount(); ++TextureIndex )
		//{
		//	TSharedPtr<IDatasmithTextureElement> TextureElement = ExportedScene.GetDatasmithScene()->GetTexture(TextureIndex);
		//	FString Name = TextureElement->GetName();
		//	if (TexturesAdded.Contains(Name))
		//	{
		//		TexturesToRemove.Add(TexturesAdded[Name]);
		//		TexturesAdded[Name] = TextureElement;
		//	}
		//	else
		//	{
		//		TexturesAdded.Add(Name, TextureElement);
		//	}
		//}

		//for(TSharedPtr<IDatasmithTextureElement> Texture: TexturesToRemove)
		//{
		//	ExportedScene.GetDatasmithScene()->RemoveTexture(Texture);
		//}

		LogDebug("Scene update: done");

		return bChangeEncountered;
	}

	void ExportAnimations()
	{
		FDatasmithConverter Converter;
		// Use the same name for the unique level sequence as the scene name
		TSharedRef<IDatasmithLevelSequenceElement> LevelSequence = FDatasmithSceneFactory::CreateLevelSequence(ExportedScene.GetDatasmithScene()->GetName());
		LevelSequence->SetFrameRate(GetFrameRate());

		for (TPair<FNodeKey, FNodeTrackerHandle> NodeKeyAndNodeTracker: NodeTrackers)
		{
			FNodeTracker* NodeTracker = NodeKeyAndNodeTracker.Value.GetNodeTracker();
			
			if (NodeTracker->DatasmithActorElement)
			{
				
				if (Lights.Contains(NodeTracker))
				{
					const TSharedPtr<IDatasmithLightActorElement> LightElement = StaticCastSharedPtr< IDatasmithLightActorElement >(NodeTracker->DatasmithActorElement);
					const FMaxLightCoordinateConversionParams LightParams(NodeTracker->Node,
						LightElement->IsA(EDatasmithElementType::AreaLight) ? StaticCastSharedPtr<IDatasmithAreaLightElement>(LightElement)->GetLightShape() : EDatasmithLightShape::None);
					FDatasmithMaxSceneExporter::ExportAnimation(LevelSequence, NodeTracker->Node, NodeTracker->DatasmithActorElement->GetName(), Converter.UnitToCentimeter, LightParams);
				}
				else
				{
					FDatasmithMaxSceneExporter::ExportAnimation(LevelSequence, NodeTracker->Node, NodeTracker->DatasmithActorElement->GetName(), Converter.UnitToCentimeter);
				}
			}
		}
		if (LevelSequence->GetAnimationsCount() > 0)
		{
			ExportedScene.GetDatasmithScene()->AddLevelSequence(LevelSequence);
		}
	}

	FORCENOINLINE
	FNodeTrackerHandle& AddNode(FNodeKey NodeKey, INode* Node)
	{
		LogDebugNode(TEXT("AddNode"), Node);
		FNodeTrackerHandle& NodeTracker = NodeTrackers.Emplace(NodeKey, FNodeTrackerHandle(NodeKey, Node));
		
		NodeTrackersNames.FindOrAdd(NodeTracker.GetNodeTracker()->Name).Add(NodeTracker.GetNodeTracker());
		InvalidatedNodeTrackers.Add(NodeTracker.GetNodeTracker());

		return NodeTracker;
	}

	virtual void RemoveMaterial(const TSharedPtr<IDatasmithBaseMaterialElement>& DatasmithMaterial) override
	{
		ExportedScene.DatasmithSceneRef->RemoveMaterial(DatasmithMaterial);		
	}

	void InvalidateNode(FNodeTracker& NodeTracker)
	{
		ensure(!bUpdateInProgress);

		NodeTracker.Invalidate();
		InvalidatedNodeTrackers.Add(&NodeTracker);

		// Invalidate whole sub-hierarchy of nodes that were previously recorded as children(this might have changed)
		for (FNodeTracker* Child : NodeTracker.Children)
		{
			InvalidateNode(*Child);
		}

		// Invalidate whole sub-hierarchy of nodes are now children.
		// E.g. a node could have been hidden so its children were attached to grandparent(parent of hidden node)
		// Need to invalidate those to reattach
		int32 ChildNum = NodeTracker.Node->NumberOfChildren();
		for (int32 ChildIndex = 0; ChildIndex < ChildNum; ++ChildIndex)
		{
			InvalidateNode(NodeEventNamespace::GetKeyByNode(NodeTracker.Node->GetChildNode(ChildIndex)));
		}

		NodeTracker.Children.Reset();
	}

	// todo: make fine invalidates - full only something like geometry change, but finer for transform, name change and more
	FNodeTracker* InvalidateNode(FNodeKey NodeKey)
	{
		if (FNodeTrackerHandle* NodeTrackerHandle = NodeTrackers.Find(NodeKey))
		{
			FNodeTracker* NodeTracker = NodeTrackerHandle->GetNodeTracker();
			if (NodeEventNamespace::GetNodeByKey(NodeKey))
			{
				InvalidateNode(*NodeTracker);
				return NodeTracker;
			}
			else
			{
				// Sometimes note update received without node Delete event
				// Test case: create container, add node to it. Close it, open it, close again, then sync
				InvalidatedNodeTrackers.Add(NodeTracker);
				NodeTracker->bDeleted = true;
			}
		}
		return nullptr;
	}

	bool IsNodeInvalidated(const FNodeTrackerHandle& NodeTracker)
	{
		return NodeTracker.GetNodeTracker()->IsInvalidated();
	}


	void ClearNodeFromDatasmithScene(FNodeTracker& NodeTracker)
	{
		// remove from hierarchy
		if (NodeTracker.DatasmithActorElement)
		{
			TSharedPtr<IDatasmithMeshActorElement> DatasmithMeshActor = NodeTracker.DatasmithMeshActor;

			// Remove mesh actor before removing its parent Actor in case there a separate MeshActor
			if (NodeTracker.DatasmithMeshActor)
			{
				// if (NodeTracker.DatasmithMeshActor != NodeTracker.DatasmithActorElement)
				{
					NodeTracker.DatasmithActorElement->RemoveChild(NodeTracker.DatasmithMeshActor);
				}
				NodeTracker.DatasmithMeshActor.Reset();
				// todo: consider pool of MeshActors
			}

			if (TSharedPtr<IDatasmithActorElement> ParentActor = NodeTracker.DatasmithActorElement->GetParentActor())
			{
				ParentActor->RemoveChild(NodeTracker.DatasmithActorElement);
			}
			else
			{
				// Detach all children(so they won't be reattached automatically to root when actor is detached from parent below)
				// Children reattachment will happen later in Update
				int32 ChildCount = NodeTracker.DatasmithActorElement->GetChildrenCount();
				// Remove last child each time to optimize array elements relocation
				for(int32 ChildIndex = ChildCount-1; ChildIndex >= 0; --ChildIndex)
				{
					NodeTracker.DatasmithActorElement->RemoveChild(NodeTracker.DatasmithActorElement->GetChild(ChildIndex));
				}
				ExportedScene.DatasmithSceneRef->RemoveActor(NodeTracker.DatasmithActorElement, EDatasmithActorRemovalRule::RemoveChildren);
			}
			NodeTracker.DatasmithActorElement.Reset();
			NodeTracker.Children.Reset();
		}
	}

	// Called when mesh element is not needed anymore and should be removed from the scene
	virtual void ReleaseMeshElement(TSharedPtr<IDatasmithMeshElement> Mesh) override
	{
		ExportedScene.GetDatasmithScene()->RemoveMesh(Mesh);
	}

	void RemoveFromConverted(FNodeTracker& NodeTracker)
	{
		// todo: record previous converter Node type to speed up cleanup. Or just add 'unconverted' flag to speed up this for nodes that weren't converted yet

		if (NodeTracker.Layer)
		{
			if (TSet<FNodeTracker*>* NodeTrackerPtr = NodesPerLayer.Find(NodeTracker.Layer))
			{
				NodeTrackerPtr->Remove(&NodeTracker);
			}
			NodeTracker.Layer = nullptr;
		}

		TSharedPtr<IDatasmithMetaDataElement> DatasmithMetadata;
		if (NodeDatasmithMetadata.RemoveAndCopyValue(&NodeTracker, DatasmithMetadata))
		{
			ExportedScene.GetDatasmithScene()->RemoveMetaData(DatasmithMetadata);
		}

		Helpers.Remove(&NodeTracker);
		Cameras.Remove(&NodeTracker);
		Lights.Remove(&NodeTracker);
		
		{
			// remove static meshes used by the RailClone
			TUniquePtr<FRailClonesConverted> RailClonesConverted;
			if (RailClones.RemoveAndCopyValue(&NodeTracker, RailClonesConverted))
			{
				for (TSharedPtr<IDatasmithMeshElement> Mesh : RailClonesConverted->Meshes)
				{
					ReleaseMeshElement(Mesh);
				}
			}
		}

		if (NodeTracker.IsInstance())
		{
			if (TUniquePtr<FInstances>* InstancesPtr = InstancesForAnimHandle.Find(NodeTracker.InstanceHandle))
			{
				FInstances& Instances = **InstancesPtr;
				Instances.NodeTrackers.Remove(&NodeTracker);
				if (!Instances.NodeTrackers.Num())
				{
					ReleaseMeshElement(Instances.DatasmithMeshElement);
					InstancesForAnimHandle.Remove(NodeTracker.InstanceHandle);
					InvalidatedInstances.Remove(&Instances);
				}
				else
				{
					InvalidateInstances(Instances); // Invalidate instances that had a node removed - need to rebuild for various reasons(mesh might have been built from removed node, material assignment needds rebuild))
				}
			}
		}

		ClearNodeFromDatasmithScene(NodeTracker);
	}

	void UpdateCollisionStatus(FNodeTracker* NodeTracker, TSet<FNodeTracker*>& NodesWithChangedCollisionStatus)
	{
		// Check if collision assigned for node changed
		{
			TOptional<FDatasmithMaxStaticMeshAttributes> DatasmithAttributes = FDatasmithMaxStaticMeshAttributes::ExtractStaticMeshAttributes(NodeTracker->Node);

			bool bOutFromDatasmithAttributes;
			INode* CollisionNode = FDatasmithMaxMeshExporter::GetCollisionNode(NodeTracker->Node, DatasmithAttributes ? &DatasmithAttributes.GetValue() : nullptr, bOutFromDatasmithAttributes);

			FNodeTracker* CollisionNodeTracker = nullptr;
			FNodeKey CollisionNodeKey = NodeEventNamespace::GetKeyByNode(CollisionNode);
			if (FNodeTrackerHandle* NodeTrackerHandle = NodeTrackers.Find(CollisionNodeKey)) // This node should be tracked
			{
				CollisionNodeTracker = NodeTrackerHandle->GetNodeTracker();
			}

			if (NodeTracker->Collision != CollisionNodeTracker)
			{
				// Update usage counters for collision nodes

				// Remove previous
				if (TSet<FNodeTracker*>* CollisionUsersPtr = CollisionNodes.Find(NodeTracker->Collision))
				{
					TSet<FNodeTracker*>& CollisionUsers = *CollisionUsersPtr;
					CollisionUsers.Remove(NodeTracker);

					if (CollisionUsers.IsEmpty())
					{
						CollisionNodes.Remove(NodeTracker->Collision);
						NodesWithChangedCollisionStatus.Add(NodeTracker->Collision);
					}
				}

				// Add new
				if (CollisionNodeTracker)
				{
					if (TSet<FNodeTracker*>* CollisionUsersPtr = CollisionNodes.Find(CollisionNodeTracker))
					{
						TSet<FNodeTracker*>& CollisionUsers = *CollisionUsersPtr;
						CollisionUsers.Add(NodeTracker);
					}
					else
					{
						TSet<FNodeTracker*>& CollisionUsers = CollisionNodes.Add(CollisionNodeTracker);
						CollisionUsers.Add(NodeTracker);
						NodesWithChangedCollisionStatus.Add(CollisionNodeTracker);
					}
				}
				NodeTracker->Collision = CollisionNodeTracker;
			}
		}

		// Check if node changed its being assigned as collision
		{
			if (FDatasmithMaxSceneParser::HasCollisionName(NodeTracker->Node))
			{
				CollisionNodes.Add(NodeTracker); // Always view node with 'collision' name as a collision node(i.e. no render)

				//Check named collision assignment(e.g. 'UCP_<other nothe name>')
				// Split collision prefix and find node that might use this node as collision mesh
				FString NodeName = NodeTracker->Node->GetName();
				FString LeftString, RightString;
				NodeName.Split(TEXT("_"), &LeftString, &RightString);

				if (TSet<FNodeTracker*>* CollisionUserNodeTrackersPtr = NodeTrackersNames.Find(RightString))
				{
					for (FNodeTracker* CollisionUserNodeTracker: *CollisionUserNodeTrackersPtr)
					{
						if (CollisionUserNodeTracker->Collision != NodeTracker)
						{
							NodesWithChangedCollisionStatus.Add(CollisionUserNodeTracker); // Invalidate each node that has collision changed
						}
					}
				}
			}
			else
			{
				// Remove from registered collision nodes if there's not other users(i.e. using Datasmith attributes reference)
				if (TSet<FNodeTracker*>* CollisionUsersPtr = CollisionNodes.Find(NodeTracker))
				{
					if (CollisionUsersPtr->IsEmpty())
					{
						CollisionNodes.Remove(NodeTracker);
					}
				}
			}
		}
	}

	void RemoveNodeTracker(FNodeTracker& NodeTracker)
	{
		InvalidatedNodeTrackers.Remove(&NodeTracker);

		RemoveFromConverted(NodeTracker);

		if (TSet<FNodeTracker*>* NodeTrackersPtr = NodeTrackersNames.Find(NodeTracker.Name))
		{
			NodeTrackersPtr->Remove(&NodeTracker);
		}

		if (TSet<FNodeTracker*>* CollisionUsersPtr = CollisionNodes.Find(NodeTracker.Collision))
		{
			TSet<FNodeTracker*>& CollisionUsers = *CollisionUsersPtr;
			CollisionUsers.Remove(&NodeTracker);

			if (CollisionUsers.IsEmpty())
			{
				CollisionNodes.Remove(NodeTracker.Collision);
			}
		}

		NodeTrackers.Remove(NodeTracker.NodeKey);
	}

	void UpdateNode(FNodeTracker& NodeTracker)
	{

		// Forget anything that this node was before update: place in datasmith hierarchy, datasmith objects, instances connection. Updating may change anything 
		RemoveFromConverted(NodeTracker);
		ConvertNodeObject(NodeTracker);
	}

	void ConvertNodeObject(FNodeTracker& NodeTracker)
	{
		// Update layer connection
		ILayer* Layer = (ILayer*)NodeTracker.Node->GetReference(NODE_LAYER_REF);
		if (Layer)
		{
			if (TUniquePtr<FLayerTracker>* LayerPtr = LayersForAnimHandle.Find(Animatable::GetHandleByAnim(Layer)))
			{
				FLayerTracker* LayerTracker =  LayerPtr->Get();
				NodeTracker.Layer = LayerTracker;
				NodesPerLayer.FindOrAdd(LayerTracker).Add(&NodeTracker);
			}
		}

		if (CollisionNodes.Contains(&NodeTracker))
		{
			return;
		}

		if (NodeTracker.Node->IsNodeHidden(TRUE) || !NodeTracker.Node->Renderable())
		{
			return;
		}

		if (Options.bSelectedOnly && !NodeTracker.Node->Selected())
		{
			return;
		}

		ObjectState ObjState = NodeTracker.Node->EvalWorldState(0);
		Object* Obj = ObjState.obj;

		if (!Obj)
		{
			return;
		}

		switch (Obj->SuperClassID())
		{
		case HELPER_CLASS_ID:
			ConvertHelper(NodeTracker, Obj);
			break;
		case CAMERA_CLASS_ID:
			ConvertCamera(NodeTracker, Obj);
			break;
		case LIGHT_CLASS_ID:
		{
			ConvertLight(NodeTracker, Obj);
			break;
		}
		case SHAPE_CLASS_ID:
		case GEOMOBJECT_CLASS_ID:
		{
			Class_ID ClassID = ObjState.obj->ClassID();
			if (ClassID.PartA() == TARGET_CLASS_ID) // Convert camera target as regular actor
			{
				ConvertHelper(NodeTracker, Obj);
			}
			else if (ClassID == RAILCLONE_CLASS_ID)
			{
				ConvertRailClone(*this, NodeTracker, Obj);
				break;
			}
			else if (ClassID == ITOOFOREST_CLASS_ID)
			{
				ConvertForest(*this, NodeTracker, Obj);
				break;
			}
			else
			{
				if (FDatasmithMaxSceneParser::HasCollisionName(NodeTracker.Node))
				{
					ConvertNamedCollisionNode(NodeTracker);
				}
				else
				{
					ConvertGeomObj(NodeTracker, Obj);
				}
			}
			break;
		}
		// todo: other object types besides geometry
		default:;
		}
	}

	void InvalidateInstances(FInstances& Instances)
	{
		InvalidatedInstances.Add(&Instances);
	}

	void UpdateInstances(FInstances& Instances)
	{
		 if (!Instances.NodeTrackers.IsEmpty())
		 {
			 bool bGeometryUpdated = false; // Use first node to extract information from evaluated object(e.g. GetRenderMesh needs it)

			 bool bMaterialsAssignToStaticMesh = true; // assign materials to static mesh for the first instance(others will use override on mesh actors)
			 for(FNodeTracker* NodeTrackerPtr: Instances.NodeTrackers)
			 {
				FNodeTracker& NodeTracker = *NodeTrackerPtr;
				ClearNodeFromDatasmithScene(NodeTracker);

				if (!bGeometryUpdated)
				{
					// todo: use single EnumProc instance to enumerate all nodes during update to:
					//    - have single call to BeginEnumeration and EndEnumeration
					//    - track all Begin'd nodes to End them together after all is updated(to prevent duplicated Begin's of referenced objects that might be shared by different ndoes)
					NodesPreparer.PrepareNode(NodeTracker.Node);
					UpdateInstancesGeometry(Instances, NodeTracker);

					bGeometryUpdated = true;
				}

				UpdateGeometryNode(NodeTracker, Instances, bMaterialsAssignToStaticMesh);
				bMaterialsAssignToStaticMesh = false;

			 	// Mark node as updated as soon as it is - in order for next nodes to be able to use its DatasmithActor
				NodeTracker.bInvalidated = false;
			 }
		 }
	}

	void UpdateNodeMetadata(FNodeTracker& NodeTracker)
	{
		TSharedPtr<IDatasmithMetaDataElement> MetadataElement = FDatasmithMaxSceneExporter::ParseUserProperties(NodeTracker.Node, NodeTracker.DatasmithActorElement.ToSharedRef(), ExportedScene.GetDatasmithScene());
		NodeDatasmithMetadata.Add(&NodeTracker, MetadataElement);
	}

	// Find first ansestor node which has DatasmithActor created for it
	FNodeTracker* GetParentNodeTracker(FNodeTracker& NodeTracker)
	{
		INode* XRefParent = NodeTracker.GetXRefParent();
		FNodeKey ParentNodeKey = NodeEventNamespace::GetKeyByNode(XRefParent ? XRefParent : NodeTracker.Node->GetParentNode());
		FNodeTrackerHandle* ParentNodeTrackerHandle = NodeTrackers.Find(ParentNodeKey);
		if (!ParentNodeTrackerHandle)
		{
			return nullptr; // Max node not known, assume it doesn't exist
		}
		
		return ParentNodeTrackerHandle->GetNodeTracker();
	}

	// Not all nodes result in creation of DatasmithActor for them(e.g. skipped as invisible), find first ascestor that has it
	FNodeTracker* GetAncestorNodeTrackerWithDatasmithActor(FNodeTracker& InNodeTracker)
	{
		FNodeTracker* NodeTracker = &InNodeTracker;
		while (FNodeTracker* ParentNodeTracker = GetParentNodeTracker(*NodeTracker))
		{
			if (ParentNodeTracker->DatasmithActorElement)
			{
				return ParentNodeTracker;
			}
			NodeTracker = ParentNodeTracker;
		}
		return nullptr;
	}

	void AttachNodeToDatasmithScene(FNodeTracker& NodeTracker)
	{
		if (!NodeTracker.DatasmithActorElement)
		{
			return;
		}

		if (FNodeTracker* ParentNodeTracker = GetAncestorNodeTrackerWithDatasmithActor(NodeTracker))
		{
			ParentNodeTracker->DatasmithActorElement->AddChild(NodeTracker.DatasmithActorElement, EDatasmithActorAttachmentRule::KeepWorldTransform);
			ParentNodeTracker->Children.Add(&NodeTracker);
		}
		else
		{
			// If there's no ancestor node with DatasmithActor assume node it at root
			// (node's parent might be node that was skipped - e.g. it was hidden in Max or not selected when exporting only selected objects)
			ExportedScene.GetDatasmithScene()->AddActor(NodeTracker.DatasmithActorElement);
		}
	}

	void GetNodeObjectTransform(FNodeTracker& NodeTracker, FDatasmithConverter Converter, FTransform& ObjectTransform)
	{
		FVector Translation, Scale;
		FQuat Rotation;

		const FMaxLightCoordinateConversionParams LightParams = FMaxLightCoordinateConversionParams(NodeTracker.Node);
		// todo: do we really need to call GetObjectTM if there's no WSM attached? Maybe just call GetObjTMAfterWSM always?
		if (NodeTracker.Node->GetWSMDerivedObject() != nullptr)
		{
			FDatasmithMaxSceneExporter::MaxToUnrealCoordinates(NodeTracker.Node->GetObjTMAfterWSM(GetCOREInterface()->GetTime()), Translation, Rotation, Scale, Converter.UnitToCentimeter, LightParams);
		}
		else
		{
			FDatasmithMaxSceneExporter::MaxToUnrealCoordinates(NodeTracker.Node->GetObjectTM(GetCOREInterface()->GetTime()), Translation, Rotation, Scale, Converter.UnitToCentimeter, LightParams);
		}
		Rotation.Normalize();
		ObjectTransform = FTransform(Rotation, Translation, Scale);
	}

	void RegisterNodeForMaterial(FNodeTracker& NodeTracker, Mtl* Material)
	{
		if (!NodeTracker.MaterialTracker || NodeTracker.MaterialTracker->Material != Material)
		{
			// Release old material
			if (NodeTracker.MaterialTracker)
			{
				// Release material assignment
				MaterialsAssignedToNodes[NodeTracker.MaterialTracker].Remove(&NodeTracker);

				// Clean tracker if it's not used aby any node
				if (MaterialsAssignedToNodes[NodeTracker.MaterialTracker].IsEmpty())
				{
					MaterialsCollectionTracker.ReleaseMaterial(*NodeTracker.MaterialTracker);
				}
			}

			NodeTracker.MaterialTracker = MaterialsCollectionTracker.AddMaterial(Material);
			MaterialsAssignedToNodes.FindOrAdd(NodeTracker.MaterialTracker).Add(&NodeTracker);
		}
	}

	void UnregisterNodeForMaterial(FNodeTracker& NodeTracker, Mtl* Material)
	{
		if (NodeTracker.MaterialTracker)
		{
			MaterialsAssignedToNodes[NodeTracker.MaterialTracker].Remove(&NodeTracker);
			if (MaterialsAssignedToNodes[NodeTracker.MaterialTracker].IsEmpty())
			{
				MaterialsCollectionTracker.ReleaseMaterial(*NodeTracker.MaterialTracker);
			}
		}
	}

	void UpdateGeometryNode(FNodeTracker& NodeTracker, FInstances& Instances, bool bMaterialsAssignToStaticMesh)
	{
		FDatasmithConverter Converter;

		FTransform ObjectTransform;
		GetNodeObjectTransform(NodeTracker, Converter, ObjectTransform);

		FTransform Pivot = FDatasmithMaxSceneExporter::GetPivotTransform(NodeTracker.Node, Converter.UnitToCentimeter);
		FTransform NodeTransform = Pivot.Inverse() * ObjectTransform; // Remove pivot from the node actor transform

		const TSharedPtr<IDatasmithMeshElement>& DatasmithMeshElement = Instances.DatasmithMeshElement;
		bool bHasMesh = DatasmithMeshElement.IsValid();
		bool bNeedPivotComponent = !Pivot.Equals(FTransform::Identity);

		TSharedPtr<IDatasmithActorElement> DatasmithActorElement;
		TSharedPtr<IDatasmithMeshActorElement> DatasmithMeshActor;

		FString UniqueName = FString::FromInt(NodeTracker.Node->GetHandle());
		FString Label = FString((const TCHAR*)NodeTracker.Node->GetName());

		// Create and setup mesh actor if there's a mesh 
		if (bHasMesh)
		{
			FString MeshActorName = UniqueName;
			if (bNeedPivotComponent)
			{
				MeshActorName += TEXT("_Pivot");
			}

			FString MeshActorLabel = FString((const TCHAR*)NodeTracker.Node->GetName());
			DatasmithMeshActor = FDatasmithSceneFactory::CreateMeshActor(*MeshActorName);
			DatasmithMeshActor->SetLabel(*Label);

			TOptional<FDatasmithMaxStaticMeshAttributes> DatasmithAttributes = FDatasmithMaxStaticMeshAttributes::ExtractStaticMeshAttributes(NodeTracker.Node);
			if (DatasmithAttributes &&  (DatasmithAttributes->GetExportMode() == EStaticMeshExportMode::BoundingBox))
			{
				DatasmithMeshActor->AddTag(TEXT("Datasmith.Attributes.Geometry: BoundingBox"));
			}

			DatasmithMeshActor->SetStaticMeshPathName(DatasmithMeshElement->GetName());
		}

		if (bNeedPivotComponent || !bHasMesh)
		{
			DatasmithActorElement = FDatasmithSceneFactory::CreateActor(*UniqueName);
			DatasmithActorElement->SetLabel(*Label);
		}
		else
		{
			DatasmithActorElement = DatasmithMeshActor;
		}

		DatasmithActorElement->SetTranslation(NodeTransform.GetTranslation());
		DatasmithActorElement->SetScale(NodeTransform.GetScale3D());
		DatasmithActorElement->SetRotation(NodeTransform.GetRotation());

		if (bNeedPivotComponent && bHasMesh)
		{
			DatasmithMeshActor->SetTranslation(Pivot.GetTranslation());
			DatasmithMeshActor->SetRotation(Pivot.GetRotation());
			DatasmithMeshActor->SetScale(Pivot.GetScale3D());
			DatasmithMeshActor->SetIsAComponent( true );

			DatasmithActorElement->AddChild(DatasmithMeshActor, EDatasmithActorAttachmentRule::KeepRelativeTransform);
		}

		NodeTracker.DatasmithActorElement = DatasmithActorElement;
		NodeTracker.DatasmithMeshActor = DatasmithMeshActor;

		UpdateNodeMetadata(NodeTracker);
		TagsConverter.ConvertNodeTags(NodeTracker);
		if (NodeTracker.Layer)
		{
			NodeTracker.DatasmithActorElement->SetLayer(*NodeTracker.Layer->Name);
		}

		// Apply material 
		if (DatasmithMeshElement)
		{
			if (Mtl* Material = NodeTracker.Node->GetMtl())
			{
				RegisterNodeForMaterial(NodeTracker, Material);

				// Assign materials
				if (bMaterialsAssignToStaticMesh)
				{
					AssignMeshMaterials(Instances.DatasmithMeshElement, Material, Instances.SupportedChannels);
					Instances.Material = Material;
				}
				else // Assign material overrides to meshactor
				{
					if (Instances.Material != Material)
					{
						TSharedRef<IDatasmithMeshActorElement> DatasmithMeshActorRef = NodeTracker.DatasmithMeshActor.ToSharedRef();
						FDatasmithMaxSceneExporter::ParseMaterialForMeshActor(Material, DatasmithMeshActorRef, Instances.SupportedChannels, NodeTracker.DatasmithMeshActor->GetTranslation());
					}
				}
			}
			else
			{
				// Release old material
				UnregisterNodeForMaterial(NodeTracker, Material);
				NodeTracker.MaterialTracker = nullptr;
				NodeTracker.DatasmithMeshActor->ResetMaterialOverrides();
			}

			// todo: test mesh becoming empty/invalid/not created - what happens?
			// todo: test multimaterial changes
			// todo: check other material permutations
		}
	}

	virtual void AddMeshElement(TSharedPtr<IDatasmithMeshElement>& DatasmithMeshElement, FDatasmithMesh& DatasmithMesh, FDatasmithMesh* CollisionMesh) override
	{
		ExportedScene.GetDatasmithScene()->AddMesh(DatasmithMeshElement);

		// todo: parallelize this
		FDatasmithMeshExporter DatasmithMeshExporter;
		if (DatasmithMeshExporter.ExportToUObject(DatasmithMeshElement, ExportedScene.GetSceneExporter().GetAssetsOutputPath(), DatasmithMesh, CollisionMesh, FDatasmithExportOptions::LightmapUV))
		{
			// todo: handle error exporting mesh?
		}
	}

	bool UpdateInstancesGeometry(FInstances& Instances, FNodeTracker& NodeTracker)
	{
		INode* Node = NodeTracker.Node;
		Object* Obj = Instances.EvaluatedObj;

		TSharedPtr<IDatasmithMeshElement>& DatasmithMeshElement = Instances.DatasmithMeshElement;
		TSet<uint16>& SupportedChannels = Instances.SupportedChannels;
		FString MeshName = FString::FromInt(Node->GetHandle());

		FRenderMeshForConversion RenderMesh = GetMeshForGeomObject(Node, Obj);
		FRenderMeshForConversion CollisionMesh = GetMeshForCollision(Node);

		if (RenderMesh.GetMesh())
		{
			if (ConvertMaxMeshToDatasmith(*this, DatasmithMeshElement, Node, *MeshName, RenderMesh, SupportedChannels, CollisionMesh)) // export might produce anything(e.g. if mesh is empty)
			{
				DatasmithMeshElement->SetLabel(Node->GetName());
				return true;
			}
		}

		DatasmithMeshElement.Reset();
		return false;
	}

	virtual void SetupActor(FNodeTracker& NodeTracker) override
	{
		NodeTracker.DatasmithActorElement->SetLabel(NodeTracker.Node->GetName());

		UpdateNodeMetadata(NodeTracker);
		TagsConverter.ConvertNodeTags(NodeTracker);
		if (NodeTracker.Layer)
		{
			NodeTracker.DatasmithActorElement->SetLayer(*NodeTracker.Layer->Name);
		}

		FDatasmithConverter Converter;
		FTransform ObjectTransform;
		GetNodeObjectTransform(NodeTracker, Converter, ObjectTransform);

		FTransform NodeTransform = ObjectTransform;
		TSharedRef<IDatasmithActorElement> DatasmithActorElement = NodeTracker.DatasmithActorElement.ToSharedRef();
		DatasmithActorElement->SetTranslation(NodeTransform.GetTranslation());
		DatasmithActorElement->SetScale(NodeTransform.GetScale3D());
		DatasmithActorElement->SetRotation(NodeTransform.GetRotation());
	}

	bool ConvertHelper(FNodeTracker& NodeTracker, Object* Obj)
	{
		Helpers.Add(&NodeTracker);

		if(!NodeTracker.DatasmithActorElement)
		{
			// note: this is how baseline exporter derives names
			FString UniqueName = FString::FromInt(NodeTracker.Node->GetHandle());
			NodeTracker.DatasmithActorElement = FDatasmithSceneFactory::CreateActor((const TCHAR*)*UniqueName);
		}
		SetupActor(NodeTracker);

		NodeTracker.bInvalidated = false;

		return true;
	}

	bool ConvertCamera(FNodeTracker& NodeTracker, Object* Obj)
	{
		Cameras.Add(&NodeTracker);

		if(!NodeTracker.DatasmithActorElement)
		{
			// note: this is how baseline exporter derives names
			FString UniqueName = FString::FromInt(NodeTracker.Node->GetHandle());
			NodeTracker.DatasmithActorElement = FDatasmithSceneFactory::CreateCameraActor((const TCHAR*)*UniqueName);
		}

		FDatasmithMaxCameraExporter::ExportCamera(*NodeTracker.Node, StaticCastSharedPtr<IDatasmithCameraActorElement>(NodeTracker.DatasmithActorElement).ToSharedRef());

		SetupActor(NodeTracker);

		// Max camera view direction is Z-, Unreal's X+
		// Max camera Up is Y+,  Unrela's Z+
		FQuat Rotation = NodeTracker.DatasmithActorElement->GetRotation();
		Rotation *= FQuat(0.0, 0.707107, 0.0, 0.707107);
		Rotation *= FQuat(0.707107, 0.0, 0.0, 0.707107);
		NodeTracker.DatasmithActorElement->SetRotation(Rotation);

		NodeTracker.bInvalidated = false;

		return true;
	}

	bool ConvertLight(FNodeTracker& NodeTracker, Object* Obj)
	{
		if (EMaxLightClass::Unknown == FDatasmithMaxSceneParser::GetLightClass(NodeTracker.Node))
		{
			return false;
		}

		Lights.Add(&NodeTracker);

		TSharedPtr<IDatasmithLightActorElement> LightElement;
		if(!NodeTracker.DatasmithActorElement)
		{
			// note: this is how baseline exporter derives names
			FString UniqueName = FString::FromInt(NodeTracker.Node->GetHandle());

			LightElement = FDatasmithMaxSceneExporter::CreateLightElementForNode(NodeTracker.Node, *UniqueName);

			if (!LightElement)
			{
				if (FDatasmithMaxSceneParser::GetLightClass(NodeTracker.Node) == EMaxLightClass::SkyEquivalent)
				{
					ExportedScene.DatasmithSceneRef->SetUsePhysicalSky(true);
				}
				else
				{
					LogWarningDialog(TEXT("Unsupported light: ") + DatasmithMaxLogger::Get().GetLightDescription(NodeTracker.Node));
				}
				return false;
			}
			else
			{
				if ( !FDatasmithMaxSceneExporter::ParseLight(NodeTracker.Node, LightElement.ToSharedRef(), ExportedScene.DatasmithSceneRef.ToSharedRef()) )
				{
					return false;
				}
			}

			NodeTracker.DatasmithActorElement = LightElement;
		}
		SetupActor(NodeTracker);

		//Cylinder shaped lights don't have the same default orientations, so we recalculate their transform and add the shape information.
		if (LightElement->IsA(EDatasmithElementType::AreaLight) 
			&& StaticCastSharedPtr<IDatasmithAreaLightElement>(LightElement)->GetLightShape() == EDatasmithLightShape::Cylinder)
		{
			FVector Translation, Scale;
			FQuat Rotation;

			const float UnitMultiplier = (float)GetSystemUnitScale(UNITS_CENTIMETERS);
			const FMaxLightCoordinateConversionParams LightParams = FMaxLightCoordinateConversionParams(NodeTracker.Node, EDatasmithLightShape::Cylinder);
			if (NodeTracker.Node->GetWSMDerivedObject() != nullptr)
			{
				FDatasmithMaxSceneExporter::MaxToUnrealCoordinates(NodeTracker.Node->GetObjTMAfterWSM(GetCOREInterface()->GetTime()), Translation, Rotation, Scale, UnitMultiplier, LightParams);
			}
			else
			{
				FDatasmithMaxSceneExporter::MaxToUnrealCoordinates(NodeTracker.Node->GetObjectTM(GetCOREInterface()->GetTime()), Translation, Rotation, Scale, UnitMultiplier, LightParams);
			}

			Rotation.Normalize();
			LightElement->SetTranslation(Translation);
			LightElement->SetScale(Scale);
			LightElement->SetRotation(Rotation);
		}


		NodeTracker.bInvalidated = false;

		return true;
	}

	virtual void SetupDatasmithHISMForNode(FNodeTracker& NodeTracker, INode* GeometryNode, const FRenderMeshForConversion& RenderMesh, Mtl* Material, int32 MeshIndex, const TArray<Matrix3>& Transforms) override
	{
		FString MeshName = FString::FromInt(NodeTracker.Node->GetHandle()) + TEXT("_") + FString::FromInt(MeshIndex);

		// note: when export Mesh goes to other place due to parallellizing it's result would be unknown here so MeshIndex handling will change(i.e. increment for any mesh)
				
		TSharedPtr<IDatasmithMeshElement> DatasmithMeshElement;
		TSet<uint16> SupportedChannels;

		if (ConvertMaxMeshToDatasmith(*this, DatasmithMeshElement, GeometryNode, *MeshName, RenderMesh, SupportedChannels))
		{
			TUniquePtr<FRailClonesConverted>& RailClonesConverted = RailClones.FindOrAdd(&NodeTracker);
			if (!RailClonesConverted)
			{
				RailClonesConverted = MakeUnique<FRailClonesConverted>();
			}
			RailClonesConverted->Meshes.Add(DatasmithMeshElement);

			RegisterNodeForMaterial(NodeTracker, Material);
			AssignMeshMaterials(DatasmithMeshElement, Material, SupportedChannels);

			FString MeshLabel = NodeTracker.Node->GetName() + (TEXT("_") + FString::FromInt(MeshIndex));
			DatasmithMeshElement->SetLabel(*MeshLabel);

			FDatasmithConverter Converter;
				
			// todo: override material
			TSharedPtr< IDatasmithActorElement > InversedHISMActor;
			// todo: ExportHierarchicalInstanceStaticMeshActor CustomMeshNode only used for Material - can be simplified, Material anyway is dealt with outside too
			TSharedRef<IDatasmithActorElement> HismActorElement = FDatasmithMaxSceneExporter::ExportHierarchicalInstanceStaticMeshActor( 
				ExportedScene.GetDatasmithScene(), NodeTracker.Node, GeometryNode, *MeshLabel, SupportedChannels,
				Material, &Transforms, *MeshName, Converter.UnitToCentimeter, EStaticMeshExportMode::Default, InversedHISMActor);
			NodeTracker.DatasmithActorElement->AddChild(HismActorElement, EDatasmithActorAttachmentRule::KeepWorldTransform);
			if (InversedHISMActor)
			{
				NodeTracker.DatasmithActorElement->AddChild(InversedHISMActor, EDatasmithActorAttachmentRule::KeepWorldTransform);
			}
			MeshIndex++;
		}
	}

	bool ConvertGeomObj(FNodeTracker& NodeTracker, Object* Obj)
	{
		// todo: reuse mesh element(make sure to reset all)

		bool bResult = false;
		if (!Obj->IsRenderable()) // Shape's Enable In Render flag(note - different from Node's Renderable flag)
		{
			return bResult;
		}

		// AnimHandle is unique and never reused for new objects 
		// todo: reset instances and nodes when one node of an instance changes??? Check how it should be done actually, dependencies - nodes, object, invalidation place(Update, Event) etc...
		AnimHandle Handle = Animatable::GetHandleByAnim(Obj);

		NodeTracker.InstanceHandle = Handle;

		TUniquePtr<FInstances>& Instances = InstancesForAnimHandle.FindOrAdd(Handle);

		if (!Instances)
		{
			Instances = MakeUnique<FInstances>();
			Instances->EvaluatedObj = Obj;
		}

		// need to invalidate mesh assignment to node that wasn't the first to add to instances(so if instances weren't invalidated - this node needs mesh anyway)
		Instances->NodeTrackers.Add(&NodeTracker);
		InvalidateInstances(*Instances);

		return bResult;
	}

	void ConvertNamedCollisionNode(FNodeTracker& NodeTracker)
	{
		// Split collision prefix and find node that might use this node as collision mesh
		FString NodeName = NodeTracker.Node->GetName();
		FString LeftString, RightString;
		NodeName.Split(TEXT("_"), &LeftString, &RightString);

		INode* CollisionUserNode = GetCOREInterface()->GetINodeByName(*RightString);
		if (!CollisionUserNode)
		{
			return;
		}

		// If some node is using this collision node then invalidate that node's instances
		FNodeKey CollisionUserNodeKey = NodeEventNamespace::GetKeyByNode(CollisionUserNode);
		if (FNodeTrackerHandle* NodeTrackerHandle = NodeTrackers.Find(CollisionUserNodeKey)) // This node should be tracked
		{
			FNodeTracker& CollisionUserNodeTracker = *NodeTrackerHandle->GetNodeTracker();

			if (CollisionUserNodeTracker.IsInstance())
			{
				if (TUniquePtr<FInstances>* InstancesPtr = InstancesForAnimHandle.Find(CollisionUserNodeTracker.InstanceHandle))
				{
					FInstances& Instances = **InstancesPtr;
					InvalidateInstances(Instances);
				}
			}
		}
	}

	/******************* Events *****************************/

	virtual void NodeAdded(INode* Node) override
	{
		// Node sometimes is null. 'Added' NodeEvent might come after node was actually deleted(immediately after creation)
		// e.g.[mxs]: b = box(); delete b 
		// NodeEvents are delayed(not executed in the same stack frame as command that causes them) so they come later. 
		if (!Node)
		{
			return;
		}

		if (NotificationsHandler)
		{
			NotificationsHandler->AddNode(Node);
		}

		ParseNode(Node);
	}

	virtual void NodeXRefMerged(INode* Node) override
	{
		if (!Node)
		{
			return;
		}

		int32 XRefIndex = -1; // Node that has this xref scene attached to(e.g. to place in hierarchy and to transform)
		INode* SceneRootNode = GetCOREInterface()->GetRootNode();
		for (int XRefChild = 0; XRefChild < SceneRootNode->GetXRefFileCount(); ++XRefChild)
		{
			if (Node == SceneRootNode->GetXRefTree(XRefChild))
			{
				XRefIndex = XRefChild;
			}
		}

		FNodeKey NodeKey = NodeEventNamespace::GetKeyByNode(Node);
		InvalidateNode(NodeKey);

		ParseScene(Node, FXRefScene{SceneRootNode, XRefIndex}); // Parse xref hierarchy - it won't add itself! Or will it?
	}

	virtual void NodeDeleted(INode* Node) override
	{
		LogDebugNode(TEXT("NodeDeleted"), Node);
		// todo: check for null
		FNodeKey NodeKey = NodeEventNamespace::GetKeyByNode(Node);

		if (FNodeTrackerHandle* NodeTrackerHandle = NodeTrackers.Find(NodeKey))
		{
			// todo: schedule for delete on Update?
			FNodeTracker* NodeTracker = NodeTrackerHandle->GetNodeTracker();
			InvalidatedNodeTrackers.Add(NodeTracker);
			NodeTracker->bDeleted = true;
		}
	}

	virtual void NodeTransformChanged(FNodeKey NodeKey) override
	{
		// todo: invalidate transform only

		// todo: grouping makes this crash. Need to handle event before?
		InvalidateNode(NodeKey);

		// ControllerOtherEvent sent only for top actors in hierarchy when it's moved
		INode* Node = NodeEventNamespace::GetNodeByKey(NodeKey);
		if (Node)
		{
			int32 ChildNum = Node->NumberOfChildren();
			for (int32 ChildIndex = 0; ChildIndex < ChildNum; ++ChildIndex)
			{
				// todo: pass INode to NodeTransformChanged to remove redundant get
				NodeTransformChanged(NodeEventNamespace::GetKeyByNode(Node->GetChildNode(ChildIndex)));
			}
		}
	}

	virtual void NodeMaterialAssignmentChanged(FNodeKey NodeKey) override
	{
		//todo: handle more precisely
		InvalidateNode(NodeKey);
	}

	virtual void NodeMaterialGraphModified(FNodeKey NodeKey) override
	{
		//identify material tree and update all materials
		//todo: possible to handle this more precisely(only refresh changed materials) - see FMaterialObserver

		if (FNodeTrackerHandle* NodeTracker = NodeTrackers.Find(NodeKey))
		{
			// todo: investigate why NodeEventNamespace::GetNodeByKey may stil return NULL
			// testcase - add XRef Material - this will immediately have this 
			// even though NOTIFY_SCENE_ADDED_NODE was called for node and NOTIFY_SCENE_PRE_DELETED_NODE wasn't!
			INode* Node = NodeEventNamespace::GetNodeByKey(NodeKey);
			if (Node)
			{
				if (Mtl* Material = Node->GetMtl())
				{
					MaterialsCollectionTracker.InvalidateMaterial(Material);
				}
			}
		}

		InvalidateNode(NodeKey); // Invalidate node that has this material assigned. This is needed to trigger rebuild - exported geometry might change(e.g. multimaterial changed to slots will change on static mesh)
	}

	virtual void NodeGeometryChanged(FNodeKey NodeKey) override
	{
		// GeometryChanged is executed to handle:
		// - actual geometry modification(in any way)
		// - change of baseObject

		InvalidateNode(NodeKey);
	}

	virtual void NodeHideChanged(FNodeKey NodeKey) override
	{
		// todo: invalidate visibility only - note to handle this not enought add/remove 
		// actor. make sure to invalidate instances(in case geometry usage changed - like hidden node with multimat), materials

		InvalidateNode(NodeKey);
	}

	virtual void NodePropertiesChanged(FNodeKey NodeKey) override
	{
		// todo: invalidate visibility only - note to handle this not enought add/remove 
		// actor. make sure to invalidate instances(in case geometry usage changed - like hidden node with multimat), materials

		InvalidateNode(NodeKey);
	}

	virtual void NodeLinkChanged(FNodeKey NodeKey) override
	{
		InvalidateNode(NodeKey);
	}
	
	///////////////////////////////////////////////

	const FExportOptions& Options;
	FDatasmith3dsMaxScene& ExportedScene;
	FNotifications* NotificationsHandler;

	bool bSceneParsed = false;
	bool bUpdateInProgress = false;


	TMap<FNodeKey, FNodeTrackerHandle> NodeTrackers; // All scene nodes
	TMap<FString, TSet<FNodeTracker*>> NodeTrackersNames; // Nodes grouped by name
	TSet<FNodeTracker*> InvalidatedNodeTrackers; // Nodes that need to be rebuilt
	TMap<FNodeTracker*, TSharedPtr<IDatasmithMetaDataElement>> NodeDatasmithMetadata; // All scene nodes

	TMap<FNodeTracker*, TSet<FNodeTracker*>> CollisionNodes; // Nodes used as collision meshes for other nodes, counted by each user 

	FMaterialsCollectionTracker MaterialsCollectionTracker;

	TMap<FMaterialTracker*, TSet<FNodeTracker*>> MaterialsAssignedToNodes;

	TMap<AnimHandle, TUniquePtr<FInstances>> InstancesForAnimHandle; // set of instanced nodes for each AnimHandle
	TSet<FNodeTracker*> Helpers;
	TSet<FNodeTracker*> Lights;
	TSet<FNodeTracker*> Cameras;

	TMap<AnimHandle, TUniquePtr<FLayerTracker>> LayersForAnimHandle;
	TMap<FLayerTracker*, TSet<FNodeTracker*>> NodesPerLayer;

	FNodesPreparer NodesPreparer;
	
	struct FRailClonesConverted
	{
		TArray<TSharedPtr<IDatasmithMeshElement>> Meshes; // Meshes created for this railclones object
	};

	TMap<FNodeTracker*, TUniquePtr<FRailClonesConverted>> RailClones;

	TSet<FInstances*> InvalidatedInstances;

	FTagsConverter TagsConverter; // Converts max node information to Datasmith tags
};

class FExporter: public IExporter
{
public:
	FExporter(FExportOptions& InOptions): Options(InOptions), NotificationsHandler(*this), SceneTracker(Options, ExportedScene, &NotificationsHandler)
	{
		ResetSceneTracking();
		InitializeDirectLinkForScene(); // Setup DL connection immediately when plugin loaded
	}

	virtual void Shutdown() override;

	virtual void SetOutputPath(const TCHAR* Path) override
	{
		OutputPath = Path;
		ExportedScene.SetOutputPath(*OutputPath);		
	}

	virtual void SetName(const TCHAR* Name) override
	{
		ExportedScene.SetName(Name);
	}

	virtual void InitializeScene() override
	{
		ExportedScene.SetupScene();
	}

	virtual void ParseScene() override
	{
		SceneTracker.ParseScene();
	}

	virtual void InitializeDirectLinkForScene() override
	{
		if (DirectLinkImpl) 
		{
			return;
		}

		InitializeScene();

		// XXX: PreExport needs to be called before DirectLink instance is constructed - 
		// Reason - it calls initialization of FTaskGraphInterface. Callstack:
		// PreExport:
		//  - FDatasmithExporterManager::Initialize 
		//	-- DatasmithGameThread::InitializeInCurrentThread
		//  --- GEngineLoop.PreInit
		//  ---- PreInitPreStartupScreen
		//  ----- FTaskGraphInterface::Startup
		ExportedScene.PreExport();

		SetOutputPath(GetDirectlinkCacheDirectory());
		FString SceneName = FPaths::GetCleanFilename(GetCOREInterface()->GetCurFileName().data());
		SetName(*SceneName);

		DirectLinkImpl.Reset(new FDatasmithDirectLink);
		DirectLinkImpl->InitializeForScene(ExportedScene.GetDatasmithScene());
	}

	virtual void UpdateDirectLinkScene() override
	{
		if (!DirectLinkImpl) 
		{
			// InitializeDirectLinkForScene wasn't called yet. This rarely happens when Sync is pressed right before event like PostSceneReset(for New All UI command) was handled
			// Very quickly! Unfortunately code needs to wait for PostSceneReset to get proper scene name there(no earlier event signals that name is available)
			InitializeDirectLinkForScene();
		}

		LogDebug(TEXT("UpdateDirectLinkScene"));
		DirectLinkImpl->UpdateScene(ExportedScene.GetDatasmithScene());
		StartSceneChangeTracking(); // Always track scene changes if it's synced with DirectLink
	}

	static VOID AutoSyncTimerProc(HWND, UINT, UINT_PTR TimerIdentifier, DWORD)
	{
		reinterpret_cast<FExporter*>(TimerIdentifier)->UpdateAutoSync(); 
	}

	// Update is user was idle for some time
	void UpdateAutoSync()
	{
		LASTINPUTINFO LastInputInfo;
		LastInputInfo.cbSize = sizeof(LASTINPUTINFO);
		LastInputInfo.dwTime = 0;
		if (GetLastInputInfo(&LastInputInfo))
		{
			DWORD CurrentTime = GetTickCount();
			int32 IdlePeriod = GetTickCount() - LastInputInfo.dwTime;
			LogDebug(FString::Printf(TEXT("CurrentTime: %ld, Idle time: %ld, IdlePeriod: %ld"), CurrentTime, LastInputInfo.dwTime, IdlePeriod));

			if (IdlePeriod > FMath::RoundToInt(AutoSyncIdleDelaySeconds*1000))
			{
				// Don't create progress bar for autosync - it steals focus, closes listener and what else
				// todo: consider creating progress when a big change in scene is detected, e.g. number of nodes?
				if (UpdateScene(true)) // Don't sent redundant update if scene change wasn't detected
				{
					UpdateDirectLinkScene();
				}
			}
		}
	}

	virtual bool IsAutoSyncEnabled() override
	{
		return bAutoSyncEnabled;
	}

	virtual bool ToggleAutoSync() override
	{
		if (bAutoSyncEnabled)
		{
			KillTimer(GetCOREInterface()->GetMAXHWnd(), reinterpret_cast<UINT_PTR>(this));
		}
		else
		{
			// Perform full Sync when AutoSync is first enabled
			UpdateScene(false);
			UpdateDirectLinkScene();

			const uint32 AutoSyncCheckIntervalMs = FMath::RoundToInt(AutoSyncDelaySeconds*1000);
			SetTimer(GetCOREInterface()->GetMAXHWnd(), reinterpret_cast<UINT_PTR>(this), AutoSyncCheckIntervalMs, AutoSyncTimerProc);
		}
		bAutoSyncEnabled = !bAutoSyncEnabled;

		LogDebug(bAutoSyncEnabled ? TEXT("AutoSync ON") : TEXT("AutoSync OFF"));
		return bAutoSyncEnabled;
	}

	virtual void SetAutoSyncDelay(float Seconds) override
	{
		AutoSyncDelaySeconds = Seconds;
	}

	virtual void SetAutoSyncIdleDelay(float Seconds) override
	{
		AutoSyncIdleDelaySeconds = Seconds;
	}

	// Install change notification systems
	virtual void StartSceneChangeTracking() override
	{
		NotificationsHandler.StartSceneChangeTracking();
	}

	virtual bool UpdateScene(bool bQuiet) override
	{
		return SceneTracker.Update(bQuiet, false);
	}

	virtual void ResetSceneTracking() override
	{
		NotificationsHandler.StopSceneChangeTracking();
		if (IsAutoSyncEnabled())
		{
			ToggleAutoSync();
		}

		ExportedScene.ResetScene();

		SceneTracker.Reset();

		DirectLinkImpl.Reset();
	}

	virtual ISceneTracker& GetSceneTracker() override
	{
		return SceneTracker;
	}

	FExportOptions Options;

	FDatasmith3dsMaxScene ExportedScene;
	TUniquePtr<FDatasmithDirectLink> DirectLinkImpl;
	FString OutputPath;

	FNotifications NotificationsHandler;
	FSceneTracker SceneTracker;

	bool bAutoSyncEnabled = false;
	float AutoSyncDelaySeconds = 0.5f; // AutoSync is attempted periodically using this interval
	float AutoSyncIdleDelaySeconds = 0.5f; // Time period user should be idle to run AutoSync

};

FPersistentExportOptions PersistentExportOptions;

TUniquePtr<IExporter> Exporter;

bool CreateExporter(bool bEnableUI, const TCHAR* EnginePath)
{
	FDatasmithExporterManager::FInitOptions Options;
	Options.bEnableMessaging = true; // DirectLink requires the Messaging service.
	Options.bSuppressLogs = false;   // Log are useful, don't suppress them
	Options.bUseDatasmithExporterUI = bEnableUI;
	Options.RemoteEngineDirPath = EnginePath;

	if (!FDatasmithExporterManager::Initialize(Options))
	{
		return false;
	}

	if (int32 ErrorCode = FDatasmithDirectLink::ValidateCommunicationSetup())
	{
		return false;
	}

	static FExportOptions ExporterOptions; // Default options
	Exporter = MakeUnique<FExporter>(ExporterOptions);

	PersistentExportOptions.Load(); // Access GConfig only after FDatasmithExporterManager::Initialize finishes, which ensures that Unreal game thread was initialized(GConfig is created there)

	return true;
}

void ShutdownExporter()
{
	ShutdownScripts();
	Exporter.Reset();
	FDatasmithDirectLink::Shutdown();
	FDatasmithExporterManager::Shutdown();
}

IExporter* GetExporter()
{
	return Exporter.Get();	
}

IPersistentExportOptions& GetPersistentExportOptions()
{
	return PersistentExportOptions;
}

void FExporter::Shutdown() 
{
	Exporter.Reset();
	FDatasmithDirectLink::Shutdown();
	FDatasmithExporterManager::Shutdown();
}

bool Export(const TCHAR* Name, const TCHAR* OutputPath, bool bQuiet)
{
	FDatasmith3dsMaxScene ExportedScene;
	ExportedScene.SetupScene();
	ExportedScene.SetName(Name);
	ExportedScene.SetOutputPath(OutputPath);

	FSceneTracker SceneTracker(PersistentExportOptions.Options, ExportedScene, nullptr);
	SceneTracker.Update(bQuiet, true);

	if (PersistentExportOptions.Options.bAnimatedTransforms)
	{
		SceneTracker.ExportAnimations();
	}

	ExportedScene.GetSceneExporter().Export(ExportedScene.GetDatasmithScene(), false);

	return true;
}

bool OpenDirectLinkUI()
{
	if (IDatasmithExporterUIModule* Module = IDatasmithExporterUIModule::Get())
	{
		if (IDirectLinkUI* UI = Module->GetDirectLinkExporterUI())
		{
			UI->OpenDirectLinkStreamWindow();
			return true;
		}
	}
	return false;
}

const TCHAR* GetDirectlinkCacheDirectory()
{
	if (IDatasmithExporterUIModule* Module = IDatasmithExporterUIModule::Get())
	{
		if (IDirectLinkUI* UI = Module->GetDirectLinkExporterUI())
		{
			return UI->GetDirectLinkCacheDirectory();
		}
	}
	return nullptr;
}

FDatasmithConverter::FDatasmithConverter(): UnitToCentimeter(FMath::Abs(GetSystemUnitScale(UNITS_CENTIMETERS)))
{
}

}

#include "Windows/HideWindowsPlatformTypes.h"

#endif // NEW_DIRECTLINK_PLUGIN
