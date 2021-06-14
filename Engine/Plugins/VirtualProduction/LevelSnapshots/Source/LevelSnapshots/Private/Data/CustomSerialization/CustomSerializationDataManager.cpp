// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomSerializationDataManager.h"

#include "CustomObjectSerializationWrapper.h"
#include "LevelSnapshotsLog.h"
#include "TakeWorldObjectSnapshotArchive.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

class FSnapshotSubobjectMetaDataManager
	:
	public ISnapshotSubobjectMetaData,
	public TSharedFromThis<FSnapshotSubobjectMetaDataManager>
{
public:
	
	const FSoftObjectPath CachedSubobjectPath;
	const int32 OwningDataSubobjectIndex;
	const FWorldSnapshotData& WorldData;
	
	const FCustomSerializationDataGetter_ReadOnly SerializationDataGetter_ReadOnly;
	const FCustomSerializationDataGetter_ReadWrite SerializationDataGetter_ReadWrite;

	FSnapshotSubobjectMetaDataManager(
		FSoftObjectPath CachedSubobjectPath,
		int32 SubobjectIndex,
		const FWorldSnapshotData& WorldData,
		const FCustomSerializationDataGetter_ReadOnly& SerializationDataGetter_ReadOnly,
		FCustomSerializationDataGetter_ReadWrite SerializationDataGetter_ReadWrite)
		:
		CachedSubobjectPath(CachedSubobjectPath),
		OwningDataSubobjectIndex(SubobjectIndex),
		WorldData(WorldData),
		SerializationDataGetter_ReadOnly(SerializationDataGetter_ReadOnly),
		SerializationDataGetter_ReadWrite(SerializationDataGetter_ReadWrite)
	{}

	virtual FSoftObjectPath GetOriginalPath() const override
	{
		return CachedSubobjectPath;
	}
	
	virtual void WriteObjectAnnotation(const FObjectAnnotator& Writer) override
	{
		if (Writer.IsBound() && ensure(SerializationDataGetter_ReadWrite.IsBound()))
		{
			FMemoryWriter MemoryWriter(SerializationDataGetter_ReadWrite.Execute()->Subobjects[OwningDataSubobjectIndex].SubobjectAnnotationData, true);
			
			Writer.Execute(MemoryWriter);
		}
	}
	
	virtual void ReadObjectAnnotation(const FObjectAnnotator& Reader) const override
	{
		if (Reader.IsBound())
		{
			FMemoryReader MemoryReader(SerializationDataGetter_ReadOnly.Execute()->Subobjects[OwningDataSubobjectIndex].SubobjectAnnotationData, true);
			WorldData.GetSnapshotVersionInfo().ApplyToArchive(MemoryReader);
			Reader.Execute(MemoryReader);
		}
	}
};

FCustomSerializationDataReader::FCustomSerializationDataReader(FCustomSerializationDataGetter_ReadOnly SerializationDataGetter, const FWorldSnapshotData& ConstWorldData)
		:
		SerializationDataGetter_ReadOnly(SerializationDataGetter),
		WorldData_ReadOnly(ConstWorldData)
{
	if (SerializationDataGetter.IsBound())
	{
		const FCustomSerializationData* SerializationData = SerializationDataGetter_ReadOnly.Execute();
		for (int32 i = 0; i < SerializationData->Subobjects.Num(); ++i)
		{
			const FCustomSubbjectSerializationData& SubobjectData = SerializationData->Subobjects[i];
			CachedSubobjectMetaData.Add(
				MakeShared<FSnapshotSubobjectMetaDataManager>(
					ConstWorldData.SerializedObjectReferences[SubobjectData.ObjectPathIndex],
						i,
						WorldData_ReadOnly,
						SerializationDataGetter_ReadOnly,
						FCustomSerializationDataGetter_ReadWrite()
					)
				);
		}
	}
}

void FCustomSerializationDataReader::WriteObjectAnnotation(const FObjectAnnotator& Writer)
{
	checkNoEntry();
}

void FCustomSerializationDataReader::ReadObjectAnnotation(const FObjectAnnotator& Reader) const
{
	if (Reader.IsBound())
	{
		FMemoryReader MemoryReader(SerializationDataGetter_ReadOnly.Execute()->RootAnnotationData, true);
		WorldData_ReadOnly.GetSnapshotVersionInfo().ApplyToArchive(MemoryReader);
		Reader.Execute(MemoryReader);
	}
}

int32 FCustomSerializationDataReader::AddSubobjectSnapshot(UObject* Subobject)
{
	return INDEX_NONE;	
}

TSharedPtr<ISnapshotSubobjectMetaData> FCustomSerializationDataReader::GetSubobjectMetaData(int32 Index)
{
	return CachedSubobjectMetaData[Index];
}

const TSharedPtr<ISnapshotSubobjectMetaData> FCustomSerializationDataReader::GetSubobjectMetaData(int32 Index) const
{
	return CachedSubobjectMetaData[Index];
}

int32 FCustomSerializationDataReader::GetNumSubobjects() const
{
	return SerializationDataGetter_ReadOnly.Execute()->Subobjects.Num();
}

FCustomSerializationDataWriter::FCustomSerializationDataWriter(FCustomSerializationDataGetter_ReadWrite SerializationDataGetter, FWorldSnapshotData& WorldData, UObject* SerializedObject)
	:
	FCustomSerializationDataReader(FCustomSerializationDataGetter_ReadOnly(), WorldData),
	SerializationDataGetter_ReadWrite(SerializationDataGetter),
	WorldData_ReadWrite(WorldData),
	SerializedObject(SerializedObject)
{
	SerializationDataGetter_ReadOnly = FCustomSerializationDataGetter_ReadOnly::CreateLambda([this]() -> const FCustomSerializationData* { return SerializationDataGetter_ReadWrite.Execute(); });
	
	const FCustomSerializationData* SerializationData = SerializationDataGetter_ReadOnly.Execute();
	for (int32 i = 0; i < SerializationData->Subobjects.Num(); ++i)
	{
		const FCustomSubbjectSerializationData& SubobjectData = SerializationData->Subobjects[i];
		CachedSubobjectMetaData.Add(
			MakeShared<FSnapshotSubobjectMetaDataManager>(
				WorldData.SerializedObjectReferences[SubobjectData.ObjectPathIndex],
					i,
					WorldData_ReadOnly,
					SerializationDataGetter_ReadOnly,
					FCustomSerializationDataGetter_ReadWrite()
				)
			);
	}
}

void FCustomSerializationDataWriter::WriteObjectAnnotation(const FObjectAnnotator& Writer)
{
	if (Writer.IsBound())
	{
		FMemoryWriter MemoryWriter(SerializationDataGetter_ReadWrite.Execute()->RootAnnotationData, true);
		Writer.Execute(MemoryWriter);
	}
}

int32 FCustomSerializationDataWriter::AddSubobjectSnapshot(UObject* Subobject)
{
	if (!ensure(Subobject && Subobject->IsIn(SerializedObject)))
	{
		return INDEX_NONE;
	}

	FCustomSerializationData* SerializationData = SerializationDataGetter_ReadWrite.Execute();
	// This may be slow because the array may have thousands of elements but AddSubobject shouldn't be called that often hopefully ...
	const int32 ObjectIndex = WorldData_ReadWrite.SerializedObjectReferences.Find(Subobject);
	const int32 ExistingSubobjectIndex = SerializationData->Subobjects.FindLastByPredicate([ObjectIndex](const FCustomSubbjectSerializationData& Data)
	{
		return Data.ObjectPathIndex == ObjectIndex;
	});

	if (ObjectIndex != INDEX_NONE)
	{
		UE_LOG(LogLevelSnapshots, Error, TEXT("You tried to register an object which was already found by standard Level Snapshot serialisation. Is your subobject referenced by an property with a CPF_Edit flag?"));
		UE_DEBUG_BREAK();
		return ExistingSubobjectIndex;
	}
	if (ExistingSubobjectIndex != INDEX_NONE)
	{
		UE_LOG(LogLevelSnapshots, Error, TEXT("You tried to register the same subobject twice."));
		UE_DEBUG_BREAK();
		return ExistingSubobjectIndex;
	}

	FCustomSubbjectSerializationData SubobjectData;
	SubobjectData.ObjectPathIndex = WorldData_ReadWrite.AddObjectDependency(Subobject);
	const int32 SubobjectIndex = SerializationData->Subobjects.Emplace(
		MoveTemp(SubobjectData) // Not profiled
		);
	// Optimisation: Serialize into the allocated SerializationData directly avoiding a possibly large copy of the serialized data
	FTakeWorldObjectSnapshotArchive Archive = FTakeWorldObjectSnapshotArchive::MakeArchiveForSavingWorldObject(SerializationData->Subobjects[SubobjectIndex], WorldData_ReadWrite, Subobject);
	Subobject->Serialize(Archive);
	
	CachedSubobjectMetaData.Add(MakeShared<FSnapshotSubobjectMetaDataManager>(Subobject, SubobjectIndex, WorldData_ReadWrite, SerializationDataGetter_ReadOnly, SerializationDataGetter_ReadWrite));

	FCustomObjectSerializationWrapper::TakeSnapshotForSubobject(Subobject, WorldData_ReadWrite);
	return SubobjectIndex;
}
