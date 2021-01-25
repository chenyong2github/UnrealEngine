// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionDebugDrawActor.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionParticlesData.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionActor.h"

#include "DrawDebugHelpers.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/BillboardComponent.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "HAL/IConsoleManager.h"
#include "PBDRigidsSolver.h"
#include "PhysicsSolver.h"  // #if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
#include "GeometryCollection/GeometryCollectionDebugDrawComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogGeometryCollectionDebugDrawActor, Log, All);

// Constants
namespace GeometryCollectionDebugDrawActorConstants
{
	// Invariables
	static const bool bPersistent = true;  // Debug draw needs persistency to work well within the editor.
	static const float LifeTime = -1.0f;  // Lifetime is infinite.
	static const uint8 DepthPriority = 0;
	static const uint32 CircleSegments = 32;
	static const bool bDrawCircleAxis = true;
	static const TArray<FIntPoint> BoxEdges(
	{
		FIntPoint(0, 1), FIntPoint(1, 2), FIntPoint(2, 3), FIntPoint(3, 0),
		FIntPoint(4, 5), FIntPoint(5, 6), FIntPoint(6, 7), FIntPoint(7, 4),
		FIntPoint(0, 4), FIntPoint(1, 5), FIntPoint(2, 6), FIntPoint(3, 7)
	});

	// Base colors
	static const FLinearColor DarkerTintFactor(1.0f, 1.0f, 0.7f);  // Darker HSV multiplier
	static const FLinearColor LighterTintFactor(1.0f, 1.0f, 2.0f);  // Lighter HSV multiplier
	static const FLinearColor RigidBodyTint(0.8f, 0.1f, 0.1f);  // Red
	static const FLinearColor ClusteringTint(0.6, 0.4f, 0.2f);  // Orange
	static const FLinearColor GeometryTint(0.4f, 0.2f, 0.6f);  // Purple
	static const FLinearColor SingleFaceTint(0.6, 0.2f, 0.4f);  // Pink
	static const FLinearColor VertexTint(0.2f, 0.4f, 0.6f);  // Blue

	// Defaults
	static const FString SelectedRigidBodySolverDefault = FName(NAME_None).ToString();
	static const int32 SelectedRigidBodyIdDefault = INDEX_NONE;
	static const int32 DebugDrawWholeCollectionDefault = 0;
	static const int32 DebugDrawHierarchyDefault = 0;
	static const int32 DebugDrawClusteringDefault = 0;
	static const int32 HideGeometryDefault = int32(EGeometryCollectionDebugDrawActorHideGeometry::HideWithCollision);
	static const int32 ShowRigidBodyCollisionDefault = 0;
	static const int32 ShowRigidBodyIdDefault = 0;
	static const int32 CollisionAtOriginDefault = 0;
	static const int32 ShowRigidBodyTransformDefault = 0;
	static const int32 ShowRigidBodyInertiaDefault = 0;
	static const int32 ShowRigidBodyVelocityDefault = 0;
	static const int32 ShowRigidBodyForceDefault = 0;
	static const int32 ShowRigidBodyInfosDefault = 0;
	static const int32 ShowTransformIndexDefault = 0;
	static const int32 ShowTransformDefault = 0;
	static const int32 ShowParentDefault = 0;
	static const int32 ShowLevelDefault = 0;
	static const int32 ShowConnectivityEdgesDefault = 0;
	static const int32 ShowGeometryIndexDefault = 0;
	static const int32 ShowGeometryTransformDefault = 0;
	static const int32 ShowBoundingBoxDefault = 0;
	static const int32 ShowFacesDefault = 0;
	static const int32 ShowFaceIndicesDefault = 0;
	static const int32 ShowFaceNormalsDefault = 0;
	static const int32 ShowSingleFaceDefault = 0;
	static const int32 SingleFaceIndexDefault = 0;
	static const int32 ShowVerticesDefault = 0;
	static const int32 ShowVertexIndicesDefault = 0;
	static const int32 ShowVertexNormalsDefault = 0;
	static const int32 UseActiveVisualizationDefault = true;
	static const float PointThicknessDefault = 6.0f;
	static const float LineThicknessDefault = 1.0f;
	static const int32 TextShadowDefault = 1;
	static const float TextScaleDefault = 1.0f;
	static const float NormalScaleDefault = 10.0f;
	static const float AxisScaleDefault = 20.0f;
	static const float ArrowScaleDefault = 2.5f;
	static const float TransformScaleDefault = 1.f;
	static const FColor RigidBodyIdsColorDefault = (RigidBodyTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor RigidBodyCollisionColorDefault = RigidBodyTint.ToFColor(true);
	static const FColor RigidBodyInertiaColorDefault = (RigidBodyTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor RigidBodyVelocityColorDefault = (RigidBodyTint.LinearRGBToHSV() * DarkerTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor RigidBodyForceColorDefault = (RigidBodyTint.LinearRGBToHSV() * DarkerTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor RigidBodyInfoColorDefault = (RigidBodyTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor TransformIndexColorDefault = (ClusteringTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor LevelColorDefault = (ClusteringTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor ParentColorDefault = ClusteringTint.ToFColor(true);
	static const FColor GeometryIndexColorDefault = (GeometryTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor BoundingBoxColorDefault = (GeometryTint.LinearRGBToHSV() * DarkerTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor FaceColorDefault = GeometryTint.ToFColor(true);
	static const FColor FaceIndexColorDefault = (GeometryTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor FaceNormalColorDefault = (GeometryTint.LinearRGBToHSV() * DarkerTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor SingleFaceColorDefault = (SingleFaceTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor VertexColorDefault = VertexTint.ToFColor(true);
	static const FColor VertexIndexColorDefault = (VertexTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor VertexNormalColorDefault = (VertexTint.LinearRGBToHSV() * DarkerTintFactor).HSVToLinearRGB().ToFColor(true);
}

namespace GeometryCollectionDebugDrawActorCVars
{
	// Console variables, also exposed as settings in this actor
	static TAutoConsoleVariable<FString> SelectedRigidBodySolver (TEXT("p.gc.SelectedRigidBodySolver" ), GeometryCollectionDebugDrawActorConstants::SelectedRigidBodySolverDefault , TEXT("Geometry Collection debug draw, visualize debug informations for the selected rigid body solver.\nDefault = None"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > SelectedRigidBodyId     (TEXT("p.gc.SelectedRigidBodyId"     ), GeometryCollectionDebugDrawActorConstants::SelectedRigidBodyIdDefault     , TEXT("Geometry Collection debug draw, visualize debug informations for the selected rigid body ids.\nDefault = -1"), ECVF_Cheat);
	//static TAutoConsoleVariable<FGuid  > SelectedRigidBodyId     (TEXT("p.gc.SelectedRigidBodyId"     ), GeometryCollectionDebugDrawActorConstants::SelectedRigidBodyIdDefault     , TEXT("Geometry Collection debug draw, visualize debug informations for the selected rigid body ids.\nDefault = 0:0:0:0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > DebugDrawWholeCollection(TEXT("p.gc.DebugDrawWholeCollection"), GeometryCollectionDebugDrawActorConstants::DebugDrawWholeCollectionDefault, TEXT("Geometry Collection debug draw, show debug visualization for the rest of the geometry collection related to the current rigid body id selection.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > DebugDrawHierarchy      (TEXT("p.gc.DebugDrawHierarchy"      ), GeometryCollectionDebugDrawActorConstants::DebugDrawHierarchyDefault      , TEXT("Geometry Collection debug draw, show debug visualization for the top level node rather than the bottom leaf nodes of a cluster's hierarchy..\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > DebugDrawClustering     (TEXT("p.gc.DebugDrawClustering"     ), GeometryCollectionDebugDrawActorConstants::DebugDrawClusteringDefault     , TEXT("Geometry Collection debug draw, show debug visualization for all clustered children associated to the current rigid body id selection.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > HideGeometry            (TEXT("p.gc.HideGeometry"            ), GeometryCollectionDebugDrawActorConstants::HideGeometryDefault            , TEXT("Geometry Collection debug draw, geometry visibility setting, select the part of the geometry to hide in order to better visualize the debug information.\n0: Do not hide any geometries.\n1: Hide the geometry associated to the rigid bodies selected for collision display.\n2: Hide the geometry associated to the selected rigid bodies.\n3: Hide the entire geometry collection associated to the selected rigid bodies.\n4: Hide all geometry collections.\nDefault = 1"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowRigidBodyId         (TEXT("p.gc.ShowRigidBodyId"         ), GeometryCollectionDebugDrawActorConstants::ShowRigidBodyIdDefault         , TEXT("Geometry Collection debug draw, show the rigid body id(s).\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowRigidBodyCollision  (TEXT("p.gc.ShowRigidBodyCollision"  ), GeometryCollectionDebugDrawActorConstants::ShowRigidBodyCollisionDefault  , TEXT("Geometry Collection debug draw, show the selected's rigid body's collision volume.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > CollisionAtOrigin       (TEXT("p.gc.CollisionAtOrigin"       ), GeometryCollectionDebugDrawActorConstants::CollisionAtOriginDefault       , TEXT("Geometry Collection debug draw, show any collision volume at the origin, in local space.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowRigidBodyTransform  (TEXT("p.gc.ShowRigidBodyTransform"  ), GeometryCollectionDebugDrawActorConstants::ShowRigidBodyTransformDefault  , TEXT("Geometry Collection debug draw, show the selected's rigid body's transform.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowRigidBodyInertia    (TEXT("p.gc.ShowRigidBodyInertia"    ), GeometryCollectionDebugDrawActorConstants::ShowRigidBodyInertiaDefault    , TEXT("Geometry Collection debug draw, show the selected's rigid body's inertia tensor box.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowRigidBodyVelocity   (TEXT("p.gc.ShowRigidBodyVelocity"   ), GeometryCollectionDebugDrawActorConstants::ShowRigidBodyVelocityDefault   , TEXT("Geometry Collection debug draw, show the selected's rigid body's linear and angular velocities.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowRigidBodyForce      (TEXT("p.gc.ShowRigidBodyForce"      ), GeometryCollectionDebugDrawActorConstants::ShowRigidBodyForceDefault      , TEXT("Geometry Collection debug draw, show the selected's rigid body's applied force and torque.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowRigidBodyInfos      (TEXT("p.gc.ShowRigidBodyInfos"      ), GeometryCollectionDebugDrawActorConstants::ShowRigidBodyInfosDefault      , TEXT("Geometry Collection debug draw, show the selected's rigid body's information.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowTransformIndex      (TEXT("p.gc.ShowTransformIndex"      ), GeometryCollectionDebugDrawActorConstants::ShowTransformIndexDefault      , TEXT("Geometry Collection debug draw, show the transform index for the selected rigid body's associated cluster nodes.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowTransform           (TEXT("p.gc.ShowTransform"           ), GeometryCollectionDebugDrawActorConstants::ShowTransformDefault           , TEXT("Geometry Collection debug draw, show the transform for the selected rigid body's associated cluster nodes.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowParent              (TEXT("p.gc.ShowParent"              ), GeometryCollectionDebugDrawActorConstants::ShowParentDefault              , TEXT("Geometry Collection debug draw, show a link from the selected rigid body's associated cluster nodes to their parent's nodes.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowLevel               (TEXT("p.gc.ShowLevel"               ), GeometryCollectionDebugDrawActorConstants::ShowLevelDefault               , TEXT("Geometry Collection debug draw, show the hierarchical level for the selected rigid body's associated cluster nodes.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowConnectivityEdges   (TEXT("p.gc.ShowConnectivityEdges"   ), GeometryCollectionDebugDrawActorConstants::ShowConnectivityEdgesDefault   , TEXT("Geometry Collection debug draw, show the connectivity edges for the rigid body's associated cluster nodes.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowGeometryIndex       (TEXT("p.gc.ShowGeometryIndex"       ), GeometryCollectionDebugDrawActorConstants::ShowGeometryIndexDefault       , TEXT("Geometry Collection debug draw, show the geometry index for the selected rigid body's associated geometries.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowGeometryTransform   (TEXT("p.gc.ShowGeometryTransform"   ), GeometryCollectionDebugDrawActorConstants::ShowGeometryTransformDefault   , TEXT("Geometry Collection debug draw, show the geometry transform for the selected rigid body's associated geometries.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowBoundingBox         (TEXT("p.gc.ShowBoundingBox"         ), GeometryCollectionDebugDrawActorConstants::ShowBoundingBoxDefault         , TEXT("Geometry Collection debug draw, show the bounding box for the selected rigid body's associated geometries.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowFaces               (TEXT("p.gc.ShowFaces"               ), GeometryCollectionDebugDrawActorConstants::ShowFacesDefault               , TEXT("Geometry Collection debug draw, show the faces for the selected rigid body's associated geometries.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowFaceIndices         (TEXT("p.gc.ShowFaceIndices"         ), GeometryCollectionDebugDrawActorConstants::ShowFaceIndicesDefault         , TEXT("Geometry Collection debug draw, show the face indices for the selected rigid body's associated geometries.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowFaceNormals         (TEXT("p.gc.ShowFaceNormals"         ), GeometryCollectionDebugDrawActorConstants::ShowFaceNormalsDefault         , TEXT("Geometry Collection debug draw, show the face normals for the selected rigid body's associated geometries.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowSingleFace          (TEXT("p.gc.ShowSingleFace"          ), GeometryCollectionDebugDrawActorConstants::ShowSingleFaceDefault          , TEXT("Geometry Collection debug draw, enable single face visualization for the selected rigid body's associated geometries.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > SingleFaceIndex         (TEXT("p.gc.SingleFaceIndex"         ), GeometryCollectionDebugDrawActorConstants::SingleFaceIndexDefault         , TEXT("Geometry Collection debug draw, the index of the single face to visualize.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowVertices            (TEXT("p.gc.ShowVertices"            ), GeometryCollectionDebugDrawActorConstants::ShowVerticesDefault            , TEXT("Geometry Collection debug draw, show the vertices for the selected rigid body's associated geometries.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowVertexIndices       (TEXT("p.gc.ShowVertexIndices"       ), GeometryCollectionDebugDrawActorConstants::ShowVertexIndicesDefault       , TEXT("Geometry Collection debug draw, show the vertex index for the selected rigid body's associated geometries.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > ShowVertexNormals       (TEXT("p.gc.ShowVertexNormals"       ), GeometryCollectionDebugDrawActorConstants::ShowVertexNormalsDefault       , TEXT("Geometry Collection debug draw, show the vertex normals for the selected rigid body's associated geometries.\nDefault = 0"), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > UseActiveVisualization  (TEXT("p.gc.UseActiveVisualization"  ), GeometryCollectionDebugDrawActorConstants::UseActiveVisualizationDefault  , TEXT("Geometry Collection debug draw, adapt visualization depending of the cluster nodes' hierarchical level..\nDefault = 1."), ECVF_Cheat);
	static TAutoConsoleVariable<float  > PointThickness          (TEXT("p.gc.PointThickness"          ), GeometryCollectionDebugDrawActorConstants::PointThicknessDefault          , TEXT("Geometry Collection debug draw, point thickness.\nDefault = 6."), ECVF_Cheat);
	static TAutoConsoleVariable<float  > LineThickness           (TEXT("p.gc.LineThickness"           ), GeometryCollectionDebugDrawActorConstants::LineThicknessDefault           , TEXT("Geometry Collection debug draw, line thickness.\nDefault = 1."), ECVF_Cheat);
	static TAutoConsoleVariable<int32  > TextShadow              (TEXT("p.gc.TextShadow"              ), GeometryCollectionDebugDrawActorConstants::TextShadowDefault              , TEXT("Geometry Collection debug draw, text shadow under indices for better readability.\nDefault = 1."), ECVF_Cheat);
	static TAutoConsoleVariable<float  > TextScale               (TEXT("p.gc.TextScale"               ), GeometryCollectionDebugDrawActorConstants::TextScaleDefault               , TEXT("Geometry Collection debug draw, text scale.\nDefault = 1."), ECVF_Cheat);
	static TAutoConsoleVariable<float  > NormalScale             (TEXT("p.gc.NormalScale"             ), GeometryCollectionDebugDrawActorConstants::NormalScaleDefault             , TEXT("Geometry Collection debug draw, normal size.\nDefault = 10."), ECVF_Cheat);
	static TAutoConsoleVariable<float  > AxisScale               (TEXT("p.gc.AxisScale"               ), GeometryCollectionDebugDrawActorConstants::AxisScaleDefault               , TEXT("Geometry Collection debug draw, size of the axis used for visualizing all transforms.\nDefault = 20."), ECVF_Cheat);
	static TAutoConsoleVariable<float  > ArrowScale              (TEXT("p.gc.ArrowScale"              ), GeometryCollectionDebugDrawActorConstants::ArrowScaleDefault              , TEXT("Geometry Collection debug draw, arrow size for normals.\nDefault = 2.5."), ECVF_Cheat);
}

FString FGeometryCollectionDebugDrawActorSelectedRigidBody::GetSolverName() const
{
	return !Solver ? FName(NAME_None).ToString() : Solver->GetName();
}

AGeometryCollectionDebugDrawActor* AGeometryCollectionDebugDrawActor::FindOrCreate(UWorld* World)
{
	AGeometryCollectionDebugDrawActor* Actor = nullptr;
	if (!World)
	{
		UE_LOG(LogGeometryCollectionDebugDrawActor, Warning, TEXT("No valid World for where to search for an existing GeometryCollectionDebugDrawActor singleton actor."));
	}
	else
	{
		const TActorIterator<AGeometryCollectionDebugDrawActor> ActorIterator(World);
		if (ActorIterator)
		{
			Actor = *ActorIterator;
		}
		else
		{
			FActorSpawnParameters SpawnInfo;
			SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Actor = World->SpawnActor<AGeometryCollectionDebugDrawActor>(SpawnInfo);
		}
		if (!Actor)
		{
			UE_LOG(LogGeometryCollectionDebugDrawActor, Warning, TEXT("No GeometryCollectionDebugDrawActor singleton actor could be found or created."));
		}
	}
	return Actor;
}

AGeometryCollectionDebugDrawActor::AGeometryCollectionDebugDrawActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SelectedRigidBody        (  GeometryCollectionDebugDrawActorConstants::SelectedRigidBodyIdDefault     )
	, bDebugDrawWholeCollection(!!GeometryCollectionDebugDrawActorConstants::DebugDrawWholeCollectionDefault)
	, bDebugDrawHierarchy      (!!GeometryCollectionDebugDrawActorConstants::DebugDrawHierarchyDefault      )
	, bDebugDrawClustering     (!!GeometryCollectionDebugDrawActorConstants::DebugDrawClusteringDefault     )
	, HideGeometry             ( EGeometryCollectionDebugDrawActorHideGeometry(
		                          GeometryCollectionDebugDrawActorConstants::HideGeometryDefault            ))
	, bShowRigidBodyId         (!!GeometryCollectionDebugDrawActorConstants::ShowRigidBodyIdDefault         )
	, bShowRigidBodyCollision  (!!GeometryCollectionDebugDrawActorConstants::ShowRigidBodyCollisionDefault  )
	, bCollisionAtOrigin       (!!GeometryCollectionDebugDrawActorConstants::CollisionAtOriginDefault       )
	, bShowRigidBodyTransform  (!!GeometryCollectionDebugDrawActorConstants::ShowRigidBodyTransformDefault  )
	, bShowRigidBodyInertia    (!!GeometryCollectionDebugDrawActorConstants::ShowRigidBodyInertiaDefault    )
	, bShowRigidBodyVelocity   (!!GeometryCollectionDebugDrawActorConstants::ShowRigidBodyVelocityDefault   )
	, bShowRigidBodyForce      (!!GeometryCollectionDebugDrawActorConstants::ShowRigidBodyForceDefault      )
	, bShowRigidBodyInfos      (!!GeometryCollectionDebugDrawActorConstants::ShowRigidBodyInfosDefault      )
	, bShowTransformIndex      (!!GeometryCollectionDebugDrawActorConstants::ShowTransformIndexDefault      )
	, bShowTransform           (!!GeometryCollectionDebugDrawActorConstants::ShowTransformDefault           )
	, bShowParent              (!!GeometryCollectionDebugDrawActorConstants::ShowParentDefault              )
	, bShowLevel               (!!GeometryCollectionDebugDrawActorConstants::ShowLevelDefault               )
	, bShowConnectivityEdges   (!!GeometryCollectionDebugDrawActorConstants::ShowConnectivityEdgesDefault   )
	, bShowGeometryIndex       (!!GeometryCollectionDebugDrawActorConstants::ShowGeometryIndexDefault       )
	, bShowGeometryTransform   (!!GeometryCollectionDebugDrawActorConstants::ShowGeometryTransformDefault   )
	, bShowBoundingBox         (!!GeometryCollectionDebugDrawActorConstants::ShowBoundingBoxDefault         )
	, bShowFaces               (!!GeometryCollectionDebugDrawActorConstants::ShowFacesDefault               )
	, bShowFaceIndices         (!!GeometryCollectionDebugDrawActorConstants::ShowFaceIndicesDefault         )
	, bShowFaceNormals         (!!GeometryCollectionDebugDrawActorConstants::ShowFaceNormalsDefault         )
	, bShowVertices            (!!GeometryCollectionDebugDrawActorConstants::ShowVerticesDefault            )
	, bShowVertexIndices       (!!GeometryCollectionDebugDrawActorConstants::ShowVertexIndicesDefault       )
	, bShowVertexNormals       (!!GeometryCollectionDebugDrawActorConstants::ShowVertexNormalsDefault       )
	, bUseActiveVisualization  (!!GeometryCollectionDebugDrawActorConstants::UseActiveVisualizationDefault  )
	, PointThickness           (  GeometryCollectionDebugDrawActorConstants::PointThicknessDefault          )
	, LineThickness            (  GeometryCollectionDebugDrawActorConstants::LineThicknessDefault           )
	, bTextShadow              (!!GeometryCollectionDebugDrawActorConstants::TextShadowDefault              )
	, TextScale                (  GeometryCollectionDebugDrawActorConstants::TextScaleDefault               )
	, NormalScale              (  GeometryCollectionDebugDrawActorConstants::NormalScaleDefault             )
	, AxisScale                (  GeometryCollectionDebugDrawActorConstants::AxisScaleDefault               )
	, ArrowScale               (  GeometryCollectionDebugDrawActorConstants::ArrowScaleDefault              )
	, RigidBodyIdColor         (  GeometryCollectionDebugDrawActorConstants::RigidBodyIdsColorDefault       )
	, RigidBodyTransformScale  (  GeometryCollectionDebugDrawActorConstants::TransformScaleDefault          )
	, RigidBodyCollisionColor  (  GeometryCollectionDebugDrawActorConstants::RigidBodyCollisionColorDefault )
	, RigidBodyInertiaColor    (  GeometryCollectionDebugDrawActorConstants::RigidBodyInertiaColorDefault   )
	, RigidBodyVelocityColor   (  GeometryCollectionDebugDrawActorConstants::RigidBodyVelocityColorDefault  )
	, RigidBodyForceColor      (  GeometryCollectionDebugDrawActorConstants::RigidBodyForceColorDefault     )
	, RigidBodyInfoColor       (  GeometryCollectionDebugDrawActorConstants::RigidBodyInfoColorDefault      )
	, TransformIndexColor      (  GeometryCollectionDebugDrawActorConstants::TransformIndexColorDefault     )
	, TransformScale           (  GeometryCollectionDebugDrawActorConstants::TransformScaleDefault          )
	, LevelColor               (  GeometryCollectionDebugDrawActorConstants::LevelColorDefault              )
	, ParentColor              (  GeometryCollectionDebugDrawActorConstants::ParentColorDefault             )
	, ConnectivityEdgeThickness(  GeometryCollectionDebugDrawActorConstants::LineThicknessDefault           )
	, GeometryIndexColor       (  GeometryCollectionDebugDrawActorConstants::GeometryIndexColorDefault      )
	, GeometryTransformScale   (  GeometryCollectionDebugDrawActorConstants::TransformScaleDefault          )
	, BoundingBoxColor         (  GeometryCollectionDebugDrawActorConstants::BoundingBoxColorDefault        )
	, FaceColor                (  GeometryCollectionDebugDrawActorConstants::FaceColorDefault               )
	, FaceIndexColor           (  GeometryCollectionDebugDrawActorConstants::FaceIndexColorDefault          )
	, FaceNormalColor          (  GeometryCollectionDebugDrawActorConstants::FaceNormalColorDefault         )
	, SingleFaceColor          (  GeometryCollectionDebugDrawActorConstants::SingleFaceColorDefault         )
	, VertexColor              (  GeometryCollectionDebugDrawActorConstants::VertexColorDefault             )
	, VertexIndexColor         (  GeometryCollectionDebugDrawActorConstants::VertexIndexColorDefault        )
	, VertexNormalColor        (  GeometryCollectionDebugDrawActorConstants::VertexNormalColorDefault       )
	, SpriteComponent()
	, ConsoleVariableSinkHandle()
	, DebugDrawTextDelegateHandle()
	, DebugDrawTexts()
	, bNeedsDebugLinesFlush(false)
#if WITH_EDITOR
	, bWasEditorPaused(false)
#endif
{
	// Enable game tick calls
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;  // Debug draw must still runs while paused
	SetActorTickEnabled(true);

	// Register console variable sink
	ConsoleVariableSinkHandle = IConsoleManager::Get().RegisterConsoleVariableSink_Handle(FConsoleCommandDelegate::CreateUObject(this, &AGeometryCollectionDebugDrawActor::OnCVarsChanged));

	// Create scene component
	RootComponent = ObjectInitializer.CreateDefaultSubobject<USceneComponent>(this, TEXT("SceneComponent"));
	RootComponent->Mobility = EComponentMobility::Static;

#if WITH_EDITORONLY_DATA
	// Create and attach sprite
	SpriteComponent = ObjectInitializer.CreateEditorOnlyDefaultSubobject<UBillboardComponent>(this, TEXT("Sprite"));
	if (SpriteComponent)
	{
		// Structure to hold one-time sprite initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> NoteTextureObject;  // A helper class object used to find target UTexture2D object in resource package
			FName ID_Notes;  // Icon sprite category name
			FText NAME_Notes;  // Icon sprite display name

			FConstructorStatics()
				: NoteTextureObject(TEXT("/Engine/EditorResources/S_Note"))
				, ID_Notes(TEXT("Notes"))
				, NAME_Notes(NSLOCTEXT("SpriteCategory", "Notes", "Notes"))
			{}
		};
		static FConstructorStatics ConstructorStatics;
		SpriteComponent->Sprite = ConstructorStatics.NoteTextureObject.Get();
		SpriteComponent->SpriteInfo.Category = ConstructorStatics.ID_Notes;
		SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_Notes;
		SpriteComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		SpriteComponent->Mobility = EComponentMobility::Static;
	}
#endif // WITH_EDITORONLY_DATA
}

void AGeometryCollectionDebugDrawActor::BeginDestroy()
{
	// Unregister console variable sink
	IConsoleManager::Get().UnregisterConsoleVariableSink_Handle(ConsoleVariableSinkHandle);

	Super::BeginDestroy();
}

void AGeometryCollectionDebugDrawActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Clear all persistent strings and debug lines.
	Flush();

	UWorld* const World = GetWorld();
#if WITH_EDITOR
	// Check editor pause status and force a dynamic update on all components to catchup with the physics thread
	// This can't be done in the GeometryCollectionDebugDrawComponent since it doesn't tick at every frame,
	// and can't be done in GeometryCollectionComponent either since it doesn't usually tick while paused.
	const bool bIsEditorPaused = World && World->IsPlayInEditor() && World->bDebugPauseExecution;
	if (bIsEditorPaused && !bWasEditorPaused)
	{
		// For dynamic update of transforms
		for (TActorIterator<AGeometryCollectionActor> ActorIterator(World); ActorIterator; ++ActorIterator)
		{
			UGeometryCollectionDebugDrawComponent* const GeometryCollectionDebugDrawComponent = ActorIterator->GetGeometryCollectionDebugDrawComponent();
			if (GeometryCollectionDebugDrawComponent &&
				GeometryCollectionDebugDrawComponent->GeometryCollectionDebugDrawActor == this &&
				ensure(GeometryCollectionDebugDrawComponent->GeometryCollectionComponent))
			{
				GeometryCollectionDebugDrawComponent->GeometryCollectionComponent->ForceRenderUpdateDynamicData();
			}
		}
	}
	bWasEditorPaused = bIsEditorPaused;
#endif  // #if WITH_EDITOR

#if GEOMETRYCOLLECTION_DEBUG_DRAW
	// Check badly synced collections in case it is still looking for an id match
	if (World && SelectedRigidBody.Id != INDEX_NONE && !SelectedRigidBody.GeometryCollection)
	//if (World && SelectedRigidBody.Id.IsValid() && !SelectedRigidBody.GeometryCollection)
	{
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		// Check the id is within the selected solver range
		const Chaos::FPBDRigidsSolver* const Solver = 
			SelectedRigidBody.Solver ? SelectedRigidBody.Solver->GetSolver() :  // Selected solver
			World->PhysicsScene_Chaos ? World->PhysicsScene_Chaos->GetSolver() :  // Default world solver
			nullptr;  // No solver

		const bool IsWithinRange = Solver ? (uint32(SelectedRigidBody.Id) < Solver->GetRigidParticles().Size()): false;
		if (!IsWithinRange)
		{
			UE_LOG(LogGeometryCollectionDebugDrawActor, VeryVerbose, TEXT("The selection id is out of range."));
		}
		else  // Statement continues below...
#endif  // #if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		{
			UE_LOG(LogGeometryCollectionDebugDrawActor, VeryVerbose, TEXT("The selection couldn't be found. The property update will run on all components still containing any invalid rigid body ids."));

			// Check for delayed Rigid Body Id array initializations
			for (TActorIterator<AGeometryCollectionActor> ActorIterator(World); ActorIterator; ++ActorIterator)
			{
				if (UGeometryCollectionDebugDrawComponent* const GeometryCollectionDebugDrawComponent = ActorIterator->GetGeometryCollectionDebugDrawComponent())
				{
					if (GeometryCollectionDebugDrawComponent->GeometryCollectionDebugDrawActor == this &&
						GeometryCollectionDebugDrawComponent->HasIncompleteRigidBodyIdSync())
					{
						const bool bIsSelected = GeometryCollectionDebugDrawComponent->OnDebugDrawPropertiesChanged(false);
						if (bIsSelected)
						{
							SelectedRigidBody.GeometryCollection = *ActorIterator;
							UE_LOG(LogGeometryCollectionDebugDrawActor, Verbose, TEXT("Selection found. Stopping continuous property update."));
							break;
						}
					}
				}
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
}

void AGeometryCollectionDebugDrawActor::BeginPlay()
{
	Super::BeginPlay();
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	// Initialize text renderer
	const FDebugDrawDelegate DebugDrawTextDelegate = FDebugDrawDelegate::CreateUObject(this, &AGeometryCollectionDebugDrawActor::DebugDrawText);
	DebugDrawTextDelegateHandle = UDebugDrawService::Register(TEXT("TextRender"), DebugDrawTextDelegate);  // TextRender is an engine show flag that works in both editor and game modes
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::EndPlay(EEndPlayReason::Type ReasonEnd)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	UDebugDrawService::Unregister(DebugDrawTextDelegateHandle);
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	Super::EndPlay(ReasonEnd);
}

void AGeometryCollectionDebugDrawActor::PostLoad()
{
	static const EConsoleVariableFlags SetBy = ECVF_SetByConsole;  // Can't use the default ECVF_SetByCode as otherwise it won't update the global console variable.

	GeometryCollectionDebugDrawActorCVars::SelectedRigidBodySolver ->Set(*SelectedRigidBody.GetSolverName(), SetBy);
	GeometryCollectionDebugDrawActorCVars::SelectedRigidBodyId     ->Set( SelectedRigidBody.Id             , SetBy);
	//GeometryCollectionDebugDrawActorCVars::SelectedRigidBodyId     ->Set(*SelectedRigidBody.Id.ToString()  , SetBy);
	GeometryCollectionDebugDrawActorCVars::DebugDrawWholeCollection->Set(int32(bDebugDrawWholeCollection  ), SetBy);
	GeometryCollectionDebugDrawActorCVars::DebugDrawHierarchy      ->Set(int32(bDebugDrawHierarchy        ), SetBy);
	GeometryCollectionDebugDrawActorCVars::DebugDrawClustering     ->Set(int32(bDebugDrawClustering       ), SetBy);
	GeometryCollectionDebugDrawActorCVars::HideGeometry            ->Set(int32(HideGeometry               ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowRigidBodyId         ->Set(int32(bShowRigidBodyId           ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowRigidBodyCollision  ->Set(int32(bShowRigidBodyCollision    ), SetBy);
	GeometryCollectionDebugDrawActorCVars::CollisionAtOrigin       ->Set(int32(bCollisionAtOrigin         ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowRigidBodyTransform  ->Set(int32(bShowRigidBodyTransform    ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowRigidBodyInertia    ->Set(int32(bShowRigidBodyInertia      ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowRigidBodyVelocity   ->Set(int32(bShowRigidBodyVelocity     ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowRigidBodyForce      ->Set(int32(bShowRigidBodyForce        ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowRigidBodyInfos      ->Set(int32(bShowRigidBodyInfos        ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowTransformIndex      ->Set(int32(bShowTransformIndex        ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowTransform           ->Set(int32(bShowTransform             ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowParent              ->Set(int32(bShowParent                ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowLevel               ->Set(int32(bShowLevel                 ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowConnectivityEdges   ->Set(int32(bShowConnectivityEdges     ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowGeometryIndex       ->Set(int32(bShowGeometryIndex         ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowGeometryTransform   ->Set(int32(bShowGeometryTransform     ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowBoundingBox         ->Set(int32(bShowBoundingBox           ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowFaces               ->Set(int32(bShowFaces                 ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowFaceIndices         ->Set(int32(bShowFaceIndices           ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowFaceNormals         ->Set(int32(bShowFaceNormals           ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowVertices            ->Set(int32(bShowVertices              ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowVertexIndices       ->Set(int32(bShowVertexIndices         ), SetBy);
	GeometryCollectionDebugDrawActorCVars::ShowVertexNormals       ->Set(int32(bShowVertexNormals         ), SetBy);
	GeometryCollectionDebugDrawActorCVars::UseActiveVisualization  ->Set(int32(bUseActiveVisualization    ), SetBy);
	GeometryCollectionDebugDrawActorCVars::PointThickness          ->Set(      PointThickness              , SetBy);
	GeometryCollectionDebugDrawActorCVars::LineThickness           ->Set(      LineThickness               , SetBy);
	GeometryCollectionDebugDrawActorCVars::TextShadow              ->Set(int32(bTextShadow                ), SetBy);
	GeometryCollectionDebugDrawActorCVars::TextScale               ->Set(      TextScale                   , SetBy);
	GeometryCollectionDebugDrawActorCVars::NormalScale             ->Set(      NormalScale                 , SetBy);
	GeometryCollectionDebugDrawActorCVars::AxisScale               ->Set(      AxisScale                   , SetBy);
	GeometryCollectionDebugDrawActorCVars::ArrowScale              ->Set(      ArrowScale                  , SetBy);

	Super::PostLoad();
}

FColor AGeometryCollectionDebugDrawActor::MakeDarker(const FColor& Color, int32 Level)
{
	FLinearColor LinearColor(Color);
	LinearColor = LinearColor.LinearRGBToHSV();
	LinearColor.Component(2) /= float(1L << Level);
	return LinearColor.HSVToLinearRGB().ToFColor(true);
}

int32 AGeometryCollectionDebugDrawActor::GetLevel(int32 TransformIndex, const TManagedArray<int32>& Parents)
{
	check(TransformIndex != FGeometryCollection::Invalid);
	int32 Level = 0;
	while ((TransformIndex = Parents[TransformIndex]) != FGeometryCollection::Invalid)
	{
		++Level;
	}
	return Level;
}

void AGeometryCollectionDebugDrawActor::OnPropertyChanged(bool bForceVisibilityUpdate)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW
	// Reset selected rigid body's actor
	SelectedRigidBody.GeometryCollection = nullptr;

	// Update component states
	UWorld* const World = GetWorld();
	if (!World || !World->HasBegunPlay() || !HasActorBegunPlay()) { return; }

	for (TActorIterator<AGeometryCollectionActor> ActorIterator(World); ActorIterator; ++ActorIterator)
	{
		UGeometryCollectionDebugDrawComponent* const GeometryCollectionDebugDrawComponent = ActorIterator->GetGeometryCollectionDebugDrawComponent();
		if (GeometryCollectionDebugDrawComponent)
		{
			check(GeometryCollectionDebugDrawComponent->GeometryCollectionDebugDrawActor == this);
			const bool bIsSelected = GeometryCollectionDebugDrawComponent->OnDebugDrawPropertiesChanged(bForceVisibilityUpdate);
			if (bIsSelected)
			{
				SelectedRigidBody.GeometryCollection = *ActorIterator;
			}
		}
	}
#endif
}

template<typename T1, typename T2>
void AGeometryCollectionDebugDrawActor::UpdatePropertyValue(T1& PropertyValue, const TAutoConsoleVariable<T2>& ConsoleVariable, bool& bHasChanged)
{
	const T1 NewValue = static_cast<T1>(ConsoleVariable.GetValueOnGameThread());
	if (PropertyValue != NewValue)
	{
		bHasChanged = true;
		PropertyValue = NewValue;
	}
}

template<>
void AGeometryCollectionDebugDrawActor::UpdatePropertyValue<AChaosSolverActor*, FString>(AChaosSolverActor*& PropertyValue, const TAutoConsoleVariable<FString>& ConsoleVariable, bool& bHasChanged)
{
	AChaosSolverActor* NewValue = nullptr;

	if (UWorld* const World = GetWorld())
	{
		const FString SolverName = ConsoleVariable.GetValueOnGameThread();
		for (TActorIterator<AChaosSolverActor> ActorIterator(World); ActorIterator; ++ActorIterator)
		{
			if (ActorIterator->GetName() == SolverName)
			{
				NewValue = *ActorIterator;
				break;
			}
		}
	}

	if (PropertyValue != NewValue)
	{
		bHasChanged = true;
		PropertyValue = NewValue;
	}
}

void AGeometryCollectionDebugDrawActor::OnCVarsChanged()
{
	// Discard callback if this actor isn't in any world
	if (!GetWorld()) { return; }

	// Update properties from cvars
	bool bHavePropertiesChanged = false;
	bool bHasDebugDrawClusteringChanged = false;

	UpdatePropertyValue(SelectedRigidBody.Solver , GeometryCollectionDebugDrawActorCVars::SelectedRigidBodySolver , bHavePropertiesChanged);
	UpdatePropertyValue(SelectedRigidBody.Id     , GeometryCollectionDebugDrawActorCVars::SelectedRigidBodyId     , bHavePropertiesChanged);
	UpdatePropertyValue(bDebugDrawWholeCollection, GeometryCollectionDebugDrawActorCVars::DebugDrawWholeCollection, bHavePropertiesChanged);
	UpdatePropertyValue(bDebugDrawHierarchy      , GeometryCollectionDebugDrawActorCVars::DebugDrawHierarchy      , bHavePropertiesChanged);
	UpdatePropertyValue(bDebugDrawClustering     , GeometryCollectionDebugDrawActorCVars::DebugDrawClustering     , bHasDebugDrawClusteringChanged);
	UpdatePropertyValue(HideGeometry             , GeometryCollectionDebugDrawActorCVars::HideGeometry            , bHavePropertiesChanged);
	UpdatePropertyValue(bShowRigidBodyId         , GeometryCollectionDebugDrawActorCVars::ShowRigidBodyId         , bHavePropertiesChanged);
	UpdatePropertyValue(bShowRigidBodyCollision  , GeometryCollectionDebugDrawActorCVars::ShowRigidBodyCollision  , bHavePropertiesChanged);
	UpdatePropertyValue(bCollisionAtOrigin       , GeometryCollectionDebugDrawActorCVars::CollisionAtOrigin       , bHavePropertiesChanged);
	UpdatePropertyValue(bShowRigidBodyTransform  , GeometryCollectionDebugDrawActorCVars::ShowRigidBodyTransform  , bHavePropertiesChanged);
	UpdatePropertyValue(bShowRigidBodyInertia    , GeometryCollectionDebugDrawActorCVars::ShowRigidBodyInertia    , bHavePropertiesChanged);
	UpdatePropertyValue(bShowRigidBodyVelocity   , GeometryCollectionDebugDrawActorCVars::ShowRigidBodyVelocity   , bHavePropertiesChanged);
	UpdatePropertyValue(bShowRigidBodyForce      , GeometryCollectionDebugDrawActorCVars::ShowRigidBodyForce      , bHavePropertiesChanged);
	UpdatePropertyValue(bShowRigidBodyInfos      , GeometryCollectionDebugDrawActorCVars::ShowRigidBodyInfos      , bHavePropertiesChanged);
	UpdatePropertyValue(bShowTransformIndex      , GeometryCollectionDebugDrawActorCVars::ShowTransformIndex      , bHavePropertiesChanged);
	UpdatePropertyValue(bShowTransform           , GeometryCollectionDebugDrawActorCVars::ShowTransform           , bHavePropertiesChanged);
	UpdatePropertyValue(bShowParent              , GeometryCollectionDebugDrawActorCVars::ShowParent              , bHavePropertiesChanged);
	UpdatePropertyValue(bShowLevel               , GeometryCollectionDebugDrawActorCVars::ShowLevel               , bHavePropertiesChanged);
	UpdatePropertyValue(bShowConnectivityEdges   , GeometryCollectionDebugDrawActorCVars::ShowConnectivityEdges   , bHavePropertiesChanged);
	UpdatePropertyValue(bShowGeometryIndex       , GeometryCollectionDebugDrawActorCVars::ShowGeometryIndex       , bHavePropertiesChanged);
	UpdatePropertyValue(bShowGeometryTransform   , GeometryCollectionDebugDrawActorCVars::ShowGeometryTransform   , bHavePropertiesChanged);
	UpdatePropertyValue(bShowBoundingBox         , GeometryCollectionDebugDrawActorCVars::ShowBoundingBox         , bHavePropertiesChanged);
	UpdatePropertyValue(bShowFaces               , GeometryCollectionDebugDrawActorCVars::ShowFaces               , bHavePropertiesChanged);
	UpdatePropertyValue(bShowFaceIndices         , GeometryCollectionDebugDrawActorCVars::ShowFaceIndices         , bHavePropertiesChanged);
	UpdatePropertyValue(bShowFaceNormals         , GeometryCollectionDebugDrawActorCVars::ShowFaceNormals         , bHavePropertiesChanged);
	UpdatePropertyValue(bShowSingleFace          , GeometryCollectionDebugDrawActorCVars::ShowSingleFace          , bHavePropertiesChanged);
	UpdatePropertyValue(SingleFaceIndex          , GeometryCollectionDebugDrawActorCVars::SingleFaceIndex         , bHavePropertiesChanged);
	UpdatePropertyValue(bShowVertices            , GeometryCollectionDebugDrawActorCVars::ShowVertices            , bHavePropertiesChanged);
	UpdatePropertyValue(bShowVertexIndices       , GeometryCollectionDebugDrawActorCVars::ShowVertexIndices       , bHavePropertiesChanged);
	UpdatePropertyValue(bShowVertexNormals       , GeometryCollectionDebugDrawActorCVars::ShowVertexNormals       , bHavePropertiesChanged);
	UpdatePropertyValue(bUseActiveVisualization  , GeometryCollectionDebugDrawActorCVars::UseActiveVisualization  , bHavePropertiesChanged);
	UpdatePropertyValue(PointThickness           , GeometryCollectionDebugDrawActorCVars::PointThickness          , bHavePropertiesChanged);
	UpdatePropertyValue(LineThickness            , GeometryCollectionDebugDrawActorCVars::LineThickness           , bHavePropertiesChanged);
	UpdatePropertyValue(bTextShadow              , GeometryCollectionDebugDrawActorCVars::TextShadow              , bHavePropertiesChanged);
	UpdatePropertyValue(TextScale                , GeometryCollectionDebugDrawActorCVars::TextScale               , bHavePropertiesChanged);
	UpdatePropertyValue(NormalScale              , GeometryCollectionDebugDrawActorCVars::NormalScale             , bHavePropertiesChanged);
	UpdatePropertyValue(AxisScale                , GeometryCollectionDebugDrawActorCVars::AxisScale               , bHavePropertiesChanged);
	UpdatePropertyValue(ArrowScale               , GeometryCollectionDebugDrawActorCVars::ArrowScale              , bHavePropertiesChanged);

	// Update geometry collection component, but only if this actor has begun play
	if (bHavePropertiesChanged || bHasDebugDrawClusteringChanged)
	{
		OnPropertyChanged(bHasDebugDrawClusteringChanged);
	}
}

#if WITH_EDITOR
void AGeometryCollectionDebugDrawActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Synchronize the command variables to this Actor's properties if the property name matches.
	static const EConsoleVariableFlags SetBy = ECVF_SetByConsole;  // Can't use the default ECVF_SetByCode as otherwise changing the UI won't update the global console variable.
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	bool bForceVisibilityUpdate = false;

	if      (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, SelectedRigidBody        )) { GeometryCollectionDebugDrawActorCVars::SelectedRigidBodySolver ->Set(*SelectedRigidBody.GetSolverName(), SetBy);
																													  GeometryCollectionDebugDrawActorCVars::SelectedRigidBodyId     ->Set( SelectedRigidBody.Id             , SetBy); }
//																													  GeometryCollectionDebugDrawActorCVars::SelectedRigidBodyId     ->Set(*SelectedRigidBody.Id.ToString()  , SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FGeometryCollectionDebugDrawActorSelectedRigidBody, Solver  )) { GeometryCollectionDebugDrawActorCVars::SelectedRigidBodySolver ->Set(*SelectedRigidBody.GetSolverName(), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FGeometryCollectionDebugDrawActorSelectedRigidBody, Id      )) { GeometryCollectionDebugDrawActorCVars::SelectedRigidBodyId     ->Set( SelectedRigidBody.Id             , SetBy); }
//	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FGeometryCollectionDebugDrawActorSelectedRigidBody, Id      )) { GeometryCollectionDebugDrawActorCVars::SelectedRigidBodyId     ->Set(*SelectedRigidBody.Id.ToString()  , SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bDebugDrawWholeCollection)) { GeometryCollectionDebugDrawActorCVars::DebugDrawWholeCollection->Set(int32(bDebugDrawWholeCollection  ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bDebugDrawHierarchy      )) { GeometryCollectionDebugDrawActorCVars::DebugDrawHierarchy      ->Set(int32(bDebugDrawHierarchy        ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bDebugDrawClustering     )) { GeometryCollectionDebugDrawActorCVars::DebugDrawClustering     ->Set(int32(bDebugDrawClustering       ), SetBy); bForceVisibilityUpdate = true; }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, HideGeometry             )) { GeometryCollectionDebugDrawActorCVars::HideGeometry            ->Set(int32(HideGeometry               ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowRigidBodyId         )) { GeometryCollectionDebugDrawActorCVars::ShowRigidBodyId         ->Set(int32(bShowRigidBodyId           ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowRigidBodyCollision  )) { GeometryCollectionDebugDrawActorCVars::ShowRigidBodyCollision  ->Set(int32(bShowRigidBodyCollision    ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bCollisionAtOrigin       )) { GeometryCollectionDebugDrawActorCVars::CollisionAtOrigin       ->Set(int32(bCollisionAtOrigin         ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowRigidBodyTransform  )) { GeometryCollectionDebugDrawActorCVars::ShowRigidBodyTransform  ->Set(int32(bShowRigidBodyTransform    ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowRigidBodyInertia    )) { GeometryCollectionDebugDrawActorCVars::ShowRigidBodyInertia    ->Set(int32(bShowRigidBodyInertia      ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowRigidBodyVelocity   )) { GeometryCollectionDebugDrawActorCVars::ShowRigidBodyVelocity   ->Set(int32(bShowRigidBodyVelocity     ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowRigidBodyForce      )) { GeometryCollectionDebugDrawActorCVars::ShowRigidBodyForce      ->Set(int32(bShowRigidBodyForce        ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowRigidBodyInfos      )) { GeometryCollectionDebugDrawActorCVars::ShowRigidBodyInfos      ->Set(int32(bShowRigidBodyInfos        ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowTransformIndex      )) { GeometryCollectionDebugDrawActorCVars::ShowTransformIndex      ->Set(int32(bShowTransformIndex        ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowTransform           )) { GeometryCollectionDebugDrawActorCVars::ShowTransform           ->Set(int32(bShowTransform             ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowParent              )) { GeometryCollectionDebugDrawActorCVars::ShowParent              ->Set(int32(bShowParent                ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowLevel               )) { GeometryCollectionDebugDrawActorCVars::ShowLevel               ->Set(int32(bShowLevel                 ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowConnectivityEdges   )) { GeometryCollectionDebugDrawActorCVars::ShowConnectivityEdges   ->Set(int32(bShowConnectivityEdges     ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowGeometryIndex       )) { GeometryCollectionDebugDrawActorCVars::ShowGeometryIndex       ->Set(int32(bShowGeometryIndex         ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowGeometryTransform   )) { GeometryCollectionDebugDrawActorCVars::ShowGeometryTransform   ->Set(int32(bShowGeometryTransform     ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowBoundingBox         )) { GeometryCollectionDebugDrawActorCVars::ShowBoundingBox         ->Set(int32(bShowBoundingBox           ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowFaces               )) { GeometryCollectionDebugDrawActorCVars::ShowFaces               ->Set(int32(bShowFaces                 ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowFaceIndices         )) { GeometryCollectionDebugDrawActorCVars::ShowFaceIndices         ->Set(int32(bShowFaceIndices           ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowFaceNormals         )) { GeometryCollectionDebugDrawActorCVars::ShowFaceNormals         ->Set(int32(bShowFaceNormals           ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowSingleFace          )) { GeometryCollectionDebugDrawActorCVars::ShowSingleFace          ->Set(int32(bShowSingleFace            ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, SingleFaceIndex          )) { GeometryCollectionDebugDrawActorCVars::SingleFaceIndex         ->Set(      SingleFaceIndex             , SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowVertices            )) { GeometryCollectionDebugDrawActorCVars::ShowVertices            ->Set(int32(bShowVertices              ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowVertexIndices       )) { GeometryCollectionDebugDrawActorCVars::ShowVertexIndices       ->Set(int32(bShowVertexIndices         ), SetBy); }
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowVertexNormals       )) { GeometryCollectionDebugDrawActorCVars::ShowVertexNormals       ->Set(int32(bShowVertexNormals         ), SetBy); }
	else
	{
		// These properties are cosmetic changes and don't require visibility updates or enabling the component tick
		if      (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bUseActiveVisualization  )) { GeometryCollectionDebugDrawActorCVars::UseActiveVisualization  ->Set(int32(bUseActiveVisualization    ), SetBy); }
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, PointThickness           )) { GeometryCollectionDebugDrawActorCVars::PointThickness          ->Set(      PointThickness              , SetBy); }
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, LineThickness            )) { GeometryCollectionDebugDrawActorCVars::LineThickness           ->Set(      LineThickness               , SetBy); }
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bTextShadow              )) { GeometryCollectionDebugDrawActorCVars::TextShadow              ->Set(int32(bTextShadow                ), SetBy); }
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, TextScale                )) { GeometryCollectionDebugDrawActorCVars::TextScale               ->Set(      TextScale                   , SetBy); }
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, NormalScale              )) { GeometryCollectionDebugDrawActorCVars::NormalScale             ->Set(      NormalScale                 , SetBy); }
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, AxisScale                )) { GeometryCollectionDebugDrawActorCVars::AxisScale               ->Set(      AxisScale                   , SetBy); }
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, ArrowScale               )) { GeometryCollectionDebugDrawActorCVars::ArrowScale              ->Set(      ArrowScale                  , SetBy); }

		Super::PostEditChangeProperty(PropertyChangedEvent);
		return;  // Don't call OnPropertyChanged()
	}
	OnPropertyChanged(bForceVisibilityUpdate);
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool AGeometryCollectionDebugDrawActor::CanEditChange(const FProperty* InProperty) const
{
	const FName PropertyName = InProperty->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bDebugDrawWholeCollection) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AGeometryCollectionDebugDrawActor, bShowSingleFace))
	{
		return SelectedRigidBody.Id != INDEX_NONE;
		//return SelectedRigidBody.Id.IsValid();
	}
	return Super::CanEditChange(InProperty);
}
#endif  // #if WITH_EDITOR

void AGeometryCollectionDebugDrawActor::AddDebugText(const FString& Text, const FVector& Position, const FColor& Color, float Scale, bool bDrawShadow)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	DebugDrawTexts.Add({ Text, Position, Color, Scale, bDrawShadow });
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DebugDrawText(UCanvas* Canvas, APlayerController* PlayerController)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	for (const FDebugDrawText& DebugDrawText : DebugDrawTexts)
	{
		const FVector Position = Canvas->Project(DebugDrawText.Position);
		if (Position.Z < KINDA_SMALL_NUMBER) { continue; }  // Don't draw behind the camera

		const FVector2D Postion2D(FMath::CeilToFloat(Position.X), FMath::CeilToFloat(Position.Y));
		const FText Text = FText::FromString(DebugDrawText.Text);

		FCanvasTextItem TextItem(Postion2D, Text, GEngine->GetSmallFont(), DebugDrawText.Color);
		TextItem.Scale = FVector2D(DebugDrawText.Scale, DebugDrawText.Scale);
		if (DebugDrawText.bDrawShadow)
		{
			TextItem.EnableShadow(FLinearColor::Black);
		}
		else
		{
			TextItem.DisableShadow();
		}
		TextItem.Draw(Canvas->Canvas);
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::Flush()
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	DebugDrawTexts.Reset();

	// Note that a flush will remove all the other persistent debug draw elements, so best to only do them when needed
	if (bNeedsDebugLinesFlush)  // Only flush if a geometry collection debug draw function has been drawing lines
	{
		const UWorld* const World = GetWorld();
		check(World);
		FlushPersistentDebugLines(World);
		bNeedsDebugLinesFlush = false;
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawVertices(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformIndexArray = GeometryCollectionComponent->GetTransformIndexArray();
	const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
	const TManagedArray<int32>& VertexStartArray = GeometryCollectionComponent->GetVertexStartArray();
	const TManagedArray<int32>& VertexCountArray = GeometryCollectionComponent->GetVertexCountArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	const int32 NumGeometries = GeometryCollectionComponent->GetNumElements(FGeometryCollection::GeometryGroup);
	for (int32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
	{
		const int32 TransformIndex = TransformIndexArray[GeometryIndex];

		bool bIsVisible;
		const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
		if (bDebugDrawHierarchy)
		{
			const bool bIsLeafNode = (ChildrenArray[TransformIndex].Num() == 0);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			bIsVisible = (!bHasParent && bIsLeafNode == bIsLeafNodeRest) || (bHasParent && bDebugDrawClustering);
		}
		else
		{
			bIsVisible = bIsLeafNodeRest;
		}
		if (bIsVisible)
		{
			const FTransform& Transform = GlobalTransforms[TransformIndex];

			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)): Color;

			const int32 VertexStart = VertexStartArray[GeometryIndex];
			const int32 VertexEnd = VertexStart + VertexCountArray[GeometryIndex];

			for (int32 VertexIndex = VertexStart; VertexIndex < VertexEnd; ++VertexIndex)
			{
				const FVector Position = Transform.TransformPosition(VertexArray[VertexIndex]);
				DrawDebugPoint(World, Position, PointThickness, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority);
			}
			bNeedsDebugLinesFlush = true;
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawVertices(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
	const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	// Debug draw this geometry
	if (GeometryIndex != INDEX_NONE && (bIsLeafNodeRest || bDebugDrawHierarchy))
	{
		const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
		const TManagedArray<int32>& VertexStartArray = GeometryCollectionComponent->GetVertexStartArray();
		const TManagedArray<int32>& VertexCountArray = GeometryCollectionComponent->GetVertexCountArray();
		const int32 VertexStart = VertexStartArray[GeometryIndex];
		const int32 VertexEnd = VertexStart + VertexCountArray[GeometryIndex];

		const FTransform& Transform = GlobalTransforms[TransformIndex];

		for (int32 VertexIndex = VertexStart; VertexIndex < VertexEnd; ++VertexIndex)
		{
			const FVector Position = Transform.TransformPosition(VertexArray[VertexIndex]);
			DrawDebugPoint(World, Position, PointThickness, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority);
		}
		bNeedsDebugLinesFlush = true;
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawVertices(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveColor);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawVertexIndices(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformIndexArray = GeometryCollectionComponent->GetTransformIndexArray();
	const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
	const TManagedArray<int32>& VertexStartArray = GeometryCollectionComponent->GetVertexStartArray();
	const TManagedArray<int32>& VertexCountArray = GeometryCollectionComponent->GetVertexCountArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	const int32 NumGeometries = GeometryCollectionComponent->GetNumElements(FGeometryCollection::GeometryGroup);
	for (int32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
	{
		const int32 TransformIndex = TransformIndexArray[GeometryIndex];

		bool bIsVisible;
		const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
		if (bDebugDrawHierarchy)
		{
			const bool bIsLeafNode = (ChildrenArray[TransformIndex].Num() == 0);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			bIsVisible = (!bHasParent && bIsLeafNode == bIsLeafNodeRest) || (bHasParent && bDebugDrawClustering);
		}
		else
		{
			bIsVisible = bIsLeafNodeRest;
		}
		if (bIsVisible)
		{
			const FTransform& Transform = GlobalTransforms[TransformIndex];

			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;

			const int32 VertexStart = VertexStartArray[GeometryIndex];
			const int32 VertexEnd = VertexStart + VertexCountArray[GeometryIndex];

			for (int32 VertexIndex = VertexStart; VertexIndex < VertexEnd; ++VertexIndex)
			{
				const FVector Position = Transform.TransformPosition(VertexArray[VertexIndex]);
				const FString Text = FString::Printf(TEXT("%d"), VertexIndex);
				AddDebugText(Text, Position, ActiveColor, TextScale, bTextShadow);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawVertexIndices(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
	const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	// Debug draw this geometry
	if (GeometryIndex != INDEX_NONE && (bIsLeafNodeRest || bDebugDrawHierarchy))
	{
		const FTransform& Transform = GlobalTransforms[TransformIndex];

		const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
		const TManagedArray<int32>& VertexStartArray = GeometryCollectionComponent->GetVertexStartArray();
		const TManagedArray<int32>& VertexCountArray = GeometryCollectionComponent->GetVertexCountArray();
		const int32 VertexStart = VertexStartArray[GeometryIndex];
		const int32 VertexEnd = VertexStart + VertexCountArray[GeometryIndex];

		for (int32 VertexIndex = VertexStart; VertexIndex < VertexEnd; ++VertexIndex)
		{
			const FVector Position = Transform.TransformPosition(VertexArray[VertexIndex]);
			const FString Text = FString::Printf(TEXT("%d"), VertexIndex);
			AddDebugText(Text, Position, Color, TextScale, bTextShadow);
		}
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawVertexIndices(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveColor);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawVertexNormals(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformIndexArray = GeometryCollectionComponent->GetTransformIndexArray();
	const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
	const TManagedArray<FVector>& NormalArray = GeometryCollectionComponent->GetNormalArray();
	const TManagedArray<int32>& VertexStartArray = GeometryCollectionComponent->GetVertexStartArray();
	const TManagedArray<int32>& VertexCountArray = GeometryCollectionComponent->GetVertexCountArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	const int32 NumGeometries = GeometryCollectionComponent->GetNumElements(FGeometryCollection::GeometryGroup);
	for (int32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
	{
		const int32 TransformIndex = TransformIndexArray[GeometryIndex];

		bool bIsVisible;
		const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
		if (bDebugDrawHierarchy)
		{
			const bool bIsLeafNode = (ChildrenArray[TransformIndex].Num() == 0);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			bIsVisible = (!bHasParent && bIsLeafNode == bIsLeafNodeRest) || (bHasParent && bDebugDrawClustering);
		}
		else
		{
			bIsVisible = bIsLeafNodeRest;
		}
		if (bIsVisible)
		{
			const FTransform& Transform = GlobalTransforms[TransformIndex];

			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;

			const int32 VertexStart = VertexStartArray[GeometryIndex];
			const int32 VertexEnd = VertexStart + VertexCountArray[GeometryIndex];

			for (int32 VertexIndex = VertexStart; VertexIndex < VertexEnd; ++VertexIndex)
			{
				const FVector LineStart = Transform.TransformPosition(VertexArray[VertexIndex]);
				const FVector VertexNormal = Transform.TransformVector(NormalArray[VertexIndex]).GetSafeNormal();
				const FVector LineEnd = LineStart + VertexNormal * NormalScale;
				DrawDebugDirectionalArrow(World, LineStart, LineEnd, ArrowScale, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
			}
			bNeedsDebugLinesFlush = true;
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawVertexNormals(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
	const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	// Debug draw this geometry
	if (GeometryIndex != INDEX_NONE && (bIsLeafNodeRest || bDebugDrawHierarchy))
	{
		const FTransform& Transform = GlobalTransforms[TransformIndex];

		const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
		const TManagedArray<FVector>& NormalArray = GeometryCollectionComponent->GetNormalArray();
		const TManagedArray<int32>& VertexStartArray = GeometryCollectionComponent->GetVertexStartArray();
		const TManagedArray<int32>& VertexCountArray = GeometryCollectionComponent->GetVertexCountArray();
		const int32 VertexStart = VertexStartArray[GeometryIndex];
		const int32 VertexEnd = VertexStart + VertexCountArray[GeometryIndex];

		for (int32 VertexIndex = VertexStart; VertexIndex < VertexEnd; ++VertexIndex)
		{
			const FVector LineStart = Transform.TransformPosition(VertexArray[VertexIndex]);
			const FVector VertexNormal = Transform.TransformVector(NormalArray[VertexIndex]).GetSafeNormal();
			const FVector LineEnd = LineStart + VertexNormal * NormalScale;

			DrawDebugDirectionalArrow(World, LineStart, LineEnd, ArrowScale, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
		}
		bNeedsDebugLinesFlush = true;
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawVertexNormals(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveColor);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawFaces(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
	const TManagedArray<FIntVector>& IndicesArray = GeometryCollectionComponent->GetIndicesArray();
	const TManagedArray<int32>& TransformIndexArray = GeometryCollectionComponent->GetTransformIndexArray();
	const TManagedArray<int32>& FaceStartArray = GeometryCollectionComponent->GetFaceStartArray();
	const TManagedArray<int32>& FaceCountArray = GeometryCollectionComponent->GetFaceCountArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	const int32 NumGeometries = GeometryCollectionComponent->GetNumElements(FGeometryCollection::GeometryGroup);
	for (int32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
	{
		const int32 TransformIndex = TransformIndexArray[GeometryIndex];

		bool bIsVisible;
		const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
		if (bDebugDrawHierarchy)
		{
			const bool bIsLeafNode = (ChildrenArray[TransformIndex].Num() == 0);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			bIsVisible = (!bHasParent && bIsLeafNode == bIsLeafNodeRest) || (bHasParent && bDebugDrawClustering);
		}
		else
		{
			bIsVisible = bIsLeafNodeRest;
		}
		if (bIsVisible)
		{
			const FTransform& Transform = GlobalTransforms[TransformIndex];

			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;

			const int32 FaceStart = FaceStartArray[GeometryIndex];
			const int32 FaceEnd = FaceStart + FaceCountArray[GeometryIndex];

			for (int32 FaceIndex = FaceStart; FaceIndex < FaceEnd; ++FaceIndex)
			{
				const FVector Vertex0 = Transform.TransformPosition(VertexArray[IndicesArray[FaceIndex][0]]);
				const FVector Vertex1 = Transform.TransformPosition(VertexArray[IndicesArray[FaceIndex][1]]);
				const FVector Vertex2 = Transform.TransformPosition(VertexArray[IndicesArray[FaceIndex][2]]);

				DrawDebugLine(World, Vertex0, Vertex1, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
				DrawDebugLine(World, Vertex0, Vertex2, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
				DrawDebugLine(World, Vertex1, Vertex2, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
			}
			bNeedsDebugLinesFlush = true;
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawFaces(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
	const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	// Debug draw this geometry
	if (GeometryIndex != INDEX_NONE && (bIsLeafNodeRest || bDebugDrawHierarchy))
	{
		const FTransform& Transform = GlobalTransforms[TransformIndex];

		const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
		const TManagedArray<FIntVector>& IndicesArray = GeometryCollectionComponent->GetIndicesArray();
		const TManagedArray<int32>& FaceStartArray = GeometryCollectionComponent->GetFaceStartArray();
		const TManagedArray<int32>& FaceCountArray = GeometryCollectionComponent->GetFaceCountArray();
		const int32 FaceStart = FaceStartArray[GeometryIndex];
		const int32 FaceEnd = FaceStart + FaceCountArray[GeometryIndex];

		for (int32 FaceIndex = FaceStart; FaceIndex < FaceEnd; ++FaceIndex)
		{
			const FVector Vertex0 = Transform.TransformPosition(VertexArray[IndicesArray[FaceIndex][0]]);
			const FVector Vertex1 = Transform.TransformPosition(VertexArray[IndicesArray[FaceIndex][1]]);
			const FVector Vertex2 = Transform.TransformPosition(VertexArray[IndicesArray[FaceIndex][2]]);

			DrawDebugLine(World, Vertex0, Vertex1, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
			DrawDebugLine(World, Vertex0, Vertex2, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
			DrawDebugLine(World, Vertex1, Vertex2, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
		}
		bNeedsDebugLinesFlush = true;
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawFaces(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveColor);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawFaceIndices(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
	const TManagedArray<FIntVector>& IndicesArray = GeometryCollectionComponent->GetIndicesArray();
	const TManagedArray<int32>& TransformIndexArray = GeometryCollectionComponent->GetTransformIndexArray();
	const TManagedArray<int32>& FaceStartArray = GeometryCollectionComponent->GetFaceStartArray();
	const TManagedArray<int32>& FaceCountArray = GeometryCollectionComponent->GetFaceCountArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	const int32 NumGeometries = GeometryCollectionComponent->GetNumElements(FGeometryCollection::GeometryGroup);
	for (int32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
	{
		const int32 TransformIndex = TransformIndexArray[GeometryIndex];

		bool bIsVisible;
		const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
		if (bDebugDrawHierarchy)
		{
			const bool bIsLeafNode = (ChildrenArray[TransformIndex].Num() == 0);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			bIsVisible = (!bHasParent && bIsLeafNode == bIsLeafNodeRest) || (bHasParent && bDebugDrawClustering);
		}
		else
		{
			bIsVisible = bIsLeafNodeRest;
		}
		if (bIsVisible)
		{
			const FTransform& Transform = GlobalTransforms[TransformIndex];

			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;

			const int32 FaceStart = FaceStartArray[GeometryIndex];
			const int32 FaceEnd = FaceStart + FaceCountArray[GeometryIndex];

			for (int32 FaceIndex = FaceStart; FaceIndex < FaceEnd; ++FaceIndex)
			{
				const FVector Vertex0 = VertexArray[IndicesArray[FaceIndex][0]];
				const FVector Vertex1 = VertexArray[IndicesArray[FaceIndex][1]];
				const FVector Vertex2 = VertexArray[IndicesArray[FaceIndex][2]];

				const FVector FaceCenter = (Vertex0 + Vertex1 + Vertex2) / 3.f;

				const FVector Position = Transform.TransformPosition(FaceCenter);

				const FString Text = FString::Printf(TEXT("%d"), FaceIndex);
				AddDebugText(Text, Position, ActiveColor, TextScale, bTextShadow);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawFaceIndices(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
	const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	// Debug draw this geometry
	if (GeometryIndex != INDEX_NONE && (bIsLeafNodeRest || bDebugDrawHierarchy))
	{
		const UWorld* const World = GetWorld();
		check(World);

		const FTransform& Transform = GlobalTransforms[TransformIndex];

		const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
		const TManagedArray<FIntVector>& IndicesArray = GeometryCollectionComponent->GetIndicesArray();
		const TManagedArray<int32>& FaceStartArray = GeometryCollectionComponent->GetFaceStartArray();
		const TManagedArray<int32>& FaceCountArray = GeometryCollectionComponent->GetFaceCountArray();
		const int32 FaceStart = FaceStartArray[GeometryIndex];
		const int32 FaceEnd = FaceStart + FaceCountArray[GeometryIndex];

		for (int32 FaceIndex = FaceStart; FaceIndex < FaceEnd; ++FaceIndex)
		{
			const FVector Vertex0 = VertexArray[IndicesArray[FaceIndex][0]];
			const FVector Vertex1 = VertexArray[IndicesArray[FaceIndex][1]];
			const FVector Vertex2 = VertexArray[IndicesArray[FaceIndex][2]];
			const FVector FaceCenter = (Vertex0 + Vertex1 + Vertex2) / 3.f;

			const FVector Position = Transform.TransformPosition(FaceCenter);
			const FString Text = FString::Printf(TEXT("%d"), FaceIndex);
			AddDebugText(Text, Position, Color, TextScale, bTextShadow);
		}
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawFaceIndices(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveColor);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawFaceNormals(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
	const TManagedArray<FIntVector>& IndicesArray = GeometryCollectionComponent->GetIndicesArray();
	const TManagedArray<int32>& TransformIndexArray = GeometryCollectionComponent->GetTransformIndexArray();
	const TManagedArray<int32>& FaceStartArray = GeometryCollectionComponent->GetFaceStartArray();
	const TManagedArray<int32>& FaceCountArray = GeometryCollectionComponent->GetFaceCountArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	const int32 NumGeometries = GeometryCollectionComponent->GetNumElements(FGeometryCollection::GeometryGroup);
	for (int32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
	{
		const int32 TransformIndex = TransformIndexArray[GeometryIndex];

		bool bIsVisible;
		const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
		if (bDebugDrawHierarchy)
		{
			const bool bIsLeafNode = (ChildrenArray[TransformIndex].Num() == 0);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			bIsVisible = (!bHasParent && bIsLeafNode == bIsLeafNodeRest) || (bHasParent && bDebugDrawClustering);
		}
		else
		{
			bIsVisible = bIsLeafNodeRest;
		}
		if (bIsVisible)
		{
			const FTransform& Transform = GlobalTransforms[TransformIndex];

			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;

			const int32 FaceStart = FaceStartArray[GeometryIndex];
			const int32 FaceEnd = FaceStart + FaceCountArray[GeometryIndex];

			for (int32 FaceIndex = FaceStart; FaceIndex < FaceEnd; ++FaceIndex)
			{
				const FVector Vertex0 = VertexArray[IndicesArray[FaceIndex][0]];
				const FVector Vertex1 = VertexArray[IndicesArray[FaceIndex][1]];
				const FVector Vertex2 = VertexArray[IndicesArray[FaceIndex][2]];

				const FVector FaceCenter = (Vertex0 + Vertex1 + Vertex2) / 3.f;

				const FVector Edge1 = Vertex1 - Vertex0;
				const FVector Edge2 = -(Vertex2 - Vertex1);

				const FVector FaceNormal = Transform.TransformVector(Edge1 ^ Edge2).GetSafeNormal();

				const FVector LineStart = Transform.TransformPosition(FaceCenter);
				const FVector LineEnd = LineStart + FaceNormal * NormalScale;
				DrawDebugDirectionalArrow(World, LineStart, LineEnd, ArrowScale, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
			}
			bNeedsDebugLinesFlush = true;
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawFaceNormals(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
	const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	// Debug draw this geometry
	if (GeometryIndex != INDEX_NONE && (bIsLeafNodeRest || bDebugDrawHierarchy))
	{
		const FTransform& Transform = GlobalTransforms[TransformIndex];

		const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
		const TManagedArray<FIntVector>& IndicesArray = GeometryCollectionComponent->GetIndicesArray();
		const TManagedArray<int32>& FaceStartArray = GeometryCollectionComponent->GetFaceStartArray();
		const TManagedArray<int32>& FaceCountArray = GeometryCollectionComponent->GetFaceCountArray();
		const int32 FaceStart = FaceStartArray[GeometryIndex];
		const int32 FaceEnd = FaceStart + FaceCountArray[GeometryIndex];

		for (int32 FaceIndex = FaceStart; FaceIndex < FaceEnd; ++FaceIndex)
		{
			const FVector Vertex0 = VertexArray[IndicesArray[FaceIndex][0]];
			const FVector Vertex1 = VertexArray[IndicesArray[FaceIndex][1]];
			const FVector Vertex2 = VertexArray[IndicesArray[FaceIndex][2]];

			const FVector FaceCenter = (Vertex0 + Vertex1 + Vertex2) / 3.f;

			const FVector Edge1 = Vertex1 - Vertex0;
			const FVector Edge2 = -(Vertex2 - Vertex1);

			const FVector FaceNormal = Transform.TransformVector(Edge1 ^ Edge2).GetSafeNormal();

			const FVector LineStart = Transform.TransformPosition(FaceCenter);
			const FVector LineEnd = LineStart + FaceNormal * NormalScale;
			DrawDebugDirectionalArrow(World, LineStart, LineEnd, ArrowScale, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
		}
		bNeedsDebugLinesFlush = true;
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawFaceNormals(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveColor);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawSingleFace(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const int32 FaceIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const int32 NumFaces = GeometryCollectionComponent->GetNumElements(FGeometryCollection::FacesGroup);
	if (FaceIndex < 0 || FaceIndex >= NumFaces) { return; }

	const TManagedArray<FVector>& VertexArray = GeometryCollectionComponent->GetVertexArray();
	const TManagedArray<int32>& BoneMapArray = GeometryCollectionComponent->GetBoneMapArray();
	const TManagedArray<FIntVector>& IndicesArray = GeometryCollectionComponent->GetIndicesArray();

	const int32 TransformIndex = BoneMapArray[IndicesArray[FaceIndex][0]];
	const FTransform& Transform = GlobalTransforms[TransformIndex];

	const FVector Vertex0 = Transform.TransformPosition(VertexArray[IndicesArray[FaceIndex][0]]);
	const FVector Vertex1 = Transform.TransformPosition(VertexArray[IndicesArray[FaceIndex][1]]);
	const FVector Vertex2 = Transform.TransformPosition(VertexArray[IndicesArray[FaceIndex][2]]);

	DrawDebugLine(World, Vertex0, Vertex1, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness * 2.f);
	DrawDebugLine(World, Vertex0, Vertex2, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness * 2.f);
	DrawDebugLine(World, Vertex1, Vertex2, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness * 2.f);
	bNeedsDebugLinesFlush = true;
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawGeometryIndices(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformIndexArray = GeometryCollectionComponent->GetTransformIndexArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	// Iterate though all geometries, and find those who needs to be visualized
	const int32 NumGeometries = GeometryCollectionComponent->GetNumElements(FGeometryCollection::GeometryGroup);
	for (int32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
	{
		const int32 TransformIndex = TransformIndexArray[GeometryIndex];

		bool bIsVisible;
		const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
		if (bDebugDrawHierarchy)
		{
			const bool bIsLeafNode = (ChildrenArray[TransformIndex].Num() == 0);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			bIsVisible = (!bHasParent && bIsLeafNode == bIsLeafNodeRest) || (bHasParent && bDebugDrawClustering);
		}
		else
		{
			bIsVisible = bIsLeafNodeRest;
		}
		if (bIsVisible)
		{
			const FTransform& Transform = GlobalTransforms[TransformIndex];
			const FVector Position = Transform.GetLocation();

			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;

			const FString Text = FString::Printf(TEXT("%d"), GeometryIndex);
			AddDebugText(Text, Position, ActiveColor, TextScale, bTextShadow);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawGeometryIndex(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
	const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	// Debug draw this geometry
	if (GeometryIndex != INDEX_NONE && (bIsLeafNodeRest || bDebugDrawHierarchy))
	{
		const FTransform& Transform = GlobalTransforms[TransformIndex];

		const FVector Position = Transform.GetLocation();
		const FString Text = FString::Printf(TEXT("%d"), GeometryIndex);
		AddDebugText(Text, Position, Color, TextScale, bTextShadow);
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawGeometryIndex(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveColor);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawTransforms(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, float Scale)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	Scale *= AxisScale;

	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	// Iterate though all transforms, and find those who are geometries and needs to be visualized
	const int32 NumTransforms = GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup);
	for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
	{
		bool bIsVisible;
		const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
		if (bDebugDrawHierarchy)
		{
			const bool bIsLeafNode = (ChildrenArray[TransformIndex].Num() == 0);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			bIsVisible = (!bHasParent && bIsLeafNode == bIsLeafNodeRest) || (bHasParent && bDebugDrawClustering);
		}
		else
		{
			const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];
			bIsVisible = (GeometryIndex != INDEX_NONE && bIsLeafNodeRest);
		}
		if (bIsVisible)
		{
			const FTransform& Transform = GlobalTransforms[TransformIndex];
			const FVector Position = Transform.GetLocation();
			const FRotator Rotator = Transform.Rotator();

			const float ActiveScale = bUseActiveVisualization ? MakeSmaller(Scale, GetLevel(TransformIndex, ParentArray)) : Scale;

			DrawDebugCoordinateSystem(World, Position, Rotator, ActiveScale, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
			bNeedsDebugLinesFlush = true;
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawTransform(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, float Scale)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	// Debug draw this geometry
	if (bIsLeafNodeRest || bDebugDrawHierarchy)
	{
		const FTransform& Transform = GlobalTransforms[TransformIndex];

		const FVector Position = Transform.GetLocation();
		const FRotator Rotator = Transform.Rotator();

		DrawDebugCoordinateSystem(World, Position, Rotator, Scale * AxisScale, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
		bNeedsDebugLinesFlush = true;
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const float ActiveScale = bUseActiveVisualization ? MakeSmaller(Scale) : Scale;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawTransform(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveScale);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawTransformIndices(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	// Iterate though all transforms, and find those who are geometries and needs to be visualized
	const int32 NumTransforms = GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup);
	for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
	{
		bool bIsVisible;
		const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
		if (bDebugDrawHierarchy)
		{
			const bool bIsLeafNode = (ChildrenArray[TransformIndex].Num() == 0);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			bIsVisible = (!bHasParent && bIsLeafNode == bIsLeafNodeRest) || (bHasParent && bDebugDrawClustering);
		}
		else
		{
			const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];
			bIsVisible = (GeometryIndex != INDEX_NONE && bIsLeafNodeRest);
		}
		if (bIsVisible)
		{
			const FTransform& Transform = GlobalTransforms[TransformIndex];
			const FVector Position = Transform.GetLocation();

			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;

			const FString Text = FString::Printf(TEXT("%d"), TransformIndex);
			AddDebugText(Text, Position, ActiveColor, TextScale, bTextShadow);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawTransformIndex(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	// Debug draw this geometry
	if (bIsLeafNodeRest || bDebugDrawHierarchy)
	{
		const FTransform& Transform = GlobalTransforms[TransformIndex];

		const FVector Position = Transform.GetLocation();
		const FString Text = FString::Printf(TEXT("%d"), TransformIndex);
		AddDebugText(Text, Position, Color, TextScale, bTextShadow);
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawTransformIndex(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveColor);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawLevels(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	// Iterate though all transforms, and find those who are geometries and needs to be visualized
	const int32 NumTransforms = GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup);
	for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
	{
		bool bIsVisible;
		const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
		if (bDebugDrawHierarchy)
		{
			const bool bIsLeafNode = (ChildrenArray[TransformIndex].Num() == 0);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			bIsVisible = (!bHasParent && bIsLeafNode == bIsLeafNodeRest) || (bHasParent && bDebugDrawClustering);
		}
		else
		{
			const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];
			bIsVisible = (GeometryIndex != INDEX_NONE && bIsLeafNodeRest);
		}
		if (bIsVisible)
		{
			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;

			const FTransform& Transform = GlobalTransforms[TransformIndex];

			const FVector Position = Transform.GetLocation();
			const int32 Level = GetLevel(TransformIndex, ParentArray);
			const FString Text = FString::Printf(TEXT("%d"), Level);
			AddDebugText(Text, Position, ActiveColor, TextScale, bTextShadow);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawLevel(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	// Debug draw this geometry
	if (bIsLeafNodeRest || bDebugDrawHierarchy)
	{
		const FTransform& Transform = GlobalTransforms[TransformIndex];
		const FVector Position = Transform.GetLocation();

		const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
		const int32 Level = GetLevel(TransformIndex, ParentArray);

		const FString Text = FString::Printf(TEXT("%d"), Level);
		AddDebugText(Text, Position, Color, TextScale, bTextShadow);
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawLevel(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveColor);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawParents(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	// Iterate though all transforms, and find those who are geometries and needs to be visualized
	const int32 NumTransforms = GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup);
	for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
	{
		const int32 ParentTransformIndex = ParentArray[TransformIndex];
		const bool bHasParent = (ParentTransformIndex != FGeometryCollection::Invalid);
		if (bHasParent)
		{
			bool bIsVisible;
			if (bDebugDrawHierarchy)
			{
				bIsVisible = bDebugDrawClustering;
			}
			else
			{
				const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];
				const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
				bIsVisible = (GeometryIndex != INDEX_NONE && bIsLeafNodeRest);
			}
			if (bIsVisible)
			{
				const FTransform& Transform = GlobalTransforms[TransformIndex];
				const FVector Position = Transform.GetLocation();

				const FTransform ParentTransform = GlobalTransforms[ParentTransformIndex];
				const FVector ParentPosition = ParentTransform.GetLocation();

				const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;

				DrawDebugLine(World, ParentPosition, Position, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
	
				bNeedsDebugLinesFlush = true;
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawParent(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const int32 ParentTransformIndex = ParentArray[TransformIndex];
	const bool bHasParent = (ParentTransformIndex != FGeometryCollection::Invalid);

	// Debug draw this geometry
	if (bHasParent && (bIsLeafNodeRest || bDebugDrawHierarchy))
	{
		const FTransform& Transform = GlobalTransforms[TransformIndex];
		const FVector Position = Transform.GetLocation();

		const FTransform ParentTransform = GlobalTransforms[ParentTransformIndex];
		const FVector ParentPosition = ParentTransform.GetLocation();

		const float Scale = ArrowScale * FVector::Dist(ParentPosition, Position);

		DrawDebugLine(World, ParentPosition, Position, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);

		bNeedsDebugLinesFlush = true;
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawParent(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveColor);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawBoundingBoxes(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<FBox>& BoundingBoxArray = GeometryCollectionComponent->GetBoundingBoxArray();
	const TManagedArray<int32>& TransformIndexArray = GeometryCollectionComponent->GetTransformIndexArray();

	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

	// Iterate though all geometries, and find those who are active and needs to be visualized
	const int32 NumGeometries = GeometryCollectionComponent->GetNumElements(FGeometryCollection::GeometryGroup);
	for (int32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
	{
		const int32 TransformIndex = TransformIndexArray[GeometryIndex];

		bool bIsVisible;
		const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
		if (bDebugDrawHierarchy)
		{
			const bool bIsLeafNode = (ChildrenArray[TransformIndex].Num() == 0);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			bIsVisible = (!bHasParent && bIsLeafNode == bIsLeafNodeRest) || (bHasParent && bDebugDrawClustering);
		}
		else
		{
			bIsVisible = bIsLeafNodeRest;
		}
		if (bIsVisible)
		{
			const FTransform& Transform = GlobalTransforms[TransformIndex];

			const FBox& BBox = BoundingBoxArray[GeometryIndex];
			const FVector Vertices[8] =
			{
				Transform.TransformPosition(BBox.Min),
				Transform.TransformPosition(FVector(BBox.Max.X, BBox.Min.Y, BBox.Min.Z)),
				Transform.TransformPosition(FVector(BBox.Max.X, BBox.Max.Y, BBox.Min.Z)),
				Transform.TransformPosition(FVector(BBox.Min.X, BBox.Max.Y, BBox.Min.Z)),
				Transform.TransformPosition(FVector(BBox.Min.X, BBox.Min.Y, BBox.Max.Z)),
				Transform.TransformPosition(FVector(BBox.Max.X, BBox.Min.Y, BBox.Max.Z)),
				Transform.TransformPosition(BBox.Max),
				Transform.TransformPosition(FVector(BBox.Min.X, BBox.Max.Y, BBox.Max.Z))
			};

			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;

			for (const auto& BoxEdge : GeometryCollectionDebugDrawActorConstants::BoxEdges)
			{
				DrawDebugLine(World, Vertices[BoxEdge(0)], Vertices[BoxEdge(1)], ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
			}

			bNeedsDebugLinesFlush = true;
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawBoundingBox(const TArray<FTransform>& GlobalTransforms, const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	const UWorld* const World = GetWorld();
	check(World);

	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
	const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];

	const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);

	// Debug draw this geometry
	if (GeometryIndex != INDEX_NONE && (bIsLeafNodeRest || bDebugDrawHierarchy))
	{
		const FTransform& Transform = GlobalTransforms[TransformIndex];

		const TManagedArray<FBox>& BoundingBoxArray = GeometryCollectionComponent->GetBoundingBoxArray();
		const FBox& BBox = BoundingBoxArray[GeometryIndex];
		const FVector Vertices[8] =
		{
			Transform.TransformPosition(BBox.Min),
			Transform.TransformPosition(FVector(BBox.Max.X, BBox.Min.Y, BBox.Min.Z)),
			Transform.TransformPosition(FVector(BBox.Max.X, BBox.Max.Y, BBox.Min.Z)),
			Transform.TransformPosition(FVector(BBox.Min.X, BBox.Max.Y, BBox.Min.Z)),
			Transform.TransformPosition(FVector(BBox.Min.X, BBox.Min.Y, BBox.Max.Z)),
			Transform.TransformPosition(FVector(BBox.Max.X, BBox.Min.Y, BBox.Max.Z)),
			Transform.TransformPosition(BBox.Max),
			Transform.TransformPosition(FVector(BBox.Min.X, BBox.Max.Y, BBox.Max.Z))
		};

		for (const auto& BoxEdge : GeometryCollectionDebugDrawActorConstants::BoxEdges)
		{
			DrawDebugLine(World, Vertices[BoxEdge(0)], Vertices[BoxEdge(1)], Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
		}

		bNeedsDebugLinesFlush = true;
	}

	// Debug draw children if the cluster mode is on, or if there is no geometry attached to this node
	if (!(bIsLeafNodeRest || bDebugDrawHierarchy) || bDebugDrawClustering)
	{
		const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
		for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
		{
			DrawBoundingBox(GlobalTransforms, GeometryCollectionComponent, ChildTransformIndex, ActiveColor);
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

FTransform AGeometryCollectionDebugDrawActor::GetParticleTransform(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);

	// Check particle sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	if (bSynced)
	{
		return GetParticleTransformNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData);
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	return FTransform::Identity;
}

FTransform AGeometryCollectionDebugDrawActor::GetParticleTransformNoChecks(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData)
{
	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();

	// Retrieve particle transform
	Chaos::FVec3 Translation = Chaos::FVec3::ZeroVector;
	Chaos::TRotation<float, 3> Rotation = Chaos::TRotation<float, 3>::Identity;
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	int32 Index;
	for (Index = TransformIndex;
		ParentArray[Index] != FGeometryCollection::Invalid;
		Index = ParentArray[Index])
	{
		const Chaos::TRigidTransform<float, 3>& ChildToParentTransform = ParticlesData.GetChildToParentMap(Index);
		Translation = ChildToParentTransform.GetTranslation() + ChildToParentTransform.GetRotation().RotateVector(Translation);
		Rotation = ChildToParentTransform.GetRotation() * Rotation;
	}
	const Chaos::FVec3& RootTranslation = ParticlesData.GetX(Index);
	const Chaos::TRotation<float, 3>& RootRotation = ParticlesData.GetR(Index);
	Translation = RootTranslation + RootRotation.RotateVector(Translation);
	Rotation = RootRotation * Rotation;
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	return FTransform(Rotation, Translation);
}

//void AGeometryCollectionDebugDrawActor::DrawRigidBodiesId(const UGeometryCollectionComponent* GeometryCollectionComponent, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<int32>& RigidBodyIdArray, const FColor& Color)
void AGeometryCollectionDebugDrawActor::DrawRigidBodiesId(const UGeometryCollectionComponent* GeometryCollectionComponent, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<FGuid>& RigidBodyIdArray, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(GetWorld());

	// Check rigid body array sync status
	if (RigidBodyIdArray.Num() == 0) { return; }

	// Check particle sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
	if (bSynced)
	{
		const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
		const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
		const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
		const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

		// Iterate though all transforms, and find those who are active (has no parent, is a leaf node with geometry or has children) and needs to be visualized
		const int32 NumTransforms = FGenericPlatformMath::Min(GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup), RigidBodyIdArray.Num());
		for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
		{
			const bool bHasChildren = (ChildrenArray[TransformIndex].Num() > 0);
			const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
			const bool bIsGeometry = (TransformToGeometryIndexArray[TransformIndex] != FGeometryCollection::Invalid);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			if ((bHasParent && bDebugDrawClustering) || (!bHasParent && (bHasChildren || (bIsLeafNodeRest && bIsGeometry))))
			{
				const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;
				DrawRigidBodyIdNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, RigidBodyIdArray, ActiveColor);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

//void AGeometryCollectionDebugDrawActor::DrawRigidBodyId(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<int32>& RigidBodyIdArray, const FColor& Color)
void AGeometryCollectionDebugDrawActor::DrawRigidBodyId(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<FGuid>& RigidBodyIdArray, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);
	check(GetWorld());

	// Request/check sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
	if (bSynced)
	{
		DrawRigidBodyIdNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, RigidBodyIdArray, Color);

		// Debug draw children if the cluster mode is on
		if (bDebugDrawClustering)
		{
			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
			const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
			for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
			{
				DrawRigidBodyId(GeometryCollectionComponent, ChildTransformIndex, ParticlesData, RigidBodyIdArray, ActiveColor);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

//void AGeometryCollectionDebugDrawActor::DrawRigidBodyIdNoChecks(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<int32>& RigidBodyIdArray, const FColor& Color)
void AGeometryCollectionDebugDrawActor::DrawRigidBodyIdNoChecks(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<FGuid>& RigidBodyIdArray, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	//const int32 RigidBodyId = RigidBodyIdArray[TransformIndex];
	const FGuid& RigidBodyId = RigidBodyIdArray[TransformIndex];

	// Retrieve particle transform
	const FTransform Transform = GetParticleTransformNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData);
	const FVector Position = Transform.GetTranslation();

	// Retrieve disabled state
	const bool bIsDisabled = ParticlesData.IsDisabled(TransformIndex);
	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const FColor& DisabledColor = bIsDisabled ? FColor::Silver: Color;

	// Draw rigid body id
	//const FString Text = FString::Printf(TEXT("%d"), RigidBodyId);
	const FString Text = RigidBodyId.ToString();
	AddDebugText(Text, Position, DisabledColor, TextScale, bTextShadow);
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodiesTransform(const UGeometryCollectionComponent* GeometryCollectionComponent, const FGeometryCollectionParticlesData& ParticlesData, float Scale)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(GetWorld());

	// Request/check sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
	if (bSynced)
	{
		const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
		const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
		const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
		const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

		const int32 NumTransforms = GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup);
		for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
		{
			const bool bHasChildren = (ChildrenArray[TransformIndex].Num() > 0);
			const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
			const bool bIsGeometry = (TransformToGeometryIndexArray[TransformIndex] != FGeometryCollection::Invalid);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			if ((bHasParent && bDebugDrawClustering) || (!bHasParent && (bHasChildren || (bIsLeafNodeRest && bIsGeometry))))
			{
				const float ActiveScale = bUseActiveVisualization ? MakeSmaller(Scale, GetLevel(TransformIndex, ParentArray)) : Scale;
				DrawRigidBodyTransformNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, ActiveScale);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyTransform(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, float Scale)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);
	check(GetWorld());

	// Request/check sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
	if (bSynced)
	{
		DrawRigidBodyTransformNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, Scale);

		// Debug draw children if the cluster mode is on
		if (bDebugDrawClustering)
		{
			const float ActiveScale = bUseActiveVisualization ? MakeSmaller(Scale) : Scale;
			const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
			for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
			{
				DrawRigidBodyTransform(GeometryCollectionComponent, ChildTransformIndex, ParticlesData, ActiveScale);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyTransformNoChecks(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, float Scale)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	Scale *= AxisScale;

	// Retrieve particle transform
	const FTransform Transform = GetParticleTransformNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData);
	const FVector Position = Transform.GetTranslation();
	const FQuat Rotation = Transform.GetRotation();

	// Retrieve disabled status
	const bool bIsDisabled = ParticlesData.IsDisabled(TransformIndex);

	// Draw transform
	if (bIsDisabled)
	{
		// Only visualize non clustered disabled elements
		const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
		if (ParentArray[TransformIndex] == FGeometryCollection::Invalid)
		{
			const FVector Forward = Position + Rotation.RotateVector(FVector::ForwardVector) * Scale;
			const FVector Right = Position + Rotation.RotateVector(FVector::RightVector) * Scale;
			const FVector Up = Position + Rotation.RotateVector(FVector::UpVector) * Scale;

			DrawDebugLine(GetWorld(), Position, Forward, FColor(64, 0, 0), GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
			DrawDebugLine(GetWorld(), Position, Right, FColor(0, 64, 0), GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
			DrawDebugLine(GetWorld(), Position, Up, FColor(0, 0, 64), GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
		}
		else
		{
			DrawDebugPoint(GetWorld(), Position, PointThickness, FColor::Black, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority);
		}
	}
	else
	{
		const FRotator Rotator(Rotation);
		DrawDebugCoordinateSystem(GetWorld(), Position, Rotator, Scale, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
	}
	bNeedsDebugLinesFlush = true;
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodiesInertia(const UGeometryCollectionComponent* GeometryCollectionComponent, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(GetWorld());

	// Request/check sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::I) && bSynced;
	if (bSynced)
	{
		const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
		const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
		const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
		const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

		const int32 NumTransforms = GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup);
		for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
		{
			const bool bHasChildren = (ChildrenArray[TransformIndex].Num() > 0);
			const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
			const bool bIsGeometry = (TransformToGeometryIndexArray[TransformIndex] != FGeometryCollection::Invalid);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			if ((bHasParent && bDebugDrawClustering) || (!bHasParent && (bHasChildren || (bIsLeafNodeRest && bIsGeometry))))
			{
				const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;
				DrawRigidBodyInertiaNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, ActiveColor);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyInertia(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);
	check(GetWorld());

	// Request/check sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::I) && bSynced;
	if (bSynced)
	{
		DrawRigidBodyInertiaNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, Color);

		// Debug draw children if the cluster mode is on
		if (bDebugDrawClustering)
		{
			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
			const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
			for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
			{
				DrawRigidBodyInertia(GeometryCollectionComponent, ChildTransformIndex, ParticlesData, ActiveColor);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyInertiaNoChecks(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	// Retrieve particle transform
	const FTransform Transform = GetParticleTransformNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData);
	const FVector Position = Transform.GetTranslation();
	const FQuat Rotation = Transform.GetRotation();

	// Retrieve disabled status
	const bool bIsDisabled = ParticlesData.IsDisabled(TransformIndex);
	const FColor ActiveColor = bIsDisabled ? FColor::Black : Color;

	// Draw transform
	const Chaos::PMatrix<float, 3, 3>& Inertia = ParticlesData.GetI(TransformIndex);
	const FVector Side(
		FMath::Sqrt(6.f * Inertia.M[1][1] + 6.f * Inertia.M[2][2] - 6.f * Inertia.M[0][0]),
		FMath::Sqrt(6.f * Inertia.M[0][0] + 6.f * Inertia.M[2][2] - 6.f * Inertia.M[1][1]),
		FMath::Sqrt(6.f * Inertia.M[0][0] + 6.f * Inertia.M[1][1] - 6.f * Inertia.M[2][2]));

	const FVector VertexMin = Side * -0.5f;
	const FVector VertexMax = Side * 0.5f;
	const FVector Vertices[8] = 
	{
		Position + Rotation.RotateVector(VertexMin),
		Position + Rotation.RotateVector(FVector(VertexMax.X, VertexMin.Y, VertexMin.Z)),
		Position + Rotation.RotateVector(FVector(VertexMax.X, VertexMax.Y, VertexMin.Z)),
		Position + Rotation.RotateVector(FVector(VertexMin.X, VertexMax.Y, VertexMin.Z)),
		Position + Rotation.RotateVector(FVector(VertexMin.X, VertexMin.Y, VertexMax.Z)),
		Position + Rotation.RotateVector(FVector(VertexMax.X, VertexMin.Y, VertexMax.Z)),
		Position + Rotation.RotateVector(VertexMax),
		Position + Rotation.RotateVector(FVector(VertexMin.X, VertexMax.Y, VertexMax.Z))
	};
	for (const auto& BoxEdge : GeometryCollectionDebugDrawActorConstants::BoxEdges)
	{
		DrawDebugLine(GetWorld(), Vertices[BoxEdge(0)], Vertices[BoxEdge(1)], ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
	}
	bNeedsDebugLinesFlush = true;
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodiesCollision(const UGeometryCollectionComponent* GeometryCollectionComponent, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(GetWorld());

	// Request/check sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::GeometryType) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::GeometryBoxMin) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::GeometryBoxMax) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::GeometrySphereCenter) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::GeometrySphereRadius) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
	if (bSynced)
	{
		const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
		const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
		const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
		const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

		const int32 NumTransforms = GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup);
		for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
		{
			const bool bHasChildren = (ChildrenArray[TransformIndex].Num() > 0);
			const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
			const bool bIsGeometry = (TransformToGeometryIndexArray[TransformIndex] != FGeometryCollection::Invalid);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			const bool bHasParentUnion = (bHasParent && ParticlesData.GetGeometryType(ParentArray[TransformIndex]) == Chaos::ImplicitObjectType::Union);
			if ((bHasParent && bDebugDrawClustering) || (!bHasParent && (bHasChildren || (bIsLeafNodeRest && bIsGeometry))) || bHasParentUnion)
			{
				const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;
				DrawRigidBodyCollisionNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, ActiveColor);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyCollision(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);
	check(GetWorld());

	// Request/check sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::GeometryType) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::GeometryBoxMin) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::GeometryBoxMax) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::GeometrySphereCenter) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::GeometrySphereRadius) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
	if (bSynced)
	{
		DrawRigidBodyCollisionNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, Color);

		if (bDebugDrawClustering)
		{
			// Debug draw all children
			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
			const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
			for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
			{
				DrawRigidBodyCollision(GeometryCollectionComponent, ChildTransformIndex, ParticlesData, ActiveColor);
			}
		}
		else if (ParticlesData.GetGeometryType(TransformIndex) == Chaos::ImplicitObjectType::Union)
		{
			// Only debug draw children that are still attached
			const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
			const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
			for (int32 ChildTransformIndex : ChildrenArray[TransformIndex])
			{
				DrawRigidBodyCollision(GeometryCollectionComponent, ChildTransformIndex, ParticlesData, ActiveColor);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyCollisionNoChecks(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	const UWorld* const World = GetWorld();

	// Retrieve particle transform
	FVector Position;
	FQuat Rotation;
	if (bCollisionAtOrigin)
	{
		Position = FVector::ZeroVector;
		Rotation = FQuat::Identity;
	}
	else
	{
		// Retrieve particle transform
		const FTransform Transform = GetParticleTransformNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData);
		Position = Transform.GetTranslation();
		Rotation = Transform.GetRotation();
	}

	// Set active color depending on parent type and disabled state
	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const int32 ParentTransformIndex = ParentArray[TransformIndex];
	const bool bIsParentUnion = (ParentTransformIndex != FGeometryCollection::Invalid &&
		ParticlesData.GetGeometryType(ParentTransformIndex) == Chaos::ImplicitObjectType::Union);
	const bool bIsDisabled = ParticlesData.IsDisabled(TransformIndex);
	const FColor& ActiveColor = (bIsDisabled && !bIsParentUnion) ? FColor::Black: Color;

	// Draw collision volume
	const Chaos::EImplicitObjectType GeometryType = ParticlesData.GetGeometryType(TransformIndex);
	switch (GeometryType)
	{
	case Chaos::ImplicitObjectType::Box:
		{
			const FVector VertexMin = ParticlesData.GetGeometryBoxMin(TransformIndex);
			const FVector VertexMax = ParticlesData.GetGeometryBoxMax(TransformIndex);
			const FVector Vertices[8] = 
			{
				Position + Rotation.RotateVector(VertexMin),
				Position + Rotation.RotateVector(FVector(VertexMax.X, VertexMin.Y, VertexMin.Z)),
				Position + Rotation.RotateVector(FVector(VertexMax.X, VertexMax.Y, VertexMin.Z)),
				Position + Rotation.RotateVector(FVector(VertexMin.X, VertexMax.Y, VertexMin.Z)),
				Position + Rotation.RotateVector(FVector(VertexMin.X, VertexMin.Y, VertexMax.Z)),
				Position + Rotation.RotateVector(FVector(VertexMax.X, VertexMin.Y, VertexMax.Z)),
				Position + Rotation.RotateVector(VertexMax),
				Position + Rotation.RotateVector(FVector(VertexMin.X, VertexMax.Y, VertexMax.Z))
			};
			for (const auto& BoxEdge : GeometryCollectionDebugDrawActorConstants::BoxEdges)
			{
				DrawDebugLine(World, Vertices[BoxEdge(0)], Vertices[BoxEdge(1)], ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
			}
		}
		break;
	case Chaos::ImplicitObjectType::Sphere:
		{
			const FVector Center = Position + Rotation.RotateVector(ParticlesData.GetGeometrySphereCenter(TransformIndex));
			const float Radius = ParticlesData.GetGeometrySphereRadius(TransformIndex);
			DrawDebugSphere(World, Center, Radius, GeometryCollectionDebugDrawActorConstants::CircleSegments, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
		}
		break;
	default:
		DrawDebugPoint(World, Position, PointThickness, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority);
		break;
	}
	bNeedsDebugLinesFlush = true;
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodiesInfo(const UGeometryCollectionComponent* GeometryCollectionComponent, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(GetWorld());

	// Request all data sync
	ParticlesData.SetAllDataSyncFlag();

	// Check sync status (only need to check position, since ParticlesData.ToString() returns whatever data has been synced
	if (ParticlesData.HasSyncedData(EGeometryCollectionParticlesData::X) &&
		ParticlesData.HasSyncedData(EGeometryCollectionParticlesData::R) &&
		ParticlesData.HasSyncedData(EGeometryCollectionParticlesData::ChildToParentMap))
	{
		const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
		const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
		const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
		const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

		const int32 NumTransforms = GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup);
		for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
		{
			const bool bHasChildren = (ChildrenArray[TransformIndex].Num() > 0);
			const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
			const bool bIsGeometry = (TransformToGeometryIndexArray[TransformIndex] != FGeometryCollection::Invalid);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			if ((bHasParent && bDebugDrawClustering) || (!bHasParent && (bHasChildren || (bIsLeafNodeRest && bIsGeometry))))
			{
				const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;
				DrawRigidBodyInfoNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, ActiveColor);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyInfo(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);
	check(GetWorld());

	// Only visualize non clustered elements
	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	if (ParentArray[TransformIndex] == FGeometryCollection::Invalid)
	{
		// Request all data sync
		ParticlesData.SetAllDataSyncFlag();

		// Check sync status (only need to check position, since ParticlesData.ToString() returns whatever data has been synced
		if (ParticlesData.HasSyncedData(EGeometryCollectionParticlesData::X) &&
			ParticlesData.HasSyncedData(EGeometryCollectionParticlesData::R) &&
			ParticlesData.HasSyncedData(EGeometryCollectionParticlesData::ChildToParentMap))
		{
			DrawRigidBodyInfoNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, Color);

			// Debug draw children if the cluster mode is on
			if (bDebugDrawClustering)
			{
				const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
				const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
				for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
				{
					DrawRigidBodyInfo(GeometryCollectionComponent, ChildTransformIndex, ParticlesData, ActiveColor);
				}
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyInfoNoChecks(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	// Retrieve particle information
	const FString Infos = TEXT("\n") + ParticlesData.ToString(TransformIndex, TEXT("\n"));  // First line is skipped for particle Id

	// Retrieve particle transform
	const FTransform Transform = GetParticleTransformNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData);
	const FVector Position = Transform.GetTranslation();

	// Draw string
	AddDebugText(Infos, Position, Color, TextScale, bTextShadow);
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

//void AGeometryCollectionDebugDrawActor::DrawConnectivityEdges(const UGeometryCollectionComponent* GeometryCollectionComponent, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<int32>& RigidBodyIdArray)
void AGeometryCollectionDebugDrawActor::DrawConnectivityEdges(const UGeometryCollectionComponent* GeometryCollectionComponent, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<FGuid>& RigidBodyIdArray)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(GetWorld());

	// Request/check sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ConnectivityEdges) && bSynced;
	if (bSynced)
	{
		const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();

		uint8 Hue = 0;

		const int32 NumTransforms = GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup);
		for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
		{
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			if (bHasParent)  // Root nodes aren't clustered
			{
				Hue = (Hue + 157) % 256;  // 157 is a prime number that gives a good spread of colors without getting too similar as a rand might do.
				const FColor RandomColor = FLinearColor::MakeFromHSV8(Hue, 160, 128).ToFColor(true);
				const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(RandomColor, GetLevel(TransformIndex, ParentArray)) : RandomColor;
				DrawConnectivityEdgesNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, RigidBodyIdArray, ActiveColor);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

//void AGeometryCollectionDebugDrawActor::DrawConnectivityEdges(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<int32>& RigidBodyIdArray, FColor HSVColor)
void AGeometryCollectionDebugDrawActor::DrawConnectivityEdges(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<FGuid>& RigidBodyIdArray, FColor HSVColor)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);
	check(GetWorld());

	// Request/check sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ConnectivityEdges) && bSynced;
	if (bSynced)
	{
		// Debug draw connectivity edges if it has a parent
		const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
		const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
		if (bHasParent)
		{
			const FLinearColor LinearColor = FLinearColor::MakeFromHSV8(HSVColor.R, HSVColor.G, HSVColor.B);  // HSV stored as RGB values
			DrawConnectivityEdgesNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, RigidBodyIdArray, LinearColor.ToFColor(true));
		}

		// Debug draw children if the cluster mode is on
		if (bDebugDrawClustering)
		{
			if (bUseActiveVisualization) { HSVColor.B /= 2; }  // HSV stored as RGB values, this makes the color darker
			const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
			for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
			{
				HSVColor.R = (HSVColor.R + 157) % 256;   // HSV stored as RGB values, this moves to the next "random" hue
				DrawConnectivityEdges(GeometryCollectionComponent, ChildTransformIndex, ParticlesData, RigidBodyIdArray, HSVColor);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

//void AGeometryCollectionDebugDrawActor::DrawConnectivityEdgesNoChecks(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<int32>& RigidBodyIdArray, const FColor& Color)
void AGeometryCollectionDebugDrawActor::DrawConnectivityEdgesNoChecks(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const TManagedArray<FGuid>& RigidBodyIdArray, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
	const UWorld* const World = GetWorld();

	// Get parent index
	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
	check(ParentArray[TransformIndex] != FGeometryCollection::Invalid);

	// Retrieve mass to local transform so to draw edges from local origin rather than from particle location
	const TManagedArray<FTransform>& MassToLocalArray = GeometryCollectionComponent->RestCollection->GetGeometryCollection()->GetAttribute<FTransform>("MassToLocal", FTransformCollection::TransformGroup);

	// Retrieve parent particle transform
	const FTransform Transform = GetParticleTransformNoChecks(GeometryCollectionComponent, ParentArray[TransformIndex], ParticlesData);
	const FVector ParentPosition = Transform.GetTranslation();
	const FQuat ParentRotation = Transform.GetRotation();

	// Retrieve local transform
	const Chaos::TRigidTransform<float, 3>& ChildToParentMap = ParticlesData.GetChildToParentMap(TransformIndex);
	const FVector Position = !ChildrenArray[TransformIndex].Num() ?
		ParentPosition + ParentRotation.RotateVector(ChildToParentMap.TransformPositionNoScale(-MassToLocalArray[TransformIndex].GetLocation())):
		ParentPosition + ParentRotation.RotateVector(ChildToParentMap.GetTranslation());

	// Retrieve connectivity edges information
	const TArray<Chaos::TConnectivityEdge<float>>& ConnectivityEdges = ParticlesData.GetConnectivityEdges(TransformIndex);

	// Edge thickness
	const float Thickness = ConnectivityEdgeThickness * LineThickness;

	// Draw connectivity information
	for (const auto& ConnectivityEdge: ConnectivityEdges)
	{
		// Retrieve the sibling's transform index in the geometry collection
		const int32 SiblingId = ConnectivityEdge.Sibling;
		int32 SiblingTransformIndex = FGeometryCollection::Invalid;
		for (int32 i = 0; i < RigidBodyIdArray.Num(); ++i)
		{
			if (RigidBodyIdArray[i] == SiblingId)
			{
				SiblingTransformIndex = i;
				break;
			}
		}

		// Draw connection
		if (SiblingTransformIndex != FGeometryCollection::Invalid)
		{
			// Retrieve local transform for sibling
			const Chaos::TRigidTransform<float, 3>& SiblingToParentMap = ParticlesData.GetChildToParentMap(SiblingTransformIndex);
			const FVector SiblingPosition = !ChildrenArray[SiblingTransformIndex].Num() ?
				ParentPosition + ParentRotation.RotateVector(SiblingToParentMap.TransformPositionNoScale(-MassToLocalArray[SiblingTransformIndex].GetLocation())):
				ParentPosition + ParentRotation.RotateVector(SiblingToParentMap.GetTranslation());

			// Draw half line
			const FVector HalfPosition = (Position + SiblingPosition) * 0.5f;
			DrawDebugLine(World, Position, HalfPosition, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, Thickness);
			DrawDebugPoint(World, SiblingPosition, PointThickness, Color, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority);
			bNeedsDebugLinesFlush = true;
		}
	}
#endif // TODO_REIMPLEMENT_RIGID_CLUSTERING
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodiesVelocity(const UGeometryCollectionComponent* GeometryCollectionComponent, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(GetWorld());

	// Request/check sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::V) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::W) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
	if (bSynced)
	{
		const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
		const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
		const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
		const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

		const int32 NumTransforms = GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup);
		for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
		{
			const bool bHasChildren = (ChildrenArray[TransformIndex].Num() > 0);
			const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
			const bool bIsGeometry = (TransformToGeometryIndexArray[TransformIndex] != FGeometryCollection::Invalid);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			if ((bHasParent && bDebugDrawClustering) || (!bHasParent && (bHasChildren || (bIsLeafNodeRest && bIsGeometry))))
			{
				const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;
				DrawRigidBodyVelocityNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, ActiveColor);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyVelocity(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);
	check(GetWorld());

	// Only visualize non clustered elements
	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	if (ParentArray[TransformIndex] == FGeometryCollection::Invalid)
	{
		// Request/check sync status
		bool bSynced;
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R);
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::V) && bSynced;
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::W) && bSynced;
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
		if (bSynced)
		{
			DrawRigidBodyVelocityNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, Color);

			// Debug draw children if the cluster mode is on
			if (bDebugDrawClustering)
			{
				const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
				const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
				for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
				{
					DrawRigidBodyVelocity(GeometryCollectionComponent, ChildTransformIndex, ParticlesData, ActiveColor);
				}
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyVelocityNoChecks(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	// Retrieve particle transform
	const FTransform Transform = GetParticleTransformNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData);
	const FVector Position = Transform.GetTranslation();

	// Retrieve disabled state
	const bool bIsDisabled = ParticlesData.IsDisabled(TransformIndex);
	const FColor& ActiveColor = bIsDisabled ? FColor::Black: Color;

	// Retrieve particle velocities
	const FVector& LinearVelocity = ParticlesData.GetV(TransformIndex);
	const FVector& AngularVelocity  = ParticlesData.GetW(TransformIndex);

	// Draw position
	DrawDebugPoint(GetWorld(), Position, PointThickness, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority);

	// Draw linear velocity
	const FVector LinearEnd = Position + LinearVelocity;
	const float Scale = ArrowScale * LinearVelocity.Size();
	DrawDebugDirectionalArrow(GetWorld(), Position, LinearEnd, Scale, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);

	// Draw angular velocity
	const float Radius = AngularVelocity.Size();
	if (Radius > KINDA_SMALL_NUMBER)
	{
		FVector YAxis, ZAxis;
		AngularVelocity.GetUnsafeNormal().FindBestAxisVectors(YAxis, ZAxis);
		const FVector AngularEnd = Position + AngularVelocity;
		DrawDebugLine(GetWorld(), Position, AngularEnd, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
		DrawDebugCircle(GetWorld(), AngularEnd, Radius, GeometryCollectionDebugDrawActorConstants::CircleSegments, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness, YAxis, ZAxis, false);
	}
	bNeedsDebugLinesFlush = true;
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodiesForce(const UGeometryCollectionComponent* GeometryCollectionComponent, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(GetWorld());

	// Request/check sync status
	bool bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R);
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::F) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Torque) && bSynced;
	bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
	if (bSynced)
	{
		const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
		const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollectionComponent->GetChildrenArray();
		const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
		const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();

		const int32 NumTransforms = GeometryCollectionComponent->GetNumElements(FGeometryCollection::TransformGroup);
		for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
		{
			const bool bHasChildren = (ChildrenArray[TransformIndex].Num() > 0);
			const bool bIsLeafNodeRest = (ChildrenArrayRest[TransformIndex].Num() == 0);
			const bool bIsGeometry = (TransformToGeometryIndexArray[TransformIndex] != FGeometryCollection::Invalid);
			const bool bHasParent = (ParentArray[TransformIndex] != FGeometryCollection::Invalid);
			if ((bHasParent && bDebugDrawClustering) || (!bHasParent && (bHasChildren || (bIsLeafNodeRest && bIsGeometry))))
			{
				const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color, GetLevel(TransformIndex, ParentArray)) : Color;
				DrawRigidBodyForceNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, ActiveColor);
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyForce(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(TransformIndex >= 0);
	check(GetWorld());

	// Only visualize non clustered elements
	const TManagedArray<int32>& ParentArray = GeometryCollectionComponent->GetParentArray();
	if (ParentArray[TransformIndex] == FGeometryCollection::Invalid)
	{
		// Request/check sync status
		bool bSynced;
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R);
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::F) && bSynced;
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Torque) && bSynced;
		bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Disabled) && bSynced;
		if (bSynced)
		{
			DrawRigidBodyForceNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData, Color);

			// Debug draw children if the cluster mode is on
			if (bDebugDrawClustering)
			{
				const FColor ActiveColor = bUseActiveVisualization ? MakeDarker(Color) : Color;
				const TManagedArray<TSet<int32>>& ChildrenArrayRest = GeometryCollectionComponent->GetChildrenArrayRest();
				for (int32 ChildTransformIndex : ChildrenArrayRest[TransformIndex])
				{
					DrawRigidBodyForce(GeometryCollectionComponent, ChildTransformIndex, ParticlesData, ActiveColor);
				}
			}
		}
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}

void AGeometryCollectionDebugDrawActor::DrawRigidBodyForceNoChecks(const UGeometryCollectionComponent* GeometryCollectionComponent, int32 TransformIndex, const FGeometryCollectionParticlesData& ParticlesData, const FColor& Color)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
	// Retrieve particle transform
	const FTransform Transform = GetParticleTransformNoChecks(GeometryCollectionComponent, TransformIndex, ParticlesData);
	const FVector Position = Transform.GetTranslation();

	// Retrieve particle information
	const FVector& Force = ParticlesData.GetF(TransformIndex);
	const FVector& Torque  = ParticlesData.GetTorque(TransformIndex);

	// Retrieve disabled state
	const bool bIsDisabled = ParticlesData.IsDisabled(TransformIndex);
	const FColor& ActiveColor = bIsDisabled ? FColor::Black: Color;

	// Draw position
	DrawDebugPoint(GetWorld(), Position, PointThickness, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority);

	// Draw linear velocity
	const FVector LinearEnd = Position + Force;
	const float Scale = ArrowScale * Force.Size();
	DrawDebugDirectionalArrow(GetWorld(), Position, LinearEnd, Scale, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);

	// Draw angular velocity
	const float Radius = Torque.Size();
	if (Radius > KINDA_SMALL_NUMBER)
	{
		FVector YAxis, ZAxis;
		Torque.GetUnsafeNormal().FindBestAxisVectors(YAxis, ZAxis);
		const FVector AngularEnd = Position + Torque;
		DrawDebugLine(GetWorld(), Position, AngularEnd, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness);
		DrawDebugCircle(GetWorld(), AngularEnd, Radius, GeometryCollectionDebugDrawActorConstants::CircleSegments, ActiveColor, GeometryCollectionDebugDrawActorConstants::bPersistent, GeometryCollectionDebugDrawActorConstants::LifeTime, GeometryCollectionDebugDrawActorConstants::DepthPriority, LineThickness, YAxis, ZAxis, false);
	}
	bNeedsDebugLinesFlush = true;
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW && ENABLE_DRAW_DEBUG
}
