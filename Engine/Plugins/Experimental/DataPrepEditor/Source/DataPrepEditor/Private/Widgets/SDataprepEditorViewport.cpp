// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SDataprepEditorViewport.h"

#include "DataprepCoreUtils.h"
#include "DataPrepEditor.h"

#include "DataprepEditorLogCategory.h"

#include "ActorEditorUtils.h"
#include "AssetViewerSettings.h"
#include "Async/ParallelFor.h"
#include "ComponentReregisterContext.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GenericPlatform/GenericPlatformTime.h"
#include "HAL/FileManager.h"
#include "IMeshBuilderModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialShared.h"
#include "MeshDescription.h"
#include "MeshDescriptionOperations.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "PhysicsEngine/BodySetup.h"
#include "SceneInterface.h"
#include "SEditorViewportToolBarMenu.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Slate/SceneViewport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DataprepEditorViewport"

// #ueent_todo: Boolean to locally enable multi-threaded build of meshes while the discussion about the proper solution is going on
static bool bComputeUVStretching = false;

TWeakObjectPtr<UMaterial> SDataprepEditorViewport::PreviewMaterial;
TWeakObjectPtr<UMaterial> SDataprepEditorViewport::XRayMaterial;
TWeakObjectPtr<UMaterial> SDataprepEditorViewport::BackFaceMaterial;
TWeakObjectPtr<UMaterial> SDataprepEditorViewport::PerMeshMaterial;
TWeakObjectPtr<UMaterial> SDataprepEditorViewport::ReflectionMaterial;
TWeakObjectPtr<UMaterialInstanceConstant> SDataprepEditorViewport::TransparentMaterial;
TArray<TWeakObjectPtr<UMaterialInstanceConstant>> SDataprepEditorViewport::PerMeshMaterialInstances;
int32 SDataprepEditorViewport::AssetViewerProfileIndex = INDEX_NONE;


const FColor PerMeshColor[] =
{
	FColor(255, 49,  0),
	FColor(255,135,  0),
	FColor( 11,182,255),
	FColor(  0,255,103),

	FColor(255,181,164),
	FColor(255,212,164),
	FColor(168,229,255),
	FColor(164,255,201),

	FColor(255,139,112),
	FColor(255,188,112),
	FColor(118,214,255),
	FColor(112,255,170),

	FColor(217, 41,  0),
	FColor(217,115,  0),
	FColor(  0, 95,137),
	FColor(  0,156, 63),

	FColor(167, 32,  0),
	FColor(167, 88,  0),
	FColor(  0, 73,105),
	FColor(  0,120, 49),
};

// #ueent_todo: Remove this debug utility before releasing
namespace ViewportDebug
{
	static bool bLogTiming = false;

	class FTimeLogger
	{
	public:
		FTimeLogger(const FString& InText)
			: StartTime( FPlatformTime::Cycles64() )
			, Text( InText )
		{
			if( bLogTiming )
			{
				UE_LOG( LogDataprepEditor, Log, TEXT("%s ..."), *Text );
			}
		}

		~FTimeLogger()
		{
			if( bLogTiming )
			{
				// Log time spent to import incoming file in minutes and seconds
				double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

				int ElapsedMin = int(ElapsedSeconds / 60.0);
				ElapsedSeconds -= 60.0 * (double)ElapsedMin;
				UE_LOG( LogDataprepEditor, Log, TEXT("%s took [%d min %.3f s]"), *Text, ElapsedMin, ElapsedSeconds );
			}
		}

	private:
		uint64 StartTime;
		FString Text;
	};
}

// Extension of the FStaticMeshSceneProxy class to allow wireframe display per individual mesh
// #ueent_todo: Could we unify this behavior across classes deriving from UMeshComponent?
class FStaticMeshSceneProxyExt : public FStaticMeshSceneProxy
{
public:
	FStaticMeshSceneProxyExt(UStaticMeshComponent* Component, bool bForceLODsShareStaticLighting)
		: FStaticMeshSceneProxy( Component, bForceLODsShareStaticLighting )
	{
		CustomComponent = Cast<UCustomStaticMeshComponent>(Component);
		check( CustomComponent );
	}

	virtual bool GetMeshElement(
		int32 LODIndex,
		int32 BatchIndex,
		int32 ElementIndex,
		uint8 InDepthPriorityGroup,
		bool bUseSelectionOutline,
		bool bAllowPreCulledIndices,
		FMeshBatch& OutMeshBatch) const override
	{
		if(FStaticMeshSceneProxy::GetMeshElement(LODIndex, BatchIndex, ElementIndex, InDepthPriorityGroup, bUseSelectionOutline, bAllowPreCulledIndices, OutMeshBatch))
		{
			OutMeshBatch.bWireframe = CustomComponent->bForceWireframe;
			OutMeshBatch.bUseWireframeSelectionColoring = 0;

			return true;
		}

		return false;
	}

	virtual bool GetWireframeMeshElement(int32 LODIndex, int32 BatchIndex, const FMaterialRenderProxy* WireframeRenderProxy, uint8 InDepthPriorityGroup, bool bAllowPreCulledIndices, FMeshBatch& OutMeshBatch) const override
	{
		if(FStaticMeshSceneProxy::GetWireframeMeshElement(LODIndex, BatchIndex, WireframeRenderProxy, InDepthPriorityGroup, bAllowPreCulledIndices, OutMeshBatch))
		{
			OutMeshBatch.bWireframe = CustomComponent->bForceWireframe;
			OutMeshBatch.bUseWireframeSelectionColoring = 0;

			return true;
		}

		return false;
	}

private:
	UCustomStaticMeshComponent* CustomComponent;
};

namespace DataprepEditor3DPreviewUtils
{
	/** Build the render data based on the current geometry available in the static mesh */
	void BuildStaticMeshes(TSet<UStaticMesh*>& StaticMeshes, TArray<UStaticMesh*>& BuiltMeshes);

	/** Compile all materials included in the input array*/
	// Copied from DatasmithImporterImpl::CompileMaterial
	void CompileMaterials(const TArray< UMaterialInterface* > Materials);

	void FindMeshComponents(const AActor * InActor, TArray<UStaticMeshComponent*>& MeshComponents, bool bRecursive );

	void ComputeUVStretching(FStaticMeshLODResources&  Resource);

	/** Returns array of static mesh components in world */
	template<class T>
	TArray< T* > GetComponentsFromWorld( UWorld* World )
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UMeshComponent>::Value, "'T' template parameter to FindComponentByClass must be derived from UActorComponent");

		TArray< T* > Result;

		const EActorIteratorFlags Flags = EActorIteratorFlags::SkipPendingKill;
		// #ueent_todo: Maybe just enumerate the actors in the current level
		for (TActorIterator<AActor> It( World, AActor::StaticClass(), Flags ); It; ++It)
		{
			AActor* Actor = *It;

			// Don't consider transient actors in non-play worlds
			// Don't consider the builder brush
			// Don't consider the WorldSettings actor, even though it is technically editable
			const bool bIsValid = Actor != nullptr && Actor->IsEditable() && !Actor->IsTemplate() /*&& !Actor->HasAnyFlags(RF_Transient) */&& !FActorEditorUtils::IsABuilderBrush(Actor) && !Actor->IsA(AWorldSettings::StaticClass());

			if ( bIsValid )
			{
				TArray< T* > Components;
				Actor->GetComponents<T>( Components );
				for( T* Component : Components )
				{
					// If a mesh is displayable, it should have at least one material
					if( Component->GetNumMaterials() > 0 )
					{
						Result.Add( Component );
					}
				}
			}
		}

		return Result;
	}
}

//
// SDataprepEditorViewport Class
//

SDataprepEditorViewport::SDataprepEditorViewport()
	: PreviewScene( MakeShareable( new FAdvancedPreviewScene( FPreviewScene::ConstructionValues() ) ) )
	, Extender( MakeShareable( new FExtender ) )
	, WorldToPreview( nullptr )
	, RenderingMaterialType( ERenderingMaterialType::OriginalRenderingMaterial )
	, CurrentSelectionMode( ESelectionModeType::OutlineSelectionMode )
	, bWireframeRenderingMode( false )
#ifdef VIEWPORT_EXPERIMENTAL
	, bShowOrientedBox( false )
#endif
{
}

SDataprepEditorViewport::~SDataprepEditorViewport()
{
	CastChecked<UEditorEngine>(GEngine)->OnPreviewFeatureLevelChanged().Remove(PreviewFeatureLevelChangedHandle);
	ClearMeshes();
}

void SDataprepEditorViewport::Construct(const FArguments& InArgs, TSharedPtr<FDataprepEditor> InDataprepEditor)
{
	DataprepEditor = InDataprepEditor;

	FDataprepEditorViewportCommands::Register();

	// restore last used feature level
	UWorld* PreviewSceneWorld = PreviewScene->GetWorld();
	if (PreviewSceneWorld != nullptr)
	{
		PreviewSceneWorld->ChangeFeatureLevel(GWorld->FeatureLevel);
	}

	// Listen to and act on changes in feature level
	UEditorEngine* Editor = CastChecked<UEditorEngine>(GEngine);
	PreviewFeatureLevelChangedHandle = Editor->OnPreviewFeatureLevelChanged().AddLambda([&PreviewSceneWorld](ERHIFeatureLevel::Type NewFeatureLevel)
	{
		PreviewSceneWorld->ChangeFeatureLevel(NewFeatureLevel);
	});

	// Create actor in preview world to hold all preview mesh components
	PreviewActor = TWeakObjectPtr<AActor>( PreviewSceneWorld->SpawnActor<AActor>( AActor::StaticClass(), FTransform::Identity ) );

	if ( PreviewActor->GetRootComponent() == nullptr )
	{
		USceneComponent* RootComponent = NewObject< USceneComponent >( PreviewActor.Get(), USceneComponent::StaticClass(), TEXT("PreviewActor"), RF_NoFlags );
		PreviewActor->SetRootComponent( RootComponent );
	}

	WorldToPreview = InArgs._WorldToPreview;
	check(WorldToPreview);

	SEditorViewport::Construct( SEditorViewport::FArguments() );
}

void SDataprepEditorViewport::ClearMeshes()
{
	const int32 PreviousCount = PreviewMeshComponents.Num();
	TArray<UObject*> ObjectsToDelete;
	ObjectsToDelete.Reserve( PreviousCount );

	for( const TWeakObjectPtr< UStaticMeshComponent >& PreviewMeshComponent : PreviewMeshComponents )
	{
		if(UStaticMeshComponent* MeshComponent = PreviewMeshComponent.Get())
		{
			ObjectsToDelete.Add( MeshComponent );
		}
	}

	FDataprepCoreUtils::PurgeObjects( MoveTemp( ObjectsToDelete ) );

	// Release render data created for display
	for(UStaticMesh* StaticMesh : BuiltMeshes)
	{
		if(StaticMesh)
		{
			// Free any RHI resources created for display
			StaticMesh->PreEditChange(nullptr);
			StaticMesh->RenderData.Reset();
			// No need to post-edit
		}
	}
	BuiltMeshes.Empty( BuiltMeshes.Num() );

	// Restoring mesh components' render states
	for( TWeakObjectPtr< UStaticMeshComponent >& MeshComponentPtr : MeshComponentsToRestore )
	{
		if(UStaticMeshComponent* MeshComponent = MeshComponentPtr.Get())
		{
			MeshComponent->RecreateRenderState_Concurrent();
		}
	}
	MeshComponentsToRestore.Empty( MeshComponentsToRestore.Num() );

	PreviewMeshComponents.Empty(PreviousCount);
	MeshComponentsMapping.Empty(PreviousCount);
	MeshComponentsReverseMapping.Empty(PreviousCount);
	DisplayMaterialsMap.Empty(DisplayMaterialsMap.Num());
	SelectedPreviewComponents.Empty(SelectedPreviewComponents.Num());

	OverlayTextVerticalBox->ClearChildren();
}

void SDataprepEditorViewport::UpdateMeshes()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SDataprepEditorViewport::UpdateMeshes);

	ClearMeshes();

	RenderingMaterialType = ERenderingMaterialType::OriginalRenderingMaterial;
	bWireframeRenderingMode = false;

	ViewportDebug::FTimeLogger TimeLogger( TEXT("Updating viewport") );

	SceneBounds = FBox( FVector::ZeroVector, FVector(100.0f) );
	SceneUniformScale = 1.0f;

	// Gather all static meshes used by actors in PreviewWorld
	TArray< UStaticMeshComponent* > SceneMeshComponents = DataprepEditor3DPreviewUtils::GetComponentsFromWorld<UStaticMeshComponent>( WorldToPreview );

	if(SceneMeshComponents.Num() > 0)
	{
		TSet<UStaticMesh*> StaticMeshes;

		for( UStaticMeshComponent*& MeshComponent : SceneMeshComponents )
		{
			if( UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh() )
			{
				StaticMeshes.Add( StaticMesh );
			}
			else
			{
				MeshComponent = nullptr;
			}
		}

		if( StaticMeshes.Num() > 0)
		{
			FScopedSlowTask SlowTask( 100.0f, LOCTEXT("UpdateMeshes_Title", "Updating 3D viewport ...") );
			SlowTask.MakeDialog(false);

			// Build render data of static meshes for display
			SlowTask.EnterProgressFrame( 50.0f, LOCTEXT("UpdateMeshes_StaticMeshes", "Buidling static meshes ...") );
			DataprepEditor3DPreviewUtils::BuildStaticMeshes( StaticMeshes, BuiltMeshes );

			// Clear render state of static mesh components from the preview world which
			// static meshes have been built for the 3D viewport
			// Required so mesh components from the preview world are not impacted by the creation
			// and deletion of render data done for the viewport
			// The mesh components' render state will be restored when the viewport is cleared
			{
				TSet<UStaticMesh*> BuiltMeshesSet;
				BuiltMeshesSet.Reserve( BuiltMeshes.Num() );
				BuiltMeshesSet.Append( BuiltMeshes );

				MeshComponentsToRestore.Empty( SceneMeshComponents.Num() );
				for( UStaticMeshComponent*& MeshComponent : SceneMeshComponents )
				{
					if( MeshComponent->IsRegistered() && BuiltMeshesSet.Contains( MeshComponent->GetStaticMesh() ) )
					{
						if( MeshComponent->IsRenderStateCreated() )
						{
							if( !MeshComponent->IsRenderStateDirty() )
							{
								MeshComponent->DoDeferredRenderUpdates_Concurrent();
							}

							MeshComponent->DestroyRenderState_Concurrent();

							MeshComponentsToRestore.Add( MeshComponent );
						}
					}
				}
			}

			CreateDisplayMaterials( SceneMeshComponents );

			PreviewMeshComponents.Empty( SceneMeshComponents.Num() );

			// Compute bounding box of scene to determine camera position and scaling to apply for smooth navigation
			SlowTask.EnterProgressFrame( 10.0f, LOCTEXT("UpdateMeshes_Prepare", "Preparing viewport ...") );
			SceneBounds.Init();
			for( UStaticMeshComponent* SceneMeshComponent : SceneMeshComponents )
			{
				if(SceneMeshComponent != nullptr)
				{
					const UStaticMesh* StaticMesh = SceneMeshComponent->GetStaticMesh();
					const FTransform& ComponentToWorldTransform = SceneMeshComponent->GetComponentTransform();
					SceneBounds += StaticMesh->ExtendedBounds.GetBox().TransformBy( ComponentToWorldTransform );
				}
			}

			// Compute uniform scale
			FVector Extents = SceneBounds.GetExtent();
			if(Extents.GetMax() < 100.0f)
			{
				SceneUniformScale = 100.0f / ( Extents.GetMax() * 1.1f );
			}
			SceneBounds.Max *= SceneUniformScale;
			SceneBounds.Min *= SceneUniformScale;

			// Set uniform scale on root actor's root component
			PreviewActor->GetRootComponent()->SetRelativeTransform( FTransform( FRotator::ZeroRotator, FVector::ZeroVector, FVector( SceneUniformScale ) ) );

			const int32 PerMeshColorsCount = sizeof(PerMeshColor);
			int32 PerMeshColorIndex = 0;
			// Replicate mesh component from world to preview in preview world
			SlowTask.EnterProgressFrame( 40.0f, LOCTEXT("UpdateMeshes_Components", "Adding meshes to viewport ...") );
			{
				FScopedSlowTask SubSlowTask( SceneMeshComponents.Num(), LOCTEXT("UpdateMeshes_Components", "Adding meshes to viewport ...") );
				SubSlowTask.MakeDialog(false);
				for( UStaticMeshComponent* SceneMeshComponent : SceneMeshComponents )
				{
					FText Message = LOCTEXT("UpdateMeshes_SkipOneComponent", "Skipping null actor ...");
					if(SceneMeshComponent != nullptr && SceneMeshComponent->GetOwner() )
					{
						Message = FText::Format( LOCTEXT("UpdateMeshes_AddOneComponent", "Adding {0} ..."), FText::FromString( SceneMeshComponent->GetOwner()->GetActorLabel() ) );
					}
					SubSlowTask.EnterProgressFrame( 1.0f, Message );

					if(SceneMeshComponent != nullptr)
					{
						UCustomStaticMeshComponent* PreviewMeshComponent = NewObject< UCustomStaticMeshComponent >( PreviewActor.Get(), NAME_None, RF_Transient );
						if (GEditor->PreviewPlatform.GetEffectivePreviewFeatureLevel() <= ERHIFeatureLevel::ES3_1)
						{
							PreviewMeshComponent->SetMobility(EComponentMobility::Static);
						}

						PreviewMeshComponent->bForceWireframe = bWireframeRenderingMode;
						PreviewMeshComponent->MeshColorIndex = PerMeshColorIndex % PerMeshColorsCount;
						++PerMeshColorIndex;

						UStaticMesh* StaticMesh = SceneMeshComponent->GetStaticMesh();

						FComponentReregisterContext ReregisterContext( PreviewMeshComponent );
						PreviewMeshComponent->SetStaticMesh( StaticMesh );

						FTransform ComponentToWorldTransform = SceneMeshComponent->GetComponentTransform();

						PreviewMeshComponent->AttachToComponent( PreviewActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform );
						PreviewMeshComponent->SetRelativeTransform( ComponentToWorldTransform );
						PreviewMeshComponent->RegisterComponentWithWorld( PreviewScene->GetWorld() );

						// Apply preview material to preview static mesh component
						for(int32 Index = 0; Index < StaticMesh->StaticMaterials.Num(); ++Index)
						{
							UMaterialInterface* MaterialInterface = SceneMeshComponent->GetMaterial(Index);

							if(MaterialInterface == nullptr)
							{
								MaterialInterface = StaticMesh->StaticMaterials[Index].MaterialInterface;
							}

							PreviewMeshComponent->SetMaterial( Index, DisplayMaterialsMap[MaterialInterface].Get() );
						}

						PreviewMeshComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw( this, &SDataprepEditorViewport::IsComponentSelected );

						PreviewMeshComponents.Emplace( PreviewMeshComponent );

						MeshComponentsMapping.Add( SceneMeshComponent, PreviewMeshComponent );
						MeshComponentsReverseMapping.Add( PreviewMeshComponent, SceneMeshComponent );
					}
				}
			}

#ifdef VIEWPORT_EXPERIMENTAL
			{
				ViewportDebug::FTimeLogger LapTimeLogger( TEXT("Building mesh properties") );

				TMap<UStaticMesh*, FPrototypeOrientedBox> MeshPropertiesMap;
				TArray<UStaticMesh*> StaticMeshesToBuild = StaticMeshes.Array();

				if( StaticMeshes.Num() > 1 )
				{
					TArray<FPrototypeOrientedBox> MeshProperties;
					MeshProperties.AddDefaulted( StaticMeshes.Num() );

					ParallelFor( StaticMeshesToBuild.Num(), [&]( int32 Index ) {
						MeshDescriptionPrototype::GenerateOrientedBox( *StaticMeshesToBuild[Index]->GetMeshDescription(0), MeshProperties[Index], StaticMeshesToBuild[Index]->GetName());
					});

					for(int32 Index = 0; Index < StaticMeshesToBuild.Num(); ++Index)
					{
						MeshPropertiesMap.Add( StaticMeshesToBuild[Index], MeshProperties[Index]);
					}
				}
				else
				{
					MeshPropertiesMap.Add( StaticMeshesToBuild[0], MeshDescriptionPrototype::GenerateOrientedBox( *StaticMeshesToBuild[0]->GetMeshDescription(0), StaticMeshesToBuild[0]->GetName() ) );
				}

				TSet<UStaticMesh*> ShouldBeInstanced;
				MeshDescriptionPrototype::IdentifyInstances( MeshPropertiesMap, ShouldBeInstanced);

				for( const TWeakObjectPtr< UStaticMeshComponent >& PreviewMeshComponentPtr : PreviewMeshComponents )
				{
					if(UCustomStaticMeshComponent* PreviewMeshComponent = Cast<UCustomStaticMeshComponent>(PreviewMeshComponentPtr.Get()))
					{
						PreviewMeshComponent->bShoudlBeInstanced = ShouldBeInstanced.Contains(PreviewMeshComponent->GetStaticMesh());
						PreviewMeshComponent->MeshProperties = MeshPropertiesMap[PreviewMeshComponent->GetStaticMesh()];
					}
				}
			}
#endif
		}
	}

	PreviewScene->SetFloorOffset(-SceneBounds.Min.Z );

	EditorViewportClient->FocusViewportOnBox( SceneBounds );

	UpdateOverlayText();

	SceneViewport->Invalidate();
}

TSharedRef<SEditorViewport> SDataprepEditorViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SDataprepEditorViewport::GetExtenders() const
{
	return Extender;
}

void SDataprepEditorViewport::OnFloatingButtonClicked()
{
}

TSharedRef<FEditorViewportClient> SDataprepEditorViewport::MakeEditorViewportClient()
{
	EditorViewportClient = MakeShareable( new FDataprepEditorViewportClient( SharedThis(this), PreviewScene.ToSharedRef() ) );

	EditorViewportClient->ViewportType = LVT_Perspective;
	EditorViewportClient->bSetListenerPosition = false;
	EditorViewportClient->SetViewLocation( EditorViewportDefs::DefaultPerspectiveViewLocation );
	EditorViewportClient->SetViewRotation( EditorViewportDefs::DefaultPerspectiveViewRotation );
	EditorViewportClient->SetRealtime( true );

	return EditorViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SDataprepEditorViewport::MakeViewportToolbar()
{
	return SNew(SDataprepEditorViewportToolbar, SharedThis(this));
}

void SDataprepEditorViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
	SEditorViewport::PopulateViewportOverlays(Overlay);

	ScreenSizeText = SNew(STextBlock)
	.Text( LOCTEXT( "ScreenSize", "Current Screen Size:"))
	.TextStyle(FEditorStyle::Get(), "TextBlock.ShadowedText");

	Overlay->AddSlot()
	.VAlign(VAlign_Top)
	.HAlign(HAlign_Left)
	.Padding(FMargin(10.0f, 40.0f, 10.0f, 10.0f))
	[
		SAssignNew(OverlayTextVerticalBox, SVerticalBox)
	];

	// this widget will display the current viewed feature level
	Overlay->AddSlot()
	.VAlign(VAlign_Bottom)
	.HAlign(HAlign_Right)
	.Padding(5.0f)
	[
		BuildFeatureLevelWidget()
	];
}

void SDataprepEditorViewport::UpdateOverlayText()
{
	TArray<FOverlayTextItem> TextItems;

	TSet<UStaticMesh*> StaticMeshes;

	int32 TrianglesCount = 0;
	int32 VerticesCount = 0;
	for( const TWeakObjectPtr< UStaticMeshComponent >& PreviewMeshComponent : PreviewMeshComponents )
	{
		if(UStaticMeshComponent* MeshComponent = PreviewMeshComponent.Get())
		{
			UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
			TrianglesCount += StaticMesh->RenderData->LODResources[0].GetNumTriangles();
			VerticesCount += StaticMesh->RenderData->LODResources[0].GetNumVertices();
			StaticMeshes.Add(StaticMesh);
		}
	}

	TextItems.Add(FOverlayTextItem(
		FText::Format( LOCTEXT( "Meshes", "#Static Meshes:  {0}"), FText::AsNumber( StaticMeshes.Num() ) ) ) );

	TextItems.Add(FOverlayTextItem(
		FText::Format( LOCTEXT( "DrawnMeshes", "#Meshes drawn:  {0}"), FText::AsNumber(PreviewMeshComponents.Num()))));

	TextItems.Add(FOverlayTextItem(
		FText::Format( LOCTEXT( "Triangles_F", "#Triangles To Draw:  {0}"), FText::AsNumber(TrianglesCount))));

	TextItems.Add(FOverlayTextItem(
		FText::Format( LOCTEXT( "Vertices_F", "#Vertices Used:  {0}"), FText::AsNumber(VerticesCount))));

	FVector SceneExtents = SceneBounds.GetExtent();
	TextItems.Add(FOverlayTextItem(
		FText::Format( LOCTEXT( "ApproxSize_F", "Approx Size: {0}x{1}x{2}"),
		FText::AsNumber(int32(SceneExtents.X * 2.0f)), // x2 as artists wanted length not radius
		FText::AsNumber(int32(SceneExtents.Y * 2.0f)),
		FText::AsNumber(int32(SceneExtents.Z * 2.0f)))));
	
	OverlayTextVerticalBox->ClearChildren();

	OverlayTextVerticalBox->AddSlot()
	[
		ScreenSizeText.ToSharedRef()
	];

	for (const auto& TextItem : TextItems)
	{
		OverlayTextVerticalBox->AddSlot()
		[
			SNew(STextBlock)
			.Text(TextItem.Text)
			.TextStyle(FEditorStyle::Get(), TextItem.Style)
		];
	}
}

void SDataprepEditorViewport::UpdateScreenSizeText( FText Text )
{
	ScreenSizeText->SetText( Text );
}

void SDataprepEditorViewport::BindCommands()
{
	SEditorViewport::BindCommands();

	const FDataprepEditorViewportCommands& Commands = FDataprepEditorViewportCommands::Get();

	TSharedRef<FDataprepEditorViewportClient> EditorViewportClientRef = EditorViewportClient.ToSharedRef();

	CommandList->MapAction(
		Commands.SetShowGrid,
		FExecuteAction::CreateSP( EditorViewportClientRef, &FEditorViewportClient::SetShowGrid ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( EditorViewportClientRef, &FEditorViewportClient::IsSetShowGridChecked ) );

	CommandList->MapAction(
		Commands.SetShowBounds,
		FExecuteAction::CreateSP( EditorViewportClientRef, &FEditorViewportClient::ToggleShowBounds ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( EditorViewportClientRef, &FEditorViewportClient::IsSetShowBoundsChecked ) );

	CommandList->MapAction(
		Commands.ApplyOriginalMaterial,
		FExecuteAction::CreateSP( this, &SDataprepEditorViewport::SetRenderingMaterial, ERenderingMaterialType::OriginalRenderingMaterial ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &SDataprepEditorViewport::IsRenderingMaterialApplied, ERenderingMaterialType::OriginalRenderingMaterial ) );

	CommandList->MapAction(
		Commands.ApplyBackFaceMaterial,
		FExecuteAction::CreateSP( this, &SDataprepEditorViewport::SetRenderingMaterial, ERenderingMaterialType::BackFaceRenderingMaterial ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &SDataprepEditorViewport::IsRenderingMaterialApplied, ERenderingMaterialType::BackFaceRenderingMaterial ) );

	CommandList->MapAction(
		Commands.ApplyXRayMaterial,
		FExecuteAction::CreateSP( this, &SDataprepEditorViewport::SetRenderingMaterial, ERenderingMaterialType::XRayRenderingMaterial ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &SDataprepEditorViewport::IsRenderingMaterialApplied, ERenderingMaterialType::XRayRenderingMaterial ) );

	CommandList->MapAction(
		Commands.ApplyPerMeshMaterial,
		FExecuteAction::CreateSP( this, &SDataprepEditorViewport::SetRenderingMaterial, ERenderingMaterialType::PerMeshRenderingMaterial ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &SDataprepEditorViewport::IsRenderingMaterialApplied, ERenderingMaterialType::PerMeshRenderingMaterial ) );

	CommandList->MapAction(
		Commands.ApplyReflectionMaterial,
		FExecuteAction::CreateSP( this, &SDataprepEditorViewport::SetRenderingMaterial, ERenderingMaterialType::ReflectionRenderingMaterial ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &SDataprepEditorViewport::IsRenderingMaterialApplied, ERenderingMaterialType::ReflectionRenderingMaterial ) );

	CommandList->MapAction(
		Commands.ApplyOutlineSelection,
		FExecuteAction::CreateSP( this, &SDataprepEditorViewport::SetSelectionMode, ESelectionModeType::OutlineSelectionMode ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &SDataprepEditorViewport::IsSetSelectionModeApplied, ESelectionModeType::OutlineSelectionMode ) );

	CommandList->MapAction(
		Commands.ApplyXRaySelection,
		FExecuteAction::CreateSP( this, &SDataprepEditorViewport::SetSelectionMode, ESelectionModeType::XRaySelectionMode ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &SDataprepEditorViewport::IsSetSelectionModeApplied, ESelectionModeType::XRaySelectionMode ) );

	CommandList->MapAction(
		Commands.ApplyWireframeMode,
		FExecuteAction::CreateSP( this, &SDataprepEditorViewport::ToggleWireframeRenderingMode ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &SDataprepEditorViewport::IsWireframeRenderingModeOn ) );

#ifdef VIEWPORT_EXPERIMENTAL
	CommandList->MapAction(
		Commands.ShowOOBs,
		FExecuteAction::CreateSP( this, &SDataprepEditorViewport::ToggleShowOrientedBox ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &SDataprepEditorViewport::IsShowOrientedBoxOn ) );
#endif
}

EVisibility SDataprepEditorViewport::OnGetViewportContentVisibility() const
{
	return EVisibility::Visible/*IsVisible() ? EVisibility::Visible : EVisibility::Collapsed*/;
}

void SDataprepEditorViewport::OnFocusViewportToSelection()
{
	if(SelectedPreviewComponents.Num() == 0)
	{
		EditorViewportClient->FocusViewportOnBox( SceneBounds );
		return;
	}

	FBox SelectionBounds;
	SelectionBounds.Init();

	// Compute bounding box of scene to determine camera position and scaling to apply
	for( UStaticMeshComponent* SelectedComponent : SelectedPreviewComponents )
	{
		if(SelectedComponent != nullptr)
		{
			const UStaticMesh* StaticMesh = SelectedComponent->GetStaticMesh();
			const FTransform& ComponentToWorldTransform = SelectedComponent->GetComponentTransform();
			SelectionBounds += StaticMesh->ExtendedBounds.GetBox().TransformBy( ComponentToWorldTransform );
		}
	}

	FVector Center = SelectionBounds.GetCenter();
	FVector Extents = SelectionBounds.GetExtent() * 1.1f;
	EditorViewportClient->FocusViewportOnBox( FBox(Center - Extents, Center + Extents) );

	SceneViewport->Invalidate();
}

void SDataprepEditorViewport::InitializeDefaultMaterials()
{
	const int32 DefaultMaterialsCount = 4;

	TArray< UMaterialInterface* > Materials;
	Materials.Reserve( DefaultMaterialsCount );

	if(!PreviewMaterial.IsValid())
	{
		PreviewMaterial = TWeakObjectPtr<UMaterial>( Cast< UMaterial >( FSoftObjectPath("/DataPrepEditor/PreviewMaterial.PreviewMaterial").TryLoad() ) );

		if(!PreviewMaterial.IsValid())
		{
			PreviewMaterial = TWeakObjectPtr<UMaterial>( Cast< UMaterial >( FSoftObjectPath("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial").TryLoad() ) );
		}

		check( PreviewMaterial.IsValid() );

		Materials.Add( PreviewMaterial.Get() );
	}

	if(!TransparentMaterial.IsValid())
	{
		TransparentMaterial = TWeakObjectPtr<UMaterialInstanceConstant>( NewObject<UMaterialInstanceConstant>( GetTransientPackage(), NAME_None, EObjectFlags::RF_Transient) );
		TransparentMaterial->Parent = PreviewMaterial.Get();

		TransparentMaterial->BasePropertyOverrides.bOverride_BlendMode = true;
		TransparentMaterial->BasePropertyOverrides.BlendMode = BLEND_Translucent;

		TransparentMaterial->SetScalarParameterValueEditorOnly( TEXT("Transparency"), 0.75f );
		TransparentMaterial->SetVectorParameterValueEditorOnly( TEXT( "DiffuseColor" ), FLinearColor::Gray );

		check( TransparentMaterial.IsValid() );

		Materials.Add( TransparentMaterial.Get() );
	}

	if(!XRayMaterial.IsValid())
	{
		XRayMaterial = TWeakObjectPtr<UMaterial>( Cast< UMaterial >( FSoftObjectPath("/DataPrepEditor/xray_master.xray_master").TryLoad() ) );

		check( XRayMaterial.IsValid() );

		Materials.Add( XRayMaterial.Get() );
	}

	if(!BackFaceMaterial.IsValid())
	{
		BackFaceMaterial = TWeakObjectPtr<UMaterial>( Cast< UMaterial >( FSoftObjectPath("/DataPrepEditor/BackFaceMaterial.BackFaceMaterial").TryLoad() ) );

		check( BackFaceMaterial.IsValid() );

		Materials.Add( BackFaceMaterial.Get() );
	}

	if(!PerMeshMaterial.IsValid())
	{
		PerMeshMaterial = TWeakObjectPtr<UMaterial>( Cast< UMaterial >( FSoftObjectPath("/DataPrepEditor/PerMeshMaterial.PerMeshMaterial").TryLoad() ) );

		check( PerMeshMaterial.IsValid() );

		Materials.Add( PerMeshMaterial.Get() );
	}

	if(PerMeshMaterialInstances.Num() == 0)
	{
		PerMeshMaterialInstances.AddDefaulted( sizeof(PerMeshColor) );
	}

	for(int32 Index = 0; Index < PerMeshMaterialInstances.Num(); ++Index)
	{
		TWeakObjectPtr<UMaterialInstanceConstant>& PerMeshMaterialInstance = PerMeshMaterialInstances[Index];

		if(!PerMeshMaterialInstance.IsValid())
		{
			PerMeshMaterialInstance = TWeakObjectPtr<UMaterialInstanceConstant>( NewObject<UMaterialInstanceConstant>( GetTransientPackage(), NAME_None, EObjectFlags::RF_Transient) );
			PerMeshMaterialInstance->Parent = PerMeshMaterial.Get();

			check( PerMeshMaterialInstance.IsValid() );

			PerMeshMaterialInstance->SetVectorParameterValueEditorOnly( TEXT( "Color" ), FLinearColor( PerMeshColor[Index] ) );

			Materials.Add( PerMeshMaterialInstance.Get() );
		}
	}

	if(!ReflectionMaterial.IsValid())
	{
		ReflectionMaterial = TWeakObjectPtr<UMaterial>( Cast< UMaterial >( FSoftObjectPath("/DataPrepEditor/ReflectionMaterial.ReflectionMaterial").TryLoad() ) );

		check( ReflectionMaterial.IsValid() );

		Materials.Add( ReflectionMaterial.Get() );
	}

	if(Materials.Num() > 0)
	{
		DataprepEditor3DPreviewUtils::CompileMaterials( Materials );
	}
}

void SDataprepEditorViewport::CreateDisplayMaterials(const TArray< UStaticMeshComponent* >& SceneMeshComponents)
{
	InitializeDefaultMaterials();

	DisplayMaterialsMap.Empty();

	for( const UStaticMeshComponent* SceneMeshComponent : SceneMeshComponents )
	{
		if(SceneMeshComponent != nullptr)
		{
			UStaticMesh* StaticMesh = SceneMeshComponent->GetStaticMesh();

			for(int32 Index = 0; Index < StaticMesh->StaticMaterials.Num(); ++Index)
			{
				if(UMaterialInterface* MaterialInterface = SceneMeshComponent->GetMaterial(Index))
				{
					DisplayMaterialsMap.Add( MaterialInterface );
				}
				else
				{
					DisplayMaterialsMap.Add( StaticMesh->StaticMaterials[Index].MaterialInterface );
				}
			}
		}
	}

	TArray< UMaterialInterface* > Materials;
	Materials.Reserve( DisplayMaterialsMap.Num() );

	TMap< UMaterialInterface*, UMaterialInterface* > ParentMaterials;

	for(auto& MaterialEntry : DisplayMaterialsMap)
	{
		UMaterialInstanceConstant* MaterialInstance = nullptr;
		if(UMaterialInstanceConstant* ConstantMaterialInstance = Cast< UMaterialInstanceConstant >(MaterialEntry.Key))
		{
			MaterialInstance = DuplicateObject< UMaterialInstanceConstant >( ConstantMaterialInstance, GetTransientPackage(), NAME_None);
			if( !ParentMaterials.Contains(ConstantMaterialInstance->Parent) )
			{
				// #ueent_todo: Assuming here that the parent is a Material
				UMaterial* SourceMaterial = Cast<UMaterial>(ConstantMaterialInstance->Parent);
				ensure(SourceMaterial);

				UMaterial* ParentMaterial = DuplicateObject< UMaterial >( SourceMaterial, GetTransientPackage(), NAME_None);

				ParentMaterials.Add( SourceMaterial, ParentMaterial );
				Materials.Add( ParentMaterial );
			}

			MaterialInstance->SetFlags( RF_Transient );
			MaterialInstance->Parent = ParentMaterials[ConstantMaterialInstance->Parent];
		}
		else if(UMaterial* Material = Cast<UMaterial>(MaterialEntry.Key))
		{
			UMaterial* ParentMaterial = DuplicateObject< UMaterial >( Material, GetTransientPackage(), NAME_None);

			ParentMaterials.Add( Material, ParentMaterial );
			Materials.Add( ParentMaterial );

			MaterialInstance = NewObject<UMaterialInstanceConstant>( GetTransientPackage(), NAME_None, EObjectFlags::RF_Transient);
			MaterialInstance->Parent = ParentMaterial;
		}
		else
		{
			MaterialInstance = NewObject<UMaterialInstanceConstant>( GetTransientPackage(), NAME_None, EObjectFlags::RF_Transient);
			MaterialInstance->Parent = PreviewMaterial.Get();
		}

		MaterialEntry.Value = TWeakObjectPtr<UMaterialInstanceConstant>(MaterialInstance);
		Materials.Add( MaterialInstance );
	}


	DataprepEditor3DPreviewUtils::CompileMaterials( Materials );
}

void SDataprepEditorViewport::SetSelection(UStaticMeshComponent* Component)
{
	SelectedPreviewComponents.Empty( 1 );
	SelectedPreviewComponents.Add( Component );
	UpdateSelection();
}

void SDataprepEditorViewport::AddToSelection(UStaticMeshComponent* Component)
{
	int32 PrevSelectedCount = SelectedPreviewComponents.Num();

	SelectedPreviewComponents.Add( Component );

	if(PrevSelectedCount != SelectedPreviewComponents.Num())
	{
		UpdateSelection();
	}
}

void SDataprepEditorViewport::RemoveFromSelection(UStaticMeshComponent* Component)
{
	int32 PrevSelectedCount = SelectedPreviewComponents.Num();

	SelectedPreviewComponents.Remove( Component );

	if(PrevSelectedCount != SelectedPreviewComponents.Num())
	{
		UpdateSelection();
	}
}


void SDataprepEditorViewport::ClearSelection(bool bNotify)
{
	if(SelectedPreviewComponents.Num() > 0)
	{
		SelectedPreviewComponents.Empty();
		ApplyRenderingMaterial();

		if(bNotify)
		{
			TSharedPtr<FDataprepEditor> DataprepEditorPtr = DataprepEditor.Pin();

			if(DataprepEditorPtr.IsValid())
			{
				DataprepEditorPtr->SetWorldObjectsSelection( TSet<TWeakObjectPtr<UObject>>(), FDataprepEditor::EWorldSelectionFrom::Viewport );
			}
		}
	}
}

void SDataprepEditorViewport::SelectActors(const TArray< AActor* >& SelectedActors)
{
	// Deselect all if array of selected actors is empty
	if(SelectedActors.Num() == 0)
	{
		ClearSelection();
		return;
	}

	TArray<UStaticMeshComponent*> NewSelectedPreviewComponents;
	NewSelectedPreviewComponents.Reserve( SelectedActors.Num() );

	for(const AActor* SelectedActor : SelectedActors)
	{
		TArray< UStaticMeshComponent* > Components;
		SelectedActor->GetComponents< UStaticMeshComponent >( Components );
		for( UStaticMeshComponent* SelectedComponent : Components )
		{
			// If a mesh is displayable, it should have at least one material
			if(SelectedComponent->GetStaticMesh())
			{
				// If a mesh is displayable, it should have at least one material
				if(UStaticMeshComponent** PreviewComponentPtr = MeshComponentsMapping.Find(SelectedComponent))
				{
					NewSelectedPreviewComponents.Add( *PreviewComponentPtr );
				}
			}
		}
	}

	if(NewSelectedPreviewComponents.Num() == 0)
	{
		ClearSelection();
		return;
	}

	SelectedPreviewComponents.Empty( NewSelectedPreviewComponents.Num() );

	SelectedPreviewComponents.Append( MoveTemp( NewSelectedPreviewComponents ) );

	UpdateSelection();
}

void SDataprepEditorViewport::SetActorVisibility(AActor* SceneActor, bool bInVisibility)
{
	TArray< UStaticMeshComponent* > SceneComponents;
	SceneActor->GetComponents< UStaticMeshComponent >(SceneComponents);
	for (UStaticMeshComponent* SceneComponent : SceneComponents)
	{
		UStaticMeshComponent** PreviewComponent = MeshComponentsMapping.Find(SceneComponent);
		if (PreviewComponent)
		{
			(*PreviewComponent)->SetVisibility(bInVisibility);
		}
	}
}

void SDataprepEditorViewport::UpdateSelection()
{
	TSharedPtr<FDataprepEditor> DataprepEditorPtr = DataprepEditor.Pin();

	if(SelectedPreviewComponents.Num() == 0)
	{
		if(DataprepEditorPtr.IsValid())
		{
			DataprepEditorPtr->SetWorldObjectsSelection( TSet<TWeakObjectPtr<UObject>>(), FDataprepEditor::EWorldSelectionFrom::Viewport );
		}

		ClearSelection();
		return;
	}

	// Apply materials. Only selected ones will be affected
	ApplyRenderingMaterial();

	// Update Dataprep editor with new selection
	TSet<TWeakObjectPtr<UObject>> SelectedActors;
	SelectedActors.Empty(SelectedPreviewComponents.Num());

	for(UStaticMeshComponent* SelectedComponent : SelectedPreviewComponents)
	{
		UStaticMeshComponent* SceneMeshComponent = MeshComponentsReverseMapping[SelectedComponent];
		SelectedActors.Emplace(SceneMeshComponent->GetOwner());
	}

	if(DataprepEditorPtr.IsValid())
	{
		DataprepEditorPtr->SetWorldObjectsSelection( MoveTemp(SelectedActors), FDataprepEditor::EWorldSelectionFrom::Viewport );
	}

	SceneViewport->Invalidate();
}

bool SDataprepEditorViewport::IsComponentSelected(const UPrimitiveComponent* InPrimitiveComponent)
{
	UPrimitiveComponent* PrimitiveComponent = const_cast<UPrimitiveComponent*>(InPrimitiveComponent);
	return SelectedPreviewComponents.Contains( Cast<UCustomStaticMeshComponent>(PrimitiveComponent) ) && CurrentSelectionMode == ESelectionModeType::OutlineSelectionMode;
}

void SDataprepEditorViewport::SetRenderingMaterial(ERenderingMaterialType InRenderingMaterialType)
{
	if(RenderingMaterialType != InRenderingMaterialType)
	{
		RenderingMaterialType = InRenderingMaterialType;
		ApplyRenderingMaterial();
	}
}

void SDataprepEditorViewport::ToggleWireframeRenderingMode()
{
	bWireframeRenderingMode = !bWireframeRenderingMode;

	for( const TWeakObjectPtr< UStaticMeshComponent >& PreviewMeshComponent : PreviewMeshComponents )
	{
		if(UCustomStaticMeshComponent* CustomComponent = Cast<UCustomStaticMeshComponent>(PreviewMeshComponent))
		{
			CustomComponent->bForceWireframe = bWireframeRenderingMode;
			PreviewMeshComponent->MarkRenderStateDirty();
		}
	}

	SceneViewport->Invalidate();
}

void SDataprepEditorViewport::SetSelectionMode(ESelectionModeType InSelectionMode)
{
	if(CurrentSelectionMode != InSelectionMode)
	{
		CurrentSelectionMode = InSelectionMode;

		ApplyRenderingMaterial();
	}
}

UMaterialInterface* SDataprepEditorViewport::GetRenderingMaterial(UStaticMeshComponent* PreviewMeshComponent)
{
	switch(RenderingMaterialType)
	{
		case ERenderingMaterialType::XRayRenderingMaterial:
		{
			return XRayMaterial.Get();
		}

		case ERenderingMaterialType::BackFaceRenderingMaterial:
		{
			return BackFaceMaterial.Get();
		}

		case ERenderingMaterialType::PerMeshRenderingMaterial:
		{
			if(UCustomStaticMeshComponent* CustomComponent = Cast<UCustomStaticMeshComponent>(PreviewMeshComponent))
			{
				return PerMeshMaterialInstances[CustomComponent->MeshColorIndex].Get();
			}
		}

		case ERenderingMaterialType::ReflectionRenderingMaterial:
		{
			return ReflectionMaterial.Get();
		}

		case ERenderingMaterialType::OriginalRenderingMaterial:
		default:
		{
			break;
		}
	}

	return nullptr;
}

void SDataprepEditorViewport::ApplyRenderingMaterial()
{
	auto ApplyMaterial = [&](UStaticMeshComponent* PreviewMeshComponent)
	{
		UMaterialInterface* RenderingMaterial = GetRenderingMaterial( PreviewMeshComponent );

		UStaticMeshComponent* SceneMeshComponent = MeshComponentsReverseMapping[PreviewMeshComponent];

		UStaticMesh* StaticMesh = SceneMeshComponent->GetStaticMesh();

		for(int32 Index = 0; Index < StaticMesh->StaticMaterials.Num(); ++Index)
		{
			UMaterialInterface* MaterialInterface = SceneMeshComponent->GetMaterial(Index);

			if(MaterialInterface == nullptr)
			{
				MaterialInterface = StaticMesh->StaticMaterials[Index].MaterialInterface;
			}

			PreviewMeshComponent->SetMaterial( Index, RenderingMaterial ? RenderingMaterial : DisplayMaterialsMap[MaterialInterface].Get() );
		}

		PreviewMeshComponent->MarkRenderStateDirty();
	};

	if(SelectedPreviewComponents.Num() > 0)
	{
		switch(CurrentSelectionMode)
		{
			case ESelectionModeType::XRaySelectionMode:
			{
				// Apply transparent material on all mesh components
				for( const TWeakObjectPtr< UStaticMeshComponent >& PreviewMeshComponentPtr : PreviewMeshComponents )
				{
					if(UStaticMeshComponent* PreviewMeshComponent = PreviewMeshComponentPtr.Get())
					{
						UStaticMesh* StaticMesh = PreviewMeshComponent->GetStaticMesh();

						for(int32 Index = 0; Index < StaticMesh->StaticMaterials.Num(); ++Index)
						{
							PreviewMeshComponent->SetMaterial( Index, TransparentMaterial.Get() );
						}

						PreviewMeshComponent->MarkRenderStateDirty();
					}
				}

				// Apply rendering material only on selected mesh components
				for( UStaticMeshComponent* PreviewMeshComponent : SelectedPreviewComponents )
				{
					ApplyMaterial( PreviewMeshComponent );
				}
			}
			break;

			default:
			case ESelectionModeType::OutlineSelectionMode:
			{
				for( const TWeakObjectPtr< UStaticMeshComponent >& PreviewMeshComponentPtr : PreviewMeshComponents )
				{
					if(UStaticMeshComponent* PreviewMeshComponent = PreviewMeshComponentPtr.Get())
					{
						ApplyMaterial( PreviewMeshComponent );
					}
				}
			}
			break;
		}
	}
	else
	{
		for( const TWeakObjectPtr< UStaticMeshComponent >& PreviewMeshComponentPtr : PreviewMeshComponents )
		{
			if(UStaticMeshComponent* PreviewMeshComponent = PreviewMeshComponentPtr.Get())
			{
				ApplyMaterial( PreviewMeshComponent );
			}
		}
	}

	SceneViewport->Invalidate();
}

void SDataprepEditorViewport::LoadDefaultSettings()
{
	// Disable viewing settings for the time being
	static bool bAllowViewingSettings = false;
	if(!bAllowViewingSettings)
	{
		AssetViewerProfileIndex = 0;
		return;
	}

	// Find index of Dataprep's viewport's settings
	const TCHAR* DataprepViewportSettingProfileName = TEXT("DataprepViewportSetting");

	UAssetViewerSettings* DefaultSettings = UAssetViewerSettings::Get();
	check(DefaultSettings);

	for(int32 Index = 0; Index < DefaultSettings->Profiles.Num(); ++Index)
	{
		if(DefaultSettings->Profiles[Index].ProfileName == DataprepViewportSettingProfileName)
		{
			AssetViewerProfileIndex = Index;
			break;
		}
	}

	// No profile found, create one
	if(AssetViewerProfileIndex == INDEX_NONE)
	{
		FPreviewSceneProfile Profile = DefaultSettings->Profiles[0];

		Profile.bSharedProfile = false;
		Profile.ProfileName = DataprepViewportSettingProfileName;
		AssetViewerProfileIndex = DefaultSettings->Profiles.Num();

		DefaultSettings->Profiles.Add( Profile );
		DefaultSettings->Save();
	}

	// Update the profile with the settings for the project
	FPreviewSceneProfile& DataprepViewportSettingProfile = 	DefaultSettings->Profiles[AssetViewerProfileIndex];

	// Read default settings, tessellation and import, for Datasmith file producer
	const FString DataprepEditorIni = FString::Printf(TEXT("%s%s/%s.ini"), *FPaths::GeneratedConfigDir(), ANSI_TO_TCHAR(FPlatformProperties::PlatformName()), TEXT("DataprepEditor") );

	const TCHAR* ViewportSectionName = TEXT("ViewportSettings");
	if(GConfig->DoesSectionExist( ViewportSectionName, DataprepEditorIni ))
	{
		FString EnvironmentCubeMapPath = GConfig->GetStr( ViewportSectionName, TEXT("EnvironmentCubeMap"), DataprepEditorIni);

		if(EnvironmentCubeMapPath != DataprepViewportSettingProfile.EnvironmentCubeMapPath)
		{
			// Check that the Cube map does exist
			FSoftObjectPath EnvironmentCubeMap( EnvironmentCubeMapPath );
			UObject* LoadedObject = EnvironmentCubeMap.TryLoad();

			while (UObjectRedirector* Redirector = Cast<UObjectRedirector>( LoadedObject ))
			{
				LoadedObject = Redirector->DestinationObject;
			}

			// Good to go, update the profile's related parameters
			if( Cast<UTextureCube>( LoadedObject ) != nullptr )
			{
				DataprepViewportSettingProfile.EnvironmentCubeMapPath = EnvironmentCubeMapPath;
				DataprepViewportSettingProfile.EnvironmentCubeMap = LoadedObject;
			}
		}
	}
}

//
// FDataprepEditorViewportClient Class
//

FDataprepEditorViewportClient::FDataprepEditorViewportClient(const TSharedRef<SEditorViewport>& InDataprepEditorViewport, const TSharedRef<FAdvancedPreviewScene>& InPreviewScene)
	: FEditorViewportClient( nullptr, &InPreviewScene.Get(), InDataprepEditorViewport )
	, AdvancedPreviewScene( nullptr )
	, DataprepEditorViewport( nullptr )
{
	EngineShowFlags.SetSelectionOutline( true );

	if (EditorViewportWidget.IsValid())
	{
		DataprepEditorViewport = static_cast<SDataprepEditorViewport*>( EditorViewportWidget.Pin().Get() );
	}

	AdvancedPreviewScene = static_cast<FAdvancedPreviewScene*>(PreviewScene);
	check(AdvancedPreviewScene);

	AdvancedPreviewScene->SetProfileIndex( SDataprepEditorViewport::AssetViewerProfileIndex );
}

bool FDataprepEditorViewportClient::InputKey(FViewport * InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad)
{
	bool bHandled = false;
	
	// #ueent_todo: Put code for specific handling

	return bHandled ? true : FEditorViewportClient::InputKey(InViewport, ControllerId, Key, Event, AmountDepressed, bGamepad );
}

void FDataprepEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	if(DataprepEditorViewport != nullptr)
	{
		if(HitProxy != nullptr)
		{
			if( HitProxy->IsA( HActor::StaticGetType() ) )
			{
				// A static mesh component has been selected
				if( UStaticMeshComponent* Component = Cast<UStaticMeshComponent>( const_cast<UPrimitiveComponent*>(((HActor*)HitProxy)->PrimComponent )) )
				{
					// A static mesh component part of the ones to preview has been selected
					if( DataprepEditorViewport->IsAPreviewComponent(Component) )
					{
						// Applies the selection logic
						if(Key == EKeys::LeftMouseButton)
						{
							if( DataprepEditorViewport->IsSelected(Component) )
							{
								if( IsCtrlPressed() || IsShiftPressed() )
								{
									DataprepEditorViewport->RemoveFromSelection(Component);
								}
							}
							else
							{
								if( IsCtrlPressed() || IsShiftPressed() )
								{
									DataprepEditorViewport->AddToSelection(Component);
								}
								else
								{
									DataprepEditorViewport->SetSelection(Component);
								}
							}

							return;
						}
						// A contextual menu is requested
						else if(Key == EKeys::RightMouseButton)
						{
							// #ueent_todo: Display contextual menu
						}
					}
				}
			}
		}
		// No geometry picked, de-select all
		else if(Key == EKeys::LeftMouseButton)
		{
			DataprepEditorViewport->ClearSelection(true);
			return;
		}
	}

	// Nothing to be done, delegate to base class
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY );
}

void FDataprepEditorViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FBoxSphereBounds SphereBounds(DataprepEditorViewport->SceneBounds);
	float CurrentScreenSize = ComputeBoundsScreenSize(SphereBounds.Origin, SphereBounds.SphereRadius, View);

	FNumberFormattingOptions FormatOptions;
	FormatOptions.MinimumFractionalDigits = 3;
	FormatOptions.MaximumFractionalDigits = 6;
	FormatOptions.MaximumIntegralDigits = 6;

	DataprepEditorViewport->UpdateScreenSizeText(
		FText::Format( LOCTEXT( "ScreenSize_F", "Current Screen Size:  {0}"), FText::AsNumber(CurrentScreenSize, &FormatOptions)));

	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);
}

void FDataprepEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

#ifdef VIEWPORT_EXPERIMENTAL
	if(DataprepEditorViewport->IsShowOrientedBoxOn())
	{
		for(TWeakObjectPtr< UStaticMeshComponent >& ComponentPtr : DataprepEditorViewport->PreviewMeshComponents)
		{
			if(UCustomStaticMeshComponent* MeshComponent = Cast<UCustomStaticMeshComponent>(ComponentPtr.Get()))
			{
				FPrototypeOrientedBox& Box = MeshComponent->MeshProperties;
				FTransform Transform = MeshComponent->GetComponentToWorld();

				FVector Positions[8];

				Positions[0] = Transform.TransformPosition(Box.Center + (Box.HalfExtents.X * Box.LocalXAxis) + (Box.HalfExtents.Y * Box.LocalYAxis) + (Box.HalfExtents.Z * Box.LocalZAxis));
				Positions[1] = Transform.TransformPosition(Box.Center - (Box.HalfExtents.X * Box.LocalXAxis) + (Box.HalfExtents.Y * Box.LocalYAxis) + (Box.HalfExtents.Z * Box.LocalZAxis));
				Positions[2] = Transform.TransformPosition(Box.Center - (Box.HalfExtents.X * Box.LocalXAxis) - (Box.HalfExtents.Y * Box.LocalYAxis) + (Box.HalfExtents.Z * Box.LocalZAxis));
				Positions[3] = Transform.TransformPosition(Box.Center + (Box.HalfExtents.X * Box.LocalXAxis) - (Box.HalfExtents.Y * Box.LocalYAxis) + (Box.HalfExtents.Z * Box.LocalZAxis));
				Positions[4] = Transform.TransformPosition(Box.Center + (Box.HalfExtents.X * Box.LocalXAxis) + (Box.HalfExtents.Y * Box.LocalYAxis) - (Box.HalfExtents.Z * Box.LocalZAxis));
				Positions[5] = Transform.TransformPosition(Box.Center - (Box.HalfExtents.X * Box.LocalXAxis) + (Box.HalfExtents.Y * Box.LocalYAxis) - (Box.HalfExtents.Z * Box.LocalZAxis));
				Positions[6] = Transform.TransformPosition(Box.Center - (Box.HalfExtents.X * Box.LocalXAxis) - (Box.HalfExtents.Y * Box.LocalYAxis) - (Box.HalfExtents.Z * Box.LocalZAxis));
				Positions[7] = Transform.TransformPosition(Box.Center + (Box.HalfExtents.X * Box.LocalXAxis) - (Box.HalfExtents.Y * Box.LocalYAxis) - (Box.HalfExtents.Z * Box.LocalZAxis));

				int32 Indices[24] = { 0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7 };

				for(int32 Index = 0; Index < 24; Index += 2)
				{
					PDI->DrawLine( Positions[Indices[Index + 0]], Positions[Indices[Index + 1]], MeshComponent->bShoudlBeInstanced ? FColor( 255, 0, 0 ) : FColor( 255, 255, 0 ), SDPG_World );
				}

				FVector TransformedCenter = Transform.TransformPosition(Box.Center);
				PDI->DrawLine( TransformedCenter, Transform.TransformPosition(Box.Center + /*Box.Moments.X*/10.0f * Box.LocalXAxis), FColor( 0, 0, 255 ), SDPG_World );
				PDI->DrawLine( TransformedCenter, Transform.TransformPosition(Box.Center + /*Box.Moments.Y*/10.0f * Box.LocalYAxis), FColor( 0, 0, 255 ), SDPG_World );
				PDI->DrawLine( TransformedCenter, Transform.TransformPosition(Box.Center + /*Box.Moments.Z*/10.0f * Box.LocalZAxis), FColor( 0, 0, 255 ), SDPG_World );
			}
		}
	}
#endif
}

void FDataprepEditorViewportClient::Draw(FViewport* InViewport, FCanvas* InCanvas)
{
	FEditorViewportClient::Draw(InViewport, InCanvas);
}

///////////////////////////////////////////////////////////
// SDataprepEditorViewportToolbar

void SDataprepEditorViewportToolbar::Construct(const FArguments& InArgs, TSharedPtr<class ICommonEditorViewportToolbarInfoProvider> InInfoProvider)
{
	// Create default widgets in toolbar: View, 
	SCommonEditorViewportToolbarBase::Construct(SCommonEditorViewportToolbarBase::FArguments(), InInfoProvider);
}

TSharedRef<SWidget> SDataprepEditorViewportToolbar::GenerateShowMenu() const
{
	GetInfoProvider().OnFloatingButtonClicked();

	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder ShowMenuBuilder(bInShouldCloseWindowAfterMenuSelection, ViewportRef->GetCommandList());
	{
		ShowMenuBuilder.AddMenuEntry(FDataprepEditorViewportCommands::Get().SetShowGrid);
		ShowMenuBuilder.AddMenuEntry(FDataprepEditorViewportCommands::Get().SetShowBounds);
	}

	// #ueent_todo: Look at SAnimViewportToolBar::GenerateShowMenu in SAnimViewportToolBar.cpp for adding ShowFlagFilter to the Show menu

	return ShowMenuBuilder.MakeWidget();
}

void SDataprepEditorViewportToolbar::ExtendLeftAlignedToolbarSlots(TSharedPtr<SHorizontalBox> MainBoxPtr, TSharedPtr<SViewportToolBar> ParentToolBarPtr) const
{
	const FMargin ToolbarSlotPadding(2.0f, 2.0f);
	const FMargin ToolbarButtonPadding(2.0f, 0.0f);
	static const FName DefaultForegroundName("DefaultForeground");

	if (!MainBoxPtr.IsValid())
	{
		return;
	}

	MainBoxPtr->AddSlot()
	.AutoWidth()
	.Padding(ToolbarSlotPadding)
	[
		SNew(SEditorViewportToolbarMenu)
		.Label( LOCTEXT("DataprepEditor_Rendering", "Rendering") )
		.ToolTipText(LOCTEXT("DataprepEditor_RenderingTooltip", "Rendering Options. Use this enable/disable the rendering of types of meshes."))
		.ParentToolBar( ParentToolBarPtr )
		.Cursor(EMouseCursor::Default)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("RenderingMenuButton")))
		.OnGetMenuContent( this, &SDataprepEditorViewportToolbar::GenerateRenderingMenu)
	];

#ifdef VIEWPORT_EXPERIMENTAL
	MainBoxPtr->AddSlot()
	.AutoWidth()
	.Padding(ToolbarSlotPadding)
	[
		SNew(SEditorViewportToolbarMenu)
		.Label( LOCTEXT("DataprepEditor_Experimental", "Experimental") )
		.ToolTipText(LOCTEXT("DataprepEditor_ExperimentalTooltip", "Experimental viewing modes or actions."))
		.ParentToolBar( ParentToolBarPtr )
		.Cursor(EMouseCursor::Default)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ExperimentalMenuButton")))
		.OnGetMenuContent( this, &SDataprepEditorViewportToolbar::GenerateExperimentalMenu)
	];
#endif
}

bool SDataprepEditorViewportToolbar::IsViewModeSupported(EViewModeIndex ViewModeIndex) const
{
	// #ueent_todo: Eliminate view mode we do not want
	return true;
}

TSharedRef<SWidget> SDataprepEditorViewportToolbar::GenerateRenderingMenu() const
{
	TSharedPtr<FExtender> MenuExtender = GetInfoProvider().GetExtenders();
	TSharedPtr<SEditorViewport> Viewport = GetInfoProvider().GetViewportWidget();
	TSharedPtr<FUICommandList> CommandList = Viewport->GetCommandList();

	const FDataprepEditorViewportCommands& Commands = FDataprepEditorViewportCommands::Get();

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, CommandList, MenuExtender);

	MenuBuilder.PushCommandList( CommandList.ToSharedRef() );
	MenuBuilder.PushExtender( MenuExtender.ToSharedRef() );
	{
		MenuBuilder.BeginSection("DataprepEditorViewportRenderingMenu", LOCTEXT("DataprepEditor_RenderingMaterial", "Materials"));
		MenuBuilder.AddMenuEntry(Commands.ApplyOriginalMaterial);
		MenuBuilder.AddMenuEntry(Commands.ApplyBackFaceMaterial);
#ifdef VIEWPORT_EXPERIMENTAL
		MenuBuilder.AddMenuEntry(Commands.ApplyXRayMaterial);
#endif
		MenuBuilder.AddMenuEntry(Commands.ApplyPerMeshMaterial);
		MenuBuilder.AddMenuEntry(Commands.ApplyReflectionMaterial);
		MenuBuilder.EndSection();
		MenuBuilder.BeginSection("DataprepEditorViewportRenderingMenu", LOCTEXT("DataprepEditor_SelectionMode", "Selection"));
		MenuBuilder.AddMenuEntry(Commands.ApplyOutlineSelection);
		MenuBuilder.AddMenuEntry(Commands.ApplyXRaySelection);
		MenuBuilder.EndSection();
		MenuBuilder.BeginSection("DataprepEditorViewportRenderingMenu", LOCTEXT("DataprepEditor_RenderingMode", "Modes"));
		MenuBuilder.AddMenuEntry(Commands.ApplyWireframeMode);
		MenuBuilder.EndSection();
	}

	MenuBuilder.PopCommandList();
	MenuBuilder.PopExtender();

	return MenuBuilder.MakeWidget();
}

#ifdef VIEWPORT_EXPERIMENTAL
TSharedRef<SWidget> SDataprepEditorViewportToolbar::GenerateExperimentalMenu() const
{
	TSharedPtr<FExtender> MenuExtender = GetInfoProvider().GetExtenders();
	TSharedPtr<SEditorViewport> Viewport = GetInfoProvider().GetViewportWidget();
	TSharedPtr<FUICommandList> CommandList = Viewport->GetCommandList();

	const FDataprepEditorViewportCommands& Commands = FDataprepEditorViewportCommands::Get();

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, CommandList, MenuExtender);

	MenuBuilder.PushCommandList( CommandList.ToSharedRef() );
	MenuBuilder.PushExtender( MenuExtender.ToSharedRef() );
	{
		MenuBuilder.BeginSection("DataprepEditorViewportExperimentalMenu", LOCTEXT("DataprepEditor_Experimental_Viewing", "Viewing"));
		MenuBuilder.AddMenuEntry(Commands.ShowOOBs);
		MenuBuilder.EndSection();
		MenuBuilder.BeginSection("DataprepEditorViewportExperimentalMenu", LOCTEXT("DataprepEditor_Experimental_Actions", "Actions"));
		MenuBuilder.EndSection();
	}

	MenuBuilder.PopCommandList();
	MenuBuilder.PopExtender();

	return MenuBuilder.MakeWidget();
}

void SDataprepEditorViewport::ToggleShowOrientedBox()
{
	bShowOrientedBox = !bShowOrientedBox;

	SceneViewport->Invalidate();
}
#endif

//////////////////////////////////////////////////////////////////////////
// FDataprepEditorViewportCommands

void FDataprepEditorViewportCommands::RegisterCommands()
{
	// Show menu
	UI_COMMAND(SetShowGrid, "Grid", "Displays the viewport grid.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(SetShowBounds, "Bounds", "Toggles display of the bounds of the selected component.", EUserInterfaceActionType::ToggleButton, FInputChord());

	// Rendering Material
	UI_COMMAND(ApplyOriginalMaterial, "None", "Display all meshes with original materials.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(ApplyBackFaceMaterial, "BackFace", "Display front face and back face of triangles with different colors.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(ApplyXRayMaterial, "XRay", "Use XRay material to render meshes.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(ApplyPerMeshMaterial, "MultiColored", "Assign a different color for each rendered mesh.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(ApplyReflectionMaterial, "ReflectionLines", "Use reflective material to show lines of reflection.", EUserInterfaceActionType::RadioButton, FInputChord());
	
	// Selection Mode
	UI_COMMAND(ApplyOutlineSelection, "Outline", "Outline selected meshes with a colored contour.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(ApplyXRaySelection, "XRay", "Use XRay material on non selected meshes.", EUserInterfaceActionType::RadioButton, FInputChord());

	// Rendering Mode
	UI_COMMAND(ApplyWireframeMode, "Wireframe", "Display all meshes in wireframe.", EUserInterfaceActionType::ToggleButton, FInputChord());

#ifdef VIEWPORT_EXPERIMENTAL
	// Experimental
	UI_COMMAND(ShowOOBs, "OrientedBox", "Display object oriented bounding boxes.", EUserInterfaceActionType::ToggleButton, FInputChord());
#endif
}

// Copied from UStaticMeshComponent::CreateSceneProxy
FPrimitiveSceneProxy* UCustomStaticMeshComponent::CreateSceneProxy()
{
	if (GetStaticMesh() == nullptr || GetStaticMesh()->RenderData == nullptr)
	{
		return nullptr;
	}

	const TIndirectArray<FStaticMeshLODResources>& LODResources = GetStaticMesh()->RenderData->LODResources;
	if (LODResources.Num() == 0	|| LODResources[FMath::Clamp<int32>(GetStaticMesh()->MinLOD.Default, 0, LODResources.Num()-1)].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() == 0)
	{
		return nullptr;
	}
	LLM_SCOPE(ELLMTag::StaticMesh);

	FPrimitiveSceneProxy* Proxy = ::new FStaticMeshSceneProxyExt(this, false);
#if STATICMESH_ENABLE_DEBUG_RENDERING
	SendRenderDebugPhysics(Proxy);
#endif

	return Proxy;
}

namespace DataprepEditor3DPreviewUtils
{
	void BuildStaticMeshes(TSet<UStaticMesh*>& StaticMeshes, TArray<UStaticMesh*>& BuiltMeshes)
	{
		ViewportDebug::FTimeLogger LapTimeLogger( TEXT("Building static meshes") );

		BuiltMeshes.Empty( StaticMeshes.Num() );

		TArray< TArray<FMeshBuildSettings> > StaticMeshesSettings;
		StaticMeshesSettings.Reserve( StaticMeshes.Num() );

		for(UStaticMesh* StaticMesh : StaticMeshes)
		{
			if(StaticMesh && (!StaticMesh->RenderData.IsValid() || !StaticMesh->RenderData->IsInitialized()))
			{
				BuiltMeshes.Add( StaticMesh );
			}
		}

		if(BuiltMeshes.Num() > 0)
		{
			FScopedSlowTask SlowTask( BuiltMeshes.Num(), LOCTEXT("BuildStaticMeshes_Title", "Building static meshes ...") );
			SlowTask.MakeDialog(false);

			auto ProgressFunction = [&](UStaticMesh* StaticMesh)
			{
				SlowTask.EnterProgressFrame(1.f, FText::FromString(FString::Printf(TEXT("Building Static Mesh %s ..."), *StaticMesh->GetName())));
				return true;
			};

			// Start with the biggest mesh first to help balancing tasks on threads
			BuiltMeshes.Sort(
				[](const UStaticMesh& Lhs, const UStaticMesh& Rhs) 
			{ 
				int32 LhsVerticesNum = Lhs.IsMeshDescriptionValid(0) ? Lhs.GetMeshDescription(0)->Vertices().Num() : 0;
				int32 RhsVerticesNum = Rhs.IsMeshDescriptionValid(0) ? Rhs.GetMeshDescription(0)->Vertices().Num() : 0;

				return LhsVerticesNum > RhsVerticesNum;
			}
			);

			//Cache the BuildSettings and update them before building the meshes.
			for (UStaticMesh* StaticMesh : BuiltMeshes)
			{
				TArray<FStaticMeshSourceModel>& SourceModels = StaticMesh->GetSourceModels();
				TArray<FMeshBuildSettings> BuildSettings;
				BuildSettings.Reserve(SourceModels.Num());

				for (FStaticMeshSourceModel& SourceModel : SourceModels)
				{
					BuildSettings.Add(SourceModel.BuildSettings);

					SourceModel.BuildSettings.bGenerateLightmapUVs = false;
					SourceModel.BuildSettings.bRecomputeNormals = false;
					SourceModel.BuildSettings.bRecomputeTangents = false;
					SourceModel.BuildSettings.bBuildAdjacencyBuffer = false;
					SourceModel.BuildSettings.bBuildReversedIndexBuffer = false;
				}

				StaticMeshesSettings.Add(MoveTemp(BuildSettings));				
			}

			// Disable warnings from LogStaticMesh. Not useful
			ELogVerbosity::Type PrevLogStaticMeshVerbosity = LogStaticMesh.GetVerbosity();
			LogStaticMesh.SetVerbosity( ELogVerbosity::Error );

			UStaticMesh::BatchBuild(BuiltMeshes, true, ProgressFunction);

			// Restore LogStaticMesh verbosity
			LogStaticMesh.SetVerbosity( PrevLogStaticMeshVerbosity );

			for(int32 Index = 0; Index < BuiltMeshes.Num(); ++Index)
			{
				UStaticMesh* StaticMesh = BuiltMeshes[Index];
				TArray<FMeshBuildSettings>& PrevBuildSettings = StaticMeshesSettings[Index];

				TArray<FStaticMeshSourceModel>& SourceModels = StaticMesh->GetSourceModels();

				for(int32 SourceModelIndex = 0; SourceModelIndex < SourceModels.Num(); ++SourceModelIndex)
				{
					SourceModels[SourceModelIndex].BuildSettings = PrevBuildSettings[SourceModelIndex];
				}

				for ( FStaticMeshLODResources& LODResources : StaticMesh->RenderData->LODResources )
				{
					LODResources.bHasColorVertexData = true;
				}
			}
		}
	}

	// Copied from DatasmithImporterImpl::CompileMaterial
	void CompileMaterials(const TArray< UMaterialInterface* > Materials)
	{
		FMaterialUpdateContext MaterialUpdateContext;

		for(UMaterialInterface* MaterialInterface : Materials)
		{
			MaterialUpdateContext.AddMaterialInterface( MaterialInterface );

			if(UMaterialInstanceConstant* ConstantMaterialInstance = Cast< UMaterialInstanceConstant >(MaterialInterface))
			{
				// If BlendMode override property has been changed, make sure this combination of the parent material is compiled
				if ( ConstantMaterialInstance->BasePropertyOverrides.bOverride_BlendMode == true )
				{
					ConstantMaterialInstance->ForceRecompileForRendering();
				}
				else
				{
					// If a static switch is overridden, we need to recompile
					FStaticParameterSet StaticParameters;
					ConstantMaterialInstance->GetStaticParameterValues( StaticParameters );

					for ( FStaticSwitchParameter& Switch : StaticParameters.StaticSwitchParameters )
					{
						if ( Switch.bOverride )
						{
							ConstantMaterialInstance->ForceRecompileForRendering();
							break;
						}
					}
				}

				ConstantMaterialInstance->PreEditChange( nullptr );
				ConstantMaterialInstance->PostEditChange();
			}
		}
	}

	void FindMeshComponents(const AActor * InActor, TArray<UStaticMeshComponent*>& MeshComponents, bool bRecursive )
	{
		if(InActor == nullptr)
		{
			return;
		}

		TArray<UStaticMeshComponent*> StaticMeshComponents;
		InActor->GetComponents<UStaticMeshComponent>( StaticMeshComponents );
		for(UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
		{
			MeshComponents.Add( StaticMeshComponent );
		}

		if(bRecursive)
		{
			TArray<AActor*> Children;
			InActor->GetAttachedActors( Children );
			for(AActor* ChildActor : Children)
			{
				FindMeshComponents( ChildActor, MeshComponents, bRecursive );
			}
		}
	}

#ifdef VIEWPORT_EXPERIMENTAL
	// Area using 3d positions
	// Area is half length of the normal vector
	float CalculateTriangleArea(const FVector& P0, const FVector& P1, const FVector& P2)
	{
		FVector Normal = ( P1 - P2 ) ^ ( P0 - P2 );
		return Normal.Size() * 0.5f;
	}

	// Area using 2d positions
	// Heron's formula: using length of the 3 sides
	float CalculateTriangleArea(const FVector2D& P0, const FVector2D& P1, const FVector2D& P2)
	{
		return FMath::Sqrt( FMath::Abs( ( P1 - P2 ) ^ ( P0 - P2 ) ) ) * 0.5f;
	}

	// add exposure to colors to make them pop
	void ExposureCompensation(FVector4& in)
	{
		for (int i = 0; i < 3; i++)
		{
			in[i] = FMath::Pow(in[i], 2.4);
		}
	}

	void ComputeUVStretching(FStaticMeshLODResources& Resource)
	{
		if(!bComputeUVStretching)
		{
			return;
		}

		const FIndexArrayView& Indices = Resource.IndexBuffer.GetArrayView();
		FStaticMeshVertexBuffer& VertexBuffer = Resource.VertexBuffers.StaticMeshVertexBuffer;
		const FPositionVertexBuffer& Positions = Resource.VertexBuffers.PositionVertexBuffer;

		const int32 UVIndex = 0;
		const int32 NumTriangles = Indices.Num() / 3;
		const int32 NumVertices = (int32)VertexBuffer.GetNumVertices();

		TArray<FLinearColor> Colors;
		Colors.AddZeroed( NumVertices );

		float AverageArea3D = 0.0f;
		float AverageArea2D = 0.0f;

		for(int32 TriangleIndex = 0, Offset = 0; TriangleIndex < NumTriangles; ++TriangleIndex, Offset += 3)
		{
			const int32 Idx0 = Indices[Offset + 0];
			const int32 Idx1 = Indices[Offset + 1];
			const int32 Idx2 = Indices[Offset + 2];

			// 2d
			float Area2d = CalculateTriangleArea(
				VertexBuffer.GetVertexUV_Typed<EStaticMeshVertexUVType::Default>(Idx0, UVIndex), 
				VertexBuffer.GetVertexUV_Typed<EStaticMeshVertexUVType::Default>(Idx1, UVIndex),
				VertexBuffer.GetVertexUV_Typed<EStaticMeshVertexUVType::Default>(Idx2, UVIndex)
			);

			// 3d
			float Area3d = CalculateTriangleArea( Positions.VertexPosition(Idx0), Positions.VertexPosition(Idx1), Positions.VertexPosition(Idx2) );

			{
				FLinearColor& Color = Colors[Idx0];
				Color.R += Area2d;
				Color.G += Area3d;
			}

			{
				FLinearColor& Color = Colors[Idx1];
				Color.R += Area2d;
				Color.G += Area3d;
			}

			{
				FLinearColor& Color = Colors[Idx2];
				Color.R += Area2d;
				Color.G += Area3d;
			}

			// accumulate 
			AverageArea2D += Area2d;
			AverageArea3D += Area3d;
		}

		// average
		AverageArea2D = AverageArea2D / NumTriangles;
		AverageArea3D = AverageArea3D / NumTriangles;

		//----------------------------------------------------------------------------------
		// Step 2: Calculate distortion value per vert Instances
		//		2d == 3d  -> optimal (white)
		//		2d > 3d   -> compression (blue)
		//		2d < 3d   -> stretching (red)
		float DistortionRatioMin = 1.0f;
		float DistortionRatioMax = 1.0f;

		for(int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			FLinearColor& Color = Colors[VertexIndex];

			float Area2D = Color.R;
			float Area3D = Color.G;
			if(Area2D > SMALL_NUMBER && Area3D > SMALL_NUMBER )
			{
				Area2D /= AverageArea2D;
				Area3D /= AverageArea3D;
				const float DistortionRatio = Area2D / Area3D;

				if(DistortionRatioMin > DistortionRatio)
				{
					DistortionRatioMin = DistortionRatio;
				}

				if(DistortionRatioMax < DistortionRatio)
				{
					DistortionRatioMax = DistortionRatio;
				}

				Color.R = Area2D;
				Color.G = Area3D;
				Color.A = DistortionRatio;
			}
		}

		DistortionRatioMin = 1.0f / DistortionRatioMin;

		float DistortionStrectchRatioRange = DistortionRatioMax - 1.0f;
		float DistortionShrinkRatioRange = DistortionRatioMin - 1.0f;
		for(int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			FLinearColor& Color = Colors[VertexIndex];
			const float DistortionRatio = Color.A;

			if(DistortionRatio > SMALL_NUMBER)
			{
				FLinearColor DistortionColor(1, 1, 1, 1);
				if(DistortionRatio < 1.0f)
				{
					const float D_Norm = ( (1.0f / DistortionRatio) - 1.0f) / DistortionShrinkRatioRange;
					DistortionColor = FMath::Lerp(FLinearColor::Red, FLinearColor::White, D_Norm);
				}
				else
				{
					const float D_Norm = ( DistortionRatio - 1.0f) / DistortionStrectchRatioRange;
					DistortionColor = FMath::Lerp(FLinearColor::Blue, FLinearColor::White, D_Norm);
				}

				Color = DistortionColor;
			}
		}

		//----------------------------------------------------------------------------------
		// Step 3 : Quadify colors 
		// Average colors of every 2 triangles...
		// Gives a faceted look
		//int QuadItr = 0;
		//TArray<int32> QuadIndexes;
		//TArray<FVector4> OutQuadColors;
		//OutQuadColors.AddZeroed(VertexInstancesCount);

		//for (const FPolygonID PolygonID : MeshDescription.Polygons().GetElementIDs())
		//{
		//	const TArray<FMeshTriangle>& MeshTriangles = MeshDescription.GetPolygonTriangles(PolygonID);
		//	for (const FMeshTriangle& MeshTriangle : MeshTriangles)
		//	{
		//		QuadIndexes.Add(MeshTriangle.GetVertexInstanceID(0).GetValue());
		//		QuadIndexes.Add(MeshTriangle.GetVertexInstanceID(1).GetValue());
		//		QuadIndexes.Add(MeshTriangle.GetVertexInstanceID(2).GetValue());
		//	}

		//	QuadItr++;
		//	if (QuadItr % 2 == 0)
		//	{
		//		QuadItr = 0;
		//		FVector4 AverageColor(0, 0, 0, 0);
		//		for (int index : QuadIndexes)
		//		{
		//			AverageColor += OutColors[index];
		//		}
		//		AverageColor = AverageColor / QuadIndexes.Num();

		//		for (int index : QuadIndexes)
		//		{
		//			OutQuadColors[index] = AverageColor;
		//		}
		//		QuadIndexes.Empty();
		//	}
		//}

		//// last one
		//if (QuadItr==1)
		//{
		//	for (int index : QuadIndexes)
		//	{
		//		OutQuadColors[index] = OutColors[index];
		//	}
		//}

		//----------------------------------------------------------------------------------
		// Smoother
		//for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
		//{
		//	FMeshVertex vert = MeshDescription.GetVertex(VertexID);
		//	TArray<FVertexInstanceID> vertInstances = vert.VertexInstanceIDs;
		//	FVector4 AverageColor(0, 0, 0, 0);

		//	// accumulate
		//	for (const FVertexInstanceID& VertexInstanceID : vertInstances)
		//	{
		//		AverageColor += OutQuadColors[VertexInstanceID.GetValue()];
		//	}

		//	// average
		//	AverageColor = AverageColor / vertInstances.Num();

		//	// save
		//	for (const FVertexInstanceID& VertexInstanceID : vertInstances)
		//	{
		//		OutColors[VertexInstanceID.GetValue()] = AverageColor;
		//	}
		//}


		//----------------------------------------------------------------------------------
		// Exposure compensation
		//for (FVector4& color : OutColors)
		//{
		//	ExposureCompensation(color);
		//}
		FColorVertexBuffer& VertexColors = Resource.VertexBuffers.ColorVertexBuffer;
		for(int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			FLinearColor& DistortionColor = Colors[VertexIndex];

			//DistortionColor.R = FMath::Pow( DistortionColor.R, 2.4f );
			//DistortionColor.G = FMath::Pow( DistortionColor.G, 2.4f );
			//DistortionColor.B = FMath::Pow( DistortionColor.B, 2.4f );

			VertexColors.VertexColor(VertexIndex) = DistortionColor.ToFColor(true);
		}
	}
#endif
}

#undef LOCTEXT_NAMESPACE
