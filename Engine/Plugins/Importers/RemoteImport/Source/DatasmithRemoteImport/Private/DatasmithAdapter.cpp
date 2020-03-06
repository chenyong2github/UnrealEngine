// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithAdapter.h"
#include "DatasmithRemoteImportLog.h"

#include "DatasmithPayload.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithTranslatableSource.h"
#include "DatasmithTranslator.h"
#include "IDatasmithSceneElements.h"

#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "MeshDescription.h"
#include "Templates/SharedPointer.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Editor/UnrealEdEngine.h"
#endif // WITH_EDITOR


namespace DatasmithAdapter
{

FTranslateResult Translate(const FString& InFilePath)
{
	FDatasmithSceneSource Source;
	Source.SetSourceFile(InFilePath);

	TSharedPtr<FDatasmithTranslatableSceneSource> TranslatableSourcePtr = MakeShared<FDatasmithTranslatableSceneSource>(Source);
	FDatasmithTranslatableSceneSource& TranslatableSource = *TranslatableSourcePtr;

	if (!TranslatableSource.IsTranslatable())
	{
		UE_LOG(LogDatasmithRemoteImport, Error, TEXT("Datasmith adapter import error: no suitable translator found for this source. Abort import."));
		return {};
	}

	TSharedRef<IDatasmithScene> Scene = FDatasmithSceneFactory::CreateScene(*Source.GetSceneName());

	if (!TranslatableSource.Translate(Scene))
	{
		UE_LOG(LogDatasmithRemoteImport, Error, TEXT("Datasmith import error: Scene translation failure. Abort import."));
		return {};
	}

	return { Scene, TranslatableSource.GetTranslator(), TranslatableSourcePtr};
}


void ListActorElements(IDatasmithActorElement* Actor, TArray<IDatasmithActorElement*>& OutElements)
{
	for (int32 ChildElementIndex = 0 ; ChildElementIndex < Actor->GetChildrenCount(); ++ChildElementIndex)
	{
		const TSharedPtr<IDatasmithActorElement>& ActorElement = Actor->GetChild(ChildElementIndex);
		ListActorElements(ActorElement.Get(), OutElements);
		OutElements.Add(ActorElement.Get());
	}
}


void ListActorElements(IDatasmithScene* Scene, TArray<IDatasmithActorElement*>& OutElements)
{
	for (int32 ActorElementIndex = 0 ; ActorElementIndex < Scene->GetActorsCount(); ++ActorElementIndex)
	{
		const TSharedPtr<IDatasmithActorElement>& ActorElement = Scene->GetActor(ActorElementIndex);
		ListActorElements(ActorElement.Get(), OutElements);
		OutElements.Add(ActorElement.Get());
	}
}


bool Import(FTranslateResult& Translation, UWorld* TargetWorld, const FTransform& RootTM)
{
	IDatasmithScene* Scene = Translation.Scene.Get();
	IDatasmithTranslator* Translator = Translation.Translator.Get();

	if (Scene == nullptr || Translator == nullptr || TargetWorld == nullptr)
	{
		UE_LOG(LogDatasmithRemoteImport, Error, TEXT("Can't import: incomplete input"));
		return false;
	}

	struct FMeshPath
	{
		const TCHAR* Name;
		TSharedRef<IDatasmithMeshElement> Element;
	};
	TMap<FString, FMeshPath> KnownMeshInfo;

	for (int32 i = 0 ; i < Scene->GetMeshesCount(); ++i)
	{
		TSharedRef<IDatasmithMeshElement> MeshElement = Scene->GetMesh(i).ToSharedRef();
		const TCHAR* MeshName = MeshElement->GetName();
		KnownMeshInfo.Add(MeshName, {MeshName, MeshElement});
	}

	TArray<IDatasmithActorElement*> AllMeshActors;
	ListActorElements(Scene, AllMeshActors);


	for (IDatasmithActorElement* ActorElement : AllMeshActors)
	{
		if (ActorElement && ActorElement->IsA(EDatasmithElementType::StaticMeshActor))
		{
			IDatasmithMeshActorElement* MeshActorElement = static_cast<IDatasmithMeshActorElement*>(ActorElement);

			const TCHAR* MeshName = MeshActorElement->GetStaticMeshPathName();

			if (FMeshPath* ThisMeshInfo = KnownMeshInfo.Find(MeshName))
			{
				FDatasmithMeshElementPayload MeshPayload;
				Translator->LoadStaticMesh(ThisMeshInfo->Element, MeshPayload);

				if (MeshPayload.LodMeshes.IsValidIndex(0))
				{
					FTransform WorldTransform (
						ActorElement->GetRotation(),
						ActorElement->GetTranslation(),
						ActorElement->GetScale()
					);

					AStaticMeshActor* NewMeshActor = TargetWorld->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), WorldTransform*RootTM);
					if (NewMeshActor == nullptr)
					{
						UE_LOG(LogDatasmithRemoteImport, Error, TEXT("Error on static mesh actor spawn"));
						return false;
					}

#if WITH_EDITOR
					NewMeshActor->SetActorLabel(MeshActorElement->GetLabel());
#endif // WITH_EDITOR

					if (UStaticMesh* StaticMeshObject = NewObject<UStaticMesh>())
					{
						TArray<const FMeshDescription*> MeshDescriptions;
						for (auto& MeshDescription: MeshPayload.LodMeshes)
						{
							MeshDescriptions.Add(&MeshDescription);
						}

						StaticMeshObject->BuildFromMeshDescriptions(MeshDescriptions);
						NewMeshActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
						NewMeshActor->GetStaticMeshComponent()->SetStaticMesh(StaticMeshObject);
					}
				}
			}
		}
	}
	return true;
}

} // ns DatasmithAdapter

