// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintNamespaceHelper.h"
#include "Engine/Blueprint.h"
#include "BlueprintEditorSettings.h"
#include "Settings/EditorProjectSettings.h"

FBlueprintNamespaceHelper::FBlueprintNamespaceHelper(const UBlueprint* InBlueprint)
{
	// Default namespace paths implicitly imported by every Blueprint.
	AddNamespaces(GetDefault<UBlueprintEditorSettings>()->NamespacesToAlwaysInclude);
	AddNamespaces(GetDefault<UBlueprintEditorProjectSettings>()->NamespacesToAlwaysInclude);

	if (InBlueprint)
	{
		AddNamespace(InBlueprint->BlueprintNamespace);
		AddNamespaces(InBlueprint->ImportedNamespaces);

		// @todo - Add parent class namespaces as well?
	}
}

bool FBlueprintNamespaceHelper::IsIncludedInNamespaceList(const FString& TestNamespace) const
{
	// Empty namespace == global namespace
	if (TestNamespace.IsEmpty())
	{
		return true;
	}

	// Check recursively to see if X.Y.Z is present, and if not X.Y (which contains X.Y.Z), and so on until we run out of path segments
	if (FullyQualifiedListOfNamespaces.Contains(TestNamespace))
	{
		return true;
	}
	else
	{
		int32 RightmostDotIndex;
		if (TestNamespace.FindLastChar(TEXT('.'), /*out*/ RightmostDotIndex))
		{
			if (RightmostDotIndex > 0)
			{
				return IsIncludedInNamespaceList(TestNamespace.Left(RightmostDotIndex));
			}
		}
	}

	return false;
}

static TAutoConsoleVariable<bool> CVarBPEnableNamespaceImportScoping(
	TEXT("BP.EnableNamespaceImportScoping"),
	false,
	TEXT("Enable Blueprint namespace import scoping features (experimental)."));

bool FBlueprintNamespaceHelper::IsNamespaceImportScopingEnabled()
{
	// @todo - Replace with editor/project setting at some point?
	return CVarBPEnableNamespaceImportScoping.GetValueOnAnyThread();
}

static TAutoConsoleVariable<bool> CVarBPEnableNamespaceImportEditorUX(
	TEXT("BP.EnableNamespaceImportEditorUX"),
	false,
	TEXT("Enables the namespace importing UX in the Blueprint editor (experimental)."));

bool FBlueprintNamespaceHelper::IsNamespaceImportEditorUXEnabled()
{
	if (!IsNamespaceImportScopingEnabled())
	{
		return false;
	}

	// @todo - Replace with editor/project setting at some point?
	return CVarBPEnableNamespaceImportEditorUX.GetValueOnAnyThread();
}