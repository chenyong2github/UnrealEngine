// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshEditingContext.h"
#include "StaticMeshAdapter.h"
#include "StaticMeshEditorAssetContainer.h"

#include "EditableMesh.h"
#include "EditableMesh/EditableStaticMeshAdapter.h"
#include "EditableMeshFactory.h"
#include "EditableMeshTypes.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "EditorModeManager.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "IStaticMeshEditor.h"
#include "IViewportInteractionModule.h"
#include "StaticMeshAttributes.h"
#include "OverlayComponent.h"
#include "StaticMeshEditorModule.h"
#include "ViewportWorldInteraction.h"
#include "WireframeMeshComponent.h"

#define LOCTEXT_NAMESPACE "StaticMeshEditorExtensionToolbar"

FEditableMeshCache* FEditableMeshCache::EditableMeshCacheSingleton = nullptr;

const IConsoleVariable* HoveredSizeBias = IConsoleManager::Get().FindConsoleVariable(TEXT( "MeshEd.HoveredSizeBias" ));
const IConsoleVariable* SelectedSizeBias = IConsoleManager::Get().FindConsoleVariable(TEXT( "MeshEd.SelectedSizeBias" ));

namespace MeshEditingContext
{
	static FAutoConsoleVariable OverlayHoverDistance(TEXT("MeshEditing.OverlayHoverDistance"), 0.01f, TEXT("Distance of overlay triangle to hover above element"));

	void SetEditableMeshDescription(UEditableMesh* EditableMesh, UStaticMeshComponent* StaticMeshComponent, int32 LODIndex)
	{
		if (EditableMesh == nullptr || StaticMeshComponent == nullptr || LODIndex < 0)
		{
			return;
		}

		// Point EditableMesh's MeshDescription to StaticMesh's
		FMeshDescription* EditableMeshDescription = EditableMesh->GetMeshDescription();
		FMeshDescription* MeshDescription = StaticMeshComponent->GetStaticMesh()->GetMeshDescription(LODIndex);
		if (MeshDescription == EditableMeshDescription)
		{
			return;
		}

		UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();

		// Register additional attributes required by features modifying EditableMesh
		MeshDescription->PolygonAttributes().RegisterAttribute<FVector>( MeshAttribute::Polygon::Normal, 1, FVector::ZeroVector, EMeshAttributeFlags::Transient );
		MeshDescription->PolygonAttributes().RegisterAttribute<FVector>( MeshAttribute::Polygon::Tangent, 1, FVector::ZeroVector, EMeshAttributeFlags::Transient );
		MeshDescription->PolygonAttributes().RegisterAttribute<FVector>( MeshAttribute::Polygon::Binormal, 1, FVector::ZeroVector, EMeshAttributeFlags::Transient );
		MeshDescription->PolygonAttributes().RegisterAttribute<FVector>( MeshAttribute::Polygon::Center, 1, FVector::ZeroVector, EMeshAttributeFlags::Transient );
		MeshDescription->PolygonGroupAttributes().RegisterAttribute<FName>( MeshAttribute::PolygonGroup::MaterialAssetName );
		MeshDescription->PolygonGroupAttributes().RegisterAttribute<bool>( MeshAttribute::PolygonGroup::EnableCollision );
		MeshDescription->PolygonGroupAttributes().RegisterAttribute<bool>( MeshAttribute::PolygonGroup::CastShadow );

		// Match EditableMesh material asset name with material slot names
		TPolygonGroupAttributesConstRef<FName> SlotNames = MeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>( MeshAttribute::PolygonGroup::ImportedMaterialSlotName );
		TPolygonGroupAttributesRef<FName> AssetNames = MeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>( MeshAttribute::PolygonGroup::MaterialAssetName );
		TPolygonGroupAttributesRef<bool> EnableCollisions = MeshDescription->PolygonGroupAttributes().GetAttributesRef<bool>( MeshAttribute::PolygonGroup::EnableCollision );
		TPolygonGroupAttributesRef<bool> CastShadows = MeshDescription->PolygonGroupAttributes().GetAttributesRef<bool>( MeshAttribute::PolygonGroup::CastShadow );

		TPolygonGroupAttributesConstRef<FName> SrcSlotNames = EditableMeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>( MeshAttribute::PolygonGroup::ImportedMaterialSlotName );
		TPolygonGroupAttributesConstRef<FName> SrcAssetNames = EditableMeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>( MeshAttribute::PolygonGroup::MaterialAssetName );
		TPolygonGroupAttributesConstRef<bool> SrcEnableCollisions = EditableMeshDescription->PolygonGroupAttributes().GetAttributesRef<bool>( MeshAttribute::PolygonGroup::EnableCollision );
		TPolygonGroupAttributesConstRef<bool> SrcCastShadows = EditableMeshDescription->PolygonGroupAttributes().GetAttributesRef<bool>( MeshAttribute::PolygonGroup::CastShadow );

		for( const FPolygonGroupID PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs() )
		{
			FPolygonGroupID MatchingPolygonGroup(FPolygonGroupID::Invalid);
			for( const FPolygonGroupID SrcPolygonGroupID : EditableMeshDescription->PolygonGroups().GetElementIDs() )
			{
				if( SlotNames[ PolygonGroupID ] == SrcSlotNames[ SrcPolygonGroupID ] )
				{
					MatchingPolygonGroup = SrcPolygonGroupID;
					break;
				}
			}

			if( MatchingPolygonGroup != FPolygonGroupID::Invalid )
			{
				AssetNames[ PolygonGroupID ] = SrcAssetNames[ MatchingPolygonGroup ];
				EnableCollisions[ PolygonGroupID ] = SrcEnableCollisions[ MatchingPolygonGroup ];
				CastShadows[ PolygonGroupID ] = SrcCastShadows[ MatchingPolygonGroup ];
			}
		}

		{
			// Compute the polygon attributes that are used by the StaticMeshEditor
			// Taken from UEditableMesh::GeneratePolygonTangentsAndNormals
			// TODO: Unify the code with FMeshDescriptionOperations::ConvertFromRawMesh 
			TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

			TVertexInstanceAttributesConstRef<FVector2D> VertexUVs = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

			TPolygonAttributesRef<FVector> PolygonNormals = MeshDescription->PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Normal);
			TPolygonAttributesRef<FVector> PolygonTangents = MeshDescription->PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Tangent);
			TPolygonAttributesRef<FVector> PolygonBinormals = MeshDescription->PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Binormal);
			TPolygonAttributesRef<FVector> PolygonCenters = MeshDescription->PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Center);

			for (const FPolygonID PolygonID : MeshDescription->Polygons().GetElementIDs())
			{
				// Calculate the center of this polygon
				FVector Center = FVector::ZeroVector;
				const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription->GetPolygonVertexInstances(PolygonID);
				for (const FVertexInstanceID VertexInstanceID : VertexInstanceIDs)
				{
					Center += VertexPositions[MeshDescription->GetVertexInstanceVertex(VertexInstanceID)];
				}
				Center /= float(VertexInstanceIDs.Num());

				// Calculate the tangent basis for the polygon, based on the average of all constituent triangles
				FVector Normal = FVector::ZeroVector;
				FVector Tangent = FVector::ZeroVector;
				FVector Binormal = FVector::ZeroVector;

				for (const FTriangleID TriangleID : MeshDescription->GetPolygonTriangleIDs(PolygonID))
				{
					TArrayView<const FVertexInstanceID> TriVertexInstanceIDs = MeshDescription->GetTriangleVertexInstances(TriangleID);
					const FVertexID VertexID0 = MeshDescription->GetVertexInstanceVertex(TriVertexInstanceIDs[0]);
					const FVertexID VertexID1 = MeshDescription->GetVertexInstanceVertex(TriVertexInstanceIDs[1]);
					const FVertexID VertexID2 = MeshDescription->GetVertexInstanceVertex(TriVertexInstanceIDs[2]);

					const FVector DPosition1 = VertexPositions[VertexID1] - VertexPositions[VertexID0];
					const FVector DPosition2 = VertexPositions[VertexID2] - VertexPositions[VertexID0];

					const FVector2D DUV1 = VertexUVs.Get(TriVertexInstanceIDs[1], 0) - VertexUVs.Get(TriVertexInstanceIDs[0], 0);
					const FVector2D DUV2 = VertexUVs.Get(TriVertexInstanceIDs[2], 0) - VertexUVs.Get(TriVertexInstanceIDs[0], 0);

					// We have a left-handed coordinate system, but a counter-clockwise winding order
					// Hence normal calculation has to take the triangle vectors cross product in reverse.
					Normal += FVector::CrossProduct(DPosition2, DPosition1);

					// ...and tangent space seems to be right-handed.
					const float DetUV = FVector2D::CrossProduct(DUV1, DUV2);
					const float InvDetUV = (DetUV == 0.0f) ? 0.0f : 1.0f / DetUV;

					Tangent += (DPosition1 * DUV2.Y - DPosition2 * DUV1.Y) * InvDetUV;
					Binormal += (DPosition2 * DUV1.X - DPosition1 * DUV2.X) * InvDetUV;
				}

				PolygonNormals[PolygonID] = Normal.GetSafeNormal();
				PolygonTangents[PolygonID] = Tangent.GetSafeNormal();
				PolygonBinormals[PolygonID] = Binormal.GetSafeNormal();
				PolygonCenters[PolygonID] = Center;
			}
		}

		EditableMesh->SetMeshDescription(MeshDescription);
		EditableMesh->InitializeAdapters();
	}
}


FEditableMeshCache& FEditableMeshCache::Get()
{
	if (EditableMeshCacheSingleton == nullptr)
	{
		EditableMeshCacheSingleton = new FEditableMeshCache();
		check(EditableMeshCacheSingleton);

		GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetReimport.AddRaw(EditableMeshCacheSingleton, &FEditableMeshCache::OnObjectReimported);
	}

	return *EditableMeshCacheSingleton;
}

UEditableMesh* FEditableMeshCache::FindOrCreateEditableMesh(const UPrimitiveComponent& Component, const FEditableMeshSubMeshAddress& SubMeshAddress)
{
	if (Cast<UStaticMeshComponent>(&Component) == nullptr || SubMeshAddress.EditableMeshFormat == nullptr)
	{
		return nullptr;
	}

	// Grab the existing editable mesh from our cache if we have one, otherwise create one now
	TStrongObjectPtr<UEditableMesh>* EditableMeshPtr = CachedEditableMeshes.Find( SubMeshAddress );
	if( EditableMeshPtr )
	{
		return EditableMeshPtr->Get();
	}

	// @todo mesheditor perf: This is going to HITCH as you hover over meshes.  Ideally we do this on a thread, or worst case give the user a progress dialog.  Maybe save out the editable mesh in editor builds?
	UEditableMesh* EditableMesh = UEditableMeshFactory::MakeEditableMesh( const_cast<UPrimitiveComponent*>(&Component), SubMeshAddress );

	// We don't want to regenerate the collision when entering Edit Mode or editing the mesh in any way, so turn off the simple collision regeneration on the associated EditableStaticMeshAdapter
	// as normally UEditableStaticMeshAdapter::UpdateCollision would generate a box simple collision whenever the mesh is modified (including modifications that didn't change the geometry like
	// flipping normals or setting material) whether the mesh initially had no simple collision or a custom collision.
	for (UEditableMeshAdapter* Adapter : EditableMesh->Adapters)
	{
		if (UEditableStaticMeshAdapter* StaticMeshAdapter = Cast<UEditableStaticMeshAdapter>(Adapter))
		{
			StaticMeshAdapter->SetRecreateSimpleCollision(false);
		}
	}

	// Enable undo tracking on this mesh
	EditableMesh->SetAllowUndo( true );

	// Disable octree, it will be enabled and updated when editable mesh is attached to context
	EditableMesh->SetAllowSpatialDatabase( false );

	// Enable compaction on this mesh
	EditableMesh->SetAllowCompact( true );

	CachedEditableMeshes.Add( SubMeshAddress ).Reset(EditableMesh);

	const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(&Component);

	StaticMeshesToComponents.Add(StaticMeshComponent->GetStaticMesh(), StaticMeshComponent);

	return EditableMesh;
}

const UEditableMesh* FEditableMeshCache::FindEditableMesh(const UPrimitiveComponent& Component, const FEditableMeshSubMeshAddress& SubMeshAddress) const
{
	return FindModifiableEditableMesh(Component, SubMeshAddress);
}

UEditableMesh* FEditableMeshCache::FindModifiableEditableMesh(const UPrimitiveComponent& Component, const FEditableMeshSubMeshAddress& SubMeshAddress) const
{
	if (Cast<UStaticMeshComponent>(&Component) == nullptr || SubMeshAddress.EditableMeshFormat == nullptr)
	{
		return nullptr;
	}

	const TStrongObjectPtr<UEditableMesh>* EditableMeshPtr = CachedEditableMeshes.Find( SubMeshAddress );

	return EditableMeshPtr ? EditableMeshPtr->Get() : nullptr;
}

void FEditableMeshCache::OnObjectReimported(UObject* InObject)
{
	// If a static mesh has been re-imported, it might have been edited
	if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(InObject))
	{
		RemoveObject(StaticMesh);
	}
}

void FEditableMeshCache::RemoveObject(UStaticMesh * StaticMesh)
{
	const UStaticMeshComponent* StaticMeshComponent = nullptr;
	if (StaticMeshesToComponents.RemoveAndCopyValue(StaticMesh, StaticMeshComponent))
	{
		// If one of the LODs has been edited, remove it
		for (int32 LODIndex = 0; LODIndex < StaticMesh->GetNumSourceModels(); ++LODIndex)
		{
			const FEditableMeshSubMeshAddress SubMeshAddressToQuery = UEditableMeshFactory::MakeSubmeshAddress(const_cast<UStaticMeshComponent*>(StaticMeshComponent), LODIndex);
			CachedEditableMeshes.Remove(SubMeshAddressToQuery);
		}
	}
}

void FEditableMeshCache::ResetObject(UStaticMesh * StaticMesh)
{
	const UStaticMeshComponent* StaticMeshComponent = StaticMeshesToComponents.FindRef(StaticMesh);
	if (StaticMeshComponent)
	{
		// If one of the LODs has been edited, reset its MeshDescription
		for (int32 LODIndex = 0; LODIndex < StaticMesh->GetNumSourceModels(); ++LODIndex)
		{
			const FEditableMeshSubMeshAddress SubMeshAddressToQuery = UEditableMeshFactory::MakeSubmeshAddress(const_cast<UStaticMeshComponent*>(StaticMeshComponent), LODIndex);
			TStrongObjectPtr<UEditableMesh> EditableMeshPtr = CachedEditableMeshes.FindRef(SubMeshAddressToQuery);
			if (EditableMeshPtr)
			{
				EditableMeshPtr->SetMeshDescription(&EditableMeshPtr->OwnedMeshDescription);
			}
		}
	}
}

FMeshEditingContext::FMeshEditingContext()
	: LODIndex(INDEX_NONE)
	, StaticMeshComponent(nullptr)
	, EditableMesh(nullptr)
{
}

FMeshEditingContext::FMeshEditingContext(UStaticMeshComponent* InStaticMeshComponent)
	: LODIndex(INDEX_NONE)
	, StaticMeshComponent(InStaticMeshComponent)
	, EditableMesh(nullptr)
{
	check(StaticMeshComponent);
}

void FMeshEditingContext::Reset()
{
	if (StaticMeshComponent == nullptr)
	{
		return;
	}

	SelectedMeshElements.Empty();

	if (EditableMesh != nullptr)
	{
		EditableMesh->OnElementIDsRemapped().RemoveAll( this );

		EditableMesh = nullptr;
		LODIndex = INDEX_NONE;
	}
}

void FMeshEditingContext::Activate(FEditorViewportClient& ViewportClient, int32 InLODIndex)
{
	if (StaticMeshComponent == nullptr)
	{
		return;
	}

	SelectedMeshElements.Empty();

	SetLODIndex(InLODIndex);
}

void FMeshEditingContext::Deactivate()
{
	if (StaticMeshComponent == nullptr)
	{
		return;
	}

	Reset();
}

void FMeshEditingContext::SetLODIndex(int32 InLODIndex)
{
	if (StaticMeshComponent == nullptr || InLODIndex < 0 || InLODIndex == LODIndex)
	{
		return;
	}

	Reset();

	LODIndex = InLODIndex;

	FEditableMeshSubMeshAddress SubMeshAddressToQuery = UEditableMeshFactory::MakeSubmeshAddress( StaticMeshComponent, LODIndex );
	EditableMesh = FEditableMeshCache::Get().FindOrCreateEditableMesh( *StaticMeshComponent, SubMeshAddressToQuery );
	check(EditableMesh);

	// Set a callback so any cached ElementIDs can be remapped
	EditableMesh->OnElementIDsRemapped().AddRaw( this, &FMeshEditingContext::OnEditableMeshElementIDsRemapped );

	// Point EditableMesh's MeshDescription to StaticMesh's
	FMeshDescription* EditableMeshDescription = EditableMesh->GetMeshDescription();
	FMeshDescription* MeshDescription = StaticMeshComponent->GetStaticMesh()->GetMeshDescription(LODIndex);
	if (MeshDescription != EditableMeshDescription)
	{
		MeshEditingContext::SetEditableMeshDescription(EditableMesh, StaticMeshComponent, LODIndex);

		// Disable spatial database to flush it
		EditableMesh->SetAllowSpatialDatabase( false );

		// Force generation of spatial database to use mesh's bounding box instead of world's maximum extents
		IConsoleVariable* UseBoundlessOctree = IConsoleManager::Get().FindConsoleVariable(TEXT( "EditableMesh.UseBoundlessOctree" ));
		int32 UseBoundlessOctreeValue = 1;
		if (UseBoundlessOctree != nullptr)
		{
			UseBoundlessOctreeValue = UseBoundlessOctree->GetInt();
			UseBoundlessOctree->Set(0);
		}

		// Enable spatial database, to create it
		EditableMesh->SetAllowSpatialDatabase( true );

		// Restore console variable to previous value
		if (UseBoundlessOctree != nullptr)
		{
			UseBoundlessOctree->Set(UseBoundlessOctreeValue);
		}
	}
}

void FMeshEditingContext::OnEditableMeshElementIDsRemapped( UEditableMesh* InEditableMesh, const FElementIDRemappings& Remappings )
{
	if (InEditableMesh != EditableMesh)
	{
		return;
	}

	// Helper function which performs the remapping of a given FMeshElement
	auto RemapMeshElement = [ this, &Remappings ]( FMeshElement& MeshElement )
	{
		if( MeshElement.Component.IsValid() && Cast<UStaticMeshComponent>(MeshElement.Component.Get()) == StaticMeshComponent)
		{
			switch( MeshElement.ElementAddress.ElementType )
			{
			case EEditableMeshElementType::Vertex:
				MeshElement.ElementAddress.ElementID = Remappings.GetRemappedVertexID( FVertexID( MeshElement.ElementAddress.ElementID ) );
				break;

			case EEditableMeshElementType::Edge:
				MeshElement.ElementAddress.ElementID = Remappings.GetRemappedEdgeID( FEdgeID( MeshElement.ElementAddress.ElementID ) );
				break;

			case EEditableMeshElementType::Polygon:
				MeshElement.ElementAddress.ElementID = Remappings.GetRemappedPolygonID( FPolygonID( MeshElement.ElementAddress.ElementID ) );
				break;
			}
		}
	};

	TSet<FMeshElementKey> OldSelectedMeshElements = SelectedMeshElements;
	SelectedMeshElements.Empty();

	for( FMeshElementKey& MeshElementKey : OldSelectedMeshElements )
	{
		FMeshElement MeshElement;
		MeshElement.Component = MeshElementKey.PrimitiveComponent;
		MeshElement.ElementAddress = MeshElementKey.MeshElementAddress;

		RemapMeshElement( MeshElement );

		SelectedMeshElements.Add(FMeshElementKey(MeshElement));
	}
}

void FMeshEditingContext::ClearSelectedElements()
{
	SelectedMeshElements.Empty();
}

void FMeshEditingContext::RemoveElementFromSelection(const FMeshElement & MeshElement)
{
	FMeshElementKey MeshElementKey(MeshElement);

	if (SelectedMeshElements.Find(MeshElementKey) != nullptr)
	{
		SelectedMeshElements.Remove(MeshElementKey);
	}
}

void FMeshEditingContext::AddElementToSelection(const FMeshElement& MeshElement)
{
	FMeshElementKey MeshElementKey(MeshElement);

	if (SelectedMeshElements.Find(MeshElementKey) == nullptr)
	{
		SelectedMeshElements.Add(MeshElementKey);
	}
}

void FMeshEditingContext::ToggleElementSelection(const FMeshElement& MeshElement)
{
	FMeshElementKey MeshElementKey(MeshElement);

	if (SelectedMeshElements.Find(MeshElementKey) != nullptr)
	{
		SelectedMeshElements.Remove(MeshElementKey);
		return;
	}

	SelectedMeshElements.Add(MeshElementKey);
}

TArray<FMeshElement> FMeshEditingContext::GetSelectedElements(EEditableMeshElementType ElementType)
{
	TArray<FMeshElement> MeshElements;

	for (const FMeshElementKey& MeshElementKey : SelectedMeshElements)
	{
		if (EEditableMeshElementType::Any == ElementType || MeshElementKey.MeshElementAddress.ElementType == ElementType)
		{
			FMeshElement MeshElement;
			MeshElement.Component = MeshElementKey.PrimitiveComponent;
			MeshElement.ElementAddress = MeshElementKey.MeshElementAddress;

			MeshElements.Add(MeshElement);
		}
	}

	return MeshElements;
}

bool FMeshEditingContext::IsMeshElementTypeSelected(EEditableMeshElementType ElementType) const
{
	for (const FMeshElementKey& MeshElementKey : SelectedMeshElements)
	{
		if ( EEditableMeshElementType::Any == ElementType || MeshElementKey.MeshElementAddress.ElementType == ElementType)
		{
			return true;
		}
	}

	return false;
}

void FMeshEditingContext::ExpandPolygonSelection()
{
	// Get current polygon selection
	TArray<FMeshElement> SelectedPolygons = GetSelectedElements(EEditableMeshElementType::Polygon);

	TArray<FMeshElement> MeshElementsToSelect;

	// Expand current polygon selection by checking the vertices of the selected polygons and adding all the polygons connected to a vertex
	TSet< FPolygonID > PolygonsToSelect;
	for (const FMeshElement& PolygonElement : SelectedPolygons)
	{
		FPolygonID PolygonID(PolygonElement.ElementAddress.ElementID);

		PolygonsToSelect.Add(PolygonID);

		TArray< FVertexID > PolygonVertices;
		EditableMesh->GetPolygonPerimeterVertices(PolygonID, PolygonVertices);

		for (const FVertexID& VertexID : PolygonVertices)
		{
			TArray<FPolygonID> ConnectedPolygons;
			EditableMesh->GetVertexConnectedPolygons(VertexID, ConnectedPolygons);

			for (const FPolygonID& ConnectedPolygonID : ConnectedPolygons)
			{
				if (!PolygonsToSelect.Contains(ConnectedPolygonID))
				{
					PolygonsToSelect.Add(ConnectedPolygonID);
					MeshElementsToSelect.Emplace(PolygonElement.Component.Get(), EditableMesh->GetSubMeshAddress(), ConnectedPolygonID);
				}
			}
		}
	}

	// Refresh display for the new expanded selection
	AddElementsToSelection(MeshElementsToSelect);
}

void FMeshEditingContext::ShrinkPolygonSelection()
{
	// Get current polygon selection
	TArray<FMeshElement> SelectedPolygons = GetSelectedElements(EEditableMeshElementType::Polygon);

	TArray<FMeshElement> MeshElementsToDeselect;

	// Collect the IDs of all the polygons in the current selection
	TSet< FPolygonID > SelectedPolygonIDs;
	for (const FMeshElement& PolygonElement : SelectedPolygons)
	{
		FPolygonID PolygonID(PolygonElement.ElementAddress.ElementID);
		SelectedPolygonIDs.Add(PolygonID);
	}

	// Shrink current polygon selection by removing any polygon which has vertices on the selection boundary
	// ie. the vertex-connected polygons are not all in the current selection
	TSet< FPolygonID > PolygonsToDeselect;
	for (const FMeshElement& PolygonElement : SelectedPolygons)
	{
		FPolygonID PolygonID(PolygonElement.ElementAddress.ElementID);

		if (!PolygonsToDeselect.Contains(PolygonID))
		{
			TArray< FVertexID > PolygonVertices;
			EditableMesh->GetPolygonPerimeterVertices(PolygonID, PolygonVertices);

			for (const FVertexID& VertexID : PolygonVertices)
			{
				TArray<FPolygonID> ConnectedPolygons;
				EditableMesh->GetVertexConnectedPolygons(VertexID, ConnectedPolygons);

				for (const FPolygonID& ConnectedPolygonID : ConnectedPolygons)
				{
					if (!SelectedPolygonIDs.Contains(ConnectedPolygonID))
					{
						PolygonsToDeselect.Add(PolygonID);
						MeshElementsToDeselect.Emplace(PolygonElement.Component.Get(), EditableMesh->GetSubMeshAddress(), PolygonID);
						goto next_polygon;
					}
				}
			}

		}
		next_polygon: ;
	}

	// Refresh display for the new shrunk selection
	RemoveElementsFromSelection(MeshElementsToDeselect);
}

FMeshEditingUIContext::FMeshEditingUIContext()
	: FMeshEditingContext()
{
}

FMeshEditingUIContext::FMeshEditingUIContext(UStaticMeshComponent* InStaticMeshComponent)
	: FMeshEditingContext(InStaticMeshComponent)
{
	AssetContainer = TStrongObjectPtr<UStaticMeshEditorAssetContainer>(LoadObject<UStaticMeshEditorAssetContainer>( nullptr, TEXT( "/StaticMeshEditorExtension/StaticMeshEditorAssetContainer" ) ));
	check(AssetContainer.IsValid());
}

void FMeshEditingUIContext::Initialize(FEditorViewportClient & ViewportClient)
{
	// Create actor holding onto UI mesh component if not done yet
	if (WireframeComponentContainer.Get() == nullptr)
	{
		FActorSpawnParameters ActorSpawnParameters;
		ActorSpawnParameters.ObjectFlags |= RF_Transient;
		WireframeComponentContainer = TWeakObjectPtr<AActor>(ViewportClient.GetWorld()->SpawnActor<AActor>( ActorSpawnParameters ));
		check(WireframeComponentContainer.Get() != nullptr);
	}
}

void FMeshEditingUIContext::Reset()
{
	// Delete all objects created for current EditableMesh
	if (EditableMesh != nullptr)
	{
		if (WireframeMeshComponent.Get() != nullptr)
		{
			WireframeMeshComponent->DestroyComponent();
			WireframeMeshComponent.Reset();
		}

		// Get WireframeAdaper currently used
		UStaticMeshEditorStaticMeshAdapter* WireframeAdapter = nullptr;
		for (UEditableMeshAdapter* MeshAdapter : EditableMesh->Adapters)
		{
			if (MeshAdapter->IsA(UStaticMeshEditorStaticMeshAdapter::StaticClass()))
			{
				WireframeAdapter = Cast<UStaticMeshEditorStaticMeshAdapter>(MeshAdapter);
				break;
			}
		}
		check(WireframeAdapter);

		EditableMesh->Adapters.Remove(WireframeAdapter);
		WireframeAdapter->ConditionalBeginDestroy();
		WireframeBaseCage->ConditionalBeginDestroy();
	}

	FMeshEditingContext::Reset();
}

void FMeshEditingUIContext::Activate(FEditorViewportClient& ViewportClient, int32 InLODIndex)
{
	if (!IsValid())
	{
		return;
	}

	Initialize(ViewportClient);

	FMeshEditingContext::Activate(ViewportClient, InLODIndex);

	if (AssetContainer.IsValid() && WireframeComponentContainer.Get() != nullptr)
	{
		// Add overlay component for rendering selected elements
		SelectedElementsComponent = TWeakObjectPtr<UOverlayComponent>(NewObject<UOverlayComponent>( WireframeComponentContainer.Get() ));
		SelectedElementsComponent->SetLineMaterial( AssetContainer->OverlayLineMaterial );
		SelectedElementsComponent->SetPointMaterial( AssetContainer->OverlayPointMaterial );
		SelectedElementsComponent->SetWorldTransform( WireframeMeshComponent->GetComponentTransform() );
		SelectedElementsComponent->TranslucencySortPriority = 500;
		SelectedElementsComponent->RegisterComponent();

		// Add overlay component for rendering hovered elements
		HoveredElementsComponent = TWeakObjectPtr<UOverlayComponent>(NewObject<UOverlayComponent>( WireframeComponentContainer.Get() ));
		HoveredElementsComponent->SetLineMaterial( AssetContainer->OverlayLineMaterial );
		HoveredElementsComponent->SetPointMaterial( AssetContainer->OverlayPointMaterial );
		HoveredElementsComponent->SetWorldTransform( WireframeMeshComponent->GetComponentTransform() );
		HoveredElementsComponent->TranslucencySortPriority = 400;
		HoveredElementsComponent->RegisterComponent();
	}
}

void FMeshEditingUIContext::Deactivate()
{
	CachedOverlayIDs.Empty();

	if (SelectedElementsComponent.Get() != nullptr)
	{
		SelectedElementsComponent->DestroyComponent();
		SelectedElementsComponent.Reset();
	}

	if (HoveredElementsComponent.Get() != nullptr)
	{
		HoveredElementsComponent->DestroyComponent();
		HoveredElementsComponent.Reset();
	}

	FMeshEditingContext::Deactivate();
}

void FMeshEditingUIContext::SetLODIndex(int32 InLODIndex)
{
	if (StaticMeshComponent == nullptr || InLODIndex < 0 || InLODIndex >= StaticMeshComponent->GetStaticMesh()->GetNumLODs() || InLODIndex == LODIndex)
	{
		return;
	}

	FMeshEditingContext::SetLODIndex(InLODIndex);

	// Create a wireframe mesh for the base cage
	WireframeBaseCage = TWeakObjectPtr<UWireframeMesh>(NewObject<UWireframeMesh>());

	UStaticMeshEditorStaticMeshAdapter* WireframeAdapter = NewObject<UStaticMeshEditorStaticMeshAdapter>();
	EditableMesh->Adapters.Add( WireframeAdapter );
	WireframeAdapter->Initialize( EditableMesh, WireframeBaseCage.Get() );
	WireframeAdapter->SetContext(StaticMeshComponent->GetStaticMesh(), LODIndex);

	// Rebuild mesh so that the wireframe meshes get their render data built through the adapters
	EditableMesh->RebuildRenderMesh();

	if (AssetContainer.IsValid() && WireframeComponentContainer.Get() != nullptr)
	{
		WireframeMeshComponent = TWeakObjectPtr<UWireframeMeshComponent>(NewObject<UWireframeMeshComponent>( WireframeComponentContainer.Get() ));
		WireframeMeshComponent->SetMaterial( 0, AssetContainer->WireMaterial );
		WireframeMeshComponent->TranslucencySortPriority = 300;
		WireframeMeshComponent->SetWireframeMesh( WireframeBaseCage.Get() );
		WireframeMeshComponent->SetWorldTransform( StaticMeshComponent->GetComponentTransform() );
		WireframeMeshComponent->RegisterComponent();

		if (SelectedElementsComponent.Get() != nullptr)
		{
			SelectedElementsComponent->Clear();
			HoveredElementsComponent->Clear();
		}
	}
}

void FMeshEditingUIContext::OnMeshChanged()
{
	if (StaticMeshComponent == nullptr)
	{
		return;
	}

	// A PostEdit has been called on the edited static mesh, the associated editable mesh has to be reset
	// The same EditableMesh has to be reused for the undo operations since they reference it
	FEditableMeshCache::Get().ResetObject(StaticMeshComponent->GetStaticMesh());

	// Re-initialize EditableMesh if context was active
	if (LODIndex != INDEX_NONE)
	{
		CachedOverlayIDs.Empty();

		int32 CachedLODIndex = LODIndex;
		// Do not deactivate the UI only the mesh context
		FMeshEditingContext::Deactivate();
		// Regenerate editable mesh and related data
		SetLODIndex(CachedLODIndex);
	}

	// Make sure the EditableMesh MeshDescription for all LODs are updated after the ResetObject (even when not in Edit Mode, LODIndex == INDEX_NONE)
	// The user could be undoing operations outside of Edit Mode
	for (int32 CurrentLODIndex = 0; CurrentLODIndex < StaticMeshComponent->GetStaticMesh()->GetNumSourceModels(); ++CurrentLODIndex)
	{
		// MeshDescription for LODIndex is already set through SetLODIndex
		if (CurrentLODIndex == LODIndex)
		{
			continue;
		}

		FEditableMeshSubMeshAddress SubMeshAddressToQuery = UEditableMeshFactory::MakeSubmeshAddress(StaticMeshComponent, CurrentLODIndex);
		UEditableMesh* CurrentEditableMesh = FEditableMeshCache::Get().FindModifiableEditableMesh(*StaticMeshComponent, SubMeshAddressToQuery);
		MeshEditingContext::SetEditableMeshDescription(CurrentEditableMesh, StaticMeshComponent, CurrentLODIndex);
	}
}

void FMeshEditingUIContext::ClearHoveredElements()
{
	HoveredElementsComponent->Clear();
	if (CachedOverlayIDs.Find(HoveredElementsComponent.Get()) != nullptr)
	{
		CachedOverlayIDs[HoveredElementsComponent.Get()].Empty();
	}
}

void FMeshEditingUIContext::RemoveHoveredElement(const FMeshElement & MeshElement)
{
	RemoveMeshElementFromOverlay(HoveredElementsComponent.Get(), MeshElement);
}

void FMeshEditingUIContext::AddHoveredElement(const FMeshElement & MeshElement)
{
	const float SizeBias = HoveredSizeBias ? HoveredSizeBias->GetFloat() : 0.1f;
	const FColor Color = FLinearColor(0.9, 0.7, 0.02, 1.0).ToFColor( false );
	AddMeshElementToOverlay(HoveredElementsComponent.Get(), MeshElement, Color, SizeBias);
}

void FMeshEditingUIContext::ClearSelectedElements()
{
	FMeshEditingContext::ClearSelectedElements();

	if (CachedOverlayIDs.Find(SelectedElementsComponent.Get()) != nullptr)
	{
		SelectedElementsComponent->Clear();
		CachedOverlayIDs[SelectedElementsComponent.Get()].Empty();
	}
}

void FMeshEditingUIContext::RemoveElementFromSelection(const FMeshElement & MeshElement)
{
	int32 SelectedCount = SelectedMeshElements.Num();

	FMeshEditingContext::RemoveElementFromSelection(MeshElement);

	// Update UI if anything has changed
	if (SelectedCount != SelectedMeshElements.Num())
	{
		RemoveMeshElementFromOverlay(SelectedElementsComponent.Get(), MeshElement);
	}
}

void FMeshEditingUIContext::AddElementToSelection(const FMeshElement& MeshElement)
{
	int32 SelectedCount = SelectedMeshElements.Num();

	FMeshEditingContext::AddElementToSelection(MeshElement);

	// Update UI if anything has changed
	if (SelectedCount != SelectedMeshElements.Num())
	{
		const float SizeBias = SelectedSizeBias ? SelectedSizeBias->GetFloat() : 0.1f;
		const FColor Color = FLinearColor(0.9, 0.2, 0.02).ToFColor( false );
		AddMeshElementToOverlay(SelectedElementsComponent.Get(), MeshElement, Color, SizeBias, true);
	}
}

void FMeshEditingUIContext::ToggleElementSelection(const FMeshElement& MeshElement)
{
	int32 SelectedCount = SelectedMeshElements.Num();

	FMeshEditingContext::ToggleElementSelection(MeshElement);

	// Update UI if anything has changed
	if (SelectedCount < SelectedMeshElements.Num())
	{
		const float SizeBias = SelectedSizeBias ? SelectedSizeBias->GetFloat() : 0.1f;
		const FColor Color = FLinearColor(0.9, 0.2, 0.02).ToFColor( false );
		AddMeshElementToOverlay(SelectedElementsComponent.Get(), MeshElement, Color, SizeBias, true);
	}
	else if (SelectedCount > SelectedMeshElements.Num())
	{
		RemoveMeshElementFromOverlay(SelectedElementsComponent.Get(), MeshElement);
	}
}

void FMeshEditingUIContext::RemoveMeshElementFromOverlay( UOverlayComponent* OverlayComponent, const FMeshElement& MeshElement )
{
	if (!MeshElement.IsValidMeshElement())
	{
		return;
	}

	FMeshElementKey MeshElementKey(MeshElement);
	TMap< FMeshElementKey, TArray<int32> >& OverlayIDs = CachedOverlayIDs.FindOrAdd(OverlayComponent);
	if (OverlayIDs.Find(MeshElementKey) == nullptr)
	{
		return;
	}

	UPrimitiveComponent* Component = MeshElement.Component.Get();
	check( Component != nullptr );

	const FMatrix ComponentToWorldMatrix = Component->GetRenderMatrix();

	switch( MeshElement.ElementAddress.ElementType )
	{
		case EEditableMeshElementType::Vertex:
		{
			FOverlayPointID PointID(OverlayIDs[MeshElementKey][0]);
			OverlayComponent->RemovePoint(PointID);
			break;
		}

		case EEditableMeshElementType::Edge:
		{
			FOverlayLineID LineID(OverlayIDs[MeshElementKey][0]);
			OverlayComponent->RemoveLine(LineID);
			break;
		}

		case EEditableMeshElementType::Polygon:
		{
			TArray<int32>& TriangleIDSet = OverlayIDs[MeshElementKey];
			for( int32 TriangleID : TriangleIDSet )
			{
				OverlayComponent->RemoveTriangle(FOverlayTriangleID(TriangleID));
			}
			// if polygon's contour has been added, remove it
			{
				FMeshElementKey MeshEdgeKey(MeshElement);
				MeshEdgeKey.MeshElementAddress.ElementType = EEditableMeshElementType::Edge;

				if (OverlayIDs.Find(MeshEdgeKey) != nullptr)
				{
					TArray<int32>& LineIDs = OverlayIDs[MeshEdgeKey];

					for (int32 LineID : LineIDs)
					{
						OverlayComponent->RemoveLine(FOverlayLineID(LineID));
					}
				}

			}
			break;
		}
	}

	OverlayIDs.Remove(MeshElementKey);
}

void FMeshEditingUIContext::AddMeshElementToOverlay( UOverlayComponent* OverlayComponent, const FMeshElement& MeshElement, const FColor Color, const float Size, bool bAddContour )
{
	if (OverlayComponent == nullptr || !MeshElement.IsValidMeshElement())
	{
		return;
	}

	FMeshElementKey MeshElementKey(MeshElement);
	TMap< FMeshElementKey, TArray<int32> >& OverlayIDs = CachedOverlayIDs.FindOrAdd(OverlayComponent);
	if (OverlayIDs.Find(MeshElementKey) != nullptr)
	{
		return;
	}

	const FMeshDescription* MeshDescription = EditableMesh->GetMeshDescription();
	TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription->VertexAttributes().GetAttributesRef<FVector>( MeshAttribute::Vertex::Position );

	UPrimitiveComponent* Component = MeshElement.Component.Get();
	check( Component != nullptr );

	switch( MeshElement.ElementAddress.ElementType )
	{
		case EEditableMeshElementType::Vertex:
		{
			const FVertexID VertexID( MeshElement.ElementAddress.ElementID );
			const FVector Position( VertexPositions[ VertexID ] );

			FOverlayPointID PointID = OverlayComponent->AddPoint( FOverlayPoint( Position, Color, Size ) );
			OverlayIDs.Add(MeshElementKey).Add(PointID.GetValue());
			break;
		}

		case EEditableMeshElementType::Edge:
		{
			const FEdgeID EdgeID( MeshElement.ElementAddress.ElementID );
			const FVertexID StartVertexID( EditableMesh->GetEdgeVertex( EdgeID, 0 ) );
			const FVertexID EndVertexID( EditableMesh->GetEdgeVertex( EdgeID, 1 ) );
			const FVector StartPosition( VertexPositions[ StartVertexID ] );
			const FVector EndPosition( VertexPositions[ EndVertexID ] );

			FOverlayLineID LineID = OverlayComponent->AddLine( FOverlayLine( StartPosition, EndPosition, Color, Size ) );
			OverlayIDs.Add(MeshElementKey).Add(LineID.GetValue());
			break;
		}

		case EEditableMeshElementType::Polygon:
		{
			const FPolygonID PolygonID( MeshElement.ElementAddress.ElementID );
			const int32 PolygonTriangleCount = EditableMesh->GetPolygonTriangulatedTriangleCount( PolygonID );

			TArray<int32>& TriangleIDs = OverlayIDs.Add(MeshElementKey);
			const float OverlayDistance = MeshEditingContext::OverlayHoverDistance->GetFloat();

			for( int32 PolygonTriangle = 0; PolygonTriangle < PolygonTriangleCount; PolygonTriangle++ )
			{
				FVector TriangleVertexPositions[ 3 ];
				for( int32 TriangleVertex = 0; TriangleVertex < 3; TriangleVertex++ )
				{
					const FTriangleID PolygonTriangleID = EditableMesh->GetPolygonTriangulatedTriangle( PolygonID, PolygonTriangle );
					const FVertexInstanceID VertexInstanceID = MeshDescription->GetTriangleVertexInstance( PolygonTriangleID, TriangleVertex );
					const FVertexID VertexID = EditableMesh->GetVertexInstanceVertex( VertexInstanceID );
					TriangleVertexPositions[ TriangleVertex ] = VertexPositions[ VertexID ];
				}

				// Add a small offset to overlay triangle to render better over mesh that use translucent material
				FVector TriangleNormal(((TriangleVertexPositions[2] - TriangleVertexPositions[0]) ^ (TriangleVertexPositions[1] - TriangleVertexPositions[0])).GetSafeNormal());
				FVector Offset = TriangleNormal * OverlayDistance;

				FOverlayTriangleID TriangleID = OverlayComponent->AddTriangle( FOverlayTriangle(
					AssetContainer->HoveredFaceMaterial,
					FOverlayTriangleVertex( TriangleVertexPositions[ 0 ] + Offset, FVector2D( 0, 0 ), FVector::UpVector, Color ),
					FOverlayTriangleVertex( TriangleVertexPositions[ 1 ] + Offset, FVector2D( 0, 1 ), FVector::UpVector, Color ),
					FOverlayTriangleVertex( TriangleVertexPositions[ 2 ] + Offset, FVector2D( 1, 1 ), FVector::UpVector, Color )
				) );
				TriangleIDs.Add(TriangleID.GetValue());
			}

			if (bAddContour)
			{
				// Add a entry in the map for the edges of the polygon
				// Just change the ElementType on the key used for the triangles
				FMeshElementKey MeshEdgeKey(MeshElement);
				MeshEdgeKey.MeshElementAddress.ElementType = EEditableMeshElementType::Edge;

				TArray<int32>& LineIDs = OverlayIDs.Add(MeshEdgeKey);

				TArray<FEdgeID> PolygonEdges;
				EditableMesh->GetMeshDescription()->GetPolygonPerimeterEdges(PolygonID, PolygonEdges);

				for (FEdgeID& EdgeID : PolygonEdges)
				{
					const FVertexID StartVertexID( EditableMesh->GetEdgeVertex( EdgeID, 0 ) );
					const FVertexID EndVertexID( EditableMesh->GetEdgeVertex( EdgeID, 1 ) );
					const FVector StartPosition( VertexPositions[ StartVertexID ] );
					const FVector EndPosition( VertexPositions[ EndVertexID ] );

					FOverlayLineID LineID = OverlayComponent->AddLine( FOverlayLine( StartPosition, EndPosition, Color, Size ) );
					LineIDs.Add(LineID.GetValue());
				}
			}
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE