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
	return FString::Printf(TEXT("unreal.Transform(location=[%f,%f,%f],rotation=[%f,%f,%f],scale=[%f,%f,%f])"),
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
	return FString::Printf(TEXT("unreal.Vector2D(%f, %f)"),
	                       Vector.X,
	                       Vector.Y);
}

FString RigVMPythonUtils::LinearColorToPythonString(const FLinearColor& Color)
{
	return FString::Printf(TEXT("unreal.LinearColor(%f, %f, %f, %f)"),
	                       Color.R, Color.G, Color.B, Color.A);
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
		
	TArray<FString> PyCommands = {
		TEXT("import unreal"),
		FString::Printf(TEXT("blueprint = unreal.load_object(name = '%s', outer = None)"), *InBlueprintPath),
		TEXT("library = blueprint.get_local_function_library()"),
		TEXT("library_controller = blueprint.get_controller(library)"),
		TEXT("hierarchy = blueprint.hierarchy"),
		TEXT("hierarchy_controller = hierarchy.get_controller()")};

	for (FString& Command : PyCommands)
	{
		RigVMPythonUtils::Print(BlueprintName, Command);
	}
}

#undef LOCTEXT_NAMESPACE

#endif
