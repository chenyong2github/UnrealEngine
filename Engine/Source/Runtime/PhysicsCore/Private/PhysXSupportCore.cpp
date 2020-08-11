// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysXSupportCore.h"

#if PHYSICS_INTERFACE_PHYSX
#include "PhysicsPublicCore.h"
#include "PhysXSupportCore.h"
#include "HAL/IConsoleManager.h"

PxFoundation* GPhysXFoundation = nullptr;
FPhysXAllocator* GPhysXAllocator = nullptr;
PxPvd* GPhysXVisualDebugger = nullptr;
physx::PxPhysics* GPhysXSDK = nullptr;

TArray<PxMaterial*> GPhysXPendingKillMaterial;

#if WITH_APEX
PHYSICSCORE_API apex::ApexSDK* GApexSDK = nullptr;

#if WITH_APEX_LEGACY
PHYSICSCORE_API apex::Module* GApexModuleLegacy = nullptr;
#endif

#if WITH_APEX_CLOTHING
PHYSICSCORE_API apex::ModuleClothing* GApexModuleClothing = nullptr;
#endif
#endif

#if WITH_APEX
FApexNullRenderResourceManager		GApexNullRenderResourceManager;
FApexResourceCallback				GApexResourceCallback;
#endif	// #if WITH_APEX


int32 GPhysXHackLoopCounter = -1;
FAutoConsoleVariableRef CVarHackLoopCounter(TEXT("p.TriMeshBufferOverflowCounter"), GPhysXHackLoopCounter, TEXT("Loop logging counter - set to -1 to disable logging"), ECVF_Default);


/** Util to convert PhysX error code to string */
FString ErrorCodeToString(PxErrorCode::Enum e)
{
	FString CodeString;

	switch (e)
	{
	case PxErrorCode::eNO_ERROR:
		CodeString = TEXT("eNO_ERROR");
		break;
	case PxErrorCode::eDEBUG_INFO:
		CodeString = TEXT("eDEBUG_INFO");
		break;
	case PxErrorCode::eDEBUG_WARNING:
		CodeString = TEXT("eDEBUG_WARNING");
		break;
	case PxErrorCode::eINVALID_PARAMETER:
		CodeString = TEXT("eINVALID_PARAMETER");
		break;
	case PxErrorCode::eINVALID_OPERATION:
		CodeString = TEXT("eINVALID_OPERATION");
		break;
	case PxErrorCode::eOUT_OF_MEMORY:
		CodeString = TEXT("eOUT_OF_MEMORY");
		break;
	case PxErrorCode::eINTERNAL_ERROR:
		CodeString = TEXT("eINTERNAL_ERROR");
		break;
	case PxErrorCode::eABORT:
		CodeString = TEXT("eABORT");
		break;
	case PxErrorCode::ePERF_WARNING:
		CodeString = TEXT("ePERF_WARNING");
		break;
	case PxErrorCode::eLOGGING_INFO:
		CodeString = TEXT("eLOGGING_INFO");
		break;
	default:
		CodeString = TEXT("UNKONWN");
	}

	return CodeString;
}


int32 GPhysXHackCurrentLoopCounter = 0;

void FPhysXErrorCallback::reportError(PxErrorCode::Enum e, const char* message, const char* file, int line)
{
	// if not in game, ignore Perf warnings - i.e. Moving Static actor in editor will produce this warning
	if (GIsEditor && e == PxErrorCode::ePERF_WARNING)
	{
		return;
	}

	if (e == PxErrorCode::eLOGGING_INFO)
	{
		if (GPhysXHackLoopCounter == -1)
		{
			return;
		}
		GPhysXHackCurrentLoopCounter++;
		if (GPhysXHackCurrentLoopCounter <= GPhysXHackLoopCounter)
		{
			return;
		}
	}

	if (e == PxErrorCode::eINTERNAL_ERROR)
	{
		const char* HillClimbError = "HillClimbing";
		const char* TestSATCapsulePoly = "testSATCapsulePoly";
		const char* MeshCleanFailed = "cleaning the mesh failed";

		// HACK: Internal errors which we want to suppress in release builds should be changed to debug warning error codes.
		// This way we see them in debug but not in production.
		if (FPlatformString::Strstr(message, MeshCleanFailed))
		{
			e = PxErrorCode::eDEBUG_WARNING;
		}
	}
	// Make string to print out, include physx file/line
	FString ErrorString = FString::Printf(TEXT("PHYSX: (%s %d) %s : %s"), ANSI_TO_TCHAR(file), line, *ErrorCodeToString(e), ANSI_TO_TCHAR(message));

	if (e == PxErrorCode::eOUT_OF_MEMORY || e == PxErrorCode::eABORT)
	{
		UE_LOG(LogPhysicsCore, Error, TEXT("%s"), *ErrorString);
		//ensureMsgf(false, TEXT("%s"), *ErrorString);
	}
	else if (e == PxErrorCode::eINVALID_PARAMETER || e == PxErrorCode::eINVALID_OPERATION)
	{
		UE_LOG(LogPhysicsCore, Error, TEXT("%s"), *ErrorString);
		//ensureMsgf(false, TEXT("%s"), *ErrorString);
	}
	else if (e == PxErrorCode::ePERF_WARNING || e == PxErrorCode::eINTERNAL_ERROR || e == PxErrorCode::eLOGGING_INFO)
	{
		UE_LOG(LogPhysicsCore, Warning, TEXT("%s"), *ErrorString);
	}
#if UE_BUILD_DEBUG
	else if (e == PxErrorCode::eDEBUG_WARNING)
	{
		UE_LOG(LogPhysicsCore, Warning, TEXT("%s"), *ErrorString);
	}
#endif
	else
	{
		UE_LOG(LogPhysicsCore, Log, TEXT("%s"), *ErrorString);
	}
}

FPhysxSharedData* FPhysxSharedData::Singleton = nullptr;

void FPhysxSharedData::Initialize()
{
	check(Singleton == nullptr);
	Singleton = new FPhysxSharedData();
}

void FPhysxSharedData::Terminate()
{
	if (Singleton)
	{
		delete Singleton;
		Singleton = nullptr;
	}
}

void FPhysxSharedData::LockAccess()
{
	if (Singleton)
	{
		Singleton->CriticalSection.Lock();
	}
}

void FPhysxSharedData::UnlockAccess()
{
	if (Singleton)
	{
		Singleton->CriticalSection.Unlock();
	}
}

void FPhysxSharedData::Add( PxBase* Obj, const FString& OwnerName )
{
	if(Obj) 
	{ 
		SharedObjects->add(*Obj, (PxSerialObjectId)Obj);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		OwnerNames.Add(Obj, OwnerName);
#endif
	}
}

void FPhysxSharedData::Remove(PxBase* Obj) 
{ 
	// Check for containment first due to multiple UBodySetups sharing the same ref counted object causing harmless double-frees
	if (Obj && SharedObjects->contains(*Obj)) 
	{ 
		SharedObjects->remove(*Obj); 
		OwnerNames.Remove(Obj); 
	} 
}	

SIZE_T GetPhysxObjectSize(PxBase* Obj, const PxCollection* SharedCollection)
{
	PxSerializationRegistry* Sr = PxSerialization::createSerializationRegistry(*GPhysXSDK);
	PxCollection* Collection = PxCreateCollection();

	Collection->add(*Obj);
	PxSerialization::complete(*Collection, *Sr, SharedCollection);	// chase all other stuff (shared shaps, materials, etc) needed to serialize this collection

	FPhysXCountMemoryStream Out;
	PxSerialization::serializeCollectionToBinary(Out, *Collection, *Sr, SharedCollection);

	Collection->release();
	Sr->release();

	return Out.UsedMemory;
}

struct FSharedResourceEntry
{
	uint64 MemorySize;
	uint64 Count;
};

void HelperCollectUsage(const TMap<FString, TArray<PxBase*> >& ObjectsByType, TMap<FString, FSharedResourceEntry>& AllocationsByType, uint64& OverallSize, int32& OverallCount)
{
	TArray<FString> TypeNames;
	ObjectsByType.GetKeys(TypeNames);

	for (int32 TypeIdx = 0; TypeIdx < TypeNames.Num(); ++TypeIdx)
	{
		const FString& TypeName = TypeNames[TypeIdx];

		const TArray<PxBase*>* ObjectsArray = ObjectsByType.Find(TypeName);
		check(ObjectsArray);

		PxSerializationRegistry* Sr = PxSerialization::createSerializationRegistry(*GPhysXSDK);
		PxCollection* Collection = PxCreateCollection();

		for (int32 i = 0; i < ObjectsArray->Num(); ++i)
		{
			Collection->add(*((*ObjectsArray)[i]));;
		}

		PxSerialization::complete(*Collection, *Sr);	// chase all other stuff (shared shaps, materials, etc) needed to serialize this collection

		FPhysXCountMemoryStream Out;
		PxSerialization::serializeCollectionToBinary(Out, *Collection, *Sr);

		Collection->release();
		Sr->release();

		OverallSize += Out.UsedMemory;
		OverallCount += ObjectsArray->Num();

		FSharedResourceEntry NewEntry;
		NewEntry.Count = ObjectsArray->Num();
		NewEntry.MemorySize = Out.UsedMemory;

		AllocationsByType.Add(TypeName, NewEntry);
	}
}

void FPhysxSharedData::DumpSharedMemoryUsage(FOutputDevice* Ar)
{
	

	struct FSortBySize
	{
		FORCEINLINE bool operator()( const FSharedResourceEntry& A, const FSharedResourceEntry& B ) const 
		{ 
			// Sort descending
			return B.MemorySize < A.MemorySize;
		}
	};

	TMap<FString, FSharedResourceEntry> AllocationsByType;
	TMap<FString, FSharedResourceEntry> AllocationsByObject;

	uint64 OverallSize = 0;
	int32 OverallCount = 0;

	TMap<FString, TArray<PxBase*> > ObjectsByType;
	TMap<FString, TArray<PxBase*> > ObjectsByObjectName;	//array is just there for code reuse, should be a single object

	for (int32 i=0; i < (int32)SharedObjects->getNbObjects(); ++i)
	{
		PxBase& Obj = SharedObjects->getObject(i);
		FString TypeName = ANSI_TO_TCHAR(Obj.getConcreteTypeName());

		TArray<PxBase*>* ObjectsArray = ObjectsByType.Find(TypeName);
		if (ObjectsArray == NULL)
		{
			ObjectsByType.Add(TypeName, TArray<PxBase*>());
			ObjectsArray = ObjectsByType.Find(TypeName);
		}

		check(ObjectsArray);
		ObjectsArray->Add(&Obj);

		if (const FString* OwnerName = OwnerNames.Find(&Obj))
		{
			TArray<PxBase*> Objs;
			Objs.Add(&Obj);
			ObjectsByObjectName.Add(*OwnerName, Objs);
		}
	}

	HelperCollectUsage(ObjectsByType, AllocationsByType, OverallSize, OverallCount);

	uint64 IgnoreSize;
	int32 IgnoreCount;
	HelperCollectUsage(ObjectsByObjectName, AllocationsByObject, IgnoreSize, IgnoreCount);


	Ar->Logf(TEXT(""));
	Ar->Logf(TEXT("Shared Resources:"));
	Ar->Logf(TEXT(""));

	AllocationsByType.ValueSort(FSortBySize());
	AllocationsByObject.ValueSort(FSortBySize());
	
	Ar->Logf(TEXT("%-10d %s (%d)"), OverallSize, TEXT("Overall"), OverallCount );
	
	for( auto It=AllocationsByType.CreateConstIterator(); It; ++It )
	{
		Ar->Logf(TEXT("%-10d %s (%d)"), It.Value().MemorySize, *It.Key(), It.Value().Count );
	}

	Ar->Logf(TEXT("Detailed:"));

	for (auto It = AllocationsByObject.CreateConstIterator(); It; ++It)
	{
		Ar->Logf(TEXT("%-10d %s (%d)"), It.Value().MemorySize, *It.Key(), It.Value().Count);
	}
}

void PvdConnect(FString Host, bool bVisualization)
{
	int32	Port = 5425;         // TCP port to connect to, where PVD is listening
	uint32	Timeout = 100;          // timeout in milliseconds to wait for PVD to respond, consoles and remote PCs need a higher timeout.

	PxPvdInstrumentationFlags ConnectionFlags = bVisualization ? PxPvdInstrumentationFlag::eALL : (PxPvdInstrumentationFlag::ePROFILE | PxPvdInstrumentationFlag::eMEMORY);

	PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate(TCHAR_TO_ANSI(*Host), Port, Timeout);
	GPhysXVisualDebugger->disconnect();	//make sure we're disconnected first
	GPhysXVisualDebugger->connect(*transport, ConnectionFlags);

	// per scene properties (e.g. PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS) are 
	// set on the PxPvdSceneClient in PhysScene.cpp, FPhysScene::InitPhysScene
}
#endif // WITH_PHYSX
