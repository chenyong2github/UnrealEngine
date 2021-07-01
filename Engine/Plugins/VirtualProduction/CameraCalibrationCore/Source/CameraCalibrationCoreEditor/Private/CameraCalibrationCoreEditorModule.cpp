// Copyright Epic Games, Inc. All Rights Reserved.


#include "CameraCalibrationCoreEditorModule.h"

#include "CoreMinimal.h"

#include "ActorFactories/ActorFactory.h"
#include "CalibrationPointComponent.h"
#include "CalibrationPointComponentDetails.h"
#include "DistortionHandlerPickerDetailCustomization.h"
#include "Editor.h"
#include "IPlacementModeModule.h"
#include "LensFile.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "CameraCalibrationCoreEditor"

DEFINE_LOG_CATEGORY(LogCameraCalibrationCoreEditor);


void FCameraCalibrationCoreEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyEditorModule.RegisterCustomPropertyTypeLayout(
		FDistortionHandlerPicker::StaticStruct()->GetFName(), 
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDistortionHandlerPickerDetailCustomization::MakeInstance)
	);

	PropertyEditorModule.RegisterCustomClassLayout(
		UCalibrationPointComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FCalibrationPointComponentDetails::MakeInstance)
	);

	RegisterPlacementModeItems();
}

void FCameraCalibrationCoreEditorModule::ShutdownModule()
{
	if (!IsEngineExitRequested() && GEditor && UObjectInitialized())
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyModule.UnregisterCustomClassLayout(ULensFile::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UCalibrationPointComponent::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomPropertyTypeLayout(FDistortionHandlerPicker::StaticStruct()->GetFName());

		UnregisterPlacementModeItems();
	}
}

void FCameraCalibrationCoreEditorModule::UnregisterPlacementModeItems()
{
	IPlacementModeModule& PlacementModeModule = IPlacementModeModule::Get();

	for (TOptional<FPlacementModeID>& PlaceActor : PlaceActors)
	{
		if (PlaceActor.IsSet())
		{
			PlacementModeModule.UnregisterPlaceableItem(*PlaceActor);
		}
	}

	PlaceActors.Empty();
}

const FPlacementCategoryInfo* FCameraCalibrationCoreEditorModule::GetVirtualProductionCategoryRegisteredInfo() const
{
	IPlacementModeModule& PlacementModeModule = IPlacementModeModule::Get();

	static const FName VirtualProductionName = TEXT("VirtualProduction");

	if (const FPlacementCategoryInfo* RegisteredInfo = PlacementModeModule.GetRegisteredPlacementCategory(VirtualProductionName))
	{
		return RegisteredInfo;
	}
	else
	{
		FPlacementCategoryInfo Info(
			LOCTEXT("VirtualProductionCategoryName", "Virtual Production"),
			VirtualProductionName,
			TEXT("PMVirtualProduction"),
			25 // Determines where the category shows up in the list with respect to the others.
		);

		IPlacementModeModule::Get().RegisterPlacementCategory(Info);

		return PlacementModeModule.GetRegisteredPlacementCategory(VirtualProductionName);
	}
}

void FCameraCalibrationCoreEditorModule::RegisterPlacementModeItems()
{
	auto RegisterPlaceActors = [&]() -> void
	{
		if (!GEditor)
		{
			return;
		}

		const FPlacementCategoryInfo* Info = GetVirtualProductionCategoryRegisteredInfo();

		if (!Info)
		{
			UE_LOG(LogCameraCalibrationCoreEditor, Warning, TEXT("Could not find or create VirtualProduction Place Actor Category"));
			return;
		}

		// Register the Checkerboard
		{
			FAssetData CheckerboardAssetData(
				TEXT("/Script/CameraCalibrationCore.CameraCalibrationCheckerboard"),
				TEXT("/CameraCalibrationCore"),
				TEXT("CameraCalibrationCheckerboard"),
				TEXT("Actor")
			);
		
			PlaceActors.Add(IPlacementModeModule::Get().RegisterPlaceableItem(Info->UniqueHandle, MakeShared<FPlaceableItem>(
				*UActorFactory::StaticClass(),
				CheckerboardAssetData,
				NAME_None,
				TOptional<FLinearColor>(),
				TOptional<int32>(),
				NSLOCTEXT("PlacementMode", "Checkerboard", "Checkerboard")
			)));
		}
	};

	if (FApp::CanEverRender())
	{
		if (GEngine && GEngine->IsInitialized())
		{
			RegisterPlaceActors();
		}
		else
		{
			PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda(RegisterPlaceActors);
		}
	}
}

void FCameraCalibrationCoreEditorModule::RegisterCalibrationPointDetailsRow(const TWeakPtr<ICalibrationPointComponentDetailsRow> Row)
{
	// Take the opportunity to clean up the list
	RegisteredCalibrationPointComponentDetailsRows.RemoveAll([](TWeakPtr<ICalibrationPointComponentDetailsRow> Elem) {
		return !Elem.IsValid();
	});

	if (!Row.IsValid() || RegisteredCalibrationPointComponentDetailsRows.Contains(Row))
	{
		return;
	}

	RegisteredCalibrationPointComponentDetailsRows.Add(Row);
}

void FCameraCalibrationCoreEditorModule::UnregisterCalibrationPointDetailsRow(const TWeakPtr<ICalibrationPointComponentDetailsRow> Row)
{
	// Take the opportunity to clean up the list
	RegisteredCalibrationPointComponentDetailsRows.RemoveAll([](TWeakPtr<ICalibrationPointComponentDetailsRow> Elem) {
		return !Elem.IsValid();
	});

	if (!Row.IsValid())
	{
		return;
	}

	RegisteredCalibrationPointComponentDetailsRows.Remove(Row);
}

TArray<TWeakPtr<ICalibrationPointComponentDetailsRow>> FCameraCalibrationCoreEditorModule::GetRegisteredCalibrationPointComponentDetailsRows()
{
	return RegisteredCalibrationPointComponentDetailsRows;
}

IMPLEMENT_MODULE(FCameraCalibrationCoreEditorModule, CameraCalibrationCoreEditor);


#undef LOCTEXT_NAMESPACE
