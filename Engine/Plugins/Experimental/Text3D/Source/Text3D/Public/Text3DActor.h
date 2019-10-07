// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Text3DActor.generated.h"

class UText3DComponent;

UCLASS(Meta = (DisplayName = "Text 3D", ComponentWrapperClass))
class TEXT3D_API AText3DActor : public AActor
{
	GENERATED_BODY()

public:
	AText3DActor();

	/** Returns Text3D subobject **/
	UText3DComponent * GetText3DComponent() const				{ return Text3DComponent; }

private:
	UPROPERTY(Category=Text, VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	UText3DComponent* Text3DComponent;
};
