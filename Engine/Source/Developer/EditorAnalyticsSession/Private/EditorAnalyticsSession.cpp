// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorAnalyticsSession.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FEditorAnalyticsSessionModule, EditorAnalyticsSession);

namespace EditorAnalyticsDefs
{
	static const FString FalseValueString(TEXT("0"));
	static const FString TrueValueString(TEXT("1"));

	static const FString DefaultUserActivity(TEXT("Unknown"));
	static const FString UnknownProjectValueString(TEXT("UnknownProject"));

	// storage location
	static const FString StoreId(TEXT("Epic Games"));
	static const FString SessionSummarySection(TEXT("Unreal Engine/Session Summary/1_0"));
	static const FString GlobalLockName(TEXT("UE4_SessionSummary_Lock"));
	static const FString SessionListStoreKey(TEXT("SessionList"));

	// general values
	static const FString ProjectNameStoreKey(TEXT("ProjectName"));
	static const FString ProjectIDStoreKey(TEXT("ProjectID"));
	static const FString ProjectDescriptionStoreKey(TEXT("ProjectDescription"));
	static const FString ProjectVersionStoreKey(TEXT("ProjectVersion"));
	static const FString EngineVersionStoreKey(TEXT("EngineVersion"));
	static const FString PlatformProcessIDStoreKey(TEXT("PlatformProcessID"));

	// timestamps
	static const FString StartupTimestampStoreKey(TEXT("StartupTimestamp"));
	static const FString TimestampStoreKey(TEXT("Timestamp"));
	static const FString SessionDurationStoreKey(TEXT("SessionDuration"));
	static const FString Idle1MinStoreKey(TEXT("Idle1Min"));
	static const FString Idle5MinStoreKey(TEXT("Idle5Min"));
	static const FString Idle30MinStoreKey(TEXT("Idle30Min"));
	static const FString CurrentUserActivityStoreKey(TEXT("CurrentUserActivity"));
	static const FString PluginsStoreKey(TEXT("Plugins"));
	static const FString AverageFPSStoreKey(TEXT("AverageFPS"));

	// GPU details
	static const FString DesktopGPUAdapterStoreKey(TEXT("DesktopGPUAdapter"));
	static const FString RenderingGPUAdapterStoreKey(TEXT("RenderingGPUAdapter"));
	static const FString GPUVendorIDStoreKey(TEXT("GPUVendorID"));
	static const FString GPUDeviceIDStoreKey(TEXT("GPUDeviceID"));
	static const FString GRHIDeviceRevisionStoreKey(TEXT("GRHIDeviceRevision"));
	static const FString GRHIAdapterInternalDriverVersionStoreKey(TEXT("GRHIAdapterInternalDriverVersion"));
	static const FString GRHIAdapterUserDriverVersionStoreKey(TEXT("GRHIAdapterUserDriverVersion"));

	// CPU details
	static const FString TotalPhysicalRAMStoreKey(TEXT("TotalPhysicalRAM"));
	static const FString CPUPhysicalCoresStoreKey(TEXT("CPUPhysicalCores"));
	static const FString CPULogicalCoresStoreKey(TEXT("CPULogicalCores"));
	static const FString CPUVendorStoreKey(TEXT("CPUVendor"));
	static const FString CPUBrandStoreKey(TEXT("CPUBrand"));

	// OS details
	static const FString OSMajorStoreKey(TEXT("OSMajor"));
	static const FString OSMinorStoreKey(TEXT("OSMinor"));
	static const FString OSVersionStoreKey(TEXT("OSVersion"));
	static const FString bIs64BitOSStoreKey(TEXT("bIs64BitOS"));

	// boolean flags
	static const FString IsCrashStoreKey(TEXT("IsCrash"));
	static const FString IsGPUCrashStoreKey(TEXT("IsGPUCrash"));
	static const FString IsDebuggerStoreKey(TEXT("IsDebugger"));
	static const FString WasDebuggerStoreKey(TEXT("WasEverDebugger"));
	static const FString IsVanillaStoreKey(TEXT("IsVanilla"));
	static const FString IsTerminatingKey(TEXT("Terminating"));
	static const FString WasShutdownStoreKey(TEXT("WasShutdown"));
	static const FString IsInPIEStoreKey(TEXT("IsInPIE"));
	static const FString IsInEnterpriseStoreKey(TEXT("IsInEnterprise"));
	static const FString IsInVRModeStoreKey(TEXT("IsInVRMode"));
}

// Utilities for writing to stored values
namespace EditorAnalyticsUtils
{
	static FString TimestampToString(FDateTime InTimestamp)
	{
		return LexToString(InTimestamp.ToUnixTimestamp());
	}

	static FDateTime StringToTimestamp(FString InString)
	{
		int64 TimestampUnix;
		if (LexTryParseString(TimestampUnix, *InString))
		{
			return FDateTime::FromUnixTimestamp(TimestampUnix);
		}
		return FDateTime::MinValue();
	}

	static FString BoolToStoredString(bool bInValue)
	{
		return bInValue ? EditorAnalyticsDefs::TrueValueString : EditorAnalyticsDefs::FalseValueString;
	}

	static bool GetStoredBool(const FString& SectionName, const FString& StoredKey)
	{
		FString StoredString = EditorAnalyticsDefs::FalseValueString;
		FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, StoredKey, StoredString);

		return StoredString == EditorAnalyticsDefs::TrueValueString;
	}

	static FString GetSessionStorageLocation(const FString& SessionID)
	{
		return EditorAnalyticsDefs::SessionSummarySection + TEXT("/") + SessionID;
	}

// Utility macros to make it easier to check that all fields are being written to.
#define GET_STORED_STRING(FieldName) FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs:: FieldName ## StoreKey, Session.FieldName)
#define GET_STORED_INT(FieldName) \
	{ \
		FString FieldName ## Temp; \
		FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs:: FieldName ## StoreKey, FieldName ## Temp); \
		Session.FieldName = FCString::Atoi(*FieldName ## Temp); \
	}
	

	static void LoadInternal(FEditorAnalyticsSession& Session, const FString& InSessionId)
	{
		Session.SessionId = InSessionId;

		FString SectionName = EditorAnalyticsUtils::GetSessionStorageLocation(Session.SessionId);

		GET_STORED_STRING(ProjectName);
		GET_STORED_STRING(ProjectID);
		GET_STORED_STRING(ProjectDescription);
		GET_STORED_STRING(ProjectVersion);
		GET_STORED_STRING(EngineVersion);

		GET_STORED_INT(PlatformProcessID);

		// scope is just to isolate the temporary value
		{
			FString StartupTimestampString;
			FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::StartupTimestampStoreKey, StartupTimestampString);
			Session.StartupTimestamp = EditorAnalyticsUtils::StringToTimestamp(StartupTimestampString);
		}

		{
			FString TimestampString;
			FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::TimestampStoreKey, TimestampString);
			Session.Timestamp = EditorAnalyticsUtils::StringToTimestamp(TimestampString);
		}

		GET_STORED_INT(SessionDuration);
		GET_STORED_INT(Idle1Min);
		GET_STORED_INT(Idle5Min);
		GET_STORED_INT(Idle30Min);

		GET_STORED_STRING(CurrentUserActivity);

		{
			FString PluginsString;
			FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::PluginsStoreKey, PluginsString);
			PluginsString.ParseIntoArray(Session.Plugins, TEXT(","));
		}

		{
			FString AverageFPSString;
			FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::AverageFPSStoreKey, AverageFPSString);
			Session.AverageFPS = FCString::Atof(*AverageFPSString);
		}

		GET_STORED_STRING(DesktopGPUAdapter);
		GET_STORED_STRING(RenderingGPUAdapter);

		GET_STORED_INT(GPUVendorID);
		GET_STORED_INT(GPUDeviceID);
		GET_STORED_INT(GRHIDeviceRevision);

		GET_STORED_STRING(GRHIAdapterInternalDriverVersion);
		GET_STORED_STRING(GRHIAdapterUserDriverVersion);

		{
			FString TotalPhysicalRAMString;
			FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::GRHIAdapterUserDriverVersionStoreKey, TotalPhysicalRAMString);
			Session.TotalPhysicalRAM = FCString::Atoi64(*TotalPhysicalRAMString);
		}

		GET_STORED_INT(CPUPhysicalCores);
		GET_STORED_INT(CPULogicalCores);

		GET_STORED_STRING(CPUVendor);
		GET_STORED_STRING(CPUBrand);

		GET_STORED_STRING(OSMajor);
		GET_STORED_STRING(OSMinor);
		GET_STORED_STRING(OSVersion);

		Session.bIs64BitOS = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::bIs64BitOSStoreKey);
		Session.bCrashed = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsCrashStoreKey);
		Session.bGPUCrashed = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsGPUCrashStoreKey);
		Session.bIsDebugger = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsDebuggerStoreKey);
		Session.bWasEverDebugger = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::WasDebuggerStoreKey);
		Session.bIsVanilla = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsVanillaStoreKey);
		Session.bIsTerminating = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsTerminatingKey);
		Session.bWasShutdown = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::WasShutdownStoreKey);
		Session.bIsInPIE = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsInPIEStoreKey);
		Session.bIsInVRMode = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsInVRModeStoreKey);
		Session.bIsInEnterprise = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsInEnterpriseStoreKey);
	}

#undef GET_STORED_INT
#undef GET_STORED_STRING

	static TArray<FString> GetSessionList()
	{
		FString SessionListString;
		FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, EditorAnalyticsDefs::SessionSummarySection, EditorAnalyticsDefs::SessionListStoreKey, SessionListString);

		TArray<FString> SessionIDs;
		SessionListString.ParseIntoArray(SessionIDs, TEXT(","));

		return MoveTemp(SessionIDs);
	}
}

FSystemWideCriticalSection* FEditorAnalyticsSession::StoredValuesLock = nullptr;

FEditorAnalyticsSession::FEditorAnalyticsSession()
{
	ProjectName = EditorAnalyticsDefs::UnknownProjectValueString;
	PlatformProcessID = 0;
	StartupTimestamp = FDateTime::MinValue();
	Timestamp = FDateTime::MinValue();
	SessionDuration = 0;
	Idle1Min = 0;
	Idle5Min = 0;
	Idle30Min = 0;
	CurrentUserActivity = EditorAnalyticsDefs::DefaultUserActivity;
	AverageFPS = 0;
	GPUVendorID = 0;
	GPUDeviceID = 0;
	GRHIDeviceRevision = 0;
	TotalPhysicalRAM = 0;
	CPUPhysicalCores = 0;
	CPULogicalCores = 0;

	bIs64BitOS = false;
	bCrashed = false;
	bGPUCrashed = false;
	bIsDebugger = false;
	bWasEverDebugger = false;
	bIsVanilla = false;
	bIsTerminating = false;
	bWasShutdown = false;
	bIsInPIE = false;
	bIsInEnterprise = false;
	bIsInVRMode = false;
	bAlreadySaved = false;
}

bool FEditorAnalyticsSession::Lock(FTimespan Timeout)
{
	if (!ensure(!IsLocked()))
	{
		return true;
	}
	
	StoredValuesLock = new FSystemWideCriticalSection(EditorAnalyticsDefs::GlobalLockName, Timeout);

	if (!IsLocked())
	{
		delete StoredValuesLock;
		StoredValuesLock = nullptr;

		return false;
	}

	return true;
}

void FEditorAnalyticsSession::Unlock()
{
	if (!ensure(IsLocked()))
	{
		return;
	}

	delete StoredValuesLock;
	StoredValuesLock = nullptr;
}

bool FEditorAnalyticsSession::IsLocked()
{
	return StoredValuesLock != nullptr && StoredValuesLock->IsValid();
}

// Utility macros to make it easier to see that all values are correctly set
#define SET_STORED_STRING(FieldName) FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::FieldName ## StoreKey, FieldName)
#define SET_STORED_INT(FieldName) FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::FieldName ## StoreKey, FString::FromInt(FieldName))

bool FEditorAnalyticsSession::Save()
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	const FString StorageLocation = EditorAnalyticsUtils::GetSessionStorageLocation(SessionId);

	if (!bAlreadySaved)
	{
		SET_STORED_STRING(EngineVersion);
		SET_STORED_INT(PlatformProcessID);
		SET_STORED_STRING(DesktopGPUAdapter);
		SET_STORED_STRING(RenderingGPUAdapter);
		SET_STORED_INT(GPUVendorID);
		SET_STORED_INT(GPUDeviceID);
		SET_STORED_INT(GRHIDeviceRevision);
		SET_STORED_STRING(GRHIAdapterInternalDriverVersion);
		SET_STORED_STRING(GRHIAdapterUserDriverVersion);

		FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::TotalPhysicalRAMStoreKey, FString::Printf(TEXT("%llu"), TotalPhysicalRAM));

		SET_STORED_INT(CPUPhysicalCores);
		SET_STORED_INT(CPULogicalCores);
		SET_STORED_STRING(CPUVendor);
		SET_STORED_STRING(CPUBrand);

		FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::StartupTimestampStoreKey, EditorAnalyticsUtils::TimestampToString(StartupTimestamp));

		SET_STORED_STRING(OSMajor);
		SET_STORED_STRING(OSMinor);
		SET_STORED_STRING(OSVersion);

		FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::bIs64BitOSStoreKey, EditorAnalyticsUtils::BoolToStoredString(bIs64BitOS));

		const FString PluginsString = FString::Join(Plugins, TEXT(","));
		FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::PluginsStoreKey, PluginsString);

		bAlreadySaved = true;
	}

	SET_STORED_STRING(ProjectName);
	SET_STORED_STRING(ProjectID);
	SET_STORED_STRING(ProjectDescription);
	SET_STORED_STRING(ProjectVersion);

	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::TimestampStoreKey, EditorAnalyticsUtils::TimestampToString(Timestamp));

	SET_STORED_INT(SessionDuration);
	SET_STORED_INT(Idle1Min);
	SET_STORED_INT(Idle5Min);
	SET_STORED_INT(Idle30Min);

	SET_STORED_STRING(CurrentUserActivity);

	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::AverageFPSStoreKey, FString::SanitizeFloat(AverageFPS));
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::IsCrashStoreKey, EditorAnalyticsUtils::BoolToStoredString(bCrashed));
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::IsGPUCrashStoreKey, EditorAnalyticsUtils::BoolToStoredString(bGPUCrashed));
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::IsDebuggerStoreKey, EditorAnalyticsUtils::BoolToStoredString(bIsDebugger));
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::WasDebuggerStoreKey, EditorAnalyticsUtils::BoolToStoredString(bWasEverDebugger));
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::IsVanillaStoreKey, EditorAnalyticsUtils::BoolToStoredString(bIsVanilla));
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::IsTerminatingKey, EditorAnalyticsUtils::BoolToStoredString(bIsTerminating));
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::WasShutdownStoreKey, EditorAnalyticsUtils::BoolToStoredString(bWasShutdown));
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::IsInPIEStoreKey, EditorAnalyticsUtils::BoolToStoredString(bIsInPIE));
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::IsInEnterpriseStoreKey, EditorAnalyticsUtils::BoolToStoredString(bIsInEnterprise));
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::IsInVRModeStoreKey, EditorAnalyticsUtils::BoolToStoredString(bIsInVRMode));

	return true;
}

bool FEditorAnalyticsSession::SaveForCrash()
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	// These ini writes are causing MallocCrash to go over its LARGE_MEMORYPOOL_SIZE due to writing to ini files causing a potentially a few large allocations per write
	if (!PLATFORM_UNIX)
	{
		const FString StorageLocation = EditorAnalyticsUtils::GetSessionStorageLocation(SessionId);
		
		FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::IsCrashStoreKey, EditorAnalyticsUtils::BoolToStoredString(bCrashed));
		FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::IsGPUCrashStoreKey, EditorAnalyticsUtils::BoolToStoredString(bGPUCrashed));
		FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::IsTerminatingKey, EditorAnalyticsUtils::BoolToStoredString(bIsTerminating));
		FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::WasShutdownStoreKey, EditorAnalyticsUtils::BoolToStoredString(bWasShutdown));
		FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::TimestampStoreKey, EditorAnalyticsUtils::TimestampToString(Timestamp));
		FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::SessionDurationStoreKey, FString::FromInt(SessionDuration));
	}

	return true;
}

#undef SET_STORED_INT
#undef SET_STORED_STRING

bool FEditorAnalyticsSession::Load(const FString& InSessionID)
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	EditorAnalyticsUtils::LoadInternal(*this, InSessionID);
	bAlreadySaved = false;

	return true;
}

bool FEditorAnalyticsSession::Delete() const
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	FString SectionName = EditorAnalyticsUtils::GetSessionStorageLocation(SessionId);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::ProjectNameStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::ProjectIDStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::ProjectDescriptionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::ProjectVersionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::EngineVersionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::PlatformProcessIDStoreKey);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::StartupTimestampStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::TimestampStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::SessionDurationStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::Idle1MinStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::Idle5MinStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::Idle30MinStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::CurrentUserActivityStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::PluginsStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::AverageFPSStoreKey);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::DesktopGPUAdapterStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::RenderingGPUAdapterStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::GPUVendorIDStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::GPUDeviceIDStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::GRHIDeviceRevisionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::GRHIAdapterInternalDriverVersionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::GRHIAdapterUserDriverVersionStoreKey);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::TotalPhysicalRAMStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::CPUPhysicalCoresStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::CPULogicalCoresStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::CPUVendorStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::CPUBrandStoreKey);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::OSMajorStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::OSMinorStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::OSVersionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::bIs64BitOSStoreKey);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsCrashStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsGPUCrashStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsDebuggerStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::WasDebuggerStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsVanillaStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsTerminatingKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::WasShutdownStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsInPIEStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsInEnterpriseStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsInVRModeStoreKey);

	return true;
}

bool FEditorAnalyticsSession::GetStoredSessionIDs(TArray<FString>& OutSessions)
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	OutSessions = EditorAnalyticsUtils::GetSessionList();
	return true;
}

bool FEditorAnalyticsSession::LoadAllStoredSessions(TArray<FEditorAnalyticsSession>& OutSessions)
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	TArray<FString> SessionIDs = EditorAnalyticsUtils::GetSessionList();

	// Retrieve all the sessions in the list from storage
	for (const FString& Id : SessionIDs)
	{
		FEditorAnalyticsSession NewSession;
		EditorAnalyticsUtils::LoadInternal(NewSession, Id);

		OutSessions.Add(MoveTemp(NewSession));
	}

	return true;
}

bool FEditorAnalyticsSession::SaveStoredSessionIDs(const TArray<FString>& InSessions)
{
	// build up a new SessionList string
	FString SessionListString;
	for (const FString& Session : InSessions)
	{
		if (!SessionListString.IsEmpty())
		{
			SessionListString.Append(TEXT(","));
		}

		SessionListString.Append(Session);
	}

	if (!ensure(IsLocked()))
	{
		return false;
	}

	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, EditorAnalyticsDefs::SessionSummarySection, EditorAnalyticsDefs::SessionListStoreKey, SessionListString);
	return true;
}
