// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotTestRunner.h"
#include "SnapshotTestActor.h"

#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMeshActor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestoreSimpleProperties, "VirtualProduction.LevelSnapshots.Snapshot.RestoreSimpleProperties", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FRestoreSimpleProperties::RunTest(const FString& Parameters)
{
	const FVector StartLocation(100.f, -200.f, 300.f);
	const FVector EndLocation(500.f, -625.f, 750.f);
	const FRotator StartRotation(20.f, 30.f, -30.f);
	const FRotator EndRotation(50.f, -60.f, -40.f);
	const FVector StartScale(1.f, 1.f, 2.f);
	const FVector EndScale(2.f, 3.f, -2.f);

	const FPostProcessSettings StartSettings = []()
	{
		FPostProcessSettings Result;
		Result.bOverride_ColorSaturation = true;
		Result.ColorSaturation = FVector4(0.5f, 0.5f, 0.5f, 0.5f);
		Result.bOverride_ReflectionsType = true;
		Result.ReflectionsType = EReflectionsType::ScreenSpace;
		Result.bOverride_TranslucencyType = true;
		Result.TranslucencyType = ETranslucencyType::Raster;
		return Result;
	}();
	const FPostProcessSettings EndSettings = []()
	{
		FPostProcessSettings Result;
		Result.bOverride_ColorSaturation = true;
		Result.ColorSaturation = FVector4(1.5f, 1.5f, 1.5f, 1.5f);
		Result.bOverride_ReflectionsType = true;
		Result.ReflectionsType = EReflectionsType::RayTracing;
		Result.bOverride_TranslucencyType = false;
		Result.TranslucencyType = ETranslucencyType::RayTracing;
		return Result;
	}();

	const float StartRadius = 200.f;
	const float EndRadius = 500.f;
	const FColor StartColor = FColor::Red;
	const FColor EndColor = FColor::Blue;
	const uint32 bStartCastShadows = true;
	const uint32 bEndCastShaodws = false;
	
	AStaticMeshActor* StaticMesh	= nullptr;
	APostProcessVolume* PostProcess = nullptr;
	APointLight* PointLight			= nullptr;
	
	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			StaticMesh = World->SpawnActor<AStaticMeshActor>(StartLocation, StartRotation);
			StaticMesh->SetActorScale3D(StartScale);

			PostProcess = World->SpawnActor<APostProcessVolume>();
			PostProcess->Settings = StartSettings;

			PointLight = World->SpawnActor<APointLight>(StartLocation, StartRotation);
			PointLight->PointLightComponent->AttenuationRadius = StartRadius;
			PointLight->PointLightComponent->LightColor = StartColor;
			PointLight->PointLightComponent->CastShadows = bStartCastShadows;
		})
		.TakeSnapshot()
	
		.ModifyWorld([&](UWorld* World)
		{
			StaticMesh->SetActorLocationAndRotation(EndLocation, EndRotation);
			StaticMesh->SetActorScale3D(EndScale);

			PostProcess->Settings = EndSettings;
			PostProcess->SetActorLocationAndRotation(StartLocation, StartRotation);

			PointLight->PointLightComponent->AttenuationRadius = EndRadius;
			PointLight->PointLightComponent->LightColor = EndColor;
			PointLight->PointLightComponent->CastShadows = bEndCastShaodws;
		})
		.ApplySnapshot()

		.RunTest([&]()
		{
			TestEqual("Static Mesh Location", StaticMesh->GetActorLocation(), StartLocation);
			TestEqual("Static Mesh Rotation", StaticMesh->GetActorRotation(), StartRotation);
			TestEqual("Static Mesh Scale", StaticMesh->GetActorScale3D(), StartScale);

			TestEqual("Post Process bOverride_ColorSaturation", PostProcess->Settings.bOverride_ColorSaturation, StartSettings.bOverride_ColorSaturation);
			TestEqual("Post Process ColorSaturation", PostProcess->Settings.ColorSaturation, StartSettings.ColorSaturation);
			TestEqual("Post Process bOverride_ReflectionsType", PostProcess->Settings.bOverride_ReflectionsType, StartSettings.bOverride_ReflectionsType);
			TestEqual("Post Process ReflectionsType", PostProcess->Settings.ReflectionsType, StartSettings.ReflectionsType);
			TestEqual("Post Process bOverride_TranslucencyType", PostProcess->Settings.bOverride_TranslucencyType, StartSettings.bOverride_TranslucencyType);
			TestEqual("Post Process TranslucencyType", PostProcess->Settings.TranslucencyType, StartSettings.TranslucencyType);
			
			TestEqual("Post Process Location", PostProcess->GetActorLocation(), FVector(0.f));
			TestEqual("Post Process Rotation", PostProcess->GetActorRotation(), FRotator(0.f));

			TestEqual("Point Light Radius", PointLight->PointLightComponent->AttenuationRadius, StartRadius);
			TestEqual("Point Light Colour", PointLight->PointLightComponent->LightColor, StartColor);
			TestEqual("Point Light Cast Shadows", PointLight->PointLightComponent->CastShadows, bStartCastShadows);
		});
	
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestoreReferenceProperties, "VirtualProduction.LevelSnapshots.Snapshot.RestoreReferenceProperties", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FRestoreReferenceProperties::RunTest(const FString& Parameters)
{
	for (TFieldIterator<FProperty> FieldIt(ASnapshotTestActor::StaticClass()); FieldIt; ++FieldIt)
	{
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(*FieldIt))
		{
			FSoftObjectPath ClassPath(StructProperty->Struct);
			FString AsString = ClassPath.ToString();
			UE_LOG(LogTemp, Log, TEXT("%s"), *AsString);
		}
	}
	ASnapshotTestActor* FirstActor = nullptr;
	ASnapshotTestActor* SecondActor = nullptr;
	ASnapshotTestActor* PointToSelfActor = nullptr;
	ASnapshotTestActor* FromWorldToExternal = nullptr;
	ASnapshotTestActor* FromExternalToWorld = nullptr;
	ASnapshotTestActor* MaterialAndMesh = nullptr;

	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			FirstActor = World->SpawnActor<ASnapshotTestActor>();
			SecondActor = World->SpawnActor<ASnapshotTestActor>();
			PointToSelfActor = World->SpawnActor<ASnapshotTestActor>();
			FromWorldToExternal = World->SpawnActor<ASnapshotTestActor>();
			FromExternalToWorld = World->SpawnActor<ASnapshotTestActor>();
			
			MaterialAndMesh = World->SpawnActor<ASnapshotTestActor>();
			
			FirstActor->SetObjectReference(SecondActor);
			SecondActor->SetObjectReference(FirstActor);
			PointToSelfActor->SetObjectReference(PointToSelfActor);
			FromWorldToExternal->SetObjectReference(FirstActor);
			FromExternalToWorld->SetObjectReference(FromExternalToWorld->CubeMesh);
			
			MaterialAndMesh->InstancedMeshComponent->SetStaticMesh(MaterialAndMesh->CubeMesh);
			MaterialAndMesh->InstancedMeshComponent->SetMaterial(0, MaterialAndMesh->GradientLinearMaterial);
		})
		.TakeSnapshot()

		.ModifyWorld([&](UWorld* World)
		{
			FirstActor->ClearObjectReferences();
			// No change to SecondActor
			PointToSelfActor->SetObjectReference(FirstActor);
			FromWorldToExternal->SetObjectReference(FromWorldToExternal->CubeMesh);
			FromExternalToWorld->SetObjectReference(FirstActor);
			
			MaterialAndMesh->InstancedMeshComponent->SetStaticMesh(MaterialAndMesh->CylinderMesh);
			MaterialAndMesh->InstancedMeshComponent->SetMaterial(0, MaterialAndMesh->GradientLinearMaterial);
		})
		.ApplySnapshot()

		.RunTest([&]()
		{
			TestTrue("Cyclic Reference Restored", FirstActor->HasObjectReference(SecondActor));
			TestTrue("Reference Not Lost", SecondActor->HasObjectReference(FirstActor));
			TestTrue("Restore Pointing To Self", PointToSelfActor->HasObjectReference(PointToSelfActor));
			TestTrue("World > External > World", FromWorldToExternal->HasObjectReference(FirstActor));
			TestTrue("External > World > External", FromExternalToWorld->HasObjectReference(FromExternalToWorld->CubeMesh));

			TestEqual("Mesh", MaterialAndMesh->InstancedMeshComponent->GetStaticMesh(), MaterialAndMesh->CubeMesh);
			TestEqual("Material", MaterialAndMesh->InstancedMeshComponent->GetMaterial(0), MaterialAndMesh->GradientLinearMaterial);
			
		});

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstancedStaticMesh, "VirtualProduction.LevelSnapshots.Snapshot.InstancedStaticMesh", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FInstancedStaticMesh::RunTest(const FString& Parameters)
{
	ASnapshotTestActor* MaterialAndMesh = nullptr;

	const FTransform StartFirstTransform(FRotator(100.f).Quaternion(), FVector(110.f) , FVector(1.f));
	const FTransform EndFirstTransform(FRotator(200.f).Quaternion(), FVector(210.f) , FVector(2.f));
	const FTransform StartSecondTransform(FRotator(100.f).Quaternion(), FVector(-1100.f) , FVector(1.f));
	const FTransform NewThirdTransform(FRotator(-300.f).Quaternion(), FVector(-310.f) , FVector(30.f));
	
	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			MaterialAndMesh = World->SpawnActor<ASnapshotTestActor>();
			
			MaterialAndMesh->InstancedMeshComponent->AddInstance(StartFirstTransform);
			MaterialAndMesh->InstancedMeshComponent->AddInstance(StartSecondTransform);
		})
		.TakeSnapshot()

		.ModifyWorld([&](UWorld* World)
		{
			MaterialAndMesh->InstancedMeshComponent->UpdateInstanceTransform(0, EndFirstTransform);
			MaterialAndMesh->InstancedMeshComponent->AddInstance(NewThirdTransform);
		})
		.ApplySnapshot()

		.RunTest([&]()
		{
			TestEqual("Instance Count", MaterialAndMesh->InstancedMeshComponent->GetInstanceCount(), 2);

			FTransform ActualFirstTransform, ActualSecondTransform;
			MaterialAndMesh->InstancedMeshComponent->GetInstanceTransform(0, ActualFirstTransform);
			MaterialAndMesh->InstancedMeshComponent->GetInstanceTransform(1, ActualSecondTransform);

			// TODO: Investigate rotation not working correcty... it is some kind of math problem that causes angle to flip, i.e. 80 and 100 represent the same angle but are obviously not equal floats...
			TestTrue("Instance 1", ActualFirstTransform.GetLocation().Equals(StartFirstTransform.GetLocation()) && ActualFirstTransform.GetScale3D().Equals(StartFirstTransform.GetScale3D()));
			TestTrue("Instance 2", ActualSecondTransform.GetLocation().Equals(StartSecondTransform.GetLocation())&& ActualSecondTransform.GetScale3D().Equals(StartSecondTransform.GetScale3D()));
		});

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkipTransientAndDeprecatedProperties, "VirtualProduction.LevelSnapshots.Snapshot.SkipTransientAndDeprecatedProperties", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FSkipTransientAndDeprecatedProperties::RunTest(const FString& Parameters)
{
	ASnapshotTestActor* TestActor = nullptr;

	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			TestActor = World->SpawnActor<ASnapshotTestActor>();
		})
		.TakeSnapshot()

		.ModifyWorld([&](UWorld* World)
		{
			TestActor->DeprecatedProperty_DEPRECATED = 500;
			TestActor->TransientProperty = 500;
		})
		.ApplySnapshot()

		.RunTest([&]()
		{
			TestEqual("Skip Deprecated Property", TestActor->DeprecatedProperty_DEPRECATED, 500);
			TestEqual("Skip Transient Property", TestActor->TransientProperty, 500);
		});

	return true;
}

