// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDConvexMeshGenerator.h"
#include "ChaosVDHeightFieldMeshGenerator.h"
#include "ChaosVDTriMeshGenerator.h"
#include "Chaos/HeightField.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ImplicitObjectType.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Generators/CapsuleGenerator.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Generators/SphereGenerator.h"
#include "UDynamicMesh.h"
#include "UObject/GCObject.h"
#include "UObject/UObjectGlobals.h"

class AActor;
class UDynamicMesh;
class UDynamicMeshComponent;

namespace UE
{
	namespace Geometry
	{
		class FMeshShapeGenerator;
		class FDynamicMesh3;
	}
}

/*
 * Generates Dynamic mesh components and dynamic meshes based on Chaos implicit object data
 */
class FChaosVDGeometryBuilder : public FGCObject
{
public:

	/** Creates Dynamic Mesh components for each object within the provided Implicit object
	 *	@param InImplicitObject : Implicit object to process
	 *	@param Owner Actor who will own the generated components
	 *	@param OutMeshComponents Array containing all the generated components
	 *	@param Index Index of the current component being processed. This is useful when this method is called recursively
	 *	@param Transform to apply to the generated components/geometry
	 */
	template<typename MeshType, typename ComponentType>
	void CreateMeshComponentsFromImplicit(const Chaos::FImplicitObject* InImplicitObject, AActor* Owner, TArray<UMeshComponent*>& OutMeshComponents, Chaos::FRigidTransform3& Transform, int32 Index = 0);
	
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FChaosVDScene");
	}

private:

	/** Creates a Dynamic Mesh for the provided Implicit object and generator, and then caches it to be reused later
	 * @param GeometryCacheKey Key to be used to find this geometry in the cache
	 * @param MeshGenerator Generator class with the data and rules to create the mesh
	 */
	UDynamicMesh* CreateAndCacheDynamicMesh(const uint32 GeometryCacheKey, UE::Geometry::FMeshShapeGenerator& MeshGenerator);

	/** Returns an already mesh for the provided implicit object if exists, otherwise returns null
	 * @param GeometryCacheKey Key to be used to find this geometry in the cache
	 */
	template<typename MeshType>
	MeshType* GetCachedMeshForImplicit(const uint32 GeometryCacheKey);

	/** Creates a Dynamic Mesh for the provided Implicit object and generator, and then caches it to be reused later
	 * @param GeometryCacheKey Key to be used to find this geometry in the cache
	 * @param MeshGenerator Generator class with the data and rules to create the mesh
	 */
	UStaticMesh* CreateAndCacheStaticMesh(const uint32 GeometryCacheKey, UE::Geometry::FMeshShapeGenerator& MeshGenerator);

	/**
	 * Creates an empty DynamicMeshComponent/Static Mesh Component or Instanced mesg component and adds it to the actor
	 * @param Owner Actor who will own the component
	 * @param Name Name of the component. It has to be unique within the components in the owner actor
	 * @param Transform (Optional) Transform to apply as Relative Transform in the component after its creating and attachment to the provided actor
	 * */
	template<typename ComponentType>
	ComponentType* CreateMeshComponent(AActor* Owner, const FString& Name, const Chaos::FRigidTransform3& Transform) const;

	/**
	 * Creates a Mesh from the provided Implicit object geometry data
	 * @param GeometryCacheKey Key to be used to find this geometry in the cache
	 * @param MeshGenerator Generator class that has the data to generate the mesh
	 * */
	template<typename MeshType>
	MeshType* CreateAndCacheMeshForImplicit(const uint32 GeometryCacheKey, UE::Geometry::FMeshShapeGenerator& MeshGenerator);

	/* Process an FImplicitObject and returns de desired geometry type. Could be directly the shape or another version of the implicit */
	template <bool bIsInstanced, typename GeometryType>
	const GeometryType* GetGeometry(const Chaos::FImplicitObject* InImplicit, const bool bIsScaled, Chaos::FRigidTransform3& OutTransform) const;

	/** Process an FImplicitObject and returns de desired geometry type based on the packed object flags. Could be directly the shape or another version of the implicit */
	template<typename GeometryType>
	const GeometryType* GetGeometryBasedOnPackedType(const Chaos::FImplicitObject* InImplicitObject, Chaos::FRigidTransform3& Transform, const Chaos::EImplicitObjectType PackedType) const;

	/** Map containing already generated dynamic mesh for any given implicit object */
	TMap<uint32, UDynamicMesh*> MeshCacheMap;

	/** Map containing already generated static mesh for any given implicit object */
	TMap<uint32, UStaticMesh*> StaticMeshCacheMap;
};


template <typename MeshType, typename ComponentType>
void FChaosVDGeometryBuilder::CreateMeshComponentsFromImplicit(const Chaos::FImplicitObject* InImplicitObject, AActor* Owner, TArray<UMeshComponent*>& OutMeshComponents, Chaos::FRigidTransform3& Transform, int32 Index)
{
	static_assert(std::is_same_v<MeshType, UStaticMesh> || std::is_same_v<MeshType, UDynamicMesh>, "CreateMeshComponentsFromImplicit Only supports DynamicMesh and Static Mesh");
	static_assert(std::is_same_v<ComponentType, UStaticMeshComponent> || std::is_same_v<ComponentType, UInstancedStaticMeshComponent> || std::is_same_v<MeshType, UDynamicMeshComponent>, "CreateMeshComponentsFromImplicit Only supports DynamicMeshComponent, Static MeshComponent and Instanced Static Mesh Component");

	// We could have you make the Mesh type as template and infer the component type based on that, but we also want to be able to create Static Meshes with either Instanced or normal static mesh components
	constexpr bool bHasValidCombinationForStaticMesh =  std::is_same_v<MeshType, UStaticMesh> && (std::is_same_v<ComponentType, UStaticMeshComponent> || std::is_same_v<ComponentType, UInstancedStaticMeshComponent>);
	constexpr bool bHasValidCombinationForDynamicMesh =  std::is_same_v<MeshType, UDynamicMesh> && std::is_same_v<ComponentType, UDynamicMeshComponent>;
	static_assert(bHasValidCombinationForStaticMesh || bHasValidCombinationForDynamicMesh , "Incorrect Component type for Mesh type. Did you use a Dynamic Mesh with a Static Mesh component type?.");

	using namespace Chaos;

	const EImplicitObjectType InnerType = GetInnerType(InImplicitObject->GetType());
	const EImplicitObjectType PackedType = InImplicitObject->GetType();
	
	if (InnerType == ImplicitObjectType::Union)
	{
		const FImplicitObjectUnion* Union = InImplicitObject->template GetObject<FImplicitObjectUnion>();

		for (int i = 0; i < Union->GetObjects().Num(); ++i)
		{
			const TUniquePtr<FImplicitObject>& UnionImplicit = Union->GetObjects()[i];

			CreateMeshComponentsFromImplicit<MeshType, ComponentType>(UnionImplicit.Get(), Owner, OutMeshComponents, Transform, i);	
		}

		return;
	}

	if (InnerType ==  ImplicitObjectType::Transformed)
	{
		const TImplicitObjectTransformed<FReal, 3>* Transformed = InImplicitObject->template GetObject<TImplicitObjectTransformed<FReal, 3>>();
		FRigidTransform3 TransformCopy = Transformed->GetTransform();
		CreateMeshComponentsFromImplicit<MeshType, ComponentType>(Transformed->GetTransformedObject(), Owner, OutMeshComponents,TransformCopy, Index);
		return;
	}

	ComponentType* MeshComponent = nullptr;
	MeshType* Mesh = nullptr;
	switch (InnerType)
	{
		case ImplicitObjectType::Sphere:
		{
			const TSphere<FReal, 3>* Sphere = InImplicitObject->template GetObject<TSphere<FReal, 3>>();

			const FString Name = FString::Format(TEXT("{0} - {1}"), {TEXT("Sphere"), FString::FromInt(Index)});

			MeshComponent = CreateMeshComponent<ComponentType>(Owner, Name, Transform);

			Mesh = GetCachedMeshForImplicit<MeshType>(Sphere->GetTypeHash());

			if (!Mesh)
			{
				UE::Geometry::FSphereGenerator SphereGen;
				SphereGen.Radius = Sphere->GetRadius();
				SphereGen.NumTheta = 50;
				SphereGen.NumPhi = 50;
				SphereGen.bPolygroupPerQuad = false;

				Mesh = CreateAndCacheMeshForImplicit<MeshType>(Sphere->GetTypeHash(), SphereGen);
			}

			break;
		}
		case ImplicitObjectType::Box:
		{
			const TBox<FReal, 3>* Box = InImplicitObject->template GetObject<TBox<FReal, 3>>();

			const FString Name = FString::Format(TEXT("{0} - {1}"), {TEXT("Box"), FString::FromInt(Index)});
			MeshComponent = CreateMeshComponent<ComponentType>(Owner, Name, Transform);

			Mesh = GetCachedMeshForImplicit<MeshType>(Box->GetTypeHash());

			if (!Mesh)
			{
				UE::Geometry::FMinimalBoxMeshGenerator BoxGen;
				UE::Geometry::FOrientedBox3d OrientedBox;
				OrientedBox.Frame = UE::Geometry::FFrame3d(Box->Center());
				OrientedBox.Extents = Box->Extents() * 0.5;
				BoxGen.Box = OrientedBox;

				Mesh = CreateAndCacheMeshForImplicit<MeshType>(Box->GetTypeHash(), BoxGen);
			}

			break;
		}
		case ImplicitObjectType::Plane:
			break;
		case ImplicitObjectType::Capsule:
		{
			const FCapsule* Capsule = InImplicitObject->template GetObject<FCapsule>();

			const FString Name = FString::Format(TEXT("{0} - {1}"), {TEXT("Capsule"), FString::FromInt(Index)});
			const FRigidTransform3 StartingTransform;
			MeshComponent = CreateMeshComponent<ComponentType>(Owner, Name, StartingTransform);

			// Re-adjust the location so the pivot is not the center of the capsule, and transform it based on the provided transform
			const FVector FinalLocation = Transform.TransformPosition(Capsule->GetCenter() - Capsule->GetAxis() * Capsule->GetSegment().GetLength() * 0.5f);
			const FQuat Rotation = FRotationMatrix::MakeFromZ(Capsule->GetAxis()).Rotator().Quaternion();

			MeshComponent->SetRelativeRotation(Transform.GetRotation() * Rotation);
			MeshComponent->SetRelativeLocation(FinalLocation);
			MeshComponent->SetRelativeScale3D(Transform.GetScale3D());

			Mesh = GetCachedMeshForImplicit<MeshType>(Capsule->GetTypeHash());

			if (!Mesh)
			{
				UE::Geometry::FCapsuleGenerator CapsuleGenerator;
				CapsuleGenerator.Radius = FMath::Max(FMathf::ZeroTolerance, Capsule->GetRadius());
				CapsuleGenerator.SegmentLength = FMath::Max(FMathf::ZeroTolerance, Capsule->GetSegment().GetLength());
				CapsuleGenerator.NumHemisphereArcSteps = 12;
				CapsuleGenerator.NumCircleSteps = 12;
				CapsuleGenerator.bPolygroupPerQuad = false;
				
				Mesh = CreateAndCacheMeshForImplicit<MeshType>(Capsule->GetTypeHash(), CapsuleGenerator);
			}
			break;
		}
		case ImplicitObjectType::LevelSet:
		{
			//TODO: Implement
			break;
		}
		break;
		case ImplicitObjectType::Convex:
		{
			if (const FConvex* Convex = GetGeometryBasedOnPackedType<FConvex>(InImplicitObject, Transform, PackedType))
			{
				const FString Name = FString::Format(TEXT("{0} - {1}"), {TEXT("Convex"), FString::FromInt(Index)});
				MeshComponent = CreateMeshComponent<ComponentType>(Owner, Name, Transform);
				Mesh = GetCachedMeshForImplicit<MeshType>(Convex->GetTypeHash());
	
				if (!Mesh)
				{
					FChaosVDConvexMeshGenerator ConvexMeshGen;
					ConvexMeshGen.GenerateFromConvex(*Convex);
					Mesh = CreateAndCacheMeshForImplicit<MeshType>(Convex->GetTypeHash() ,ConvexMeshGen);
				}
			}

			break;
		}
		case ImplicitObjectType::TaperedCylinder:
			break;
		case ImplicitObjectType::Cylinder:
			break;
		case ImplicitObjectType::TriangleMesh:
		{
			if (const FTriangleMeshImplicitObject* TriangleMesh = GetGeometryBasedOnPackedType<FTriangleMeshImplicitObject>(InImplicitObject, Transform, PackedType))
			{
				const FString Name = FString::Format(TEXT("{0} - {1}"), {TEXT("Trimesh"), FString::FromInt(Index)});
				MeshComponent = CreateMeshComponent<ComponentType>(Owner, Name, Transform);

				Mesh = GetCachedMeshForImplicit<MeshType>(TriangleMesh->GetTypeHash());
				if (!Mesh)
				{
					FChaosVDTriMeshGenerator TriMeshGen;
					TriMeshGen.bReverseOrientation = true;
					TriMeshGen.GenerateFromTriMesh(*TriangleMesh);		
					Mesh = CreateAndCacheMeshForImplicit<MeshType>(TriangleMesh->GetTypeHash(), TriMeshGen);
				}
			}
				
			break;
		}
		case ImplicitObjectType::HeightField:
		{
			if (const FHeightField* HeightField = GetGeometryBasedOnPackedType<FHeightField>(InImplicitObject, Transform, PackedType))
			{
				const FString Name = FString::Format(TEXT("{0} - {1}"), {TEXT("HeightField"), FString::FromInt(Index)});
				MeshComponent = CreateMeshComponent<ComponentType>(Owner, Name, Transform);
				Mesh = GetCachedMeshForImplicit<MeshType>(HeightField->GetTypeHash());
	
				if (!Mesh)
				{
					FChaosVDHeightFieldMeshGenerator HeightFieldMeshGen;
					HeightFieldMeshGen.bReverseOrientation = false;
					HeightFieldMeshGen.GenerateFromHeightField(*HeightField);

					Mesh = CreateAndCacheMeshForImplicit<MeshType>(HeightField->GetTypeHash(), HeightFieldMeshGen);
				}
			}
		
			break;
		}
		default:
			break;
		}

		if (MeshComponent != nullptr && Mesh != nullptr)
		{
			if constexpr (std::is_same_v<ComponentType, UDynamicMeshComponent>)
			{
				MeshComponent->SetDynamicMesh(Mesh);
			}
			else if constexpr(std::is_same_v<ComponentType, UInstancedStaticMeshComponent> || std::is_same_v<ComponentType, UStaticMeshComponent>)
			{
				MeshComponent->SetStaticMesh(Mesh);
			}

			OutMeshComponents.Add(MeshComponent);
		}
}

template <typename MeshType>
MeshType* FChaosVDGeometryBuilder::GetCachedMeshForImplicit(const uint32 GeometryCacheKey)
{
	if constexpr (std::is_same_v<MeshType, UDynamicMesh>)
	{
		if (MeshType** MeshPtrPtr = MeshCacheMap.Find(GeometryCacheKey))
		{
			return *MeshPtrPtr;
		}
	}
	else if constexpr (std::is_same_v<MeshType, UStaticMesh>)
	{
		if (MeshType** MeshPtrPtr = StaticMeshCacheMap.Find(GeometryCacheKey))
		{
			return *MeshPtrPtr;
		}
	}

	return nullptr;
}

template <typename ComponentType>
ComponentType* FChaosVDGeometryBuilder::CreateMeshComponent(AActor* Owner, const FString& Name, const Chaos::FRigidTransform3& Transform) const
{
	ComponentType* MeshComponent = NewObject<ComponentType>(Owner, *Name);

	if (Owner)
	{
		MeshComponent->RegisterComponent();
		MeshComponent->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::SnapToTargetIncludingScale);
		Owner->AddInstanceComponent(MeshComponent);
	}

	MeshComponent->bSelectable = true;

	if constexpr (std::is_same_v<ComponentType, UDynamicMeshComponent> || std::is_same_v<ComponentType, UStaticMeshComponent>)
	{
		MeshComponent->SetRelativeTransform(Transform);
	}
	else if constexpr (std::is_same_v<ComponentType, UInstancedStaticMeshComponent>)
	{
		MeshComponent->AddInstance(Transform);
	}

	return MeshComponent;
}

template <typename MeshType>
MeshType* FChaosVDGeometryBuilder::CreateAndCacheMeshForImplicit(const uint32 GeometryKey, UE::Geometry::FMeshShapeGenerator& MeshGenerator)
{
	if constexpr (std::is_same_v<MeshType, UDynamicMesh>)
	{
		return CreateAndCacheDynamicMesh(GeometryKey, MeshGenerator);
	}
	else if constexpr (std::is_same_v<MeshType, UStaticMesh>)
	{
		return CreateAndCacheStaticMesh(GeometryKey, MeshGenerator);
	}

	return nullptr;
}

template <bool bIsInstanced, typename GeometryType>
const GeometryType* FChaosVDGeometryBuilder::GetGeometry(const Chaos::FImplicitObject* InImplicitObject, const bool bIsScaled, Chaos::FRigidTransform3& OutTransform) const
{
	if (bIsScaled)
	{
		if (const Chaos::TImplicitObjectScaled<GeometryType, bIsInstanced>* ImplicitScaled = InImplicitObject->template GetObject<Chaos::TImplicitObjectScaled<GeometryType, bIsInstanced>>())
		{
			OutTransform.SetScale3D(ImplicitScaled->GetScale());
			return ImplicitScaled->GetUnscaledObject()->template GetObject<GeometryType>();
		}
	}
	else
	{
		if (bIsInstanced)
		{
			const Chaos::TImplicitObjectInstanced<GeometryType>* ImplicitInstanced = InImplicitObject->template GetObject<Chaos::TImplicitObjectInstanced<GeometryType>>();
			return ImplicitInstanced->GetInnerObject()->template GetObject<GeometryType>();
		}
		else
		{
			return InImplicitObject->template GetObject<GeometryType>();
		}
	}
	
	return nullptr;
}

template <typename GeometryType>
const GeometryType* FChaosVDGeometryBuilder::GetGeometryBasedOnPackedType(const Chaos::FImplicitObject* InImplicitObject, Chaos::FRigidTransform3& Transform,  const Chaos::EImplicitObjectType PackedType) const
{
	using namespace Chaos;

	const bool bIsInstanced = IsInstanced(PackedType);
	const bool bIsScaled = IsScaled(PackedType);

	if (bIsInstanced)
	{
		return GetGeometry<true,GeometryType>(InImplicitObject, bIsScaled, Transform);
	}
	else
	{
		return GetGeometry<false, GeometryType>(InImplicitObject, bIsScaled, Transform);
	}

	return nullptr;
}
