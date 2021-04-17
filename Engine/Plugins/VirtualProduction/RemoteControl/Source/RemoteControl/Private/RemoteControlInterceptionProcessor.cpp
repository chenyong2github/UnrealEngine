// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlInterceptionProcessor.h"

// Interception related headers
#include "IRemoteControlModule.h"
#include "RemoteControlInterceptionHelpers.h"
#include "Backends/CborStructDeserializerBackend.h"
#include "Backends/JsonStructDeserializerBackend.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "UObject/FieldPath.h"


void FRemoteControlInterceptionProcessor::SetObjectProperties(FRCIPropertiesMetadata& PropsMetadata)
{
	// Set object reference
	FRCObjectReference ObjectRef;
	ObjectRef.Property = TFieldPath<FProperty>(*PropsMetadata.PropertyPath).Get();
	ObjectRef.Access = ToInternal(PropsMetadata.Access);
	ObjectRef.PropertyPathInfo = FRCFieldPathInfo(PropsMetadata.PropertyPathInfo);
	UObject* LoadedObject = StaticLoadObject(UObject::StaticClass(), nullptr, *PropsMetadata.ObjectPath, nullptr, LOAD_None, nullptr, true);
	check(ObjectRef.Property.IsValid());
	check(LoadedObject);

	// Resolve object property
	const bool bResult = IRemoteControlModule::Get().ResolveObjectProperty(ObjectRef.Access, LoadedObject, ObjectRef.PropertyPathInfo, ObjectRef);
	if (bResult)
	{
		// Create a backend based on SerializationMethod
		FMemoryReader BackendMemoryReader(PropsMetadata.Payload);
		TSharedPtr<IStructDeserializerBackend> StructDeserializerBackend = nullptr;
		if (PropsMetadata.PayloadType == ERCIPayloadType::Cbor)
		{
			StructDeserializerBackend = MakeShared<FCborStructDeserializerBackend>(BackendMemoryReader);
		}
		else if (PropsMetadata.PayloadType == ERCIPayloadType::Json)
		{
			StructDeserializerBackend = MakeShared<FJsonStructDeserializerBackend>(BackendMemoryReader);
		}

		check(StructDeserializerBackend.IsValid());

		// Deserialize without replication
		IRemoteControlModule::Get().SetObjectProperties(ObjectRef, *StructDeserializerBackend);
	}
}

void FRemoteControlInterceptionProcessor::ResetObjectProperties(FRCIObjectMetadata& InObject)
{
	// Set object reference
	FRCObjectReference ObjectRef;
	ObjectRef.Property = TFieldPath<FProperty>(*InObject.PropertyPath).Get();
	ObjectRef.Access = ToInternal(InObject.Access);
	ObjectRef.PropertyPathInfo = FRCFieldPathInfo(InObject.PropertyPathInfo);
	UObject* LoadedObject = StaticLoadObject(UObject::StaticClass(), nullptr, *InObject.ObjectPath, nullptr, LOAD_None, nullptr, true);
	check(ObjectRef.Property.IsValid());
	check(LoadedObject);

	// Resolve object property
	const bool bResult = IRemoteControlModule::Get().ResolveObjectProperty(ObjectRef.Access, LoadedObject, ObjectRef.PropertyPathInfo, ObjectRef);
	if (bResult)
	{
		// Reset object property without replication
		IRemoteControlModule::Get().ResetObjectProperties(ObjectRef, false);
	}
}
