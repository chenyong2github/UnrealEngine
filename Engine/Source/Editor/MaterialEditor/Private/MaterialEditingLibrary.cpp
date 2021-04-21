// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialEditingLibrary.h"
#include "Editor.h"
#include "MaterialEditor.h"
#include "MaterialInstanceEditor.h"
#include "MaterialEditorUtilities.h"
#include "MaterialShared.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionDynamicParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "MaterialStatsCommon.h"
#include "Particles/ParticleSystemComponent.h"
#include "EditorSupportDelegates.h"
#include "Misc/RuntimeErrors.h"
#include "SceneTypes.h"
#include "AssetRegistryModule.h"
#include "DebugViewModeHelpers.h"
#include "Subsystems/AssetEditorSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMaterialEditingLibrary, Warning, All);

/** Util to find expression  */
static FExpressionInput* GetExpressionInputByName(UMaterialExpression* Expression, const FName InputName)
{
	check(Expression);
	FExpressionInput* Result = nullptr;

	TArray<FExpressionInput*> Inputs = Expression->GetInputs();

	// Return first input if no name specified
	if (InputName.IsNone())
	{
		if (Inputs.Num() > 0)
		{
			return Inputs[0];
		}
	}
	else
	{
		// Get all inputs
		// Get name of each input, see if its the one we want
		for (int InputIdx = 0; InputIdx < Inputs.Num(); InputIdx++)
		{
			FName TestName;
			if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
			{
				// If a function call, don't want to compare string with type postfix
				TestName = FuncCall->GetInputNameWithType(InputIdx, false);
			}
			else
			{
				const FName ExpressionInputName = Expression->GetInputName(InputIdx);
				TestName = UMaterialGraphNode::GetShortenPinName(ExpressionInputName);
			}

			if (TestName == InputName)
			{
				Result = Inputs[InputIdx];
				break;
			}
		}
	}

	return Result;
}

static int32 GetExpressionOutputIndexByName(UMaterialExpression* Expression, const FName OutputName)
{
	check(Expression);
	
	int32 Result = INDEX_NONE;

	if (Expression->Outputs.Num() == 0)
	{
		// leave as INDEX_NONE
	}
	// Return first output if no name specified
	else if (OutputName.IsNone())
	{
		Result = 0;
	}
	else
	{
		// Iterate over outputs and look for name match
		for (int OutIdx = 0; OutIdx < Expression->Outputs.Num(); OutIdx++)
		{
			bool bFoundMatch = false;

			FExpressionOutput& Output = Expression->Outputs[OutIdx];
			// If output name is no empty - see if it matches
			if(!Output.OutputName.IsNone())
			{
				if (OutputName == Output.OutputName)
				{
					bFoundMatch = true;
				}
			}
			// if it is empty we look for R/G/B/A
			else
			{
				if (Output.MaskR && !Output.MaskG && !Output.MaskB && !Output.MaskA && OutputName == TEXT("R"))
				{
					bFoundMatch = true;
				}
				else if (!Output.MaskR && Output.MaskG && !Output.MaskB && !Output.MaskA && OutputName == TEXT("G"))
				{
					bFoundMatch = true;
				}
				else if (!Output.MaskR && !Output.MaskG && Output.MaskB && !Output.MaskA && OutputName == TEXT("B"))
				{
					bFoundMatch = true;
				}
				else if (!Output.MaskR && !Output.MaskG && !Output.MaskB && Output.MaskA && OutputName == TEXT("A"))
				{
					bFoundMatch = true;
				}
			}

			// Got a match, remember the index, exit iteration
			if (bFoundMatch)
			{
				Result = OutIdx;
				break;
			}
		}
	}

	return Result;
}

namespace MaterialEditingLibraryImpl
{
	struct FMaterialExpressionLayoutInfo
	{
		static const int32 LayoutWidth = 260;

		UMaterialExpression* Connected = nullptr;
		int32 Column = 0;
		int32 Row = 0;
	};

	void LayoutMaterialExpression( UMaterialExpression* MaterialExpression, UMaterialExpression* ConnectedExpression, TMap< UMaterialExpression*, FMaterialExpressionLayoutInfo >& MaterialExpressionsToLayout, int32 Row, int32 Depth )
	{
		if ( !MaterialExpression )
		{
			return;
		}

		FMaterialExpressionLayoutInfo LayoutInfo;

		if ( MaterialExpressionsToLayout.Contains( MaterialExpression ) )
		{
			LayoutInfo = MaterialExpressionsToLayout[ MaterialExpression ];
		}

		LayoutInfo.Row = FMath::Max( LayoutInfo.Row, Row );

		if ( Depth > LayoutInfo.Column )
		{
			LayoutInfo.Connected = ConnectedExpression;
		}

		LayoutInfo.Column = FMath::Max( LayoutInfo.Column, Depth );

		MaterialExpressionsToLayout.Add( MaterialExpression ) = MoveTemp( LayoutInfo );

		for ( FExpressionInput* ExpressionInput : MaterialExpression->GetInputs() )
		{
			LayoutMaterialExpression( ExpressionInput->Expression, MaterialExpression, MaterialExpressionsToLayout, Row, Depth + 1 );
		}
	}

	void LayoutMaterialExpressions( UObject* MaterialOrMaterialFunction )
	{
		if ( !MaterialOrMaterialFunction )
		{
			return;
		}

		TMap< UMaterialExpression*, FMaterialExpressionLayoutInfo > MaterialExpressionsToLayout;

		if ( UMaterial* Material = Cast< UMaterial >( MaterialOrMaterialFunction ) )
		{
			for ( int32 MaterialPropertyIndex = 0; MaterialPropertyIndex < MP_MAX; ++MaterialPropertyIndex )
			{
				FExpressionInput* ExpressionInput = Material->GetExpressionInputForProperty( EMaterialProperty(MaterialPropertyIndex) );
		
				if ( ExpressionInput  )
				{
					LayoutMaterialExpression( ExpressionInput->Expression, nullptr, MaterialExpressionsToLayout, MaterialPropertyIndex, 0 );
				}
			}
		}
		else if ( UMaterialFunction* MaterialFunction = Cast< UMaterialFunction >( MaterialOrMaterialFunction ) )
		{
			TArray< FFunctionExpressionInput > Inputs;
			TArray< FFunctionExpressionOutput > Outputs;
			
			MaterialFunction->GetInputsAndOutputs( Inputs, Outputs );

			int32 InputIndex = 0;

			if ( Inputs.Num() > 0 )
			{
				for ( FFunctionExpressionInput& FunctionExpressionInput : Inputs )
				{
					LayoutMaterialExpression( FunctionExpressionInput.ExpressionInput, nullptr, MaterialExpressionsToLayout, ++InputIndex, 0 );
				}
			}
			else
			{
				for ( FFunctionExpressionOutput& FunctionExpressionOutput : Outputs )
				{
					LayoutMaterialExpression( FunctionExpressionOutput.ExpressionOutput, nullptr, MaterialExpressionsToLayout, ++InputIndex, 0 );
				}
			}
		}

		TMap< int32, TMap< int32, bool > > UsedColumnRows;

		TMap< int32, int32 > ColumnsHeights;

		for ( TMap< UMaterialExpression*, FMaterialExpressionLayoutInfo >::TIterator It = MaterialExpressionsToLayout.CreateIterator(); It; ++It )
		{
			UMaterialExpression* MaterialExpression = It->Key;
			FMaterialExpressionLayoutInfo& LayoutInfo = It->Value;

			if ( !UsedColumnRows.Contains( LayoutInfo.Column ) )
			{
				UsedColumnRows.Add( LayoutInfo.Column );
			}

			while ( UsedColumnRows[ LayoutInfo.Column ].Contains( LayoutInfo.Row ) )
			{
				++LayoutInfo.Row;
			}

			UsedColumnRows[ LayoutInfo.Column ].Add( LayoutInfo.Row ) = true;

			if ( !ColumnsHeights.Contains( LayoutInfo.Column ) )
			{
				ColumnsHeights.Add( LayoutInfo.Column ) = 0;
			}

			int32& ColumnHeight = ColumnsHeights[ LayoutInfo.Column ];

			MaterialExpression->MaterialExpressionEditorX = -FMaterialExpressionLayoutInfo::LayoutWidth * ( LayoutInfo.Column + 1 );

			int32 ConnectedHeight = LayoutInfo.Connected ? LayoutInfo.Connected->MaterialExpressionEditorY : 0;
			MaterialExpression->MaterialExpressionEditorY = FMath::Max( ColumnHeight, ConnectedHeight );

			ColumnHeight = MaterialExpression->MaterialExpressionEditorY + MaterialExpression->GetHeight() + ME_STD_HPADDING;
		}
	}

	IMaterialEditor* FindMaterialEditorForAsset(UObject* InAsset)
	{
		if (IAssetEditorInstance* AssetEditorInstance = (InAsset != nullptr) ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(InAsset, false) : nullptr)
		{
			// Ensure this is not a UMaterialInstanceDynamic, as that doesn't use IMaterialEditor as its editor
			if (!InAsset->IsA(UMaterialInstanceDynamic::StaticClass()))
			{
				return static_cast<IMaterialEditor*>(AssetEditorInstance);
			}
		}

		return nullptr;
	}

	FMaterialInstanceEditor* FindMaterialInstanceEditorForAsset(UObject* InAsset)
	{
		if (IAssetEditorInstance* AssetEditorInstance = (InAsset != nullptr) ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(InAsset, false) : nullptr)
		{
			// Ensure this is not a UMaterialInstanceDynamic, as that doesn't use FMaterialInstanceEditor as its editor
			if (!InAsset->IsA(UMaterialInstanceDynamic::StaticClass()))
			{
				return static_cast<FMaterialInstanceEditor*>(AssetEditorInstance);
			}
		}

		return nullptr;
	}
	
}

void UMaterialEditingLibrary::RebuildMaterialInstanceEditors(UMaterial* BaseMaterial)
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();

	for (int32 AssetIdx = 0; AssetIdx < EditedAssets.Num(); AssetIdx++)
	{
		UObject* EditedAsset = EditedAssets[AssetIdx];

		UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(EditedAsset);
		if (!SourceInstance)
		{
			// Check to see if the EditedAssets are from material instance editor
			UMaterialEditorInstanceConstant* EditorInstance = Cast<UMaterialEditorInstanceConstant>(EditedAsset);
			if (EditorInstance && EditorInstance->SourceInstance)
			{
				SourceInstance = EditorInstance->SourceInstance;
			}
		}

		if (SourceInstance != nullptr)
		{
			UMaterial* MICOriginalMaterial = SourceInstance->GetMaterial();
			if (MICOriginalMaterial == BaseMaterial)
			{
				if (FMaterialInstanceEditor* MaterialInstanceEditor = MaterialEditingLibraryImpl::FindMaterialInstanceEditorForAsset(SourceInstance))
				{
					MaterialInstanceEditor->RebuildMaterialInstanceEditor();
				}
			}
		}
	}
}

void UMaterialEditingLibrary::RebuildMaterialInstanceEditors(UMaterialFunction* BaseFunction)
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();

	for (int32 AssetIdx = 0; AssetIdx < EditedAssets.Num(); AssetIdx++)
	{
		UObject* EditedAsset = EditedAssets[AssetIdx];

		UMaterialFunctionInstance* FunctionInstance = Cast<UMaterialFunctionInstance>(EditedAsset);	
		UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(EditedAsset);
	
		if (FunctionInstance)
		{
			// Update function instances that are children of this material function	
			if (BaseFunction && BaseFunction == FunctionInstance->GetBaseFunction())
			{
				if (FMaterialInstanceEditor* MaterialInstanceEditor = MaterialEditingLibraryImpl::FindMaterialInstanceEditorForAsset(EditedAsset))
				{
					MaterialInstanceEditor->RebuildMaterialInstanceEditor();
				}
			}
		}
		else
		{
			if (!SourceInstance)
			{
				// Check to see if the EditedAssets are from material instance editor
				UMaterialEditorInstanceConstant* EditorInstance = Cast<UMaterialEditorInstanceConstant>(EditedAsset);
				if (EditorInstance && EditorInstance->SourceInstance)
				{
					SourceInstance = EditorInstance->SourceInstance;
				}
			}

			// Ensure the material instance is valid and not a UMaterialInstanceDynamic, as that doesn't use FMaterialInstanceEditor as its editor
			if (SourceInstance != nullptr && !SourceInstance->IsA(UMaterialInstanceDynamic::StaticClass()))
			{
				TArray<UMaterialFunctionInterface*> DependentFunctions;
				SourceInstance->GetDependentFunctions(DependentFunctions);

				if (BaseFunction && (DependentFunctions.Contains(BaseFunction) || DependentFunctions.Contains(BaseFunction->ParentFunction)))
				{
					if (FMaterialInstanceEditor* MaterialInstanceEditor = MaterialEditingLibraryImpl::FindMaterialInstanceEditorForAsset(EditedAsset))
					{
						MaterialInstanceEditor->RebuildMaterialInstanceEditor();
					}
				}
			}
		}
	}
}

int32 UMaterialEditingLibrary::GetNumMaterialExpressions(const UMaterial* Material)
{
	int32 Result = 0;
	if (Material)
	{
		Result = Material->Expressions.Num();
	}
	return Result;
}

void UMaterialEditingLibrary::DeleteAllMaterialExpressions(UMaterial* Material)
{
	if (Material)
	{
		TArray<UMaterialExpression*> AllExpressions = Material->Expressions;
		for (UMaterialExpression* Expression : AllExpressions)
		{
			DeleteMaterialExpression(Material, Expression);
		}
	}
}

/** Util to iterate over list of expressions, and break any links to specified expression */
static void BreakLinksToExpression(TArray<UMaterialExpression*>& Expressions, UMaterialExpression* Expression)
{
	// Need to find any other expressions which are connected to this one, and break link
	for (UMaterialExpression* TestExp : Expressions)
	{
		// Don't check myself, though that shouldn't really matter...
		if (TestExp != Expression)
		{
			TArray<FExpressionInput*> Inputs = TestExp->GetInputs();
			for (FExpressionInput* Input : Inputs)
			{
				if (Input->Expression == Expression)
				{
					Input->Expression = nullptr;
				}
			}
		}
	}
}

void UMaterialEditingLibrary::DeleteMaterialExpression(UMaterial* Material, UMaterialExpression* Expression)
{
	if (Material && Expression && Expression->GetOuter() == Material)
	{
		// Break any links to this expression
		BreakLinksToExpression(Material->Expressions, Expression);

		// Check material parameter inputs, to make sure expression is not connected to it
		for (int32 InputIndex = 0; InputIndex < MP_MAX; InputIndex++)
		{
			FExpressionInput* Input = Material->GetExpressionInputForProperty((EMaterialProperty)InputIndex);
			if (Input && Input->Expression == Expression)
			{
				Input->Expression = nullptr;
			}
		}

		Material->RemoveExpressionParameter(Expression);

		Material->Expressions.Remove(Expression);

		Expression->MarkPendingKill();

		Material->MarkPackageDirty();
	}
}


UMaterialExpression* UMaterialEditingLibrary::CreateMaterialExpression(UMaterial* Material, TSubclassOf<UMaterialExpression> ExpressionClass, int32 NodePosX, int32 NodePosY)
{
	return CreateMaterialExpressionEx(Material, nullptr, ExpressionClass, nullptr, NodePosX, NodePosY);
}

UMaterialExpression* UMaterialEditingLibrary::CreateMaterialExpressionInFunction(UMaterialFunction* MaterialFunction, TSubclassOf<UMaterialExpression> ExpressionClass, int32 NodePosX, int32 NodePosY)
{
	return CreateMaterialExpressionEx(nullptr, MaterialFunction, ExpressionClass, nullptr, NodePosX, NodePosY);
}


UMaterialExpression* UMaterialEditingLibrary::CreateMaterialExpressionEx(UMaterial* Material, UMaterialFunction* MaterialFunction, TSubclassOf<UMaterialExpression> ExpressionClass, UObject* SelectedAsset, int32 NodePosX, int32 NodePosY)
{
	UMaterialExpression* NewExpression = nullptr;
	if (Material || MaterialFunction)
	{
		UObject* ExpressionOuter = Material;
		if (MaterialFunction)
		{
			ExpressionOuter = MaterialFunction;
		}

		NewExpression = NewObject<UMaterialExpression>(ExpressionOuter, ExpressionClass.Get(), NAME_None, RF_Transactional);

		if (Material)
		{
			Material->Expressions.Add(NewExpression);
			NewExpression->Material = Material;
		}

		if (MaterialFunction && !Material)
		{
			MaterialFunction->FunctionExpressions.Add(NewExpression);
		}

		NewExpression->MaterialExpressionEditorX = NodePosX;
		NewExpression->MaterialExpressionEditorY = NodePosY;

		// Create a GUID for the node
		NewExpression->UpdateMaterialExpressionGuid(true, true);

		if (SelectedAsset)
		{
			// If the user is adding a texture, automatically assign the currently selected texture to it.
			UMaterialExpressionTextureBase* METextureBase = Cast<UMaterialExpressionTextureBase>(NewExpression);
			if (METextureBase)
			{
				if (UTexture* SelectedTexture = Cast<UTexture>(SelectedAsset))
				{
					METextureBase->Texture = SelectedTexture;
				}
				METextureBase->AutoSetSampleType();
			}

			UMaterialExpressionMaterialFunctionCall* MEMaterialFunction = Cast<UMaterialExpressionMaterialFunctionCall>(NewExpression);
			if (MEMaterialFunction)
			{
				MEMaterialFunction->SetMaterialFunction(Cast<UMaterialFunction>(SelectedAsset));
			}

			UMaterialExpressionCollectionParameter* MECollectionParameter = Cast<UMaterialExpressionCollectionParameter>(NewExpression);
			if (MECollectionParameter)
			{
				MECollectionParameter->Collection = Cast<UMaterialParameterCollection>(SelectedAsset);
			}
		}

		UMaterialExpressionFunctionInput* FunctionInput = Cast<UMaterialExpressionFunctionInput>(NewExpression);
		if (FunctionInput)
		{
			FunctionInput->ConditionallyGenerateId(true);
			FunctionInput->ValidateName();
		}

		UMaterialExpressionFunctionOutput* FunctionOutput = Cast<UMaterialExpressionFunctionOutput>(NewExpression);
		if (FunctionOutput)
		{
			FunctionOutput->ConditionallyGenerateId(true);
			FunctionOutput->ValidateName();
		}

		NewExpression->UpdateParameterGuid(true, true);

		if (NewExpression->HasAParameterName())
		{
			NewExpression->ValidateParameterName(false);
		}

		UMaterialExpressionComponentMask* ComponentMaskExpression = Cast<UMaterialExpressionComponentMask>(NewExpression);
		// Setup defaults for the most likely use case
		// Can't change default properties as that will affect existing content
		if (ComponentMaskExpression)
		{
			ComponentMaskExpression->R = true;
			ComponentMaskExpression->G = true;
		}

		UMaterialExpressionStaticComponentMaskParameter* StaticComponentMaskExpression = Cast<UMaterialExpressionStaticComponentMaskParameter>(NewExpression);
		// Setup defaults for the most likely use case
		// Can't change default properties as that will affect existing content
		if (StaticComponentMaskExpression)
		{
			StaticComponentMaskExpression->DefaultR = true;
		}

		// Setup defaults for the most likely use case
		// Can't change default properties as that will affect existing content
		UMaterialExpressionTransformPosition* PositionTransform = Cast<UMaterialExpressionTransformPosition>(NewExpression);
		if (PositionTransform)
		{
			PositionTransform->TransformSourceType = TRANSFORMPOSSOURCE_Local;
			PositionTransform->TransformType = TRANSFORMPOSSOURCE_World;
		}

		// Make sure the dynamic parameters are named based on existing ones
		UMaterialExpressionDynamicParameter* DynamicExpression = Cast<UMaterialExpressionDynamicParameter>(NewExpression);
		if (DynamicExpression)
		{
			DynamicExpression->UpdateDynamicParameterProperties();
		}

		if (Material)
		{
			Material->AddExpressionParameter(NewExpression, Material->EditorParameters);
		}

		NewExpression->MarkPackageDirty();
	}
	return NewExpression;
}

bool UMaterialEditingLibrary::SetMaterialUsage(UMaterial* Material, EMaterialUsage Usage, bool& bNeedsRecompile)
{
	bool bResult = false;
	bNeedsRecompile = false;
	if (Material)
	{
		bResult = Material->SetMaterialUsage(bNeedsRecompile, Usage);
	}
	return bResult;
}

bool UMaterialEditingLibrary::HasMaterialUsage(UMaterial* Material, EMaterialUsage Usage)
{
	bool bResult = false;
	if (Material)
	{
		bResult = Material->GetUsageByFlag(Usage);
	}
	return bResult;
}

bool UMaterialEditingLibrary::ConnectMaterialProperty(UMaterialExpression* FromExpression, FString FromOutputName, EMaterialProperty Property)
{
	bool bResult = false;
	if (FromExpression)
	{
		// Get material that owns this expression
		UMaterial* Material = Cast<UMaterial>(FromExpression->GetOuter());
		if (Material)
		{
			FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
			int32 FromIndex = GetExpressionOutputIndexByName(FromExpression, *FromOutputName);
			if (Input && FromIndex != INDEX_NONE)
			{
				Input->Connect(FromIndex, FromExpression);
				bResult = true;
			}
		}
	}
	return bResult;
}

bool UMaterialEditingLibrary::ConnectMaterialExpressions(UMaterialExpression* FromExpression, FString FromOutputName, UMaterialExpression* ToExpression, FString ToInputName)
{
	bool bResult = false;
	if (FromExpression && ToExpression)
	{
		FExpressionInput* Input = GetExpressionInputByName(ToExpression, *ToInputName);
		int32 FromIndex = GetExpressionOutputIndexByName(FromExpression, *FromOutputName);
		if (Input && FromIndex != INDEX_NONE)
		{
			Input->Connect(FromIndex, FromExpression);
			bResult = true;
		}
	}
	return bResult;
}

void UMaterialEditingLibrary::RecompileMaterial(UMaterial* Material)
{
	if (ensureAsRuntimeWarning(Material != nullptr))
	{
		{
			FMaterialUpdateContext UpdateContext;

			UpdateContext.AddMaterial(Material);

			// Propagate the change to this material
			Material->PreEditChange(nullptr);
			Material->PostEditChange();

			Material->MarkPackageDirty();

			// update the world's viewports
			FEditorDelegates::RefreshEditor.Broadcast();
			FEditorSupportDelegates::RedrawAllViewports.Broadcast();

			// Force particle components to update their view relevance.
			for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
			{
				It->bIsViewRelevanceDirty = true;
			}

			// Update parameter names on any child material instances
			for (TObjectIterator<UMaterialInstance> It; It; ++It)
			{
				if (It->Parent == Material)
				{
					It->UpdateParameterNames();
				}
			}

			// Leaving this scope will update all dependent material instances.
		}

		UMaterialEditingLibrary::RebuildMaterialInstanceEditors(Material);
		FMaterialEditorUtilities::BuildTextureStreamingData(Material);
	}
}

void UMaterialEditingLibrary::LayoutMaterialExpressions(UMaterial* Material)
{
	MaterialEditingLibraryImpl::LayoutMaterialExpressions( Material );
}

float UMaterialEditingLibrary::GetMaterialDefaultScalarParameterValue(UMaterial* Material, FName ParameterName)
{
	float Result = 0.f;
	if (Material)
	{
		Material->GetScalarParameterDefaultValue(ParameterName, Result);
	}
	return Result;
}

UTexture* UMaterialEditingLibrary::GetMaterialDefaultTextureParameterValue(UMaterial* Material, FName ParameterName)
{
	UTexture* Result = nullptr;
	if (Material)
	{
		Material->GetTextureParameterDefaultValue(ParameterName, Result);
	}
	return Result;
}

FLinearColor UMaterialEditingLibrary::GetMaterialDefaultVectorParameterValue(UMaterial* Material, FName ParameterName)
{
	FLinearColor Result = FLinearColor::Black;
	if (Material)
	{
		Material->GetVectorParameterDefaultValue(ParameterName, Result);
	}
	return Result;
}

bool UMaterialEditingLibrary::GetMaterialDefaultStaticSwitchParameterValue(UMaterial* Material, FName ParameterName)
{
	bool bResult = false;
	if (Material)
	{
		FGuid OutGuid;
		Material->GetStaticSwitchParameterDefaultValue(ParameterName, bResult, OutGuid);
	}
	return bResult;
}

TSet<UObject*> UMaterialEditingLibrary::GetMaterialSelectedNodes(UMaterial* Material)
{
	if (IMaterialEditor* MaterialEditor = MaterialEditingLibraryImpl::FindMaterialEditorForAsset(Material))
	{
		TSet<UObject*> SelectedMaterialObjects;
		for (const FFieldVariant SelectedNode : MaterialEditor->GetSelectedNodes())
		{
			check(SelectedNode.IsUObject());
			SelectedMaterialObjects.Add(SelectedNode.ToUObject());
		}
		return SelectedMaterialObjects;
	}

	return TSet<UObject*>();
}

UMaterialExpression* UMaterialEditingLibrary::GetMaterialPropertyInputNode(UMaterial* Material, EMaterialProperty Property)
{
	if (Material)
	{
		FExpressionInput*  ExpressionInput = Material->GetExpressionInputForProperty(Property);
		return ExpressionInput->Expression;
	}

	return nullptr;
}

TArray<UMaterialExpression*> UMaterialEditingLibrary::GetInputsForMaterialExpression(UMaterial* Material, UMaterialExpression* MaterialExpression)
{
	TArray<UMaterialExpression*> MaterialExpressions;
	if (Material)
	{
		for (const FExpressionInput* Input : MaterialExpression->GetInputs())
		{
			MaterialExpressions.Add(Input->Expression);
		}
	}

	return MaterialExpressions;
}

TArray<UTexture*> UMaterialEditingLibrary::GetUsedTextures(UMaterial* Material)
{
	TArray<UTexture*> OutTextures;
	Material->GetUsedTextures(OutTextures, EMaterialQualityLevel::Num, false, GMaxRHIFeatureLevel, true);
	return OutTextures;
}

//////////////////////////////////////////////////////////////////////////

int32 UMaterialEditingLibrary::GetNumMaterialExpressionsInFunction(const UMaterialFunction* MaterialFunction)
{
	int32 Result = 0;
	if (MaterialFunction)
	{
		Result = MaterialFunction->FunctionExpressions.Num();
	}
	return Result;
}

void UMaterialEditingLibrary::DeleteAllMaterialExpressionsInFunction(UMaterialFunction* MaterialFunction)
{
	if (MaterialFunction)
	{
		TArray<UMaterialExpression*> AllExpressions = MaterialFunction->FunctionExpressions;
		for (UMaterialExpression* Expression : AllExpressions)
		{
			DeleteMaterialExpressionInFunction(MaterialFunction, Expression);
		}
	}
}


void UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(UMaterialFunction* MaterialFunction, UMaterialExpression* Expression)
{
	if (MaterialFunction && Expression && Expression->GetOuter() == MaterialFunction)
	{
		// Break any links to this expression
		BreakLinksToExpression(MaterialFunction->FunctionExpressions, Expression);

		MaterialFunction->FunctionExpressions.Remove(Expression);

		Expression->MarkPendingKill();

		MaterialFunction->MarkPackageDirty();
	}
}


void UMaterialEditingLibrary::UpdateMaterialFunction(UMaterialFunctionInterface* MaterialFunction, UMaterial* PreviewMaterial)
{
	if (MaterialFunction)
	{
		// mark the function as changed
		MaterialFunction->PreEditChange(nullptr);
		MaterialFunction->PostEditChange();
		MaterialFunction->MarkPackageDirty();

		// Create a material update context so we can safely update materials using this function.
		{
			FMaterialUpdateContext UpdateContext;

			// Go through all function instances in memory and update them if they are children
			for (TObjectIterator<UMaterialFunctionInstance> It; It; ++It)
			{
				UMaterialFunctionInstance* FunctionInstance = *It;

				TArray<UMaterialFunctionInterface*> Functions;
				FunctionInstance->GetDependentFunctions(Functions);
				if (Functions.Contains(MaterialFunction))
				{
					FunctionInstance->UpdateParameterSet();
					FunctionInstance->MarkPackageDirty();
				}
			}

			// Go through all materials in memory and recompile them if they use this material function
			for (TObjectIterator<UMaterial> It; It; ++It)
			{
				UMaterial* CurrentMaterial = *It;
				if (CurrentMaterial != PreviewMaterial)
				{
					bool bRecompile = false;

					// Preview materials often use expressions for rendering that are not in their Expressions array, 
					// And therefore their MaterialFunctionInfos are not up to date.
					// However we don't want to trigger this if the Material is a preview material itself. This can now be the case with thumbnail preview materials for material functions.
					if (CurrentMaterial->bIsPreviewMaterial && (PreviewMaterial != nullptr) && !PreviewMaterial->bIsPreviewMaterial)
					{
						bRecompile = true;
					}
					else
					{
						TArray<UMaterialFunctionInterface*> Functions;
						CurrentMaterial->GetDependentFunctions(Functions);
						if (Functions.Contains(MaterialFunction))
						{
							bRecompile = true;
						}
					}

					if (bRecompile)
					{
						UpdateContext.AddMaterial(CurrentMaterial);

						// Propagate the function change to this material
						CurrentMaterial->PreEditChange(nullptr);
						CurrentMaterial->PostEditChange();
						CurrentMaterial->MarkPackageDirty();

						if (CurrentMaterial->MaterialGraph)
						{
							CurrentMaterial->MaterialGraph->RebuildGraph();
						}

						// if this instance was opened in an editor notify the change
						if (IMaterialEditor* MaterialEditor = MaterialEditingLibraryImpl::FindMaterialEditorForAsset(CurrentMaterial))
						{
							MaterialEditor->NotifyExternalMaterialChange();
						}
					}
				}
			}

			// Go through all material instances in memory and recompile them if they use this material function
			for (TObjectIterator<UMaterialInstance> It; It; ++It)
			{
				UMaterialInstance* CurrentInstance = *It;
				if (CurrentInstance->GetBaseMaterial())
				{
					TArray<UMaterialFunctionInterface*> Functions;
					CurrentInstance->GetDependentFunctions(Functions);
					if (Functions.Contains(MaterialFunction))
					{
						UpdateContext.AddMaterialInstance(CurrentInstance);
						CurrentInstance->PreEditChange(nullptr);
						CurrentInstance->PostEditChange();

						// if this instance was opened in an editor notify the change
						if (IMaterialEditor* MaterialEditor = MaterialEditingLibraryImpl::FindMaterialEditorForAsset(CurrentInstance))
						{
							MaterialEditor->NotifyExternalMaterialChange();
						}
					}
				}
			}
		}

		// update the world's viewports	
		UMaterialFunctionInstance* FunctionAsInstance = Cast<UMaterialFunctionInstance>(MaterialFunction);
		UMaterialFunction* BaseFunction = Cast<UMaterialFunction>(FunctionAsInstance ? FunctionAsInstance->GetBaseFunction() : MaterialFunction);

		UMaterialEditingLibrary::RebuildMaterialInstanceEditors(BaseFunction);
		FEditorDelegates::RefreshEditor.Broadcast();
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}

}

void UMaterialEditingLibrary::LayoutMaterialFunctionExpressions(UMaterialFunction* MaterialFunction)
{
	MaterialEditingLibraryImpl::LayoutMaterialExpressions( MaterialFunction );
}

void UMaterialEditingLibrary::SetMaterialInstanceParent(UMaterialInstanceConstant* Instance, UMaterialInterface* NewParent)
{
	if (Instance)
	{
		Instance->SetParentEditorOnly(NewParent);
	}
}

void UMaterialEditingLibrary::ClearAllMaterialInstanceParameters(UMaterialInstanceConstant* Instance)
{
	if (Instance)
	{
		Instance->ClearParameterValuesEditorOnly();
	}
}


float UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(UMaterialInstanceConstant* Instance, FName ParameterName)
{
	float Result = 0.f;
	if (Instance)
	{
		Instance->GetScalarParameterValue(ParameterName, Result);
	}
	return Result;
}

bool UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(UMaterialInstanceConstant* Instance, FName ParameterName, float Value)
{
	bool bResult = false;
	if (Instance)
	{
		Instance->SetScalarParameterValueEditorOnly(ParameterName, Value);
	}
	return bResult;
}


UTexture* UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(UMaterialInstanceConstant* Instance, FName ParameterName)
{
	UTexture* Result = nullptr;
	if (Instance)
	{
		Instance->GetTextureParameterValue(ParameterName, Result);
	}
	return Result;
}

bool UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(UMaterialInstanceConstant* Instance, FName ParameterName, UTexture* Value)
{
	bool bResult = false;
	if (Instance)
	{
		Instance->SetTextureParameterValueEditorOnly(ParameterName, Value);
	}
	return bResult;
}


FLinearColor UMaterialEditingLibrary::GetMaterialInstanceVectorParameterValue(UMaterialInstanceConstant* Instance, FName ParameterName)
{
	FLinearColor Result = FLinearColor::Black;
	if (Instance)
	{
		Instance->GetVectorParameterValue(ParameterName, Result);
	}
	return Result;
}

bool UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(UMaterialInstanceConstant* Instance, FName ParameterName, FLinearColor Value)
{
	bool bResult = false;
	if (Instance)
	{
		Instance->SetVectorParameterValueEditorOnly(ParameterName, Value);
	}
	return bResult;
}


bool UMaterialEditingLibrary::GetMaterialInstanceStaticSwitchParameterValue(UMaterialInstanceConstant* Instance, FName ParameterName)
{
	bool bResult = false;
	if (Instance)
	{
		FGuid OutGuid;
		Instance->GetStaticSwitchParameterValue(ParameterName, bResult, OutGuid);
	}
	return bResult;
}

void UMaterialEditingLibrary::UpdateMaterialInstance(UMaterialInstanceConstant* Instance)
{
	if (Instance)
	{
		Instance->MarkPackageDirty();
		Instance->PreEditChange(nullptr);
		Instance->PostEditChange();

		Instance->UpdateStaticPermutation();
		Instance->UpdateParameterNames();

		// update the world's viewports
		FEditorDelegates::RefreshEditor.Broadcast();
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}
}

void UMaterialEditingLibrary::GetChildInstances(UMaterialInterface* Parent, TArray< FAssetData>& ChildInstances)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> AssetList;
	TMultiMap<FName, FString> TagsAndValues;
	const FString ParentNameString = FAssetData(Parent).GetExportTextName();
	TagsAndValues.Add(GET_MEMBER_NAME_CHECKED(UMaterialInstance, Parent), ParentNameString);
	AssetRegistryModule.Get().GetAssetsByTagValues(TagsAndValues, AssetList);
	
	for (const FAssetData& MatInstRef : AssetList)
	{
		ChildInstances.Add(MatInstRef);
	}
}

void UMaterialEditingLibrary::GetScalarParameterNames(UMaterialInterface* Material, TArray<FName>& ParameterNames)
{
	ParameterNames.Empty();
	if (Material)
	{
		TArray<FMaterialParameterInfo> MaterialInfo;
		TArray<FGuid> MaterialGuids;
		Material->GetAllScalarParameterInfo(MaterialInfo, MaterialGuids);

		for (const FMaterialParameterInfo& Info : MaterialInfo)
		{
			ParameterNames.Add(Info.Name);
		}
	}
}

void UMaterialEditingLibrary::GetVectorParameterNames(UMaterialInterface* Material, TArray<FName>& ParameterNames)
{
	ParameterNames.Empty();
	if (Material)
	{
		TArray<FMaterialParameterInfo> MaterialInfo;
		TArray<FGuid> MaterialGuids;
		Material->GetAllVectorParameterInfo(MaterialInfo, MaterialGuids);

		for (const FMaterialParameterInfo& Info : MaterialInfo)
		{
			ParameterNames.Add(Info.Name);
		}
	}
}

void UMaterialEditingLibrary::GetTextureParameterNames(UMaterialInterface* Material, TArray<FName>& ParameterNames)
{
	ParameterNames.Empty();
	if (Material)
	{
		TArray<FMaterialParameterInfo> MaterialInfo;
		TArray<FGuid> MaterialGuids;
		Material->GetAllTextureParameterInfo(MaterialInfo, MaterialGuids);

		for (const FMaterialParameterInfo& Info : MaterialInfo)
		{
			ParameterNames.Add(Info.Name);
		}
	}
}

void UMaterialEditingLibrary::GetStaticSwitchParameterNames(UMaterialInterface* Material, TArray<FName>& ParameterNames)
{
	ParameterNames.Empty();
	if (Material)
	{
		TArray<FMaterialParameterInfo> MaterialInfo;
		TArray<FGuid> MaterialGuids;
		Material->GetAllStaticSwitchParameterInfo(MaterialInfo, MaterialGuids);

		for (const FMaterialParameterInfo& Info : MaterialInfo)
		{
			ParameterNames.Add(Info.Name);
		}
	}
}

bool GetParameterSource(UMaterialInterface* Material, const TArray<FMaterialParameterInfo>& Info, const TArray<FGuid>& Guids, const FName& ParameterName, FSoftObjectPath& OutParameterSource)
{
	bool bResult = false;
	for (int32 Index = 0; Index < Info.Num(); ++Index)
	{
		if (Info[Index].Name == ParameterName)
		{
			UMaterial* BaseMaterial = Material->GetBaseMaterial();
			UMaterialExpression* Expression = BaseMaterial->FindExpressionByGUID<UMaterialExpression>(Guids[Index]);
			if (Expression)
			{
				bResult = true;
				OutParameterSource = Expression->GetAssetOwner();
			}
			break;
		}
	}
	return bResult;
}

bool UMaterialEditingLibrary::GetScalarParameterSource(UMaterialInterface* Material, const FName ParameterName, FSoftObjectPath& ParameterSource)
{
	if (Material)
	{
		TArray<FMaterialParameterInfo> MaterialInfo;
		TArray<FGuid> MaterialGuids;
		Material->GetAllScalarParameterInfo(MaterialInfo, MaterialGuids);
		return GetParameterSource(Material, MaterialInfo, MaterialGuids, ParameterName, ParameterSource);
	}
	return false;
}

bool UMaterialEditingLibrary::GetVectorParameterSource(UMaterialInterface* Material, const FName ParameterName, FSoftObjectPath& ParameterSource)
{
	if (Material)
	{
		TArray<FMaterialParameterInfo> MaterialInfo;
		TArray<FGuid> MaterialGuids;
		Material->GetAllVectorParameterInfo(MaterialInfo, MaterialGuids);
		return GetParameterSource(Material, MaterialInfo, MaterialGuids, ParameterName, ParameterSource);
	}
	return false;
}

bool UMaterialEditingLibrary::GetTextureParameterSource(UMaterialInterface* Material, const FName ParameterName, FSoftObjectPath& ParameterSource)
{
	if (Material)
	{
		TArray<FMaterialParameterInfo> MaterialInfo;
		TArray<FGuid> MaterialGuids;
		Material->GetAllTextureParameterInfo(MaterialInfo, MaterialGuids);
		return GetParameterSource(Material, MaterialInfo, MaterialGuids, ParameterName, ParameterSource);
	}
	return false;
}

bool UMaterialEditingLibrary::GetStaticSwitchParameterSource(UMaterialInterface* Material, const FName ParameterName, FSoftObjectPath& ParameterSource)
{
	if (Material)
	{
		TArray<FMaterialParameterInfo> MaterialInfo;
		TArray<FGuid> MaterialGuids;
		Material->GetAllStaticSwitchParameterInfo(MaterialInfo, MaterialGuids);
		return GetParameterSource(Material, MaterialInfo, MaterialGuids, ParameterName, ParameterSource);
	}
	return false;
}

FMaterialStatistics UMaterialEditingLibrary::GetStatistics(class UMaterialInterface* Material)
{
	FMaterialStatistics Result;

	FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIFeatureLevel) : nullptr;
	if (Resource)
	{
		Resource->FinishCompilation();

		TArray<FMaterialStatsUtils::FShaderInstructionsInfo> InstructionInfos;
		FMaterialStatsUtils::GetRepresentativeInstructionCounts(InstructionInfos, Resource);
		for (const FMaterialStatsUtils::FShaderInstructionsInfo& Info : InstructionInfos)
		{
			const int32 ShaderType = (int32)Info.ShaderType;
			if (ShaderType >= (int32)ERepresentativeShader::FirstFragmentShader && ShaderType <= (int32)ERepresentativeShader::LastFragmentShader)
			{
				Result.NumPixelShaderInstructions = FMath::Max(Result.NumPixelShaderInstructions, Info.InstructionCount);
			}
			else if (ShaderType >= (int32)ERepresentativeShader::FirstVertexShader && ShaderType <= (int32)ERepresentativeShader::LastVertexShader)
			{
				Result.NumVertexShaderInstructions = FMath::Max(Result.NumVertexShaderInstructions, Info.InstructionCount);
			}
		}

		Result.NumSamplers = Resource->GetSamplerUsage();

		uint32 NumVSTextureSamples = 0, NumPSTextureSamples = 0;
		Resource->GetEstimatedNumTextureSamples(NumVSTextureSamples, NumPSTextureSamples);
		Result.NumVertexTextureSamples = (int32)NumVSTextureSamples;
		Result.NumPixelTextureSamples = (int32)NumPSTextureSamples;

		Result.NumVirtualTextureSamples = Resource->GetEstimatedNumVirtualTextureLookups();

		uint32 UVScalarsUsed, CustomInterpolatorScalarsUsed;
		Resource->GetUserInterpolatorUsage(UVScalarsUsed, CustomInterpolatorScalarsUsed);
		Result.NumUVScalars = (int32)UVScalarsUsed;
		Result.NumInterpolatorScalars = (int32)CustomInterpolatorScalarsUsed;
	}

	return Result;
}