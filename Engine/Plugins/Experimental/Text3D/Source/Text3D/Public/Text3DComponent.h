// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Mesh.h"
#include "BevelType.h"

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "DynamicMeshBuilder.h"
#include "UObject/ObjectMacros.h"
#include "UObject/TextProperty.h"
#include "Text3DComponent.generated.h"


class UFont;
class FPrimitiveSceneProxy;
class UMaterialInterface;

UENUM()
enum class EText3DVerticalTextAlignment : uint8
{
	FirstLine		UMETA(DisplayName = "First Line"),
	Top				UMETA(DisplayName = "Top"),
	Center			UMETA(DisplayName = "Center"),
	Bottom			UMETA(DisplayName = "Bottom"),
};

UENUM()
enum class EText3DHorizontalTextAlignment : uint8
{
	Left			UMETA(DisplayName = "Left"),
	Center			UMETA(DisplayName = "Center"),
	Right			UMETA(DisplayName = "Right"),
};

UCLASS(HideCategories=(Object, Physics, Activation, "Components|Activation"), EditInlineNew, meta=(BlueprintSpawnableComponent))
class TEXT3D_API UText3DComponent final : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UText3DComponent();

	virtual void OnRegister() override;
	virtual void CreateRenderState_Concurrent() override;
	virtual void SendRenderDynamicData_Concurrent() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual bool ShouldRecreateProxyOnUpdateTransform() const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial) override;


	/** The text to generate a 3d mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetText, Category="Text3D", meta=(MultiLine=true))
	FText Text;

	/** Size of the extrude */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetExtrude, Category = "Text3D", Meta = (ClampMin = 0))
	float Extrude;

	/** Size of bevel */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetBevel, Category="Text3D", Meta=(ClampMin=0))
	float Bevel;

	/** Bevel Type (Linear / Half Circle) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetBevelType, Category="Text3D")
	EText3DBevelType BevelType;

	/** Half Circle Bevel Segments (Defines the amount of tesselation for the bevel part) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetHalfCircleSegments, Category="Text3D", Meta=(ClampMin=1, ClampMax=15))
	int32 HalfCircleSegments;

	/** Material for the front part */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetFrontMaterial, Category="Text3D")
	UMaterialInterface* FrontMaterial;

	/** Material for the bevel part */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetBevelMaterial, Category="Text3D")
	UMaterialInterface* BevelMaterial;

	/** Material for the extruded part */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetExtrudeMaterial, Category="Text3D")
	UMaterialInterface* ExtrudeMaterial;

	/** Material for the back part */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetBackMaterial, Category="Text3D")
	UMaterialInterface* BackMaterial;

	/** Text font */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetFont, Category="Text3D")
	UFont* Font;

	/** Horizontal text alignment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetHorizontalAlignment, Category="Text3D")
	EText3DHorizontalTextAlignment HorizontalAlignment;

	/** Vertical text alignment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetVerticalAlignment, Category="Text3D")
	EText3DVerticalTextAlignment VerticalAlignment;

	/** Text kerning */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetKerning, Category="Text3D")
	float Kerning;

	/** Extra line spacing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetLineSpacing, Category="Text3D")
	float LineSpacing;

	/** Extra word spacing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetWordSpacing, Category="Text3D")
	float WordSpacing;

	/** Enables a maximum width to the 3D Text */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetHasMaxWidth, Category="Text3D", meta=(InlineEditConditionToggle))
	bool bHasMaxWidth;

	/** Sets a maximum width to the 3D Text */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetMaxWidth, Category="Text3D", meta=(EditCondition="bHasMaxWidth", ClampMin=1))
	float MaxWidth;

	/** Enables a maximum height to the 3D Text */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetHasMaxHeight, Category="Text3D", meta = (InlineEditConditionToggle))
	bool bHasMaxHeight;

	/** Sets a maximum height to the 3D Text */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetMaxHeight, Category="Text3D", meta=(EditCondition="bHasMaxHeight", ClampMin=1))
	float MaxHeight;

	/** Should the mesh scale proportionally when Max Width/Height is set */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetScaleProportionally, Category="Text3D")
	bool bScaleProportionally;

	/** Set the text front material */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetFrontMaterial(UMaterialInterface* Value);

	/** Set the text bevel material */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetBevelMaterial(UMaterialInterface* Value);

	/** Set the text extrude material */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetExtrudeMaterial(UMaterialInterface* Value);

	/** Set the text back material */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetBackMaterial(UMaterialInterface* Value);

	/** Set the text value and signal the primitives to be rebuilt */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetText(const FText& Value);

	/** Set the kerning value and signal the primitives to be rebuilt */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetKerning(const float Value);

	/** Set the line spacing value and signal the primitives to be rebuilt */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetLineSpacing(const float Value);

	/** Set the word spacing value and signal the primitives to be rebuilt */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetWordSpacing(const float Value);

	/** Set the horizontal alignment value and signal the primitives to be rebuilt */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetHorizontalAlignment(const EText3DHorizontalTextAlignment value);

	/** Set the vertical alignment and signal the primitives to be rebuilt */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetVerticalAlignment(const EText3DVerticalTextAlignment value);

	/** Set the text extrusion size and signal the primitives to be rebuilt */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetExtrude(const float Value);

	/** Set the text font and signal the primitives to be rebuilt */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetFont(UFont* const InFont);

	/** Set the 3d bevel value */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetBevel(const float Value);

	/** Set the 3d bevel type (Linear / Half Circle) */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetBevelType(const EText3DBevelType Value);

	/** Set the amount of segments that will be used to tesselate the Half Circle Bevel */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetHalfCircleSegments(const int32 Value);

	/** Enable / Disable a Maximum Width */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetHasMaxWidth(const bool Value);

	/** Set the Maximum Width - If width is larger, mesh will scale down to fit MaxWidth value */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetMaxWidth(const float Value);

	/** Enable / Disable a Maximum Height */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetHasMaxHeight(const bool Value);

	/** Set the Maximum Height - If height is larger, mesh will scale down to fit MaxHeight value */
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Text3D")
	void SetMaxHeight(const float Value);

	/** Set if the mesh should scale proportionally when Max Width/Height is set */
	UFUNCTION(BlueprintCallable, Category = "Rendering|Components|Text3D")
	void SetScaleProportionally(const bool Value);

	/** Freeze mesh rebuild, to avoid unnecessary mesh rebuilds when setting a few properties together */
	UFUNCTION(BlueprintCallable, Category = "Rendering|Components|Text3D")
	void SetFreeze(const bool bFreeze);

private:
	bool bPendingBuild;
	bool bFreezeBuild;

	TSharedRef<TText3DMeshList> Meshes;
	FTransform CharacterTransform;

	friend class FText3DSceneProxy;

	void Rebuild();
	void BuildTextMesh();
	void CheckBevel();
	float MaxBevel() const;
};
