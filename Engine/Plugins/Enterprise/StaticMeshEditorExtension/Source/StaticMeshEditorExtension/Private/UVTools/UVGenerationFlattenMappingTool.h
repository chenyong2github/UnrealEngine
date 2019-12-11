// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/StrongObjectPtr.h"

#include "UVGenerationFlattenMappingTool.generated.h"

class FExtender;
class FMenuBuilder;
class FToolBarBuilder;
class FUICommandList;
class UStaticMesh;
class IStaticMeshEditor;
struct FAssetData;

UENUM()
enum class EUnwrappedUVChannelSelection : uint8
{
	AutomaticLightmapSetup UMETA(Tooltip = "Enable lightmap generation and use the generated unwrapped UV as the lightmap source."),
	FirstEmptyChannel UMETA(Tooltip = "Generate the unwrapped UV in the first UV channel that is empty."),
	SpecifyChannel UMETA(Tooltip = "Manually select the target UV channel for the unwrapped UV generation."),
};

UCLASS(config = EditorPerProjectUserSettings, Transient)
class UUVUnwrapSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = "Flatten Mapping")
	EUnwrappedUVChannelSelection ChannelSelection = EUnwrappedUVChannelSelection::AutomaticLightmapSetup;

	UPROPERTY(config, EditAnywhere, category = "Flatten Mapping", meta = (ToolTip = "The UV channel where to generate the flatten mapping", ClampMin = "0", ClampMax = "7", EditCondition = "ChannelSelection == EUnwrappedUVChannelSelection::SpecifyChannel")) //Clampmax is from MAX_MESH_TEXTURE_COORDS_MD - 1
	int32 UVChannel = 0;

	UPROPERTY(config, EditAnywhere, category = "Flatten Mapping", meta = (ClampMin = "1", ClampMax = "90"))
	float AngleThreshold = 66.f;
};

class FUVGenerationFlattenMappingTool
{
public:
	/*
	 * Called to extend the static mesh editor's secondary toolbar.
	 */
	static TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);

	/*
	 * Opens the Unwrap UV dialog window.
	 */
	static void OpenUnwrapUVWindow(TArray<UStaticMesh*> StaticMeshes);

private:

	/*
	 * Setup the static mesh for UV generation with the given UVUnwrapSettings, enabling bGenerateLightmap if needed and selecting the proper UVChannel according to the options.
	 * 
	 * @param StaticMesh		The StaticMesh being set up
	 * @param UVUnwrapSettings	The generation settings used to chose the UV channel index and set up the lightmap generation
	 * @param LODIndex			The LOD being set up.
	 * @param OutChannelIndex	Out parameter that gives UV channel we should generate the Unwrapped UV into.
	 *
	 * @return Returns true if a UV channel could be safely selected.
	 */
	static bool SetupMeshForUVGeneration(UStaticMesh* StaticMesh, const UUVUnwrapSettings* UVUnwrapSettings, int32 LODIndex, int32& OutChannelIndex);
};

UCLASS()
class UUVGenerationFlattenMappingToolbarProxyObject : public UObject
{
	GENERATED_BODY()

public:

	/** The UV generation flatten mapping toolbar that owns this */
	class FUVGenerationFlattenMappingToolbar* Owner;
};

class FUVGenerationFlattenMappingToolbar : public TSharedFromThis<FUVGenerationFlattenMappingToolbar>
{
public:
	/** Add UV unwrapping menu entry item to the StaticMesh Editor's toolbar */
	static void CreateMenu(FMenuBuilder& ParentMenuBuilder, const TSharedRef<FUICommandList> CommandList, UStaticMesh* InStaticMesh);
};