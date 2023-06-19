// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/GenerateMutableSource/GenerateMutableSourceTable.h"

#include "Animation/AnimInstance.h"
#include "Engine/StaticMesh.h"
#include "GameplayTagContainer.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "MuCO/CustomizableObjectSystem.h"
#include "MuCO/UnrealPortabilityHelpers.h"
#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTable.h"
#include "MuR/Mesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


bool FillTableColumn(const UCustomizableObjectNodeTable* TableNode,	mu::TablePtr MutableTable,	const FString& ColumnName,	const FString& RowName,	const int32 RowIdx,	uint8* CellData, FProperty* Property,
	const int LODIndexConnected, const int32 SectionIndexConnected, int32 LODIndex, int32 SectionIndex, const bool bOnlyConnectedLOD, FMutableGraphGenerationContext& GenerationContext)
{
	int32 CurrentColumn;

	// Getting property type
	if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
	{
		UObject* Object = SoftObjectProperty->GetPropertyValue(CellData).LoadSynchronous();

		if (SoftObjectProperty->PropertyClass->IsChildOf(USkeletalMesh::StaticClass()))
		{
			USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object);

			if(!SkeletalMesh)
			{
				return false;
			}

			// Getting Animation Blueprint and Animation Slot
			FString AnimBP, AnimSlot, GameplayTag, AnimBPAssetTag;
			TArray<FGameplayTag> GameplayTags;

			TableNode->GetAnimationColumns(ColumnName, AnimBP, AnimSlot, GameplayTag);

			if (!AnimBP.IsEmpty())
			{
				if (!AnimSlot.IsEmpty())
				{
					if (TableNode->Table)
					{
						uint8* AnimRowData = TableNode->Table->FindRowUnchecked(FName(*RowName));

						if (AnimRowData)
						{
							FName SlotIndex;

							// Getting animation slot row value from data table
							if (FProperty* AnimSlotProperty = TableNode->Table->FindTableProperty(FName(*AnimSlot)))
							{
								uint8* AnimSlotData = AnimSlotProperty->ContainerPtrToValuePtr<uint8>(AnimRowData, 0);

								if (AnimSlotData)
								{
									if (const FIntProperty* IntProperty = CastField<FIntProperty>(AnimSlotProperty))
									{
										FString Message = FString::Printf(
											TEXT("The column with name [%s] for the Anim Slot property should be an FName instead of an Integer, it will be internally converted to FName but should probaly be converted in the table itself."), 
											*AnimBP);
										GenerationContext.Compiler->CompilerLog(FText::FromString(Message), TableNode, EMessageSeverity::Info);

										SlotIndex = FName(FString::FromInt(IntProperty->GetPropertyValue(AnimSlotData)));
									}
									else if (const FNameProperty* NameProperty = CastField<FNameProperty>(AnimSlotProperty))
									{
										SlotIndex = NameProperty->GetPropertyValue(AnimSlotData);
									}
								}
							}

							if (SlotIndex.GetStringLength() != 0)
							{
								// Getting animation instance soft class from data table
								if (FProperty* AnimBPProperty = TableNode->Table->FindTableProperty(FName(*AnimBP)))
								{
									uint8* AnimBPData = AnimBPProperty->ContainerPtrToValuePtr<uint8>(AnimRowData, 0);

									if (AnimBPData)
									{
										if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(AnimBPProperty))
										{
											TSoftClassPtr<UAnimInstance> AnimInstance(SoftClassProperty->GetPropertyValue(AnimBPData).ToSoftObjectPath());

											if (!AnimInstance.IsNull())
											{
												GenerationContext.AnimBPAssetsMap.Add(AnimInstance.ToString(), AnimInstance);

												AnimBPAssetTag = GenerateAnimationInstanceTag(AnimInstance.ToString(), SlotIndex);
											}
										}
									}
								}
							}
							else
							{
								FString msg = FString::Printf(TEXT("Could not find the Slot column of the animation blueprint column [%s] for the mesh column [%s] row [%s]."), *AnimBP, *ColumnName, *RowName);
								GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
							}
						}
					}
				}
				else
				{
					FString msg = FString::Printf(TEXT("Could not found the Slot column of the animation blueprint column [%s] for the mesh column [%s]."), *AnimBP, *ColumnName);
					GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
				}
			}

			// Getting Gameplay tags
			if (!GameplayTag.IsEmpty())
			{
				if (TableNode->Table)
				{
					uint8* GameplayRowData = TableNode->Table->FindRowUnchecked(FName(*RowName));

					if (GameplayRowData)
					{
						// Getting animation tag row value from data table
						if (FProperty* GameplayTagProperty = TableNode->Table->FindTableProperty(FName(*GameplayTag)))
						{
							uint8* GameplayTagData = GameplayTagProperty->ContainerPtrToValuePtr<uint8>(GameplayRowData, 0);

							if (const FStructProperty* StructProperty = CastField<FStructProperty>(GameplayTagProperty))
							{
								if (StructProperty->Struct == TBaseStructure<FGameplayTagContainer>::Get())
								{
									if (GameplayTagData)
									{
										const FGameplayTagContainer TagContainer = *(FGameplayTagContainer*)GameplayTagData;
										TagContainer.GetGameplayTagArray(GameplayTags);
									}
								}
							}
						}
					}
				}
			}

			// Getting reference Mesh
			USkeletalMesh* ReferenceSkeletalMesh = TableNode->GetColumnDefaultAssetByType<USkeletalMesh>(ColumnName);

			if (!ReferenceSkeletalMesh)
			{
				FString msg = FString::Printf(TEXT("Reference Skeletal Mesh not found for column [%s]."), *ColumnName);
				GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);

				return false;
			}

			GetLODAndSectionForAutomaticLODs(GenerationContext, *TableNode, *SkeletalMesh, LODIndexConnected, SectionIndexConnected, LODIndex, SectionIndex, bOnlyConnectedLOD);
			
			// Parameter used for LOD differences
	
			if (GenerationContext.CurrentAutoLODStrategy != ECustomizableObjectAutomaticLODStrategy::AutomaticFromMesh || 
				SectionIndex == SectionIndexConnected)
			{
				const int32 NumLODs = SkeletalMesh->GetImportedModel()->LODModels.Num();

				if (NumLODs <= LODIndex)
				{
					LODIndex = NumLODs - 1;

					FString msg = FString::Printf(TEXT("Mesh from column [%s] row [%s] needs LOD %d but has less LODs than the reference mesh. LOD %d will be used instead. This can cause some performance penalties."),
						*ColumnName, *RowName, LODIndex, LODIndex);
					GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
				}
			}

			FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
			
			if (ImportedModel->LODModels.IsValidIndex(LODIndex)) // Ignore error since this Section is empty due to Automatic LODs From Mesh
			{
				int32 NumSections = ImportedModel->LODModels[LODIndex].Sections.Num();
				int32 ReferenceNumMaterials = ImportedModel->LODModels[LODIndex].Sections.Num();

				if (NumSections != ReferenceNumMaterials)
				{
					FString Dif_1 = NumSections > ReferenceNumMaterials ? "more" : "less";
					FString Dif_2 = NumSections > ReferenceNumMaterials ? "Some will be ignored" : "This can cause some compilation errors.";

					FString msg = FString::Printf(TEXT("Mesh from column [%s] row [%s] has %s Sections than the reference mesh. %s"), *ColumnName, *RowName, *Dif_1, *Dif_2);
					GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
				}
			}

			FString MutableColumnName = TableNode->GenerateSkeletalMeshMutableColumName(ColumnName, LODIndex, SectionIndex);

			CurrentColumn = MutableTable.get()->FindColumn(StringCast<ANSICHAR>(*MutableColumnName).Get());

			if (CurrentColumn == -1)
			{
				CurrentColumn = MutableTable->AddColumn(StringCast<ANSICHAR>(*MutableColumnName).Get(), mu::TABLE_COLUMN_TYPE::TCT_MESH);
			}

			// First process the mesh tags that are going to make the mesh unique and affect whether it's repeated in 
			// the mesh cache or not
			FString MeshUniqueTags;

			if (!AnimBPAssetTag.IsEmpty())
			{
				MeshUniqueTags += AnimBPAssetTag;
			}

			TArray<FString> ArrayAnimBPTags;

			for (const FGameplayTag& Tag : GameplayTags)
			{
				MeshUniqueTags += GenerateGameplayTag(Tag.ToString());
			}

			//TODO: Add AnimBp physics to Tables.
			mu::MeshPtr MutableMesh = GenerateMutableMesh(SkeletalMesh, TSoftClassPtr<UAnimInstance>(), LODIndexConnected, SectionIndexConnected, LODIndex, SectionIndex, MeshUniqueTags, GenerationContext, TableNode);

			if (MutableMesh)
			{
 				if (SkeletalMesh->GetPhysicsAsset() && MutableMesh->GetPhysicsBody() && MutableMesh->GetPhysicsBody()->GetBodyCount())
				{	
					TSoftObjectPtr<UPhysicsAsset> PhysicsAsset = SkeletalMesh->GetPhysicsAsset();
					GenerationContext.PhysicsAssetMap.Add(PhysicsAsset.ToString(), PhysicsAsset);
					FString PhysicsAssetTag = FString("__PhysicsAsset:") + PhysicsAsset.ToString();

					AddTagToMutableMeshUnique(*MutableMesh, PhysicsAssetTag);
				}

				if (!AnimBPAssetTag.IsEmpty())
				{
					AddTagToMutableMeshUnique(*MutableMesh, AnimBPAssetTag);
				}

				for (const FGameplayTag& Tag : GameplayTags)
				{
					AddTagToMutableMeshUnique(*MutableMesh, GenerateGameplayTag(Tag.ToString()));
				}

				AddSocketTagsToMesh(SkeletalMesh, MutableMesh, GenerationContext);

				if (UCustomizableObjectSystem::GetInstance()->IsMutableAnimInfoDebuggingEnabled())
				{
					FString MeshPath;
					SkeletalMesh->GetOuter()->GetPathName(nullptr, MeshPath);
					FString MeshTag = FString("__MeshPath:") + MeshPath;
					AddTagToMutableMeshUnique(*MutableMesh, MeshTag);
				}

				MutableTable->SetCell(CurrentColumn, RowIdx, MutableMesh.get());
			}
			else
			{
				FString msg = FString::Printf(TEXT("Error converting skeletal mesh LOD %d, Section %d from column [%s] row [%s] to mutable."),
					LODIndex, SectionIndex, *ColumnName, *RowName);
				GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
			}
		}

		else if (SoftObjectProperty->PropertyClass->IsChildOf(UStaticMesh::StaticClass()))
		{
			UStaticMesh* StaticMesh = Cast<UStaticMesh>(Object);

			if (!StaticMesh)
			{
				return false;
			}

			// Getting reference Mesh
			UStaticMesh* ReferenceStaticMesh = TableNode->GetColumnDefaultAssetByType<UStaticMesh>(ColumnName);

			if (!ReferenceStaticMesh)
			{
				FString msg = FString::Printf(TEXT("Reference Static Mesh not found for column [%s]."), *ColumnName);
				GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);

				return false;
			}

			// Parameter used for LOD differences
			int32 CurrentLOD = LODIndex;

			int NumLODs = StaticMesh->GetRenderData()->LODResources.Num();

			if (NumLODs <= CurrentLOD)
			{
				CurrentLOD = NumLODs - 1;

				FString msg = FString::Printf(TEXT("Mesh from column [%s] row [%s] needs LOD %d but has less LODs than the reference mesh. LOD %d will be used instead. This can cause some performance penalties."),
					*ColumnName, *RowName, LODIndex, CurrentLOD);
				GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
			}

			int32 NumMaterials = StaticMesh->GetRenderData()->LODResources[CurrentLOD].Sections.Num();
			int32 ReferenceNumMaterials = ReferenceStaticMesh->GetRenderData()->LODResources[CurrentLOD].Sections.Num();

			if (NumMaterials != ReferenceNumMaterials)
			{
				FString FirstTextOption = NumMaterials > ReferenceNumMaterials ? "more" : "less";
				FString SecondTextOption = NumMaterials > ReferenceNumMaterials ? "Some will be ignored" : "This can cause some compilation errors.";

				FString msg = FString::Printf(TEXT("Mesh from column [%s] row [%s] has %s Sections than the reference mesh. %s"), *ColumnName, *RowName, *FirstTextOption, *SecondTextOption);
				GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
			}

			FString MutableColumnName = TableNode->GenerateStaticMeshMutableColumName(ColumnName, SectionIndex);

			CurrentColumn = MutableTable.get()->FindColumn(StringCast<ANSICHAR>(*MutableColumnName).Get());

			if (CurrentColumn == -1)
			{
				CurrentColumn = MutableTable->AddColumn(StringCast<ANSICHAR>(*MutableColumnName).Get(), mu::TABLE_COLUMN_TYPE::TCT_MESH);
			}

			mu::MeshPtr MutableMesh = GenerateMutableMesh(StaticMesh, TSoftClassPtr<UAnimInstance>(), CurrentLOD, SectionIndex, CurrentLOD, SectionIndex, FString(), GenerationContext, TableNode); // TODO GMT

			if (MutableMesh)
			{
				MutableTable->SetCell(CurrentColumn, RowIdx, MutableMesh.get());
			}
			else
			{
				FString msg = FString::Printf(TEXT("Error converting skeletal mesh LOD %d, Section %d from column [%s] row [%s] to mutable."),
					LODIndex, SectionIndex, *ColumnName, *RowName);

				GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
			}
		}

		else if (SoftObjectProperty->PropertyClass->IsChildOf(UTexture::StaticClass()))
		{
			UTexture2D* Texture = Cast<UTexture2D>(Object);

			if (!Texture)
			{
				Texture = TableNode->GetColumnDefaultAssetByType<UTexture2D>(ColumnName);
				FString Message = Cast<UObject>(Object) ? "not a Texture2D" : "null";

				FString msg = FString::Printf(TEXT("Texture from column [%s] row [%s] is %s. The default texture will be used instead."), *ColumnName, *RowName, *Message);
				GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
			}

			// Getting column index from column name
			CurrentColumn = MutableTable->FindColumn(StringCast<ANSICHAR>(*ColumnName).Get());

			if (CurrentColumn == INDEX_NONE)
			{
				CurrentColumn = MutableTable->AddColumn(StringCast<ANSICHAR>(*ColumnName).Get(), mu::TABLE_COLUMN_TYPE::TCT_IMAGE);
			}

			if (TableNode->GetColumnImageMode(ColumnName) == ETableTextureType::PASSTHROUGH_TEXTURE)
			{
				uint32* FoundIndex = GenerationContext.PassThroughTextureToIndexMap.Find(Texture);
				uint32 ImageReferenceID;

				if (!FoundIndex)
				{
					ImageReferenceID = GenerationContext.PassThroughTextureToIndexMap.Num();
					GenerationContext.PassThroughTextureToIndexMap.Add(Texture, ImageReferenceID);
				}
				else
				{
					ImageReferenceID = *FoundIndex;
				}

				MutableTable->SetCell(CurrentColumn, RowIdx, mu::Image::CreateAsReference(ImageReferenceID).get());
			}
			else
			{
				GenerationContext.ArrayTextureUnrealToMutableTask.Add(FTextureUnrealToMutableTask(MutableTable, Texture, TableNode, CurrentColumn, RowIdx));
			}
		}

		else if (SoftObjectProperty->PropertyClass->IsChildOf(UMaterialInstance::StaticClass()))
		{
			UMaterialInstance* Material = Cast<UMaterialInstance>(Object);

			if (!Material)
			{
				Material = TableNode->GetColumnDefaultAssetByType<UMaterialInstance>(ColumnName);

				FString msg = FString::Printf(TEXT("Material Instance from column [%s] row [%s] is null. The default Material Instance will be used instead."), *ColumnName, *RowName);
				GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
			}

			//Adding an empty column for searching purposes
			if (MutableTable.get()->FindColumn(StringCast<ANSICHAR>(*ColumnName).Get()) == -1)
			{
				CurrentColumn = MutableTable->AddColumn(StringCast<ANSICHAR>(*ColumnName).Get(), mu::TABLE_COLUMN_TYPE::TCT_NONE);
			}

			UMaterialInstance* ReferenceMaterial = TableNode->GetColumnDefaultAssetByType<UMaterialInstance>(ColumnName);
			
			if (!GenerationContext.GeneratedParametersInTables.Contains(TableNode))
			{
				GenerationContext.GeneratedParametersInTables.Add(TableNode);
			}

			TArray<FGuid>& Parameters = GenerationContext.GeneratedParametersInTables[TableNode];

			if (!ReferenceMaterial)
			{
				FString msg = FString::Printf(TEXT("Reference Material not found for column [%s]."), *ColumnName);
				GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);

				return false;
			}

			if (ReferenceMaterial->GetMaterial() == Material->GetMaterial())
			{
				// Getting parameter Guids
				TArray<FMaterialParameterInfo > ParameterInfos;
				TArray<FGuid> ParameterGuids;

				ReferenceMaterial->GetAllParameterInfoOfType(EMaterialParameterType::Texture, ParameterInfos, ParameterGuids);

				// Number of modified texture parameters in the Material Instance
				int32 ModifiedTextureParameters = 0;
				
				for (FTextureParameterValue ReferenceTexture : ReferenceMaterial->TextureParameterValues)
				{
					if (UTexture2D* Texture = Cast<UTexture2D>(ReferenceTexture.ParameterValue))
					{
						int32 ColumnIndex;

						FString TextureParameterName = ReferenceTexture.ParameterInfo.Name.ToString();
						FString ParameterGuid;
						
						for (int32 i = 0; i < ParameterInfos.Num(); ++i)
						{
							if (ParameterInfos[i].Name == ReferenceTexture.ParameterInfo.Name)
							{
								ParameterGuid = ParameterGuids[i].ToString();
								Parameters.Add(ParameterGuids[i]);

								break;
							}
						}

						// Getting column index from parameter name
						ColumnIndex = MutableTable->FindColumn(StringCast<ANSICHAR>(*ParameterGuid).Get());

						if (ColumnIndex == INDEX_NONE)
						{
							// If there is no column with the parameters name, we generate a new one
							ColumnIndex = MutableTable->AddColumn(StringCast<ANSICHAR>(*ParameterGuid).Get(), mu::TABLE_COLUMN_TYPE::TCT_IMAGE);
						}

						// Getting the parameter value from the instance if it has been modified
						for (FTextureParameterValue InstanceTexture : Material->TextureParameterValues)
						{
							if (InstanceTexture.ParameterInfo.Name == ReferenceTexture.ParameterInfo.Name)
							{
								if (UTexture2D* ParameterTexture = Cast<UTexture2D>(InstanceTexture.ParameterValue))
								{
									Texture = ParameterTexture;
								}
								else
								{
									FString ParamName = ReferenceTexture.ParameterInfo.Name.ToString();
									FString Message = Cast<UObject>(InstanceTexture.ParameterValue) ? "not a Texture2D" : "null";

									FString msg = FString::Printf(TEXT("Parameter [%s] from material instance of column [%s] row [%s] is %s. The parameter texture of the default material will be used instead."), *ParamName,  *ColumnName, *RowName, *Message);
									GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
								}

								ModifiedTextureParameters++;
								
								break;
							}
						}

						check(Texture);
						GenerationContext.ArrayTextureUnrealToMutableTask.Add(FTextureUnrealToMutableTask(MutableTable, Texture, TableNode, ColumnIndex, RowIdx));
					}
					else
					{
						FString ParamName = ReferenceTexture.ParameterInfo.Name.ToString();
						FString Message = Cast<UObject>(ReferenceTexture.ParameterValue) ? "not a Texture2D" : "null";

						FString msg = FString::Printf(TEXT("Parameter [%s] from Default Material Instance of column [%s] is %s. This parameter will be ignored."), *ParamName, *ColumnName, *Message);
						GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
					}
				}

				// Checking if all modifiable textures of the material instance have been modified
				if (Material->TextureParameterValues.Num() > ModifiedTextureParameters)
				{
					int32 ParametersDiff = Material->TextureParameterValues.Num() - ModifiedTextureParameters;
					FString msg =
						FString::Printf(TEXT("Material Instance [%s] from column [%s] row [%s] has %d modifiable Textures that will not be modified, they are non-modifiable parameters in the Default Material Instance"),
						*Material->GetName(), *ColumnName, *RowName, ParametersDiff);
					GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
				}

				ParameterInfos.Empty();
				ParameterGuids.Empty();

				ReferenceMaterial->GetAllParameterInfoOfType(EMaterialParameterType::Vector, ParameterInfos, ParameterGuids);

				// Number of modified Vector parameters in the Material Instance
				int32 ModifiedVectorParameters = 0;

				for (FVectorParameterValue ReferenceVector : ReferenceMaterial->VectorParameterValues)
				{
					// Getting the parameter value from the default material
					FLinearColor VectorValue = ReferenceVector.ParameterValue;

					int32 ColumnIndex;
					FString ParameterGuid;

					for (int32 i = 0; i < ParameterInfos.Num(); ++i)
					{
						if (ParameterInfos[i].Name == ReferenceVector.ParameterInfo.Name)
						{
							ParameterGuid = ParameterGuids[i].ToString();
							Parameters.Add(ParameterGuids[i]);

							break;
						}
					}
					
					// Getting column index from parameter name
					ColumnIndex = MutableTable->FindColumn(StringCast<ANSICHAR>(*ParameterGuid).Get());

					if (ColumnIndex == INDEX_NONE)
					{
						// If there is no column with the parameters name, we generate a new one
						ColumnIndex = MutableTable->AddColumn(StringCast<ANSICHAR>(*ParameterGuid).Get(), mu::TABLE_COLUMN_TYPE::TCT_COLOUR);
					}

					// Getting the parameter value from the instance if it has been modified
					for (FVectorParameterValue InstanceVector : Material->VectorParameterValues)
					{
						if (InstanceVector.ParameterInfo.Name == ReferenceVector.ParameterInfo.Name)
						{
							VectorValue = InstanceVector.ParameterValue;
							ModifiedVectorParameters++;
							
							break;
						}
					}

					// Setting cell value
					MutableTable->SetCell(ColumnIndex, RowIdx, VectorValue.R, VectorValue.G, VectorValue.B, VectorValue.A);
				}

				// Checking if all modifiable vectors of the material instance have been modified
				if (Material->VectorParameterValues.Num() > ModifiedVectorParameters)
				{
					int32 ParametersDiff = Material->VectorParameterValues.Num() - ModifiedVectorParameters;

					FString msg =
						FString::Printf(TEXT("Material Instance [%s] from column [%s] row [%s] has %d modifiable Vectors that will not be modified, they are non-modifiable parameters in the Default Material Instance"),
							*Material->GetName(), *ColumnName, *RowName, ParametersDiff);
					GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
				}

				// Number of modified Float parameters in the Material Instance
				int32 ModifiedFloatParameters = 0;

				ParameterInfos.Empty();
				ParameterGuids.Empty();

				ReferenceMaterial->GetAllParameterInfoOfType(EMaterialParameterType::Scalar, ParameterInfos, ParameterGuids);

				for (FScalarParameterValue ReferenceScalar : ReferenceMaterial->ScalarParameterValues)
				{
					// Getting the parameter value from the parent material
					float ScalarValue = ReferenceScalar.ParameterValue;

					int32 ColumnIndex;
					FString ParameterGuid;

					for (int32 i = 0; i < ParameterInfos.Num(); ++i)
					{
						if (ParameterInfos[i].Name == ReferenceScalar.ParameterInfo.Name)
						{
							ParameterGuid = ParameterGuids[i].ToString();
							Parameters.Add(ParameterGuids[i]);
							
							break;
						}
					}

					// Getting column index from parameter name
					ColumnIndex = MutableTable->FindColumn(StringCast<ANSICHAR>(*ParameterGuid).Get());

					if (ColumnIndex == INDEX_NONE)
					{
						// If there is no column with the parameters name, we generate a new one
						ColumnIndex = MutableTable->AddColumn(StringCast<ANSICHAR>(*ParameterGuid).Get(), mu::TABLE_COLUMN_TYPE::TCT_SCALAR);
					}

					// Getting the parameter value from the instance if it has been modified
					for (FScalarParameterValue InstanceScalar : Material->ScalarParameterValues)
					{
						if (InstanceScalar.ParameterInfo.Name == ReferenceScalar.ParameterInfo.Name)
						{
							ScalarValue = InstanceScalar.ParameterValue;
							ModifiedFloatParameters++;
							
							break;
						}
					}

					// Setting cell value
					MutableTable->SetCell(ColumnIndex, RowIdx, ScalarValue);
				}

				// Checking if all modifiable floats of the material instance have been modified
				if (Material->ScalarParameterValues.Num() > ModifiedFloatParameters)
				{
					int32 ParametersDiff = Material->ScalarParameterValues.Num() - ModifiedFloatParameters;

					FString msg =
						FString::Printf(TEXT("Material Instance [%s] from column [%s] row [%s] has %d modifiable Scalars that will not be modified, they are non-modifiable parameters in the Default Material Instance"),
							*Material->GetName(), *ColumnName, *RowName, ParametersDiff);
					GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
				}
			}
			else
			{
				FString msg = FString::Printf(TEXT("Material from column [%s] row [%s] is a diferent instance than the Reference Material of the table."), *ColumnName, *RowName);
				GenerationContext.Compiler->CompilerLog(FText::FromString(msg), TableNode);
			}
		}

		else
		{
			// Unsuported Variable Type
			return false;
		}
	}

	else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
		{
			CurrentColumn = MutableTable->FindColumn(StringCast<ANSICHAR>(*ColumnName).Get());

			if (CurrentColumn == INDEX_NONE)
			{
				CurrentColumn = MutableTable->AddColumn(StringCast<ANSICHAR>(*ColumnName).Get(), mu::TABLE_COLUMN_TYPE::TCT_COLOUR);
			}

			// Setting cell value
			FLinearColor Value = *(FLinearColor*)CellData;
			MutableTable->SetCell(CurrentColumn, RowIdx, Value.R, Value.G, Value.B, Value.A);
		}
		
		else
		{
			// Unsuported Variable Type
			return false;
		}
	}

	else if (const FNumericProperty* FloatNumProperty = CastField<FFloatProperty>(Property))
	{
		CurrentColumn = MutableTable->FindColumn(StringCast<ANSICHAR>(*ColumnName).Get());

		if (CurrentColumn == INDEX_NONE)
		{
			CurrentColumn = MutableTable->AddColumn(StringCast<ANSICHAR>(*ColumnName).Get(), mu::TABLE_COLUMN_TYPE::TCT_SCALAR);
		}

		// Setting cell value
		float Value = FloatNumProperty->GetFloatingPointPropertyValue(CellData);
		MutableTable->SetCell(CurrentColumn, RowIdx, Value);
	}

	else if (const FNumericProperty* DoubleNumProperty = CastField<FDoubleProperty>(Property))
	{
		CurrentColumn = MutableTable->FindColumn(StringCast<ANSICHAR>(*ColumnName).Get());
	
		if (CurrentColumn == INDEX_NONE)
		{
			CurrentColumn = MutableTable->AddColumn(StringCast<ANSICHAR>(*ColumnName).Get(), mu::TABLE_COLUMN_TYPE::TCT_SCALAR);
		}
	
		// Setting cell value
		float Value = DoubleNumProperty->GetFloatingPointPropertyValue(CellData);
		MutableTable->SetCell(CurrentColumn, RowIdx, Value);
	}

	else
	{
		// Unsuported Variable Type
		return false;
	}

	return true;
}


bool GenerateTableColumn(const UCustomizableObjectNodeTable* TableNode, const UEdGraphPin* Pin, mu::TablePtr MutableTable, const FString& DataTableColumnName,
	const int32 LODIndexConnected, const int32 SectionIndexConnected, const int32 LODIndex, const int32 SectionIndex, const bool bOnlyConnectedLOD, FMutableGraphGenerationContext& GenerationContext)
{
	SCOPED_PIN_DATA(GenerationContext, Pin)

	bool bSuccess = false;

	if (!TableNode || !TableNode->Table || !TableNode->Table->GetRowStruct())
	{
		return false;
	}

	FProperty* ColumnProperty = TableNode->Table->FindTableProperty(FName(*DataTableColumnName));

	if (!ColumnProperty)
	{
		return false;
	}
	
	// Getting names of the rows to access the information
	TArray<FName> RowNames = TableNode->GetRowNames();

	for (int32 RowIndex = 0; RowIndex < RowNames.Num(); ++RowIndex)
	{
		// Getting Row Data
		uint8* RowData = TableNode->Table->FindRowUnchecked(RowNames[RowIndex]);

		if (RowData)
		{
			// Getting Cell Data
			uint8* CellData = ColumnProperty->ContainerPtrToValuePtr<uint8>(RowData, 0);

			if (CellData)
			{
				bool bColumnGenerated = FillTableColumn(TableNode, MutableTable, DataTableColumnName, RowNames[RowIndex].ToString(), RowIndex, CellData, ColumnProperty,
					LODIndexConnected, SectionIndexConnected, LODIndex, SectionIndex, bOnlyConnectedLOD, GenerationContext);
				
				if (!bSuccess)
				{
					bSuccess = bColumnGenerated;
				}
			}
		}
	}

	return bSuccess;
}


mu::TablePtr GenerateMutableSourceTable(const FString& TableName, const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext)
{	
	if (mu::TablePtr* Result = GenerationContext.GeneratedTables.Find(TableName))
	{
		return *Result;
	}

	mu::TablePtr MutableTable = new mu::Table();
	
	UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(Pin->GetOwningNode());
	if (Node->IsNodeOutDatedAndNeedsRefresh())
	{
		Node->SetRefreshNodeWarning();
	}

	if (const UCustomizableObjectNodeTable* TypedTable = Cast<UCustomizableObjectNodeTable>(Node))
	{
		UDataTable* Table = TypedTable->Table;

		if (!Table)
		{
			FString msg = "Couldn't find the Data Table asset in the Node.";
			GenerationContext.Compiler->CompilerLog(FText::FromString(msg), Node);

			return nullptr;
		}

		const UScriptStruct* TableStruct = Table->GetRowStruct();

		if (TableStruct)
		{
			// Getting names of the rows to access the information
			TArray<FName> RowNames = TypedTable->GetRowNames();

			// Adding and filling Name Column
			MutableTable->AddColumn("Name", mu::TABLE_COLUMN_TYPE::TCT_STRING);

			// Add metadata
			FParameterUIData ParameterUIData(
				TypedTable->ParameterName,
				TypedTable->ParamUIMetadata,
				EMutableParameterType::Int);
			
			ParameterUIData.IntegerParameterGroupType = TypedTable->bAddNoneOption ? ECustomizableObjectGroupType::COGT_ONE_OR_NONE : ECustomizableObjectGroupType::COGT_ONE;

			// Adding a None Option
			MutableTable->SetNoneOption(TypedTable->bAddNoneOption);

			for (int32 i = 0; i < RowNames.Num(); ++i)
			{
				MutableTable->AddRow(i);
				FString RowName= RowNames[i].ToString();
				MutableTable->SetCell(0, i, StringCast<ANSICHAR>(*RowName).Get());
				ParameterUIData.ArrayIntegerParameterOption.Add(FIntegerParameterUIData(
					RowName,
					FMutableParamUIMetadata()));
			}

			GenerationContext.ParameterUIDataMap.Add(TypedTable->ParameterName, ParameterUIData);
		}
		else
		{
			FString msg = "Couldn't find the Data Table's Struct asset in the Node.";
			GenerationContext.Compiler->CompilerLog(FText::FromString(msg), Node);
			
			return nullptr;
		}
	}
	else
	{
		GenerationContext.Compiler->CompilerLog(LOCTEXT("UnimplementedNode", "Node type not implemented yet."), Node);

		return nullptr;
	}

	GenerationContext.GeneratedTables.Add(TableName, MutableTable);

	return MutableTable;
}

#undef LOCTEXT_NAMESPACE

