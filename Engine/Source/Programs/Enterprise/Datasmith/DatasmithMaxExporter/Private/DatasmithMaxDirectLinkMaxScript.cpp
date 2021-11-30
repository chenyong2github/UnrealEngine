// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NEW_DIRECTLINK_PLUGIN

#include "DatasmithMaxDirectLink.h"

#include "DatasmithExporterManager.h"


#include "Logging/LogMacros.h"

#include "Resources/Windows/resource.h"

#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "impexp.h"
	#include "max.h"

	#include "maxscript/maxwrapper/mxsobjects.h"

	#include "maxscript/maxscript.h"
	#include "maxscript/foundation/numbers.h"
	#include "maxscript/foundation/arrays.h"
	#include "maxscript\macros\define_instantiation_functions.h"

	#include <maxicon.h>

MAX_INCLUDES_END

extern HINSTANCE HInstanceMax;


namespace DatasmithMaxDirectLink
{

/************************************* MaxScript exports *********************************/

Value* OnLoad_cf(Value**, int);
Primitive OnLoad_pf(_M("Datasmith_OnLoad"), OnLoad_cf);

Value* OnLoad_cf(Value **arg_list, int count)
{
	check_arg_count(OnLoad, 2, count);
	Value* pEnableUI= arg_list[0];
	Value* pEnginePath = arg_list[1];

	bool bEnableUI = pEnableUI->to_bool();

	const TCHAR* EnginePathUnreal = (const TCHAR*)pEnginePath->to_string();

	if (CreateExporter(bEnableUI, EnginePathUnreal))
	{
		return &false_value;
	}
	GetExporter()->ParseScene();

	return bool_result(true);
}

Value* OnUnload_cf(Value**, int);
Primitive OnUnload_pf(_M("Datasmith_OnUnload"), OnUnload_cf);

Value* OnUnload_cf(Value **arg_list, int count)
{
	check_arg_count(OnUnload, 0, count);

	ShutdownExporter();

	return bool_result(true);
}

Value* SetOutputPath_cf(Value**, int);
Primitive SetOutputPath_pf(_M("Datasmith_SetOutputPath"), SetOutputPath_cf);

Value* SetOutputPath_cf(Value** arg_list, int count)
{
	check_arg_count(CreateScene, 1, count);
	Value* pOutputPath = arg_list[0];

	GetExporter()->SetOutputPath(pOutputPath->to_string());

	return bool_result(true);
}

Value* CreateScene_cf(Value**, int);
Primitive CreateScene_pf(_M("Datasmith_CreateScene"), CreateScene_cf);

Value* CreateScene_cf(Value** arg_list, int count)
{
	check_arg_count(CreateScene, 1, count);
	Value* pName = arg_list[0];

	GetExporter()->SetName(pName->to_string());

	return bool_result(true);
}

Value* UpdateScene_cf(Value**, int);
Primitive UpdateScene_pf(_M("Datasmith_UpdateScene"), UpdateScene_cf);

Value* UpdateScene_cf(Value** arg_list, int count)
{
	check_arg_count_with_keys(UpdateScene, 0, count);

	bool bQuiet = key_arg_or_default(quiet, &false_value)->to_bool();

	if(!GetExporter())
	{
		return bool_result(false);
	}

	bool bResult = GetExporter()->UpdateScene(bQuiet);
	return bool_result(bResult);
}


Value* Export_cf(Value**, int);
Primitive Export_pf(_M("Datasmith_Export"), Export_cf);

Value* Export_cf(Value** arg_list, int count)
{
	check_arg_count_with_keys(Export, 2, count);
	Value* pName = arg_list[0];
	Value* pOutputPath = arg_list[1];

	bool bQuiet = key_arg_or_default(quiet, &false_value)->to_bool();

	bool bResult = Export(pName->to_string(), pOutputPath->to_string(), bQuiet);;
	return bool_result(bResult);
}


Value* Reset_cf(Value**, int);
Primitive Reset_pf(_M("Datasmith_Reset"), Reset_cf);

Value* Reset_cf(Value** arg_list, int count)
{
	check_arg_count(Reset, 0, count);

	if (!GetExporter())
	{
		return bool_result(false);
	}

	GetExporter()->Reset();
	return bool_result(true);
}

Value* StartSceneChangeTracking_cf(Value**, int);
Primitive StartSceneChangeTracking_pf(_M("Datasmith_StartSceneChangeTracking"), StartSceneChangeTracking_cf);

Value* StartSceneChangeTracking_cf(Value** arg_list, int count)
{
	check_arg_count(StartSceneChangeTracking, 0, count);

	GetExporter()->StartSceneChangeTracking();

	return bool_result(true);
}

Value* DirectLinkInitializeForScene_cf(Value** arg_list, int count)
{
	check_arg_count(DirectLinkInitializeForScene, 0, count);

	GetExporter()->InitializeDirectLinkForScene();

	return bool_result(true);
}
Primitive DirectLinkInitializeForScene_pf(_M("Datasmith_DirectLinkInitializeForScene"), DirectLinkInitializeForScene_cf);


Value* DirectLinkUpdateScene_cf(Value** arg_list, int count)
{
	check_arg_count(DirectLinkUpdateScene, 0, count);
	LogDebug(TEXT("DirectLink::UpdateScene: start"));
	GetExporter()->UpdateDirectLinkScene();
	LogDebug(TEXT("DirectLink::UpdateScene: done"));

	return bool_result(true);
}
Primitive DirectLinkUpdateScene_pf(_M("Datasmith_DirectLinkUpdateScene"), DirectLinkUpdateScene_cf);

Value* ToggleAutoSync_cf(Value** arg_list, int count) 
{
	check_arg_count(ToggleAutoSync, 0, count);

	if (GetExporter())
	{
		return bool_result(GetExporter()->ToggleAutoSync());
	}

	return &false_value;
}
Primitive ToggleAutoSync_pf(_M("Datasmith_ToggleAutoSync"), ToggleAutoSync_cf);

Value* SetAutoSyncDelay_cf(Value** arg_list, int count) 
{
	check_arg_count(ToggleAutoSync, 1, count);

	if (GetExporter())
	{
		GetExporter()->SetAutoSyncDelay(arg_list[0]->to_float());
		return &true_value;
	}

	return &false_value;
}
Primitive SetAutoSyncDelay_pf(_M("Datasmith_SetAutoSyncDelay"), SetAutoSyncDelay_cf);

Value* OpenDirectlinkUi_cf(Value** arg_list, int count) 
{
	check_arg_count(OpenDirectlinkUi, 0, count);

	return bool_result(OpenDirectLinkUI());
}
Primitive OpenDirectlinkUi_pf(_M("Datasmith_OpenDirectlinkUi"), OpenDirectlinkUi_cf);


Value* GetDirectlinkCacheDirectory_cf(Value** arg_list, int count)
{
	check_arg_count(GetDirectlinkCacheDirectory, 0, count);
	const TCHAR* Path = GetDirectlinkCacheDirectory();
	if (Path)
	{
		return new String(Path);
	}

	return &undefined;
}

Primitive GetDirectlinkCacheDirectory_pf(_M("Datasmith_GetDirectlinkCacheDirectory"), GetDirectlinkCacheDirectory_cf);


Value* LogFlush_cf(Value** arg_list, int count)
{
	LogFlush();
	return &undefined;
}

Primitive LogFlush_pf(_M("Datasmith_LogFlush"), LogFlush_cf);


Value* Crash_cf(Value** arg_list, int count)
{
	volatile int* P;
	P = nullptr;

	*P = 666;
	return &undefined;
}

Primitive Crash_pf(_M("Datasmith_Crash"), Crash_cf);

Value* LogInfo_cf(Value** arg_list, int count)
{
	check_arg_count(CreateScene, 1, count);
	Value* Message = arg_list[0];

	LogInfo(Message->to_string());

	return bool_result(true);
}
Primitive LogInfo_pf(_M("Datasmith_LogInfo"), LogInfo_cf);

// Setup ActionTable with Datasmith commands exposed as actions
class FDatasmithActions
{
public:
	static const ActionTableId ActionTableId = 0x291356d8;
	static const ActionContextId ActionContextId = 0x291356d9;

	enum EActionIds
	{
		ID_SYNC_ACTION_ID,
		ID_AUTOSYNC_ACTION_ID,
		ID_CONNECTIONS_ACTION_ID,
		ID_EXPORT_ACTION_ID,
		ID_SHOWLOG_ACTION_ID,
	};

	class FDatasmithActionTable : public ActionTable
	{
	public:
		explicit FDatasmithActionTable(MSTR& Name): ActionTable(ActionTableId, ActionContextId, Name)
		{
			
		}

		BOOL IsChecked(int ActionId)
		{
			switch (ActionId)
			{
			case ID_AUTOSYNC_ACTION_ID: return GetExporter() && GetExporter()->IsAutoSyncEnabled(); // Can't change AutoSync icon but able to set its state to Checked when it's on
			};
			return false;
		}

		TMap<int, TUniquePtr<MaxBmpFileIcon>> IconForAction;

		MaxIcon* GetIcon(int ActionId) override
		{
			TUniquePtr<MaxBmpFileIcon>& Icon = IconForAction.FindOrAdd(ActionId);

			if (Icon)
			{
				return Icon.Get();
			}

			switch (ActionId)
			{
			case ID_SYNC_ACTION_ID:
			{
				Icon.Reset(new MaxBmpFileIcon(_T(":/Datasmith/Icons/DatasmithSyncIcon")));
				break;
			}
			case ID_AUTOSYNC_ACTION_ID:
			{
				Icon.Reset(new MaxBmpFileIcon(_T(":/Datasmith/Icons/DatasmithAutoSyncIconOn")));
				break;
			}
			case ID_CONNECTIONS_ACTION_ID:
			{
				Icon.Reset(new MaxBmpFileIcon(_T(":/Datasmith/Icons/DatasmithManageConnectionsIcon")));
				break;
			}
			case ID_EXPORT_ACTION_ID:
			{
				Icon.Reset(new MaxBmpFileIcon(_T(":/Datasmith/Icons/DatasmithIcon")));
				break;
			}
			case ID_SHOWLOG_ACTION_ID:
			{
				Icon.Reset(new MaxBmpFileIcon(_T(":/Datasmith/Icons/DatasmithLogIcon")));
				break;
			}
			}
			return Icon.Get();
		}
	};


	class FDatasmithActionCallback : public ActionCallback
	{
	public:

		BOOL ExecuteAction (int ActionId) {
			LogDebug(FString::Printf(TEXT("Action: %d"), ActionId));

			switch (ActionId)
			{
			case ID_SYNC_ACTION_ID:
			{
				if (GetExporter())
				{
					GetExporter()->UpdateScene(false);
					GetExporter()->UpdateDirectLinkScene();
				}
				return true;
			}
			case ID_AUTOSYNC_ACTION_ID:
			{
				if (GetExporter())
				{
					GetExporter()->ToggleAutoSync();
				}
				return true;
			}
			case ID_CONNECTIONS_ACTION_ID:
			{
				OpenDirectLinkUI();
				return true;
			}
			case ID_EXPORT_ACTION_ID:
			{
				if (GetExporter())
				{
					MCHAR* ScriptCode = _T("Datasmith_ExportDialog()");
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
					 ExecuteMAXScriptScript(ScriptCode, MAXScript::ScriptSource::NonEmbedded);
#else
					 ExecuteMAXScriptScript(ScriptCode);
#endif
				}
				return true;
			}
			case ID_SHOWLOG_ACTION_ID:
			{
				return true;
			}
			}
		    return false;
		}
	};

	FDatasmithActions()
		: Name(TEXT("Datasmith"))
		, Table(Name)
	{
		// todo: localization of Name

		static ActionDescription ActionsDescriptions[] = { 
			ID_SYNC_ACTION_ID, // ID
			IDS_SYNC_DESC, // Description
			IDS_SYNC_NAME, // Name
			IDS_DATASMITH_CATEGORY, // Category name

			ID_AUTOSYNC_ACTION_ID, // ID
			IDS_AUTOSYNC_DESC, // Description
			IDS_AUTOSYNC_NAME, // Name
			IDS_DATASMITH_CATEGORY, // Category name

			ID_CONNECTIONS_ACTION_ID, // ID
			IDS_CONNECTIONS_DESC, // Description
			IDS_CONNECTIONS_NAME, // Name
			IDS_DATASMITH_CATEGORY, // Category name

			ID_EXPORT_ACTION_ID, // ID
			IDS_EXPORT_DESC, // Description
			IDS_EXPORT_NAME, // Name
			IDS_DATASMITH_CATEGORY, // Category name

			ID_SHOWLOG_ACTION_ID, // ID
			IDS_SHOWLOG_DESC, // Description
			IDS_SHOWLOG_NAME, // Name
			IDS_DATASMITH_CATEGORY, // Category name
		};

		Table.BuildActionTable(nullptr, sizeof(ActionsDescriptions) / sizeof(ActionsDescriptions[0]), ActionsDescriptions, HInstanceMax);
		GetCOREInterface()->GetActionManager()->RegisterActionContext(ActionContextId, Name.data());

		// Register table - this needs to be called explicitly when action table is not returned to Max with ClassDesc's GetActionTable method
		GetCOREInterface()->GetActionManager()->RegisterActionTable(&Table);

		GetCOREInterface()->GetActionManager()->ActivateActionTable(&ActionCallback, ActionTableId);
	}

private:
	TSTR Name;
	FDatasmithActionTable Table;
	FDatasmithActionCallback ActionCallback;
};


TUniquePtr<FDatasmithActions> Actions;

Value* SetupActions_cf(Value** arg_list, int count)
{
	if (!Actions)
	{
		Actions = MakeUnique<FDatasmithActions>();
	}

	return bool_result(true);
}
Primitive SetupActions_pf(_M("Datasmith_SetupActions"), SetupActions_cf);


}

#include "Windows/HideWindowsPlatformTypes.h"

#endif // NEW_DIRECTLINK_PLUGIN
