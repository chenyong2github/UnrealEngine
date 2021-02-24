// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshAttributes.h"
#include "StaticMeshAttributes.h"
#include "SkinWeightsAttributesRef.h"

//Add any skeletalmesh specific attributes here

namespace MeshAttribute
{
 	namespace Vertex
 	{
		extern SKELETALMESHDESCRIPTION_API const FName SkinWeights;
 	}
}



class SKELETALMESHDESCRIPTION_API FSkeletalMeshAttributes : public FStaticMeshAttributes
{
public:

	explicit FSkeletalMeshAttributes(FMeshDescription& InMeshDescription)
		: FStaticMeshAttributes(InMeshDescription)
	{}

	// The name of the default skin weight profile.
	static FName DefaultSkinWeightProfileName;
	
	virtual void Register() override;

	/// Register a new skin weight profile with the given name. The attribute name will encode the profile name and
	/// it will be listed in GetSkinWeightProfileNames(). Returns \c true if the profile was successfully registered.
	/// Returns \c false if the attribute was already registered or if IsValidSkinWeightProfileName() returned false.
	bool RegisterSkinWeightAttribute(const FName InProfileName);

	/// Returns the list of all registered skin weight profile names on this mesh.
	TArray<FName> GetSkinWeightProfileNames() const;
	
	/// Returns \c true if the given identifier is a valid profile name. If the name is empty, or matches the default profile,
	/// then the profile name is considered invalid. 
	static bool IsValidSkinWeightProfileName(const FName InProfileName);

	/// Returns the skin weight profile given by its name. NAME_None corresponds to the default profile.
	FSkinWeightsVertexAttributesRef GetVertexSkinWeights(const FName InProfileName = NAME_None)
	{
		return MeshDescription.VertexAttributes().GetAttributesRef<TArrayAttribute<int32>>(CreateSkinWeightAttributeName(InProfileName));
	}
	FSkinWeightsVertexAttributesConstRef GetVertexSkinWeights(const FName InProfileName = NAME_None) const
	{
		return MeshDescription.VertexAttributes().GetAttributesRef<TArrayAttribute<int32>>(CreateSkinWeightAttributeName(InProfileName));
	}

protected:
	friend class FSkeletalMeshConstAttributes;

	/// Construct a name for a skin weight attribute with the given skin weight profile name.
	/// Each mesh description can hold different skin weight profiles, although the default
	/// is always present.
	static FName CreateSkinWeightAttributeName(const FName InProfileName);
};


class FSkeletalMeshConstAttributes : public FStaticMeshConstAttributes
{
public:

	explicit FSkeletalMeshConstAttributes(const FMeshDescription& InMeshDescription)
		: FStaticMeshConstAttributes(InMeshDescription)
	{}

	FSkinWeightsVertexAttributesConstRef GetVertexSkinWeights(const FName InProfileName = NAME_None) const
	{
		return MeshDescription.VertexAttributes().GetAttributesRef<TArrayAttribute<int32>>(FSkeletalMeshAttributes::CreateSkinWeightAttributeName(InProfileName));
	}
};
