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


#include "DatasmithSketchUpSDKBegins.h"
#include <SketchUpAPI/sketchup.h>
#include <SketchUpAPI/application/ruby_api.h>
#include "DatasmithSketchUpSDKCeases.h"

#pragma warning(push)
// disable: "__GNUC__' is not defined as a preprocessor macro, replacing"
#pragma warning(disable: 4668)
// disable: macro name '_INTEGRAL_MAX_BITS' is reserved, '#define' ignored
#pragma warning(disable: 4117)
// disable: 'DEPRECATED' : macro redefinition; 'ASSUME': macro redefinition
#pragma warning(disable: 4005)
#include <ruby.h>
#pragma warning(pop)

// Datasmith SDK.
#include "Containers/Array.h"
#include "Containers/StringConv.h"
#include "DatasmithExporterManager.h"
#include "DatasmithSceneExporter.h"
#include "DatasmithSceneFactory.h"
#include "Misc/Paths.h"

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
#define SKETCHUP_PRODUCT_NAME    TEXT("SketchUp Pro")
#define SKETCHUP_PRODUCT_VERSION TEXT("Version Unknown")

		DatasmithSceneRef->SetHost(SKETCHUP_HOST_NAME);

		// Set the vendor name of the application used to build the scene.
		DatasmithSceneRef->SetVendor(SKETCHUP_VENDOR_NAME);

		// Set the product name of the application used to build the scene.
		DatasmithSceneRef->SetProductName(SKETCHUP_PRODUCT_NAME);

		// Set the product version of the application used to build the scene.
		DatasmithSceneRef->SetProductVersion(SKETCHUP_PRODUCT_VERSION);

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

	static bool Init()
	{
		FDatasmithExporterManager::FInitOptions Options;
		Options.bEnableMessaging = true; // DirectLink requires the Messaging service.
		Options.bSuppressLogs = false;   // Log are useful, don't suppress them
		Options.bUseDatasmithExporterUI = false;
		Options.RemoteEngineDirPath = nullptr;

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
		FDatasmithSceneUtils::CleanUpScene(Scene.GetDatasmithSceneRef(), true);
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

	FDatasmithSketchUpDirectLinkExporter(bool bInEnableDirectLink) : bEnableDirectLink(bInEnableDirectLink) {}

	~FDatasmithSketchUpDirectLinkExporter()
	{
	}

	bool Start(const TCHAR* InName, const TCHAR* InOutputPath)
	{
		ExportedScene.SetName(InName);
		ExportedScene.SetOutputPath(InOutputPath);

		if (bEnableDirectLink)
		{
			DirectLinkManager.InitializeForScene(ExportedScene);
		}

		Context.DatasmithScene = ExportedScene.GetDatasmithSceneRef();
		Context.SceneExporter = ExportedScene.GetSceneExporterRef();
		Context.Populate();

		SetSceneModified();
		return true;
	}

	void SendUpdate()
	{
		if (bEnableDirectLink)
		{
			DirectLinkManager.UpdateScene(ExportedScene);
		}
	}

	void ExportCurrentDatasmithScene()
	{
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

	bool OnComponentInstanceChanged(SUEntityRef Entity)
	{
		DatasmithSketchUp::FEntityIDType EntityId = DatasmithSketchUpUtils::GetEntityID(Entity);
		if (TArray<TSharedPtr<DatasmithSketchUp::FNodeOccurence>>* OccurencesPtr = Context.ComponentInstances.GetOccurrencesForComponentInstance(EntityId))
		{
			for(TSharedPtr<DatasmithSketchUp::FNodeOccurence>& Occurence: *OccurencesPtr)
			{
				Occurence->Update(Context);
			}
		}
		else
		{
			// todo: implement. This could happen if
			//  - component instance was previously skipped because it doesn't contain meaningful data(probably it's not needed to process it it's still empty)
			//  - was removed recently. Not sure if 'changed' can code after 'removed'
			//  - addition wasn't handled
			// - anything else?
			ensureMsgf(false, TEXT("ComponentInstance was not found in ComponentInstanceMap"));
		}

		SetSceneModified();
		return true;
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
				// When Face is modified find Entities it belongs too, reexport Entities meshes and update Occurrences using this Entities
				if (DatasmithSketchUp::FEntities* Entities = Context.EntitiesObjects.FindFace(FaceId))
				{
					// Re-extract and reexport entities geometry
					Entities->UpdateGeometry(Context);

					// todo: make sure that - DatasmithMeshActor's updated that use this mesh, materials 
					// Then update all occurrences of the definition instances within the hierarchy
					// Reason: Faces change ComponentInstances 
					Entities->Definition.UpdateInstances(Context);
					SetSceneModified();
				}
				else
				{
					// todo: why there's could have been no Entities registered for a 'modified' Face?
				}

				// todo: can we change DatasmithMesh in-place?
				// - In case mesh need to be updated on DatasmithMeshActors:
				// - and reset on every MeshActor, (or create if was empty? or not - modified, not added, not expected)
			}
			else
			{
				// todo: unexpected
			}
		}
		default:
		{
			// todo: not expected
		}
		}
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
VALUE DatasmithSketchUpDirectLinkExporter_new(VALUE cls, VALUE enable_directlink)
{
	// Converting args
	bool bEnableDirectLink = RTEST(enable_directlink);
	// Done converting args

	FDatasmithSketchUpDirectLinkExporter* ptr = new FDatasmithSketchUpDirectLinkExporter(bEnableDirectLink);
	VALUE wrapped = Data_Wrap_Struct(cls, 0, DatasmithSketchUpDirectLinkExporter_free, ptr);
	rb_obj_call_init(wrapped, 0, NULL);
	return wrapped;
}

VALUE DatasmithSketchUpDirectLinkExporter_start(VALUE self, VALUE name, VALUE path)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* Ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, Ptr);

	Check_Type(name, T_STRING);
	Check_Type(path, T_STRING);

	FString NameUnreal = RubyStringToUnreal(name);
	FString PathUnreal = RubyStringToUnreal(path);
	// Done converting args

	return Ptr->Start(*NameUnreal, *PathUnreal) ? Qtrue : Qfalse;
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

VALUE DatasmithSketchUpDirectLinkExporter_export_current_datasmith_scene(VALUE self)
{
	// Converting args
	FDatasmithSketchUpDirectLinkExporter* ptr;
	Data_Get_Struct(self, FDatasmithSketchUpDirectLinkExporter, ptr);
	// Done converting args

	ptr->ExportCurrentDatasmithScene();
	return Qtrue;
}



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

VALUE on_load() {
	// This needs to be called before creating instance of DirectLink
	return FDatasmithSketchUpDirectLinkManager::Init() ? Qtrue : Qfalse;
}

VALUE on_unload() {
	FDatasmithDirectLink::Shutdown();
	return Qtrue;
}

// todo: hardcoded init module function name
extern "C" __declspec(dllexport) void Init_DatasmithSketchUpRuby2020()
{
	VALUE EpicGames = rb_define_module("EpicGames");
	VALUE Datasmith = rb_define_module_under(EpicGames, "DatasmithBackend");

	rb_define_module_function(Datasmith, "on_load", ToRuby(on_load), 0);
	rb_define_module_function(Datasmith, "on_unload", ToRuby(on_unload), 0);

	DatasmithSketchUpDirectLinkExporterCRubyClass = rb_define_class_under(Datasmith, "DatasmithSketchUpDirectLinkExporter", rb_cObject);

	rb_define_singleton_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "new", ToRuby(DatasmithSketchUpDirectLinkExporter_new), 1);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "start", ToRuby(DatasmithSketchUpDirectLinkExporter_start), 2);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "on_component_instance_changed", ToRuby(DatasmithSketchUpDirectLinkExporter_on_component_instance_changed), 1);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "on_entity_modified", ToRuby(DatasmithSketchUpDirectLinkExporter_on_entity_modified), 1);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "send_update", ToRuby(DatasmithSketchUpDirectLinkExporter_send_update), 0);
	rb_define_method(DatasmithSketchUpDirectLinkExporterCRubyClass, "export_current_datasmith_scene", ToRuby(DatasmithSketchUpDirectLinkExporter_export_current_datasmith_scene), 0);
}

/* todo:
- error reporting(Ruby Console, Log etc)
*/
