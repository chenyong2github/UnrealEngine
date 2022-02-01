// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#ifdef CADKERNEL_DEV
#include "CADKernel/Core/Types.h"
#else
#include "CoreMinimal.h"
#endif

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

THIRD_PARTY_INCLUDES_START
#ifdef USE_TECHSOFT_SDK
#pragma push_macro("TEXT")
#include "A3DSDKIncludes.h"
#pragma pop_macro("TEXT")
#endif
THIRD_PARTY_INCLUDES_END

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define JSON_ENTRY_FILE_UNIT		TEXT("FileUnit")
#define JSON_ENTRY_COLOR_NAME		TEXT("ColorName")
#define JSON_ENTRY_MATERIAL_NAME	TEXT("MaterialName")

namespace CADLibrary
{
	class FImportParameters;
	class FBodyMesh;

CADINTERFACES_API class FTechSoftInterface
{
public:
	/*
	* Returns true if the object has been created outside of the memory pool of the running process
	* This is the case when the object has been created by the DatasmithRuntime plugin
	*/
	CADINTERFACES_API bool IsExternal()
	{
		return bIsExternal;
	}

	CADINTERFACES_API void SetExternal(bool Value)
	{
		bIsExternal = Value;
	}

	CADINTERFACES_API bool InitializeKernel(const TCHAR* = TEXT(""));

#ifdef USE_TECHSOFT_SDK
	A3DStatus Import(const A3DImport& Import);
	A3DAsmModelFile* GetModelFile();
	A3DStatus UnloadModel();

	static A3DUns32 InvalidScriptIndex;
#endif

	CADINTERFACES_API bool GetBodyFromHsfFile(const FString& Filename, const FImportParameters& ImportParameters, FBodyMesh& BodyMesh);

	bool FillBodyMesh(void* BodyPtr, const FImportParameters& ImportParameters, double FileUnit, FBodyMesh& BodyMesh);

	void SaveBodyToHsfFile(void* BodyPtr, const FString& Filename, const FString& JsonString);

private:

	bool bIsExternal = false;
	bool bIsInitialize = false;

#ifdef USE_TECHSOFT_SDK
	TUniquePtr<A3DSDKHOOPSExchangeLoader> ExchangeLoader;
#endif
};

namespace TechSoftUtils
{
CADINTERFACES_API bool TECHSOFT_InitializeKernel(const TCHAR* = TEXT(""));
CADINTERFACES_API FTechSoftInterface& GetTechSoftInterface();
CADINTERFACES_API FString GetTechSoftVersion();

#ifdef USE_TECHSOFT_SDK
FColor GetColorAt(uint32 ColorIndex);

CADINTERFACES_API A3DStatus GetGlobalPointer(A3DGlobal** GlobalPtr);

CADINTERFACES_API A3DStatus GetSurfaceAsNurbs(const A3DSurfBase* SurfacePtr, A3DSurfNurbsData* DataPtr, A3DDouble Tolerance, A3DBool bUseSameParameterization);
CADINTERFACES_API A3DStatus GetCurveAsNurbs(const A3DCrvBase* A3DCurve, A3DCrvNurbsData* DataPtr, A3DDouble Tolerance, A3DBool bUseSameParameterization);

CADINTERFACES_API A3DStatus GetOriginalFilePathName(const A3DAsmProductOccurrence* A3DOccurrencePtr, A3DUTF8Char** FilePathUTF8Ptr);
CADINTERFACES_API A3DStatus GetFilePathName(const A3DAsmProductOccurrence* A3DOccurrencePtr, A3DUTF8Char** FilePathUTF8Ptr);

CADINTERFACES_API A3DStatus GetEntityType(const A3DEntity* pEntity, A3DEEntityType* peEntityType);
CADINTERFACES_API bool IsEntityBaseWithGraphicsType(const A3DEntity* pEntity);
CADINTERFACES_API bool IsEntityBaseType(const A3DEntity* EntityPtr);
CADINTERFACES_API bool IsMaterialTexture(const uint32 MaterialIndex);

CADINTERFACES_API A3DStatus HealBRep(A3DRiBrepModel** BRepToHeal, double Tolerance, A3DSewOptionsData const* SewOptions, A3DRiBrepModel*** OutNewBReps, uint32& OutNewBRepCount);
CADINTERFACES_API A3DStatus SewBReps(A3DRiBrepModel*** BRepsToSew, uint32 const BRepCount, double Tolerance, A3DSewOptionsData const* SewOptions, A3DRiBrepModel*** OutNewBReps, uint32& OutNewBRepCount);
CADINTERFACES_API A3DStatus SewModel(A3DAsmModelFile** ModelPtr, double Tolerance, A3DSewOptionsData const* SewOptions);

CADINTERFACES_API A3DEntity* GetPointerFromIndex(const uint32 Index, const A3DEEntityType Type);

#endif

} // NS TechSoftUtils

}

