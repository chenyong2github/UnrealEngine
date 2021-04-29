// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpCommon.h"
#include "DatasmithSketchUpUtils.h"

#include "DatasmithSketchUpCamera.h"
#include "DatasmithSketchUpComponent.h"
#include "DatasmithSketchUpMaterial.h"
#include "DatasmithSketchUpMesh.h"
#include "DatasmithSketchUpMetadata.h"
#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpSummary.h"

#include "DatasmithSketchUpExportContext.h"

#include "DatasmithDirectLink.h"

#include "IDatasmithExporterUIModule.h"
#include "IDirectLinkUI.h"

#include "DatasmithSketchUpSDKBegins.h"
#include <SketchUpAPI/sketchup.h>
// SketchUp prior to 2020.2 doesn't have api to convert SU entities between Ruby and C 
#ifndef SKP_SDK_2019
#include <SketchUpAPI/application/ruby_api.h>
#endif
#include "DatasmithSketchUpSDKCeases.h"

#pragma warning(push)
// disable(SU2020): "__GNUC__' is not defined as a preprocessor macro, replacing"
#pragma warning(disable: 4668)
// disable(SU2020): macro name '_INTEGRAL_MAX_BITS' is reserved, '#define' ignored
#pragma warning(disable: 4117)
// disable(SU2020): 'DEPRECATED' : macro redefinition; 'ASSUME': macro redefinition
#pragma warning(disable: 4005)
// disable(SU2021): 'reinterpret_cast': unsafe conversion from 'ruby::backward::cxxanyargs::void_type (__cdecl *)' to 'rb_gvar_setter_t (__cdecl *)'	
#pragma warning(disable: 4191)
// disable(SU2019 & SU2020): 'register' is no longer a supported storage class	
#pragma warning(disable: 5033)
#include <ruby.h>
#pragma warning(pop)

// Datasmith SDK.
#include "Containers/Array.h"
#include "Containers/StringConv.h"
#include "DatasmithExporterManager.h"
#include "DatasmithSceneExporter.h"
#include "DatasmithSceneFactory.h"
#include "Misc/Paths.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "DatasmithSceneXmlWriter.h"

#include "DatasmithSceneFactory.h"

class FDatasmithSketchUpScene
{
public:
	TSharedRef<IDatasmithScene> DatasmithSceneRef;
	TSharedRef<FDatasmithSceneExporter> SceneExporterRef;

	FDatasmithSketchUpScene() 
		: DatasmithSceneRef(FDatasmithSceneFactory::CreateScene(TEXT("")))
		, SceneExporterRef(MakeShared<FDatasmithSceneExporter>())
	{
#define SKETCHUP_HOST_NAME       TEXT("SketchUp")
#define SKETCHUP_VENDOR_NAME     TEXT("Trimble Inc.")

		DatasmithSceneRef->SetHost(SKETCHUP_HOST_NAME);

		// Set the vendor name of the application used to build the scene.
		DatasmithSceneRef->SetVendor(SKETCHUP_VENDOR_NAME);

		SUEdition Edition;
		SUGetEdition(&Edition);
		FString ProductName = TEXT("SketchUp Pro");
		switch (Edition)
		{
		case SUEdition_Make: 
			ProductName = TEXT("SketchUp Make"); 
			break;
		case SUEdition_Pro: 
			ProductName = TEXT("SketchUp Pro"); 
			break;
		default:
			ProductName = TEXT("SketchUp Unknown"); 
			break;
		};

		// Set the product name of the application used to build the scene.
		DatasmithSceneRef->SetProductName(*ProductName);

		TArray<char> VersionArr;
		VersionArr.SetNum(32);

		while (SUGetVersionStringUtf8(VersionArr.Num(), VersionArr.GetData()) == SU_ERROR_INSUFFICIENT_SIZE)
		{
			VersionArr.SetNum(VersionArr.Num());
		}

		FUTF8ToTCHAR Converter(VersionArr.GetData(), VersionArr.Num());
		FString VersionStr(Converter.Length(), Converter.Get());


		// Set the product version of the application used to build the scene.
		DatasmithSceneRef->SetProductVersion(*VersionStr);

		// XXX: PreExport needs to be called before DirectLink instance is constructed - 
		// Reason - it calls initialization of FTaskGraphInterface. Callstack:
		// PreExport:
		//  - FDatasmithExporterManager::Initialize 
		//	-- DatasmithGameThread::InitializeInCurrentThread
		//  --- GEngineLoop.PreInit
		//  ---- PreInitPreStartupScreen
		//  ----- FTaskGraphInterface::Startup
		PreExport();
	}

	TSharedRef<IDatasmithScene>& GetDatasmithSceneRef()
	{
		return DatasmithSceneRef;
	}

	TSharedRef<FDatasmithSceneExporter>& GetSceneExporterRef()
	{
		return SceneExporterRef;
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
};

class FDatasmithSketchUpDirectLinkManager
{
public:

	static bool Init(bool bEnableUI, const FString& InEnginePath)
	{
		FDatasmithExporterManager::FInitOptions Options;
		Options.bEnableMessaging = true; // DirectLink requires the Messaging service.
		Options.bSuppressLogs = false;   // Log are useful, don't suppress them
		Options.bUseDatasmithExporterUI = bEnableUI;
		Options.RemoteEngineDirPath = *InEnginePath;

		if (!FDatasmithExporterManager::Initialize(Options))
		{
			return false;
		}

		if (int32 ErrorCode = FDatasmithDirectLink::ValidateCommunicationSetup())
		{
			return false;
		}
		return true;
	}

	void InitializeForScene(FDatasmithSketchUpScene& Scene)
	{
		DirectLink.InitializeForScene(Scene.GetDatasmithSceneRef());
	}

	void UpdateScene(FDatasmithSketchUpScene& Scene)
	{
		// FDatasmithSceneUtils::CleanUpScene(Scene.GetDatasmithSceneRef(), true);
		DirectLink.UpdateScene(Scene.GetDatasmithSceneRef());
	}

private:
	FDatasmithDirectLink DirectLink;
};

// Maintains Datasmith scene and promotes Sketchup scene change events to it, updating DirectLink
class FDatasmithSketchUpDirectLinkExporter
{
public:
	FDatasmithSketchUpScene ExportedScene;

	bool bEnableDirectLink;
	FDatasmithSketchUpDirectLinkManager DirectLinkManager;

	DatasmithSketchUp::FExportContext Context;

	FDatasmithSketchUpDirectLinkExporter(const TCHAR* InName, const TCHAR* InOutputPath, bool bInEnableDirectLink) : bEnableDirectLink(bInEnableDirectLink)
	{
		// Set scene name before initializing DirectLink for the scene so that the name is passed along
		ExportedScene.SetName(InName);
		ExportedScene.SetOutputPath(InOutputPath);

		// NOTE: InitializeForScene needs to be called in order to have DirectLink UI working(is was crashing otherwise)
		if (bEnableDirectLink)
		{
			DirectLinkManager.InitializeForScene(ExportedScene);
		}
	}

	~FDatasmithSketchUpDirectLinkExporter()
	{
	}

	bool Start()
	{
		Context.DatasmithScene = ExportedScene.GetDatasmithSceneRef();
		Context.SceneExporter = ExportedScene.GetSceneExporterRef();
		Context.Populate();

		SetSceneModified();
		return true;
	}

	void Update()
	{
		Context.Update();
	}

	void SendUpdate()
	{
		if (bEnableDirectLink)
		{
			DirectLinkManager.UpdateScene(ExportedScene);
		}
	}

	// XXX: REMOVE before shipping
	// Used for simple testing on the plugin side what is being sent to DL
	void ExportCurrentDatasmithSceneWithoutCleanup()
	{
		{
			TSharedRef<IDatasmithScene> DatasmithScene = ExportedScene.GetDatasmithSceneRef();
			FString FilePath = FPaths::Combine(ExportedScene.GetSceneExporterRef()->GetOutputPath(), ExportedScene.GetSceneExporterRef()->GetName()) + TEXT(".") + FDatasmithUtils::GetFileExtension();

			TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileWriter(*FilePath));

			if (!Archive.IsValid())
			{
				// XXX - removed 
				//if (Impl->Logger.IsValid())
				//{
				//	Impl->Logger->AddGeneralError(*(TEXT("Unable to create file ") + FilePath + TEXT(", Aborting the export process")));
				//}
				return;
			}

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			PlatformFile.CreateDirectoryTree(ExportedScene.GetSceneExporterRef()->GetAssetsOutputPath());

			// Add Bump maps from Material objects to scene as TextureElement
			// XXX - removed Impl->CheckBumpMaps(DatasmithScene);

			// XXX - removed FDatasmithSceneUtils::CleanUpScene(DatasmithScene, bCleanupUnusedElements);

			// Update TextureElements
			// XXX - removed Impl->UpdateTextureElements(DatasmithScene);

			// XXX - note todo
			// todo: keep relative myself
			// Convert paths to relative
			FString AbsoluteDir = FString(ExportedScene.GetSceneExporterRef()->GetOutputPath()) + TEXT("/");

			for (int32 MeshIndex = 0; MeshIndex < DatasmithScene->GetMeshesCount(); ++MeshIndex)
			{
				TSharedPtr< IDatasmithMeshElement > Mesh = DatasmithScene->GetMesh(MeshIndex);

				FString RelativePath = Mesh->GetFile();
				FPaths::MakePathRelativeTo(RelativePath, *AbsoluteDir);

				Mesh->SetFile(*RelativePath);
			}

			for (int32 TextureIndex = 0; TextureIndex < DatasmithScene->GetTexturesCount(); ++TextureIndex)
			{
				TSharedPtr< IDatasmithTextureElement > Texture = DatasmithScene->GetTexture(TextureIndex);

				FString TextureFile = Texture->GetFile();
				FPaths::MakePathRelativeTo(TextureFile, *AbsoluteDir);
				Texture->SetFile(*TextureFile);
			}

			// XXX - removed 
			//FDatasmithAnimationSerializer AnimSerializer;
			//int32 NumSequences = DatasmithScene->GetLevelSequencesCount();
			//for (int32 SequenceIndex = 0; SequenceIndex < NumSequences; ++SequenceIndex)
			//{
			//	const TSharedPtr<IDatasmithLevelSequenceElement>& LevelSequence = DatasmithScene->GetLevelSequence(SequenceIndex);
			//	if (LevelSequence.IsValid())
			//	{
			//		FString AnimFilePath = FPaths::Combine(Impl->AssetsOutputPath, LevelSequence->GetName()) + DATASMITH_ANIMATION_EXTENSION;

			//		if (AnimSerializer.Serialize(LevelSequence.ToSharedRef(), *AnimFilePath))
			//		{
			//			TUniquePtr<FArchive> AnimArchive(IFileManager::Get().CreateFileReader(*AnimFilePath));
			//			if (AnimArchive)
			//			{
			//				LevelSequence->SetFileHash(FMD5Hash::HashFileFromArchive(AnimArchive.Get()));
			//			}

			//			FPaths::MakePathRelativeTo(AnimFilePath, *AbsoluteDir);
			//			LevelSequence->SetFile(*AnimFilePath);
			//		}
			//	}
			//}

			// XXX - removed 
			// Log time spent to export scene in seconds
			//int ElapsedTime = (int)FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Impl->ExportStartCycles);
			//DatasmithScene->SetExportDuration(ElapsedTime);

			FDatasmithSceneXmlWriter DatasmithSceneXmlWriter;
			DatasmithSceneXmlWriter.Serialize(DatasmithScene, *Archive);

			Archive->Close();

			// Run the garbage collector at this point so that we're in a good state for the next export
			FDatasmithExporterManager::RunGarbageCollection();
		}

		TSharedRef<IDatasmithScene> DatasmithScene = ExportedScene.GetDatasmithSceneRef();

		// Convert paths back to absolute(they were changes by Export)
		FString AbsoluteDir = FString(ExportedScene.GetSceneExporterRef()->GetOutputPath()) + TEXT("/");
		for (int32 MeshIndex = 0; MeshIndex < DatasmithScene->GetMeshesCount(); ++MeshIndex)
		{
			TSharedPtr< IDatasmithMeshElement > Mesh = DatasmithScene->GetMesh(MeshIndex);

			FString RelativePath = Mesh->GetFile();
			Mesh->SetFile(*FPaths::ConvertRelativePathToFull(AbsoluteDir, *RelativePath));
		}

		for (int32 TextureIndex = 0; TextureIndex < DatasmithScene->GetTexturesCount(); ++TextureIndex)
		{
			TSharedPtr< IDatasmithTextureElement > Texture = DatasmithScene->GetTexture(TextureIndex);

			FString RelativePath = Texture->GetFile();

			Texture->SetFile(*FPaths::ConvertRelativePathToFull(AbsoluteDir, *RelativePath));
		}
	}

	void ExportCurrentDatasmithScene()
	{
		// NOTE: FDatasmithSceneUtils::CleanUpScene is called from FDatasmithSceneExporter::Export
		ExportedScene.GetSceneExporterRef()->Export(ExportedScene.GetDatasmithSceneRef());

		TSharedRef<IDatasmithScene> DatasmithScene = ExportedScene.GetDatasmithSceneRef();

		// Convert paths back to absolute(they were changes by Export)
		FString AbsoluteDir = FString(ExportedScene.GetSceneExporterRef()->GetOutputPath()) + TEXT("/");
		for (int32 MeshIndex = 0; MeshIndex < DatasmithScene->GetMeshesCount(); ++MeshIndex)
		{
			TSharedPtr< IDatasmithMeshElement > Mesh = DatasmithScene->GetMesh(MeshIndex);

			FString RelativePath = Mesh->GetFile();
			Mesh->SetFile(*FPaths::ConvertRelativePathToFull(AbsoluteDir, *RelativePath));
		}

		for (int32 TextureIndex = 0; TextureIndex < DatasmithScene->GetTexturesCount(); ++TextureIndex)
		{
			TSharedPtr< IDatasmithTextureElement > Texture = DatasmithScene->GetTexture(TextureIndex);

			FString RelativePath = Texture->GetFile();
			
			Texture->SetFile(*FPaths::ConvertRelativePathToFull(AbsoluteDir, *RelativePath));
		}
	}

	void SetSceneModified()
	{
		// todo: set flag that update needs to be sent
	}

	FORCENOINLINE bool OnComponentInstanceChanged(SUEntityRef Entity)
	{
		DatasmithSketchUp::FEntityIDType EntityId = DatasmithSketchUpUtils::GetEntityID(Entity);

		Context.ComponentInstances.InvalidateComponentInstanceProperties(EntityId);
		SetSceneModified();
		return true;
	}

	FORCENOINLINE bool InvalidateGeometryForFace(DatasmithSketchUp::FEntityIDType FaceId)
	{
		// When Face is modified find Entities it belongs too, reexport Entities meshes and update Occurrences using this Entities
		if (DatasmithSketchUp::FEntities* Entities = Context.EntitiesObjects.FindFace(FaceId.EntityID))
		{
			Entities->Definition.InvalidateDefinitionGeometry();
			return true;
		}
		// todo: why there's could have been no Entities registered for a 'modified' Face?
		return false;
	}

	FORCENOINLINE bool OnEntityRemoved(DatasmithSketchUp::FEntityIDType ParentEntityId, DatasmithSketchUp::FEntityIDType EntityId)
	{
		// todo: map each existing entity id to its type so don't need to check every collection?

		//Try ComponentInstance/Group
		if (Context.ComponentInstances.RemoveComponentInstance(ParentEntityId, EntityId))
		{
			return true;
		}

		// Try Face
		if (InvalidateGeometryForFace(EntityId))
		{
			return true;
		}

		// Try Material
		if (Context.Materials.RemoveMaterial(EntityId))
		{
			return true;
		}

		return false;
	}

	bool OnEntityModified(DatasmithSketchUp::FEntityIDType EntityId)
	{
		if (Context.ComponentInstances.InvalidateComponentInstanceProperties(EntityId))
		{
			return true;
		}

		if (Context.Materials.InvalidateMaterial(EntityId))
		{
			return true;
		}

		return false;
	}

	bool OnEntityModified(SUEntityRef Entity)
	{
		switch (SUEntityGetType(Entity))
		{
		case SURefType_Group:
		case SURefType_ComponentInstance:
		{
			OnComponentInstanceChanged(Entity);
			break;
		}
		case SURefType_Face:
		{
			int32_t FaceId = 0;
			if (SUEntityGetID(Entity, &FaceId) == SU_ERROR_NONE)
			{
				InvalidateGeometryForFace(DatasmithSketchUp::FEntityIDType(FaceId));
				// todo: can we change DatasmithMesh in-place?
				// - In case mesh need to be updated on DatasmithMeshActors:
				// - and reset on every MeshActor, (or create if was empty? or not - modified, not added, not expected)
			}
			else
			{
				// todo: unexpected
			}
			break;
		}
		case SURefType_Material:
		{
			Context.Materials.InvalidateMaterial(SUMaterialFromEntity(Entity));
			break;
		}
		default:
		{
			// todo: not expected
		}
		}
		return true;
	}

	bool OnGeometryModified(DatasmithSketchUp::FEntityIDType EntityId)
	{
		DatasmithSketchUp::FDefinition* DefinitionPtr = Context.GetDefinition(EntityId);

		if (!DefinitionPtr)
		{
			// Not a component entity
			return false;
		}

		DefinitionPtr->InvalidateDefinitionGeometry();

		return false;
	}


	bool OnEntityAdded(DatasmithSketchUp::FEntityIDType ParentEntityId, DatasmithSketchUp::FEntityIDType EntityId)
	{
		DatasmithSketchUp::FDefinition* DefinitionPtr = Context.GetDefinition(ParentEntityId);
		
		if (!DefinitionPtr)
		{
			// Not a component entity
			return false;
		}

		DatasmithSketchUp::FEntities& Entities = DefinitionPtr->GetEntities();

		TArray<SUGroupRef> Groups = Entities.GetGroups();
		if (SUGroupRef* GroupRefPtr = Groups.FindByPredicate([EntityId](const SUGroupRef& GroupRef) { return DatasmithSketchUpUtils::GetGroupID(GroupRef) == EntityId; }))
		{
			SUGroupRef GroupRef = *GroupRefPtr;

			SUEntityRef Entity = SUGroupToEntity(GroupRef);

			SUComponentInstanceRef Ref = SUComponentInstanceFromEntity(Entity);

			// todo: remove dup
			DefinitionPtr->AddInstance(Context, Context.ComponentInstances.AddComponentInstance(*DefinitionPtr, Ref));
			return true;
		}

		// todo: embed Find by id into Entities itself
		TArray<SUComponentInstanceRef> ComponentInstances = Entities.GetComponentInstances();
		if (SUComponentInstanceRef* ComponentInstanceRefPtr = ComponentInstances.FindByPredicate([EntityId](const SUComponentInstanceRef& EntityRef) { return DatasmithSketchUpUtils::GetComponentInstanceID(EntityRef) == EntityId; }))
		{
			SUComponentInstanceRef ComponentInstanceRef = *ComponentInstanceRefPtr;

			SUEntityRef Entity = SUComponentInstanceToEntity(ComponentInstanceRef);
			// todo: remove dup

			SUComponentInstanceRef Ref = SUComponentInstanceFromEntity(Entity);
			DefinitionPtr->AddInstance(Context, Context.ComponentInstances.AddComponentInstance(*DefinitionPtr, Ref));
			return true;
		}

		return false;
	}

	bool OnEntityAdded(SUEntityRef EntityParent, SUEntityRef Entity)
	{
		SURefType EntityType = SUEntityGetType(Entity);
		switch (EntityType)
		{
		case SURefType_Group:
		case SURefType_ComponentInstance:
		{
			DatasmithSketchUp::FDefinition* DefinitionPtr = Context.GetDefinition(EntityParent);
			if (ensure(DefinitionPtr)) // Parent definition expected to already exist when new entity being added
			{
				DefinitionPtr->AddInstance(Context, Context.ComponentInstances.AddComponentInstance(*DefinitionPtr, SUComponentInstanceFromEntity(Entity)));
			}

			break;
		}
		case SURefType_Face:
		{
			Context.GetDefinition(EntityParent)->InvalidateDefinitionGeometry();
			break;
		}
		case SURefType_Material:
		{
			Context.Materials.CreateMaterial(SUMaterialFromEntity(Entity));
			break;
		}
		default:
		{
			// todo: not expected
		}
		}
		return true;
	}
	
	bool OnMaterialAdded(DatasmithSketchUp::FEntityIDType EntityId)
	{
		Context.Materials.CreateMaterial(EntityId);
		return true;
	}
};

///////////////////////////////////////////////////////////////////////////
// Ruby wrappers:

// Ruby wrapper utility functions
FString RubyStringToUnreal(VALUE path)
{
	const char* PathPtr = RSTRING_PTR(path);
	long PathLen = RSTRING_LEN(path);

	FUTF8ToTCHAR Converter(PathPtr, PathLen);
	// todo: check that Ruby has utf-8 internally
	// todo: test that non-null term doesn't fail
	return FString(Converter.Length(), Converter.Get());
}

VALUE UnrealStringToRuby(const FString& InStr)
{
	FTCHARToUTF8 Converter(*InStr, InStr.Len());

	return rb_str_new(Converter.Get(), Converter.Length());
}

typedef VALUE(*RubyFunctionType)(ANYARGS);

#pragma warning(push)
// disable: 'reinterpret_cast': unsafe conversion from 'F' to 'RubyFunctionType'
// This IS unsafe but so is Ruby C API
#pragma warning(disable: 4191)
template<typename F>
RubyFunctionType ToRuby(F f)
{
	return reinterpret_cast<RubyFunctionType>(f);
}
#pragma warning(pop)

// DatasmithSketchUpDirectLinkExporter wrapper
VALUE DatasmithSketchUpDirectLinkExporterCRubyClass;

void DatasmithSketchUpDirectLinkExporter_free(void* ptr)
{
	delete reinterpret_cast<FDatasmithSketchUpDirectLinkExporter*>(ptr);
}

// Created object and wrap it to return to Ruby
VALUE DatasmithSketchUpDirectLinkExporter_new(VALUE cls, VALUE name, VALUE path, VALUE enable_directlink)
{
	// Converting args
	bool bEnableDirectLink = RTEST(enable_directlink);

	Check_Type(name, T_STRING);
	Check_Type(path, T_STRING);

	FString NameUnreal = RubyStringToUnreal(name);
	FString PathUnreal = RubyStringToUnreal(path);
	// Done converting args

	FDatasmithSketchUpDirectLinkExporter* ptr = new FDatasmithSketchUpDirectLinkExporter(*NameUnreal, *PathUnreal, bEnableDirectLink);
	VALUE wrapped = Data_Wrap_Struct(cls, 0, DatasmithSketchUpDirectLinkExporter_free, ptr);
	rb_obj_call_init(wrapped, 0, NULL);
	return wrapped;
}

VALUE DatasmithSketchUpDirectLinkExporter_start(VALUE self)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* Ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, Ptr);

	// Done converting args

	return Ptr->Start() ? Qtrue : Qfalse;
}

VALUE DatasmithSketchUpDirectLinkExporter_send_update(VALUE self)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, ptr);
	// Done converting args

	ptr->SendUpdate();
	return Qtrue;
}

VALUE DatasmithSketchUpDirectLinkExporter_update(VALUE self)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, ptr);
	// Done converting args

	ptr->Update();
	return Qtrue;
}

VALUE DatasmithSketchUpDirectLinkExporter_export_current_datasmith_scene(VALUE self)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, ptr);
	// Done converting args

	ptr->ExportCurrentDatasmithScene();
	return Qtrue;
}

VALUE DatasmithSketchUpDirectLinkExporter_export_current_datasmith_scene_no_cleanup(VALUE self)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, ptr);
	// Done converting args

	ptr->ExportCurrentDatasmithSceneWithoutCleanup();
	return Qtrue;
}

#ifndef SKP_SDK_2019
VALUE DatasmithSketchUpDirectLinkExporter_on_component_instance_changed(VALUE self, VALUE ruby_entity)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* Ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, Ptr);

	SUEntityRef Entity = SU_INVALID;
	if (SUEntityFromRuby(ruby_entity, &Entity) != SU_ERROR_NONE) {
		rb_raise(rb_eTypeError, "Expected SketchUp Entity");
	}
	// Done converting args

	Ptr->OnComponentInstanceChanged(Entity);

	return Qtrue;
}

VALUE DatasmithSketchUpDirectLinkExporter_on_entity_modified(VALUE self, VALUE ruby_entity)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* Ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, Ptr);

	int RubyEntityRubyObjectType = TYPE(ruby_entity);

	SUEntityRef Entity = SU_INVALID;

	if (SUEntityFromRuby(ruby_entity, &Entity) != SU_ERROR_NONE) {
		rb_raise(rb_eTypeError, "Expected SketchUp Entity or nil");
	}
	// Done converting args

	Ptr->OnEntityModified(Entity);

	return Qtrue;
}

VALUE DatasmithSketchUpDirectLinkExporter_on_entity_added(VALUE self, VALUE ruby_parent_entity, VALUE ruby_entity)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* Ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, Ptr);

	SUEntityRef ParentEntity = SU_INVALID;
	if (!NIL_P(ruby_parent_entity) && (SUEntityFromRuby(ruby_parent_entity, &ParentEntity) != SU_ERROR_NONE)) {
		
		rb_raise(rb_eTypeError, "Expected SketchUp Entity but found '%s'", StringValue(ruby_parent_entity));
	}

	SUEntityRef Entity = SU_INVALID;
	if (SUEntityFromRuby(ruby_entity, &Entity) != SU_ERROR_NONE) {
		rb_raise(rb_eTypeError, "Expected SketchUp Entity or nil");
	}
	// Done converting args

	Ptr->OnEntityAdded(ParentEntity, Entity);

	return Qtrue;
}
#endif

VALUE DatasmithSketchUpDirectLinkExporter_on_entity_modified_by_id(VALUE self, VALUE ruby_entity_id)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* Ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, Ptr);

	Check_Type(ruby_entity_id, T_FIXNUM);
	int32 EntityId = FIX2LONG(ruby_entity_id);
	// Done converting args

	Ptr->OnEntityModified(DatasmithSketchUp::FEntityIDType(EntityId));

	return Qtrue;
}

VALUE DatasmithSketchUpDirectLinkExporter_on_geometry_modified_by_id(VALUE self, VALUE ruby_entity_id)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* Ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, Ptr);

	Check_Type(ruby_entity_id, T_FIXNUM);
	int32 EntityId = FIX2LONG(ruby_entity_id);
	// Done converting args

	Ptr->OnGeometryModified(DatasmithSketchUp::FEntityIDType(EntityId));

	return Qtrue;
}

VALUE DatasmithSketchUpDirectLinkExporter_on_entity_added_by_id(VALUE self, VALUE ruby_parent_entity_id, VALUE ruby_entity_id)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* Ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, Ptr);

	Check_Type(ruby_parent_entity_id, T_FIXNUM);
	int32 ParentEntityId = FIX2LONG(ruby_parent_entity_id);

	Check_Type(ruby_entity_id, T_FIXNUM);
	int32 EntityId = FIX2LONG(ruby_entity_id);
	// Done converting args

	Ptr->OnEntityAdded(DatasmithSketchUp::FEntityIDType(ParentEntityId), DatasmithSketchUp::FEntityIDType(EntityId));

	return Qtrue;
}

VALUE DatasmithSketchUpDirectLinkExporter_on_material_added_by_id(VALUE self, VALUE ruby_entity_id)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* Ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, Ptr);

	Check_Type(ruby_entity_id, T_FIXNUM);
	int32 EntityId = FIX2LONG(ruby_entity_id);
	// Done converting args

	Ptr->OnMaterialAdded(DatasmithSketchUp::FEntityIDType(EntityId));

	return Qtrue;
}


VALUE DatasmithSketchUpDirectLinkExporter_on_entity_removed(VALUE self, VALUE ruby_parent_entity_id, VALUE ruby_entity_id)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* Ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, Ptr);

	Check_Type(ruby_parent_entity_id, T_FIXNUM);
	int32 ParentEntityId = FIX2LONG(ruby_parent_entity_id);

	Check_Type(ruby_entity_id, T_FIXNUM);
	int32 EntityId = FIX2LONG(ruby_entity_id);
	// Done converting args
	
	Ptr->OnEntityRemoved(DatasmithSketchUp::FEntityIDType(ParentEntityId), DatasmithSketchUp::FEntityIDType(EntityId));

	return Qtrue;
}

VALUE on_load(VALUE self, VALUE enable_ui, VALUE engine_path) {
	// Converting args
	Check_Type(engine_path, T_STRING);

	bool bEnableUI = RTEST(enable_ui);
	FString EnginePathUnreal = RubyStringToUnreal(engine_path);
	// Done converting args

	// This needs to be called before creating instance of DirectLink
	return FDatasmithSketchUpDirectLinkManager::Init(bEnableUI, EnginePathUnreal) ? Qtrue : Qfalse;
}

VALUE on_unload() {
	FDatasmithExporterManager::Shutdown();
	return Qtrue;
}

VALUE open_directlink_ui() {
	if (IDatasmithExporterUIModule * Module = IDatasmithExporterUIModule::Get())
	{
		if (IDirectLinkUI* UI = Module->GetDirectLinkExporterUI())
		{
			UI->OpenDirectLinkStreamWindow();
			return Qtrue;
		}
	}
	return Qfalse;
}

VALUE get_directlink_cache_directory() {
	if (IDatasmithExporterUIModule* Module = IDatasmithExporterUIModule::Get())
	{
		if (IDirectLinkUI* UI = Module->GetDirectLinkExporterUI())
		{
			return UnrealStringToRuby(UI->GetDirectLinkCacheDirectory());
		}
	}
	return Qnil;
}


// todo: hardcoded init module function name
extern "C" __declspec(dllexport) void Init_DatasmithSketchUpRuby()
{

	VALUE EpicGames = rb_define_module("EpicGames");
	VALUE Datasmith = rb_define_module_under(EpicGames, "DatasmithBackend");

	rb_define_module_function(Datasmith, "on_load", ToRuby(on_load), 2);
	rb_define_module_function(Datasmith, "on_unload", ToRuby(on_unload), 0);

	rb_define_module_function(Datasmith, "open_directlink_ui", ToRuby(open_directlink_ui), 0);
	rb_define_module_function(Datasmith, "get_directlink_cache_directory", ToRuby(get_directlink_cache_directory), 0);

	DatasmithSketchUpDirectLinkExporterCRubyClass = rb_define_class_under(Datasmith, "DatasmithSketchUpDirectLinkExporter", rb_cObject);

	rb_define_singleton_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "new", ToRuby(DatasmithSketchUpDirectLinkExporter_new), 3);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "start", ToRuby(DatasmithSketchUpDirectLinkExporter_start), 0);

#ifndef SKP_SDK_2019
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "on_component_instance_changed", ToRuby(DatasmithSketchUpDirectLinkExporter_on_component_instance_changed), 1);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "on_entity_modified", ToRuby(DatasmithSketchUpDirectLinkExporter_on_entity_modified), 1);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "on_entity_added", ToRuby(DatasmithSketchUpDirectLinkExporter_on_entity_added), 2);
#endif

	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "on_entity_modified_by_id", ToRuby(DatasmithSketchUpDirectLinkExporter_on_entity_modified_by_id), 1);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "on_geometry_modified_by_id", ToRuby(DatasmithSketchUpDirectLinkExporter_on_geometry_modified_by_id), 1);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "on_entity_added_by_id", ToRuby(DatasmithSketchUpDirectLinkExporter_on_entity_added_by_id), 2);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "on_material_added_by_id", ToRuby(DatasmithSketchUpDirectLinkExporter_on_material_added_by_id), 1);

	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "on_entity_removed", ToRuby(DatasmithSketchUpDirectLinkExporter_on_entity_removed), 2);

	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "update", ToRuby(DatasmithSketchUpDirectLinkExporter_update), 0);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "send_update", ToRuby(DatasmithSketchUpDirectLinkExporter_send_update), 0);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "export_current_datasmith_scene", ToRuby(DatasmithSketchUpDirectLinkExporter_export_current_datasmith_scene), 0);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "export_current_datasmith_scene_no_cleanup", ToRuby(DatasmithSketchUpDirectLinkExporter_export_current_datasmith_scene_no_cleanup), 0);
	
}

/* todo:
- error reporting(Ruby Console, Log etc)
*/
