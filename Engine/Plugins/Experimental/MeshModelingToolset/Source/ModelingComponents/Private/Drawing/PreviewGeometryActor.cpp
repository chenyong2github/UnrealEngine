// Copyright Epic Games, Inc. All Rights Reserved.

#include "Drawing/PreviewGeometryActor.h"
#include "ToolSetupUtil.h"

// to create sphere root component
#include "Components/SphereComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/CollisionProfile.h"


UPreviewGeometry::~UPreviewGeometry()
{
	checkf(ParentActor == nullptr, TEXT("You must explicitly Disconnect() UPreviewGeometry before it is GCd"));
}

void UPreviewGeometry::CreateInWorld(UWorld* World, const FTransform& WithTransform)
{
	FRotator Rotation(0.0f, 0.0f, 0.0f);
	FActorSpawnParameters SpawnInfo;
	ParentActor = World->SpawnActor<APreviewGeometryActor>(FVector::ZeroVector, Rotation, SpawnInfo);

	// root component is a hidden sphere
	USphereComponent* SphereComponent = NewObject<USphereComponent>(ParentActor);
	ParentActor->SetRootComponent(SphereComponent);
	SphereComponent->InitSphereRadius(1.0f);
	SphereComponent->SetVisibility(false);
	SphereComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	ParentActor->SetActorTransform(WithTransform);
}


void UPreviewGeometry::Disconnect()
{
	if (ParentActor != nullptr)
	{
		ParentActor->Destroy();
		ParentActor = nullptr;
	}
}




ULineSetComponent* UPreviewGeometry::AddLineSet(const FString& SetIdentifier)
{
	if (LineSets.Contains(SetIdentifier))
	{
		check(false);
		return nullptr;
	}

	ULineSetComponent* LineSet = NewObject<ULineSetComponent>(ParentActor);
	LineSet->SetupAttachment(ParentActor->GetRootComponent());

	UMaterialInterface* LineMaterial = ToolSetupUtil::GetDefaultLineComponentMaterial(nullptr);
	if (LineMaterial != nullptr)
	{
		LineSet->SetLineMaterial(LineMaterial);
	}

	LineSet->RegisterComponent();

	LineSets.Add(SetIdentifier, LineSet);
	return LineSet;
}


ULineSetComponent* UPreviewGeometry::FindLineSet(const FString& LineSetIdentifier)
{
	ULineSetComponent** Found = LineSets.Find(LineSetIdentifier);
	if (Found != nullptr)
	{
		return *Found;
	}
	return nullptr;
}

bool UPreviewGeometry::RemoveLineSet(const FString& LineSetIdentifier, bool bDestroy)
{
	ULineSetComponent** Found = LineSets.Find(LineSetIdentifier);
	if (Found != nullptr)
	{
		ULineSetComponent* LineSet = *Found;
		LineSets.Remove(LineSetIdentifier);
		if (bDestroy)
		{
			LineSet->UnregisterComponent();
			LineSet->DestroyComponent();
			LineSet = nullptr;
		}
	}
	return false;
}



void UPreviewGeometry::RemoveAllLineSets(bool bDestroy)
{
	for (TPair<FString, ULineSetComponent*> Entry : LineSets)
	{
		if (bDestroy)
		{
			Entry.Value->UnregisterComponent();
			Entry.Value->DestroyComponent();
		}
	}
	LineSets.Reset();
}



bool UPreviewGeometry::SetLineSetVisibility(const FString& LineSetIdentifier, bool bVisible)
{
	ULineSetComponent** Found = LineSets.Find(LineSetIdentifier);
	if (Found != nullptr)
	{
		(*Found)->SetVisibility(bVisible);
		return true;
	}
	return false;
}


bool UPreviewGeometry::SetLineSetMaterial(const FString& LineSetIdentifier, UMaterialInterface* NewMaterial)
{
	ULineSetComponent** Found = LineSets.Find(LineSetIdentifier);
	if (Found != nullptr)
	{
		(*Found)->SetLineMaterial(NewMaterial);
		return true;
	}
	return false;
}


void UPreviewGeometry::SetAllLineSetsMaterial(UMaterialInterface* Material)
{
	for (TPair<FString, ULineSetComponent*> Entry : LineSets)
	{
		Entry.Value->SetLineMaterial(Material);
	}
}



void UPreviewGeometry::CreateOrUpdateLineSet(const FString& LineSetIdentifier, int32 NumIndices,
	TFunctionRef<void(int32 Index, TArray<FRenderableLine>& LinesOut)> LineGenFunc,
	int32 LinesPerIndexHint)
{
	ULineSetComponent* LineSet = FindLineSet(LineSetIdentifier);
	if (LineSet == nullptr)
	{
		LineSet = AddLineSet(LineSetIdentifier);
		if (LineSet == nullptr)
		{
			check(false);
			return;
		}
	}

	LineSet->Clear();
	LineSet->AddLines(NumIndices, LineGenFunc, LinesPerIndexHint);
}

