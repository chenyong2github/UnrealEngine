// Copyright Epic Games, Inc. All Rights Reserved.

#include "DependsNode.h"
#include "AssetRegistryPrivate.h"

void FDependsNode::PrintNode() const
{
	UE_LOG(LogAssetRegistry, Log, TEXT("*** Printing DependsNode: %s ***"), *Identifier.ToString());
	UE_LOG(LogAssetRegistry, Log, TEXT("*** Dependencies:"));
	PrintDependencies();
	UE_LOG(LogAssetRegistry, Log, TEXT("*** Referencers:"));
	PrintReferencers();
}

void FDependsNode::PrintDependencies() const
{
	TSet<const FDependsNode*> VisitedNodes;

	PrintDependenciesRecursive(TEXT(""), VisitedNodes);
}

void FDependsNode::PrintReferencers() const
{
	TSet<const FDependsNode*> VisitedNodes;

	PrintReferencersRecursive(TEXT(""), VisitedNodes);
}

void FDependsNode::GetDependencies(TArray<FDependsNode*>& OutDependencies, EAssetRegistryDependencyType::Type InDependencyType) const
{
	IterateOverDependencies([&OutDependencies](FDependsNode* InDependency, EAssetRegistryDependencyType::Type /*InDependencyType*/)
	{
		OutDependencies.Add(InDependency);
	}, 
	InDependencyType);
}

void FDependsNode::GetDependencies(TArray<FAssetIdentifier>& OutDependencies, EAssetRegistryDependencyType::Type InDependencyType) const
{
	IterateOverDependencies([&OutDependencies](const FDependsNode* InDependency, EAssetRegistryDependencyType::Type /*InDependencyType*/)
	{
		OutDependencies.Add(InDependency->GetIdentifier());
	},
	InDependencyType);
}

void FDependsNode::GetReferencers(TArray<FDependsNode*>& OutReferencers, EAssetRegistryDependencyType::Type InDependencyType) const
{
	for (FDependsNode* Referencer : Referencers)
	{
		bool bShouldAdd = false;
		// If type specified, filter
		if (InDependencyType != EAssetRegistryDependencyType::All)
		{
			// If type is specified, filter. We don't use the iterate wrapper for performance
			if (InDependencyType & EAssetRegistryDependencyType::Hard)
			{
				bShouldAdd = Referencer->HardDependencies.Contains(const_cast<FDependsNode*>(this));
			}

			if (InDependencyType & EAssetRegistryDependencyType::Soft && !bShouldAdd)
			{
				bShouldAdd = Referencer->SoftDependencies.Contains(const_cast<FDependsNode*>(this));
			}

			if (InDependencyType & EAssetRegistryDependencyType::HardManage && !bShouldAdd)
			{
				bShouldAdd = Referencer->HardManageDependencies.Contains(const_cast<FDependsNode*>(this));
			}

			if (InDependencyType & EAssetRegistryDependencyType::SoftManage && !bShouldAdd)
			{
				bShouldAdd = Referencer->SoftManageDependencies.Contains(const_cast<FDependsNode*>(this));
			}

			if (InDependencyType & EAssetRegistryDependencyType::SearchableName && !bShouldAdd)
			{
				bShouldAdd = Referencer->NameDependencies.Contains(const_cast<FDependsNode*>(this));
			}
		}
		else
		{
			bShouldAdd = true;
		}

		if (bShouldAdd)
		{
			OutReferencers.Add(Referencer);
		}
	}
}

void FDependsNode::AddDependency(FDependsNode* InDependency, EAssetRegistryDependencyType::Type InDependencyType, bool bGuaranteedUnique)
{
	IterateOverDependencyLists([InDependency,&bGuaranteedUnique](FDependsNodeList& InList, EAssetRegistryDependencyType::Type)
	{
#if USE_DEPENDS_NODE_LIST_SETS
		InList.Add(InDependency);
#else
		if (bGuaranteedUnique)
		{
			InList.Add(InDependency);
		}
		else
		{
			InList.AddUnique(InDependency);
		}
#endif
	}, InDependencyType);
}

void FDependsNode::AddReferencer(FDependsNode* InReferencer, bool bGuaranteedUnique)
{
#if USE_DEPENDS_NODE_LIST_SETS
	Referencers.Add(InReferencer);
#else
	if (bGuaranteedUnique)
	{
		Referencers.Add(InReferencer);
	}
	else
	{
		Referencers.AddUnique(InReferencer);
	}
#endif
}

void FDependsNode::RemoveDependency(FDependsNode* InDependency)
{
	IterateOverDependencyLists([InDependency](FDependsNodeList& InList, EAssetRegistryDependencyType::Type)
	{
#if USE_DEPENDS_NODE_LIST_SETS
		InList.Remove(InDependency);
#else
		InList.RemoveSingleSwap(InDependency, false);
#endif
	});
}

void FDependsNode::RemoveReferencer(FDependsNode* InReferencer)
{
#if USE_DEPENDS_NODE_LIST_SETS
	Referencers.Remove(InReferencer);
#else
	Referencers.RemoveSingleSwap(InReferencer, false);
#endif
}

void FDependsNode::ClearDependencies()
{
	IterateOverDependencyLists([](FDependsNodeList& InSet, EAssetRegistryDependencyType::Type)
	{
		InSet.Empty();
	});
}

void FDependsNode::RemoveManageReferencesToNode()
{
	EAssetRegistryDependencyType::Type InDependencyType = EAssetRegistryDependencyType::Manage;
#if USE_DEPENDS_NODE_LIST_SETS
	// Iterate referencers set, possibly removing 
	for (auto RefsIt = Referencers.CreateIterator(); RefsIt; ++RefsIt)
	{
		bool bStillExists = false;
		(*RefsIt)->IterateOverDependencyLists([InDependencyType, this, &bStillExists](FDependsNodeList& InList, EAssetRegistryDependencyType::Type CurrentType)
		{
			if (InList.Contains(this))
			{
				if (CurrentType & InDependencyType)
				{
					InList.Remove(this);
				}
				else
				{
					// Reference of another type still exists
					bStillExists = true;
				}
			}
		}, EAssetRegistryDependencyType::All);

		if (!bStillExists)
		{
			RefsIt.RemoveCurrent();
		}
	}
#else
	// Iterate referencers array, possibly removing 
	for (int32 i = Referencers.Num() - 1; i >= 0; i--)
	{
		bool bStillExists = false;
		Referencers[i]->IterateOverDependencyLists([InDependencyType, this, &bStillExists](FDependsNodeList& InList, EAssetRegistryDependencyType::Type CurrentType)
		{
			int32 FoundIndex = InList.Find(this);

			if (FoundIndex != INDEX_NONE)
			{
				if (CurrentType & InDependencyType)
				{
					InList.RemoveAt(FoundIndex);
				}
				else
				{
					// Reference of another type still exists
					bStillExists = true;
				}
			}
		}, EAssetRegistryDependencyType::All);

		if (!bStillExists)
		{
			Referencers.RemoveAt(i);
		}
	}
#endif // USE_DEPENDS_NODE_LIST_SETS
}

void FDependsNode::PrintDependenciesRecursive(const FString& Indent, TSet<const FDependsNode*>& VisitedNodes) const
{
	if ( this == NULL )
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("%sNULL"), *Indent);
	}
	else if ( VisitedNodes.Contains(this) )
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("%s[CircularReferenceTo]%s"), *Indent, *Identifier.ToString());
	}
	else
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("%s%s"), *Indent, *Identifier.ToString());
		VisitedNodes.Add(this);

		IterateOverDependencies([&Indent, &VisitedNodes](FDependsNode* InDependency, EAssetRegistryDependencyType::Type)
		{
			InDependency->PrintDependenciesRecursive(Indent + TEXT("  "), VisitedNodes);
		},
		EAssetRegistryDependencyType::All
		);
	}
}

void FDependsNode::PrintReferencersRecursive(const FString& Indent, TSet<const FDependsNode*>& VisitedNodes) const
{
	if ( this == NULL )
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("%sNULL"), *Indent);
	}
	else if ( VisitedNodes.Contains(this) )
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("%s[CircularReferenceTo]%s"), *Indent, *Identifier.ToString());
	}
	else
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("%s%s"), *Indent, *Identifier.ToString());
		VisitedNodes.Add(this);

		for (FDependsNode* Node : Referencers)
		{
			Node->PrintReferencersRecursive(Indent + TEXT("  "), VisitedNodes);
		}
	}
}

int32 FDependsNode::GetConnectionCount() const
{
	return HardDependencies.Num() + SoftDependencies.Num() + NameDependencies.Num() + SoftManageDependencies.Num() + HardManageDependencies.Num() + Referencers.Num();
}