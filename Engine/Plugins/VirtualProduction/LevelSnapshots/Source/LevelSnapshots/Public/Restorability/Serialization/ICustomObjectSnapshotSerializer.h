// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class ISnapshotSubobjectMetaData;
class ICustomSnapshotSerializationData;
class UObject;

struct FPropertySelectionMap;

/**
 * External modules can implement this interface to customise how specific classes are snapshot and restored.
 * Implementations of this interface can be registered with the Level Snapshots module.
 * 
 * Typically one instance handles on type of class.
 * 
 * ISnapshotObjectSerializer handles the serialisation of the object you're registered to. You can use it to add custom
 * annotation data you need to restoring object info. You can also save & restore subobjects you wish to manually restore.
 */
class LEVELSNAPSHOTS_API ICustomObjectSnapshotSerializer
{
public:

	/**
	 * Called when taking a snapshot of an object with the class this implementation is registered to.
	 *
	 * You can use DataStorage to add any data additional meta data needed and add subobjects you want to restore manually.
	 * Note that all uproperties will still be restored normally as with all other objects.
	 *
	 * @param EditorObject The object being snapshotted. Same as DataStorage->GetSerializedObject().
	 * @param DataStorage Use it for tracking additional data
	 */
	virtual void OnTakeSnapshot(UObject* EditorObject, ICustomSnapshotSerializationData& DataStorage) = 0;
	
    /**
     * Called when creating objects for the temporary snapshot world. This is called for every subobject added using ISnapshotObjectSerializer::AddSubobjectDependency.
     * 
     * This function must either find the subobject in SnapshotObject or recreate it. If the object is recreated, you must fix up any property references yourself.
     * After this function is called, properties will be serialized into this function's return value. After this, OnPostSerializeSnapshotSubobject is called.
     * 
     * This function may return null, in which case the subobject is ignored.
     */
	virtual UObject* FindOrRecreateSubobjectInSnapshotWorld(UObject* SnapshotObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) = 0;
	
	/**
	 * Called when applying into the editor world. This is called for every subobject added using ISnapshotObjectSerializer::AddSubobjectDependency.
	 * 
	 * This function must either find the subobject in EditorObject or recreate it. If the object is recreated, you must fix up any property references yourself.
	 * After this function is called, properties will be serialized into this function's return value. After this, OnPostSerializeEditorSubobject is called.
	 * 
	 * This function may return null, in which case the subobject is ignored.
	 */
	virtual UObject* FindOrRecreateSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) = 0;

	/**
	 * Similar to FindOrRecreateSubobjectInEditorWorld, only that the subobject is not recreated if not present. Called when diffing against the world.
	 */
	virtual UObject* FindSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) = 0;
	
	/** Optional. Called after GetOrRecreateSubobjectInSnapshotWorld when all properties have been serialized into the subobject. You can do any post processing here. */
	virtual void OnPostSerializeSnapshotSubobject(UObject* Subobject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) {}

	/** Optional. Called after GetOrRecreateSubobjectInEditorWorld when all properties have been serialized into the subobject. You can do any post processing here. */
	virtual void OnPostSerializeEditorSubobject(UObject* Subobject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) {}
	
	/** Optional. Called before properties are applied to the object. */
	virtual void PreApplySnapshotProperties(UObject* EditorObject, const ICustomSnapshotSerializationData& DataStorage) {}
	
	/** Optional. Called after properties are applied to the object. */
	virtual void PostApplySnapshotProperties(UObject* EditorObject, const ICustomSnapshotSerializationData& DataStorage) {}
	
	virtual ~ICustomObjectSnapshotSerializer() = default;
};