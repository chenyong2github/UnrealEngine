// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsModule.h"
#include "PropertySelectionMap.h"
#include "Serialization/ICustomObjectSnapshotSerializer.h"
#include "Serialization/ObjectSnapshotSerializationData.h"
#include "SnapshotTestActor.h"
#include "SnapshotTestRunner.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

/**
 * Tests all interface functions are called at the correct time.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestoreActorCustomSubobject, "VirtualProduction.LevelSnapshots.Snapshot.CustomObjectSerialization.RestoreActorCustomSubobject", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FRestoreActorCustomSubobject::RunTest(const FString& Parameters)
{
	enum class EFunctionCall
	{
		OnTakeSnapshot,
		FindOrRecreateSubobjectInSnapshotWorld,
		FindOrRecreateSubobjectInEditorWorld,
		FindSubobjectInEditorWorld,
		OnPostSerializeSnapshotSubobject,
		OnPostSerializeEditorSubobject,
		PreApplySnapshotProperties,
		PostApplySnapshotProperties
	};
	
	class FStub : public ICustomObjectSnapshotSerializer
	{
		FRestoreActorCustomSubobject& Test;
	public:

		FStub(FRestoreActorCustomSubobject& Test)
			: Test(Test)
		{}
		
		virtual void OnTakeSnapshot(UObject* EditorObject, ICustomSnapshotSerializationData& DataStorage) override
		{
			CallOrder.Add(EFunctionCall::OnTakeSnapshot);
			
			Test.TestTrue(TEXT("Correct editor object passed in"), EditorObject == TestActor);
			const int32 Index = DataStorage.AddSubobjectSnapshot(TestActor->InstancedSubobject);
			DataStorage.GetSubobjectMetaData(Index)->WriteObjectAnnotation(FObjectAnnotator::CreateLambda([](FArchive& Archive)
			{
				int32 TestSubobjectInfo = 42;
				Archive << TestSubobjectInfo;
			}));

			DataStorage.WriteObjectAnnotation(FObjectAnnotator::CreateLambda([](FArchive& Archive)
			{
				int32 TestActorInfo = 21;
				Archive << TestActorInfo;
			}));
		}

		virtual UObject* FindOrRecreateSubobjectInSnapshotWorld(UObject* SnapshotObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			CallOrder.Add(EFunctionCall::FindOrRecreateSubobjectInSnapshotWorld);

			if (ASnapshotTestActor* SnapshotActor = Cast<ASnapshotTestActor>(SnapshotObject))
			{
				return SnapshotActor->InstancedSubobject;
			}
			
			Test.AddError(TEXT("Expected SnapshotObject to be an instance of ASnapshotTestActor"));
			return nullptr;
		}
		
		virtual UObject* FindOrRecreateSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			CallOrder.Add(EFunctionCall::FindOrRecreateSubobjectInEditorWorld);
			
			Test.TestTrue(TEXT("Correct editor object passed in"), EditorObject == TestActor);
			return TestActor->InstancedSubobject;	
		}
		
		virtual UObject* FindSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			CallOrder.Add(EFunctionCall::FindSubobjectInEditorWorld);
			
			Test.TestTrue(TEXT("Correct editor object passed in"), EditorObject == TestActor);

			int32 TestSubobjectInfo = 0;
			ObjectData.ReadObjectAnnotation(FObjectAnnotator::CreateLambda([&TestSubobjectInfo](FArchive& Archive)
			{
				Archive << TestSubobjectInfo;
			}));
			Test.TestEqual(TEXT("Saved custom subobject data is correct"), TestSubobjectInfo, 42);

			int32 TestActorInfo = 0;
			DataStorage.ReadObjectAnnotation(FObjectAnnotator::CreateLambda([&TestActorInfo](FArchive& Archive)
			{
				Archive << TestActorInfo;
			}));
			Test.TestEqual(TEXT("Saved custom actor data is correct"), TestActorInfo, 21);
			
			return TestActor->InstancedSubobject;	
		}

		
		virtual void OnPostSerializeSnapshotSubobject(UObject* Subobject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			CallOrder.Add(EFunctionCall::OnPostSerializeSnapshotSubobject);
		}
		
		virtual void OnPostSerializeEditorSubobject(UObject* Subobject, const ISnapshotSubobjectMetaData& ObjectData,const ICustomSnapshotSerializationData& DataStorage) override
		{
			CallOrder.Add(EFunctionCall::OnPostSerializeEditorSubobject);
		}
		
		virtual void PreApplySnapshotProperties(UObject* OriginalObject, const ICustomSnapshotSerializationData& DataStorage) override
		{
			CallOrder.Add(EFunctionCall::PreApplySnapshotProperties);
		}
		virtual void PostApplySnapshotProperties(UObject* OriginalObject, const ICustomSnapshotSerializationData& DataStorage) override
		{
			CallOrder.Add(EFunctionCall::PostApplySnapshotProperties);
		}

		TArray<EFunctionCall> CallOrder;
		ASnapshotTestActor* TestActor;
	};

	// Handle registering and unregistering of custom serializer
	TSharedRef<FStub> Stub = MakeShared<FStub>(*this);
	FLevelSnapshotsModule& Module = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
	Module.RegisterCustomObjectSerializer(ASnapshotTestActor::StaticClass(), Stub);
	ON_SCOPE_EXIT
	{
		Module.UnregisterCustomObjectSerializer(ASnapshotTestActor::StaticClass());
	};
	
	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			Stub->TestActor = World->SpawnActor<ASnapshotTestActor>();

			Stub->TestActor->InstancedSubobject->FloatProperty = 42.f;
			Stub->TestActor->InstancedSubobject->IntProperty = 21;
			Stub->TestActor->IntProperty = 42;
			Stub->TestActor->TestComponent->IntProperty = 42;
		})
		.TakeSnapshot()
	
		.ModifyWorld([&](UWorld* World)
		{
			Stub->TestActor->InstancedSubobject->FloatProperty = 420.f;
			Stub->TestActor->InstancedSubobject->IntProperty = 210;
			Stub->TestActor->IntProperty = 420;
			Stub->TestActor->TestComponent->IntProperty = 420;
		})
		.ApplySnapshot()

		.RunTest([&]()
		{
			// Ignore FindObjectCounterparts... it does not matter
			const int32 TakeSnapshotIndex								= Stub->CallOrder.Find(EFunctionCall::OnTakeSnapshot);
			const int32 OnPostSerializeSnapshotSubobjectIndex			= Stub->CallOrder.Find(EFunctionCall::OnPostSerializeSnapshotSubobject);
			const int32 OnPostSerializeEditorSubobjectIndex				= Stub->CallOrder.Find(EFunctionCall::OnPostSerializeEditorSubobject);
			const int32 PreApplySnapshotPropertiesIndex					= Stub->CallOrder.Find(EFunctionCall::PreApplySnapshotProperties);
			const int32 PostApplySnapshotPropertiesIndex				= Stub->CallOrder.Find(EFunctionCall::PostApplySnapshotProperties);

			// Expected call order?
			TestTrue(TEXT("OnTakeSnapshot was called"), TakeSnapshotIndex != INDEX_NONE);
			TestTrue(TEXT("OnPostSerializeSnapshotSubobject was called"), OnPostSerializeSnapshotSubobjectIndex != INDEX_NONE);
			TestTrue(TEXT("OnPostSerializeEditorSubobject was called"), OnPostSerializeEditorSubobjectIndex != INDEX_NONE);
			TestTrue(TEXT("PreApplySnapshotProperties was called"), PreApplySnapshotPropertiesIndex != INDEX_NONE);
			TestTrue(TEXT("PostApplySnapshotProperties was called"), PostApplySnapshotPropertiesIndex != INDEX_NONE);
			TestTrue(TEXT("PreApplySnapshotProperties called before PostApplySnapshotPropertiesIndex"), PreApplySnapshotPropertiesIndex < OnPostSerializeEditorSubobjectIndex);

			// Custom subobject restored
			TestEqual(TEXT("FloatProperty restored"), Stub->TestActor->InstancedSubobject->FloatProperty, 42.f);
			TestEqual(TEXT("IntProperty restored"), Stub->TestActor->InstancedSubobject->IntProperty, 21);
			// Normal values are still restored
			TestEqual(TEXT("Actor property still restored"), Stub->TestActor->IntProperty, 42);
			TestEqual(TEXT("Component property still restored"), Stub->TestActor->TestComponent->IntProperty, 42);
		});
	
	return true;
}

/**
 * Tests that custom serialization works when an actor adds a subobject dependency to a subobject that also has a custom serializer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestoreNestedCustomSubobject, "VirtualProduction.LevelSnapshots.Snapshot.CustomObjectSerialization.RestoreNestedCustomSubobject", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FRestoreNestedCustomSubobject::RunTest(const FString& Parameters)
{
	class FActorSerializer : public ICustomObjectSnapshotSerializer
	{
	public:
		
		virtual void OnTakeSnapshot(UObject* EditorObject, ICustomSnapshotSerializationData& DataStorage) override
		{
			DataStorage.AddSubobjectSnapshot(TestActor->InstancedSubobject);
		}

		virtual UObject* FindOrRecreateSubobjectInSnapshotWorld(UObject* SnapshotObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			if (ASnapshotTestActor* SnapshotActor = Cast<ASnapshotTestActor>(SnapshotObject))
			{
				return SnapshotActor->InstancedSubobject;
			}

			checkNoEntry();
			return nullptr;
		}
		
		virtual UObject* FindOrRecreateSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			return TestActor->InstancedSubobject;	
		}
		
		virtual UObject* FindSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			return TestActor->InstancedSubobject;	
		}

		ASnapshotTestActor* TestActor;
	};
	
	class FSubobjectSerializer : public ICustomObjectSnapshotSerializer
	{
	public:
		
		virtual void OnTakeSnapshot(UObject* EditorObject, ICustomSnapshotSerializationData& DataStorage) override
		{
			DataStorage.AddSubobjectSnapshot(TestActor->InstancedSubobject->NestedChild);
		}

		virtual UObject* FindOrRecreateSubobjectInSnapshotWorld(UObject* SnapshotObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			if (USubobject* Subobject = Cast<USubobject>(SnapshotObject))
			{
				return Subobject->NestedChild;
			}

			checkNoEntry();
			return nullptr;
		}
		
		virtual UObject* FindOrRecreateSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			return TestActor->InstancedSubobject->NestedChild;	
		}
		
		virtual UObject* FindSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			return TestActor->InstancedSubobject->NestedChild;	
		}

		ASnapshotTestActor* TestActor;
	};
	
	// Handle registering and unregistering of custom serializer
	TSharedRef<FActorSerializer> ActorSerializer = MakeShared<FActorSerializer>();
	TSharedRef<FSubobjectSerializer> SubobjectSerializer = MakeShared<FSubobjectSerializer>();
	FLevelSnapshotsModule& Module = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
	Module.RegisterCustomObjectSerializer(ASnapshotTestActor::StaticClass(), ActorSerializer);
	Module.RegisterCustomObjectSerializer(USubobject::StaticClass(), SubobjectSerializer);
	ON_SCOPE_EXIT
	{
		Module.UnregisterCustomObjectSerializer(ASnapshotTestActor::StaticClass());
		Module.UnregisterCustomObjectSerializer(USubobject::StaticClass());
	};

	ASnapshotTestActor* TestActor = nullptr;

	// Change properties on subobject and it's subobject. Both restored.
	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			TestActor = World->SpawnActor<ASnapshotTestActor>();
			ActorSerializer->TestActor = TestActor;
			SubobjectSerializer->TestActor = TestActor;
			
			TestActor->InstancedSubobject->FloatProperty = 42.f;
			TestActor->InstancedSubobject->NestedChild->FloatProperty = 21.f;
		})
		.TakeSnapshot()
	
		.ModifyWorld([&](UWorld* World)
		{
			TestActor->InstancedSubobject->FloatProperty = 420.f;
			TestActor->InstancedSubobject->NestedChild->FloatProperty = 210.f;
		})
		.ApplySnapshot()

		.RunTest([&]()
		{
			TestEqual(TEXT("Subobject property restored"), TestActor->InstancedSubobject->FloatProperty, 42.f);
			TestEqual(TEXT("Nested subobject property restored"), TestActor->InstancedSubobject->NestedChild->FloatProperty, 21.f);
		});


	// Change properties on subobject's subobject only. Still restored.
	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			TestActor = World->SpawnActor<ASnapshotTestActor>();
			ActorSerializer->TestActor = TestActor;
			SubobjectSerializer->TestActor = TestActor;
			
			TestActor->InstancedSubobject->NestedChild->FloatProperty = 21.f;
		})
		.TakeSnapshot()
	
		.ModifyWorld([&](UWorld* World)
		{
			TestActor->InstancedSubobject->NestedChild->FloatProperty = 210.f;
		})
		.ApplySnapshot()

		.RunTest([&]()
		{
			TestEqual(TEXT("Nested subobject property restored when outer had no changed properties"), TestActor->InstancedSubobject->NestedChild->FloatProperty, 21.f);
		});
	
	return true;
}

/**
 * Tests that the following properties are not restored if no callbacks are registered for it:
 *
 * class ABlah
 * {
 *		UPROPERTY(Instanced)
 *		UObject* Object;
 *
 *		UPROPERTY()
 *		UObject* OtherObject;
 * };
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNonEditableObjectPropertyNotRestoredByDefault, "VirtualProduction.LevelSnapshots.Snapshot.CustomObjectSerialization.NonEditableObjectPropertyNotRestoredByDefault", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FNonEditableObjectPropertyNotRestoredByDefault::RunTest(const FString& Parameters)
{
	ASnapshotTestActor* ChangedPropertiesActor = nullptr;
	ASnapshotTestActor* NulledActor = nullptr;
	
	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			ChangedPropertiesActor = World->SpawnActor<ASnapshotTestActor>();
			NulledActor = World->SpawnActor<ASnapshotTestActor>();

			ChangedPropertiesActor->InstancedSubobject->FloatProperty = 42.f;
			ChangedPropertiesActor->NakedSubobject->FloatProperty = 21.f;
		})
		.TakeSnapshot()
	
		.ModifyWorld([&](UWorld* World)
		{
			ChangedPropertiesActor->InstancedSubobject->FloatProperty = 420.f;
			ChangedPropertiesActor->NakedSubobject->FloatProperty = 210.f;

			NulledActor->InstancedSubobject = nullptr;
			NulledActor->NakedSubobject = nullptr;
		})
		.ApplySnapshot()

		.RunTest([&]()
		{
			TestEqual(TEXT("Unsupported instanced property not restored"), ChangedPropertiesActor->InstancedSubobject->FloatProperty, 420.f);
			TestEqual(TEXT("Unsupported naked property not restored"), ChangedPropertiesActor->NakedSubobject->FloatProperty, 210.f);
			
			TestTrue(TEXT("Unsupported instanced property stays null"), NulledActor->InstancedSubobject == nullptr);
			TestTrue(TEXT("Unsupported naked property stays null"), NulledActor->NakedSubobject == nullptr);
		});

	return true;
}

/**
 * Checks that changed properties on custom restored subobjects are discovered
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilterForPropertiesOnSubobjects, "VirtualProduction.LevelSnapshots.Snapshot.CustomObjectSerialization.FilterForPropertiesOnSubobjects", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FFilterForPropertiesOnSubobjects::RunTest(const FString& Parameters)
{
	class FStub : public ICustomObjectSnapshotSerializer
	{
	public:
		
		virtual void OnTakeSnapshot(UObject* EditorObject, ICustomSnapshotSerializationData& DataStorage) override
		{
			DataStorage.AddSubobjectSnapshot(TestActor->InstancedSubobject);
		}

		virtual UObject* FindOrRecreateSubobjectInSnapshotWorld(UObject* SnapshotObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			if (ASnapshotTestActor* SnapshotActor = Cast<ASnapshotTestActor>(SnapshotObject))
			{
				return SnapshotActor->InstancedSubobject;
			}

			checkNoEntry();
			return nullptr;
		}
		
		virtual UObject* FindOrRecreateSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			return TestActor->InstancedSubobject;	
		}
		
		virtual UObject* FindSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override
		{
			return TestActor->InstancedSubobject;	
		}

		ASnapshotTestActor* TestActor;
	};
	
	// Handle registering and unregistering of custom serializer
	TSharedRef<FStub> Stub = MakeShared<FStub>();
	FLevelSnapshotsModule& Module = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
	Module.RegisterCustomObjectSerializer(ASnapshotTestActor::StaticClass(), Stub);
	ON_SCOPE_EXIT
	{
		Module.UnregisterCustomObjectSerializer(ASnapshotTestActor::StaticClass());
	};



	
	// Modify subobject. Actor unchanged. Actor is in selection map.
	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			Stub->TestActor = World->SpawnActor<ASnapshotTestActor>();
			
			Stub->TestActor->InstancedSubobject->FloatProperty = 42.f;
		})
		.TakeSnapshot()
	
		.ModifyWorld([&](UWorld* World)
		{
			Stub->TestActor->InstancedSubobject->FloatProperty = 420.f;
		})

		.FilterProperties(Stub->TestActor, [&](const FPropertySelectionMap& PropertySelectionMap)
		{
			// Custom subobject properties
			UClass* SubobjectClass = USubobject::StaticClass();
			const FProperty* ChangedSubobjectProperty = SubobjectClass->FindPropertyByName(GET_MEMBER_NAME_CHECKED(USubobject, FloatProperty));

			const FPropertySelection* SelectedSubobjectProperties = PropertySelectionMap.GetSelectedProperties(Stub->TestActor->InstancedSubobject);
			const bool bSubobjectHasExpectedNumChangedProperties = SelectedSubobjectProperties && SelectedSubobjectProperties->GetSelectedLeafProperties().Num() == 1;
			TestTrue(TEXT("Subobject has changed properties"), bSubobjectHasExpectedNumChangedProperties);
			TestTrue(TEXT("Changed property on subobject contained"), bSubobjectHasExpectedNumChangedProperties && SelectedSubobjectProperties->IsPropertySelected(nullptr, ChangedSubobjectProperty));


			// Actor properties
			const FPropertySelection* SelectedActorProperties = PropertySelectionMap.GetSelectedProperties(Stub->TestActor);
			TestTrue(TEXT("Unchanged actor in selection map when subobject was changed"), SelectedActorProperties != nullptr);
		});





	
	// Modify nothing. No properties in selection map.
	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			Stub->TestActor = World->SpawnActor<ASnapshotTestActor>();
		})
		.TakeSnapshot()
		.FilterProperties(Stub->TestActor, [&](const FPropertySelectionMap& PropertySelectionMap)
		{
			// Custom subobject properties
			const FPropertySelection* SelectedSubobjectProperties = PropertySelectionMap.GetSelectedProperties(Stub->TestActor->InstancedSubobject);
			TestTrue(TEXT("Unchanged subobject not in selection map"), SelectedSubobjectProperties == nullptr);


			// Actor properties
			const FPropertySelection* SelectedActorProperties = PropertySelectionMap.GetSelectedProperties(Stub->TestActor);
			TestTrue(TEXT("Unchanged actor not in selection map when subobject was not changed"), SelectedActorProperties == nullptr);
		});






	
	// Make sure normal properties show up too:
	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			Stub->TestActor = World->SpawnActor<ASnapshotTestActor>();
			
			Stub->TestActor->InstancedSubobject->FloatProperty = 42.f;
			Stub->TestActor->IntProperty = 42;
			Stub->TestActor->TestComponent->IntProperty = 42;
		})
		.TakeSnapshot()
	
		.ModifyWorld([&](UWorld* World)
		{
			Stub->TestActor->InstancedSubobject->FloatProperty = 420.f;
			Stub->TestActor->IntProperty = 420;
			Stub->TestActor->TestComponent->IntProperty = 420;
		})

		.FilterProperties(Stub->TestActor, [&](const FPropertySelectionMap& PropertySelectionMap)
		{
			// Custom subobject properties
			UClass* SubobjectClass = USubobject::StaticClass();
			const FProperty* ChangedSubobjectProperty = SubobjectClass->FindPropertyByName(GET_MEMBER_NAME_CHECKED(USubobject, FloatProperty));

			const FPropertySelection* SelectedSubobjectProperties = PropertySelectionMap.GetSelectedProperties(Stub->TestActor->InstancedSubobject);
			const bool bSubobjectHasExpectedNumChangedProperties = SelectedSubobjectProperties && SelectedSubobjectProperties->GetSelectedLeafProperties().Num() == 1;
			TestTrue(TEXT("Subobject has changed properties"), bSubobjectHasExpectedNumChangedProperties);
			TestTrue(TEXT("Changed property on subobject contained"), bSubobjectHasExpectedNumChangedProperties && SelectedSubobjectProperties->IsPropertySelected(nullptr, ChangedSubobjectProperty));


			// Actor properties
			UClass* TestActorClass = ASnapshotTestActor::StaticClass();
			const FProperty* ChangedActorProperty = TestActorClass->FindPropertyByName(GET_MEMBER_NAME_CHECKED(ASnapshotTestActor, IntProperty));
			
			const FPropertySelection* SelectedActorProperties = PropertySelectionMap.GetSelectedProperties(Stub->TestActor);
			const bool bActorHasExpectedNumChangedProperties = SelectedActorProperties && SelectedActorProperties->GetSelectedLeafProperties().Num();
			TestTrue(TEXT("Actor has changed properties"), bActorHasExpectedNumChangedProperties);
			TestTrue(TEXT("Changed property on actor contained"), bActorHasExpectedNumChangedProperties && SelectedActorProperties->IsPropertySelected(nullptr, ChangedActorProperty));


			// Normal subobject properties, e.g. component
			UClass* TestComponentClass = USnapshotTestComponent::StaticClass();
			const FProperty* ChangedComponentProperty = TestComponentClass->FindPropertyByName(GET_MEMBER_NAME_CHECKED(USnapshotTestComponent, IntProperty));
			
			const FPropertySelection* SelectedComponentProperties = PropertySelectionMap.GetSelectedProperties(Stub->TestActor->TestComponent);
			const bool bComponentHasExpectedNumChangedProperties = SelectedComponentProperties && SelectedComponentProperties->GetSelectedLeafProperties().Num();
			TestTrue(TEXT("Component has changed properties"), bComponentHasExpectedNumChangedProperties);
			TestTrue(TEXT("Changed property on component contained"), bComponentHasExpectedNumChangedProperties && SelectedComponentProperties->IsPropertySelected(nullptr, ChangedComponentProperty));
		});
	
	return true;
}