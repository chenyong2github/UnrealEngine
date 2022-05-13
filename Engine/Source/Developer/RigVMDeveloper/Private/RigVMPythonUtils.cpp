// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMPythonUtils.h"

#if WITH_EDITOR
#include "Modules/ModuleManager.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#endif

#define LOCTEXT_NAMESPACE "RigVMDeveloperModule"


FString RigVMPythonUtils::NameToPep8(const FString& Name)
{
	// Wish we could use PyGenUtil::PythonizeName, but unfortunately it's private

	const FString NameNoSpaces = Name.Replace(TEXT(" "), TEXT("_"));
	FString Result;

	for (const TCHAR& Char : NameNoSpaces)
	{
		if (FChar::IsUpper(Char))
		{
			if (!Result.IsEmpty() && !Result.EndsWith(TEXT("_")))
			{
				Result.AppendChar(TEXT('_'));
			}
			Result.AppendChar(FChar::ToLower(Char));
		}
		else
		{
			Result.AppendChar(Char);
		}
	}
	return Result;
}

FString RigVMPythonUtils::TransformToPythonString(const FTransform& Transform)
{
	static constexpr TCHAR TransformFormat[] = TEXT("unreal.Transform(location=[%f,%f,%f],rotation=[%f,%f,%f],scale=[%f,%f,%f])");
	return FString::Printf(TransformFormat,
	                       Transform.GetLocation().X,
	                       Transform.GetLocation().Y,
	                       Transform.GetLocation().Z,
	                       Transform.Rotator().Pitch,
	                       Transform.Rotator().Yaw,
	                       Transform.Rotator().Roll,
	                       Transform.GetScale3D().X,
	                       Transform.GetScale3D().Y,
	                       Transform.GetScale3D().Z);
}

FString RigVMPythonUtils::Vector2DToPythonString(const FVector2D& Vector)
{
	static constexpr TCHAR Vector2DFormat[] = TEXT("unreal.Vector2D(%f, %f)");
	return FString::Printf(Vector2DFormat,
	                       Vector.X,
	                       Vector.Y);
}

FString RigVMPythonUtils::LinearColorToPythonString(const FLinearColor& Color)
{
	static constexpr TCHAR LinearColorFormat[] = TEXT("unreal.LinearColor(%f, %f, %f, %f)");
	return FString::Printf(LinearColorFormat,
	                       Color.R, Color.G, Color.B, Color.A);
}

FString RigVMPythonUtils::EnumValueToPythonString(UEnum* Enum, int64 Value)
{
	static constexpr TCHAR EnumPrefix[] = TEXT("E");
	static constexpr TCHAR EnumValueFormat[] = TEXT("unreal.%s.%s");

	FString EnumName = Enum->GetName();
	EnumName.RemoveFromStart(EnumPrefix, ESearchCase::CaseSensitive);
	
	return FString::Printf(
		EnumValueFormat,
		*EnumName,
		*NameToPep8(Enum->GetNameStringByValue((int64)Value)).ToUpper()
	);
}

#if WITH_EDITOR
void RigVMPythonUtils::Print(const FString& BlueprintTitle, const FString& InMessage)
{
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");

	if (!MessageLogModule.IsRegisteredLogListing("ControlRigPythonLog"))
	{
		FMessageLogInitializationOptions InitOptions;
		InitOptions.bShowFilters = true;
		InitOptions.bShowPages = true;
		InitOptions.bAllowClear = true;
		InitOptions.bScrollToBottom = true;
		MessageLogModule.RegisterLogListing("ControlRigPythonLog", LOCTEXT("ControlRigPythonLog", "Control Rig Python Log"), InitOptions);
	}
	TSharedRef<IMessageLogListing> PythonLog = MessageLogModule.GetLogListing( TEXT("ControlRigPythonLog") );
	PythonLog->SetCurrentPage(FText::FromString(BlueprintTitle));

	TSharedRef<FTokenizedMessage> Token = FTokenizedMessage::Create(EMessageSeverity::Info, FText::FromString(InMessage));
	PythonLog->AddMessage(Token, false);
}

void RigVMPythonUtils::PrintPythonContext(const FString& InBlueprintPath)
{
	FString BlueprintName = InBlueprintPath;
	int32 DotIndex = BlueprintName.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (DotIndex != INDEX_NONE)
	{
		BlueprintName = BlueprintName.Right(BlueprintName.Len() - DotIndex - 1);
	}

	static constexpr TCHAR LoadObjectFormat[] = TEXT("blueprint = unreal.load_object(name = '%s', outer = None)");
	static constexpr TCHAR DefineFunctionLibraryFormat[] = TEXT("library = blueprint.get_local_function_library()");
	static constexpr TCHAR DefineLibraryControllerFormat[] = TEXT("library_controller = blueprint.get_controller(library)");
	static constexpr TCHAR DefineHierarchyFormat[] = TEXT("hierarchy = blueprint.hierarchy");
	static constexpr TCHAR DefineHierarchyControllerFormat[] = TEXT("hierarchy_controller = hierarchy.get_controller()");
		
	TArray<FString> PyCommands = {
		TEXT("import unreal"),
		FString::Printf(LoadObjectFormat, *InBlueprintPath),
		DefineFunctionLibraryFormat,
		DefineLibraryControllerFormat,
		DefineHierarchyFormat,
		DefineHierarchyControllerFormat
	};

	for (FString& Command : PyCommands)
	{
		RigVMPythonUtils::Print(BlueprintName, Command);
	}
}

#undef LOCTEXT_NAMESPACE

#endif
