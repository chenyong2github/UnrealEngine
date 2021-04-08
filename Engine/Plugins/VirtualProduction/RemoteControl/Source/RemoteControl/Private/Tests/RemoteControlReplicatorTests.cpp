// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

// Test related headers
#include "Backends/CborStructSerializerBackend.h"
#include "Backends/JsonStructSerializerBackend.h"
#include "CborWriter.h"
#include "RemoteControlReplicatorTestData.h"
#include "RemoteControlPreset.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"

// Replicator related headers
#include "Backends/CborStructDeserializerBackend.h"
#include "Backends/JsonStructDeserializerBackend.h"
#include "IRemoteControlModule.h"
#include "IRemoteControlReplicator.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "UObject/FieldPath.h"

// Replicator test delegates
DECLARE_DELEGATE_OneParam(FOnSetObjectPropertyReplicationEvent, FRCSetObjectPropertiesReplication&);
DECLARE_DELEGATE_OneParam(FOnResetObjectPropertyReplicationEvent, FRCObjectReplication&);

/**
 * Replicator interface implementation
 */
class FRemoteControlReplicatorCluster : public IRemoteControlReplicator
{
public:
	FRemoteControlReplicatorCluster(FOnSetObjectPropertyReplicationEvent InSetObjectPropertyReplicationEvent, FOnResetObjectPropertyReplicationEvent InResetObjectPropertyReplicationEvent)
		: SetObjectPropertyReplicationEvent(InSetObjectPropertyReplicationEvent)
		, ResetObjectPropertyReplicationEvent(InResetObjectPropertyReplicationEvent)
	{}

	virtual ERemoteControlReplicatorFlag InterceptSetObjectProperties(FRCSetObjectPropertiesReplication& InReplicatorReference) override
	{
		// Execure replication delegate
		SetObjectPropertyReplicationEvent.ExecuteIfBound(InReplicatorReference);
		return ReplicatorFlag;
	}

	virtual ERemoteControlReplicatorFlag InterceptResetObjectProperties(FRCObjectReplication& InReplicatorReference) override
	{
		// Execure replication delegate
		ResetObjectPropertyReplicationEvent.ExecuteIfBound(InReplicatorReference);
		return ReplicatorFlag;
	}

	virtual FName GetName() const override
	{
		return ClusterName;
	}

public:
	static auto constexpr ClusterName = TEXT("ClusterReplicator");

private:
	FOnSetObjectPropertyReplicationEvent SetObjectPropertyReplicationEvent;
	FOnResetObjectPropertyReplicationEvent ResetObjectPropertyReplicationEvent;
	static const ERemoteControlReplicatorFlag ReplicatorFlag = RCRF_Intercept;
};

struct FRemoteControlReplicatorTest 
{
	FRemoteControlReplicatorTest() = default;

	FRemoteControlReplicatorTest(FAutomationTestBase* InTest)
		: Test(InTest)
	{
		// Create Preset and Test object
		Preset = TStrongObjectPtr<URemoteControlPreset>(NewObject<URemoteControlPreset>());
		RemoteControlReplicatorTestObject = TStrongObjectPtr<URemoteControlReplicatorTestObject>{ NewObject<URemoteControlReplicatorTestObject>() };

		// Expose property
		CustomStructProperty = FindFProperty<FProperty>(URemoteControlReplicatorTestObject::StaticClass(), GET_MEMBER_NAME_CHECKED(URemoteControlReplicatorTestObject, CustomStruct));
		Int32ValueProperty = FindFProperty<FProperty>(FRemoteControlReplicatorCustomStruct::StaticStruct(), GET_MEMBER_NAME_CHECKED(FRemoteControlReplicatorCustomStruct, Int32Value));
		FString Int32ValuePropertyFullPath = FString::Printf(TEXT("%s.%s"), *CustomStructProperty->GetName(), *Int32ValueProperty->GetName());
		Int32ValuePropertyRCProp = Preset->ExposeProperty(
			RemoteControlReplicatorTestObject.Get(), 
			FRCFieldPathInfo{ Int32ValuePropertyFullPath }).Pin();

		// Reset tested value
		RemoteControlReplicatorTestObject->CustomStruct.Int32Value = FRemoteControlReplicatorCustomStruct::Int32ValueDefault;

		// Set up the replicator with Set and Reset object delegates
		IRemoteControlModule::Get().RegisterReplicator(
			MakeShared<FRemoteControlReplicatorCluster>(
				FOnSetObjectPropertyReplicationEvent::CreateRaw(this, &FRemoteControlReplicatorTest::OnSetObjectPropertyReplicationEvent),
				FOnResetObjectPropertyReplicationEvent::CreateRaw(this, &FRemoteControlReplicatorTest::OnResetObjectPropertyReplicationEvent))
		);
	}

	~FRemoteControlReplicatorTest()
	{
		IRemoteControlModule::Get().UnregisterReplicator(FRemoteControlReplicatorCluster::ClusterName);
	}

	void TestCPPApi()
	{
		// Set testing Archives
		TArray<uint8> ApiBuffer;
		FMemoryWriter MemoryWriter(ApiBuffer);
		FMemoryReader MemoryReader(ApiBuffer);
		FCborWriter CborWriter(&MemoryWriter);

		// Simulate CPP api replication buffer
		CborWriter.WriteContainerStart(ECborCode::Map, -1/*Indefinite*/);
		CborWriter.WriteValue(Int32ValuePropertyRCProp->GetProperty()->GetName());
		RemoteControlReplicatorTestObject->CustomStruct.Int32Value = Int32ValueTest; // Set test value before serialization
		CborWriter.WriteValue((int64)RemoteControlReplicatorTestObject->CustomStruct.Int32Value);
		RemoteControlReplicatorTestObject->CustomStruct.Int32Value = FRemoteControlReplicatorCustomStruct::Int32ValueDefault; // Set default value after serialization
		CborWriter.WriteContainerEnd();

		// Set a deserializer backend
		FCborStructDeserializerBackend CborStructDeserializerBackend(MemoryReader);

		// Set object reference
		FRCObjectReference ObjectRef;
		ObjectRef.Property = Int32ValuePropertyRCProp->GetProperty();
		ObjectRef.Access = ERCAccess::WRITE_TRANSACTION_ACCESS;
		ObjectRef.PropertyPathInfo = Int32ValuePropertyRCProp->FieldPathInfo.ToString();

		for (UObject* Object : Int32ValuePropertyRCProp->GetBoundObjects())
		{
			IRemoteControlModule::Get().ResolveObjectProperty(ObjectRef.Access, Object, ObjectRef.PropertyPathInfo, ObjectRef);
			// Arhive should be intercepted inside SetObjectProperties
			IRemoteControlModule::Get().SetObjectProperties(ObjectRef, CborStructDeserializerBackend, ERCPayloadType::Cbor, ApiBuffer);
		}
	}

	void TestCbor()
	{
		// Set testing Archives
		TArray<uint8> CborBuffer;
		FMemoryReader Reader(CborBuffer);
		FMemoryWriter Writer(CborBuffer);

		// Set serializers
		FCborStructSerializerBackend SerializerBackend(Writer, EStructSerializerBackendFlags::Default);
		FCborStructDeserializerBackend DeserializerBackend(Reader);

		RemoteControlReplicatorTestObject->CustomStruct.Int32Value = Int32ValueTest; // Set test value before serialization
		FStructSerializer::SerializeElement(&RemoteControlReplicatorTestObject->CustomStruct, Int32ValueProperty, INDEX_NONE, SerializerBackend, FStructSerializerPolicies());
		RemoteControlReplicatorTestObject->CustomStruct.Int32Value = FRemoteControlReplicatorCustomStruct::Int32ValueDefault; // Set default value after serialization

		// Set object references
		FRCObjectReference ObjectRef;
		ObjectRef.Property = Int32ValuePropertyRCProp->GetProperty();
		ObjectRef.Access = ERCAccess::WRITE_TRANSACTION_ACCESS;
		ObjectRef.PropertyPathInfo = Int32ValuePropertyRCProp->FieldPathInfo.ToString();

		for (UObject* Object : Int32ValuePropertyRCProp->GetBoundObjects())
		{
			IRemoteControlModule::Get().ResolveObjectProperty(ObjectRef.Access, Object, ObjectRef.PropertyPathInfo, ObjectRef);
			// Arhive should be intercepted inside SetObjectProperties
			IRemoteControlModule::Get().SetObjectProperties(ObjectRef, DeserializerBackend, ERCPayloadType::Cbor, CborBuffer);
		}
	}

	void TestJson()
	{
		// Set testing Archives
		TArray<uint8> JsonBuffer;
		FMemoryReader Reader(JsonBuffer);
		FMemoryWriter Writer(JsonBuffer);

		// Set serializers
		FJsonStructSerializerBackend SerializerBackend(Writer, EStructSerializerBackendFlags::Default);
		FJsonStructDeserializerBackend DeserializerBackend(Reader);

		// Serialize with test value and change the to default value back
		RemoteControlReplicatorTestObject->CustomStruct.Int32Value = Int32ValueTest; // Set test value before serialization
		FStructSerializer::SerializeElement(&RemoteControlReplicatorTestObject->CustomStruct, Int32ValueProperty, INDEX_NONE, SerializerBackend, FStructSerializerPolicies());
		RemoteControlReplicatorTestObject->CustomStruct.Int32Value = FRemoteControlReplicatorCustomStruct::Int32ValueDefault; // Set default value after serialization

		// Set object reference
		FRCObjectReference ObjectRef;
		ObjectRef.Property = Int32ValuePropertyRCProp->GetProperty();
		ObjectRef.Access = ERCAccess::WRITE_TRANSACTION_ACCESS;
		ObjectRef.PropertyPathInfo = Int32ValuePropertyRCProp->FieldPathInfo.ToString();

		for (UObject* Object : Int32ValuePropertyRCProp->GetBoundObjects())
		{
			IRemoteControlModule::Get().ResolveObjectProperty(ObjectRef.Access, Object, ObjectRef.PropertyPathInfo, ObjectRef);
			// Arhive should be intercepted inside SetObjectProperties
			IRemoteControlModule::Get().SetObjectProperties(ObjectRef, DeserializerBackend, ERCPayloadType::Json, JsonBuffer);
		}
	}

	void ResetObjectProperties()
	{
		// Set reste value before reset object property
		RemoteControlReplicatorTestObject->CustomStruct.Int32Value = Int32ValueTest;

		// Set ovject reference
		FRCObjectReference ObjectRef;
		ObjectRef.Property = Int32ValuePropertyRCProp->GetProperty();
		ObjectRef.Access = ERCAccess::WRITE_TRANSACTION_ACCESS;
		ObjectRef.PropertyPathInfo = Int32ValuePropertyRCProp->FieldPathInfo.ToString();

		for (UObject* Object : Int32ValuePropertyRCProp->GetBoundObjects())
		{
			IRemoteControlModule::Get().ResolveObjectProperty(ObjectRef.Access, Object, ObjectRef.PropertyPathInfo, ObjectRef);
			// Reset property should be intercepted inside SetObjectProperties
			IRemoteControlModule::Get().ResetObjectProperties(ObjectRef, true);
		}

		Test->TestNotEqual(TEXT("The property value should be applyed only after replication"), Int32ValueTest, RemoteControlReplicatorTestObject->CustomStruct.Int32Value);
	}

	// Custor logic for SetObjectReplicator
	void OnSetObjectPropertyReplicationEvent(FRCSetObjectPropertiesReplication& InReplicatorReference)
	{
		Test->TestEqual(TEXT("The property value should not be applyed and should be forwarded to Replicator"), FRemoteControlReplicatorCustomStruct::Int32ValueDefault, RemoteControlReplicatorTestObject->CustomStruct.Int32Value);

		// Simulate sending cluster event with binary payload
		TArray<uint8> Buffer;
		FMemoryWriter MemoryWiter(Buffer);
		MemoryWiter << InReplicatorReference;

		// ->->->->->->-> TRANSPORT OVER NETWORK or THREADS ->->->->->->->

		// Simulate recieving cluster event with binary payload
		FMemoryReader MemoryReader(Buffer);
		TArray<uint8> Payload;
		FRCSetObjectPropertiesReplication ReplicatorReference(Payload);
		MemoryReader << ReplicatorReference;

		// Set object refernce 
		FRCObjectReference ObjectRef;
		ObjectRef.Property = TFieldPath<FProperty>(*ReplicatorReference.PropertyPath).Get();
		ObjectRef.Access = ReplicatorReference.Access;
		ObjectRef.PropertyPathInfo = FRCFieldPathInfo(ReplicatorReference.PropertyPathInfo);
		UObject* LoadedObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ReplicatorReference.ObjectPath, nullptr, LOAD_None, nullptr, true);
		check(ObjectRef.Property.IsValid());
		check(LoadedObject);

		IRemoteControlModule::Get().ResolveObjectProperty(ObjectRef.Access, LoadedObject, ObjectRef.PropertyPathInfo, ObjectRef);

		// Create a backend based on SerializationMethod
		FMemoryReader BackendMemoryReader(Payload);
		TSharedPtr<IStructDeserializerBackend> StructDeserializerBackend = nullptr;
		if (ReplicatorReference.PayloadType == ERCPayloadType::Cbor)
		{
			StructDeserializerBackend = MakeShared<FCborStructDeserializerBackend>(BackendMemoryReader);
		}
		else if (ReplicatorReference.PayloadType == ERCPayloadType::Json)
		{
			StructDeserializerBackend = MakeShared<FJsonStructDeserializerBackend>(BackendMemoryReader);
		}
		check(StructDeserializerBackend.IsValid());

		// Deserialize without replication
		IRemoteControlModule::Get().SetObjectProperties(ObjectRef, *StructDeserializerBackend);

		Test->TestEqual(TEXT("The property value should be applyed after replication"), Int32ValueTest, RemoteControlReplicatorTestObject->CustomStruct.Int32Value);
	}

	// Custor logic for ResetObjectReplicator
	void OnResetObjectPropertyReplicationEvent(FRCObjectReplication& InReplicatorReference)
	{
		Test->TestEqual(TEXT("The property value should not be reset and should be forwarded to Replicator"), Int32ValueTest, RemoteControlReplicatorTestObject->CustomStruct.Int32Value);

		// Simulate sending cluster event with binary payload
		TArray<uint8> Buffer;
		FMemoryWriter MemoryWiter(Buffer);
		MemoryWiter << InReplicatorReference;

		// Simulate recieving cluster event with binary payload
		FMemoryReader MemoryReader(Buffer);
		FRCObjectReplication ReplicatorReference;
		MemoryReader << ReplicatorReference;

		// Set object refernce 
		FRCObjectReference ObjectRef;
		ObjectRef.Property = TFieldPath<FProperty>(*ReplicatorReference.PropertyPath).Get();
		ObjectRef.Access = ReplicatorReference.Access;
		ObjectRef.PropertyPathInfo = FRCFieldPathInfo(ReplicatorReference.PropertyPathInfo);
		UObject* LoadedObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ReplicatorReference.ObjectPath, nullptr, LOAD_None, nullptr, true);
		check(ObjectRef.Property.IsValid());
		check(LoadedObject);

		IRemoteControlModule::Get().ResolveObjectProperty(ObjectRef.Access, LoadedObject, ObjectRef.PropertyPathInfo, ObjectRef);

		// Reset object property without replication
		IRemoteControlModule::Get().ResetObjectProperties(ObjectRef);

		Test->TestEqual(TEXT("The property value should be reset after replication"), FRemoteControlReplicatorCustomStruct::Int32ValueDefault, RemoteControlReplicatorTestObject->CustomStruct.Int32Value);
	}

private:
	FAutomationTestBase* Test;
	TStrongObjectPtr<URemoteControlPreset> Preset;
	TStrongObjectPtr<URemoteControlReplicatorTestObject> RemoteControlReplicatorTestObject;
	TSharedPtr<FRemoteControlProperty> Int32ValuePropertyRCProp;
	FProperty* CustomStructProperty = nullptr;
	FProperty* Int32ValueProperty = nullptr;
	const int32 Int32ValueTest = 34780;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteControlPresetReplicatorTest, "Plugin.RemoteControl.Replicator", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRemoteControlPresetReplicatorTest::RunTest(const FString& Parameters)
{	
	// Run tests
	FRemoteControlReplicatorTest RemoteControlReplicatorTest(this);
	RemoteControlReplicatorTest.TestCPPApi();
	RemoteControlReplicatorTest.TestCbor();
	RemoteControlReplicatorTest.TestJson();
	RemoteControlReplicatorTest.ResetObjectProperties();
	
	return true;
}

