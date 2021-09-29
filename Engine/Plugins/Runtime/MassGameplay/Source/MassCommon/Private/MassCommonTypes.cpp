// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCommonTypes.h"
#include "MassEntitySubsystem.h"
#include "Math/ColorList.h"

#if WITH_MASS_DEBUG
namespace UE { namespace MassDebug {

// First Id of a range of lightweight entity for which we want to activate debug information
int32 DebugEntityBegin = INDEX_NONE;

// Last Id of a range of lightweight entity for which we want to activate debug information
int32 DebugEntityEnd = INDEX_NONE;

static FAutoConsoleCommand SetDebugEntityRange(
	TEXT("ai.debug.mass.SetDebugEntityRange"),
	TEXT("Range of lightweight entity IDs that we want to debug.")
	TEXT("Usage: \"ai.debug.mass.SetDebugEntityRange <FirstEntity> <LastEntity>\""),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() != 2)
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: Expecting 2 parameters"));
				return;
			}

			int32 FirstID = INDEX_NONE;
			int32 LastID = INDEX_NONE;
			if (!LexTryParseString<int32>(FirstID, *Args[0]))
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: first parameter must be an integer"));
				return;
			}
			
			if (!LexTryParseString<int32>(LastID, *Args[1]))
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: second parameter must be an integer"));
				return;
			}

			DebugEntityBegin = FirstID;
			DebugEntityEnd = LastID;
		}));

static FAutoConsoleCommand SetDebugEntity(
	TEXT("ai.debug.mass.DebugEntity"),
	TEXT("ID of a lightweight entity that we want to debug.")
	TEXT("Usage: \"ai.debug.mass.DebugEntity <Entity>\""),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() != 1)
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: Expecting 1 parameter"));
				return;
			}

			int32 ID = INDEX_NONE;
			if (!LexTryParseString<int32>(ID, *Args[0]))
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("Error: parameter must be an integer"));
				return;
			}

			DebugEntityBegin = ID;
			DebugEntityEnd = ID;
		}));

static FAutoConsoleCommand ResetDebugEntity(
	TEXT("ai.debug.mass.ResetDebugEntity"),
	TEXT("Disables lightweight entities debugging.")
	TEXT("Usage: \"ai.debug.mass.ResetDebugEntity\""),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			DebugEntityBegin = INDEX_NONE;
			DebugEntityEnd = INDEX_NONE;
		}));

bool HasDebugEntities()
{
	return DebugEntityBegin != INDEX_NONE && DebugEntityEnd != INDEX_NONE;
}
	
bool IsDebuggingEntity(FLWEntity Entity, FColor* OutEntityColor)
{
	const int32 EntityIdx = Entity.Index;
	const bool bIsDebuggingEntity = (DebugEntityBegin != INDEX_NONE && DebugEntityEnd != INDEX_NONE && DebugEntityBegin <= EntityIdx && EntityIdx <= DebugEntityEnd);
	
	if (bIsDebuggingEntity && OutEntityColor != nullptr)
	{
		*OutEntityColor = GetEntityDebugColor(Entity);
	}

	return bIsDebuggingEntity;
}

FColor GetEntityDebugColor(FLWEntity Entity)
{
	const int32 EntityIdx = Entity.Index;
	return EntityIdx != INDEX_NONE ? GColorList.GetFColorByIndex(EntityIdx % GColorList.GetColorsNum()) : FColor::Black;
}

}} // namespace UE::MassDebug
#endif // WITH_MASS_DEBUG

const FMassHandle FMassHandle::InvalidHandle(UMassEntitySubsystem::InvalidEntity);

//----------------------------------------------------------------------//
// FMassUniqueFragmentCollection 
//----------------------------------------------------------------------//
void FMassUniqueFragmentCollection::Add(const FInstancedStruct& InFragment)
{
	if (InFragment.GetScriptStruct() && FragmentInstances.FindByPredicate(FSameTypeScriptStructPredicate(InFragment)) == nullptr)
	{
		FragmentInstances.Add(InFragment);
	}
}

void FMassUniqueFragmentCollection::Add(const UScriptStruct* InFragmentType)
{
	if (InFragmentType && FragmentInstances.FindByPredicate(FSameTypeScriptStructPredicate(InFragmentType)) == nullptr)
	{
		FragmentInstances.Add(FInstancedStruct(InFragmentType));
		FragmentBitSet.Add(*InFragmentType);
	}
}

void FMassUniqueFragmentCollection::Append(const FMassUniqueFragmentCollection& Other)
{
	FragmentInstances.Reserve(FragmentInstances.Num() + Other.FragmentInstances.Num());
	for (const FInstancedStruct& NewFragment : Other.FragmentInstances)
	{
		Add(NewFragment);
	}
	// not shrinking on purpose, we can take the hit
}

void FMassUniqueFragmentCollection::DebugOutputDescription(FOutputDevice& Ar) const
{
#if WITH_MASS_DEBUG
	const bool bAutoLineEnd = Ar.GetAutoEmitLineTerminator();
	Ar.SetAutoEmitLineTerminator(false);

	for (const FInstancedStruct& Fragment : FragmentInstances)
	{
		const UScriptStruct* StructType = Fragment.GetScriptStruct();
		Ar.Logf(TEXT("%s, "), *GetNameSafe(StructType));
	}
	Ar.SetAutoEmitLineTerminator(bAutoLineEnd);
#endif // WITH_MASS_DEBUG
}
