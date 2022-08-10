// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorSettings.h"

#include "LightCardTemplates/DisplayClusterLightCardTemplate.h"

#include "DisplayClusterMeshProjectionRenderer.h"

#include "DisplayClusterLightCardActor.h"

#include "Editor/UnrealEdTypes.h"
#include "Engine/Texture.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "DisplayClusterLightCardEditorSettings"

UDisplayClusterLightCardEditorProjectSettings::UDisplayClusterLightCardEditorProjectSettings()
{
	LightCardTemplateDefaultPath.Path = TEXT("/Game/VP/LightCards");
	LightCardLabelScale = 1.f;
	bDisplayLightCardLabels = false;
}

const FName FDisplayClusterLightCardEditorRecentItem::Type_LightCard = "LightCard";
const FName FDisplayClusterLightCardEditorRecentItem::Type_LightCardTemplate = "LightCardTemplate";

FText FDisplayClusterLightCardEditorRecentItem::GetItemDisplayName() const
{
	if (ItemType == Type_LightCard)
	{
		return LOCTEXT("LightCardRecentItemName", "Light Card");
	}
	if (const UObject* Object = ObjectPath.LoadSynchronous())
	{
		return FText::FromString(Object->GetName());
	}

	return FText::GetEmpty();
}

const FSlateBrush* FDisplayClusterLightCardEditorRecentItem::GetSlateBrush() const
{
	if (ItemType == Type_LightCard)
	{
		return FSlateIconFinder::FindIconBrushForClass(ADisplayClusterLightCardActor::StaticClass());
	}
	
	if (ItemType == Type_LightCardTemplate && !SlateBrush.IsValid())
	{
		if (const UDisplayClusterLightCardTemplate* Template = Cast<UDisplayClusterLightCardTemplate>(ObjectPath.LoadSynchronous()))
		{
			UTexture* Texture = Template->LightCardActor != nullptr ? Template->LightCardActor->Texture.Get() : nullptr;
			if (Texture == nullptr)
			{
				return FSlateIconFinder::FindIconBrushForClass(Template->GetClass());;
			}
			
			if (!SlateBrush.IsValid())
			{
				SlateBrush = MakeShared<FSlateBrush>();
			}
		
			SlateBrush->SetResourceObject(Texture);
			SlateBrush->ImageSize = FVector2D(16.f, 16.f);
		}
	}

	if (SlateBrush.IsValid())
	{
		return &*SlateBrush;
	}

	return nullptr;
}

UDisplayClusterLightCardEditorSettings::UDisplayClusterLightCardEditorSettings()
{
	ProjectionMode = EDisplayClusterMeshProjectionType::Azimuthal;
	RenderViewportType = ELevelViewportType::LVT_Perspective;
}

#undef LOCTEXT_NAMESPACE