// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RepLayout.cpp: Unreal replication layout implementation.
=============================================================================*/

#include "Net/RepLayout.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"
#include "EngineStats.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Engine/PackageMapClient.h"
#include "Engine/NetConnection.h"
#include "Net/NetworkProfiler.h"
#include "Engine/ActorChannel.h"
#include "Engine/NetworkSettings.h"
#include "HAL/LowLevelMemTracker.h"
#include "Misc/NetworkVersion.h"
#include "Misc/App.h"
#include "Algo/Sort.h"
#include "Net/NetworkGranularMemoryLogging.h"
#include "Serialization/ArchiveCountMem.h"
#include "Templates/AndOrNot.h"

DECLARE_CYCLE_STAT(TEXT("RepLayout AddPropertyCmd"), STAT_RepLayout_AddPropertyCmd, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("RepLayout InitFromObjectClass"), STAT_RepLayout_InitFromObjectClass, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("RepLayout BuildShadowOffsets"), STAT_RepLayout_BuildShadowOffsets, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("RepLayout DeltaSerializeFastArray"), STAT_RepLayout_DeltaSerializeFastArray, STATGROUP_Game);

// LogRepProperties is very spammy, and the logs are in a very hot code path,
// so prevent anything less than a warning from even being compiled in on
// test and shipping builds.
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
DEFINE_LOG_CATEGORY_STATIC(LogRepProperties, Warning, All);
#else
DEFINE_LOG_CATEGORY_STATIC(LogRepProperties, Warning, Warning);
#endif

int32 GDoPropertyChecksum = 0;
static FAutoConsoleVariableRef CVarDoPropertyChecksum(TEXT("net.DoPropertyChecksum"), GDoPropertyChecksum, TEXT(""));

int32 GDoReplicationContextString = 0;
static FAutoConsoleVariableRef CVarDoReplicationContextString(TEXT("net.ContextDebug"), GDoReplicationContextString, TEXT(""));

int32 GNetSharedSerializedData = 1;
static FAutoConsoleVariableRef CVarNetShareSerializedData(TEXT("net.ShareSerializedData"), GNetSharedSerializedData, TEXT(""));

int32 GNetVerifyShareSerializedData = 0;
static FAutoConsoleVariableRef CVarNetVerifyShareSerializedData(TEXT("net.VerifyShareSerializedData"), GNetVerifyShareSerializedData, TEXT(""));

int32 LogSkippedRepNotifies = 0;
static FAutoConsoleVariable CVarLogSkippedRepNotifies(TEXT("Net.LogSkippedRepNotifies"), LogSkippedRepNotifies, TEXT("Log when the networking code skips calling a repnotify clientside due to the property value not changing."), ECVF_Default);

int32 GUsePackedShadowBuffers = 1;
static FAutoConsoleVariableRef CVarUsePackedShadowBuffers(TEXT("Net.UsePackedShadowBuffers"), GUsePackedShadowBuffers, TEXT("When enabled, FRepLayout will generate shadow buffers that are packed with only the necessary NetProperties, instead of copying entire object state."));

int32 GShareShadowState = 1;
static FAutoConsoleVariableRef CVarShareShadowState(TEXT("net.ShareShadowState"), GShareShadowState, TEXT("If true, work done to compare properties will be shared across connections"));

int32 GShareInitialCompareState = 0;
static FAutoConsoleVariableRef CVarShareInitialCompareState(TEXT("net.ShareInitialCompareState"), GShareInitialCompareState, TEXT("If true and net.ShareShadowState is enabled, attempt to also share initial replication compares across connections."));

int32 MaxRepArraySize = UNetworkSettings::DefaultMaxRepArraySize;
int32 MaxRepArrayMemory = UNetworkSettings::DefaultMaxRepArrayMemory;

extern int32 GNumSharedSerializationHit;
extern int32 GNumSharedSerializationMiss;

extern TAutoConsoleVariable<int32> CVarNetEnableDetailedScopeCounters;

FConsoleVariableSinkHandle CreateMaxArraySizeCVarAndRegisterSink()
{
	static FAutoConsoleVariable CVarMaxArraySize(TEXT("net.MaxRepArraySize"), MaxRepArraySize, TEXT("Maximum allowable size for replicated dynamic arrays (in number of elements). Value must be between 1 and 65535."));
	static FConsoleCommandDelegate Delegate = FConsoleCommandDelegate::CreateLambda(
		[]()
		{
			const int32 NewMaxRepArraySizeValue = CVarMaxArraySize->GetInt();

			if ((int32)UINT16_MAX < NewMaxRepArraySizeValue || 1 > NewMaxRepArraySizeValue)
			{
				UE_LOG(LogRepTraffic, Error,
					TEXT("SerializeProperties_DynamicArray_r: MaxRepArraySize (%l) must be between 1 and 65535. Cannot accept new value."),
					NewMaxRepArraySizeValue);

				// Use SetByConsole to guarantee the value gets updated.
				CVarMaxArraySize->Set(MaxRepArraySize, ECVF_SetByConsole);
			}
			else
			{
				MaxRepArraySize = NewMaxRepArraySizeValue;
			}
		}
	);

	return IConsoleManager::Get().RegisterConsoleVariableSink_Handle(Delegate);
}

FConsoleVariableSinkHandle CreateMaxArrayMemoryCVarAndRegisterSink()
{
	static FAutoConsoleVariableRef CVarMaxArrayMemory(TEXT("net.MaxRepArrayMemory"), MaxRepArrayMemory, TEXT("Maximum allowable size for replicated dynamic arrays (in bytes). Value must be between 1 and 65535"));
	static FConsoleCommandDelegate Delegate = FConsoleCommandDelegate::CreateLambda(
		[]()
		{
			const int32 NewMaxRepArrayMemoryValue = CVarMaxArrayMemory->GetInt();

			if ((int32)UINT16_MAX < NewMaxRepArrayMemoryValue || 1 > NewMaxRepArrayMemoryValue)
			{
				UE_LOG(LogRepTraffic, Error,
					TEXT("SerializeProperties_DynamicArray_r: MaxRepArrayMemory (%l) must be between 1 and 65535. Cannot accept new value."),
					NewMaxRepArrayMemoryValue);

				// Use SetByConsole to guarantee the value gets updated.
				CVarMaxArrayMemory->Set(MaxRepArrayMemory, ECVF_SetByConsole);
			}
			else
			{
				MaxRepArrayMemory = NewMaxRepArrayMemoryValue;
			}
		}
	);

	return IConsoleManager::Get().RegisterConsoleVariableSink_Handle(Delegate);
}

// This just forces the above to get called.
FConsoleVariableSinkHandle MaxRepArraySizeHandle = CreateMaxArraySizeCVarAndRegisterSink();
FConsoleVariableSinkHandle MaxRepArrayMemorySink = CreateMaxArrayMemoryCVarAndRegisterSink();

namespace UE4_RepLayout_Private
{
	template<typename OutputType, typename CommandType, typename BufferType>
	static typename TCopyQualifiersFromTo<BufferType, OutputType>::Type*
	GetTypedProperty(const BufferType& Buffer, const CommandType& Cmd)
	{
		using ConstOrNotOutputType = typename TCopyQualifiersFromTo<BufferType, OutputType>::Type;

		using BaseBufferType = typename TRemovePointer<typename TDecay<BufferType>::Type>::Type;

		static_assert(!TIsPointer<OutputType>::Value, "GetTypedProperty invalid OutputType!  Don't specify output as a pointer.");
		static_assert(TOr<TAreTypesEqual<uint8, BaseBufferType>, TAreTypesEqual<void, BaseBufferType>>::Value, "GetTypedProperty invalid BufferType! Only TRepDataBufferBase, void*, and uint8* are supported!");

		// TODO: Conditionally compilable runtime type validation.
		return reinterpret_cast<ConstOrNotOutputType *>(Buffer);
	}

	template<typename OutputType, typename CommandType, enum ERepDataBufferType BufferDataType, typename BufferUnderlyingType>
	static typename TCopyQualifiersFromTo<typename TRepDataBufferBase<BufferDataType, BufferUnderlyingType>::ConstOrNotVoid, OutputType>::Type*
	GetTypedProperty(const TRepDataBufferBase<BufferDataType, BufferUnderlyingType>& Buffer, const CommandType& Cmd)
	{
		using ConstOrNotOutputType = typename TCopyQualifiersFromTo<typename TRepDataBufferBase<BufferDataType, BufferUnderlyingType>::ConstOrNotVoid, OutputType>::Type;

		static_assert(!TIsPointer<OutputType>::Value, "GetTypedProperty invalid OutputType! Don't specify output as a pointer.");

		// TODO: Conditionally compilable runtime type validation.
		return reinterpret_cast<ConstOrNotOutputType *>((Buffer + Cmd).Data);
	}

	static void QueueRepNotifyForCustomDeltaProperty(
		FReceivingRepState* RESTRICT ReceivingRepState,
		FNetDeltaSerializeInfo& Params,
		UProperty* Property,
		uint32 StaticArrayIndex)
	{
		//@note: AddUniqueItem() here for static arrays since RepNotify() currently doesn't indicate index,
		//			so reporting the same property multiple times is not useful and wastes CPU
		//			were that changed, this should go back to AddItem() for efficiency
		// @todo UE4 - not checking if replicated value is changed from old.  Either fix or document, as may get multiple repnotifies of unacked properties.
		ReceivingRepState->RepNotifies.AddUnique(Property);

		UFunction* RepNotifyFunc = Params.Object->FindFunctionChecked(Property->RepNotifyFunc);

		if (RepNotifyFunc->NumParms > 0)
		{
			if (Property->ArrayDim != 1)
			{
				// For static arrays, we build the meta data here, but adding the Element index that was just read into the PropMetaData array.
				UE_LOG(LogRepTraffic, Verbose, TEXT("Property %s had ArrayDim: %d change"), *Property->GetName(), StaticArrayIndex);

				// Property is multi dimensional, keep track of what elements changed
				TArray<uint8>& PropMetaData = ReceivingRepState->RepNotifyMetaData.FindOrAdd(Property);
				PropMetaData.Add(StaticArrayIndex);
			}
		}
	}

	static void WritePropertyHeaderAndPayload(
		UObject* Object,
		UClass* ObjectClass,
		UProperty* Property,
		UNetConnection* Connection,
		UActorChannel* OwningChannel,
		FNetFieldExportGroup* NetFieldExportGroup,
		FNetBitWriter& Bunch,
		FNetBitWriter& Payload)
	{
		// Get class network info cache.
		const FClassNetCache* ClassCache = Connection->Driver->NetCache->GetClassNetCache(ObjectClass);

		check(ClassCache);

		// Get the network friend property index to replicate
		const FFieldNetCache * FieldCache = ClassCache->GetFromField(Property);

		checkSlow(FieldCache);

		// Send property name and optional array index.
		check(FieldCache->FieldNetIndex <= ClassCache->GetMaxIndex());

		// WriteFieldHeaderAndPayload will return the total number of bits written.
		// So, we subtract out the Payload size to get the actual number of header bits.
		const int32 HeaderBits = static_cast<int64>(OwningChannel->WriteFieldHeaderAndPayload(Bunch, ClassCache, FieldCache, NetFieldExportGroup, Payload)) - Payload.GetNumBits();

		NETWORK_PROFILER(GNetworkProfiler.TrackWritePropertyHeader(Property, HeaderBits, nullptr));
	}
}

//~ TODO: Consider moving the FastArray members into their own sub-struct to save memory for non fast array
//~			custom delta properties. Almost all Custom Delta properties now **are** Fast Arrays, so this
//~			probably doesn't matter much at the moment.

struct FLifetimeCustomDeltaProperty
{
	FLifetimeCustomDeltaProperty(const uint16 InPropertyRepIndex)
		: PropertyRepIndex(InPropertyRepIndex)
	{
	}

	FLifetimeCustomDeltaProperty(
		const uint16 InPropertyRepIndex,
		const int32 InFastArrayItemsCommand,
		const int32 InFastArrayNumber,
		const int32 InFastArrayDeltaFlagsOffset,
		const int32 InFastArrayArrayReplicationKeyOffset,
		const int32 InFastArrayItemReplicationIdOffset
	)
		: PropertyRepIndex(InPropertyRepIndex)
		, FastArrayItemsCommand(InFastArrayItemsCommand)
		, FastArrayNumber(InFastArrayNumber)
		, FastArrayDeltaFlagsOffset(InFastArrayDeltaFlagsOffset)
		, FastArrayArrayReplicationKeyOffset(InFastArrayArrayReplicationKeyOffset)
		, FastArrayItemReplicationIdOffset(InFastArrayItemReplicationIdOffset)
	{
	}

	/** The RepIndex of the corresponding Property. This can be used as an index into FRepLayout::Parents. */
	const uint16 PropertyRepIndex = INDEX_NONE;

	/** If this is a Fast Array Serializer property, this will be the command index for the Fast Array Item array. */
	const int32 FastArrayItemsCommand = INDEX_NONE;

	/**
	 * If this is a Fast Array Serializer property, this will be the instance number in the class.
	 * This is used to lookup Changelists.
	 */
	const int32 FastArrayNumber = INDEX_NONE;

	/**
	 * If this is a Fast Array Serializer property (and it is set up correctly for Delta Serialization), this will be an
	 * offset from to the property.
	 */
	const int32 FastArrayDeltaFlagsOffset = INDEX_NONE;

	/**
	 * If this is a Fast Array Serializer property (and it is set up correctly for Delta Serialization), this will be a pointer
	 * to the FFastArraySerializer::ArrayReplicationKey property.
	 */
	const int32 FastArrayArrayReplicationKeyOffset = INDEX_NONE;

	/**
	 * If this is a Fast Array Serializer property (and it is set up correctly for Delta Serialization), this will be a pointer
	 * to the FFastArraySerializerItem::ReplicationID property.
	 */
	const int32 FastArrayItemReplicationIdOffset = INDEX_NONE;

	const EFastArraySerializerDeltaFlags GetFastArrayDeltaFlags(void const * const FastArray) const
	{
		return GetRefFromOffsetAndMemory<EFastArraySerializerDeltaFlags>(FastArray, FastArrayDeltaFlagsOffset);
	}

	const int32 GetFastArrayArrayReplicationKey(void const * const FastArray) const
	{
		return GetRefFromOffsetAndMemory<int32>(FastArray, FastArrayArrayReplicationKeyOffset);
	}

	const int32 GetFastArrayItemReplicationID(void const * const FastArrayItem) const
	{
		return GetRefFromOffsetAndMemory<int32>(FastArrayItem, FastArrayItemReplicationIdOffset);
	}

	int32& GetFastArrayItemReplicationIDMutable(void* const FastArrayItem) const
	{
		return GetRefFromOffsetAndMemory<int32>(FastArrayItem, FastArrayItemReplicationIdOffset);
	}

private:

	template<typename Output>
	static Output& GetRefFromOffsetAndMemory(void* Memory, const int32 Offset)
	{
		checkSlow(Offset != INDEX_NONE);
		return *reinterpret_cast<Output*>(reinterpret_cast<uint8*>(Memory) + Offset);
	}

	template<typename Output>
	static const Output& GetRefFromOffsetAndMemory(void const * const Memory, const int32 Offset)
	{
		return GetRefFromOffsetAndMemory<Output>(const_cast<void*>(Memory), Offset);
	}
};

/**
 * Acceleration tracking which properties are custom delta.
 * This will ultimately replace the need for FObjectReplicator::LifetimeCustomDeltaProperties.
 */
struct FLifetimeCustomDeltaState
{
public:

	FLifetimeCustomDeltaState(int32 HighestCustomDeltaRepIndex)
	{
		check(HighestCustomDeltaRepIndex >= 0);
		LifetimeCustomDeltaIndexLookup.Init(static_cast<uint16>(INDEX_NONE), HighestCustomDeltaRepIndex + 1);
	}

	void CountBytes(FArchive& Ar) const
	{
		LifetimeCustomDeltaProperties.CountBytes(Ar);
		LifetimeCustomDeltaIndexLookup.CountBytes(Ar);
	}

	const uint16 GetNumCustomDeltaProperties() const
	{
		return LifetimeCustomDeltaProperties.Num();
	}

	const uint16 GetNumFastArrayProperties() const
	{
		return NumFastArrayProperties;
	}

	const FLifetimeCustomDeltaProperty& GetCustomDeltaProperty(const uint16 CustomDeltaIndex) const
	{
		return LifetimeCustomDeltaProperties[CustomDeltaIndex];
	}

	const uint16 GetCustomDeltaIndexFromPropertyRepIndex(const uint16 PropertyRepIndex) const
	{
		const uint16 CustomDeltaIndex = LifetimeCustomDeltaIndexLookup[PropertyRepIndex];
		check(static_cast<uint16>(INDEX_NONE) != CustomDeltaIndex);
		return CustomDeltaIndex;
	}

	void Add(FLifetimeCustomDeltaProperty&& ToAdd)
	{
		check(static_cast<uint16>(INDEX_NONE) == LifetimeCustomDeltaIndexLookup[ToAdd.PropertyRepIndex]);

		if (ToAdd.FastArrayNumber != INDEX_NONE)
		{
			++NumFastArrayProperties;
		}

		LifetimeCustomDeltaIndexLookup[ToAdd.PropertyRepIndex] = LifetimeCustomDeltaProperties.Num();
		LifetimeCustomDeltaProperties.Emplace(MoveTemp(ToAdd));
	}

	void CompactMemory()
	{
		LifetimeCustomDeltaProperties.Shrink();
		LifetimeCustomDeltaIndexLookup.Shrink();
	}

private:

	//~ Since there is only 1 RepLayout per class, and we will only create a FLifetimeCustomDeltaState for a RepLayout whose owning class
	//~ has Custom Delta Properties, using 2 arrays here seems like a good trade off for performance, memory, and convenience as opposed
	//~ to a TMap or TSortedMap.
	//~
	//~ Having just a map alone makes it harder for external code to iterate over custom delta properties without exposing these internal
	//~ classes.
	//~
	//~ However, maintaining just an array of CustomDeltaProperties makes it less efficient to perform lookups (we either need to keep
	//~ the list sorted or do linear searches).

	TArray<FLifetimeCustomDeltaProperty> LifetimeCustomDeltaProperties;
	TArray<uint16> LifetimeCustomDeltaIndexLookup;

	/** The number of valid FFastArraySerializer properties we found. */
	uint16 NumFastArrayProperties = 0;
};

//~ Some of this complexity could go away if we introduced a new Compare step to Custom Delta Serializers
//~ instead of just relying on the standard serialization stuff. That would be a bigger backwards compatibility
//~ risk, however.

struct FDeltaArrayHistoryItem
{
	/** The set of changelists by element ID.*/
	TMap<int32, TArray<uint16>> ChangelistByID;

	bool bWasUpdated = false;

	void Reset()
	{
		ChangelistByID.Empty();
		bWasUpdated = false;
	}

	void CountBytes(FArchive& Ar) const
	{
		ChangelistByID.CountBytes(Ar);

		for (const auto& KVP : ChangelistByID)
		{
			KVP.Value.CountBytes(Ar);
		}
	}
};

struct FDeltaArrayHistoryState
{
	/** The maximum number of individual changelists allowed.*/
	static const uint32 MAX_CHANGE_HISTORY = FRepChangelistState::MAX_CHANGE_HISTORY;

	//~ TODO: Investigate either making this a Dynamically sized container,
	//~			or potentially changing ArrayStates to unique pointers / 
	//~			an indirect array.
	//~
	//~			Right now, if a Delta Struct Serialization is disabled for
	//~			a given Fast Array, this will still use up memory.

	/** Circular buffer of changelists. */
	FDeltaArrayHistoryItem ChangeHistory[MAX_CHANGE_HISTORY];

	/** The latest ArrayReplicationKey sent to any connection. */
	int32 ArrayReplicationKey = INDEX_NONE;

	/** Copy of the IDToIndexMap from the array when we last sent it. */
	TMap<int32, int32> IDToIndexMap;

	/** Index in the buffer where changelist history starts (i.e., the Oldest changelist). */
	uint32 HistoryStart = 0;

	/** Index in the buffer where changelist history ends (i.e., the Newest changelist). */
	uint32 HistoryEnd = 0;

	void CountBytes(FArchive& Ar) const
	{
		IDToIndexMap.CountBytes(Ar);

		for (const FDeltaArrayHistoryItem& HistoryItem : ChangeHistory)
		{
			HistoryItem.CountBytes(Ar);
		}
	}
};

struct FCustomDeltaChangelistState
{
	FCustomDeltaChangelistState(const int32 NumArrays)
	{
		ArrayStates.SetNum(NumArrays);
	}

	/** Index used to determine whether or not we've compared already on a given frame. */
	uint32 CompareIndex = 0;

	/**
	 * An array tracking the last compared history of Arrays.
	 * Indices should match FLifetimeCustomDeltaProperty::FastArrayNumber.
	 */
	TArray<FDeltaArrayHistoryState> ArrayStates;

	void CountBytes(FArchive& Ar) const
	{
		ArrayStates.CountBytes(Ar);
		for (const FDeltaArrayHistoryState& ArrayState : ArrayStates)
		{
			ArrayState.CountBytes(Ar);
		}
	}
};

#define ENABLE_PROPERTY_CHECKSUMS

//#define SANITY_CHECK_MERGES

#define USE_CUSTOM_COMPARE

//#define ENABLE_SUPER_CHECKSUMS

#ifdef USE_CUSTOM_COMPARE
static FORCEINLINE bool CompareBool(
	const FRepLayoutCmd&	Cmd,
	const void* 			A,
	const void* 			B)
{
	return Cmd.Property->Identical(A, B);
}

static FORCEINLINE bool CompareObject(
	const FRepLayoutCmd&	Cmd,
	const void* 			A,
	const void* 			B)
{
	// Until UObjectPropertyBase::Identical is made safe for GC'd objects, we need to do it manually
	// This saves us from having to add referenced objects during GC
	UObjectPropertyBase* ObjProperty = CastChecked<UObjectPropertyBase>(Cmd.Property);

	UObject* ObjectA = ObjProperty->GetObjectPropertyValue(A);
	UObject* ObjectB = ObjProperty->GetObjectPropertyValue(B);

	return ObjectA == ObjectB;
}

static FORCEINLINE bool CompareSoftObject(
	const FRepLayoutCmd& Cmd,
	const void* A,
	const void* B)
{
	// USoftObjectProperty::Identical will get the SoftObjectPath for each pointer, and compare the Path etc.
	// It should also handle null checks, and doesn't try to dereference the object, so is GC safe.
	return Cmd.Property->Identical(A, B);
}

static FORCEINLINE bool CompareWeakObject(
	const FRepLayoutCmd& Cmd,
	const void* A,
	const void* B)
{
	const UWeakObjectProperty* const WeakObjectProperty = CastChecked<UWeakObjectProperty>(Cmd.Property);
	const FWeakObjectPtr ObjectA = WeakObjectProperty->GetPropertyValue(A);
	const FWeakObjectPtr ObjectB = WeakObjectProperty->GetPropertyValue(B);

	return ObjectA.HasSameIndexAndSerialNumber(ObjectB);
}

template<typename T>
bool CompareValue(const T * A, const T * B)
{
	return *A == *B;
}

template<typename T>
bool CompareValue(const void* A, const void* B)
{
	return CompareValue((T*)A, (T*)B);
}

static FORCEINLINE bool PropertiesAreIdenticalNative(
	const FRepLayoutCmd&	Cmd,
	const void*				A,
	const void*				B)
{
	switch (Cmd.Type)
	{
		case ERepLayoutCmdType::PropertyBool:			return CompareBool(Cmd, A, B);
		case ERepLayoutCmdType::PropertyNativeBool:		return CompareValue<bool>(A, B);
		case ERepLayoutCmdType::PropertyByte:			return CompareValue<uint8>(A, B);
		case ERepLayoutCmdType::PropertyFloat:			return CompareValue<float>(A, B);
		case ERepLayoutCmdType::PropertyInt:			return CompareValue<int32>(A, B);
		case ERepLayoutCmdType::PropertyName:			return CompareValue<FName>(A, B);
		case ERepLayoutCmdType::PropertyObject:			return CompareObject(Cmd, A, B);
		case ERepLayoutCmdType::PropertySoftObject:		return CompareSoftObject(Cmd, A, B);
		case ERepLayoutCmdType::PropertyWeakObject:		return CompareWeakObject(Cmd, A, B);
		case ERepLayoutCmdType::PropertyUInt32:			return CompareValue<uint32>(A, B);
		case ERepLayoutCmdType::PropertyUInt64:			return CompareValue<uint64>(A, B);
		case ERepLayoutCmdType::PropertyVector:			return CompareValue<FVector>(A, B);
		case ERepLayoutCmdType::PropertyVector100:		return CompareValue<FVector_NetQuantize100>(A, B);
		case ERepLayoutCmdType::PropertyVectorQ:		return CompareValue<FVector_NetQuantize>(A, B);
		case ERepLayoutCmdType::PropertyVectorNormal:	return CompareValue<FVector_NetQuantizeNormal>(A, B);
		case ERepLayoutCmdType::PropertyVector10:		return CompareValue<FVector_NetQuantize10>(A, B);
		case ERepLayoutCmdType::PropertyPlane:			return CompareValue<FPlane>(A, B);
		case ERepLayoutCmdType::PropertyRotator:		return CompareValue<FRotator>(A, B);
		case ERepLayoutCmdType::PropertyNetId:			return CompareValue<FUniqueNetIdRepl>(A, B);
		case ERepLayoutCmdType::RepMovement:			return CompareValue<FRepMovement>(A, B);
		case ERepLayoutCmdType::PropertyString:			return CompareValue<FString>(A, B);
		case ERepLayoutCmdType::Property:				return Cmd.Property->Identical(A, B);
		default: 
			UE_LOG(LogRep, Fatal, TEXT("PropertiesAreIdentical: Unsupported type! %i (%s)"), (uint8)Cmd.Type, *Cmd.Property->GetName());
	}

	return false;
}

static FORCEINLINE bool PropertiesAreIdentical(
	const FRepLayoutCmd&	Cmd,
	const void*				A,
	const void*				B)
{
	const bool bIsIdentical = PropertiesAreIdenticalNative(Cmd, A, B);
#if 0
	// Sanity check result
	if (bIsIdentical != Cmd.Property->Identical(A, B))
	{
		UE_LOG(LogRep, Fatal, TEXT("PropertiesAreIdentical: Result mismatch! (%s)"), *Cmd.Property->GetFullName());
	}
#endif
	return bIsIdentical;
}
#else
static FORCEINLINE bool PropertiesAreIdentical(const FRepLayoutCmd& Cmd, const void* A, const void* B)
{
	return Cmd.Property->Identical(A, B);
}
#endif

static FORCEINLINE void StoreProperty(const FRepLayoutCmd& Cmd, void* A, const void* B)
{
	Cmd.Property->CopySingleValue(A, B);
}

static FORCEINLINE void SerializeGenericChecksum(FBitArchive& Ar)
{
	uint32 Checksum = 0xABADF00D;
	Ar << Checksum;
	check(Checksum == 0xABADF00D);
}

template<ERepDataBufferType DataType>
static void SerializeReadWritePropertyChecksum(
	const FRepLayoutCmd& Cmd,
	const int32 CurCmdIndex,
	const TConstRepDataBuffer<DataType> Data,
	FBitArchive& Ar)
{
	// Serialize various attributes that will mostly ensure we are working on the same property
	const uint32 NameHash = GetTypeHash(Cmd.Property->GetName());

	uint32 MarkerChecksum = 0;

	// Evolve the checksum over several values that will uniquely identity where we are and should be
	MarkerChecksum = FCrc::MemCrc_DEPRECATED(&NameHash, sizeof(NameHash), MarkerChecksum);
	MarkerChecksum = FCrc::MemCrc_DEPRECATED(&Cmd.Offset, sizeof(Cmd.Offset), MarkerChecksum);
	MarkerChecksum = FCrc::MemCrc_DEPRECATED(&CurCmdIndex, sizeof(CurCmdIndex), MarkerChecksum);

	const uint32 OriginalMarkerChecksum = MarkerChecksum;

	Ar << MarkerChecksum;

	if (MarkerChecksum != OriginalMarkerChecksum)
	{
		// This is fatal, as it means we are out of sync to the point we can't recover
		UE_LOG(LogRep, Fatal, TEXT("SerializeReadWritePropertyChecksum: Property checksum marker failed! [%s]"), *Cmd.Property->GetFullName());
	}

	if (Cmd.Property->IsA(UObjectPropertyBase::StaticClass()))
	{
		// Can't handle checksums for objects right now
		// Need to resolve how to handle unmapped objects
		return;
	}

	// Now generate a checksum that guarantee that this property is in the exact state as the server
	// This will require NetSerializeItem to be deterministic, in and out
	// i.e, not only does NetSerializeItem need to write the same blob on the same input data, but
	//	it also needs to write the same blob it just read as well.
	FBitWriter Writer(0, true);

	Cmd.Property->NetSerializeItem(Writer, NULL, const_cast<uint8*>(Data.Data));

	if (Ar.IsSaving())
	{
		// If this is the server, do a read, and then another write so that we do exactly what the client will do, which will better ensure determinism 

		// We do this to force InitializeValue, DestroyValue etc to work on a single item
		const int32 OriginalDim = Cmd.Property->ArrayDim;
		Cmd.Property->ArrayDim = 1;

		TArray<uint8> TempPropMemory;
		TempPropMemory.AddZeroed(Cmd.Property->ElementSize + 4);
		uint32* Guard = (uint32*)&TempPropMemory[TempPropMemory.Num() - 4];
		const uint32 TAG_VALUE = 0xABADF00D;
		*Guard = TAG_VALUE;
		Cmd.Property->InitializeValue(TempPropMemory.GetData());
		check(*Guard == TAG_VALUE);

		// Read it back in and then write it out to produce what the client will produce
		FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
		Cmd.Property->NetSerializeItem(Reader, NULL, TempPropMemory.GetData());
		check(Reader.AtEnd() && !Reader.IsError());
		check(*Guard == TAG_VALUE);

		// Write it back out for a final time
		Writer.Reset();

		Cmd.Property->NetSerializeItem(Writer, NULL, TempPropMemory.GetData());
		check(*Guard == TAG_VALUE);

		// Destroy temp memory
		Cmd.Property->DestroyValue(TempPropMemory.GetData());

		// Restore the static array size
		Cmd.Property->ArrayDim = OriginalDim;

		check(*Guard == TAG_VALUE);
	}

	uint32 PropertyChecksum = FCrc::MemCrc_DEPRECATED(Writer.GetData(), Writer.GetNumBytes());

	const uint32 OriginalPropertyChecksum = PropertyChecksum;

	Ar << PropertyChecksum;

	if (PropertyChecksum != OriginalPropertyChecksum)
	{
		// This is a warning, because for some reason, float rounding issues in the quantization functions cause this to return false positives
		UE_LOG(LogRep, Warning, TEXT("Property checksum failed! [%s]"), *Cmd.Property->GetFullName());
	}
}

static uint32 GetRepLayoutCmdCompatibleChecksum(
	const UProperty*		Property,
	const UNetConnection*	ServerConnection,
	const uint32			StaticArrayIndex,
	const uint32			InChecksum)
{
	// Compatible checksums are only used for InternalAck connections
	if (ServerConnection && !ServerConnection->InternalAck)
	{
		return 0;
	}

	// Evolve checksum on name
	uint32 CompatibleChecksum = FCrc::StrCrc32(*Property->GetName().ToLower(), InChecksum);	
	
	// Evolve by property type			
	CompatibleChecksum = FCrc::StrCrc32(*Property->GetCPPType(nullptr, 0).ToLower(), CompatibleChecksum);
	
	// Evolve by StaticArrayIndex (to make all unrolled static array elements unique)
	if ((ServerConnection == nullptr) ||
		(ServerConnection->EngineNetworkProtocolVersion >= EEngineNetworkVersionHistory::HISTORY_REPCMD_CHECKSUM_REMOVE_PRINTF))
	{
		CompatibleChecksum = FCrc::MemCrc32(&StaticArrayIndex, sizeof(StaticArrayIndex), CompatibleChecksum);
	}
	else
	{
		CompatibleChecksum = FCrc::StrCrc32(*FString::Printf(TEXT("%i"), StaticArrayIndex), CompatibleChecksum);
	}

	return CompatibleChecksum;
}

FRepChangedPropertyTracker::FRepChangedPropertyTracker(const bool InbIsReplay, const bool InbIsClientReplayRecording):
		bIsReplay(InbIsReplay),
		bIsClientReplayRecording(InbIsClientReplayRecording),
		ExternalDataNumBits(0)
	{}

FRepChangedPropertyTracker::~FRepChangedPropertyTracker() {}

void FRepChangedPropertyTracker::SetCustomIsActiveOverride(const uint16 RepIndex, const bool bIsActive)
{
	FRepChangedParent & Parent = Parents[RepIndex];

	Parent.Active = (bIsActive || bIsClientReplayRecording) ? 1 : 0;
	Parent.OldActive = Parent.Active;
}

void FRepChangedPropertyTracker::SetExternalData(const uint8* Src, const int32 NumBits)
{
	ExternalDataNumBits = NumBits;
	const int32 NumBytes = (NumBits + 7) >> 3;
	ExternalData.Reset(NumBytes);
	ExternalData.AddUninitialized(NumBytes);
	FMemory::Memcpy(ExternalData.GetData(), Src, NumBytes);
}

bool FRepChangedPropertyTracker::IsReplay() const
{
	return bIsReplay;
}

void FRepChangedPropertyTracker::CountBytes(FArchive& Ar) const
{
	// Include our size here, because the caller won't know.
	Ar.CountBytes(sizeof(FRepChangedPropertyTracker), sizeof(FRepChangedPropertyTracker));
	Parents.CountBytes(Ar);
	ExternalData.CountBytes(Ar);

}

void FRepStateStaticBuffer::CountBytes(FArchive& Ar) const
{
	GRANULAR_NETWORK_MEMORY_TRACKING_INIT(Ar, "FRepStateStaticBuffer::CountBytes");

	// Unfortunately, this won't track Custom Serialize stucts or Custom Delta Serialize
	// structs.
	struct FCountBytesHelper
	{
		FCountBytesHelper(
			FArchive& InAr,
			const FConstRepShadowDataBuffer InShadowData,
			const TArray<FRepParentCmd>& InParents,
			const TArray<FRepLayoutCmd>& InCmds)

			: Ar((FArchiveCountMem&)InAr)
			, MainShadowData(InShadowData)
			, Parents(InParents)
			, Cmds(InCmds)
		{
		}

		void CountBytes()
		{
			uint64 NewMax = Ar.GetMax();
			uint64 OldMax = 0;

			for (const FRepParentCmd& Parent : Parents)
			{
				OldMax = NewMax;

				CountBytes_Command(Parent, Parent.CmdStart, Parent.CmdEnd, MainShadowData);

				NewMax = Ar.GetMax();

				if (0 < Parent.RepNotifyNumParams ||
					(0 == Parent.RepNotifyNumParams && REPNOTIFY_OnChanged == Parent.RepNotifyCondition))
				{
					OnRepMemory += (NewMax - OldMax);
				}
				else
				{
					NonRepMemory += (NewMax - OldMax);
				}
			}
		}

		void CountBytes_Command(const FRepParentCmd& Parent, const int32 CmdStart, const int32 CmdEnd, const FConstRepShadowDataBuffer ShadowData) const
		{
			for (int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; ++CmdIndex)
			{
				const FRepLayoutCmd& Cmd = Cmds[CmdIndex];
				CountBytes_r(Parent, Cmd, CmdIndex, ShadowData);

				if (ERepLayoutCmdType::DynamicArray == Cmd.Type)
				{
					CmdIndex = Cmd.EndCmd - 1;
				}
			}
		}

		void CountBytes_r(const FRepParentCmd& Parent, const FRepLayoutCmd& Cmd, const int32 InCmdIndex, const FConstRepShadowDataBuffer ShadowData) const
		{
			if (ERepLayoutCmdType::DynamicArray == Cmd.Type)
			{
				FScriptArray* Array = (FScriptArray*)(ShadowData + Cmd).Data;
				Array->CountBytes(Ar, Cmd.ElementSize);

				FConstRepShadowDataBuffer ShadowArrayData(Array->GetData());

				for (int32 i = 0; i < Array->Num(); ++i)
				{
					const int32 ArrayElementOffset = Cmd.ElementSize * i;
					CountBytes_Command(Parent, InCmdIndex + 1, Cmd.EndCmd, ShadowArrayData + ArrayElementOffset);
				}
			}
			else if (ERepLayoutCmdType::PropertyString == Cmd.Type)
			{
				((FString const * const)(ShadowData + Cmd).Data)->CountBytes(Ar);
			}
		}

		FArchiveCountMem& Ar;
		const FConstRepShadowDataBuffer MainShadowData;
		const TArray<FRepParentCmd>& Parents;
		const TArray<FRepLayoutCmd>& Cmds;

		uint64 OnRepMemory = 0;
		uint64 NonRepMemory = 0;
	};

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("Static Memory", Buffer.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("Dynamic Memory (Undercounts!)",
		// FCountBytesHelper CountBytesHelper(Ar, Buffer.GetData(), RepLayout->Parents, RepLayout->Cmds);
		// CountBytesHelper.CountBytes();
		// GRANULAR_NETWORK_MEMORY_TRACKING_CUSTOM_WORK("OnRepMemory", CountBytesHelper.OnRepMemory);
		// GRANULAR_NETWORK_MEMORY_TRACKING_CUSTOM_WORK("NonRepMemory", CountBytesHelper.NonRepMemory);
	);
}

FRepChangelistState::FRepChangelistState(
	const TSharedRef<const FRepLayout>& InRepLayout,
	const uint8* Source,
	FCustomDeltaChangelistState* InDeltaChangelistState):

	CustomDeltaChangelistState(InDeltaChangelistState),
	HistoryStart(0),
	HistoryEnd(0),
	CompareIndex(0),
	StaticBuffer(InRepLayout->CreateShadowBuffer(Source))
{}

void FRepChangelistState::CountBytes(FArchive& Ar) const
{
	StaticBuffer.CountBytes(Ar);
	SharedSerialization.CountBytes(Ar);

	if (CustomDeltaChangelistState)
	{
		Ar.CountBytes(sizeof(FCustomDeltaChangelistState), sizeof(FCustomDeltaChangelistState));
		CustomDeltaChangelistState->CountBytes(Ar);
	}
}

FReplicationChangelistMgr::FReplicationChangelistMgr(
	const TSharedRef<const FRepLayout>& InRepLayout,
	const uint8* Source,
	FCustomDeltaChangelistState* DeltaChangelistState):

	LastReplicationFrame(0),
	LastInitialReplicationFrame(0),
	RepChangelistState(InRepLayout, Source, DeltaChangelistState)
{
}

FReceivingRepState::FReceivingRepState(FRepStateStaticBuffer&& InStaticBuffer) :
	StaticBuffer(MoveTemp(InStaticBuffer))
{
}

FRepLayout::FRepLayout() :
	Flags(ERepLayoutFlags::None),
	RoleIndex(INDEX_NONE),
	RemoteRoleIndex(-1),
	Owner(nullptr)
{}

FRepLayout::~FRepLayout()
{
}

void FRepLayout::UpdateChangelistMgr(
	FSendingRepState* RESTRICT RepState,
	FReplicationChangelistMgr& InChangelistMgr,
	const UObject* InObject,
	const uint32 ReplicationFrame,
	const FReplicationFlags& RepFlags,
	const bool bForceCompare) const
{
	if (GShareInitialCompareState)
	{
		// See if we can re-use the work already done on a previous connection
		// Rules:
		// 1. We have replicated this actor at least once this frame
		// 2. This is not initial replication or we have done an initial replication this frame as well
		if (!bForceCompare && GShareShadowState && (InChangelistMgr.LastReplicationFrame == ReplicationFrame) && (!RepFlags.bNetInitial || (InChangelistMgr.LastInitialReplicationFrame == ReplicationFrame)))
		{
			// If this is initial replication, or we have never replicated on this connection, force a role compare
			if (RepFlags.bNetInitial || (RepState->LastCompareIndex == 0))
			{
				FReplicationFlags TempFlags = RepFlags;
				TempFlags.bRolesOnly = true;
				CompareProperties(RepState, &InChangelistMgr.RepChangelistState, (const uint8*)InObject, TempFlags);
			}

			INC_DWORD_STAT_BY(STAT_NetSkippedDynamicProps, 1);
			return;
		}
	}
	else
	{
		// See if we can re-use the work already done on a previous connection
		// Rules:
		//	1. We always compare once per frame (i.e. check LastReplicationFrame == ReplicationFrame)
		//	2. We check LastCompareIndex > 1 so we can do at least one pass per connection to compare all properties
		//		This is necessary due to how RemoteRole is manipulated per connection, so we need to give all connections a chance to see if it changed
		//	3. We ALWAYS compare on bNetInitial to make sure we have a fresh changelist of net initial properties in this case
		if (!bForceCompare && GShareShadowState && !RepFlags.bNetInitial && RepState->LastCompareIndex > 1 && InChangelistMgr.LastReplicationFrame == ReplicationFrame)
		{
			INC_DWORD_STAT_BY(STAT_NetSkippedDynamicProps, 1);
			return;
		}
	}

	CompareProperties(RepState, &InChangelistMgr.RepChangelistState, (const uint8*)InObject, RepFlags);

	InChangelistMgr.LastReplicationFrame = ReplicationFrame;

	if (GShareInitialCompareState && RepFlags.bNetInitial)
	{
		InChangelistMgr.LastInitialReplicationFrame = ReplicationFrame;
	}
}

void FReplicationChangelistMgr::CountBytes(FArchive& Ar) const
{
	RepChangelistState.CountBytes(Ar);
}

struct FComparePropertiesSharedParams
{
	const bool bIsInitial;
	const bool bForceFail;
	const int16 RoleIndex;
	const int16 RemoteRoleIndex;
	const ERepLayoutFlags Flags;
	const TArray<FRepParentCmd>& Parents;
	const TArray<FRepLayoutCmd>& Cmds;
};

static uint16 CompareProperties_r(
	const FComparePropertiesSharedParams& SharedParams,
	const uint16 CmdStart,
	const uint16 CmdEnd,
	FRepShadowDataBuffer ShadowData,
	const FConstRepObjectDataBuffer Data,
	TArray<uint16>& Changed,
	uint16 Handle);

static void CompareProperties_Array_r(
	const FComparePropertiesSharedParams& SharedParams,
	FRepShadowDataBuffer ShadowData,
	const FConstRepObjectDataBuffer Data,
	TArray<uint16>& Changed,
	const uint16 CmdIndex,
	const uint16 Handle);

static void CompareRoleProperty(
	const FComparePropertiesSharedParams& SharedParams,
	const FConstRepObjectDataBuffer Data,
	const uint16 RoleOrRemoteRoleIndex,
	TEnumAsByte<ENetRole>& SavedRoleOrRemoteRole,
	TArray<uint16>& Changed)
{
	const FRepParentCmd& RoleOrRemoteRoleParent = SharedParams.Parents[RoleOrRemoteRoleIndex];
	const FRepLayoutCmd& RoleOrRemoteRoleCmd = SharedParams.Cmds[RoleOrRemoteRoleParent.CmdStart];
	const uint16 Handle = RoleOrRemoteRoleCmd.RelativeHandle;
	const TEnumAsByte<ENetRole> ActorRoleOrRemoteRole = *(const TEnumAsByte<ENetRole>*)(Data + RoleOrRemoteRoleParent).Data;
	if (SharedParams.bForceFail || SavedRoleOrRemoteRole != ActorRoleOrRemoteRole)
	{
		SavedRoleOrRemoteRole = ActorRoleOrRemoteRole;
		Changed.Add(Handle);
	}
}

static void CompareRoleProperties(
	const FComparePropertiesSharedParams& SharedParams,
	FSendingRepState* RESTRICT RepState,
	const FConstRepObjectDataBuffer Data,
	TArray<uint16>& Changed)
{
	if (RepState && EnumHasAnyFlags(SharedParams.Flags, ERepLayoutFlags::IsActor))
	{
		CompareRoleProperty(SharedParams, Data, SharedParams.RemoteRoleIndex, RepState->SavedRemoteRole, Changed);
		CompareRoleProperty(SharedParams, Data, SharedParams.RoleIndex, RepState->SavedRole, Changed);
	}
}

static void CompareParentProperties(
	const FComparePropertiesSharedParams& SharedParams,
	FSendingRepState* RESTRICT RepState,
	FRepChangelistState* RESTRICT RepChangelistState,
	const FConstRepObjectDataBuffer Data,
	TArray<uint16>& Changed)
{
	FRepShadowDataBuffer ShadowData = RepChangelistState->StaticBuffer.GetData();
	check(ShadowData);

	FRepChangedPropertyTracker* RepChangedPropertyTracker = (RepState ? RepState->RepChangedPropertyTracker.Get() : nullptr);

	const bool bCheckForRole = EnumHasAnyFlags(SharedParams.Flags, ERepLayoutFlags::IsActor) && RepState != nullptr;

	for (uint16 ParentIndex = 0; ParentIndex < SharedParams.Parents.Num(); ++ParentIndex)
	{
		const FRepParentCmd& Parent = SharedParams.Parents[ParentIndex];

		const bool bIsLifetime = EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsLifetime);

		// Active state of a property applies to *all* connections.
		// If the property is inactive, we can skip comparing it because we know it won't be sent.
		// Further, this will keep the last active state of the property in the shadow buffer,
		// meaning the next time the property becomes active it will be sent to all connections.
		const bool bIsActive = !RepChangedPropertyTracker || RepChangedPropertyTracker->Parents[ParentIndex].Active;

		const bool bShouldSkip = !bIsLifetime || !bIsActive || (Parent.Condition == COND_InitialOnly && !SharedParams.bIsInitial);

		if (bShouldSkip)
		{
			continue;
		}

		if (bCheckForRole)
		{
			if (UNLIKELY(ParentIndex == SharedParams.RoleIndex))
			{
				CompareRoleProperty(SharedParams, Data, SharedParams.RoleIndex, RepState->SavedRole, Changed);
				continue;
			}
			else if (UNLIKELY(ParentIndex == SharedParams.RemoteRoleIndex))
			{
				CompareRoleProperty(SharedParams, Data, SharedParams.RemoteRoleIndex, RepState->SavedRemoteRole, Changed);
				continue;
			}
		}
		
		// Note, Handle - 1 to account for CompareProperties_r incrementing handles.
		const FRepLayoutCmd& Cmd = SharedParams.Cmds[Parent.CmdStart];
		const uint16 Handle = Cmd.RelativeHandle;
		CompareProperties_r(SharedParams, Parent.CmdStart, Parent.CmdEnd, ShadowData, Data, Changed, Handle - 1);
	}
}

static uint16 CompareProperties_r(
	const FComparePropertiesSharedParams& SharedParams,
	const uint16 CmdStart,
	const uint16 CmdEnd,
	FRepShadowDataBuffer ShadowData,
	const FConstRepObjectDataBuffer Data,
	TArray<uint16>& Changed,
	uint16 Handle)
{
	for (int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; ++CmdIndex)
	{
		const FRepLayoutCmd& Cmd = SharedParams.Cmds[CmdIndex];

		check(Cmd.Type != ERepLayoutCmdType::Return);

		++Handle;

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			// Once we hit an array, start using a stack based approach
			CompareProperties_Array_r(SharedParams, ShadowData + Cmd, Data + Cmd, Changed, CmdIndex, Handle);
			CmdIndex = Cmd.EndCmd - 1;		// The -1 to handle the ++ in the for loop
			continue;
		}
		else if (SharedParams.bForceFail || !PropertiesAreIdentical(Cmd, (ShadowData + Cmd).Data, (Data + Cmd).Data))
		{
			StoreProperty(Cmd, (ShadowData + Cmd).Data, (Data + Cmd.Offset).Data);
			Changed.Add(Handle);
		}
	}

	return Handle;
}

static void CompareProperties_Array_r(
	const FComparePropertiesSharedParams& SharedParams,
	FRepShadowDataBuffer ShadowData,
	const FConstRepObjectDataBuffer Data,
	TArray<uint16>& Changed,
	const uint16 CmdIndex,
	const uint16 Handle)
{
	const FRepLayoutCmd& Cmd = SharedParams.Cmds[CmdIndex];

	FScriptArray* ShadowArray = (FScriptArray*)ShadowData.Data;
	FScriptArray* Array = (FScriptArray*)Data.Data;

	const uint16 ArrayNum = Array->Num();
	const uint16 ShadowArrayNum = ShadowArray->Num();

	// Make the shadow state match the actual state at the time of compare
	FScriptArrayHelper StoredArrayHelper((UArrayProperty*)Cmd.Property, ShadowArray);
	StoredArrayHelper.Resize(ArrayNum);

	TArray<uint16> ChangedLocal;

	uint16 LocalHandle = 0;

	const FConstRepObjectDataBuffer ArrayData(Array->GetData());
	FRepShadowDataBuffer ShadowArrayData(ShadowArray->GetData());

	{
		const bool bOldForceFail = SharedParams.bForceFail;
		bool& bForceFail = const_cast<bool&>(SharedParams.bForceFail);
		TGuardValue<bool> ForceFailGuard(bForceFail, bForceFail);

		for (int32 i = 0; i < ArrayNum; i++)
		{
			const int32 ArrayElementOffset = i * Cmd.ElementSize;
			bForceFail = bOldForceFail || i >= ShadowArrayNum;
			LocalHandle = CompareProperties_r(SharedParams, CmdIndex + 1, Cmd.EndCmd - 1, ShadowArrayData + ArrayElementOffset, ArrayData + ArrayElementOffset, ChangedLocal, LocalHandle);
		}
	}

	if (ChangedLocal.Num())
	{
		Changed.Add(Handle);
		Changed.Add(ChangedLocal.Num());		// This is so we can jump over the array if we need to
		Changed.Append(ChangedLocal);
		Changed.Add(0);
	}
	else if (ArrayNum != ShadowArrayNum)
	{
		// If nothing below us changed, we either shrunk, or we grew and our inner was an array that didn't have any elements
		check(ArrayNum < ShadowArrayNum || SharedParams.Cmds[CmdIndex + 1].Type == ERepLayoutCmdType::DynamicArray);

		// Array got smaller, send the array handle to force array size change
		Changed.Add(Handle);
		Changed.Add(0);
		Changed.Add(0);
	}
}

bool FRepLayout::CompareProperties(
	FSendingRepState* RESTRICT RepState,
	FRepChangelistState* RESTRICT RepChangelistState,
	const FConstRepObjectDataBuffer Data,
	const FReplicationFlags& RepFlags) const
{
	CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_NetReplicateDynamicPropCompareTime, CVarNetEnableDetailedScopeCounters.GetValueOnAnyThread() > 0);

	if (IsEmpty())
	{
		return false;
	}

	RepChangelistState->CompareIndex++;

	check((RepChangelistState->HistoryEnd - RepChangelistState->HistoryStart) < FRepChangelistState::MAX_CHANGE_HISTORY);
	const int32 HistoryIndex = RepChangelistState->HistoryEnd % FRepChangelistState::MAX_CHANGE_HISTORY;

	FRepChangedHistory& NewHistoryItem = RepChangelistState->ChangeHistory[HistoryIndex];

	TArray<uint16>& Changed = NewHistoryItem.Changed;
	Changed.Empty(1);

	FComparePropertiesSharedParams SharedParams{
		/*bIsInitial=*/ !!RepFlags.bNetInitial,
		/*bForceFail=*/ false,
		RoleIndex,
		RemoteRoleIndex,
		Flags,
		Parents,
		Cmds
	};

	if (RepFlags.bRolesOnly)
	{
		CompareRoleProperties(SharedParams, RepState, Data, Changed);
	}
	else
	{
		CompareParentProperties(SharedParams, RepState, RepChangelistState, Data, Changed);
	}

	if (Changed.Num() == 0)
	{
		return false;
	}

	//
	// We produced a new change list, copy it to the history
	//

	// Null terminator
	Changed.Add(0);

	// Move end pointer
	RepChangelistState->HistoryEnd++;

	// New changes found so clear any existing shared serialization state
	RepChangelistState->SharedSerialization.Reset();

	// If we're full, merge the oldest up, so we always have room for a new entry
	if ((RepChangelistState->HistoryEnd - RepChangelistState->HistoryStart) == FRepChangelistState::MAX_CHANGE_HISTORY)
	{
		const int32 FirstHistoryIndex = RepChangelistState->HistoryStart % FRepChangelistState::MAX_CHANGE_HISTORY;

		RepChangelistState->HistoryStart++;

		const int32 SecondHistoryIndex = RepChangelistState->HistoryStart % FRepChangelistState::MAX_CHANGE_HISTORY;

		TArray<uint16>& FirstChangelistRef = RepChangelistState->ChangeHistory[FirstHistoryIndex].Changed;
		TArray<uint16> SecondChangelistCopy = MoveTemp(RepChangelistState->ChangeHistory[SecondHistoryIndex].Changed);

		MergeChangeList(Data, FirstChangelistRef, SecondChangelistCopy, RepChangelistState->ChangeHistory[SecondHistoryIndex].Changed);
	}

	return true;
}

static FORCEINLINE void WritePropertyHandle(
	FNetBitWriter& Writer,
	uint16 Handle,
	bool bDoChecksum)
{
	const int NumStartingBits = Writer.GetNumBits();

	uint32 LocalHandle = Handle;
	Writer.SerializeIntPacked(LocalHandle);

	UE_LOG(LogRepProperties, VeryVerbose, TEXT("WritePropertyHandle: Handle=%d"), Handle);

#ifdef ENABLE_PROPERTY_CHECKSUMS
	if (bDoChecksum)
	{
		SerializeGenericChecksum(Writer);
	}
#endif

	NETWORK_PROFILER(GNetworkProfiler.TrackWritePropertyHandle(Writer.GetNumBits() - NumStartingBits, nullptr));
}

bool FRepLayout::ReplicateProperties(
	FSendingRepState* RESTRICT RepState,
	FRepChangelistState* RESTRICT RepChangelistState,
	const FConstRepObjectDataBuffer Data,
	UClass* ObjectClass,
	UActorChannel* OwningChannel,
	FNetBitWriter& Writer,
	const FReplicationFlags& RepFlags) const
{
	CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_NetReplicateDynamicPropTime, CVarNetEnableDetailedScopeCounters.GetValueOnAnyThread() > 0);

	check(ObjectClass == Owner);

	// If we are an empty RepLayout, there's nothing to do.
	if (IsEmpty())
	{
		return false;
	}

	FRepChangedPropertyTracker*	ChangeTracker = RepState->RepChangedPropertyTracker.Get();

	TArray<uint16> NewlyActiveChangelist;

	// Rebuild conditional state if needed
	if (RepState->RepFlags.Value != RepFlags.Value)
	{
		RebuildConditionalProperties(RepState, RepFlags);

		// Filter out any previously inactive changes from still inactive ones
		TArray<uint16> InactiveChangelist = MoveTemp(RepState->InactiveChangelist);
		FilterChangeList(InactiveChangelist, RepState->InactiveParents, RepState->InactiveChangelist, NewlyActiveChangelist);
	}

	if (OwningChannel->Connection->ResendAllDataState != EResendAllDataState::None)
	{
		check(OwningChannel->Connection->InternalAck);

		// If we are resending data since open, we don't want to affect the current state of channel/replication, so just do the minimum and send the data, and return
		if (RepState->LifetimeChangelist.Num() > 0)
		{
			// Use a pruned version of the list, in case arrays changed size since the last time we replicated
			TArray<uint16> Pruned;
			PruneChangeList(Data, RepState->LifetimeChangelist, Pruned);
			RepState->LifetimeChangelist = MoveTemp(Pruned);

			// No need to merge in the newly active properties here, as the Lifetime Changelist should contain everything
			// inactive or otherwise.
			FilterChangeListToActive(RepState->LifetimeChangelist, RepState->InactiveParents, Pruned);
			if (Pruned.Num() > 0)
			{
				SendProperties_BackwardsCompatible(RepState, ChangeTracker, Data, OwningChannel->Connection, Writer, Pruned);
				return true;
			}
		}

		return false;
	}

	check(RepState->HistoryEnd >= RepState->HistoryStart);
	check((RepState->HistoryEnd - RepState->HistoryStart) < FSendingRepState::MAX_CHANGE_HISTORY);

	const bool bFlushPreOpenAckHistory = RepState->bOpenAckedCalled && RepState->PreOpenAckHistory.Num()> 0;

	const bool bCompareIndexSame = RepState->LastCompareIndex == RepChangelistState->CompareIndex;

	RepState->LastCompareIndex = RepChangelistState->CompareIndex;

	// We can early out if we know for sure there are no new changelists to send
	if (bCompareIndexSame || RepState->LastChangelistIndex == RepChangelistState->HistoryEnd)
	{
		if (RepState->NumNaks == 0 && !bFlushPreOpenAckHistory && NewlyActiveChangelist.Num() == 0)
		{
			// Nothing changed and there are no nak's, so just do normal housekeeping and remove acked history items
			UpdateChangelistHistory(RepState, ObjectClass, Data, OwningChannel->Connection, NULL);
			return false;
		}
	}

	// Clamp to the valid history range (and log if we end up sending entire history, this should only happen if we get really far behind)
	//	NOTE - The RepState->LastChangelistIndex != 0 should handle/ignore the JIP case
	if (RepState->LastChangelistIndex <= RepChangelistState->HistoryStart)
	{
		if (RepState->LastChangelistIndex != 0)
		{
			UE_LOG(LogRep, Verbose, TEXT("FRepLayout::ReplicatePropertiesUsingChangelistState: Entire history sent for: %s"), *GetNameSafe(ObjectClass));
		}

		RepState->LastChangelistIndex = RepChangelistState->HistoryStart;
	}

	const int32 PossibleNewHistoryIndex = RepState->HistoryEnd % FSendingRepState::MAX_CHANGE_HISTORY;

	FRepChangedHistory& PossibleNewHistoryItem = RepState->ChangeHistory[PossibleNewHistoryIndex];

	TArray<uint16>& Changed = PossibleNewHistoryItem.Changed;

	// Make sure this history item is actually inactive
	check(Changed.Num() == 0);

	// Gather all change lists that are new since we last looked, and merge them all together into a single CL
	for (int32 i = RepState->LastChangelistIndex; i < RepChangelistState->HistoryEnd; ++i)
	{
		const int32 HistoryIndex = i % FRepChangelistState::MAX_CHANGE_HISTORY;

		FRepChangedHistory& HistoryItem = RepChangelistState->ChangeHistory[HistoryIndex];

		TArray<uint16> Temp = MoveTemp(Changed);
		MergeChangeList(Data, HistoryItem.Changed, Temp, Changed);
	}

	// Merge in newly active properties so they can be sent.
	if (NewlyActiveChangelist.Num() > 0)
	{
		TArray<uint16> Temp = MoveTemp(Changed);
		MergeChangeList(Data, NewlyActiveChangelist, Temp, Changed);
	}

	// We're all caught up now
	RepState->LastChangelistIndex = RepChangelistState->HistoryEnd;

	if (Changed.Num() > 0 || RepState->NumNaks > 0 || bFlushPreOpenAckHistory)
	{
		RepState->HistoryEnd++;

		UpdateChangelistHistory(RepState, ObjectClass, Data, OwningChannel->Connection, &Changed);

		// Merge in the PreOpenAckHistory (unreliable properties sent before the bunch was initially acked)
		if (bFlushPreOpenAckHistory)
		{
			for (int32 i = 0; i < RepState->PreOpenAckHistory.Num(); i++)
			{
				TArray<uint16> Temp = MoveTemp(Changed);
				MergeChangeList(Data, RepState->PreOpenAckHistory[i].Changed, Temp, Changed);
			}
			RepState->PreOpenAckHistory.Empty();
		}
	}
	else
	{
		// Nothing changed and there are no nak's, so just do normal housekeeping and remove acked history items
		UpdateChangelistHistory(RepState, ObjectClass, Data, OwningChannel->Connection, nullptr);
		return false;
	}

	// At this point we should have a non empty change list
	check(Changed.Num() > 0);

	// do not build shared state for InternalAck (demo) connections
	if (!OwningChannel->Connection->InternalAck && (GNetSharedSerializedData != 0))
	{
		// if no shared serialization info exists, build it
		if (!RepChangelistState->SharedSerialization.IsValid())
		{
			BuildSharedSerialization(Data, Changed, true, RepChangelistState->SharedSerialization);
		}
	}

	const int32 NumBits = Writer.GetNumBits();

	// Filter out the final changelist into Active and Inactive.
	TArray<uint16> UnfilteredChanged = MoveTemp(Changed);
	TArray<uint16> NewlyInactiveChangelist;
	FilterChangeList(UnfilteredChanged, RepState->InactiveParents, NewlyInactiveChangelist, Changed);

	// If we have any properties that are no longer active, make sure we track them.
	if (NewlyInactiveChangelist.Num() > 1)
	{
		TArray<uint16> Temp = MoveTemp(RepState->InactiveChangelist);
		MergeChangeList(Data, NewlyInactiveChangelist, Temp, RepState->InactiveChangelist);

	}

	// Send the final merged change list
	if (OwningChannel->Connection->InternalAck)
	{
		// Remember all properties that have changed since this channel was first opened in case we need it (for bResendAllDataSinceOpen)
		// We use UnfilteredChanged so LifetimeChangelist contains all properties, regardless of Active state.
		TArray<uint16> Temp = MoveTemp(RepState->LifetimeChangelist);
		MergeChangeList(Data, UnfilteredChanged, Temp, RepState->LifetimeChangelist);

		if (Changed.Num() > 0)
		{
			SendProperties_BackwardsCompatible(RepState, ChangeTracker, Data, OwningChannel->Connection, Writer, Changed);
		}
	}
	else if (Changed.Num() > 0)
	{
		SendProperties(RepState, ChangeTracker, Data, ObjectClass, Writer, Changed, RepChangelistState->SharedSerialization);
	}

	// See if something actually sent (this may be false due to conditional checks inside the send properties function
	const bool bSomethingSent = NumBits != Writer.GetNumBits();

	if (!bSomethingSent)
	{
		// We need to revert the change list in the history if nothing really sent (can happen due to condition checks)
		Changed.Empty();
		RepState->HistoryEnd--;
	}

	return bSomethingSent;
}

void FRepLayout::UpdateChangelistHistory(
	FSendingRepState* RepState,
	UClass* ObjectClass,
	const FConstRepObjectDataBuffer Data,
	UNetConnection* Connection,
	TArray<uint16>* OutMerged) const
{
	check(RepState->HistoryEnd >= RepState->HistoryStart);

	const int32 HistoryCount = RepState->HistoryEnd - RepState->HistoryStart;
	const bool DumpHistory = HistoryCount == FSendingRepState::MAX_CHANGE_HISTORY;
	const int32 AckPacketId = Connection->OutAckPacketId;

	// If our buffer is currently full, forcibly send the entire history
	if (DumpHistory)
	{
		UE_LOG(LogRep, Verbose, TEXT("FRepLayout::UpdateChangelistHistory: History overflow, forcing history dump %s, %s"), *ObjectClass->GetName(), *Connection->Describe());
	}

	for (int32 i = RepState->HistoryStart; i < RepState->HistoryEnd; i++)
	{
		const int32 HistoryIndex = i % FSendingRepState::MAX_CHANGE_HISTORY;

		FRepChangedHistory & HistoryItem = RepState->ChangeHistory[ HistoryIndex ];

		if (HistoryItem.OutPacketIdRange.First == INDEX_NONE)
		{
			// Hasn't been initialized in PostReplicate yet
			// No need to go further, otherwise we'll overwrite entries incorrectly.
			break;
		}

		// All active history items should contain a change list
		check(HistoryItem.Changed.Num() > 0);

		if (AckPacketId >= HistoryItem.OutPacketIdRange.Last || HistoryItem.Resend || DumpHistory)
		{
			if (HistoryItem.Resend || DumpHistory)
			{
				// Merge in nak'd change lists
				check(OutMerged != NULL);
				TArray<uint16> Temp = MoveTemp(*OutMerged);
				MergeChangeList(Data, HistoryItem.Changed, Temp, *OutMerged);

#ifdef SANITY_CHECK_MERGES
				SanityCheckChangeList(Data, *OutMerged);
#endif

				if (HistoryItem.Resend)
				{
					RepState->NumNaks--;
				}
			}

			HistoryItem.Reset();
			RepState->HistoryStart++;
		}
	}

	// Remove any tiling in the history markers to keep them from wrapping over time
	const int32 NewHistoryCount	= RepState->HistoryEnd - RepState->HistoryStart;

	check(NewHistoryCount < FSendingRepState::MAX_CHANGE_HISTORY);

	RepState->HistoryStart = RepState->HistoryStart % FSendingRepState::MAX_CHANGE_HISTORY;
	RepState->HistoryEnd = RepState->HistoryStart + NewHistoryCount;

	// Make sure we processed all the naks properly
	check(RepState->NumNaks == 0);
}

void FRepLayout::SerializeObjectReplicatedProperties(UObject* Object, FBitArchive & Ar) const
{
	static FRepSerializationSharedInfo Empty;

	for (int32 i = 0; i < Parents.Num(); i++)
	{
		UStructProperty* StructProperty = Cast<UStructProperty>(Parents[i].Property);
		UObjectProperty* ObjectProperty = Cast<UObjectProperty>(Parents[i].Property);

		// We're only able to easily serialize non-object/struct properties, so just do those.
		if (ObjectProperty == nullptr && StructProperty == nullptr)
		{
			bool bHasUnmapped = false;
			SerializeProperties_r(Ar, NULL, Parents[i].CmdStart, Parents[i].CmdEnd, (uint8*)Object, bHasUnmapped, 0, 0, Empty);
		}
	}
}

bool FRepHandleIterator::NextHandle()
{
	CmdIndex = INDEX_NONE;

	Handle = ChangelistIterator.Changed[ChangelistIterator.ChangedIndex];

	if (Handle == 0)
	{
		return false;		// Done
	}

	ChangelistIterator.ChangedIndex++;

	if (!ensureMsgf(ChangelistIterator.Changed.IsValidIndex(ChangelistIterator.ChangedIndex),
			TEXT("Attempted to access invalid iterator index: Handle=%d, ChangedIndex=%d, ChangedNum=%d, Owner=%s, LastSuccessfulCmd=%s"),
			Handle, ChangelistIterator.ChangedIndex, ChangelistIterator.Changed.Num(),
			*GetPathNameSafe(Owner),
			*((Cmds.IsValidIndex(LastSuccessfulCmdIndex) && Cmds[LastSuccessfulCmdIndex].Property) ? Cmds[LastSuccessfulCmdIndex].Property->GetPathName() : FString::FromInt(LastSuccessfulCmdIndex))))
	{
		return false;
	}

	const int32 HandleMinusOne = Handle - 1;

	ArrayIndex = (ArrayElementSize> 0 && NumHandlesPerElement> 0) ? HandleMinusOne / NumHandlesPerElement : 0;

	if (ArrayIndex >= MaxArrayIndex)
	{
		return false;
	}

	ArrayOffset	= ArrayIndex * ArrayElementSize;

	const int32 RelativeHandle = HandleMinusOne - ArrayIndex * NumHandlesPerElement;

	if (!ensureMsgf(HandleToCmdIndex.IsValidIndex(RelativeHandle),
			TEXT("Attempted to access invalid RelativeHandle Index: Handle=%d, RelativeHandle=%d, NumHandlesPerElement=%d, ArrayIndex=%d, ArrayElementSize=%d, Owner=%s, LastSuccessfulCmd=%s"),
			Handle, RelativeHandle, NumHandlesPerElement, ArrayIndex, ArrayElementSize,
			*GetPathNameSafe(Owner),
			*((Cmds.IsValidIndex(LastSuccessfulCmdIndex) && Cmds[LastSuccessfulCmdIndex].Property) ? Cmds[LastSuccessfulCmdIndex].Property->GetPathName() : FString::FromInt(LastSuccessfulCmdIndex))))
	{
		return false;
	}

	CmdIndex = HandleToCmdIndex[RelativeHandle].CmdIndex;

	if (!ensureMsgf(MinCmdIndex <= CmdIndex && CmdIndex < MaxCmdIndex,
			TEXT("Attempted to access Command Index outside of iterator range: Handle=%d, RelativeHandle=%d, CmdIndex=%d, MinCmdIdx=%d, MaxCmdIdx=%d, ArrayIndex=%d, Owner=%s, LastSuccessfulCmd=%s"),
			Handle, RelativeHandle, CmdIndex, MinCmdIndex, MaxCmdIndex, ArrayIndex,
			*GetPathNameSafe(Owner),
			*((Cmds.IsValidIndex(LastSuccessfulCmdIndex) && Cmds[LastSuccessfulCmdIndex].Property) ? Cmds[LastSuccessfulCmdIndex].Property->GetPathName() : FString::FromInt(LastSuccessfulCmdIndex))))
	{
		return false;
	}

	const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

	if (!ensureMsgf(Cmd.RelativeHandle - 1 == RelativeHandle,
			TEXT("Command Relative Handle does not match found Relative Handle: Handle=%d, RelativeHandle=%d, CmdIdx=%d, CmdRelativeHandle=%d, ArrayIndex=%d, Owner=%s, LastSuccessfulCmd=%s"),
			Handle, RelativeHandle, CmdIndex, Cmd.RelativeHandle, ArrayIndex,
			*GetPathNameSafe(Owner),
			*((Cmds.IsValidIndex(LastSuccessfulCmdIndex) && Cmds[LastSuccessfulCmdIndex].Property) ? Cmds[LastSuccessfulCmdIndex].Property->GetPathName() : FString::FromInt(LastSuccessfulCmdIndex))))
	{
		return false;
	}

	if (!ensureMsgf(Cmd.Type != ERepLayoutCmdType::Return,
			TEXT("Hit unexpected return handle: Handle=%d, RelativeHandle=%d, CmdIdx=%d, ArrayIndex=%d, Owner=%s, LastSuccessfulCmd=%s"),
			Handle, RelativeHandle, CmdIndex, ArrayIndex,
			*GetPathNameSafe(Owner),
			*((Cmds.IsValidIndex(LastSuccessfulCmdIndex) && Cmds[LastSuccessfulCmdIndex].Property) ? Cmds[LastSuccessfulCmdIndex].Property->GetPathName() : FString::FromInt(LastSuccessfulCmdIndex))))
	{
		return false;
	}

	LastSuccessfulCmdIndex = CmdIndex;

	return true;
}

bool FRepHandleIterator::JumpOverArray()
{
	const int32 ArrayChangedCount = ChangelistIterator.Changed[ChangelistIterator.ChangedIndex++];
	ChangelistIterator.ChangedIndex += ArrayChangedCount;

	if (!ensure(ChangelistIterator.Changed[ChangelistIterator.ChangedIndex] == 0))
	{
		return false;
	}

	ChangelistIterator.ChangedIndex++;

	return true;
}

int32 FRepHandleIterator::PeekNextHandle() const
{
	return ChangelistIterator.Changed[ChangelistIterator.ChangedIndex];
}

class FScopedIteratorArrayTracker
{
public:
	FScopedIteratorArrayTracker(FRepHandleIterator* InCmdIndexIterator)
	{
		CmdIndexIterator = InCmdIndexIterator;

		if (CmdIndexIterator)
		{
			ArrayChangedCount = CmdIndexIterator->ChangelistIterator.Changed[CmdIndexIterator->ChangelistIterator.ChangedIndex++];
			OldChangedIndex = CmdIndexIterator->ChangelistIterator.ChangedIndex;
		}
	}

	~FScopedIteratorArrayTracker()
	{
		if (CmdIndexIterator)
		{
			check(CmdIndexIterator->ChangelistIterator.ChangedIndex - OldChangedIndex <= ArrayChangedCount);
			CmdIndexIterator->ChangelistIterator.ChangedIndex = OldChangedIndex + ArrayChangedCount;
			check(CmdIndexIterator->PeekNextHandle() == 0);
			CmdIndexIterator->ChangelistIterator.ChangedIndex++;
		}
	}

	FRepHandleIterator* CmdIndexIterator;
	int32 ArrayChangedCount;
	int32 OldChangedIndex;
};

void FRepLayout::MergeChangeList_r(
	FRepHandleIterator& RepHandleIterator1,
	FRepHandleIterator& RepHandleIterator2,
	const FConstRepObjectDataBuffer SourceData,
	TArray<uint16>& OutChanged) const
{
	while (true)
	{
		const int32 NextHandle1 = RepHandleIterator1.PeekNextHandle();
		const int32 NextHandle2 = RepHandleIterator2.PeekNextHandle();

		if (NextHandle1 == 0 && NextHandle2 == 0)
		{
			// Done
			break;
		}

		if (NextHandle2 == 0)
		{
			PruneChangeList_r(RepHandleIterator1, SourceData, OutChanged);
			return;
		}
		else if (NextHandle1 == 0)
		{
			PruneChangeList_r(RepHandleIterator2, SourceData, OutChanged);
			return;
		}

		FRepHandleIterator* ActiveIterator1 = nullptr;
		FRepHandleIterator* ActiveIterator2 = nullptr;

		int32 CmdIndex = INDEX_NONE;
		int32 ArrayOffset = INDEX_NONE;

		if (NextHandle1 < NextHandle2)
		{
			if (!RepHandleIterator1.NextHandle())
			{
				// Array overflow
				break;
			}

			OutChanged.Add(NextHandle1);

			CmdIndex = RepHandleIterator1.CmdIndex;
			ArrayOffset = RepHandleIterator1.ArrayOffset;

			ActiveIterator1 = &RepHandleIterator1;
		}
		else if (NextHandle2 < NextHandle1)
		{
			if (!RepHandleIterator2.NextHandle())
			{
				// Array overflow
				break;
			}

			OutChanged.Add(NextHandle2);

			CmdIndex = RepHandleIterator2.CmdIndex;
			ArrayOffset = RepHandleIterator2.ArrayOffset;

			ActiveIterator2 = &RepHandleIterator2;
		}
		else
		{
			check(NextHandle1 == NextHandle2);

			if (!RepHandleIterator1.NextHandle())
			{
				// Array overflow
				break;
			}

			if (!ensure(RepHandleIterator2.NextHandle()))
			{
				// Array overflow
				break;
			}

			check(RepHandleIterator1.CmdIndex == RepHandleIterator2.CmdIndex);

			OutChanged.Add(NextHandle1);

			CmdIndex = RepHandleIterator1.CmdIndex;
			ArrayOffset = RepHandleIterator1.ArrayOffset;

			ActiveIterator1 = &RepHandleIterator1;
			ActiveIterator2 = &RepHandleIterator2;
		}

		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			const FConstRepObjectDataBuffer Data = (SourceData + Cmd) + ArrayOffset;
			const FScriptArray* Array = (FScriptArray *)Data.Data;
			const FConstRepObjectDataBuffer ArrayData(Array->GetData());

			FScopedIteratorArrayTracker ArrayTracker1(ActiveIterator1);
			FScopedIteratorArrayTracker ArrayTracker2(ActiveIterator2);

			const int32 OriginalChangedNum	= OutChanged.AddUninitialized();

			TArray<FHandleToCmdIndex>& ArrayHandleToCmdIndex = ActiveIterator1 ? *ActiveIterator1->HandleToCmdIndex[Cmd.RelativeHandle - 1].HandleToCmdIndex : *ActiveIterator2->HandleToCmdIndex[Cmd.RelativeHandle - 1].HandleToCmdIndex; //-V595

			if (!ActiveIterator1)
			{
				FRepHandleIterator ArrayIterator2(ActiveIterator2->Owner, ActiveIterator2->ChangelistIterator, Cmds, ArrayHandleToCmdIndex, Cmd.ElementSize, Array->Num(), CmdIndex + 1, Cmd.EndCmd - 1);
				PruneChangeList_r(ArrayIterator2, ArrayData, OutChanged);
			}
			else if (!ActiveIterator2)
			{
				FRepHandleIterator ArrayIterator1(ActiveIterator1->Owner, ActiveIterator1->ChangelistIterator, Cmds, ArrayHandleToCmdIndex, Cmd.ElementSize, Array->Num(), CmdIndex + 1, Cmd.EndCmd - 1);
				PruneChangeList_r(ArrayIterator1, ArrayData, OutChanged);
			}
			else
			{
				FRepHandleIterator ArrayIterator1(ActiveIterator1->Owner, ActiveIterator1->ChangelistIterator, Cmds, ArrayHandleToCmdIndex, Cmd.ElementSize, Array->Num(), CmdIndex + 1, Cmd.EndCmd - 1);
				FRepHandleIterator ArrayIterator2(ActiveIterator2->Owner, ActiveIterator2->ChangelistIterator, Cmds, ArrayHandleToCmdIndex, Cmd.ElementSize, Array->Num(), CmdIndex + 1, Cmd.EndCmd - 1);

				MergeChangeList_r(ArrayIterator1, ArrayIterator2, ArrayData, OutChanged);
			}

			// Patch in the jump offset
			OutChanged[OriginalChangedNum] = OutChanged.Num() - (OriginalChangedNum + 1);

			// Add the array terminator
			OutChanged.Add(0);
		}
	}
}

void FRepLayout::PruneChangeList_r(
	FRepHandleIterator& RepHandleIterator,
	const FConstRepObjectDataBuffer SourceData,
	TArray<uint16>& OutChanged) const
{
	while (RepHandleIterator.NextHandle())
	{
		OutChanged.Add(RepHandleIterator.Handle);

		const int32 CmdIndex = RepHandleIterator.CmdIndex;
		const int32 ArrayOffset = RepHandleIterator.ArrayOffset;

		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			const FConstRepObjectDataBuffer Data = (SourceData + Cmd) + ArrayOffset;
			const FScriptArray* Array = (FScriptArray *)Data.Data;
			const FConstRepObjectDataBuffer ArrayData(Array->GetData());

			FScopedIteratorArrayTracker ArrayTracker(&RepHandleIterator);

			const int32 OriginalChangedNum = OutChanged.AddUninitialized();

			TArray<FHandleToCmdIndex>& ArrayHandleToCmdIndex = *RepHandleIterator.HandleToCmdIndex[Cmd.RelativeHandle - 1].HandleToCmdIndex;

			FRepHandleIterator ArrayIterator(RepHandleIterator.Owner, RepHandleIterator.ChangelistIterator, Cmds, ArrayHandleToCmdIndex, Cmd.ElementSize, Array->Num(), CmdIndex + 1, Cmd.EndCmd - 1);
			PruneChangeList_r(ArrayIterator, ArrayData, OutChanged);

			// Patch in the jump offset
			OutChanged[OriginalChangedNum] = OutChanged.Num() - (OriginalChangedNum + 1);

			// Add the array terminator
			OutChanged.Add(0);
		}
	}
}

void FRepLayout::FilterChangeList(
	const TArray<uint16>& Changelist,
	const TBitArray<>& InactiveParents,
	TArray<uint16>& OutInactiveProperties,
	TArray<uint16>& OutActiveProperties) const
{
	FChangelistIterator ChangelistIterator(Changelist, 0);
	FRepHandleIterator HandleIterator(Owner, ChangelistIterator, Cmds, BaseHandleToCmdIndex, 0, 1, 0, Cmds.Num() - 1);

	OutInactiveProperties.Empty(1);
	OutActiveProperties.Empty(1);

	while (HandleIterator.NextHandle())
	{
		const FRepLayoutCmd& Cmd = Cmds[HandleIterator.CmdIndex];

		TArray<uint16>& Properties = InactiveParents[Cmd.ParentIndex] ? OutInactiveProperties : OutActiveProperties;
			
		Properties.Add(HandleIterator.Handle);

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			// No need to recursively filter the change list, as handles are only enabled/disabled at the parent level
			int32 HandleCount = ChangelistIterator.Changed[ChangelistIterator.ChangedIndex];
			Properties.Add(HandleCount);
					
			for (int32 I = 0; I < HandleCount; ++I)
			{
				Properties.Add(ChangelistIterator.Changed[ChangelistIterator.ChangedIndex + 1 + I]);
			}

			Properties.Add(0);

			HandleIterator.JumpOverArray();
		}
	}

	OutInactiveProperties.Add(0);
	OutActiveProperties.Add(0);
}

void FRepLayout::FilterChangeListToActive(
	const TArray<uint16>& Changelist,
	const TBitArray<>& InactiveParents,
	TArray<uint16>& OutProperties) const
{
	FChangelistIterator ChangelistIterator(Changelist, 0);
	FRepHandleIterator HandleIterator(Owner, ChangelistIterator, Cmds, BaseHandleToCmdIndex, 0, 1, 0, Cmds.Num() - 1);

	OutProperties.Empty(1);

	while (HandleIterator.NextHandle())
	{
		const FRepLayoutCmd& Cmd = Cmds[HandleIterator.CmdIndex];
		if (!InactiveParents[Cmd.ParentIndex])
		{
			OutProperties.Add(HandleIterator.Handle);

			if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
			{
				// No need to recursively filter the change list, as handles are only enabled/disabled at the parent level
				int32 HandleCount = ChangelistIterator.Changed[ChangelistIterator.ChangedIndex];
				OutProperties.Add(HandleCount);

				for (int32 I = 0; I < HandleCount; ++I)
				{
					OutProperties.Add(ChangelistIterator.Changed[ChangelistIterator.ChangedIndex + 1 + I]);
				}

				OutProperties.Add(0);

				HandleIterator.JumpOverArray();
			}
		}
		else if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			HandleIterator.JumpOverArray();
		}
	}

	OutProperties.Add(0);
}

const FRepSerializedPropertyInfo* FRepSerializationSharedInfo::WriteSharedProperty(
	const FRepLayoutCmd& Cmd,
	const FGuid& PropertyGuid,
	const int32 CmdIndex,
	const uint16 Handle,
	const FConstRepObjectDataBuffer Data,
	const bool bWriteHandle,
	const bool bDoChecksum)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	check(!SharedPropertyInfo.ContainsByPredicate([&](const FRepSerializedPropertyInfo& Info)
	{ 
		return (Info.Guid == PropertyGuid); 
	}));
#endif

	int32 InfoIndex = SharedPropertyInfo.Emplace();

	FRepSerializedPropertyInfo& SharedPropInfo = SharedPropertyInfo[InfoIndex];
	SharedPropInfo.Guid = PropertyGuid;
	SharedPropInfo.BitOffset = SerializedProperties->GetNumBits(); 

	UE_LOG(LogRepProperties, VeryVerbose, TEXT("WriteSharedProperty: Handle=%d, Guid=%s"), Handle, *PropertyGuid.ToString());

	if (bWriteHandle)
	{
		WritePropertyHandle(*SerializedProperties, Handle, bDoChecksum);
	}

	SharedPropInfo.PropBitOffset = SerializedProperties->GetNumBits();

	// This property changed, so send it
	Cmd.Property->NetSerializeItem(*SerializedProperties, nullptr, const_cast<uint8*>(Data.Data));

	const int64 NumPropEndBits = SerializedProperties->GetNumBits();

	SharedPropInfo.PropBitLength = NumPropEndBits - SharedPropInfo.PropBitOffset;

#ifdef ENABLE_PROPERTY_CHECKSUMS
	if (bDoChecksum)
	{
		SerializeReadWritePropertyChecksum(Cmd, CmdIndex, Data, *SerializedProperties);
	}
#endif

	SharedPropInfo.BitLength = SerializedProperties->GetNumBits() - SharedPropInfo.BitOffset;

	return &SharedPropInfo;
}

void FRepLayout::SendProperties_r(
	FSendingRepState* RESTRICT RepState,
	FNetBitWriter& Writer,
	const bool bDoChecksum,
	FRepHandleIterator& HandleIterator,
	const FConstRepObjectDataBuffer SourceData,
	const int32 ArrayDepth,
	const FRepSerializationSharedInfo* const RESTRICT SharedInfo) const
{
	const bool bDoSharedSerialization = SharedInfo && !!GNetSharedSerializedData;

	while (HandleIterator.NextHandle())
	{
		const FRepLayoutCmd& Cmd = Cmds[HandleIterator.CmdIndex];
		const FRepParentCmd& ParentCmd = Parents[Cmd.ParentIndex];

		UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_r: Parent=%d, Cmd=%d, ArrayIndex=%d"), Cmd.ParentIndex, HandleIterator.CmdIndex, HandleIterator.ArrayIndex);
		
		FConstRepObjectDataBuffer Data = (SourceData + Cmd) + HandleIterator.ArrayOffset;

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			WritePropertyHandle(Writer, HandleIterator.Handle, bDoChecksum);

			const FScriptArray* Array = (FScriptArray *)Data.Data;
			const FConstRepObjectDataBuffer ArrayData(Array->GetData());

			// Write array num
			uint16 ArrayNum = Array->Num();
			Writer << ArrayNum;

			UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_r: ArrayNum=%d"), ArrayNum);

			// Read the jump offset
			// We won't need to actually jump over anything because we expect the change list to be pruned once we get here
			// But we can use it to verify we read the correct amount.
			const int32 ArrayChangedCount = HandleIterator.ChangelistIterator.Changed[HandleIterator.ChangelistIterator.ChangedIndex++];

			const int32 OldChangedIndex = HandleIterator.ChangelistIterator.ChangedIndex;

			TArray<FHandleToCmdIndex>& ArrayHandleToCmdIndex = *HandleIterator.HandleToCmdIndex[Cmd.RelativeHandle - 1].HandleToCmdIndex;

			FRepHandleIterator ArrayHandleIterator(HandleIterator.Owner, HandleIterator.ChangelistIterator, Cmds, ArrayHandleToCmdIndex, Cmd.ElementSize, ArrayNum, HandleIterator.CmdIndex + 1, Cmd.EndCmd - 1);

			check(ArrayHandleIterator.ArrayElementSize> 0);
			check(ArrayHandleIterator.NumHandlesPerElement> 0);

			SendProperties_r(RepState, Writer, bDoChecksum, ArrayHandleIterator, ArrayData, ArrayDepth + 1, SharedInfo);

			check(HandleIterator.ChangelistIterator.ChangedIndex - OldChangedIndex == ArrayChangedCount);				// Make sure we read correct amount
			check(HandleIterator.ChangelistIterator.Changed[HandleIterator.ChangelistIterator.ChangedIndex] == 0);	// Make sure we are at the end

			HandleIterator.ChangelistIterator.ChangedIndex++;

			WritePropertyHandle(Writer, 0, bDoChecksum);		// Signify end of dynamic array
			continue;
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (GDoReplicationContextString> 0)
		{
			Writer.PackageMap->SetDebugContextString(FString::Printf(TEXT("%s - %s"), *Owner->GetPathName(), *Cmd.Property->GetPathName()));
		}
#endif

		const FRepSerializedPropertyInfo* SharedPropInfo = nullptr;

		if (bDoSharedSerialization && EnumHasAnyFlags(Cmd.Flags, ERepLayoutCmdFlags::IsSharedSerialization))
		{
			FGuid PropertyGuid(HandleIterator.CmdIndex, HandleIterator.ArrayIndex, ArrayDepth, (int32)((PTRINT)Data.Data & 0xFFFFFFFF));

			SharedPropInfo = SharedInfo->SharedPropertyInfo.FindByPredicate([&](const FRepSerializedPropertyInfo& Info) 
			{ 
				return (Info.Guid == PropertyGuid); 
			});
		}

		// Use shared serialization if was found
		if (SharedPropInfo)
		{
			UE_LOG(LogRepProperties, VeryVerbose, TEXT("SerializeProperties_r: SharedSerialization - Handle=%d, Guid=%s"), HandleIterator.Handle, *SharedPropInfo->Guid.ToString());
			GNumSharedSerializationHit++;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (GNetVerifyShareSerializedData != 0)
			{
				FBitWriterMark BitWriterMark(Writer);
			
				UE_LOG(LogRepProperties, VeryVerbose, TEXT("SerializeProperties_r: Verify SharedSerialization, NetSerializeItem"));

				WritePropertyHandle(Writer, HandleIterator.Handle, bDoChecksum);
				Cmd.Property->NetSerializeItem(Writer, Writer.PackageMap, const_cast<uint8*>(Data.Data));

#ifdef ENABLE_PROPERTY_CHECKSUMS
				if (bDoChecksum)
				{
					SerializeReadWritePropertyChecksum(Cmd, HandleIterator.CmdIndex, Data, Writer);
				}
#endif
				TArray<uint8> StandardBuffer;
				BitWriterMark.Copy(Writer, StandardBuffer);
				BitWriterMark.Pop(Writer);

				Writer.SerializeBitsWithOffset(SharedInfo->SerializedProperties->GetData(), SharedPropInfo->BitOffset, SharedPropInfo->BitLength);
				
				TArray<uint8> SharedBuffer;
				BitWriterMark.Copy(Writer, SharedBuffer);

				if (StandardBuffer != SharedBuffer)
				{
					UE_LOG(LogRep, Error, TEXT("Shared serialization data mismatch!"));
				}
			}
			else
#endif
			{
				Writer.SerializeBitsWithOffset(SharedInfo->SerializedProperties->GetData(), SharedPropInfo->BitOffset, SharedPropInfo->BitLength);
			}

			NETWORK_PROFILER(GNetworkProfiler.TrackReplicateProperty(ParentCmd.Property, SharedPropInfo->PropBitLength, nullptr));
		}
		else
		{
			GNumSharedSerializationMiss++;
			WritePropertyHandle(Writer, HandleIterator.Handle, bDoChecksum);

			const int32 NumStartBits = Writer.GetNumBits();

			// This property changed, so send it
			Cmd.Property->NetSerializeItem(Writer, Writer.PackageMap, const_cast<uint8*>(Data.Data));
			UE_LOG(LogRepProperties, VeryVerbose, TEXT("SerializeProperties_r: NetSerializeItem"));

			const int32 NumEndBits = Writer.GetNumBits();

			NETWORK_PROFILER(GNetworkProfiler.TrackReplicateProperty(ParentCmd.Property, NumEndBits - NumStartBits, nullptr));

#ifdef ENABLE_PROPERTY_CHECKSUMS
			if (bDoChecksum)
			{
				SerializeReadWritePropertyChecksum(Cmd, HandleIterator.CmdIndex, Data, Writer);
			}
#endif
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (GDoReplicationContextString> 0)
		{
			Writer.PackageMap->ClearDebugContextString();
		}
#endif
	}
}

void FRepLayout::SendProperties(
	FSendingRepState* RESTRICT RepState,
	FRepChangedPropertyTracker* ChangedTracker,
	const FConstRepObjectDataBuffer Data,
	UClass* ObjectClass,
	FNetBitWriter& Writer,
	TArray<uint16>& Changed,
	const FRepSerializationSharedInfo& SharedInfo) const
{
	SCOPE_CYCLE_COUNTER(STAT_NetReplicateDynamicPropSendTime);

	if (IsEmpty())
	{
		return;
	}

#ifdef ENABLE_PROPERTY_CHECKSUMS
	const bool bDoChecksum = (GDoPropertyChecksum == 1);
#else
	const bool bDoChecksum = false;
#endif

	FBitWriterMark Mark(Writer);

#ifdef ENABLE_PROPERTY_CHECKSUMS
	Writer.WriteBit(bDoChecksum ? 1 : 0);
#endif

	const int32 NumBits = Writer.GetNumBits();

	UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties: Owner=%s, LastChangelistIndex=%d"), *Owner->GetPathName(), RepState->LastChangelistIndex);

	FChangelistIterator ChangelistIterator(Changed, 0);
	FRepHandleIterator HandleIterator(Owner, ChangelistIterator, Cmds, BaseHandleToCmdIndex, 0, 1, 0, Cmds.Num() - 1);

	SendProperties_r(RepState, Writer, bDoChecksum, HandleIterator, Data, 0, &SharedInfo);

	if (NumBits != Writer.GetNumBits())
	{
		// We actually wrote stuff
		WritePropertyHandle(Writer, 0, bDoChecksum);
	}
	else
	{
		Mark.Pop(Writer);
	}
}

static FORCEINLINE void WritePropertyHandle_BackwardsCompatible(
	FNetBitWriter&	Writer,
	uint32			NetFieldExportHandle,
	bool			bDoChecksum)
{
	const int NumStartingBits = Writer.GetNumBits();

	Writer.SerializeIntPacked(NetFieldExportHandle);
	UE_LOG(LogRepProperties, VeryVerbose, TEXT("WritePropertyHandle_BackwardsCompatible: %d"), NetFieldExportHandle);

#ifdef ENABLE_PROPERTY_CHECKSUMS
	if (bDoChecksum)
	{
		SerializeGenericChecksum(Writer);
	}
#endif

	NETWORK_PROFILER(GNetworkProfiler.TrackWritePropertyHandle(Writer.GetNumBits() - NumStartingBits, nullptr));
}

TSharedPtr<FNetFieldExportGroup> FRepLayout::CreateNetfieldExportGroup() const
{
	TSharedPtr<FNetFieldExportGroup> NetFieldExportGroup = TSharedPtr<FNetFieldExportGroup>(new FNetFieldExportGroup());

	NetFieldExportGroup->PathName = Owner->GetPathName();
	NetFieldExportGroup->NetFieldExports.SetNum(Cmds.Num());

	for (int32 i = 0; i < Cmds.Num(); i++)
	{
		FNetFieldExport NetFieldExport(
			i,
			Cmds[i].CompatibleChecksum,
			Cmds[i].Property ? Cmds[i].Property->GetFName() : NAME_None );

		NetFieldExportGroup->NetFieldExports[i] = NetFieldExport;
	}

	return NetFieldExportGroup;
}

static FORCEINLINE void WriteProperty_BackwardsCompatible(
	FNetBitWriter& Writer,
	const FRepLayoutCmd& Cmd,
	const int32 CmdIndex,
	const UObject* Owner,
	const FConstRepObjectDataBuffer Data,
	const bool bDoChecksum)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (GDoReplicationContextString> 0)
	{
		Writer.PackageMap->SetDebugContextString(FString::Printf(TEXT("%s - %s"), *Owner->GetPathName(), *Cmd.Property->GetPathName()));
	}
#endif

	const int32 NumStartBits = Writer.GetNumBits();

	FNetBitWriter TempWriter(Writer.PackageMap, 0);

	// This property changed, so send it
	Cmd.Property->NetSerializeItem(TempWriter, TempWriter.PackageMap, const_cast<uint8*>(Data.Data));
	UE_LOG(LogRepProperties, VeryVerbose, TEXT("WriteProperty_BackwardsCompatible: (Temp) NetSerializeItem"));

	uint32 NumBits = TempWriter.GetNumBits();
	Writer.SerializeIntPacked(NumBits);
	Writer.SerializeBits(TempWriter.GetData(), NumBits);
	UE_LOG(LogRepProperties, VeryVerbose, TEXT("WriteProperty_BackwardsComptaible: Write Temp, NumBits=%d"), NumBits);

	const int32 NumEndBits = Writer.GetNumBits();

	NETWORK_PROFILER(GNetworkProfiler.TrackReplicateProperty(Cmd.Property, NumEndBits - NumStartBits, nullptr));

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (GDoReplicationContextString> 0)
	{
		Writer.PackageMap->ClearDebugContextString();
	}
#endif

#ifdef ENABLE_PROPERTY_CHECKSUMS
	if (bDoChecksum)
	{
		SerializeReadWritePropertyChecksum(Cmd, CmdIndex, Data, Writer);
	}
#endif
}

void FRepLayout::SendProperties_BackwardsCompatible_r(
	FSendingRepState* RESTRICT RepState,
	UPackageMapClient* PackageMapClient,
	FNetFieldExportGroup* NetFieldExportGroup,
	FRepChangedPropertyTracker* ChangedTracker,
	FNetBitWriter& Writer,
	const bool bDoChecksum,
	FRepHandleIterator& HandleIterator,
	const FConstRepObjectDataBuffer SourceData) const
{
	int32 OldIndex = -1;

	FNetBitWriter TempWriter(Writer.PackageMap, 0);

	while (HandleIterator.NextHandle())
	{
		const FRepLayoutCmd& Cmd = Cmds[HandleIterator.CmdIndex];
		const FRepParentCmd& ParentCmd = Parents[Cmd.ParentIndex];

		UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_BackwardsCompatible_r: Parent=%d, Cmd=%d, ArrayIndex=%d"), Cmd.ParentIndex, HandleIterator.CmdIndex, HandleIterator.ArrayIndex);

		FConstRepObjectDataBuffer Data = (SourceData + Cmd) + HandleIterator.ArrayOffset;

		PackageMapClient->TrackNetFieldExport(NetFieldExportGroup, HandleIterator.CmdIndex);

		if (HandleIterator.ArrayElementSize> 0 && HandleIterator.ArrayIndex != OldIndex)
		{
			if (OldIndex != -1)
			{
				WritePropertyHandle_BackwardsCompatible(Writer, 0, bDoChecksum);
			}

			UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_BackwardsCompatible_r: WriteArrayIndex=%d"), HandleIterator.ArrayIndex)
			uint32 Index = HandleIterator.ArrayIndex + 1;
			Writer.SerializeIntPacked(Index);
			OldIndex = HandleIterator.ArrayIndex;
		}

		WritePropertyHandle_BackwardsCompatible(Writer, HandleIterator.CmdIndex + 1, bDoChecksum);

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			const FScriptArray* Array = (FScriptArray *)Data.Data;
			const FConstRepObjectDataBuffer ArrayData(Array->GetData());

			uint32 ArrayNum = Array->Num();

			// Read the jump offset
			// We won't need to actually jump over anything because we expect the change list to be pruned once we get here
			// But we can use it to verify we read the correct amount.
			const int32 ArrayChangedCount = HandleIterator.ChangelistIterator.Changed[HandleIterator.ChangelistIterator.ChangedIndex++];

			const int32 OldChangedIndex = HandleIterator.ChangelistIterator.ChangedIndex;

			TArray<FHandleToCmdIndex>& ArrayHandleToCmdIndex = *HandleIterator.HandleToCmdIndex[Cmd.RelativeHandle - 1].HandleToCmdIndex;

			FRepHandleIterator ArrayHandleIterator(HandleIterator.Owner, HandleIterator.ChangelistIterator, Cmds, ArrayHandleToCmdIndex, Cmd.ElementSize, ArrayNum, HandleIterator.CmdIndex + 1, Cmd.EndCmd - 1);

			check(ArrayHandleIterator.ArrayElementSize> 0);
			check(ArrayHandleIterator.NumHandlesPerElement> 0);

			TempWriter.Reset();

			// Write array num
			UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_BackwardsCompatible_r: (Temp) ArrayNum=%d"), ArrayNum);
			TempWriter.SerializeIntPacked(ArrayNum);

			if (ArrayNum> 0)
			{
				UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_BackwardsCompatible_r: (Temp) Array Recurse Properties"), ArrayNum);
				SendProperties_BackwardsCompatible_r(RepState, PackageMapClient, NetFieldExportGroup, ChangedTracker, TempWriter, bDoChecksum, ArrayHandleIterator, ArrayData);
			}

			uint32 EndArrayIndex = 0;
			TempWriter.SerializeIntPacked(EndArrayIndex);
			UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_BackwardsCompatible_r: (Temp) Array Footer"), ArrayNum);

			uint32 NumBits = TempWriter.GetNumBits();
			Writer.SerializeIntPacked(NumBits);
			Writer.SerializeBits(TempWriter.GetData(), NumBits);
			UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_BackwardsCompatible_r: Write Temp, NumBits=%d"), NumBits);

			check(HandleIterator.ChangelistIterator.ChangedIndex - OldChangedIndex == ArrayChangedCount);				// Make sure we read correct amount
			check(HandleIterator.ChangelistIterator.Changed[HandleIterator.ChangelistIterator.ChangedIndex] == 0);	// Make sure we are at the end

			HandleIterator.ChangelistIterator.ChangedIndex++;
			continue;
		}

		WriteProperty_BackwardsCompatible(Writer, Cmd, HandleIterator.CmdIndex, Owner, Data, bDoChecksum);
	}

	WritePropertyHandle_BackwardsCompatible(Writer, 0, bDoChecksum);
}

void FRepLayout::SendAllProperties_BackwardsCompatible_r(
	FSendingRepState* RESTRICT RepState,
	FNetBitWriter& Writer,
	const bool bDoChecksum,
	UPackageMapClient* PackageMapClient,
	FNetFieldExportGroup* NetFieldExportGroup,
	const int32 CmdStart,
	const int32 CmdEnd, 
	const FConstRepObjectDataBuffer SourceData) const
{
	FNetBitWriter TempWriter(Writer.PackageMap, 0);

	for (int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++)
	{
		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendAllProperties_BackwardsCompatible_r: Parent=%d, Cmd=%d"), Cmd.ParentIndex, CmdIndex);

		check(Cmd.Type != ERepLayoutCmdType::Return);

		PackageMapClient->TrackNetFieldExport(NetFieldExportGroup, CmdIndex);

		WritePropertyHandle_BackwardsCompatible(Writer, CmdIndex + 1, bDoChecksum);

		FConstRepObjectDataBuffer Data = SourceData + Cmd;

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{			
			const FScriptArray* Array = (FScriptArray *)Data.Data;
			const FConstRepObjectDataBuffer ArrayData(Array->GetData());

			TempWriter.Reset();

			// Write array num
			uint32 ArrayNum = Array->Num();
			TempWriter.SerializeIntPacked(ArrayNum);

			UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendAllProperties_BackwardsCompatible_r: (Temp) ArrayNum=%d"), ArrayNum);

			for (int32 i = 0; i < Array->Num(); i++)
			{
				uint32 ArrayIndex = i + 1;
				TempWriter.SerializeIntPacked(ArrayIndex);

				UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendAllProperties_BackwardsCompatible_r: (Temp) ArrayIndex=%d"), ArrayIndex);
				const int32 ArrayElementOffset = Cmd.ElementSize * i;
				SendAllProperties_BackwardsCompatible_r(RepState, TempWriter, bDoChecksum, PackageMapClient, NetFieldExportGroup, CmdIndex + 1, Cmd.EndCmd - 1, ArrayData + ArrayElementOffset);
			}

			uint32 EndArrayIndex = 0;
			TempWriter.SerializeIntPacked(EndArrayIndex);
			UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendAllProperties_BackwardsCompatible_r: (Temp) ArrayFooter"));

			uint32 NumBits = TempWriter.GetNumBits();
			Writer.SerializeIntPacked(NumBits);
			Writer.SerializeBits(TempWriter.GetData(), NumBits);
			UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendAllProperties_BackwardsCompatible_r: Write Temp, NumBits=%d"), NumBits);

			CmdIndex = Cmd.EndCmd - 1;		// The -1 to handle the ++ in the for loop
			continue;
		}

		WriteProperty_BackwardsCompatible(Writer, Cmd, CmdIndex, Owner, Data, bDoChecksum);
	}

	WritePropertyHandle_BackwardsCompatible(Writer, 0, bDoChecksum);
}

void FRepLayout::SendProperties_BackwardsCompatible(
	FSendingRepState* RESTRICT RepState,
	FRepChangedPropertyTracker* ChangedTracker,
	const FConstRepObjectDataBuffer Data,
	UNetConnection* Connection,
	FNetBitWriter& Writer,
	TArray<uint16>& Changed) const
{
	SCOPE_CYCLE_COUNTER(STAT_NetReplicateDynamicPropSendBackCompatTime);

	FBitWriterMark Mark(Writer);

#ifdef ENABLE_PROPERTY_CHECKSUMS
	const bool bDoChecksum = (GDoPropertyChecksum == 1);
	Writer.WriteBit(bDoChecksum ? 1 : 0);
#else
	const bool bDoChecksum = false;
#endif

	UPackageMapClient* PackageMapClient = (UPackageMapClient*)Connection->PackageMap;
	const FString OwnerPathName = Owner->GetPathName();
	UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_BackwardsCompatible: Owner=%s, LastChangelistIndex=%d"), *OwnerPathName, RepState ? RepState->LastChangelistIndex : INDEX_NONE);

	TSharedPtr<FNetFieldExportGroup> NetFieldExportGroup = PackageMapClient->GetNetFieldExportGroup(OwnerPathName);

	if (!NetFieldExportGroup.IsValid())
	{
		UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_BackwardsCompatible: Create Netfield Export Group."))
		NetFieldExportGroup = CreateNetfieldExportGroup();

		PackageMapClient->AddNetFieldExportGroup(OwnerPathName, NetFieldExportGroup);
	}

	const int32 NumBits = Writer.GetNumBits();

	if (Changed.Num() == 0)
	{
		UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_BackwardsCompatible: SendAllProperties."));
		SendAllProperties_BackwardsCompatible_r(RepState, Writer, bDoChecksum, PackageMapClient, NetFieldExportGroup.Get(), 0, Cmds.Num() - 1, Data);
	}
	else
	{
		UE_LOG(LogRepProperties, VeryVerbose, TEXT("SendProperties_BackwardsCompatible: SendProperties."));
		FChangelistIterator ChangelistIterator(Changed, 0);
		FRepHandleIterator HandleIterator(Owner, ChangelistIterator, Cmds, BaseHandleToCmdIndex, 0, 1, 0, Cmds.Num() - 1);

		SendProperties_BackwardsCompatible_r(RepState, PackageMapClient, NetFieldExportGroup.Get(), ChangedTracker, Writer, bDoChecksum, HandleIterator, Data);
	}

	if (NumBits == Writer.GetNumBits())
	{
		Mark.Pop(Writer);
	}
}

static bool ReceivePropertyHelper(
	FNetBitReader& Bunch, 
	FGuidReferencesMap* GuidReferencesMap,
	const int32 ElementOffset, 
	FRepShadowDataBuffer ShadowData,
	FRepObjectDataBuffer Data,
	TArray<UProperty*>* RepNotifies,
	const TArray<FRepParentCmd>& Parents,
	const TArray<FRepLayoutCmd>& Cmds,
	const int32 CmdIndex,
	const bool bDoChecksum,
	bool& bOutGuidsChanged,
	const bool bSkipSwapRoles)
{
	const FRepLayoutCmd& Cmd = Cmds[CmdIndex];
	const FRepParentCmd& Parent = Parents[Cmd.ParentIndex];

	// This swaps Role/RemoteRole as we write it
	const FRepLayoutCmd& SwappedCmd = (!bSkipSwapRoles && Parent.RoleSwapIndex != -1) ? Cmds[Parents[Parent.RoleSwapIndex].CmdStart] : Cmd;

	if (GuidReferencesMap)		// Don't reset unmapped guids here if we are told not to (assuming calling code is handling this)
	{
		// Let package map know we want to track and know about any guids that are unmapped during the serialize call
		Bunch.PackageMap->ResetTrackedGuids(true);
	}

	// Remember where we started reading from, so that if we have unmapped properties, we can re-deserialize from this data later
	FBitReaderMark Mark(Bunch);

	if (RepNotifies != nullptr && INDEX_NONE != Parent.RepNotifyNumParams)
	{
		// Copy current value over so we can check to see if it changed
		StoreProperty(Cmd, ShadowData + Cmd, Data + SwappedCmd);

		// Read the property
		Cmd.Property->NetSerializeItem(Bunch, Bunch.PackageMap, Data + SwappedCmd);
		UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceivePropertyHelper: NetSerializeItem (WithRepNotify)"));

		// Check to see if this property changed
		if (Parent.RepNotifyCondition == REPNOTIFY_Always || !PropertiesAreIdentical(Cmd, ShadowData + Cmd, Data + SwappedCmd))
		{
			RepNotifies->AddUnique(Parent.Property);
		}
		else
		{
			UE_CLOG(LogSkippedRepNotifies > 0, LogRep, Display, TEXT("2 FReceivedPropertiesStackState Skipping RepNotify for property %s because local value has not changed."), *Cmd.Property->GetName());
		}
	}
	else
	{
		Cmd.Property->NetSerializeItem(Bunch, Bunch.PackageMap, Data + SwappedCmd);
		UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceivePropertyHelper: NetSerializeItem (WithoutRepNotify)"));
	}

#ifdef ENABLE_PROPERTY_CHECKSUMS
	if (bDoChecksum)
	{
		SerializeReadWritePropertyChecksum(Cmd, CmdIndex, FConstRepObjectDataBuffer(Data + SwappedCmd), Bunch);
	}
#endif

	if (GuidReferencesMap)
	{
		const int32 AbsOffset = ElementOffset + SwappedCmd.Offset;

		// Loop over all de-serialized network guids and track them so we can manage their pointers as their replicated reference goes in/out of relevancy
		const TSet<FNetworkGUID>& TrackedUnmappedGuids = Bunch.PackageMap->GetTrackedUnmappedGuids();
		const TSet<FNetworkGUID>& TrackedDynamicMappedGuids = Bunch.PackageMap->GetTrackedDynamicMappedGuids();

		const bool bHasUnmapped = TrackedUnmappedGuids.Num()> 0;

		FGuidReferences* GuidReferences = GuidReferencesMap->Find(AbsOffset);

		if (TrackedUnmappedGuids.Num() > 0 || TrackedDynamicMappedGuids.Num()> 0)
		{
			if (GuidReferences != nullptr)
			{
				check(GuidReferences->CmdIndex == CmdIndex);
				check(GuidReferences->ParentIndex == Cmd.ParentIndex);

				// If we're already tracking the guids, re-copy lists only if they've changed
				if (!NetworkGuidSetsAreSame(GuidReferences->UnmappedGUIDs, TrackedUnmappedGuids))
				{
					bOutGuidsChanged = true;
				}
				else if (!NetworkGuidSetsAreSame(GuidReferences->MappedDynamicGUIDs, TrackedDynamicMappedGuids))
				{
					bOutGuidsChanged = true;
				}
			}
			
			if (GuidReferences == nullptr || bOutGuidsChanged)
			{
				// First time tracking these guids (or guids changed), so add (or replace) new entry
				GuidReferencesMap->Add(AbsOffset, FGuidReferences(Bunch, Mark, TrackedUnmappedGuids, TrackedDynamicMappedGuids, Cmd.ParentIndex, CmdIndex));
				bOutGuidsChanged = true;
			}
		}
		else
		{
			// If we don't have any unmapped guids, then make sure to remove the entry so we don't serialize old data when we update unmapped objects
			if (GuidReferences != nullptr)
			{
				GuidReferencesMap->Remove(AbsOffset);
				bOutGuidsChanged = true;
			}
		}

		// Stop tracking unmapped objects
		Bunch.PackageMap->ResetTrackedGuids(false);

		return bHasUnmapped;
	}

	return false;
}

static FGuidReferencesMap* PrepReceivedArray(
	const int32 ArrayNum,
	FScriptArray* ShadowArray,
	FScriptArray* DataArray,
	FGuidReferencesMap* ParentGuidReferences,
	const int32 AbsOffset,
	const FRepParentCmd& Parent, 
	const FRepLayoutCmd& Cmd, 
	const int32 CmdIndex,
	FRepShadowDataBuffer* OutShadowBaseData,
	FRepObjectDataBuffer* OutBaseData,
	TArray<UProperty*>* RepNotifies)
{
	FGuidReferences* NewGuidReferencesArray = nullptr;

	if (ParentGuidReferences != nullptr)
	{
		// Since we don't know yet if something under us could be unmapped, go ahead and allocate an array container now
		NewGuidReferencesArray = ParentGuidReferences->Find(AbsOffset);

		if (NewGuidReferencesArray == nullptr)
		{
			NewGuidReferencesArray = &ParentGuidReferences->FindOrAdd(AbsOffset);

			NewGuidReferencesArray->Array = new FGuidReferencesMap;
			NewGuidReferencesArray->ParentIndex = Cmd.ParentIndex;
			NewGuidReferencesArray->CmdIndex = CmdIndex;
		}

		check(NewGuidReferencesArray != nullptr);
		check(NewGuidReferencesArray->ParentIndex == Cmd.ParentIndex);
		check(NewGuidReferencesArray->CmdIndex == CmdIndex);
	}

	if (RepNotifies != nullptr && INDEX_NONE != Parent.RepNotifyNumParams)
	{
		if (DataArray->Num() != ArrayNum || Parent.RepNotifyCondition == REPNOTIFY_Always)
		{
			(*RepNotifies).AddUnique(Parent.Property);
		}
		else
		{
			UE_CLOG(LogSkippedRepNotifies> 0, LogRep, Display, TEXT("1 FReceivedPropertiesStackState Skipping RepNotify for property %s because local value has not changed."), *Cmd.Property->GetName());
		}
	}

	check(CastChecked<UArrayProperty>(Cmd.Property) != nullptr);

	// Resize arrays if needed
	FScriptArrayHelper ArrayHelper((UArrayProperty*)Cmd.Property, DataArray);
	ArrayHelper.Resize(ArrayNum);

	// Re-compute the base data values since they could have changed after the resize above
	*OutBaseData = DataArray->GetData();
	*OutShadowBaseData = nullptr;

	// Only resize the shadow data array if we're actually tracking RepNotifies
	if (RepNotifies != nullptr)
	{
		check(ShadowArray != nullptr);

		FScriptArrayHelper ShadowArrayHelper((UArrayProperty*)Cmd.Property, ShadowArray);
		ShadowArrayHelper.Resize(ArrayNum);

		*OutShadowBaseData = ShadowArray->GetData();
	}

	return NewGuidReferencesArray ? NewGuidReferencesArray->Array : nullptr;
}

/** Struct containing parameters that don't change throughout recursion of ReceiveProperties_r */
struct FReceivePropertiesSharedParams
{
	const bool bDoChecksum;
	const bool bSkipRoleSwap;
	TArray<UProperty*>* RepNotifies;
	FNetBitReader& Bunch;
	bool& bOutHasUnmapped;
	bool& bOutGuidsChanged;
	const TArray<FRepParentCmd>& Parents;
	const TArray<FRepLayoutCmd>& Cmds;
	uint16 ReadHandle = 0;
};

/** Struct containing parameters that do change as we recurse into ReceiveProperties_r */
struct FReceivePropertiesStackParams
{
	FRepObjectDataBuffer ObjectData;
	FRepShadowDataBuffer ShadowData;
	FGuidReferencesMap* GuidReferences;
	const int32 CmdStart;
	const int32 CmdEnd;
	uint32 ArrayElementOffset = 0;
	uint16 CurrentHandle = 0;
};

static FORCEINLINE void ReadPropertyHandle(FReceivePropertiesSharedParams& Params)
{
	uint32 Handle = 0;
	Params.Bunch.SerializeIntPacked(Handle);

	UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReadPropertyHandle: Handle=%d"), Handle);

#ifdef ENABLE_PROPERTY_CHECKSUMS
	if (Params.bDoChecksum)
	{
		SerializeGenericChecksum(Params.Bunch);
	}
#endif

	Params.ReadHandle = Handle;
}

static bool ReceiveProperties_r(FReceivePropertiesSharedParams& Params, FReceivePropertiesStackParams& StackParams)
{
	// Note, it's never possible for the ObjectData to be nullptr.
	// However, it is possible for the ShadowData to be nullptr.
	// At the top level, ShadowData will always be valid.
	// If RepNotifies aren't being used, PrepReceivedArray will ignore the current shadow data and just null out the next level's shadow data.
	// If RepNotifies aren't being used, ReceivePropertyHelper will ignore the shadow data.

	check(StackParams.GuidReferences != nullptr);
	for (int32 CmdIndex = StackParams.CmdStart; CmdIndex < StackParams.CmdEnd; ++CmdIndex)
	{
		const FRepLayoutCmd& Cmd = Params.Cmds[CmdIndex];
		check(ERepLayoutCmdType::Return != Cmd.Type);

		++StackParams.CurrentHandle;
		if (StackParams.CurrentHandle != Params.ReadHandle)
		{
			// Skip this property.
			if (ERepLayoutCmdType::DynamicArray == Cmd.Type)
			{
				CmdIndex = Cmd.EndCmd - 1;
			}

			UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceiveProperties_r: Skipping Property Parent=%d, Cmd=%d, CurrentHandle=%d, ReadHandle=%d"),
				Cmd.ParentIndex, CmdIndex, StackParams.CurrentHandle, Params.ReadHandle);
		}
		else
		{
			UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceiveProperties_r: Parent=%d, Cmd=%d"), Cmd.ParentIndex, CmdIndex);
				
			if (ERepLayoutCmdType::DynamicArray == Cmd.Type)
			{
				const FRepParentCmd& Parent = Params.Parents[Cmd.ParentIndex];

				// Don't worry about checking the ShadowData for nullptr here.
				// We're either:
				//	1. At the top level and it's valid
				//	2. Tracking RepNotifies and it's valid.
				//	3. We aren't tracking RepNotifies in which case it will be ignored.
				FScriptArray* ShadowArray = (FScriptArray*)(StackParams.ShadowData + Cmd).Data;
				FScriptArray* ObjectArray = (FScriptArray*)(StackParams.ObjectData + Cmd).Data;

				// Setup a new Stack State for our array.
				FReceivePropertiesStackParams ArrayStackParams{
					nullptr,
					nullptr,
					nullptr,
					CmdIndex + 1,
					Cmd.EndCmd - 1,
				};

				// These buffers will track the dynamic array memory.
				FRepObjectDataBuffer ObjectArrayBuffer = StackParams.ObjectData;
				FRepShadowDataBuffer ShadowArrayBuffer = StackParams.ShadowData;

				// Read the number of elements in the array, and resize as necessary.
				uint16 ArrayNum = 0;
				Params.Bunch << ArrayNum;

				UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceiveProperties_r: ArrayNum=%d"), ArrayNum);

				ArrayStackParams.GuidReferences = PrepReceivedArray(
					ArrayNum,
					ShadowArray,
					ObjectArray,
					StackParams.GuidReferences,

					// We pass in the ArrayElementOffset + Offset here, because PrepReceivedArray expects the absolute offset.
					StackParams.ArrayElementOffset + Cmd.Offset,
					Params.Parents[Cmd.ParentIndex],
					Cmd,
					CmdIndex,
					&ShadowArrayBuffer,
					&ObjectArrayBuffer,
					Params.RepNotifies);

				// Read the next array handle.
				ReadPropertyHandle(Params);

				// It's possible that we've already hit the terminator.
				// Maybe this was just a change of size of the array (like removing an element from the end).
				if (0 != Params.ReadHandle)
				{
					const int32 ObjectArrayNum = ObjectArray->Num();
					for (int32 i = 0; i < ObjectArrayNum; ++i)
					{
						const int32 ElementOffset = i * Cmd.ElementSize;

						ArrayStackParams.ObjectData = ObjectArrayBuffer + ElementOffset;
						ArrayStackParams.ShadowData = ShadowArrayBuffer ? (ShadowArrayBuffer + ElementOffset) : nullptr;
						ArrayStackParams.ArrayElementOffset = ElementOffset;

						UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceivePropertiesArray_r: Recursing - Parent=%d, Cmd=%d, Index=%d"), Cmd.ParentIndex, CmdIndex, i);
						if (!ReceiveProperties_r(Params, ArrayStackParams))
						{
							UE_LOG(LogRep, Error, TEXT("ReceiveProperties_r: Failed to receive property, Array Property - Property=%s, Parent=%d, Cmd=%d, Index=%d"), *Parent.CachedPropertyName.ToString(), Cmd.ParentIndex, CmdIndex, i);
							return false;
						}
					}

					// Make sure we've hit the array terminator.
					if (0 != Params.ReadHandle)
					{
						UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_r: Failed to receive property, Array Property Improperly Terminated - Property=%s, Parent=%d, CmdIndex=%d, ReadHandle=%d"), *Parent.CachedPropertyName.ToString(), Cmd.ParentIndex, CmdIndex, Params.ReadHandle);
						return false;
					}
				}

				// Skip passed the inner array properties.
				CmdIndex = Cmd.EndCmd - 1;
			}
			else
			{
				// Go ahead and receive the property.
				if (ReceivePropertyHelper(
					Params.Bunch,
					StackParams.GuidReferences,
					StackParams.ArrayElementOffset,
					StackParams.ShadowData,
					StackParams.ObjectData,
					Params.RepNotifies,
					Params.Parents,
					Params.Cmds,
					CmdIndex,
					Params.bDoChecksum,
					Params.bOutGuidsChanged,
					Params.bSkipRoleSwap))
				{
					Params.bOutHasUnmapped = true;
				}
			}

			// TODO: Might be worth doing this before and after ReadNextHandle, or having ReadNextHandle check for errors?
			if (Params.Bunch.IsError())
			{
				const FRepParentCmd& Parent = Params.Parents[Cmd.ParentIndex];
				UE_LOG(LogRep, Error, TEXT("ReceiveProperties_r: Failed to receive property, BunchIsError - Property=%s, Parent=%d, Cmd=%d, ReadHandle=%d"), *Parent.CachedPropertyName.ToString(), Cmd.ParentIndex, CmdIndex, Params.ReadHandle);
				return false;
			}

			// Read the next property handle to serialize.
			// If we don't have any more properties, this could be a terminator.
			ReadPropertyHandle(Params);
		}
	}

	return true;
}

bool FRepLayout::ReceiveProperties(
	UActorChannel* OwningChannel,
	UClass* InObjectClass,
	FReceivingRepState* RESTRICT RepState,
	FRepObjectDataBuffer Data,
	FNetBitReader& InBunch,
	bool& bOutHasUnmapped,
	bool& bOutGuidsChanged,
	const EReceivePropertiesFlags ReceiveFlags) const
{
	check(InObjectClass == Owner);

	const bool bEnableRepNotifies = EnumHasAnyFlags(ReceiveFlags, EReceivePropertiesFlags::RepNotifies);

	if (OwningChannel->Connection->InternalAck)
	{
		return ReceiveProperties_BackwardsCompatible(OwningChannel->Connection, RepState, Data, InBunch, bOutHasUnmapped, bEnableRepNotifies, bOutGuidsChanged);
	}

#ifdef ENABLE_PROPERTY_CHECKSUMS
	const bool bDoChecksum = InBunch.ReadBit() ? true : false;
#else
	const bool bDoChecksum = false;
#endif

	UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceiveProperties: Owner=%s"), *Owner->GetPathName());

	bOutHasUnmapped = false;

	// If we've gotten this far, it means that the server must have sent us something.
	// That should only happen if there's actually commands to process.
	// If this is hit, it may mean the Client and Server have different properties!
	check(!IsEmpty());

	FReceivePropertiesSharedParams Params{
		bDoChecksum,
		// We can skip swapping roles if we're not an Actor layout, or if we've been explicitly told we can skip.
		EnumHasAnyFlags(ReceiveFlags, EReceivePropertiesFlags::SkipRoleSwap) || !EnumHasAnyFlags(Flags, ERepLayoutFlags::IsActor),
		EnumHasAnyFlags(ReceiveFlags, EReceivePropertiesFlags::RepNotifies) ? &RepState->RepNotifies : nullptr,
		InBunch,
		bOutHasUnmapped,
		bOutGuidsChanged,
		Parents,
		Cmds
	};

	FReceivePropertiesStackParams StackParams{
		FRepObjectDataBuffer(Data),
		FRepShadowDataBuffer(RepState->StaticBuffer.GetData()),
		&RepState->GuidReferencesMap,
		0,
		Cmds.Num() - 1
	};

	// Read the first handle, and then start receiving properties.
	ReadPropertyHandle(Params);
	if (ReceiveProperties_r(Params, StackParams))
	{
		if (0 != Params.ReadHandle)
		{
			UE_LOG(LogRep, Error, TEXT("ReceiveProperties: Invalid property terminator handle - Handle=%d"), Params.ReadHandle);
			return false;
		}

#ifdef ENABLE_SUPER_CHECKSUMS
		if (bDoChecksum)
		{
			ValidateWithChecksum<>(FConstRepShadowDataBuffer(RepState->StaticBuffer.GetData()), InBunch);
		}
#endif

		return true;
	}

	return false;
}

bool FRepLayout::ReceiveProperties_BackwardsCompatible(
	UNetConnection* Connection,
	FReceivingRepState* RESTRICT RepState,
	FRepObjectDataBuffer Data,
	FNetBitReader& InBunch,
	bool& bOutHasUnmapped,
	const bool bEnableRepNotifies,
	bool& bOutGuidsChanged) const
{
#ifdef ENABLE_PROPERTY_CHECKSUMS
	const bool bDoChecksum = InBunch.ReadBit() ? true : false;
#else
	const bool bDoChecksum = false;
#endif

	bOutHasUnmapped = false;

	const FString OwnerPathName = Owner->GetPathName();
	TSharedPtr<FNetFieldExportGroup> NetFieldExportGroup = ((UPackageMapClient*)Connection->PackageMap)->GetNetFieldExportGroup(OwnerPathName);

	UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceiveProperties_BackwardsCompatible: Owner=%s, NetFieldExportGroupFound=%d"), *OwnerPathName, !!NetFieldExportGroup.IsValid());

	return ReceiveProperties_BackwardsCompatible_r(RepState, NetFieldExportGroup.Get(), InBunch, 0, Cmds.Num() - 1, (bEnableRepNotifies && RepState) ? RepState->StaticBuffer.GetData() : nullptr, Data, Data, RepState ? &RepState->GuidReferencesMap : nullptr, bOutHasUnmapped, bOutGuidsChanged);
}

int32 FRepLayout::FindCompatibleProperty(
	const int32		CmdStart,
	const int32		CmdEnd,
	const uint32	Checksum) const
{
	for (int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++)
	{
		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		check(Cmd.Type != ERepLayoutCmdType::Return);

		if (Cmd.CompatibleChecksum == Checksum)
		{
			return CmdIndex;
		}

		// Jump over entire array and inner properties if checksum didn't match
		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			CmdIndex = Cmd.EndCmd - 1;
		}
	}

	return -1;
}

bool FRepLayout::ReceiveProperties_BackwardsCompatible_r(
	FReceivingRepState* RESTRICT RepState,
	FNetFieldExportGroup* NetFieldExportGroup,
	FNetBitReader& Reader,
	const int32 CmdStart,
	const int32 CmdEnd,
	FRepShadowDataBuffer ShadowData,
	FRepObjectDataBuffer OldData,
	FRepObjectDataBuffer Data,
	FGuidReferencesMap* GuidReferencesMap,
	bool& bOutHasUnmapped,
	bool& bOutGuidsChanged) const
{
	auto ReadHandle = [this, &Reader](uint32& Handle) -> bool
	{
		Reader.SerializeIntPacked(Handle);

		if (Reader.IsError())
		{
			UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Error reading handle. Owner: %s"), *Owner->GetName());
			return false;
		}

		UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceiveProperties_BackwardsCompatible_r: NetFieldExportHandle=%d"), Handle);
		return true;
	};

	if (NetFieldExportGroup == nullptr)
	{
		uint32 NetFieldExportHandle = 0;
		if (!ReadHandle(NetFieldExportHandle))
		{
			return false;
		}
		else if (NetFieldExportHandle != 0)
		{
			UE_CLOG(!FApp::IsUnattended(), LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: NetFieldExportGroup == nullptr. Owner: %s, NetFieldExportHandle: %u"), *Owner->GetName(), NetFieldExportHandle);
			Reader.SetError();
			ensure(false);
			return false;
		}
		else
		{
			return true;
		}
	}

	while (true)
	{
		uint32 NetFieldExportHandle = 0;
		if (!ReadHandle(NetFieldExportHandle))
		{
			return false;
		}

		if (NetFieldExportHandle == 0)
		{
			// We're done
			break;
		}

		// We purposely add 1 on save, so we can reserve 0 for "done"
		NetFieldExportHandle--;

		if (!ensure(NetFieldExportHandle < (uint32)NetFieldExportGroup->NetFieldExports.Num()))
		{
			UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: NetFieldExportHandle> NetFieldExportGroup->NetFieldExports.Num(). Owner: %s, NetFieldExportHandle: %u"), *Owner->GetName(), NetFieldExportHandle);
			return false;
		}

		const uint32 Checksum = NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].CompatibleChecksum;

		if (!ensure(Checksum != 0))
		{
			UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Checksum == 0. Owner: %s, Name: %s, NetFieldExportHandle: %i"), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].ExportName.ToString(), NetFieldExportHandle);
			return false;
		}

		uint32 NumBits = 0;
		Reader.SerializeIntPacked(NumBits);

		UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceiveProperties_BackwardsCompatible_r: NumBits=%d"), NumBits);

		if (Reader.IsError())
		{
			UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Error reading num bits. Owner: %s, Name: %s, NetFieldExportHandle: %i, Checksum: %u"), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].ExportName.ToString(), NetFieldExportHandle, Checksum);
			return false;
		}

		FNetBitReader TempReader;
		
		TempReader.PackageMap = Reader.PackageMap;
		TempReader.SetData(Reader, NumBits);

		if (Reader.IsError())
		{
			UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Error reading payload. Owner: %s, Name: %s, NetFieldExportHandle: %i, Checksum: %u"), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].ExportName.ToString(), NetFieldExportHandle, Checksum);
			return false;
		}

		if (NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].bIncompatible)
		{
			continue;		// We've already warned that this property doesn't load anymore
		}

		// Find this property
		const int32 CmdIndex = FindCompatibleProperty(CmdStart, CmdEnd, Checksum);

		if (CmdIndex == -1)
		{
			UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Property not found. Owner: %s, Name: %s, NetFieldExportHandle: %i, Checksum: %u"), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].ExportName.ToString(), NetFieldExportHandle, Checksum);

			// Mark this property as incompatible so we don't keep spamming this warning
			NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].bIncompatible = true;
			continue;
		}

		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			uint32 ArrayNum = 0;
			TempReader.SerializeIntPacked(ArrayNum);

			UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceiveProperties_BackwardsCompatible_r: ArrayNum=%d"), ArrayNum);

			if (TempReader.IsError())
			{
				UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Error reading ArrayNum. Owner: %s, Name: %s, NetFieldExportHandle: %i, Checksum: %u"), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].ExportName.ToString(), NetFieldExportHandle, Checksum);
				return false;
			}

			const int32 AbsOffset = (Data.Data - OldData.Data) + Cmd.Offset;

			FScriptArray* DataArray = (FScriptArray*)(Data + Cmd).Data;
			FScriptArray* ShadowArray = ShadowData ? (FScriptArray*)(ShadowData + Cmd).Data : nullptr;

			FRepObjectDataBuffer LocalData = Data;
			FRepShadowDataBuffer LocalShadowData = ShadowData;

			FGuidReferencesMap* NewGuidReferencesArray = PrepReceivedArray(
				ArrayNum,
				ShadowArray,
				DataArray,
				GuidReferencesMap,
				AbsOffset,
				Parents[Cmd.ParentIndex],
				Cmd,
				CmdIndex,
				&LocalShadowData,
				&LocalData,
				ShadowData ? &RepState->RepNotifies : nullptr);

			// Read until we read all array elements
			while (true)
			{
				uint32 Index = 0;
				TempReader.SerializeIntPacked(Index);

				UE_LOG(LogRepProperties, VeryVerbose, TEXT("ReceiveProperties_BackwardsCompatible_r: ArrayIndex=%d"), Index);

				if (TempReader.IsError())
				{
					UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Error reading array index. Index: %i, Owner: %s, Name: %s, NetFieldExportHandle: %i, Checksum: %u"), Index, *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].ExportName.ToString(), NetFieldExportHandle, Checksum);
					return false;
				}

				if (Index == 0)
				{
					// At this point, the 0 either signifies:
					//	An array terminator, at which point we're done.
					//	An array element terminator, which could happen if the array had tailing elements removed.
					if (TempReader.GetBitsLeft() == 8)
					{
						// We have bits left over, so see if its the Array Terminator.
						// This should be 0
						uint32 Terminator;
						TempReader.SerializeIntPacked(Terminator);

						if (Terminator != 0)
						{
							UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Invalid array terminator. Owner: %s, Name: %s, NetFieldExportHandle: %i, Terminator: %d"), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].ExportName.ToString(), NetFieldExportHandle, Terminator);
							return false;
						}
					}

					// We're done
					break;
				}

				// Shift all indexes down since 0 represents null handle
				Index--;

				if (!ensure(Index < ArrayNum))
				{
					UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Array index out of bounds. Index: %i, ArrayNum: %i, Owner: %s, Name: %s, NetFieldExportHandle: %i, Checksum: %u"), Index, ArrayNum, *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].ExportName.ToString(), NetFieldExportHandle, Checksum);
					return false;
				}

				const int32 ArrayElementOffset = Index * Cmd.ElementSize;

				FRepObjectDataBuffer ElementData = LocalData + ArrayElementOffset;
				FRepShadowDataBuffer ElementShadowData = LocalShadowData ? LocalShadowData + ArrayElementOffset : nullptr;

				if (!ReceiveProperties_BackwardsCompatible_r(RepState, NetFieldExportGroup, TempReader, CmdIndex + 1, Cmd.EndCmd - 1, ElementShadowData, LocalData, ElementData, NewGuidReferencesArray, bOutHasUnmapped, bOutGuidsChanged))
				{
					return false;
				}

				if (TempReader.IsError())
				{
					UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Error reading array index element payload. Index: %i, Owner: %s, Name: %s, NetFieldExportHandle: %i, Checksum: %u"), Index, *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].ExportName.ToString(), NetFieldExportHandle, Checksum);
					return false;
				}
			}

			if (TempReader.GetBitsLeft() != 0)
			{
				UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Array didn't read proper number of bits. Owner: %s, Name: %s, NetFieldExportHandle: %i, Checksum: %u, BitsLeft:%d"), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].ExportName.ToString(), NetFieldExportHandle, Checksum, TempReader.GetBitsLeft());
				return false;
			}
		}
		else
		{
			const int32 ElementOffset = (Data.Data - OldData.Data);

			if (ReceivePropertyHelper(
				TempReader,
				GuidReferencesMap,
				ElementOffset,
				ShadowData,
				Data,
				ShadowData ? &RepState->RepNotifies : nullptr,
				Parents,
				Cmds,
				CmdIndex,
				false,
				bOutGuidsChanged,

				// We can skip role swapping if we're not an actor.
				!EnumHasAnyFlags(Flags, ERepLayoutFlags::IsActor)))
			{
				bOutHasUnmapped = true;
			}

			if (TempReader.GetBitsLeft() != 0)
			{
				UE_LOG(LogRep, Warning, TEXT("ReceiveProperties_BackwardsCompatible_r: Property didn't read proper number of bits. Owner: %s, Name: %s, NetFieldExportHandle: %i, Checksum: %u, BitsLeft:%d"), *Owner->GetName(), *NetFieldExportGroup->NetFieldExports[NetFieldExportHandle].ExportName.ToString(), NetFieldExportHandle, Checksum, TempReader.GetBitsLeft());
				return false;
			}
		}
	}

	return true;
}

FGuidReferences::~FGuidReferences()
{
	if (Array != NULL)
	{
		delete Array;
		Array = NULL;
	}
}

void FRepLayout::GatherGuidReferences_r(
	const FGuidReferencesMap* GuidReferencesMap,
	TSet<FNetworkGUID>& OutReferencedGuids,
	int32& OutTrackedGuidMemoryBytes) const
{
	for (const auto& GuidReferencePair : *GuidReferencesMap)
	{
		const FGuidReferences& GuidReferences = GuidReferencePair.Value;

		if (GuidReferences.Array != NULL)
		{
			check(Cmds[GuidReferences.CmdIndex].Type == ERepLayoutCmdType::DynamicArray);

			GatherGuidReferences_r(GuidReferences.Array, OutReferencedGuids, OutTrackedGuidMemoryBytes);
			continue;
		}

		OutTrackedGuidMemoryBytes += GuidReferences.Buffer.Num();

		OutReferencedGuids.Append(GuidReferences.UnmappedGUIDs);
		OutReferencedGuids.Append(GuidReferences.MappedDynamicGUIDs);
	}
}

void FRepLayout::GatherGuidReferences(
	FReceivingRepState* RESTRICT RepState,
	FNetDeltaSerializeInfo& Params,
	TSet<FNetworkGUID>& OutReferencedGuids,
	int32& OutTrackedGuidMemoryBytes) const
{
	if (!IsEmpty())
	{
		GatherGuidReferences_r(&RepState->GuidReferencesMap, OutReferencedGuids, OutTrackedGuidMemoryBytes);

		// Custom Delta Properties
		if (LifetimeCustomPropertyState)
		{
			FRepObjectDataBuffer ObjectData(Params.Object);
			const int32 NumLifetimeCustomDeltaProperties = LifetimeCustomPropertyState->GetNumCustomDeltaProperties();

			for (int32 CustomDeltaIndex = 0; CustomDeltaIndex < NumLifetimeCustomDeltaProperties; ++CustomDeltaIndex)
			{
				const FLifetimeCustomDeltaProperty& CustomDeltaProperty = LifetimeCustomPropertyState->GetCustomDeltaProperty(CustomDeltaIndex);
				const FRepParentCmd& Parent = Parents[CustomDeltaProperty.PropertyRepIndex];

				// Static cast is safe here, because this property wouldn't have been marked CustomDelta otherwise.
				UStructProperty* StructProperty = static_cast<UStructProperty*>(Parent.Property);
				UScriptStruct::ICppStructOps* CppStructOps = StructProperty->Struct->GetCppStructOps();

				FNetDeltaSerializeInfo TempParams = Params;
				TempParams.Struct = StructProperty->Struct;
				TempParams.CustomDeltaIndex = CustomDeltaIndex;
				TempParams.Data = ObjectData + Parent;

				CppStructOps->NetDeltaSerialize(TempParams, TempParams.Data);
			}
		}
	}
}

bool FRepLayout::MoveMappedObjectToUnmapped_r(FGuidReferencesMap* GuidReferencesMap, const FNetworkGUID& GUID) const
{
	bool bFoundGUID = false;

	for (auto& GuidReferencePair : *GuidReferencesMap)
	{
		FGuidReferences& GuidReferences = GuidReferencePair.Value;

		if (GuidReferences.Array != NULL)
		{
			check(Cmds[GuidReferences.CmdIndex].Type == ERepLayoutCmdType::DynamicArray);

			if (MoveMappedObjectToUnmapped_r(GuidReferences.Array, GUID))
			{
				bFoundGUID = true;
			}
			continue;
		}

		if (GuidReferences.MappedDynamicGUIDs.Contains(GUID))
		{
			GuidReferences.MappedDynamicGUIDs.Remove(GUID);
			GuidReferences.UnmappedGUIDs.Add(GUID);
			bFoundGUID = true;
		}
	}

	return bFoundGUID;
}

bool FRepLayout::MoveMappedObjectToUnmapped(
	FReceivingRepState* RESTRICT RepState,
	FNetDeltaSerializeInfo& Params,
	const FNetworkGUID& GUID) const
{
	bool bFound = false;

	if (!IsEmpty())
	{
		bFound = MoveMappedObjectToUnmapped_r(&RepState->GuidReferencesMap, GUID);

		// Custom Delta Properties
		if (LifetimeCustomPropertyState && Params.Object)
		{
			FRepObjectDataBuffer ObjectData(Params.Object);
			const int32 NumLifetimeCustomDeltaProperties = LifetimeCustomPropertyState->GetNumCustomDeltaProperties();

			for (int32 CustomDeltaIndex = 0; CustomDeltaIndex < NumLifetimeCustomDeltaProperties; ++CustomDeltaIndex)
			{
				const FLifetimeCustomDeltaProperty& CustomDeltaProperty = LifetimeCustomPropertyState->GetCustomDeltaProperty(CustomDeltaIndex);
				const FRepParentCmd& Parent = Parents[CustomDeltaProperty.PropertyRepIndex];

				// Static cast is safe here, because this property wouldn't have been marked CustomDelta otherwise.
				UStructProperty* StructProperty = static_cast<UStructProperty*>(Parent.Property);
				UScriptStruct::ICppStructOps* CppStructOps = StructProperty->Struct->GetCppStructOps();

				FNetDeltaSerializeInfo TempParams = Params;

				TempParams.Struct = StructProperty->Struct;
				TempParams.Data = ObjectData + Parent;
				TempParams.CustomDeltaIndex = CustomDeltaIndex;
				TempParams.bOutHasMoreUnmapped = false;
				TempParams.bOutSomeObjectsWereMapped = false;

				if (CppStructOps->NetDeltaSerialize(TempParams, TempParams.Data))
				{
					bFound = true;
				}

				Params.bOutHasMoreUnmapped |= TempParams.bOutHasMoreUnmapped;
				Params.bOutSomeObjectsWereMapped |= TempParams.bOutSomeObjectsWereMapped;
			}
		}
	}

	return bFound;
}

void FRepLayout::UpdateUnmappedObjects_r(
	FReceivingRepState* RESTRICT RepState,
	FGuidReferencesMap* GuidReferencesMap,
	UObject* OriginalObject,
	UPackageMap* PackageMap,
	FRepShadowDataBuffer ShadowData,
	FRepObjectDataBuffer Data,
	const int32 MaxAbsOffset,
	bool& bCalledPreNetReceive,
	bool& bOutSomeObjectsWereMapped,
	bool& bOutHasMoreUnmapped) const
{
	for (auto It = GuidReferencesMap->CreateIterator(); It; ++It)
	{
		const int32 AbsOffset = It.Key();

		if (AbsOffset >= MaxAbsOffset)
		{
			// Array must have shrunk, we can remove this item
			UE_LOG(LogRep, VeryVerbose, TEXT("UpdateUnmappedObjects_r: REMOVED unmapped property: AbsOffset>= MaxAbsOffset. Offset: %i"), AbsOffset);
			It.RemoveCurrent();
			continue;
		}

		FGuidReferences& GuidReferences = It.Value();
		const FRepLayoutCmd& Cmd = Cmds[GuidReferences.CmdIndex];
		const FRepParentCmd& Parent = Parents[GuidReferences.ParentIndex];

		// Make sure if we're touching an array element, we use the correct offset for shadow values.
		// This should always be safe, because MaxAbsOffset will account for ShadowArray size for arrays.
		// For non array properties, AbsOffset should always equal Cmd.Offset.
		const int32 ShadowOffset = (AbsOffset - Cmd.Offset) + Cmd.ShadowOffset;

		if (GuidReferences.Array != nullptr)
		{
			check(Cmd.Type == ERepLayoutCmdType::DynamicArray);

			if (ShadowData)
			{
				FScriptArray* ShadowArray = (FScriptArray*)(ShadowData + ShadowOffset).Data;
				FScriptArray* Array = (FScriptArray*)(Data + AbsOffset).Data;

				FRepShadowDataBuffer ShadowArrayData(ShadowArray->GetData());
				FRepObjectDataBuffer ArrayData(Array->GetData());

				const int32 NewMaxOffset = FMath::Min(ShadowArray->Num() * Cmd.ElementSize, Array->Num() * Cmd.ElementSize);

				UpdateUnmappedObjects_r(RepState, GuidReferences.Array, OriginalObject, PackageMap, ShadowArrayData, ArrayData, NewMaxOffset, bCalledPreNetReceive, bOutSomeObjectsWereMapped, bOutHasMoreUnmapped);
			}
			else
			{
				FScriptArray* Array = (FScriptArray*)(Data + AbsOffset).Data;
				FRepObjectDataBuffer ArrayData(Array->GetData());
				const int32 NewMaxOffset = Array->Num() * Cmd.ElementSize;

				UpdateUnmappedObjects_r(RepState, GuidReferences.Array, OriginalObject, PackageMap, nullptr, ArrayData, NewMaxOffset, bCalledPreNetReceive, bOutSomeObjectsWereMapped, bOutHasMoreUnmapped);
			}
			continue;
		}

		bool bMappedSomeGUIDs = false;

		for (auto UnmappedIt = GuidReferences.UnmappedGUIDs.CreateIterator(); UnmappedIt; ++UnmappedIt)
		{
			const FNetworkGUID& GUID = *UnmappedIt;

			if (PackageMap->IsGUIDBroken(GUID, false))
			{
				UE_LOG(LogRep, Warning, TEXT("UpdateUnmappedObjects_r: Broken GUID. NetGuid: %s"), *GUID.ToString());
				UnmappedIt.RemoveCurrent();
				continue;
			}

			UObject* Object = PackageMap->GetObjectFromNetGUID(GUID, false);

			if (Object != nullptr)
			{
				UE_LOG(LogRep, VeryVerbose, TEXT("UpdateUnmappedObjects_r: REMOVED unmapped property: Offset: %i, Guid: %s, PropName: %s, ObjName: %s"), AbsOffset, *GUID.ToString(), *Cmd.Property->GetName(), *Object->GetName());

				if (GUID.IsDynamic())
				{
					// If this guid is dynamic, move it to the dynamic guids list
					GuidReferences.MappedDynamicGUIDs.Add(GUID);
				}

				// Remove from unmapped guids list
				UnmappedIt.RemoveCurrent();
				bMappedSomeGUIDs = true;
			}
		}

		// If we resolved some guids, re-deserialize the data which will hook up the object pointer with the property
		if (bMappedSomeGUIDs)
		{
			if (!bCalledPreNetReceive)
			{
				// Call PreNetReceive if we are going to change a value (some game code will need to think this is an actual replicated value)
				OriginalObject->PreNetReceive();
				bCalledPreNetReceive = true;
			}

			bOutSomeObjectsWereMapped = true;
			const bool bUpdateShadowState = (ShadowData && INDEX_NONE != Parent.RepNotifyNumParams);

			// Copy current value over so we can check to see if it changed
			if (bUpdateShadowState)
			{
				StoreProperty(Cmd, ShadowData + ShadowOffset, Data + AbsOffset);
			}

			// Initialize the reader with the stored buffer that we need to read from
			FNetBitReader Reader(PackageMap, GuidReferences.Buffer.GetData(), GuidReferences.NumBufferBits);

			// Read the property
			Cmd.Property->NetSerializeItem(Reader, PackageMap, Data + AbsOffset);

			// Check to see if this property changed
			if (bUpdateShadowState)
			{
				// I have a sneaking suspicion that this is broken.
				// AbsOffset could be Cmd.Offset, but we also may be recursing into an Array, and that
				// would mean it could be Cmd.Offset + (ArrayIndex * ElementOffset)
				// That could cause us to trigger RepNotifies more often for Dynamic Array properties.
				// That goes for the above too.

				if (Parent.RepNotifyCondition == REPNOTIFY_Always || !PropertiesAreIdentical(Cmd, ShadowData + ShadowOffset, Data + AbsOffset))
				{
					// If this properties needs an OnRep, queue that up to be handled later
					RepState->RepNotifies.AddUnique(Parent.Property);
				}
				else
				{
					UE_CLOG(LogSkippedRepNotifies, LogRep, Display, TEXT("UpdateUnmappedObjects_r: Skipping RepNotify because Property did not change. %s"), *Cmd.Property->GetName());
				}
			}
		}

		// If we still have more unmapped guids, we need to keep processing this entry
		if (GuidReferences.UnmappedGUIDs.Num() > 0)
		{
			bOutHasMoreUnmapped = true;
		}
		else if (GuidReferences.UnmappedGUIDs.Num() == 0 && GuidReferences.MappedDynamicGUIDs.Num() == 0)
		{
			It.RemoveCurrent();
		}
	}
}

void FRepLayout::UpdateUnmappedObjects(
	FReceivingRepState* RESTRICT RepState,
	UPackageMap* PackageMap,
	UObject* OriginalObject,
	FNetDeltaSerializeInfo& Params,
	bool& bCalledPreNetReceive,
	bool& bOutSomeObjectsWereMapped,
	bool& bOutHasMoreUnmapped) const
{
	bOutSomeObjectsWereMapped = false;
	bOutHasMoreUnmapped = false;
	bCalledPreNetReceive = false;

	if (!IsEmpty())
	{
		UpdateUnmappedObjects_r(
			RepState,
			&RepState->GuidReferencesMap,
			OriginalObject,
			PackageMap,
			(uint8*)RepState->StaticBuffer.GetData(),
			(uint8*)OriginalObject,
			Owner->GetPropertiesSize(),
			bCalledPreNetReceive,
			bOutSomeObjectsWereMapped,
			bOutHasMoreUnmapped);

		Params.bCalledPreNetReceive = bCalledPreNetReceive;

		// Custom Delta Properties
		if (LifetimeCustomPropertyState)
		{
			FRepObjectDataBuffer ObjectData(Params.Object);
			const int32 NumLifetimeCustomDeltaProperties = LifetimeCustomPropertyState->GetNumCustomDeltaProperties();

			for (int32 CustomDeltaIndex = 0; CustomDeltaIndex < NumLifetimeCustomDeltaProperties; ++CustomDeltaIndex)
			{
				const FLifetimeCustomDeltaProperty& CustomDeltaProperty = LifetimeCustomPropertyState->GetCustomDeltaProperty(CustomDeltaIndex);
				const FRepParentCmd& Parent = Parents[CustomDeltaProperty.PropertyRepIndex];

				// Static cast is safe here, because this property wouldn't have been marked CustomDelta otherwise.
				UStructProperty* StructProperty = static_cast<UStructProperty*>(Parent.Property);
				UScriptStruct::ICppStructOps* CppStructOps = StructProperty->Struct->GetCppStructOps();

				FNetDeltaSerializeInfo TempParams = Params;

				TempParams.DebugName = Parent.CachedPropertyName.ToString();
				TempParams.Struct = StructProperty->Struct;
				TempParams.bOutSomeObjectsWereMapped = false;
				TempParams.bOutHasMoreUnmapped = false;
				TempParams.CustomDeltaIndex = CustomDeltaIndex;
				TempParams.Data = ObjectData + Parent;

				// Call the custom delta serialize function to handle it
				CppStructOps->NetDeltaSerialize(TempParams, TempParams.Data);

				if (TempParams.bOutSomeObjectsWereMapped && INDEX_NONE != Parent.RepNotifyNumParams)
				{
					UE4_RepLayout_Private::QueueRepNotifyForCustomDeltaProperty(RepState, Params, StructProperty, Parent.ArrayIndex);
				}

				Params.bOutSomeObjectsWereMapped |= TempParams.bOutSomeObjectsWereMapped;
				Params.bOutHasMoreUnmapped |= TempParams.bOutHasMoreUnmapped;
				Params.bCalledPreNetReceive |= TempParams.bCalledPreNetReceive;
			}
		}
	}
}

bool FRepLayout::SendCustomDeltaProperty(FNetDeltaSerializeInfo& Params, uint16 CustomDeltaIndex) const
{
	const FLifetimeCustomDeltaProperty& CustomDeltaProperty = LifetimeCustomPropertyState->GetCustomDeltaProperty(CustomDeltaIndex);
	const FRepParentCmd& Parent = Parents[CustomDeltaProperty.PropertyRepIndex];

	if (!ensure(EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsCustomDelta)))
	{
		return false;
	}

	UStructProperty* StructProperty = static_cast<UStructProperty*>(Parent.Property);
	UScriptStruct::ICppStructOps * CppStructOps = StructProperty->Struct->GetCppStructOps();

	check(CppStructOps); // else should not have STRUCT_NetSerializeNative

	Params.DebugName = Parent.CachedPropertyName.ToString();
	Params.Struct = StructProperty->Struct;
	Params.CustomDeltaIndex = CustomDeltaIndex;
	Params.Data = FRepObjectDataBuffer(Params.Object) + Parent;

	bool bSupportsFastArrayDelta = Params.bSupportsFastArrayDeltaStructSerialization;

	if (Params.bSupportsFastArrayDeltaStructSerialization &&
		EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsFastArray) &&
		!!LifetimeCustomPropertyState->GetNumFastArrayProperties())
	{
		bSupportsFastArrayDelta = CustomDeltaProperty.FastArrayNumber != INDEX_NONE;
	}

	Params.Writer->WriteBit(bSupportsFastArrayDelta);

	if (Parent.Property->ArrayDim != 1)
	{
		uint32 StaticArrayIndex = Parent.ArrayIndex;
		Params.Writer->SerializeIntPacked(StaticArrayIndex);
	}

	TGuardValue<bool> SupportFastArrayDeltaGuard(Params.bSupportsFastArrayDeltaStructSerialization, bSupportsFastArrayDelta);
	return CppStructOps->NetDeltaSerialize(Params, Params.Data);
}

bool FRepLayout::ReceiveCustomDeltaProperty(
	FReceivingRepState* RESTRICT ReceivingRepState,
	FNetDeltaSerializeInfo& Params,
	UStructProperty* Property) const
{
	if (Params.Connection->EngineNetworkProtocolVersion >= EEngineNetworkVersionHistory::HISTORY_FAST_ARRAY_DELTA_STRUCT)
	{
		Params.bSupportsFastArrayDeltaStructSerialization = !!Params.Reader->ReadBit();
	}
	else
	{
		Params.bSupportsFastArrayDeltaStructSerialization = false;
	}

	uint32 StaticArrayIndex = 0;

	// Receive array index (static sized array, i.e. MemberVariable[4])
	if (Property->ArrayDim != 1)
	{
		check(Property->ArrayDim >= 2);

		Params.Reader->SerializeIntPacked(StaticArrayIndex);

		if (StaticArrayIndex >= (uint32)Property->ArrayDim)
		{
			UE_LOG(LogRep, Error, TEXT("Element index too large %s in %s"), *Property->GetName(), *Params.Object->GetFullName());
			return false;
		}
	}

	const FRepParentCmd& Parent = Parents[Property->RepIndex + StaticArrayIndex];

	// We should only be receiving custom delta properties (since RepLayout handles the rest)
	if (!EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsCustomDelta))
	{
		UE_LOG(LogNet, Error, TEXT("Client received non custom delta property value %s in %s"), *Parent.CachedPropertyName.ToString(), *Params.Object->GetFullName());
		return false;
	}


	UScriptStruct* InnerStruct = Property->Struct;
	UScriptStruct::ICppStructOps* CppStructOps = InnerStruct->GetCppStructOps();

	check(CppStructOps);

	Params.DebugName = Parent.CachedPropertyName.ToString();
	Params.Struct = InnerStruct;
	Params.CustomDeltaIndex = LifetimeCustomPropertyState->GetCustomDeltaIndexFromPropertyRepIndex(Property->RepIndex + StaticArrayIndex);
	Params.Data = FRepObjectDataBuffer(Params.Object) + Parent;

	if (CppStructOps->NetDeltaSerialize(Params, Params.Data))
	{
		if (UNLIKELY(Params.Reader->IsError()))
		{
			UE_LOG(LogNet, Error, TEXT("FRepLayout::ReceiveCustomDeltaProperty: NetDeltaSerialize - Reader.IsError() == true. Property: %s, Object: %s"), *Params.DebugName, *Params.Object->GetFullName());
			return false;
		}
		if (UNLIKELY(Params.Reader->GetBitsLeft() != 0))
		{
			UE_LOG(LogNet, Error, TEXT("FRepLayout::ReceiveCustomDeltaProperty: NetDeltaSerialize - Mismatch read. Property: %s, Object: %s"), *Params.DebugName, *Params.Object->GetFullName());
			return false;
		}

		// Successfully received it.
		if (INDEX_NONE != Parent.RepNotifyNumParams)
		{
			UE4_RepLayout_Private::QueueRepNotifyForCustomDeltaProperty(ReceivingRepState, Params, Property, StaticArrayIndex);
		}

		return true;
	}

	return false;
}

void FRepLayout::CallRepNotifies(FReceivingRepState* RepState, UObject* Object) const
{
	if (RepState->RepNotifies.Num() == 0)
	{
		return;
	}
	
	if (IsEmpty())
	{
		UE_LOG(LogRep, Error, TEXT("FRepLayout::CallRepNotifies: Empty layout with RepNotifies: %s"), *GetPathNameSafe(Owner));
		return;
	}

	FRepShadowDataBuffer ShadowData(RepState->StaticBuffer.GetData());
	FRepObjectDataBuffer ObjectData(Object);

	for (UProperty* RepProperty : RepState->RepNotifies)
	{
		if (!Parents.IsValidIndex(RepProperty->RepIndex))
		{
			UE_LOG(LogRep, Warning, TEXT("FRepLayout::CallRepNotifies: Called with invalid property %s on object %s."),
				*RepProperty->GetName(), *Object->GetName());
				continue;
		}

		UFunction* RepNotifyFunc = Object->FindFunction(RepProperty->RepNotifyFunc);

		if (RepNotifyFunc == nullptr)
		{
			UE_LOG(LogRep, Warning, TEXT("FRepLayout::CallRepNotifies: Can't find RepNotify function %s for property %s on object %s."),
				*RepProperty->RepNotifyFunc.ToString(), *RepProperty->GetName(), *Object->GetName());
			continue;
		}

		const FRepParentCmd& Parent = Parents[RepProperty->RepIndex];
		const int32 NumParms = RepNotifyFunc->NumParms;

		switch (NumParms)
		{
			case 0:
			{
				Object->ProcessEvent(RepNotifyFunc, nullptr);
				break;
			}
			case 1:
			{
				FRepShadowDataBuffer PropertyData = ShadowData + Parent;

				if (EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsCustomDelta))
				{
					Object->ProcessEvent(RepNotifyFunc, PropertyData);
				}
				else
				{
					// This could be cached off as a Parent flag, to avoid touching the Commands array.
					if (ERepLayoutCmdType::PropertyBool == Cmds[Parent.CmdStart].Type)
					{
						bool BoolPropertyValue = !!static_cast<const UBoolProperty*>(Parent.Property)->GetPropertyValue(PropertyData);
						Object->ProcessEvent(RepNotifyFunc, &BoolPropertyValue);
					}
					else
					{
						Object->ProcessEvent(RepNotifyFunc, PropertyData);
					}

					// now store the complete value in the shadow buffer
					if (!EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsNetSerialize))
					{
						RepProperty->CopyCompleteValue(ShadowData + Parent, ObjectData + Parent);
					}
				}
				break;
			}
			case 2:
			{
				check(EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsCustomDelta));

				// Fixme: this isn't as safe as it could be. Right now we have two types of parameters: MetaData (a TArray<uint8>)
				// and the last local value (pointer into the Recent[] array).
				//
				// Arrays always expect MetaData. Everything else, including structs, expect last value.
				// This is enforced with UHT only. If a ::NetSerialize function ever starts producing a MetaData array thats not in UArrayProperty,
				// we have no static way of catching this and the replication system could pass the wrong thing into ProcessEvent here.
				//
				// But this is all sort of an edge case feature anyways, so its not worth tearing things up too much over.

				FMemMark Mark(FMemStack::Get());
				uint8* Parms = new(FMemStack::Get(), MEM_Zeroed, RepNotifyFunc->ParmsSize)uint8;

				TFieldIterator<UProperty> Itr(RepNotifyFunc);
				check(Itr);

				FRepShadowDataBuffer PropertyData = ShadowData + Parent;

				Itr->CopyCompleteValue(Itr->ContainerPtrToValuePtr<void>(Parms), PropertyData);
				++Itr;
				check(Itr);

				TArray<uint8> *NotifyMetaData = RepState->RepNotifyMetaData.Find(RepProperty);
				check(NotifyMetaData);
				Itr->CopyCompleteValue(Itr->ContainerPtrToValuePtr<void>(Parms), NotifyMetaData);

				Object->ProcessEvent(RepNotifyFunc, Parms);

				Mark.Pop();
				break;
			}
			default:
			{
				checkf(false, TEXT("FRepLayout::CallRepNotifies: Invalid number of parameters for property %s on object %s. NumParms=%d, CustomDelta=%d"),
					*RepProperty->GetName(), *Object->GetName(), NumParms, !!EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsCustomDelta));
				break;
			}
		}
	}

	RepState->RepNotifies.Empty();
	RepState->RepNotifyMetaData.Empty();
}

template<ERepDataBufferType DataType>
static void ValidateWithChecksum_r(
	TArray<FRepLayoutCmd>::TConstIterator& CmdIt,
	const TConstRepDataBuffer<DataType> Data,
	FBitArchive& Ar);

template<ERepDataBufferType DataType>
static void ValidateWithChecksum_DynamicArray_r(
	TArray<FRepLayoutCmd>::TConstIterator& CmdIt,
	TConstRepDataBuffer<DataType> Data,
	FBitArchive& Ar)
{
	const FRepLayoutCmd& Cmd = *CmdIt;

	// -2 because the current index will be the Owner Array Properties Cmd Index (+1)
	// and EndCmd will be the Cmd Index just *after* the Return Command (+1) 
	const int32 ArraySubCommands = CmdIt.GetIndex() - Cmd.EndCmd - 2;

	FScriptArray* Array = (FScriptArray*)Data.Data;

	uint16 ArrayNum = Array->Num();
	uint16 ElementSize = Cmd.ElementSize;

	Ar << ArrayNum;
	Ar << ElementSize;

	if (ArrayNum != Array->Num())
	{
		UE_LOG(LogRep, Fatal, TEXT("ValidateWithChecksum_AnyArray_r: Array sizes different! %s %i / %i"), *Cmd.Property->GetFullName(), ArrayNum, Array->Num());
	}

	if (ElementSize != Cmd.ElementSize)
	{
		UE_LOG(LogRep, Fatal, TEXT("ValidateWithChecksum_AnyArray_r: Array element sizes different! %s %i / %i"), *Cmd.Property->GetFullName(), ElementSize, Cmd.ElementSize);
	}

	const TConstRepDataBuffer<DataType> ArrayData(Array->GetData());
	for (int32 i = 0; i < ArrayNum - 1; i++)
	{
		const int32 ArrayElementsOffset = i * ElementSize;
		ValidateWithChecksum_r<>(CmdIt, Data + ArrayElementsOffset, Ar);
		CmdIt -= ArraySubCommands;
	}

	const int32 ArrayElementOffset = (ArrayNum - 1) * ElementSize;
	ValidateWithChecksum_r<>(CmdIt, ArrayData + ArrayElementOffset, Ar);
}

template<ERepDataBufferType DataType>
void ValidateWithChecksum_r(
	TArray<FRepLayoutCmd>::TConstIterator& CmdIt,
	const TConstRepDataBuffer<DataType> Data, 
	FBitArchive& Ar)
{
	for (; CmdIt->Type != ERepLayoutCmdType::Return; ++CmdIt)
	{
		const FRepLayoutCmd& Cmd = *CmdIt;
		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			ValidateWithChecksum_DynamicArray_r<>(CmdIt, Data + Cmd, Ar);
		}
		else
		{
			SerializeReadWritePropertyChecksum<>(Cmd, CmdIt.GetIndex() - 1, Data + Cmd, Ar);
		}
	}
}

template<ERepDataBufferType DataType>
void FRepLayout::ValidateWithChecksum(TConstRepDataBuffer<DataType> Data, FBitArchive& Ar) const
{
	TArray<FRepLayoutCmd>::TConstIterator CmdIt = Cmds.CreateConstIterator();
	ValidateWithChecksum_r<>(CmdIt, Data, Ar);
	check(CmdIt.GetIndex() == Cmds.Num());
}

uint32 FRepLayout::GenerateChecksum(const FRepState* RepState) const
{
	FBitWriter Writer(1024, true);
	ValidateWithChecksum<>(FConstRepShadowDataBuffer(RepState->GetReceivingRepState()->StaticBuffer.GetData()), Writer);

	return FCrc::MemCrc32(Writer.GetData(), Writer.GetNumBytes(), 0);
}

void FRepLayout::PruneChangeList(
	const FConstRepObjectDataBuffer Data,
	const TArray<uint16>& Changed,
	TArray<uint16>& PrunedChanged) const
{
	check(Changed.Num() > 0);

	PrunedChanged.Empty(1);

	if (!IsEmpty())
	{
		FChangelistIterator ChangelistIterator(Changed, 0);
		FRepHandleIterator HandleIterator(Owner, ChangelistIterator, Cmds, BaseHandleToCmdIndex, 0, 1, 0, Cmds.Num() - 1);
		PruneChangeList_r(HandleIterator, Data, PrunedChanged);
	}

	PrunedChanged.Add(0);
}

void FRepLayout::MergeChangeList(
	const FConstRepObjectDataBuffer Data,
	const TArray<uint16>& Dirty1,
	const TArray<uint16>& Dirty2,
	TArray<uint16>& MergedDirty) const
{
	check(Dirty1.Num() > 0);
	MergedDirty.Empty(1);

	if (!IsEmpty())
	{
		if (Dirty2.Num() == 0)
		{
			FChangelistIterator ChangelistIterator(Dirty1, 0);
			FRepHandleIterator HandleIterator(Owner, ChangelistIterator, Cmds, BaseHandleToCmdIndex, 0, 1, 0, Cmds.Num() - 1);
			PruneChangeList_r(HandleIterator, Data, MergedDirty);
		}
		else
		{
			FChangelistIterator ChangelistIterator1(Dirty1, 0);
			FRepHandleIterator HandleIterator1(Owner, ChangelistIterator1, Cmds, BaseHandleToCmdIndex, 0, 1, 0, Cmds.Num() - 1);

			FChangelistIterator ChangelistIterator2(Dirty2, 0);
			FRepHandleIterator HandleIterator2(Owner, ChangelistIterator2, Cmds, BaseHandleToCmdIndex, 0, 1, 0, Cmds.Num() - 1);

			MergeChangeList_r(HandleIterator1, HandleIterator2, Data, MergedDirty);
		}
	}

	MergedDirty.Add(0);
}

void FRepLayout::SanityCheckChangeList_DynamicArray_r(
	const int32 CmdIndex, 
	const FConstRepObjectDataBuffer Data, 
	TArray<uint16>& Changed,
	int32& ChangedIndex) const
{
	const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

	FScriptArray * Array = (FScriptArray *)Data.Data;

	// Read the jump offset
	// We won't need to actually jump over anything because we expect the change list to be pruned once we get here
	// But we can use it to verify we read the correct amount.
	const int32 ArrayChangedCount = Changed[ChangedIndex++];

	const int32 OldChangedIndex = ChangedIndex;

	const FConstRepObjectDataBuffer ArrayData = (uint8*)Array->GetData();

	uint16 LocalHandle = 0;

	for (int32 i = 0; i < Array->Num(); i++)
	{
		const int32 ArrayElementOffset = i * Cmd.ElementSize;
		LocalHandle = SanityCheckChangeList_r(CmdIndex + 1, Cmd.EndCmd - 1, ArrayData + ArrayElementOffset, Changed, ChangedIndex, LocalHandle);
	}

	check(ChangedIndex - OldChangedIndex == ArrayChangedCount);	// Make sure we read correct amount
	check(Changed[ChangedIndex] == 0);							// Make sure we are at the end

	ChangedIndex++;
}

uint16 FRepLayout::SanityCheckChangeList_r(
	const int32 CmdStart, 
	const int32 CmdEnd, 
	const FConstRepObjectDataBuffer Data, 
	TArray<uint16>& Changed,
	int32& ChangedIndex,
	uint16 Handle 
	) const
{
	for (int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++)
	{
		const FRepLayoutCmd& Cmd = Cmds[ CmdIndex ];

		check(Cmd.Type != ERepLayoutCmdType::Return);

		Handle++;

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			if (Handle == Changed[ChangedIndex])
			{
				const int32 LastChangedArrayHandle = Changed[ChangedIndex];
				ChangedIndex++;
				SanityCheckChangeList_DynamicArray_r(CmdIndex, Data + Cmd, Changed, ChangedIndex);
				check(Changed[ChangedIndex] == 0 || Changed[ChangedIndex] > LastChangedArrayHandle);
			}
			CmdIndex = Cmd.EndCmd - 1;	// Jump past children of this array (the -1 because of the ++ in the for loop)
			continue;
		}

		if (Handle == Changed[ChangedIndex])
		{
			const int32 LastChangedArrayHandle = Changed[ChangedIndex];
			ChangedIndex++;
			check(Changed[ChangedIndex] == 0 || Changed[ChangedIndex] > LastChangedArrayHandle);
		}
	}

	return Handle;
}

void FRepLayout::SanityCheckChangeList(const FConstRepObjectDataBuffer Data, TArray<uint16> & Changed) const
{
	int32 ChangedIndex = 0;
	SanityCheckChangeList_r(0, Cmds.Num() - 1, Data, Changed, ChangedIndex, 0);
	check(Changed[ChangedIndex] == 0);
}

struct FDiffPropertiesSharedParams
{
	const ERepParentFlags PropertyFlags;
	const EDiffPropertiesFlags DiffFlags;
	TArray<UProperty*>* RepNotifies;
	const TArray<FRepParentCmd>& Parents;
	const TArray<FRepLayoutCmd>& Cmds;
};

template<ERepDataBufferType DestinationType, ERepDataBufferType SourceType>
struct TDiffPropertiesStackParams
{
	TRepDataBuffer<DestinationType> Destination;
	TConstRepDataBuffer<SourceType> Source;
	const uint16 StartCmd;
	const uint16 EndCmd;
};

template<ERepDataBufferType DestinationType, ERepDataBufferType SourceType>
static bool DiffProperties_r(FDiffPropertiesSharedParams& Params, TDiffPropertiesStackParams<DestinationType, SourceType>& StackParams)
{
	// Note, it's never possible for the Source or Destination data to be null.
	// At the top level, both will always be valid (pointing to the memory of each element in each respective buffer).
	// As we recurse, if we detect the size of the Arrays is different we'll either:
	//	1. Bail out if we're not syncing properties.
	//	2. Resize the Destination array to match the Source Array, guaranteeing both are allocated.

	check(StackParams.Source);
	check(StackParams.Destination);

	const bool bSyncProperties = EnumHasAnyFlags(Params.DiffFlags, EDiffPropertiesFlags::Sync);
	bool bDifferent = false;

	for (uint16 CmdIndex = StackParams.StartCmd; CmdIndex < StackParams.EndCmd; ++CmdIndex)
	{
		const FRepLayoutCmd& Cmd = Params.Cmds[CmdIndex];
		const FRepParentCmd& Parent = Params.Parents[Cmd.ParentIndex];

		if (EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsCustomDelta))
		{
			CmdIndex = Parent.CmdEnd - 1;
			continue;
		}

		check(ERepLayoutCmdType::Return != Cmd.Type);

		if (ERepLayoutCmdType::DynamicArray == Cmd.Type)
		{
			// This will ensure that we've skipped passed the array's properties.
			const uint16 ArrayStartCmd = CmdIndex + 1;
			const uint16 ArrayEndCmd = Cmd.EndCmd - 1;
			CmdIndex = ArrayEndCmd;

			FScriptArray* SourceArray = (FScriptArray*)(StackParams.Source + Cmd).Data;
			FScriptArray* DestinationArray = (FScriptArray*)(StackParams.Destination + Cmd).Data;

			if (SourceArray->Num() != DestinationArray->Num())
			{
				bDifferent = true;
				if (!bSyncProperties)
				{
					UE_LOG(LogRep, Warning, TEXT("FDiffPropertiesImpl: Array sizes different: %s %i / %i"), *Cmd.Property->GetFullName(), SourceArray->Num(), DestinationArray->Num());
					continue;
				}
				else if (EnumHasAnyFlags(Parent.Flags, Params.PropertyFlags))
				{
					continue;
				}

				// Make the destination state match the source state
				FScriptArrayHelper DestinationArrayHelper((UArrayProperty *)Cmd.Property, DestinationArray);
				DestinationArrayHelper.Resize(SourceArray->Num());
			}

			decltype(StackParams.Destination) ArrayDestinationData(DestinationArray->GetData());
			decltype(StackParams.Source) ArraySourceData(SourceArray->GetData());

			typename TDecay<decltype(StackParams)>::Type ArrayStackParams{
				ArrayDestinationData,
				ArraySourceData,
				ArrayStartCmd,
				ArrayEndCmd
			};

			for (int32 i = 0; i < SourceArray->Num(); ++i)
			{
				const int32 ElementOffset = i * Cmd.ElementSize;

				ArrayStackParams.Source = ArraySourceData + ElementOffset;
				ArrayStackParams.Destination = ArrayDestinationData + ElementOffset;
				bDifferent |= DiffProperties_r(Params, ArrayStackParams);
			}
		}
		else
		{
			// Make the shadow state match the actual state at the time of send
			const bool bPropertyHasRepNotifies = Params.RepNotifies && INDEX_NONE != Parent.RepNotifyNumParams;
			if ((bPropertyHasRepNotifies && Parent.RepNotifyCondition == REPNOTIFY_Always) || !PropertiesAreIdentical(Cmd, StackParams.Source + Cmd, StackParams.Destination + Cmd))
			{
				bDifferent = true;
				if (!bSyncProperties)
				{
					UE_LOG(LogRep, Warning, TEXT("DiffProperties_r: Property different: %s"), *Cmd.Property->GetFullName());
					continue;
				}
				else if (!EnumHasAnyFlags(Parent.Flags, Params.PropertyFlags))
				{
					continue;
				}

				StoreProperty(Cmd, StackParams.Destination + Cmd, StackParams.Source + Cmd);

				if (bPropertyHasRepNotifies)
				{
					Params.RepNotifies->AddUnique(Parent.Property);
				}
			}
			else
			{
				UE_CLOG(LogSkippedRepNotifies > 0, LogRep, Display, TEXT("FDiffPropertiesImpl: Skipping RepNotify because values are the same: %s"), *Cmd.Property->GetFullName());
			}
		}
	}

	return bDifferent;
}

template<ERepDataBufferType DestinationType, ERepDataBufferType SourceType>
bool FRepLayout::DiffProperties(
	TArray<UProperty*>* RepNotifies,
	TRepDataBuffer<DestinationType> Destination,
	TConstRepDataBuffer<SourceType> Source,
	const EDiffPropertiesFlags DiffFlags) const
{

	// Currently, only lifetime properties init from their defaults, so default to that,
	// but also diff conditional properties if requested.
	ERepParentFlags ParentPropertyFlags = ERepParentFlags::IsLifetime;
	if (EnumHasAnyFlags(DiffFlags, EDiffPropertiesFlags::IncludeConditionalProperties))
	{
		ParentPropertyFlags |= ERepParentFlags::IsConditional;
	}

	FDiffPropertiesSharedParams Params{
		ParentPropertyFlags,
		DiffFlags,
		RepNotifies,
		Parents,
		Cmds
	};

	TDiffPropertiesStackParams<DestinationType, SourceType> StackParams{
		Destination,
		Source,
		0u,
		static_cast<uint16>(Cmds.Num() - 1)
	};

	return DiffProperties_r(Params, StackParams);
}

struct FDiffStablePropertiesSharedParams
{
	TArray<UProperty*>* RepNotifies;
	TArray<UObject*>* ObjReferences;
	const TArray<FRepParentCmd>& Parents;
	const TArray<FRepLayoutCmd>& Cmds;
};

template<ERepDataBufferType DestinationType, ERepDataBufferType SourceType>
struct TDiffStablePropertiesStackParams
{
	TRepDataBuffer<DestinationType> Destination;
	TConstRepDataBuffer<SourceType> Source;
	const int32 StartCmd;
	const int32 EndCmd;
};

template<ERepDataBufferType DestinationType, ERepDataBufferType SourceType>
static bool DiffStableProperties_r(FDiffStablePropertiesSharedParams& Params, TDiffStablePropertiesStackParams<DestinationType, SourceType>& StackParams)
{
	// Note, it's never possible for the Source or Destination data to be null.
	// At the top level, both will always be valid (pointing to the memory of each element in each respective buffer).
	// As we recurse, if we detect the size of the Arrays is different we'll either:
	//	1. Bail out if we're not syncing properties.
	//	2. Resize the Destination array to match the Source Array, guaranteeing both are allocated.

	bool bDifferent = false;

	for (uint16 CmdIndex = StackParams.StartCmd; CmdIndex < StackParams.EndCmd; ++CmdIndex)
	{
		const FRepLayoutCmd& Cmd = Params.Cmds[CmdIndex];
		const FRepParentCmd& Parent = Params.Parents[Cmd.ParentIndex];

		if (EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsCustomDelta))
		{
			CmdIndex = Parent.CmdEnd - 1;
			continue;
		}

		check(ERepLayoutCmdType::Return != Cmd.Type);

		if (ERepLayoutCmdType::DynamicArray == Cmd.Type)
		{
			// This will ensure that we've skipped passed the array's properties.
			const uint16 ArrayStartCmd = CmdIndex + 1;
			const uint16 ArrayEndCmd = Cmd.EndCmd - 1;
			CmdIndex = ArrayEndCmd;

			FScriptArray* SourceArray = (FScriptArray*)(StackParams.Source + Cmd).Data;
			FScriptArray* DestinationArray = (FScriptArray*)(StackParams.Destination + Cmd).Data;

			if (SourceArray->Num() != DestinationArray->Num())
			{
				bDifferent = true;

				if (!EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsLifetime))
				{
					// Currently, only lifetime properties init from their defaults
					continue;
				}

				// Do not adjust source data, only the destination
				FScriptArrayHelper DestinationArrayHelper((UArrayProperty *)Cmd.Property, DestinationArray);
				DestinationArrayHelper.Resize(SourceArray->Num());
			}

			decltype(StackParams.Destination) ArrayDestinationData(DestinationArray->GetData());
			decltype(StackParams.Source) ArraySourceData(SourceArray->GetData());

			typename TDecay<decltype(StackParams)>::Type ArrayStackParams{
				ArrayDestinationData,
				ArraySourceData,
				ArrayStartCmd,
				ArrayEndCmd
			};

			for (int32 i = 0; i < SourceArray->Num(); ++i)
			{
				const int32 ElementOffset = i * Cmd.ElementSize;

				ArrayStackParams.Source = ArraySourceData + ElementOffset;
				ArrayStackParams.Destination = ArrayDestinationData + ElementOffset;
				bDifferent |= DiffStableProperties_r(Params, ArrayStackParams);
			}
		}
		else
		{
			if (!PropertiesAreIdentical(Cmd, StackParams.Destination + Cmd, StackParams.Source + Cmd))
			{
				bDifferent = true;

				if (!EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsLifetime))
				{
					// Currently, only lifetime properties init from their defaults
					continue;
				}
				else if (Cmd.Property->HasAnyPropertyFlags(CPF_Transient))
				{
					// skip transient properties
					continue;
				}

				if (Cmd.Type == ERepLayoutCmdType::PropertyObject ||
					Cmd.Type == ERepLayoutCmdType::PropertyWeakObject ||
					Cmd.Type == ERepLayoutCmdType::PropertySoftObject)
				{
					if (UObjectPropertyBase* ObjProperty = CastChecked<UObjectPropertyBase>(Cmd.Property))
					{
						if (ObjProperty->PropertyClass && (ObjProperty->PropertyClass->IsChildOf(AActor::StaticClass()) || ObjProperty->PropertyClass->IsChildOf(UActorComponent::StaticClass())))
						{
							// skip actor and component references
							continue;
						}

						if (UObject* ObjValue = ObjProperty->GetObjectPropertyValue(StackParams.Source + Cmd))
						{
							const bool bStableForNetworking = (ObjValue->HasAnyFlags(RF_WasLoaded | RF_DefaultSubObject) || ObjValue->IsNative() || ObjValue->IsDefaultSubobject());
							if (!bStableForNetworking)
							{
								// skip object references without a stable name
								continue;
							}

							if (Params.ObjReferences)
							{
								Params.ObjReferences->AddUnique(ObjValue);
							}
						}
					}
				}

				StoreProperty(Cmd, StackParams.Destination + Cmd, StackParams.Source + Cmd);

				if (Params.RepNotifies && INDEX_NONE != Parent.RepNotifyNumParams)
				{
					Params.RepNotifies->AddUnique(Parent.Property);
				}
			}
		}
	}

	return bDifferent;
}

template<ERepDataBufferType DestinationType, ERepDataBufferType SourceType>
bool FRepLayout::DiffStableProperties(
	TArray<UProperty*>* RepNotifies,
	TArray<UObject*>* ObjReferences,
	TRepDataBuffer<DestinationType> Destination,
	TConstRepDataBuffer<SourceType> Source) const
{
	FDiffStablePropertiesSharedParams Params{
		RepNotifies,
		ObjReferences,
		Parents,
		Cmds
	};

	TDiffStablePropertiesStackParams<DestinationType, SourceType> StackParams{
		Destination,
		Source,
		0,
		Cmds.Num() - 1
	};

	return DiffStableProperties_r(Params, StackParams);
}

static FName NAME_Vector_NetQuantize100(TEXT("Vector_NetQuantize100"));
static FName NAME_Vector_NetQuantize10(TEXT("Vector_NetQuantize10"));
static FName NAME_Vector_NetQuantizeNormal(TEXT("Vector_NetQuantizeNormal"));
static FName NAME_Vector_NetQuantize(TEXT("Vector_NetQuantize"));
static FName NAME_UniqueNetIdRepl(TEXT("UniqueNetIdRepl"));
static FName NAME_RepMovement(TEXT("RepMovement"));

static uint32 AddPropertyCmd(
	TArray<FRepLayoutCmd>& Cmds,
	UProperty* Property,
	int32 Offset,
	int32 RelativeHandle,
	int32 ParentIndex,
	uint32 ParentChecksum,
	int32 StaticArrayIndex,
	const UNetConnection* ServerConnection)
{
	SCOPE_CYCLE_COUNTER(STAT_RepLayout_AddPropertyCmd);

	FRepLayoutCmd & Cmd = Cmds.AddZeroed_GetRef();

	Cmd.Property = Property;
	Cmd.Type = ERepLayoutCmdType::Property;		// Initially set to generic type
	Cmd.Offset = Offset;
	Cmd.ElementSize = Property->ElementSize;
	Cmd.RelativeHandle = RelativeHandle;
	Cmd.ParentIndex = ParentIndex;
	Cmd.CompatibleChecksum = GetRepLayoutCmdCompatibleChecksum(Property, ServerConnection, StaticArrayIndex, ParentChecksum);

	UProperty* UnderlyingProperty = Property;
	if (UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property))
	{
		UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
	}

	// Try to special case to custom types we know about
	if (UnderlyingProperty->IsA(UStructProperty::StaticClass()))
	{
		UStructProperty* StructProp = Cast<UStructProperty>(UnderlyingProperty);
		UScriptStruct* Struct = StructProp->Struct;
		Cmd.Flags |= ERepLayoutCmdFlags::IsStruct;

		if (Struct->GetFName() == NAME_Vector)
		{
			Cmd.Type = ERepLayoutCmdType::PropertyVector;
		}
		else if (Struct->GetFName() == NAME_Rotator)
		{
			Cmd.Type = ERepLayoutCmdType::PropertyRotator;
		}
		else if (Struct->GetFName() == NAME_Plane)
		{
			Cmd.Type = ERepLayoutCmdType::PropertyPlane;
		}
		else if (Struct->GetFName() == NAME_Vector_NetQuantize100)
		{
			Cmd.Type = ERepLayoutCmdType::PropertyVector100;
		}
		else if (Struct->GetFName() == NAME_Vector_NetQuantize10)
		{
			Cmd.Type = ERepLayoutCmdType::PropertyVector10;
		}
		else if (Struct->GetFName() == NAME_Vector_NetQuantizeNormal)
		{
			Cmd.Type = ERepLayoutCmdType::PropertyVectorNormal;
		}
		else if (Struct->GetFName() == NAME_Vector_NetQuantize)
		{
			Cmd.Type = ERepLayoutCmdType::PropertyVectorQ;
		}
		else if (Struct->GetFName() == NAME_UniqueNetIdRepl)
		{
			Cmd.Type = ERepLayoutCmdType::PropertyNetId;
		}
		else if (Struct->GetFName() == NAME_RepMovement)
		{
			Cmd.Type = ERepLayoutCmdType::RepMovement;
		}
		else
		{
			UE_LOG(LogRep, VeryVerbose, TEXT("AddPropertyCmd: Falling back to default type for property [%s]"), *Cmd.Property->GetFullName());
		}
	}
	else if (UnderlyingProperty->IsA(UBoolProperty::StaticClass()))
	{
		const UBoolProperty* BoolProperty = static_cast<UBoolProperty*>(UnderlyingProperty);
		Cmd.Type = BoolProperty->IsNativeBool() ? ERepLayoutCmdType::PropertyNativeBool : ERepLayoutCmdType::PropertyBool;
	}
	else if (UnderlyingProperty->IsA(UFloatProperty::StaticClass()))
	{
		Cmd.Type = ERepLayoutCmdType::PropertyFloat;
	}
	else if (UnderlyingProperty->IsA(UIntProperty::StaticClass()))
	{
		Cmd.Type = ERepLayoutCmdType::PropertyInt;
	}
	else if (UnderlyingProperty->IsA(UByteProperty::StaticClass()))
	{
		Cmd.Type = ERepLayoutCmdType::PropertyByte;
	}
	else if (UnderlyingProperty->IsA(UObjectPropertyBase::StaticClass()))
	{
		if (UnderlyingProperty->IsA(USoftObjectProperty::StaticClass()))
		{
			Cmd.Type = ERepLayoutCmdType::PropertySoftObject;
		}
		else if (UnderlyingProperty->IsA(UWeakObjectProperty::StaticClass()))
		{
			Cmd.Type = ERepLayoutCmdType::PropertyWeakObject;
		}
		else
		{
			Cmd.Type = ERepLayoutCmdType::PropertyObject;
		}
	}
	else if (UnderlyingProperty->IsA(UNameProperty::StaticClass()))
	{
		Cmd.Type = ERepLayoutCmdType::PropertyName;
	}
	else if (UnderlyingProperty->IsA(UUInt32Property::StaticClass()))
	{
		Cmd.Type = ERepLayoutCmdType::PropertyUInt32;
	}
	else if (UnderlyingProperty->IsA(UUInt64Property::StaticClass()))
	{
		Cmd.Type = ERepLayoutCmdType::PropertyUInt64;
	}
	else if (UnderlyingProperty->IsA(UStrProperty::StaticClass()))
	{
		Cmd.Type = ERepLayoutCmdType::PropertyString;
	}
	else
	{
		UE_LOG(LogRep, VeryVerbose, TEXT("AddPropertyCmd: Falling back to default type for property [%s]"), *Cmd.Property->GetFullName());
	}

	// Cannot write a shared version of a property that depends on per-connection data (the PackageMap).
	// Includes object pointers and structs with custom NetSerialize functions (unless they opt in)
	// Also skip writing the RemoteRole since it can be modified per connection in FObjectReplicator
	if (Cmd.Property->SupportsNetSharedSerialization() && (Cmd.Property->GetFName() != NAME_RemoteRole))
	{
		Cmd.Flags |= ERepLayoutCmdFlags::IsSharedSerialization;
	}

	return Cmd.CompatibleChecksum;
}

static FORCEINLINE uint32 AddArrayCmd(
	TArray<FRepLayoutCmd>& Cmds,
	UArrayProperty* Property,
	int32 Offset,
	int32 RelativeHandle,
	int32 ParentIndex,
	uint32 ParentChecksum,
	int32 StaticArrayIndex,
	const UNetConnection* ServerConnection)
{
	FRepLayoutCmd& Cmd = Cmds.AddZeroed_GetRef();

	Cmd.Type = ERepLayoutCmdType::DynamicArray;
	Cmd.Property = Property;
	Cmd.Offset = Offset;
	Cmd.ElementSize = Property->Inner->ElementSize;
	Cmd.RelativeHandle = RelativeHandle;
	Cmd.ParentIndex = ParentIndex;
	Cmd.CompatibleChecksum = GetRepLayoutCmdCompatibleChecksum(Property, ServerConnection, StaticArrayIndex, ParentChecksum);

	return Cmd.CompatibleChecksum;
}

static FORCEINLINE void AddReturnCmd(TArray<FRepLayoutCmd>& Cmds)
{
	Cmds.AddZeroed_GetRef().Type = ERepLayoutCmdType::Return;
}

enum class ERepBuildType
{
	Class,
	Function,
	Struct
};

template<ERepBuildType BuildType>
static FORCEINLINE const int32 GetOffsetForProperty(UProperty& Property)
{
	return Property.GetOffset_ForGC();
}

template<>
const FORCEINLINE int32 GetOffsetForProperty<ERepBuildType::Function>(UProperty& Property)
{
	return Property.GetOffset_ForUFunction();
}

template<ERepBuildType BuildType>
static int32 InitFromProperty_r(
	TArray<FRepLayoutCmd>& Cmds,
	UProperty* Property,
	int32 Offset,
	int32 RelativeHandle,
	int32 ParentIndex,
	uint32 ParentChecksum,
	int32 StaticArrayIndex,
	const UNetConnection* ServerConnection)
{
	UArrayProperty * ArrayProp = Cast<UArrayProperty>(Property);

	if (ArrayProp != NULL)
	{
		const int32 CmdStart = Cmds.Num();

		RelativeHandle++;

		const uint32 ArrayChecksum = AddArrayCmd(Cmds, ArrayProp, Offset + GetOffsetForProperty<BuildType>(*ArrayProp), RelativeHandle, ParentIndex, ParentChecksum, StaticArrayIndex, ServerConnection);

		InitFromProperty_r<BuildType>(Cmds, ArrayProp->Inner, 0, 0, ParentIndex, ArrayChecksum, 0, ServerConnection);

		AddReturnCmd(Cmds);

		Cmds[CmdStart].EndCmd = Cmds.Num();		// Patch in the offset to jump over our array inner elements

		return RelativeHandle;
	}

	UStructProperty* StructProp = Cast<UStructProperty>(Property);

	if (StructProp != NULL)
	{
		UScriptStruct* Struct = StructProp->Struct;

		if (EnumHasAnyFlags(Struct->StructFlags, STRUCT_NetSerializeNative))
		{
			UE_CLOG(EnumHasAnyFlags(Struct->StructFlags, STRUCT_NetDeltaSerializeNative), LogRep, Warning, TEXT("RepLayout InitFromProperty_r: Struct marked both NetSerialize and NetDeltaSerialize: %s"), *StructProp->GetName());

			RelativeHandle++;
			AddPropertyCmd(Cmds, Property, Offset + GetOffsetForProperty<BuildType>(*Property), RelativeHandle, ParentIndex, ParentChecksum, StaticArrayIndex, ServerConnection);
			return RelativeHandle;
		}

		// Track properties so we can ensure they are sorted by offsets at the end
		// TODO: Do these actually need to be sorted?
		TArray<UProperty*> NetProperties;

		for (TFieldIterator<UProperty> It(Struct); It; ++It)
		{
			if ((It->PropertyFlags & CPF_RepSkip))
			{
				continue;
			}

			NetProperties.Add(*It);
		}

		// Sort NetProperties by memory offset
		struct FCompareUFieldOffsets
		{
			FORCEINLINE bool operator()(UProperty& A, UProperty& B) const
			{
				const int32 AOffset = GetOffsetForProperty<BuildType>(A);
				const int32 BOffset = GetOffsetForProperty<BuildType>(B);

				// Ensure stable sort
				if (AOffset == BOffset)
				{
					return A.GetName() < B.GetName();
				}

				return AOffset < BOffset;
			}
		};

		Sort(NetProperties.GetData(), NetProperties.Num(), FCompareUFieldOffsets());

		const uint32 StructChecksum = GetRepLayoutCmdCompatibleChecksum(Property, ServerConnection, StaticArrayIndex, ParentChecksum);

		for (int32 i = 0; i < NetProperties.Num(); i++)
		{
			for (int32 j = 0; j < NetProperties[i]->ArrayDim; j++)
			{
				const int32 ArrayElementOffset = j * NetProperties[i]->ElementSize;
				RelativeHandle = InitFromProperty_r<BuildType>(Cmds, NetProperties[i], Offset + GetOffsetForProperty<BuildType>(*StructProp) + ArrayElementOffset, RelativeHandle, ParentIndex, StructChecksum, j, ServerConnection);
			}
		}
		return RelativeHandle;
	}

	// Add actual property
	RelativeHandle++;

	AddPropertyCmd(Cmds, Property, Offset + GetOffsetForProperty<BuildType>(*Property), RelativeHandle, ParentIndex, ParentChecksum, StaticArrayIndex, ServerConnection);

	return RelativeHandle;
}

static FORCEINLINE uint16 AddParentProperty(TArray<FRepParentCmd>& Parents, UProperty* Property, int32 ArrayIndex)
{
	return Parents.Emplace(Property, ArrayIndex);
}

/** Setup some flags on our parent properties, so we can handle them properly later.*/
static FORCEINLINE void SetupRepStructFlags(FRepParentCmd& Parent, const bool bSkipCustomDeltaCheck)
{
	if (UStructProperty* StructProperty = Cast<UStructProperty>(Parent.Property))
	{
		UScriptStruct* Struct = StructProperty->Struct;

		Parent.Flags |= ERepParentFlags::IsStructProperty;

		if (!bSkipCustomDeltaCheck && EnumHasAnyFlags(Struct->StructFlags, STRUCT_NetDeltaSerializeNative))
		{
			Parent.Flags |= ERepParentFlags::IsCustomDelta;

			if (Struct->IsChildOf(FFastArraySerializer::StaticStruct()))
			{
				Parent.Flags |= ERepParentFlags::IsFastArray;
			}
		}

		if (EnumHasAnyFlags(Struct->StructFlags, STRUCT_NetSerializeNative))
		{
			Parent.Flags |= ERepParentFlags::IsNetSerialize;
		}
	}

	if (EnumHasAnyFlags(Parent.Property->PropertyFlags, CPF_ZeroConstructor))
	{
		Parent.Flags |= ERepParentFlags::IsZeroConstructible;
	}
}

/**
* Dynamic Array Properties:
*		These will have their memory allocated separate from the actual Shadow Buffer.
*		Conceptually, their layout in the Shadow Buffer is a separate sub-RepLayout with only one Parent Property
*		and potentially multiple children.
*
* Static Array Properties:
*		These will have their memory allocated inline in the shadow buffer.
*		Due to the way we currently initialize, construct, and destruct elements, we need
*		to allocate the entire size of the elements in these arrays.
*		@see InitProperties, ConstructProperties, DestructProperties.
*
* Struct Properties are broken into 3 main cases:
*
*		NetDeltaSerialize:
*			These structs will not have Child Rep Commands, but they will still have Parent Commands.
*			This is because we generally don't care about their Memory Layout, but we need to
*			be able to initialize them properly.
*
*		NetSerialize:
*			These structs will have a single Child Rep Command for the UStructProperty.
*			Similar to NetDeltaSerialize, we don't really care about the memory layout of NetSerialize
*			structs, but we still need to know where they live so we can diff them, etc.
*
*		Everything Else:
*			These structs will have potentially many Child Rep Commands, as we flatten their structure.
*			Note, there **will not** be a Child Rep Command for the actual owning property.
*			We do care about the memory layout in this case, because the RepLayout will be
*			completely in charge of serialization, comparisons, etc.
*
*		For every case, we will still end up allocating the complete struct into the shadow state.
*/
template<bool bAlreadyAligned>
static void BuildShadowOffsets_r(TArray<FRepLayoutCmd>::TIterator& CmdIt, int32& ShadowOffset)
{
	check(CmdIt);
	check(ERepLayoutCmdType::Return != CmdIt->Type);

	// Note, the only time we should see a StructProperty is if we have a NetSerialize struct.
	// Custom Delta Serialize structs won't have an associated RepLayout command,
	// and normal structs will flatten their properties.
	if (CmdIt->Type == ERepLayoutCmdType::DynamicArray || EnumHasAnyFlags(CmdIt->Flags, ERepLayoutCmdFlags::IsStruct))
	{
		if (!bAlreadyAligned)
		{
			// Note, we can't use the Commands reported element size, as Array Commands
			// will have that set to their inner property size.

			ShadowOffset = Align(ShadowOffset, CmdIt->Property->GetMinAlignment());
			CmdIt->ShadowOffset = ShadowOffset;
			ShadowOffset += CmdIt->Property->GetSize();
		}

		if (CmdIt->Type == ERepLayoutCmdType::DynamicArray)
		{
			// Iterator into the array's layout.
			++CmdIt;

			for (; ERepLayoutCmdType::Return != CmdIt->Type; ++CmdIt)
			{
				CmdIt->ShadowOffset = CmdIt->Offset;
				BuildShadowOffsets_r</*bAlreadyAligned=*/true>(CmdIt, CmdIt->ShadowOffset);
			}

			check(CmdIt);
		}
	}
	else if (!bAlreadyAligned)
	{
		// This property is already aligned, and ShadowOffset should be correct and managed elsewhere.
		if (ShadowOffset > 0)
		{
			// Bools may be packed as bitfields, and if so they can be stored in the same location
			// as a previous property.
			if (ERepLayoutCmdType::PropertyBool == CmdIt->Type && CmdIt.GetIndex() > 0)
			{
				const TArray<FRepLayoutCmd>::TIterator PrevCmdIt = CmdIt - 1;
				if (ERepLayoutCmdType::PropertyBool == PrevCmdIt->Type && PrevCmdIt->Offset == CmdIt->Offset)
				{
					ShadowOffset = PrevCmdIt->ShadowOffset;
				}
			}
			else
			{
				ShadowOffset = Align(ShadowOffset, CmdIt->Property->GetMinAlignment());
			}
		}

		CmdIt->ShadowOffset = ShadowOffset;
		ShadowOffset += CmdIt->ElementSize;
	}
}

template<ERepBuildType ShadowType>
static void BuildShadowOffsets(UStruct* Owner, TArray<FRepParentCmd>& Parents, TArray<FRepLayoutCmd>& Cmds, int32& ShadowOffset)
{
	SCOPE_CYCLE_COUNTER(STAT_RepLayout_BuildShadowOffsets);

	if (ShadowType == ERepBuildType::Class && !!GUsePackedShadowBuffers)
	{
		ShadowOffset = 0;

		if (0 != Parents.Num())
		{
			// Before filling out any ShadowOffset information, we'll sort the Parent Commands by alignment.
			// This has 2 main benefits:
			//	1. It will guarantee a minimal amount of wasted space when packing.
			//	2. It should generally improve cache hit rate when iterating over commands.
			//		Even though iteration of the commands won't actually be ordered anywhere else,
			//		this increases the likelihood that more shadow data fits into a single cache line.
			struct FParentCmdIndexAndAlignment
			{
				FParentCmdIndexAndAlignment(int32 ParentIndex, const FRepParentCmd& Parent):
					Index(ParentIndex),
					Alignment(Parent.Property->GetMinAlignment())
				{
				}

				const int32 Index;
				const int32 Alignment;

				// Needed for sorting.
				bool operator< (const FParentCmdIndexAndAlignment& RHS) const
				{
					return Alignment < RHS.Alignment;
				}
			};

			TArray<FParentCmdIndexAndAlignment> IndexAndAlignmentArray;
			IndexAndAlignmentArray.Reserve(Parents.Num());
			for (int32 i = 0; i < Parents.Num(); ++i)
			{
				IndexAndAlignmentArray.Emplace(i, Parents[i]);
			}

			IndexAndAlignmentArray.StableSort();

			for (int32 i = 0; i < IndexAndAlignmentArray.Num(); ++i)
			{
				const FParentCmdIndexAndAlignment& IndexAndAlignment = IndexAndAlignmentArray[i];
				FRepParentCmd& Parent = Parents[IndexAndAlignment.Index];

				if (Parent.Property->ArrayDim > 1 || EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsStructProperty))
				{
					const int32 ArrayStartParentOffset = GetOffsetForProperty<ShadowType>(*Parent.Property);

					ShadowOffset = Align(ShadowOffset, IndexAndAlignment.Alignment);

					for (int32 j = 0; j < Parent.Property->ArrayDim; ++j, ++i)
					{
						const FParentCmdIndexAndAlignment& NextIndexAndAlignment = IndexAndAlignmentArray[i];
						FRepParentCmd& NextParent = Parents[NextIndexAndAlignment.Index];

						NextParent.ShadowOffset = ShadowOffset + (GetOffsetForProperty<ShadowType>(*NextParent.Property) - ArrayStartParentOffset);

						for (auto CmdIt = Cmds.CreateIterator() + NextParent.CmdStart; CmdIt.GetIndex() < NextParent.CmdEnd; ++CmdIt)
						{
							CmdIt->ShadowOffset = ShadowOffset + (CmdIt->Offset - ArrayStartParentOffset);
							BuildShadowOffsets_r</*bAlreadyAligned*/true>(CmdIt, CmdIt->ShadowOffset);
						}
					}

					// The above loop will have advanced us one too far, so roll back.
					// This will make sure the outer loop has a chance to process the parent next time.
					--i;
					ShadowOffset += Parent.Property->GetSize();
				}
				else
				{
					check(Parent.CmdEnd > Parent.CmdStart);

					for (auto CmdIt = Cmds.CreateIterator() + Parent.CmdStart; CmdIt.GetIndex() < Parent.CmdEnd; ++CmdIt)
					{
						BuildShadowOffsets_r</*bAlreadyAligned=*/false>(CmdIt, ShadowOffset);
					}

					// We update this after we build child commands offsets, to make sure that
					// if there's any extra packing (like bitfield packing), we are aware of it.
					Parent.ShadowOffset = Cmds[Parent.CmdStart].ShadowOffset;
				}
			}
		}
	}
	else
	{
		ShadowOffset = Owner->GetPropertiesSize();

		for (auto ParentIt = Parents.CreateIterator(); ParentIt; ++ParentIt)
		{
			ParentIt->ShadowOffset = GetOffsetForProperty<ShadowType>(*ParentIt->Property);
		}

		for (auto CmdIt = Cmds.CreateIterator(); CmdIt; ++CmdIt)
		{
			CmdIt->ShadowOffset = CmdIt->Offset;
		}
	}
}

TSharedPtr<FRepLayout> FRepLayout::CreateFromClass(
	UClass* InClass,
	const UNetConnection* ServerConnection,
	const ECreateRepLayoutFlags CreateFlags)
{
	TSharedPtr<FRepLayout> RepLayout = MakeShareable<FRepLayout>(new FRepLayout());
	RepLayout->InitFromClass(InClass, ServerConnection, CreateFlags);
	return RepLayout;
}

void FRepLayout::InitFromClass(
	UClass* InObjectClass,
	const UNetConnection* ServerConnection,
	const ECreateRepLayoutFlags CreateFlags)
{
	SCOPE_CYCLE_COUNTER(STAT_RepLayout_InitFromObjectClass);
	SCOPE_CYCLE_UOBJECT(ObjectClass, InObjectClass);

	const bool bIsObjectActor = InObjectClass->IsChildOf(AActor::StaticClass());
	RoleIndex = INDEX_NONE;
	RemoteRoleIndex = INDEX_NONE;

	if (bIsObjectActor)
	{
		Flags |= ERepLayoutFlags::IsActor;
	}

	int32 RelativeHandle = 0;
	int32 LastOffset = INDEX_NONE;
	int32 HighestCustomDeltaRepIndex = INDEX_NONE;

	InObjectClass->SetUpRuntimeReplicationData();
	Parents.Empty(InObjectClass->ClassReps.Num());

	for (int32 i = 0; i < InObjectClass->ClassReps.Num(); i++)
	{
		UProperty * Property = InObjectClass->ClassReps[i].Property;
		const int32 ArrayIdx = InObjectClass->ClassReps[i].Index;

		check(Property->PropertyFlags & CPF_Net);

		const int32 ParentHandle = AddParentProperty(Parents, Property, ArrayIdx);

		check(ParentHandle == i);
		check(Parents[i].Property->RepIndex + Parents[i].ArrayIndex == i);

		const int32 ParentOffset = Property->ElementSize * ArrayIdx;

		Parents[ParentHandle].CmdStart = Cmds.Num();
		RelativeHandle = InitFromProperty_r<ERepBuildType::Class>(Cmds, Property, ParentOffset, RelativeHandle, ParentHandle, 0, ArrayIdx, ServerConnection);
		Parents[ParentHandle].CmdEnd = Cmds.Num();
		Parents[ParentHandle].Flags |= ERepParentFlags::IsConditional;
		Parents[ParentHandle].Offset = GetOffsetForProperty<ERepBuildType::Class>(*Property) + ParentOffset;

		if (Parents[i].CmdEnd > Parents[i].CmdStart)
		{
			check(Cmds[Parents[i].CmdStart].Offset >= LastOffset);		//>= since bool's can be combined
			LastOffset = Cmds[Parents[i].CmdStart].Offset;
		}

		// Setup flags
		SetupRepStructFlags(Parents[ParentHandle], /**bSkipCustomDeltaCheck=*/false);

		if (Property->GetPropertyFlags() & CPF_Config)
		{
			Parents[ParentHandle].Flags |= ERepParentFlags::IsConfig;
		}

		if (bIsObjectActor)
		{
		    // Find Role/RemoteRole property indexes so we can swap them on the client
		    if (Property->GetFName() == NAME_Role)
		    {
			    check(RoleIndex == INDEX_NONE);
			    check(Parents[ParentHandle].CmdEnd == Parents[ParentHandle].CmdStart + 1);
			    RoleIndex = ParentHandle;
		    }

		    if (Property->GetFName() == NAME_RemoteRole)
		    {
			    check(RemoteRoleIndex == INDEX_NONE);
			    check(Parents[ParentHandle].CmdEnd == Parents[ParentHandle].CmdStart + 1);
			    RemoteRoleIndex = ParentHandle;
		    }
	    }

		if (EnumHasAnyFlags(Parents[ParentHandle].Flags, ERepParentFlags::IsCustomDelta))
		{
			HighestCustomDeltaRepIndex = ParentHandle;
		}
	}

	// Make sure it either found both, or didn't find either
	check((RoleIndex == INDEX_NONE) == (RemoteRoleIndex == INDEX_NONE));

	// Make sure that we only find these if we're an Actor, and if we're
	// an Actor we always find these.
	check((RoleIndex == INDEX_NONE) == !bIsObjectActor);

	// This is so the receiving side can swap these as it receives them
	if (RoleIndex != -1)
	{
		// Make sure that if we have Role and RemoteRole, that Role comes before RemoteRole.
		// If this fails, it means that the order of Role and RemoteRole has been changed in AActor, and that
		// will break assumptions RepLayout makes.
		check(RemoteRoleIndex < RoleIndex);
		Parents[RoleIndex].RoleSwapIndex = RemoteRoleIndex;
		Parents[RemoteRoleIndex].RoleSwapIndex = RoleIndex;
	}
	
	AddReturnCmd(Cmds);

	// Initialize lifetime props
	// Properties that replicate for the lifetime of the channel
	TArray<FLifetimeProperty> LifetimeProps;

	UObject* Object = InObjectClass->GetDefaultObject();

	Object->GetLifetimeReplicatedProps(LifetimeProps);

	// Setup lifetime replicated properties
	for (int32 i = 0; i < LifetimeProps.Num(); i++)
	{
		const int32 ParentIndex = LifetimeProps[i].RepIndex;

		if (!ensureMsgf(Parents.IsValidIndex(ParentIndex), TEXT("Parents array index %d out of bounds! i = %d, LifetimeProps.Num() = %d, Parents.Num() = %d, InObjectClass = %s"),
				ParentIndex, i, LifetimeProps.Num(), Parents.Num(), *GetFullNameSafe(InObjectClass)))
		{
			continue;
		}

		// Don't bother doing any setup work for COND_Never properties.
		// These are never expected to replicate.
		if (COND_Never == LifetimeProps[i].Condition)
		{
			continue;
		}

		// Store the condition on the parent in case we need it
		Parents[ParentIndex].Condition = LifetimeProps[i].Condition;
		Parents[ParentIndex].RepNotifyCondition = LifetimeProps[i].RepNotifyCondition;

		if (UFunction* RepNotifyFunc = InObjectClass->FindFunctionByName(Parents[ParentIndex].Property->RepNotifyFunc))
		{
			Parents[ParentIndex].RepNotifyNumParams = RepNotifyFunc->NumParms;
		}

		if (EnumHasAnyFlags(Parents[ParentIndex].Flags, ERepParentFlags::IsCustomDelta))
		{
			// We'll track all Custom Lifetime Properties here, and we'll handle Fast Array Serialization
			// specially.

			// Note, there are in engine cases where we aren't using FFastArraySerialize, but are using
			// Custom Delta (FGameplayDebuggerNetPack).

			// Also note, we still don't mark these properties as Lifetime. This should help maintain behavior
			// in Diff Properties / Compare Properties.

			if (!LifetimeCustomPropertyState)
			{
				// We can't use the number of Lifetime Properties, because that could be smaller than
				// the highest RepIndex of a Custom Delta Property, because properties may be disabled, removed,
				// or just never added.
				// For similar reasons, we don't want to use the total number of replicated properties, especially
				// if we know we'll never use anything beyond the last Custom Delta Property anyway.
				LifetimeCustomPropertyState.Reset(new FLifetimeCustomDeltaState(HighestCustomDeltaRepIndex));
			}

			// If we're a FastArraySerializer, we'll look for our replicated item type.
			// We do this by looking for an array property whose inner type is an FFastArraySerializerItem.
			// Note, this isn't perfect. With the way the interface is set up now, there's no technically
			// enforced requirements that the Array of items lives within the Fast Array Serializer, that the
			// Array of items is marked up as a UProperty, that the Array of items is not marked RepSkip,
			// or that there's not multiple arrays of FastArraySerializerItems.
			//
			// However, comments imply these, and typically they are true (certainly, any engine cases follow this).
			// Further, these layouts are only needed for the new Delta Struct Serialization feature, so this won't break backwards compat.

			bool bAddedFastArray = false;

			if (EnumHasAnyFlags(Parents[ParentIndex].Flags, ERepParentFlags::IsFastArray))
			{
				int32 FastArrayItemArrayCmd = INDEX_NONE;

				for (int32 CmdIndex = Parents[ParentIndex].CmdStart; CmdIndex < Parents[ParentIndex].CmdEnd; ++CmdIndex)
				{
					const FRepLayoutCmd& Cmd = Cmds[CmdIndex];
					if (ERepLayoutCmdType::DynamicArray == Cmd.Type)
					{
						if (UStructProperty* MaybeFastArrayItemsArray = Cast<UStructProperty>(static_cast<UArrayProperty*>(Cmd.Property)->Inner))
						{
							UScriptStruct* MaybeFastArrayItem = MaybeFastArrayItemsArray->Struct;
							if (MaybeFastArrayItem->IsChildOf(FFastArraySerializerItem::StaticStruct()))
							{
								// Can't use GET_MEMBER_NAME_CHECKED because this is private.
								static const FName FastArrayDeltaFlagsName(TEXT("DeltaFlags"));
								static const FName FastArrayArrayReplicationKeyName(GET_MEMBER_NAME_CHECKED(FFastArraySerializer, ArrayReplicationKey));
								static const FName FastArrayItemReplicationIDName(GET_MEMBER_NAME_CHECKED(FFastArraySerializerItem, ReplicationID));

								// This better be a script struct, otherwise our flags aren't set up correctly!
								UScriptStruct* FastArray = CastChecked<UScriptStruct>(MaybeFastArrayItemsArray->GetOwnerStruct());

								LifetimeCustomPropertyState->Add(FLifetimeCustomDeltaProperty(
									/*PropertyRepIndex=*/ParentIndex,
									/*FastArrayItemsCommand=*/CmdIndex,
									/*FastArrayNumber=*/LifetimeCustomPropertyState->GetNumFastArrayProperties(),
									/*FastArrayDeltaFlagsOffset=*/FastArray->FindPropertyByName(FastArrayDeltaFlagsName)->GetOffset_ForGC(),
									/*FastArrayReplicationKeyOffset=*/FastArray->FindPropertyByName(FastArrayArrayReplicationKeyName)->GetOffset_ForGC(),
									/*FastArrayItemReplicationIdOffset=*/MaybeFastArrayItem->FindPropertyByName(FastArrayItemReplicationIDName)->GetOffset_ForGC()
								));

								bAddedFastArray = true;
								break;
							}
						}

						CmdIndex = Cmd.EndCmd - 1;
					}
				}

				if (!bAddedFastArray)
				{
					UE_LOG(LogRep, Warning, TEXT("FRepLayout::InitFromClass: Unable to find Fast Array Item array in Fast Array Serializer: %s"), *Parents[ParentIndex].CachedPropertyName.ToString());
				}
			}

			if (!bAddedFastArray)
			{
				LifetimeCustomPropertyState->Add(FLifetimeCustomDeltaProperty(ParentIndex));
			}

			continue;
		}

		Parents[ParentIndex].Flags |= ERepParentFlags::IsLifetime;

		if (LifetimeProps[i].Condition == COND_None)
		{
			Parents[ParentIndex].Flags &= ~ERepParentFlags::IsConditional;
		}
	}

	if (bIsObjectActor)
	{
		// We handle remote role specially, since it can change between connections when downgraded
		// So we force it on the conditional list
		FRepParentCmd& RemoteRoleParent = Parents[RemoteRoleIndex];
		if (RemoteRoleParent.Condition != COND_Never)
		{
			if (COND_None != RemoteRoleParent.Condition)
			{
				UE_LOG(LogRep, Warning, TEXT("FRepLayout::InitFromClass: Forcing replication of RemoteRole. Owner=%s"), *InObjectClass->GetPathName());
			}

			Parents[RemoteRoleIndex].Flags |= ERepParentFlags::IsConditional;
			Parents[RemoteRoleIndex].Condition = COND_None;
		}
	}

	if (!ServerConnection || EnumHasAnyFlags(CreateFlags, ECreateRepLayoutFlags::MaySendProperties))
	{
		BuildHandleToCmdIndexTable_r(0, Cmds.Num() - 1, BaseHandleToCmdIndex);
	}

	BuildShadowOffsets<ERepBuildType::Class>(InObjectClass, Parents, Cmds, ShadowDataBufferSize);

	Owner = InObjectClass;
}

TSharedPtr<FRepLayout> FRepLayout::CreateFromFunction(UFunction* InFunction, const UNetConnection* ServerConnection, const ECreateRepLayoutFlags CreateFlags)
{
	TSharedPtr<FRepLayout> RepLayout = MakeShareable<FRepLayout>(new FRepLayout());
	RepLayout->InitFromFunction(InFunction, ServerConnection, CreateFlags);
	return RepLayout;
}

void FRepLayout::InitFromFunction(
	UFunction* InFunction,
	const UNetConnection* ServerConnection,
	const ECreateRepLayoutFlags CreateFlags)
{
	int32 RelativeHandle = 0;

	for (TFieldIterator<UProperty> It(InFunction); It && (It->PropertyFlags & (CPF_Parm | CPF_ReturnParm)) == CPF_Parm; ++It)
	{
		for (int32 ArrayIdx = 0; ArrayIdx < It->ArrayDim; ++ArrayIdx)
		{
			const int32 ParentHandle = AddParentProperty(Parents, *It, ArrayIdx);
			Parents[ParentHandle].CmdStart = Cmds.Num();
			RelativeHandle = InitFromProperty_r<ERepBuildType::Function>(Cmds, *It, It->ElementSize * ArrayIdx, RelativeHandle, ParentHandle, 0, ArrayIdx, ServerConnection);
			Parents[ParentHandle].CmdEnd = Cmds.Num();
			Parents[ParentHandle].Offset = GetOffsetForProperty<ERepBuildType::Function>(**It);

			SetupRepStructFlags(Parents[ParentHandle], /**bSkipCustomDeltaCheck=*/true);
		}
	}

	AddReturnCmd(Cmds);

	if (!ServerConnection || EnumHasAnyFlags(CreateFlags, ECreateRepLayoutFlags::MaySendProperties))
	{
		BuildHandleToCmdIndexTable_r(0, Cmds.Num() - 1, BaseHandleToCmdIndex);
	}

	BuildShadowOffsets<ERepBuildType::Function>(InFunction, Parents, Cmds, ShadowDataBufferSize);

	Owner = InFunction;
}

TSharedPtr<FRepLayout> FRepLayout::CreateFromStruct(
	UStruct* InStruct,
	const UNetConnection* ServerConnection,
	const ECreateRepLayoutFlags CreateFlags)
{
	TSharedPtr<FRepLayout> RepLayout = MakeShareable<FRepLayout>(new FRepLayout());
	RepLayout->InitFromStruct(InStruct, ServerConnection, CreateFlags);
	return RepLayout;
}

void FRepLayout::InitFromStruct(
	UStruct* InStruct,
	const UNetConnection* ServerConnection,
	const ECreateRepLayoutFlags CreateFlags)
{
	int32 RelativeHandle = 0;

	for (TFieldIterator<UProperty> It(InStruct); It; ++It)
	{
		if (It->PropertyFlags & CPF_RepSkip)
		{
			continue;
		}
			
		for (int32 ArrayIdx = 0; ArrayIdx < It->ArrayDim; ++ArrayIdx)
		{
			const int32 ParentHandle = AddParentProperty(Parents, *It, ArrayIdx);
			Parents[ParentHandle].CmdStart = Cmds.Num();
			RelativeHandle = InitFromProperty_r<ERepBuildType::Struct>(Cmds, *It, It->ElementSize * ArrayIdx, RelativeHandle, ParentHandle, 0, ArrayIdx, ServerConnection);
			Parents[ParentHandle].CmdEnd = Cmds.Num();
			Parents[ParentHandle].Offset = GetOffsetForProperty<ERepBuildType::Struct>(**It);

			SetupRepStructFlags(Parents[ParentHandle], /**bSkipCustomDeltaCheck=*/true);
		}
	}

	AddReturnCmd(Cmds);

	if (!ServerConnection || EnumHasAnyFlags(CreateFlags, ECreateRepLayoutFlags::MaySendProperties))
	{
		BuildHandleToCmdIndexTable_r(0, Cmds.Num() - 1, BaseHandleToCmdIndex);
	}

	BuildShadowOffsets<ERepBuildType::Struct>(InStruct, Parents, Cmds, ShadowDataBufferSize);

	Owner = InStruct;
}

void FRepLayout::SerializeProperties_DynamicArray_r(
	FBitArchive& Ar, 
	UPackageMap* Map,
	const int32 CmdIndex,
	FRepObjectDataBuffer Data,
	bool& bHasUnmapped,
	const int32 ArrayDepth,
	const FRepSerializationSharedInfo& SharedInfo) const
{
	const FRepLayoutCmd& Cmd = Cmds[ CmdIndex ];

	FScriptArray* Array = (FScriptArray*)Data.Data;

	uint16 OutArrayNum = Array->Num();
	Ar << OutArrayNum;

	// If loading from the archive, OutArrayNum will contain the number of elements.
	// Otherwise, use the input number of elements.
	const int32 ArrayNum = Ar.IsLoading() ? (int32)OutArrayNum : Array->Num();

	// Validate the maximum number of elements.
	if (ArrayNum> MaxRepArraySize)
	{
		UE_LOG(LogRepTraffic, Error, TEXT("SerializeProperties_DynamicArray_r: ArraySize (%d) > net.MaxRepArraySize(%d) (%s). net.MaxRepArraySize can be updated in Project Settings under Network Settings."),
			ArrayNum, MaxRepArraySize, *Cmd.Property->GetName());

		Ar.SetError();
	}
	// Validate the maximum memory.
	else if (ArrayNum * (int32)Cmd.ElementSize > MaxRepArrayMemory)
	{
		UE_LOG(LogRepTraffic, Error,
			TEXT("SerializeProperties_DynamicArray_r: ArraySize (%d) * Cmd.ElementSize (%d) > net.MaxRepArrayMemory(%d) (%s). net.MaxRepArrayMemory can be updated in Project Settings under Network Settings."),
			ArrayNum, (int32)Cmd.ElementSize, MaxRepArrayMemory, *Cmd.Property->GetName());

		Ar.SetError();
	}

	if (!Ar.IsError())
	{
		// When loading, we may need to resize the array to properly fit the number of elements.
		if (Ar.IsLoading() && OutArrayNum != Array->Num())
		{
			FScriptArrayHelper ArrayHelper((UArrayProperty*)Cmd.Property, Data);
			ArrayHelper.Resize(OutArrayNum);
		}

		FRepObjectDataBuffer ArrayData(Array->GetData());

		for (int32 i = 0; i < Array->Num() && !Ar.IsError(); i++)
		{
			const int32 ArrayElementOffset = i * Cmd.ElementSize;
			SerializeProperties_r(Ar, Map, CmdIndex + 1, Cmd.EndCmd - 1, ArrayData + ArrayElementOffset, bHasUnmapped, i, ArrayDepth, SharedInfo);
		}
	}	
}

void FRepLayout::SerializeProperties_r(
	FBitArchive& Ar, 
	UPackageMap* Map,
	const int32 CmdStart, 
	const int32 CmdEnd,
	FRepObjectDataBuffer Data,
	bool& bHasUnmapped,
	const int32 ArrayIndex,
	const int32 ArrayDepth,
	const FRepSerializationSharedInfo& SharedInfo) const
{
	for (int32 CmdIndex = CmdStart; CmdIndex < CmdEnd && !Ar.IsError(); CmdIndex++)
	{
		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		check(Cmd.Type != ERepLayoutCmdType::Return);

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			SerializeProperties_DynamicArray_r(Ar, Map, CmdIndex, Data + Cmd, bHasUnmapped, ArrayDepth + 1, SharedInfo);
			CmdIndex = Cmd.EndCmd - 1;		// The -1 to handle the ++ in the for loop
			continue;
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (GDoReplicationContextString > 0)
		{
			Map->SetDebugContextString(FString::Printf(TEXT("%s - %s"), *Owner->GetPathName(), *Cmd.Property->GetPathName()));
		}
#endif

		const FRepSerializedPropertyInfo* SharedPropInfo = nullptr;

		if ((GNetSharedSerializedData != 0) && Ar.IsSaving() && EnumHasAnyFlags(Cmd.Flags, ERepLayoutCmdFlags::IsSharedSerialization))
		{
			FGuid PropertyGuid(CmdIndex, ArrayIndex, ArrayDepth, (int32)((PTRINT)(const uint8*)(Data + Cmd) & 0xFFFFFFFF));

			SharedPropInfo = SharedInfo.SharedPropertyInfo.FindByPredicate([&](const FRepSerializedPropertyInfo& Info) 
			{ 
				return (Info.Guid == PropertyGuid); 
			});
		}

		// Use shared serialization state if it exists
		// Not concerned with unmapped guids because object references can't be shared
		if (SharedPropInfo)
		{
			GNumSharedSerializationHit++;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if ((GNetVerifyShareSerializedData != 0) && Ar.IsSaving())
			{
				FBitWriter& Writer = static_cast<FBitWriter&>(Ar);

				FBitWriterMark BitWriterMark(Writer);

				Cmd.Property->NetSerializeItem(Writer, Map, (Data + Cmd).Data);

				TArray<uint8> StandardBuffer;
				BitWriterMark.Copy(Writer, StandardBuffer);
				BitWriterMark.Pop(Writer);

				Writer.SerializeBitsWithOffset(SharedInfo.SerializedProperties->GetData(), SharedPropInfo->PropBitOffset, SharedPropInfo->PropBitLength);

				TArray<uint8> SharedBuffer;
				BitWriterMark.Copy(Writer, SharedBuffer);

				if (StandardBuffer != SharedBuffer)
				{
					UE_LOG(LogRep, Error, TEXT("Shared serialization data mismatch!"));
				}
			}
			else
#endif
			{
				Ar.SerializeBitsWithOffset(SharedInfo.SerializedProperties->GetData(), SharedPropInfo->PropBitOffset, SharedPropInfo->PropBitLength);
			}
		}
		else
		{
			GNumSharedSerializationMiss++;
			if (!Cmd.Property->NetSerializeItem(Ar, Map, (Data + Cmd).Data))
			{
				bHasUnmapped = true;
			}
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (GDoReplicationContextString > 0)
		{
			Map->ClearDebugContextString();
		}
#endif
	}
}

void FRepLayout::BuildChangeList_r(
	const TArray<FHandleToCmdIndex>& HandleToCmdIndex,
	const int32 CmdStart,
	const int32 CmdEnd,
	const FConstRepObjectDataBuffer Data,
	const int32 HandleOffset,
	const bool bForceArraySends,
	TArray<uint16>& Changed) const
{
	for (int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++)
	{
		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		check(Cmd.Type != ERepLayoutCmdType::Return);

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{			
			FScriptArray* Array = (FScriptArray *)(Data + Cmd).Data;
			const FConstRepObjectDataBuffer ArrayData = Array->GetData();

			TArray<uint16> ChangedLocal;

			TArray<FHandleToCmdIndex>& ArrayHandleToCmdIndex = *HandleToCmdIndex[Cmd.RelativeHandle - 1].HandleToCmdIndex;

			const int32 ArrayCmdStart = CmdIndex + 1;
			const int32 ArrayCmdEnd = Cmd.EndCmd - 1;
			const int32 NumHandlesPerElement = ArrayHandleToCmdIndex.Num();

			check(NumHandlesPerElement > 0);

			for (int32 i = 0; i < Array->Num(); i++)
			{
				const int32 ArrayElementOffset = Cmd.ElementSize * i;
				BuildChangeList_r(ArrayHandleToCmdIndex, ArrayCmdStart, ArrayCmdEnd, ArrayData + ArrayElementOffset, i * NumHandlesPerElement, bForceArraySends, ChangedLocal);
			}

			if (ChangedLocal.Num())
			{
				Changed.Add(Cmd.RelativeHandle + HandleOffset);	// Identify the array cmd handle
				Changed.Add(ChangedLocal.Num());				// This is so we can jump over the array if we need to
				Changed.Append(ChangedLocal);					// Append the change list under the array
				Changed.Add(0);									// Null terminator
			}
			else if (bForceArraySends)
			{
				// Note the handle, that there were 0 changed elements, and the terminator.
				// This will force anyone sending this changelist later to send the array size
				// (which is most likely 0 in this case).
				Changed.Add(Cmd.RelativeHandle + HandleOffset);
				Changed.Add(0);
				Changed.Add(0);
			}

			CmdIndex = Cmd.EndCmd - 1;		// The -1 to handle the ++ in the for loop
			continue;
		}

		Changed.Add(Cmd.RelativeHandle + HandleOffset);
	}
}


void FRepLayout::BuildSharedSerialization(
	const FConstRepObjectDataBuffer Data,
	TArray<uint16>& Changed,
	const bool bWriteHandle,
	FRepSerializationSharedInfo& SharedInfo) const
{
#ifdef ENABLE_PROPERTY_CHECKSUMS
	const bool bDoChecksum = (GDoPropertyChecksum == 1);
#else
	const bool bDoChecksum = false;
#endif

	FChangelistIterator ChangelistIterator(Changed, 0);
	FRepHandleIterator HandleIterator(Owner, ChangelistIterator, Cmds, BaseHandleToCmdIndex, 0, 1, 0, Cmds.Num() - 1);

	BuildSharedSerialization_r(HandleIterator, Data, bWriteHandle, bDoChecksum, 0, SharedInfo);

	SharedInfo.SetValid();
}

void FRepLayout::BuildSharedSerialization_r(
	FRepHandleIterator& HandleIterator,
	const FConstRepObjectDataBuffer SourceData,
	const bool bWriteHandle,
	const bool bDoChecksum,
	const int32 ArrayDepth,
	FRepSerializationSharedInfo& SharedInfo) const
{
	while (HandleIterator.NextHandle())
	{
		const int32 CmdIndex = HandleIterator.CmdIndex;
		const int32 ArrayOffset = HandleIterator.ArrayOffset;

		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];
		const FRepParentCmd& ParentCmd = Parents[Cmd.ParentIndex];

		const FConstRepObjectDataBuffer Data = SourceData + ArrayOffset + Cmd;

		// Custom Deltas are not supported for shared serialization at this time.
		if (EnumHasAnyFlags(ParentCmd.Flags, ERepParentFlags::IsCustomDelta))
		{
			continue;
		}

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			const FScriptArray* Array = (FScriptArray *)Data.Data;
			const FConstRepObjectDataBuffer ArrayData = (uint8*)Array->GetData();

			FScopedIteratorArrayTracker ArrayTracker(&HandleIterator);

			TArray<FHandleToCmdIndex>& ArrayHandleToCmdIndex = *HandleIterator.HandleToCmdIndex[Cmd.RelativeHandle - 1].HandleToCmdIndex;

			FRepHandleIterator ArrayIterator(HandleIterator.Owner, HandleIterator.ChangelistIterator, Cmds, ArrayHandleToCmdIndex, Cmd.ElementSize, Array->Num(), CmdIndex + 1, Cmd.EndCmd - 1);
			BuildSharedSerialization_r(ArrayIterator, ArrayData, bWriteHandle, bDoChecksum, ArrayDepth + 1, SharedInfo);
			continue;
		}

		if (EnumHasAnyFlags(Cmd.Flags, ERepLayoutCmdFlags::IsSharedSerialization))
		{
			SharedInfo.WriteSharedProperty(Cmd, FGuid(HandleIterator.CmdIndex, HandleIterator.ArrayIndex, ArrayDepth, (int32)((PTRINT)Data.Data & 0xFFFFFFFF)), HandleIterator.CmdIndex, HandleIterator.Handle, Data.Data, bWriteHandle, bDoChecksum);
		}
	}
}

void FRepLayout::BuildSharedSerializationForRPC_DynamicArray_r(
	const int32 CmdIndex,
	const FConstRepObjectDataBuffer Data,
	int32 ArrayDepth,
	FRepSerializationSharedInfo& SharedInfo)
{
	const FRepLayoutCmd& Cmd = Cmds[ CmdIndex ];

	FScriptArray* Array = (FScriptArray *)Data.Data;	
	const int32 ArrayNum = Array->Num();

	// Validate the maximum number of elements.
	if (ArrayNum > MaxRepArraySize)
	{
		return;
	}
	// Validate the maximum memory.
	else if (ArrayNum * (int32)Cmd.ElementSize > MaxRepArrayMemory)
	{
		return;
	}

	const FConstRepObjectDataBuffer ArrayData(Array->GetData());

	for (int32 i = 0; i < ArrayNum; i++)
	{
		const int32 ArrayElementOffset = i * Cmd.ElementSize;
		BuildSharedSerializationForRPC_r(CmdIndex + 1, Cmd.EndCmd - 1, ArrayData + ArrayElementOffset, i, ArrayDepth, SharedInfo);
	}
}

void FRepLayout::BuildSharedSerializationForRPC_r(
	const int32 CmdStart,
	const int32 CmdEnd,
	const FConstRepObjectDataBuffer Data,
	int32 ArrayIndex,
	int32 ArrayDepth,
	FRepSerializationSharedInfo& SharedInfo)
{
	for (int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++)
	{
		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		check(Cmd.Type != ERepLayoutCmdType::Return);

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			BuildSharedSerializationForRPC_DynamicArray_r(CmdIndex, Data + Cmd, ArrayDepth + 1, SharedInfo);
			CmdIndex = Cmd.EndCmd - 1;		// The -1 to handle the ++ in the for loop
			continue;
		}

		if (!Parents[Cmd.ParentIndex].Property->HasAnyPropertyFlags(CPF_OutParm) && EnumHasAnyFlags(Cmd.Flags, ERepLayoutCmdFlags::IsSharedSerialization))
		{
			FGuid PropertyGuid(CmdIndex, ArrayIndex, ArrayDepth, (int32)((PTRINT)(const uint8*)(Data + Cmd) & 0xFFFFFFFF));

			SharedInfo.WriteSharedProperty(Cmd, PropertyGuid, CmdIndex, 0, (Data + Cmd).Data, false, false);
		}
	}
}

void FRepLayout::BuildSharedSerializationForRPC(const FConstRepObjectDataBuffer Data)
{
	if ((GNetSharedSerializedData != 0) && !SharedInfoRPC.IsValid())
	{
		SharedInfoRPCParentsChanged.Init(false, Parents.Num());

		for (int32 i = 0; i < Parents.Num(); i++)
		{
			if (Parents[i].Property->HasAnyPropertyFlags(CPF_OutParm))
			{
				continue;
			}

			bool bSend = true;

			if (!Cast<UBoolProperty>(Parents[i].Property))
			{
				// check for a complete match, including arrays
				// (we're comparing against zero data here, since 
				// that's the default.)
				bSend = !Parents[i].Property->Identical_InContainer(Data.Data, nullptr, Parents[i].ArrayIndex);
			}

			if (bSend)
			{
				// Cache result of property comparison to default so we only have to do it once
				SharedInfoRPCParentsChanged[i] = true;

				BuildSharedSerializationForRPC_r(Parents[i].CmdStart, Parents[i].CmdEnd, Data, 0, 0, SharedInfoRPC);
			}
		}

		SharedInfoRPC.SetValid();
	}
}

void FRepLayout::ClearSharedSerializationForRPC()
{
	SharedInfoRPC.Reset();
	SharedInfoRPCParentsChanged.Reset();
}

void FRepLayout::SendPropertiesForRPC(
	UFunction* Function,
	UActorChannel* Channel,
	FNetBitWriter& Writer,
	const FConstRepObjectDataBuffer Data) const
{
	check(Function == Owner);

	if (!IsEmpty())
	{
		if (Channel->Connection->InternalAck)
		{
			TArray<uint16> Changed;

			for (int32 i = 0; i < Parents.Num(); i++)
			{
				if (!Parents[i].Property->Identical_InContainer(Data.Data, NULL, Parents[i].ArrayIndex))
				{
					BuildChangeList_r(BaseHandleToCmdIndex, Parents[i].CmdStart, Parents[i].CmdEnd, Data, 0, false, Changed);
				}
			}

			Changed.Add(0); // Null terminator

			SendProperties_BackwardsCompatible(nullptr, nullptr, Data, Channel->Connection, Writer, Changed);
		}
		else
		{
			for (int32 i = 0; i < Parents.Num(); i++)
			{
				bool Send = true;

				if (!Cast<UBoolProperty>(Parents[i].Property))
				{
					// Used cached comparison result if possible
					if ((GNetSharedSerializedData != 0) && SharedInfoRPC.IsValid() && !Parents[i].Property->HasAnyPropertyFlags(CPF_OutParm))
					{
						Send = SharedInfoRPCParentsChanged[i];
					}
					else
					{
						// check for a complete match, including arrays
						// (we're comparing against zero data here, since 
						// that's the default.)
						Send = !Parents[i].Property->Identical_InContainer(Data.Data, NULL, Parents[i].ArrayIndex);
					}

					Writer.WriteBit(Send ? 1 : 0);
				}

				if (Send)
				{
					bool bHasUnmapped = false;
					SerializeProperties_r(Writer, Writer.PackageMap, Parents[i].CmdStart, Parents[i].CmdEnd, const_cast<uint8*>(Data.Data), bHasUnmapped, 0, 0, SharedInfoRPC);
				}
			}
		}	
	}
}

void FRepLayout::ReceivePropertiesForRPC(
	UObject* Object,
	UFunction* Function,
	UActorChannel* Channel,
	FNetBitReader& Reader,
	FRepObjectDataBuffer Data,
	TSet<FNetworkGUID>&	UnmappedGuids) const
{
	check(Function == Owner);

	if (!IsEmpty())
	{
		for (int32 i = 0; i < Parents.Num(); i++)
		{
			if (Parents[i].ArrayIndex == 0 && !EnumHasAnyFlags(Parents[i].Flags, ERepParentFlags::IsZeroConstructible))
			{
				// If this property needs to be constructed, make sure we do that
				Parents[i].Property->InitializeValue(Data + Parents[i]);
			}
		}

		if (Channel->Connection->InternalAck)
		{
			bool bHasUnmapped = false;
			bool bGuidsChanged = false;

			// Let package map know we want to track and know about any guids that are unmapped during the serialize call
			// We have to do this manually since we aren't passing in any unmapped info
			Reader.PackageMap->ResetTrackedGuids(true);

			ReceiveProperties_BackwardsCompatible(Channel->Connection, nullptr, Data, Reader, bHasUnmapped, false, bGuidsChanged);

			if (Reader.PackageMap->GetTrackedUnmappedGuids().Num() > 0)
			{
				bHasUnmapped = true;
				UnmappedGuids = Reader.PackageMap->GetTrackedUnmappedGuids();
			}

			Reader.PackageMap->ResetTrackedGuids(false);

			if (bHasUnmapped)
			{
				UE_LOG(LogRepTraffic, Log, TEXT("Unable to resolve RPC parameter to do being unmapped. Object[%d] %s. Function %s."),
					Channel->ChIndex, *Object->GetName(), *Function->GetName());
			}
		}
		else
		{
			Reader.PackageMap->ResetTrackedGuids(true);

			static FRepSerializationSharedInfo Empty;

			for (int32 i = 0; i < Parents.Num(); i++)
			{
				if (Cast<UBoolProperty>(Parents[i].Property) || Reader.ReadBit())
				{
					bool bHasUnmapped = false;

					SerializeProperties_r(Reader, Reader.PackageMap, Parents[i].CmdStart, Parents[i].CmdEnd, Data, bHasUnmapped, 0, 0, Empty);

					if (Reader.IsError())
					{
						return;
					}

					if (bHasUnmapped)
					{
						UE_LOG(LogRepTraffic, Log, TEXT("Unable to resolve RPC parameter. Object[%d] %s. Function %s. Parameter %s."),
							Channel->ChIndex, *Object->GetName(), *Function->GetName(), *Parents[i].Property->GetName());
					}
				}
			}

			if (Reader.PackageMap->GetTrackedUnmappedGuids().Num() > 0)
			{
				UnmappedGuids = Reader.PackageMap->GetTrackedUnmappedGuids();
			}

			Reader.PackageMap->ResetTrackedGuids(false);
		}
	}
}

void FRepLayout::SerializePropertiesForStruct(
	UStruct* Struct,
	FBitArchive& Ar,
	UPackageMap* Map,
	FRepObjectDataBuffer Data,
	bool& bHasUnmapped) const
{
	check(Struct == Owner);

	static FRepSerializationSharedInfo Empty;

	for (int32 i = 0; i < Parents.Num(); i++)
	{
		SerializeProperties_r(Ar, Map, Parents[i].CmdStart, Parents[i].CmdEnd, Data, bHasUnmapped, 0, 0, Empty);

		if (Ar.IsError())
		{
			return;
		}
	}
}

void FRepLayout::BuildHandleToCmdIndexTable_r(
	const int32 CmdStart,
	const int32 CmdEnd,
	TArray<FHandleToCmdIndex>& HandleToCmdIndex)
{
	for (int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; CmdIndex++)
	{
		const FRepLayoutCmd& Cmd = Cmds[CmdIndex];

		check(Cmd.Type != ERepLayoutCmdType::Return);

		const int32 Index = HandleToCmdIndex.Add(FHandleToCmdIndex(CmdIndex));

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			HandleToCmdIndex[Index].HandleToCmdIndex = TUniquePtr<TArray<FHandleToCmdIndex>>(new TArray<FHandleToCmdIndex>());

			TArray<FHandleToCmdIndex>& ArrayHandleToCmdIndex = *HandleToCmdIndex[Index].HandleToCmdIndex;

			BuildHandleToCmdIndexTable_r(CmdIndex + 1, Cmd.EndCmd - 1, ArrayHandleToCmdIndex);
			CmdIndex = Cmd.EndCmd - 1;		// The -1 to handle the ++ in the for loop
		}
	}
}

TStaticBitArray<COND_Max> FSendingRepState::BuildConditionMapFromRepFlags(const FReplicationFlags RepFlags)
{
	TStaticBitArray<COND_Max> ConditionMap;

	// Setup condition map
	const bool bIsInitial = RepFlags.bNetInitial ? true : false;
	const bool bIsOwner = RepFlags.bNetOwner ? true : false;
	const bool bIsSimulated = RepFlags.bNetSimulated ? true : false;
	const bool bIsPhysics = RepFlags.bRepPhysics ? true : false;
	const bool bIsReplay = RepFlags.bReplay ? true : false;

	ConditionMap[COND_None] = true;
	ConditionMap[COND_InitialOnly] = bIsInitial;

	ConditionMap[COND_OwnerOnly] = bIsOwner;
	ConditionMap[COND_SkipOwner] = !bIsOwner;

	ConditionMap[COND_SimulatedOnly] = bIsSimulated;
	ConditionMap[COND_SimulatedOnlyNoReplay] = bIsSimulated && !bIsReplay;
	ConditionMap[COND_AutonomousOnly] = !bIsSimulated;

	ConditionMap[COND_SimulatedOrPhysics] = bIsSimulated || bIsPhysics;
	ConditionMap[COND_SimulatedOrPhysicsNoReplay] = (bIsSimulated || bIsPhysics) && !bIsReplay;

	ConditionMap[COND_InitialOrOwner] = bIsInitial || bIsOwner;
	ConditionMap[COND_ReplayOrOwner] = bIsReplay || bIsOwner;
	ConditionMap[COND_ReplayOnly] = bIsReplay;
	ConditionMap[COND_SkipReplay] = !bIsReplay;

	ConditionMap[COND_Custom] = true;
	ConditionMap[COND_Never] = false;

	return ConditionMap;
}

void FRepLayout::RebuildConditionalProperties(
	FSendingRepState* RESTRICT RepState,
	const FReplicationFlags& RepFlags) const
{
	SCOPE_CYCLE_COUNTER(STAT_NetRebuildConditionalTime);
	
	TStaticBitArray<COND_Max> ConditionMap = FSendingRepState::BuildConditionMapFromRepFlags(RepFlags);
	for (auto It = TBitArray<>::FIterator(RepState->InactiveParents); It; ++It)
	{
		It.GetValue() = !ConditionMap[Parents[It.GetIndex()].Condition];
	}

	RepState->RepFlags = RepFlags;
}

void FRepLayout::InitChangedTracker(FRepChangedPropertyTracker* ChangedTracker) const
{
	ChangedTracker->Parents.SetNum(Parents.Num());

	for (int32 i = 0; i < Parents.Num(); i++)
	{
		ChangedTracker->Parents[i].IsConditional = ((Parents[i].Flags & ERepParentFlags::IsConditional) != ERepParentFlags::None) ? 1 : 0;
	}
}

FRepStateStaticBuffer FRepLayout::CreateShadowBuffer(const FConstRepObjectDataBuffer Source) const
{
	FRepStateStaticBuffer ShadowData(AsShared());

	if (!IsEmpty())
	{
		if (ShadowDataBufferSize == 0)
		{
			UE_LOG(LogRep, Error, TEXT("FRepLayout::InitShadowData: Invalid RepLayout: %s"), *GetPathNameSafe(Owner));
		}
		else
		{
			InitRepStateStaticBuffer(ShadowData, Source);
		}
	}

	return ShadowData;
}

TSharedPtr<FReplicationChangelistMgr> FRepLayout::CreateReplicationChangelistMgr(const UObject* InObject) const
{
	// ChangelistManager / ChangelistState will hold onto a unique pointer for this
	// so no need to worry about deleting it here.

	FCustomDeltaChangelistState* DeltaChangelistState = nullptr;
	if (LifetimeCustomPropertyState && !!LifetimeCustomPropertyState->GetNumFastArrayProperties())
	{
		DeltaChangelistState = new FCustomDeltaChangelistState(LifetimeCustomPropertyState->GetNumFastArrayProperties());
	}

	const uint8* ShadowStateSource = (const uint8*)InObject->GetArchetype();
	if (ShadowStateSource == nullptr)
	{
		UE_LOG(LogRep, Error, TEXT("FRepLayout::CreateReplicationChangelistMgr: Invalid object archetype, initializing shadow state to current object state: %s"), *GetFullNameSafe(InObject));
		ShadowStateSource = (const uint8*)InObject;
	}

	return MakeShareable(new FReplicationChangelistMgr(AsShared(), ShadowStateSource, DeltaChangelistState));
}

TUniquePtr<FRepState> FRepLayout::CreateRepState(
	const FConstRepObjectDataBuffer Source,
	TSharedPtr<FRepChangedPropertyTracker>& InRepChangedPropertyTracker,
	ECreateRepStateFlags CreateFlags) const
{
	// TODO: We could probably avoid allocating a RepState completely if we detect the RepLayout is empty.
	//		In that case, there won't be anything interesting to do anyway.
	//		This would require more sanity checks in code using RepStates though.

	TUniquePtr<FRepState> RepState(new FRepState());

	// If we have a changelist manager, that implies we're acting as a server.
	const bool bIsServer = InRepChangedPropertyTracker.IsValid();

	// In that case, we don't need to initialize the shadow data, as it
	// will be stored in the ChangelistManager for this object once for all connections.
	if (InRepChangedPropertyTracker.IsValid())
	{
		check(InRepChangedPropertyTracker->Parents.Num() == Parents.Num());

		RepState->SendingRepState.Reset(new FSendingRepState());
		RepState->SendingRepState->RepChangedPropertyTracker = InRepChangedPropertyTracker;

		// Start out the conditional props based on a default RepFlags struct
		// It will rebuild if it ever changes
		RebuildConditionalProperties(RepState->SendingRepState.Get(), FReplicationFlags());
		RepState->SendingRepState->InactiveParents.Init(false, Parents.Num());
	}
	
	if (!EnumHasAnyFlags(CreateFlags, ECreateRepStateFlags::SkipCreateReceivingState))
	{
		FRepStateStaticBuffer StaticBuffer(AsShared());

		// For server's, we don't need ShadowData as the ChangelistTracker / Manager will be used
		// instead.
		if (!bIsServer)
		{
			InitRepStateStaticBuffer(StaticBuffer, Source);
		}

		RepState->ReceivingRepState.Reset(new FReceivingRepState(MoveTemp(StaticBuffer)));
	}

	return RepState;
}

void FRepLayout::InitRepStateStaticBuffer(FRepStateStaticBuffer& ShadowData, const FConstRepObjectDataBuffer Source) const
{
	check(ShadowData.Buffer.Num() == 0);
	ShadowData.Buffer.SetNumZeroed(ShadowDataBufferSize);
	ConstructProperties(ShadowData);
	CopyProperties(ShadowData, Source);
}

void FRepLayout::ConstructProperties(FRepStateStaticBuffer& InShadowData) const
{
	FRepShadowDataBuffer ShadowData = InShadowData.GetData();

	// Construct all items
	for (const FRepParentCmd& Parent : Parents)
	{
		// Only construct the 0th element of static arrays (InitializeValue will handle the elements)
		if (Parent.ArrayIndex == 0)
		{
			check((Parent.ShadowOffset + Parent.Property->GetSize()) <= InShadowData.Num());
			Parent.Property->InitializeValue(ShadowData + Parent);
		}
	}
}

void FRepLayout::CopyProperties(FRepStateStaticBuffer& InShadowData, const FConstRepObjectDataBuffer Source) const
{
	FRepShadowDataBuffer ShadowData = InShadowData.GetData();

	// Init all items
	for (const FRepParentCmd& Parent : Parents)
	{
		// Only copy the 0th element of static arrays (CopyCompleteValue will handle the elements)
		if (Parent.ArrayIndex == 0)
		{
			check((Parent.ShadowOffset + Parent.Property->GetSize()) <= InShadowData.Num());
			Parent.Property->CopyCompleteValue(ShadowData + Parent, Source + Parent);
		}
	}
}

void FRepLayout::DestructProperties(FRepStateStaticBuffer& InShadowData) const
{
	FRepShadowDataBuffer ShadowData = InShadowData.GetData();

	// Destruct all items
	for (const FRepParentCmd& Parent : Parents)
	{
		// Only destroy the 0th element of static arrays (DestroyValue will handle the elements)
		if (Parent.ArrayIndex == 0)
		{
			check((Parent.ShadowOffset + Parent.Property->GetSize()) <= InShadowData.Num());
			Parent.Property->DestroyValue(ShadowData + Parent);
		}
	}

	InShadowData.Buffer.Empty();
}

void FRepLayout::AddReferencedObjects(FReferenceCollector& Collector)
{
	UProperty* Current = nullptr;
	for (FRepParentCmd& Parent : Parents)
	{
		Current = Parent.Property;
		if (Current != nullptr)
		{
			Collector.AddReferencedObject(Current);

			// The only way this could happen is if a property was marked pending kill.
			// Technically, that could happen for a BP Property if its class is no longer needed,
			// but that should also clean up the FRepLayout.
			if (Current == nullptr)
			{
				UE_LOG(LogRep, Error, TEXT("Replicated Property is no longer valid: %s"),  *(Parent.CachedPropertyName.ToString()));
				Parent.Property = nullptr;
			}
		}
	}
}

FString FRepLayout::GetReferencerName() const
{
	return TEXT("FRepLayout");
}

// TODO: There's a better way to do this, but it requires more changes.
//			Ideally, we bring Retirements management, etc., into RepLayout.
//			What we could do instead of using standard changelists is use individual circular buffers 
//			for any given array element. Each time we see a new Fast Array Rep ID, we'd add a new element
//			to the buffer. Once all connections have acked that history, we would remove it from the buffer.
//			We could still enforce a hard limit to the size of the buffers.
//
//			We might technically be able to do that without bringing retirement stuff in,
//			but it would require us exposing the handles to NetSerialization.h (where fast TArrays live)
//			and that gets included all over the place, and it'd be better to not.


// It's important to note that unlike normal RepLayout properties which require changelists to know
// what changed and am accumulated "lifetime changelist" to catch up late joiners or people with bad
// connections, Fast Arrays don't need that.
//
// Instead, Fast Arrays track Rep IDs for each individual element, and for the array itself.
// Initially, all keys start as 0 and are incremented as the items are marked dirty.
// If we ever receive a NAK, we revert our IDs back to the last ACKed ID state.
//
// This means that at any given time, each connection knows what its current state is, and
// can just compare that against the "real state" stored on the Fast Array.
//
// In FastArrayDeltaSerialize (and the DeltaSerializeStructs variant), we use those IDs to determine
// what items actually need to be sent to a client.
//
// We also store the Changelist History ID alongside the Array / Element Rep IDs, and that is reset in the
// case of NAKs and starts at a designated invalid state initially.
//
// Changelists used here are purely accelerations. In ideal scenarios, we do property
// comparisons once per frame and store those changelists, just like normal rep layouts.
// Also like normal RepLayouts, that will be shared across all connections.
//
// When we go to replicate a Fast Array, we just accumulate the changelists for all of its
// dirty items, and then send those.
//
// On initial send, or if a given connection receives too many NAKs and falls outside of our History Range,
// we will devolve into sending the full state of the dirty items. At that point we're guaranteed that
// the connection is up to date (if it receives a NAK on that packet, its state will be reset back to
// being outside history range, and we try again).

void FRepLayout::PreSendCustomDeltaProperties(
	UObject* Object,
	UNetConnection* Connection,
	FReplicationChangelistMgr& ChangelistMgr,
	TArray<TSharedPtr<INetDeltaBaseState>>& CustomDeltaStates) const
{
	using namespace UE4_RepLayout_Private;

	if (!Connection->InternalAck)
	{
		const FLifetimeCustomDeltaState& LocalLifetimeCustomPropertyState = *LifetimeCustomPropertyState;

		if (LocalLifetimeCustomPropertyState.GetNumFastArrayProperties())
		{
			FRepChangelistState& ChangelistState = *ChangelistMgr.GetRepChangelistState();
			FCustomDeltaChangelistState& CustomDeltaChangelistState = *ChangelistState.CustomDeltaChangelistState;

			// Check to see whether or not we need to do comparisons this frame.
			// If we do, then run through our fast array states and generate new history items if needed.
			if (CustomDeltaChangelistState.CompareIndex != static_cast<uint32>(GFrameCounter))
			{
				const bool bIsInitial = (CustomDeltaChangelistState.CompareIndex == 0);
				CustomDeltaChangelistState.CompareIndex = GFrameCounter;

				const FConstRepObjectDataBuffer ObjectData(Object);
				const uint16 NumLifetimeCustomDeltaProperties = LocalLifetimeCustomPropertyState.GetNumCustomDeltaProperties();

				for (uint16 CustomDeltaIndex = 0; CustomDeltaIndex < NumLifetimeCustomDeltaProperties; ++CustomDeltaIndex)
				{
					const FLifetimeCustomDeltaProperty& CustomDeltaProperty = LocalLifetimeCustomPropertyState.GetCustomDeltaProperty(CustomDeltaIndex);
					const uint16 RepIndex = CustomDeltaProperty.PropertyRepIndex;

					// If our Fast Array Items Command is invalid, we can't do anything.
					// This should have been logged on RepLayout creation.
					if (INDEX_NONE != CustomDeltaProperty.FastArrayItemsCommand)
					{
						const int32 FastArrayNumber = CustomDeltaProperty.FastArrayNumber;
						const FRepParentCmd& FastArrayCmd = Parents[RepIndex];

						void const * const FastArraySerializer = ObjectData + FastArrayCmd;
						const EFastArraySerializerDeltaFlags DeltaFlags = CustomDeltaProperty.GetFastArrayDeltaFlags(FastArraySerializer);

						// Note, we can't rely on EFastArraySerializerDeltaFlags::HasBeenSerialized here.
						// It's possible we're calling PreSendCustomDeltaProperties **before** the first time the struct
						// was ever serialized, and in that case it would still be false, and prevent us from creating
						// a history the first time.
						//
						// This does mean that Fast Arrays requesting delta serialization will still have their history
						// incremented the first time, even if the feature is generally disabled.
						//
						// TODO: If any fast arrays failed this check, we could probably reset their state,
						//			because we know we should never try sending them again
						if (EnumHasAnyFlags(DeltaFlags, EFastArraySerializerDeltaFlags::IsUsingDeltaSerialization) ||
							(!EnumHasAnyFlags(DeltaFlags, EFastArraySerializerDeltaFlags::HasBeenSerialized) &&
								EnumHasAnyFlags(DeltaFlags, EFastArraySerializerDeltaFlags::HasDeltaBeenRequested)))
						{
							FDeltaArrayHistoryState& FastArrayHistoryState = CustomDeltaChangelistState.ArrayStates[FastArrayNumber];

							const int32 FastArrayReplicationKey = CustomDeltaProperty.GetFastArrayArrayReplicationKey(FastArraySerializer);
							if (FastArrayHistoryState.ArrayReplicationKey != FastArrayReplicationKey)
							{
								const uint32 HistoryDelta = FastArrayHistoryState.HistoryEnd - FastArrayHistoryState.HistoryStart;
								const uint32 CurrentHistoryIndex = FastArrayHistoryState.HistoryEnd % FDeltaArrayHistoryState::MAX_CHANGE_HISTORY;
								const FDeltaArrayHistoryItem& CurrentHistory = FastArrayHistoryState.ChangeHistory[CurrentHistoryIndex];

								// If we don't have any history items, go ahead and create one.
								// Otherwise, check to see if our current history was actually updated.
								// If it wasn't updated, that means that no one tried to replicate it last frame (which can be possible due
								// to rep conditions), and there's no sense in creating a new one.
								if (HistoryDelta == 0 || CurrentHistory.bWasUpdated)
								{
									// If we've reached our buffer size, then move our start history marker up.
									// In that case the old start history will become our new history.
									if (HistoryDelta >= FDeltaArrayHistoryState::MAX_CHANGE_HISTORY)
									{
										++FastArrayHistoryState.HistoryStart;
									}

									++FastArrayHistoryState.HistoryEnd;
									const uint32 NewHistory = FastArrayHistoryState.HistoryEnd % FDeltaArrayHistoryState::MAX_CHANGE_HISTORY;
									FastArrayHistoryState.ChangeHistory[NewHistory].Reset();
								}
							}
						}
					}
				}
			}
		}
	}
}

void FRepLayout::PostSendCustomDeltaProperties(
	UObject* Object,
	UNetConnection* Connection,
	FReplicationChangelistMgr& ChangelistMgr,
	TArray<TSharedPtr<INetDeltaBaseState>>& CustomDeltaStates) const
{
}

bool FRepLayout::DeltaSerializeFastArrayProperty(FFastArrayDeltaSerializeParams& Params, FReplicationChangelistMgr* ChangelistMgr) const
{
	using namespace UE4_RepLayout_Private;

	CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_RepLayout_DeltaSerializeFastArray, CVarNetEnableDetailedScopeCounters.GetValueOnAnyThread() > 0);

	// A portion of this work could be shared across all Fast Array Properties for a given object,
	// but that would be easier to do if the Custom Delta Serialization was completely encapsulated in FRepLayout.

	check(LifetimeCustomPropertyState);

	FNetDeltaSerializeInfo& DeltaSerializeInfo = Params.DeltaSerializeInfo;

	const FLifetimeCustomDeltaProperty& CustomDeltaProperty = LifetimeCustomPropertyState->GetCustomDeltaProperty(DeltaSerializeInfo.CustomDeltaIndex);
	const uint16 ParentIndex = CustomDeltaProperty.PropertyRepIndex;

	const FRepParentCmd& Parent = Parents[ParentIndex];
	const int32 CmdIndex = CustomDeltaProperty.FastArrayItemsCommand;

	if (INDEX_NONE == CmdIndex)
	{
		// This should have already been caught by InitFromClass.
		// So, log with a lower verbosity.
		UE_LOG(LogRep, Log, TEXT("FRepLayout::DeltaSerializeFastArrayProperty: Invalid fast array items command index! %s"), *Parent.CachedPropertyName.ToString())
		return false;
	}

	const FRepLayoutCmd& FastArrayItemCmd = Cmds[CmdIndex];
	const int32 ElementSize = FastArrayItemCmd.ElementSize;

	const int32 ItemLayoutStart = CmdIndex + 1;
	const int32 ItemLayoutEnd = FastArrayItemCmd.EndCmd - 1;

	UObject* Object = DeltaSerializeInfo.Object;
	UPackageMapClient* PackageMap = static_cast<UPackageMapClient*>(DeltaSerializeInfo.Map);
	UNetConnection* Connection = DeltaSerializeInfo.Connection;
	const bool bIsWriting = !!DeltaSerializeInfo.Writer;
	const bool bInternalAck = !!Connection->InternalAck;

	FRepObjectDataBuffer ObjectData(Object);
	FScriptArray* ObjectArray = GetTypedProperty<FScriptArray>(ObjectData, FastArrayItemCmd);
	FRepObjectDataBuffer ObjectArrayData(ObjectArray->GetData());

	FFastArraySerializer& ArraySerializer = Params.ArraySerializer;

	check(&ArraySerializer == &Params.ArraySerializer);

	const int32 ObjectArrayNum = ObjectArray->Num();
	FNetFieldExportGroup* NetFieldExportGroup = nullptr;

	if (bInternalAck)
	{
		// Note, PackageMap should hold onto the strong reference for us, so we use raw pointers where
		// we can.

		// TODO: This feels like something we could cache in PreSend, but we'd need to add plumbing to hold onto it.
		const FString OwnerPathName = Owner->GetPathName();
		TSharedPtr<FNetFieldExportGroup> LocalNetFieldExportGroup = PackageMap->GetNetFieldExportGroup(OwnerPathName);

		if (!LocalNetFieldExportGroup.IsValid())
		{
			if (!bIsWriting)
			{
				UE_LOG(LogRep, Error, TEXT("DeltaSerializeFastArrayProperty: Unable to find NetFieldExportGroup during replay playback. Class=%s, Property=%s"), *Owner->GetName(), *Parent.CachedPropertyName.ToString());
				return false;
			}

			UE_LOG(LogRepProperties, VeryVerbose, TEXT("DeltaSerializeFastArrayProperty: Create Netfield Export Group."))
			LocalNetFieldExportGroup = CreateNetfieldExportGroup();
			PackageMap->AddNetFieldExportGroup(OwnerPathName, LocalNetFieldExportGroup);
		}

		NetFieldExportGroup = LocalNetFieldExportGroup.Get();
	}

	if (bIsWriting)
	{
		FNetBitWriter& Writer = static_cast<FNetBitWriter&>(*DeltaSerializeInfo.Writer);
		auto& ChangedElements = *Params.WriteChangedElements;
		
		// This is a list of changelists to send, corresponding to items in ChangedElements.
		TArray<TArray<uint16>> Changelists;

		const FHandleToCmdIndex& ArrayHandleToCmd = BaseHandleToCmdIndex[FastArrayItemCmd.RelativeHandle - 1];
		const TArray<FHandleToCmdIndex>& ArrayHandleToCmdIndex = *ArrayHandleToCmd.HandleToCmdIndex;

		// We only need to create changelists if we're not in a demo.
		// Note, we may change this in the future to also delta structs for replays.
		if (!bInternalAck)
		{
			// First, we'll create our changelists.
			if (ChangelistMgr)
			{
				FRepChangelistState& RepChangelistState = *ChangelistMgr->GetRepChangelistState();
				FCustomDeltaChangelistState& DeltaChangelistState = *RepChangelistState.CustomDeltaChangelistState;

				const int32 FastArrayNumber = CustomDeltaProperty.FastArrayNumber;
				FDeltaArrayHistoryState& FastArrayState = DeltaChangelistState.ArrayStates[FastArrayNumber];


				// Params.WriteBaseState should be valid, and have the most up to date IDToChangelist map for the Fast Array.
				// However, it's ChangelistHistory will be to the last History Number sent to the Fast TArray on the specific
				// connection we're replicating from.
				TSharedRef<FNetFastTArrayBaseState> NewArrayDeltaState = StaticCastSharedRef<FNetFastTArrayBaseState>(Params.WriteBaseState->AsShared());

				// Cache off the newest history, the last history sent to this connection, and then update the state
				// to notify that we're going to send it the newest history.
				const uint32 NewChangelistHistory = FastArrayState.HistoryEnd;
				const uint32 LastSentHistory = NewArrayDeltaState->ChangelistHistory;
				const uint32 LastSentChangelistDelta = NewChangelistHistory - LastSentHistory;

				NewArrayDeltaState->ChangelistHistory = NewChangelistHistory;

				// Cache off the shadow array buffers.
				FRepShadowDataBuffer ShadowData(RepChangelistState.StaticBuffer.GetData());
				FScriptArray* ShadowArray = GetTypedProperty<FScriptArray>(ShadowData, FastArrayItemCmd);
				FRepShadowDataBuffer ShadowArrayData(ShadowArray->GetData());

				// Note, we explicitly pass in a 0 handles everywhere below.
				// This is because each item will be received individually, the receiving side indices won't
				// necessarily match our indices, and we already track the changelists separately.

				// Check to see whether or not we need to update the global changelist shared between connections.
				{
					const uint32 RelativeNewHistory = NewChangelistHistory % FDeltaArrayHistoryState::MAX_CHANGE_HISTORY;
					const uint32 CompareChangelistDelta = NewChangelistHistory - FastArrayState.HistoryStart;
					FDeltaArrayHistoryItem& HistoryItem = FastArrayState.ChangeHistory[RelativeNewHistory];

					if (!HistoryItem.bWasUpdated)
					{
						HistoryItem.bWasUpdated = true;

						FastArrayState.ArrayReplicationKey = NewArrayDeltaState->ArrayReplicationKey;

						// Update our shadow array, and reset our pointer in case we reallocated.
						FScriptArrayHelper ShadowArrayHelper((UArrayProperty*)FastArrayItemCmd.Property, ShadowArray);

						TBitArray<> ShadowArrayItemIsNew(false, ObjectArrayNum);
						const bool bIsInitial = CompareChangelistDelta == 1;

						// It's possible that elements have been deleted or otherwise reordered, and our shadow state is out of date.
						// In order to prevent issues, we'll shuffle our shadow state back to the correct order.
						// Note, we can't just do a lookup in both maps directly below, because we might end up stomping the same
						// shadow state multiple times.
						//
						//	Conceptually, you can imagine this process as having two lines of elements: the Object Line and the Shadow Line.
						//	The Object Line is always considered authoritative, and we just need to make sure the Shadow Line matches that.
						//
						//	If the Shadow Line is empty, the only thing we need to do is add a matching number of elements
						//	and patch their IDs to match those of the Object Line.
						//
						//	If the Object Line is empty, the only thing we need to do is remove all elements from the shadow line.
						//
						//	If both lines are non empty, it becomes a fairly straightforward fixup, following these rules.
						//	It's important to note, that at each step all previously seen elements are guaranteed to have been
						//	validated, and are generally considered "out of play" or no longer in the line, and because of
						//	that we should never touch them again.
						//
						//	The process stops when either we run out of Shadow Line or Object Line elements.
						//
						//		1. If Elements at the front of the line have matching IDs, there's nothing that needs to be done
						//			and we can move onto the next Element in both lines.
						//
						//		2. If Elements at the front of the line have mismatched IDs, then it's either because the Object Element
						//			was reordered, a previous Object Element was deleted, or a new Object Element was added.
						//
						//			a. If we find the Shadow Element in the Shadow Line, it must have been reordered.
						//				Go ahead and swap the current front of the Shadow Line with the found Shadow Element.
						//				Now, the elements at the front of the line have matching IDs, and we can move onto
						//				the next Element in both lines.
						//
						//			b. If we don't find the Shadow Element in the Shadow Line, the Object Element must be new.
						//				Go ahead and insert a new item into the Shadow array, fix up its ID, and mark it as new.
						//				Now, the elements at the front of the line have matching IDs, and we can move onto
						//				the next Element in both lines.
						//
						//		3. If there are elements remaining in both Lines, go back to step 1. Otherwise, continue to step 4.
						//
						//		4. At this point, there should be 3 possible outcomes:
						//
						//			a. The Object Line and the Shadow Line have the same number of Elements, and all are matching. We're done.
						//
						//			b. The Object Line has more elements than the Shadow Line. All missing elements from the Shadow Line
						//				must be new elements.
						//
						//			c. The Shadow Line has more elements than the Object Line. All missing elements from the Object Line
						//				must have been removed.

						// TODO: Optimize this. Luckily, it only happens once per frame, and only if the array is dirty.
						//			Maybe this could be merged into the sending code below, the only concern is that
						//			doesn't tracked deleted elements.
						//
						//			Alternatively, if Custom Delta code was merged into FRepLayout, we might be able to track
						//			lists of deleted items on a given frame and merge those together just like changelists.
						//			This would prevent us from needing to call BuildChangedAndDeletedBuffers on Fast TArrays
						//			for every connection, unless a specific connection was very out of date.

						// Note, this code serves a very similar purpose to FFastArraySerializer::TFastArraySerializeHelper<Type, SerializerType>::BuildChangedAndDeletedBuffers.
						// The main issue is that we can't rely on that method, because it will be comparing the last state that was replicated to a given particular connection,
						// and we want to compare the last state that was replicated to *any* connection.

						{
							FScriptArrayHelper ObjectArrayHelper((UArrayProperty*)FastArrayItemCmd.Property, ObjectArray);

							// We track this as a non-const, because if we append any items into the middle of the
							// array, they will be explicitly marked as new, and we still want to compare items
							// that existed in the array originally.
							int32 ShadowArrayNum = ShadowArrayHelper.Num();

							if (ObjectArrayNum != 0 && ShadowArrayNum != 0 && !bIsInitial)
							{
								const TMap<int32, int32> OldShadowIDToIndexMap(MoveTemp(FastArrayState.IDToIndexMap));
								FastArrayState.IDToIndexMap.Reserve(ObjectArrayNum);

								// We track the Appended Shadow Items, because any index we try and use after such
								// an append needs to be shifted appropriately.
								// TODO: We may be able to iterate the list backwards instead, but that may break
								//			some assumptions laid out in the algorithm above.
								int32 AppendedShadowItems = 0;

								UE_LOG(LogRepProperties, VeryVerbose, TEXT("DeltaSerializeFastArrayProperty: Fixup Shadow State. Owner=%s, Object=%s, Property=%s, bInitial=%d, ObjectArrayNum=%d, ShadowArrayNum=%d"),
									*Owner->GetName(), *Object->GetPathName(), *Parent.CachedPropertyName.ToString(), !!bIsInitial, ObjectArrayNum, ShadowArrayHelper.Num());

								for (int32 Index = 0; Index < ObjectArrayNum && Index < ShadowArrayNum; ++Index)
								{
									const int32 ObjectReplicationID = CustomDeltaProperty.GetFastArrayItemReplicationID(ObjectArrayHelper.GetRawPtr(Index));
									int32& ShadowReplicationID = CustomDeltaProperty.GetFastArrayItemReplicationIDMutable(ShadowArrayHelper.GetRawPtr(Index));

									FastArrayState.IDToIndexMap.Emplace(ObjectReplicationID, Index);

									UE_LOG(LogRepProperties, VeryVerbose, TEXT("DeltaSerializeFastArrayProperty: Handling Item. ID=%d, Index=%d, ShadowID=%d"), ObjectReplicationID, Index, ShadowReplicationID);

									// If our IDs match, there's nothing to do.
									if (ObjectReplicationID != ShadowReplicationID)
									{
										// The IDs didn't match, so this is an insert, delete, or swap.
										if (int32 const * const FoundShadowIndex = OldShadowIDToIndexMap.Find(ObjectReplicationID))
										{
											// We found the element in the shadow array, so there must have been a swap.
											// Sanity check that the invalid element can only possibly later in our lines.
											const int32 FixedShadowIndex = *FoundShadowIndex + AppendedShadowItems;

											UE_LOG(LogRepProperties, VeryVerbose, TEXT("DeltaSerializeFastArrayProperty: Swapped Shadow Item. OldIndex=%d, NewIndex=%d"), Index, FixedShadowIndex);

											check(FixedShadowIndex > Index);

											ShadowArrayHelper.SwapValues(Index, FixedShadowIndex);
										}
										else
										{
											// This item must have been inserted into the array (or appended and then shuffled in).
											// So, insert it into our shadow array and update its ID.

											ShadowArrayItemIsNew[Index] = true;
											ShadowArrayHelper.InsertValues(Index);

											int32& NewShadowReplicationID = CustomDeltaProperty.GetFastArrayItemReplicationIDMutable(ShadowArrayHelper.GetRawPtr(Index));
											NewShadowReplicationID = ObjectReplicationID;

											++AppendedShadowItems;
											++ShadowArrayNum;
											UE_LOG(LogRepProperties, VeryVerbose, TEXT("DeltaSerializeFastArrayProperty: Added Shadow Item. AppendedShadowItems=%d"), AppendedShadowItems);
										}
									}
								}
							}

							// Now we can go ahead and resize the array, to make any other changes we need.
							ShadowArrayHelper.Resize(ObjectArrayNum);
							ShadowArrayData = ShadowArray->GetData();

							// Go ahead and fix up IDs for any elements that may have just been appended.
							// Note, we need to do this for all elements on the initial pass.
							// Deleted elements will have been chopped off by the resize.
							if (bIsInitial || (ShadowArrayNum < ObjectArrayNum))
							{
								UE_CLOG(bIsInitial, LogRepProperties, VeryVerbose, TEXT("DeltaSerializeFastArrayProperty: Adding initial properties. Owner=%s, Object=%s, Property=%s, bInitial=%d, ObjectArrayNum=%d, ShadowArrayNum=%d"),
									*Owner->GetName(), *Object->GetPathName(), *Parent.CachedPropertyName.ToString(), !!bIsInitial, ObjectArrayNum, ShadowArrayHelper.Num());

								const int32 StartIndex = bIsInitial ? 0 : ShadowArrayNum;
								for (int32 Index = StartIndex; Index < ObjectArrayNum; ++Index)
								{
									const int32 ObjectReplicationID = CustomDeltaProperty.GetFastArrayItemReplicationID(ObjectArrayHelper.GetRawPtr(Index));
									int32& ShadowReplicationID = CustomDeltaProperty.GetFastArrayItemReplicationIDMutable(ShadowArrayHelper.GetRawPtr(Index));

									ShadowReplicationID = ObjectReplicationID;
									ShadowArrayItemIsNew[Index] = true;

									UE_LOG(LogRep, VeryVerbose, TEXT("DeltaSerializeFastArrayProperty: Added Shadow Item. Index=%d, ID=%d"), Index, ShadowReplicationID);
									FastArrayState.IDToIndexMap.Emplace(ObjectReplicationID, Index);
								}
							}
						}

						TArray<uint16> NewChangelist;
						for (auto& IDIndexPair : ChangedElements)
						{
							// Go ahead and do a property compare here, regardless of what we'll actually use below.
							// This is to prevent issues where someone with an initial / outdated connection doesn't properly
							// update the changelists in our history, but does update the shadow state inadvertently.
							FComparePropertiesSharedParams SharedParams{
								/*bIsInitial=*/ bIsInitial,
								/*bForceFail=*/ bIsInitial || ShadowArrayItemIsNew[IDIndexPair.Idx],
								RoleIndex,
								RemoteRoleIndex,
								Flags,
								Parents,
								Cmds
							};

							const int32 ArrayElementOffset = ElementSize * IDIndexPair.Idx;

							FConstRepObjectDataBuffer ElementData(ObjectArrayData + ArrayElementOffset);
							FRepShadowDataBuffer ElementShadowData(ShadowArrayData + ArrayElementOffset);
							NewChangelist.Empty(1);

							CompareProperties_r(SharedParams, ItemLayoutStart, ItemLayoutEnd, ElementShadowData, ElementData, NewChangelist, 0);

							if (NewChangelist.Num())
							{
								NewChangelist.Add(0);
								HistoryItem.ChangelistByID.Emplace(IDIndexPair.ID, MoveTemp(NewChangelist));

								// If our FastArraySerializerItems are NetSerialize, then their ID may be reset to INDEX_NONE due
								// to copying them into the shadow state (see FFastArraySerializerItem::operator=).
								// In that case, we need to make make sure we reset our ID so they can be found the next
								// time we try to replicate them.
								int32& ShadowReplicationID = CustomDeltaProperty.GetFastArrayItemReplicationIDMutable(ElementShadowData.Data);
								ShadowReplicationID = IDIndexPair.ID;
							}
						}
					}
				}

				// Now, merge all of the changelists we need together.
				// If we're sufficiently far back, or if this is our first transmission, then we'll just force fail
				// and send all changes (happens in the block below, Changelists.Num() == 0).

				// Note, this won't be all changes since the beginning, but just all changes for the currently dirty items.

				if (LastSentHistory != 0 && LastSentChangelistDelta > 0 && LastSentChangelistDelta < (FDeltaArrayHistoryState::MAX_CHANGE_HISTORY - 1))
				{
					const FConstRepObjectDataBuffer ConstObjectData(ObjectData);
					Changelists.SetNum(ChangedElements.Num());

					// Note, we iterate from LastSentHistory + 1, because we don't want to send something if
					// we think its already been sent.
					// Similarly, we do <= NewChangelistHistory because we need to send the newest history.
					for (uint32 ChangelistHistory = LastSentHistory + 1; ChangelistHistory <= NewChangelistHistory; ++ChangelistHistory)
					{
						const uint32 RelativeHistory = ChangelistHistory % FDeltaArrayHistoryState::MAX_CHANGE_HISTORY;
						FDeltaArrayHistoryItem& HistoryItem = FastArrayState.ChangeHistory[RelativeHistory];

						for (int32 i = 0; i < ChangedElements.Num(); ++i)
						{
							const auto& IDIndexPair = ChangedElements[i];
							if (const TArray<uint16>* FoundChangelist = HistoryItem.ChangelistByID.Find(IDIndexPair.ID))
							{
								if (FoundChangelist->Num() > 1)
								{
									// This is basically the DynamicArray case from MergeChangelists, but specialized.
									// We could probably just make that more generic.
									// Might also be worth creating a helper Lambda / Struct to create ChangelistIterators / HandleIterators.

									TArray<uint16>& ElementChangelist = Changelists[i];
									TArray<uint16> Temp = MoveTemp(ElementChangelist);
									ElementChangelist.Empty(1);

									const FConstRepObjectDataBuffer ElementData(ObjectArrayData + (IDIndexPair.Idx * ElementSize));

									FChangelistIterator FoundChangelistIterator(*FoundChangelist, 0);
									FRepHandleIterator FoundHandleIterator(Owner, FoundChangelistIterator, Cmds, ArrayHandleToCmdIndex, ElementSize, 1, ItemLayoutStart, ItemLayoutEnd);

									if (Temp.Num() == 0)
									{
										PruneChangeList_r(FoundHandleIterator, ElementData, ElementChangelist);
									}
									else
									{
										FChangelistIterator ElementChangelistIterator(Temp, 0);
										FRepHandleIterator ElementHandleIterator(Owner, ElementChangelistIterator, Cmds, ArrayHandleToCmdIndex, ElementSize, 1, ItemLayoutStart, ItemLayoutEnd);

										MergeChangeList_r(FoundHandleIterator, ElementHandleIterator, ElementData, ElementChangelist);
									}

									ElementChangelist.Add(0);
								}
							}
						}
					}
				}
			}

			if (Changelists.Num() == 0)
			{
				// If we didn't end up building changelists earlier for whatever reason, go ahead and just
				// build a full changelist for all changed elements.

				// This could have happened if we were sending initially, we were outside of history range, or
				// we didn't have a changelist manager.

				Changelists.SetNum(ChangedElements.Num());
				for (int32 i = 0; i < ChangedElements.Num(); ++i)
				{
					const auto& IDIndexPair = ChangedElements[i];
					const int32 ArrayElementOffset = ElementSize * IDIndexPair.Idx;

					FConstRepObjectDataBuffer ElementData(ObjectArrayData + ArrayElementOffset);

					TArray<uint16>& Changelist = Changelists[i];
					BuildChangeList_r(ArrayHandleToCmdIndex, ItemLayoutStart, ItemLayoutEnd, ElementData, 0, true, Changelist);

					if (Changelist.Num())
					{
						Changelist.Add(0);
					}
				}
			}
		}

		// Ignore tracking properties in Network Profiler below.
		// We will rely on the normal custom delta property tracking which happens elsewhere.
		NETWORK_PROFILER_IGNORE_PROPERTY_SCOPE

		// Now that we have our changelists setup, we can send the data.
		for (int32 i = 0; i < ChangedElements.Num(); ++i)
		{
			const auto& IDIndexPair = ChangedElements[i];
			uint32 ID = ChangedElements[i].ID;
			Writer << ID;

			const int32 ArrayElementOffset = ElementSize * IDIndexPair.Idx;
			FConstRepObjectDataBuffer ElementData(ObjectArrayData + ArrayElementOffset);

			if (bInternalAck)
			{
				SendAllProperties_BackwardsCompatible_r(
					nullptr,
					Writer,
					false,
					PackageMap,
					NetFieldExportGroup,
					ItemLayoutStart,
					ItemLayoutEnd,
					ElementData);
			}
			else
			{
				TArray<uint16>& Changelist = Changelists[i];
				const bool bAnythingToSend = Changelist.Num() > 1;
				Writer.WriteBit(!!bAnythingToSend);

				if (bAnythingToSend)
				{
					FChangelistIterator ChangelistIterator(Changelist, 0);
					FRepHandleIterator HandleIterator(
						Owner,
						ChangelistIterator,
						Cmds,
						ArrayHandleToCmdIndex,
						ElementSize,
						1,
						ItemLayoutStart,
						ItemLayoutEnd);

					SendProperties_r(
						/*RepState=*/ nullptr,
						Writer,
						/*bDoChecksum=*/ false,
						HandleIterator,
						ElementData,
						/*ArrayDepth=*/ 1,
						/*SharedInfo=*/ nullptr);

					WritePropertyHandle(Writer, 0, false);
				}
			}
			
		}

		return !Writer.IsError();
	}
	else
	{
		FNetBitReader& Reader = static_cast<FNetBitReader&>(*DeltaSerializeInfo.Reader);
		TArray<int32, TInlineAllocator<8>>& ChangedElements = *Params.ReadChangedElements;
		TArray<int32, TInlineAllocator<8>>& AddedElements = *Params.ReadAddedElements;

		FScriptArrayHelper FastArrayHelper((UArrayProperty*)FastArrayItemCmd.Property, ObjectArray);

		bool bOutGuidsChanged = false;
		bool bOutHasUnmapped = false;

		// WARNING! Don't attempt to use ObjectArrayData below, always rely on FastArrayHelper.
		// The helper may reallocate the array, and invalidate that pointer.

		for (int32 i = 0; i < Params.ReadNumChanged; ++i)
		{
			uint32 ID = 0;
			Reader << ID;

			int32* ElementIndexPtr = ArraySerializer.ItemMap.Find(ID);
			int32 ElementIndex = 0;
			void* ThisElement = nullptr;

			if (!ElementIndexPtr)
			{
				UE_LOG(LogNetFastTArray, Log, TEXT("   New. ID: %d. New Element!"), ID);

				ElementIndex = FastArrayHelper.AddValue();
				ArraySerializer.ItemMap.Add(ID, ElementIndex);
				AddedElements.Add(ElementIndex);
			}
			else
			{
				ElementIndex = *ElementIndexPtr;
				ChangedElements.Add(ElementIndex);

				UE_LOG(LogNetFastTArray, Log, TEXT("   Changed. ID: %d -> Idx: %d"), ID, ElementIndex);
			}

			ThisElement = FastArrayHelper.GetRawPtr(ElementIndex);

			Params.ReceivedItem(ThisElement, Params, ID);

			FGuidReferencesMap& GuidReferences = ArraySerializer.GuidReferencesMap_StructDelta.FindOrAdd(ID);

			if (bInternalAck)
			{
				const bool bSuccess = ReceiveProperties_BackwardsCompatible_r(
					nullptr,
					NetFieldExportGroup,
					Reader,
					ItemLayoutStart,
					ItemLayoutEnd,
					nullptr,
					ThisElement,
					ThisElement,
					&GuidReferences,
					bOutHasUnmapped,
					bOutGuidsChanged);

				if (!bSuccess)
				{
					UE_LOG(LogNetFastTArray, Warning, TEXT("FRepLayout::DeltaSerializeFastArrayProperty: Failed to receive backwards compat properties!"));
					return false;
				}
			}
			else
			{
				const bool bAnythingSent = !!Reader.ReadBit();
				if (!bAnythingSent)
				{
					continue;
				}

				FReceivePropertiesSharedParams SharedParams{
					/*bDoChecksum=*/ false,
					/*bSkipRoleSwap=*/ !EnumHasAnyFlags(Flags, ERepLayoutFlags::IsActor),
					/*RepNotifies=*/ nullptr,
					Reader,
					bOutHasUnmapped,
					bOutGuidsChanged,
					Parents,
					Cmds
				};

				FReceivePropertiesStackParams StackParams{
					ThisElement,
					nullptr,
					&GuidReferences,
					ItemLayoutStart,
					ItemLayoutEnd
				};

				// Read the first handle, and then start receiving properties.
				ReadPropertyHandle(SharedParams);
				if (ReceiveProperties_r(SharedParams, StackParams))
				{
					if (0 != SharedParams.ReadHandle)
					{
						UE_LOG(LogRep, Error, TEXT("ReceiveFastArrayItem: Invalid property terminator handle - Handle=%d"), SharedParams.ReadHandle);
						return false;
					}
				}
				else
				{
					UE_LOG(LogNetFastTArray, Warning, TEXT("FRepLayout::DeltaSerializeFastArrayProperty: Failed to received properties"));
					return false;
				}
			}

			if (Reader.IsError())
			{
				UE_LOG(LogNetFastTArray, Warning, TEXT("FRepLayout::DeltaSerializeFastArrayProperty: Reader.IsError() == true"));
				return false;
			}

			DeltaSerializeInfo.bGuidListsChanged |= bOutGuidsChanged;
			DeltaSerializeInfo.bOutHasMoreUnmapped |= bOutHasUnmapped;
		}

		return true;
	}
}

void FRepLayout::GatherGuidReferencesForFastArray(FFastArrayDeltaSerializeParams& Params) const
{
	using namespace UE4_RepLayout_Private;

	const FConstRepObjectDataBuffer ObjectData(Params.DeltaSerializeInfo.Object);
	const FLifetimeCustomDeltaProperty& CustomDeltaProperty = LifetimeCustomPropertyState->GetCustomDeltaProperty(Params.DeltaSerializeInfo.CustomDeltaIndex);
	const FRepParentCmd& Parent = Parents[CustomDeltaProperty.PropertyRepIndex];

	const FFastArraySerializer& ArraySerializer = Params.ArraySerializer;
	TSet<FNetworkGUID>& GatherGuids = *Params.DeltaSerializeInfo.GatherGuidReferences;
	int32 TrackedGuidMemory = 0;

	for (auto& GuidReferencesPair : ArraySerializer.GuidReferencesMap_StructDelta)
	{
		GatherGuidReferences_r(&GuidReferencesPair.Value, GatherGuids, TrackedGuidMemory);
	}

	if (Params.DeltaSerializeInfo.TrackedGuidMemoryBytes)
	{
		*Params.DeltaSerializeInfo.TrackedGuidMemoryBytes += TrackedGuidMemory;
	}
}

bool FRepLayout::MoveMappedObjectToUnmappedForFastArray(FFastArrayDeltaSerializeParams& Params) const
{
	using namespace UE4_RepLayout_Private;

	const FRepObjectDataBuffer ObjectData(Params.DeltaSerializeInfo.Object);
	const FLifetimeCustomDeltaProperty& CustomDeltaProperty = LifetimeCustomPropertyState->GetCustomDeltaProperty(Params.DeltaSerializeInfo.CustomDeltaIndex);
	const FRepParentCmd& Parent = Parents[CustomDeltaProperty.PropertyRepIndex];

	FFastArraySerializer& ArraySerializer = Params.ArraySerializer;
	const FNetworkGUID& MoveToUnmapped = *Params.DeltaSerializeInfo.MoveGuidToUnmapped;

	bool bFound = false;
	for (auto& GuidReferencesPair : ArraySerializer.GuidReferencesMap_StructDelta)
	{
		bFound |= MoveMappedObjectToUnmapped_r(&GuidReferencesPair.Value, MoveToUnmapped);
	}
	return bFound;
}

void FRepLayout::UpdateUnmappedGuidsForFastArray(FFastArrayDeltaSerializeParams& Params) const
 {
	using namespace UE4_RepLayout_Private;

	check(LifetimeCustomPropertyState);

	FNetDeltaSerializeInfo& DeltaSerializeInfo = Params.DeltaSerializeInfo;

	const FLifetimeCustomDeltaProperty& CustomDeltaProperty = LifetimeCustomPropertyState->GetCustomDeltaProperty(DeltaSerializeInfo.CustomDeltaIndex);
	const int32 ParentIndex = CustomDeltaProperty.PropertyRepIndex;
	const FRepParentCmd& Parent = Parents[ParentIndex];
	const int32 CmdIndex = CustomDeltaProperty.FastArrayItemsCommand;
	
	if (INDEX_NONE == CmdIndex)
	{
		// This should have already been caught by InitFromClass.
		// So, log with a lower verbosity.
		UE_LOG(LogRep, Log, TEXT("FRepLayout::UpdateUnmappedGuidsForFastArray: Invalid fast array items command index! %s"), *Parent.CachedPropertyName.ToString())
		return;
	}

	const FRepLayoutCmd& FastArrayItemCmd = Cmds[CmdIndex];
	const int32 ElementSize = FastArrayItemCmd.ElementSize;

	UObject* Object = DeltaSerializeInfo.Object;
	UPackageMap* PackageMap = DeltaSerializeInfo.Map;

	FRepObjectDataBuffer ObjectData(Object);
	FScriptArray* ScriptArray = GetTypedProperty<FScriptArray>(ObjectData, FastArrayItemCmd);
	FRepObjectDataBuffer ArrayData(ScriptArray->GetData());

	FFastArraySerializer& ArraySerializer = Params.ArraySerializer;

	for (auto It = ArraySerializer.GuidReferencesMap_StructDelta.CreateIterator(); It; ++It)
	{
		const int32 ElementID = It.Key();
		if (int32 const * const FoundItemIndex = ArraySerializer.ItemMap.Find(ElementID))
		{
			bool bOutSomeObjectsWereMapped = false;
			bool bOutHasMoreUnmapped = false;

			const int32 ItemIndex = *FoundItemIndex;
			const int32 ArrayElementOffset = ItemIndex * ElementSize;
			FRepObjectDataBuffer ElementData(ArrayData + ArrayElementOffset);

			UpdateUnmappedObjects_r(nullptr, &It.Value(), Object, PackageMap, nullptr, ElementData, ElementSize, Params.DeltaSerializeInfo.bCalledPreNetReceive, bOutSomeObjectsWereMapped, bOutHasMoreUnmapped);

			if (bOutSomeObjectsWereMapped)
			{
				Params.PostReplicatedChange(ElementData, Params);
			}

			DeltaSerializeInfo.bOutHasMoreUnmapped |= bOutHasMoreUnmapped;
			DeltaSerializeInfo.bOutSomeObjectsWereMapped |= bOutSomeObjectsWereMapped;
		}
		else
		{
			It.RemoveCurrent();
		}
	}
}

void FRepLayout::CountBytes(FArchive& Ar) const
{
	GRANULAR_NETWORK_MEMORY_TRACKING_INIT(Ar, "FRepLayout::CountBytes");
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("Parents", Parents.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("Cmds", Cmds.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("BaseHandleToCmdIndex", BaseHandleToCmdIndex.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("SharedInfoRPC", SharedInfoRPC.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("SharedInfoRPCParentsChanged", SharedInfoRPCParentsChanged.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("LifetimeCustomPropertyState",
		if (LifetimeCustomPropertyState)
		{
			Ar.CountBytes(sizeof(FLifetimeCustomDeltaState), sizeof(FLifetimeCustomDeltaState));
			LifetimeCustomPropertyState->CountBytes(Ar);
		}
	);
}

const uint16 FRepLayout::GetNumLifetimeCustomDeltaProperties() const
{
	return LifetimeCustomPropertyState.IsValid() ? LifetimeCustomPropertyState->GetNumCustomDeltaProperties() : 0;
}

UProperty* FRepLayout::GetLifetimeCustomDeltaProperty(const uint16 CustomDeltaPropertyIndex) const
{
	const FLifetimeCustomDeltaProperty& CustomDeltaProperty = LifetimeCustomPropertyState->GetCustomDeltaProperty(CustomDeltaPropertyIndex);
	return Parents[CustomDeltaProperty.PropertyRepIndex].Property;
}

const ELifetimeCondition FRepLayout::GetLifetimeCustomDeltaPropertyCondition(const uint16 CustomDeltaPropertyIndex) const
{
	const FLifetimeCustomDeltaProperty& CustomDeltaProperty = LifetimeCustomPropertyState->GetCustomDeltaProperty(CustomDeltaPropertyIndex);
	return Parents[CustomDeltaProperty.PropertyRepIndex].Condition;
}

void FReceivingRepState::CountBytes(FArchive& Ar) const
{
	GRANULAR_NETWORK_MEMORY_TRACKING_INIT(Ar, "FReceivingRepState::CountBytes");

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("StaticBuffer", StaticBuffer.CountBytes(Ar));

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("GuidReferencesMap",
		GuidReferencesMap.CountBytes(Ar);
		for (const auto& GuidRefPair : GuidReferencesMap)
		{
			GuidRefPair.Value.CountBytes(Ar);
		}
	);

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("RepNotifies", RepNotifies.CountBytes(Ar));

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("RepNotifyMetaData",
		RepNotifyMetaData.CountBytes(Ar);
		for (const auto& MetaDataPair : RepNotifyMetaData)
		{
			MetaDataPair.Value.CountBytes(Ar);
		}
	);
}

void FSendingRepState::CountBytes(FArchive& Ar) const
{
	// RepChangedPropertyTracker is also stored on the net driver, so it's not tracked here.
	GRANULAR_NETWORK_MEMORY_TRACKING_INIT(Ar, "FSendingRepState::CountBytes");

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("ChangeHistory",
		for (const FRepChangedHistory& HistoryItem : ChangeHistory)
		{
			HistoryItem.CountBytes(Ar);
		}
	);

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("PreOpenAckHistory",
		PreOpenAckHistory.CountBytes(Ar);
		for (const FRepChangedHistory& HistoryItem : PreOpenAckHistory)
		{
			HistoryItem.CountBytes(Ar);
		}
	);

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("LifetimeChangelist", LifetimeChangelist.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("InactiveChangelist", InactiveChangelist.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("InactiveParents", InactiveParents.CountBytes(Ar));

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("Retirement", Retirement.CountBytes(Ar));

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("RecentCustomDeltaState",
		RecentCustomDeltaState.CountBytes(Ar);
		for (const TSharedPtr<INetDeltaBaseState>& LocalRecentCustomDeltaState : RecentCustomDeltaState)
		{
			if (INetDeltaBaseState const * const BaseState = LocalRecentCustomDeltaState.Get())
			{
				BaseState->CountBytes(Ar);
			}
		}
	);

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("CDOCustomDeltaState",
		CDOCustomDeltaState.CountBytes(Ar);
		for (const TSharedPtr<INetDeltaBaseState>& LocalRecentCustomDeltaState : CDOCustomDeltaState)
		{
			if (INetDeltaBaseState const* const BaseState = LocalRecentCustomDeltaState.Get())
			{
				BaseState->CountBytes(Ar);
			}
		}
	);

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("CheckpointCustomDeltaState",
		CheckpointCustomDeltaState.CountBytes(Ar);
		for (const TSharedPtr<INetDeltaBaseState>& LocalRecentCustomDeltaState : CheckpointCustomDeltaState)
		{
			if (INetDeltaBaseState const* const BaseState = LocalRecentCustomDeltaState.Get())
			{
				BaseState->CountBytes(Ar);
			}
		}
	);
}

void FRepState::CountBytes(FArchive& Ar) const
{
	GRANULAR_NETWORK_MEMORY_TRACKING_INIT(Ar, "FRepState::CountBytes");

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("ReceivingRepState",
		if (FReceivingRepState const * const LocalReceivingRepState = ReceivingRepState.Get())
		{
			Ar.CountBytes(sizeof(*LocalReceivingRepState), sizeof(*LocalReceivingRepState));
			LocalReceivingRepState->CountBytes(Ar);
		}
	);	

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("SendingRepState",
		if (FSendingRepState const * const LocalSendingRepState = SendingRepState.Get())
		{
			Ar.CountBytes(sizeof(*LocalSendingRepState), sizeof(*LocalSendingRepState));
			LocalSendingRepState->CountBytes(Ar);
		}
	);	
}

FRepStateStaticBuffer::~FRepStateStaticBuffer()
{
	if (Buffer.Num() > 0)
	{
		RepLayout->DestructProperties(*this);
	}
}

#define REPDATATYPE_SPECIALIZATION(DstType, SrcType) \
template bool FRepLayout::DiffStableProperties(TArray<UProperty*>*, TArray<UObject*>*, TRepDataBuffer<DstType>, TConstRepDataBuffer<SrcType>) const; \
template bool FRepLayout::DiffProperties(TArray<UProperty*>*, TRepDataBuffer<DstType>, TConstRepDataBuffer<SrcType>, const EDiffPropertiesFlags) const;

REPDATATYPE_SPECIALIZATION(ERepDataBufferType::ObjectBuffer, ERepDataBufferType::ObjectBuffer)
REPDATATYPE_SPECIALIZATION(ERepDataBufferType::ObjectBuffer, ERepDataBufferType::ShadowBuffer)
REPDATATYPE_SPECIALIZATION(ERepDataBufferType::ShadowBuffer, ERepDataBufferType::ObjectBuffer)
REPDATATYPE_SPECIALIZATION(ERepDataBufferType::ShadowBuffer, ERepDataBufferType::ShadowBuffer)

#undef REPDATATYPE_SPECIALIZATION

#define REPDATATYPE_SPECIALIZATION(DataType) \
template void FRepLayout::ValidateWithChecksum(TConstRepDataBuffer<DataType> Data, FBitArchive& Ar) const;

REPDATATYPE_SPECIALIZATION(ERepDataBufferType::ShadowBuffer);
REPDATATYPE_SPECIALIZATION(ERepDataBufferType::ObjectBuffer);

#undef REPDATATYPE_SPECIALIZATION