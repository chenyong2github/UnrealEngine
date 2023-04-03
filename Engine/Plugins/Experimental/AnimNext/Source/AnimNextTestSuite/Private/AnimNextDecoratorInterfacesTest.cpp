// Copyright Epic Games, Inc. All Rights Reserved.


#include "AnimNextRuntimeTest.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DecoratorBase/Decorator.h"
#include "DecoratorBase/DecoratorRegistry.h"
#include "DecoratorBase/ExecutionContext.h"
#include "DecoratorBase/IDecoratorInterface.h"
#include "DecoratorBase/NodeInstance.h"
#include "DecoratorBase/NodeTemplateRegistry.h"
#include "DecoratorInterfaces/IEvaluate.h"
#include "DecoratorInterfaces/IHierarchy.h"
#include "DecoratorInterfaces/IUpdate.h"

//****************************************************************************
// AnimNext Runtime DecoratorInterfaces Tests
//****************************************************************************

namespace UE::AnimNext
{
	namespace Private
	{
		static TArray<FDecoratorUID>* UpdatedDecorators = nullptr;
		static TArray<FDecoratorUID>* EvaluatedDecorators = nullptr;
	}

	struct FDecoratorWithNoChildren : FDecorator, IHierarchy, IUpdate, IEvaluate
	{
		DECLARE_ANIM_DECORATOR(FDecoratorWithNoChildren, 0xe2400d2a, FDecorator)

		// FDecorator impl
		virtual EDecoratorMode GetMode() const override { return EDecoratorMode::Base; }

		// IHierarchy impl
		virtual void GetChildren(FExecutionContext& Context, const TDecoratorBinding<IHierarchy>& Binding, FChildrenArray& Children) const override
		{
			IHierarchy::GetChildren(Context, Binding, Children);
		}

		// IUpdate impl
		virtual void PreUpdate(FExecutionContext& Context, const TDecoratorBinding<IUpdate>& Binding) const override
		{
			if (Private::UpdatedDecorators != nullptr)
			{
				Private::UpdatedDecorators->Add(FDecoratorWithNoChildren::DecoratorUID);
			}

			IUpdate::PreUpdate(Context, Binding);
		}

		virtual void PostUpdate(FExecutionContext& Context, const TDecoratorBinding<IUpdate>& Binding) const override
		{
			if (Private::UpdatedDecorators != nullptr)
			{
				Private::UpdatedDecorators->Add(FDecoratorWithNoChildren::DecoratorUID);
			}

			IUpdate::PostUpdate(Context, Binding);
		}

		// IEvaluate impl
		virtual void PreEvaluate(FExecutionContext& Context, const TDecoratorBinding<IEvaluate>& Binding) const override
		{
			if (Private::EvaluatedDecorators != nullptr)
			{
				Private::EvaluatedDecorators->Add(FDecoratorWithNoChildren::DecoratorUID);
			}

			IEvaluate::PreEvaluate(Context, Binding);
		}

		virtual void PostEvaluate(FExecutionContext& Context, const TDecoratorBinding<IEvaluate>& Binding) const override
		{
			if (Private::EvaluatedDecorators != nullptr)
			{
				Private::EvaluatedDecorators->Add(FDecoratorWithNoChildren::DecoratorUID);
			}

			IEvaluate::PostEvaluate(Context, Binding);
		}
	};

	DEFINE_ANIM_DECORATOR_BEGIN(FDecoratorWithNoChildren)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IHierarchy)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IUpdate)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IEvaluate)
	DEFINE_ANIM_DECORATOR_END(FDecoratorWithNoChildren)

	// This decorator does not update or evaluate
	struct FDecoratorWithOneChild : FDecorator, IHierarchy
	{
		DECLARE_ANIM_DECORATOR(FDecoratorWithOneChild, 0xba24d224, FDecorator)

		struct FSharedData : FDecorator::FSharedData
		{
			FDecoratorHandle Child;

			FSharedData(FDecoratorHandle Child_)
				: Child(Child_)
			{}
		};

		struct FInstanceData : FDecorator::FInstanceData
		{
			FDecoratorPtr Child;

			void Construct(FExecutionContext& Context, FWeakDecoratorPtr DecoratorPtr, const FSharedData& SharedData)
			{
				Child = Context.AllocateNodeInstance(DecoratorPtr, SharedData.Child);
			}
		};

		// FDecorator impl
		virtual EDecoratorMode GetMode() const override { return EDecoratorMode::Base; }

		// IHierarchy impl
		virtual void GetChildren(FExecutionContext& Context, const TDecoratorBinding<IHierarchy>& Binding, FChildrenArray& Children) const override
		{
			const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

			Children.Add(InstanceData->Child);

			IHierarchy::GetChildren(Context, Binding, Children);
		}
	};

	DEFINE_ANIM_DECORATOR_BEGIN(FDecoratorWithOneChild)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IHierarchy)
	DEFINE_ANIM_DECORATOR_END(FDecoratorWithOneChild)

	struct FDecoratorWithChildren : FDecorator, IHierarchy, IUpdate, IEvaluate
	{
		DECLARE_ANIM_DECORATOR(FDecoratorWithChildren, 0xa3ad93b9, FDecorator)

		struct FSharedData : FDecorator::FSharedData
		{
			FDecoratorHandle Children[2];

			FSharedData(FDecoratorHandle Child0, FDecoratorHandle Child1)
				: Children{ Child0, Child1 }
			{}
		};

		struct FInstanceData : FDecorator::FInstanceData
		{
			FDecoratorPtr Children[2];

			void Construct(FExecutionContext& Context, FWeakDecoratorPtr DecoratorPtr, const FSharedData& SharedData)
			{
				Children[0] = Context.AllocateNodeInstance(DecoratorPtr, SharedData.Children[0]);
				Children[1] = Context.AllocateNodeInstance(DecoratorPtr, SharedData.Children[1]);
			}
		};

		// FDecorator impl
		virtual EDecoratorMode GetMode() const override { return EDecoratorMode::Base; }

		// IHierarchy impl
		virtual void GetChildren(FExecutionContext& Context, const TDecoratorBinding<IHierarchy>& Binding, FChildrenArray& Children) const override
		{
			const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

			Children.Add(InstanceData->Children[0]);
			Children.Add(InstanceData->Children[1]);

			IHierarchy::GetChildren(Context, Binding, Children);
		}

		// IUpdate impl
		virtual void PreUpdate(FExecutionContext& Context, const TDecoratorBinding<IUpdate>& Binding) const override
		{
			if (Private::UpdatedDecorators != nullptr)
			{
				Private::UpdatedDecorators->Add(FDecoratorWithChildren::DecoratorUID);
			}

			IUpdate::PreUpdate(Context, Binding);
		}

		virtual void PostUpdate(FExecutionContext& Context, const TDecoratorBinding<IUpdate>& Binding) const override
		{
			if (Private::UpdatedDecorators != nullptr)
			{
				Private::UpdatedDecorators->Add(FDecoratorWithChildren::DecoratorUID);
			}

			IUpdate::PostUpdate(Context, Binding);
		}

		// IEvaluate impl
		virtual void PreEvaluate(FExecutionContext& Context, const TDecoratorBinding<IEvaluate>& Binding) const override
		{
			if (Private::EvaluatedDecorators != nullptr)
			{
				Private::EvaluatedDecorators->Add(FDecoratorWithChildren::DecoratorUID);
			}

			IEvaluate::PreEvaluate(Context, Binding);
		}

		virtual void PostEvaluate(FExecutionContext& Context, const TDecoratorBinding<IEvaluate>& Binding) const override
		{
			if (Private::EvaluatedDecorators != nullptr)
			{
				Private::EvaluatedDecorators->Add(FDecoratorWithChildren::DecoratorUID);
			}

			IEvaluate::PostEvaluate(Context, Binding);
		}
	};

	DEFINE_ANIM_DECORATOR_BEGIN(FDecoratorWithChildren)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IHierarchy)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IUpdate)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IEvaluate)
	DEFINE_ANIM_DECORATOR_END(FDecoratorWithChildren)
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextRuntimeTest_IHierarchy, "Animation.AnimNext.Runtime.IHierarchy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextRuntimeTest_IHierarchy::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorWithNoChildren)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorWithOneChild)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorWithChildren)

	FNodeTemplateRegistry& Registry = FNodeTemplateRegistry::Get();

	// We create a few node templates
	// Template A has a single decorator with no children
	TArray<FDecoratorUID> NodeTemplateDecoratorListA;
	NodeTemplateDecoratorListA.Add(FDecoratorWithNoChildren::DecoratorUID);

	// Template B has a single decorator with one child
	TArray<FDecoratorUID> NodeTemplateDecoratorListB;
	NodeTemplateDecoratorListB.Add(FDecoratorWithOneChild::DecoratorUID);

	// Template C has two decorators, each with one child
	TArray<FDecoratorUID> NodeTemplateDecoratorListC;
	NodeTemplateDecoratorListC.Add(FDecoratorWithOneChild::DecoratorUID);
	NodeTemplateDecoratorListC.Add(FDecoratorWithOneChild::DecoratorUID);

	// Template D has a single decorator with children
	TArray<FDecoratorUID> NodeTemplateDecoratorListD;
	NodeTemplateDecoratorListD.Add(FDecoratorWithChildren::DecoratorUID);

	// Populate our node template registry
	TArray<uint8> NodeTemplateBufferA, NodeTemplateBufferB, NodeTemplateBufferC, NodeTemplateBufferD;
	const FNodeTemplate* NodeTemplateA = BuildNodeTemplate(NodeTemplateDecoratorListA, NodeTemplateBufferA);
	const FNodeTemplate* NodeTemplateB = BuildNodeTemplate(NodeTemplateDecoratorListB, NodeTemplateBufferB);
	const FNodeTemplate* NodeTemplateC = BuildNodeTemplate(NodeTemplateDecoratorListC, NodeTemplateBufferC);
	const FNodeTemplate* NodeTemplateD = BuildNodeTemplate(NodeTemplateDecoratorListD, NodeTemplateBufferD);

	// Build our graph, it as follow (each node template has a single node instance):
	// NodeA has no children
	// NodeB has one child: NodeA
	// NodeC has two children: NodeA and NodeB (but both decorators are base, only NodeB will be referenced)
	// NodeD has two children: NodeA and NodeC

	uint16 NodeUID = 0;
	TArray<uint8> GraphSharedDataBuffer;

	const FNodeHandle NodeA = AppendNodeToGraph(*NodeTemplateA, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplateA, NodeA, GraphSharedDataBuffer, 0, FDecoratorWithNoChildren::FSharedData());

	const FNodeHandle NodeB = AppendNodeToGraph(*NodeTemplateB, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplateB, NodeB, GraphSharedDataBuffer, 0, FDecoratorWithOneChild::FSharedData(FDecoratorHandle(NodeA)));

	const FNodeHandle NodeC = AppendNodeToGraph(*NodeTemplateC, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplateC, NodeC, GraphSharedDataBuffer, 0, FDecoratorWithOneChild::FSharedData(FDecoratorHandle(NodeA)));
	InitNodeDecorator(*NodeTemplateC, NodeC, GraphSharedDataBuffer, 1, FDecoratorWithOneChild::FSharedData(FDecoratorHandle(NodeB)));

	const FNodeHandle NodeD = AppendNodeToGraph(*NodeTemplateD, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplateD, NodeD, GraphSharedDataBuffer, 0, FDecoratorWithChildren::FSharedData(FDecoratorHandle(NodeA), FDecoratorHandle(NodeC, 1)));

	FExecutionContext Context(GraphSharedDataBuffer);

	{
		FMemMark Mark(FMemStack::Get());

		FWeakDecoratorPtr NullPtr;				// Empty, no parent
		FDecoratorHandle RootHandle(NodeD);		// Point to NodeD, first base decorator

		FDecoratorPtr NodeDPtr = Context.AllocateNodeInstance(NullPtr, RootHandle);
		AddErrorIfFalse(NodeDPtr.IsValid(), "FAnimationAnimNextRuntimeTest_IHierarchy -> Failed to allocate root node instance");

		TDecoratorBinding<IHierarchy> HierarchyBindingNodeD;
		AddErrorIfFalse(Context.GetInterface(NodeDPtr, HierarchyBindingNodeD), "FAnimationAnimNextRuntimeTest_IHierarchy -> IHierarchy not found");

		FChildrenArray ChildrenNodeD;
		HierarchyBindingNodeD.GetChildren(Context, ChildrenNodeD);

		AddErrorIfFalse(ChildrenNodeD.Num() == 2, "FAnimationAnimNextRuntimeTest_IHierarchy -> Expected 2 children");
		AddErrorIfFalse(ChildrenNodeD[0].IsValid() && ChildrenNodeD[0].GetNodeInstance()->GetNodeHandle() == NodeA, "FAnimationAnimNextRuntimeTest_IHierarchy -> Expected child: NodeA");
		AddErrorIfFalse(ChildrenNodeD[1].IsValid() && ChildrenNodeD[1].GetNodeInstance()->GetNodeHandle() == NodeC, "FAnimationAnimNextRuntimeTest_IHierarchy -> Expected child: NodeC");

		{
			TDecoratorBinding<IHierarchy> HierarchyBindingNodeA;
			AddErrorIfFalse(Context.GetInterface(ChildrenNodeD[0], HierarchyBindingNodeA), "FAnimationAnimNextRuntimeTest_IHierarchy -> IHierarchy not found");

			FChildrenArray ChildrenNodeA;
			HierarchyBindingNodeA.GetChildren(Context, ChildrenNodeA);

			AddErrorIfFalse(ChildrenNodeA.Num() == 0, "FAnimationAnimNextRuntimeTest_IHierarchy -> Expected 0 children");
		}

		{
			TDecoratorBinding<IHierarchy> HierarchyBindingNodeC;
			AddErrorIfFalse(Context.GetInterface(ChildrenNodeD[1], HierarchyBindingNodeC), "FAnimationAnimNextRuntimeTest_IHierarchy -> IHierarchy not found");

			FChildrenArray ChildrenNodeC;
			HierarchyBindingNodeC.GetChildren(Context, ChildrenNodeC);

			AddErrorIfFalse(ChildrenNodeC.Num() == 1, "FAnimationAnimNextRuntimeTest_IHierarchy -> Expected 2 children");
			AddErrorIfFalse(ChildrenNodeC[0].IsValid() && ChildrenNodeC[0].GetNodeInstance()->GetNodeHandle() == NodeB, "FAnimationAnimNextRuntimeTest_IHierarchy -> Expected child: NodeB");

			{
				TDecoratorBinding<IHierarchy> HierarchyBindingNodeB;
				AddErrorIfFalse(Context.GetInterface(ChildrenNodeC[0], HierarchyBindingNodeB), "FAnimationAnimNextRuntimeTest_IHierarchy -> IHierarchy not found");

				FChildrenArray ChildrenNodeB;
				HierarchyBindingNodeB.GetChildren(Context, ChildrenNodeB);

				AddErrorIfFalse(ChildrenNodeB.Num() == 1, "FAnimationAnimNextRuntimeTest_IHierarchy -> Expected 1 child");
				AddErrorIfFalse(ChildrenNodeB[0].IsValid() && ChildrenNodeB[0].GetNodeInstance()->GetNodeHandle() == NodeA, "FAnimationAnimNextRuntimeTest_IHierarchy -> Expected child: NodeA");

				{
					TDecoratorBinding<IHierarchy> HierarchyBindingNodeA;
					AddErrorIfFalse(Context.GetInterface(ChildrenNodeB[0], HierarchyBindingNodeA), "FAnimationAnimNextRuntimeTest_IHierarchy -> IHierarchy not found");

					FChildrenArray ChildrenNodeA;
					HierarchyBindingNodeA.GetChildren(Context, ChildrenNodeA);

					AddErrorIfFalse(ChildrenNodeA.Num() == 0, "FAnimationAnimNextRuntimeTest_IHierarchy -> Expected 0 children");
				}
			}
		}
	}

	Registry.Unregister(NodeTemplateA);
	Registry.Unregister(NodeTemplateB);
	Registry.Unregister(NodeTemplateC);
	Registry.Unregister(NodeTemplateD);

	AddErrorIfFalse(Registry.GetNum() == 0, "FAnimationAnimNextRuntimeTest_IHierarchy -> Registry should contain 0 templates");

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextRuntimeTest_IUpdate, "Animation.AnimNext.Runtime.IUpdate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextRuntimeTest_IUpdate::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorWithNoChildren)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorWithOneChild)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorWithChildren)

	FNodeTemplateRegistry& Registry = FNodeTemplateRegistry::Get();

	// We create a few node templates
	// Template A has a single decorator with no children
	TArray<FDecoratorUID> NodeTemplateDecoratorListA;
	NodeTemplateDecoratorListA.Add(FDecoratorWithNoChildren::DecoratorUID);

	// Template B has a single decorator with one child, it doesn't update
	TArray<FDecoratorUID> NodeTemplateDecoratorListB;
	NodeTemplateDecoratorListB.Add(FDecoratorWithOneChild::DecoratorUID);

	// Template C has a single decorator with children
	TArray<FDecoratorUID> NodeTemplateDecoratorListC;
	NodeTemplateDecoratorListC.Add(FDecoratorWithChildren::DecoratorUID);

	// Populate our node template registry
	TArray<uint8> NodeTemplateBufferA, NodeTemplateBufferB, NodeTemplateBufferC;
	const FNodeTemplate* NodeTemplateA = BuildNodeTemplate(NodeTemplateDecoratorListA, NodeTemplateBufferA);
	const FNodeTemplate* NodeTemplateB = BuildNodeTemplate(NodeTemplateDecoratorListB, NodeTemplateBufferB);
	const FNodeTemplate* NodeTemplateC = BuildNodeTemplate(NodeTemplateDecoratorListC, NodeTemplateBufferC);

	// Build our graph, it as follow (each node template has a single node instance):
	// NodeA has no children
	// NodeB has one child: NodeA (it doesn't update)
	// NodeD has two children: NodeA and NodeB

	uint16 NodeUID = 0;
	TArray<uint8> GraphSharedDataBuffer;

	const FNodeHandle NodeA = AppendNodeToGraph(*NodeTemplateA, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplateA, NodeA, GraphSharedDataBuffer, 0, FDecoratorWithNoChildren::FSharedData());

	const FNodeHandle NodeB = AppendNodeToGraph(*NodeTemplateB, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplateB, NodeB, GraphSharedDataBuffer, 0, FDecoratorWithOneChild::FSharedData(FDecoratorHandle(NodeA)));

	const FNodeHandle NodeC = AppendNodeToGraph(*NodeTemplateC, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplateC, NodeC, GraphSharedDataBuffer, 0, FDecoratorWithChildren::FSharedData(FDecoratorHandle(NodeA), FDecoratorHandle(NodeB)));

	FExecutionContext Context(GraphSharedDataBuffer);

	{
		TArray<FDecoratorUID> UpdatedDecorators;

		Private::UpdatedDecorators = &UpdatedDecorators;

		FWeakDecoratorPtr NullPtr;				// Empty, no parent
		FDecoratorHandle RootHandle(NodeC);		// Point to NodeC, first base decorator

		FDecoratorPtr NodeCPtr = Context.AllocateNodeInstance(NullPtr, RootHandle);
		AddErrorIfFalse(NodeCPtr.IsValid(), "FAnimationAnimNextRuntimeTest_IUpdate -> Failed to allocate root node instance");

		// Call pre/post update on our graph
		UpdateGraph(Context, NodeCPtr);

		AddErrorIfFalse(UpdatedDecorators.Num() == 6, "FAnimationAnimNextRuntimeTest_IUpdate -> Expected 6 nodes to have been visited during the update traversal");
		AddErrorIfFalse(UpdatedDecorators[0] == FDecoratorWithChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IUpdate -> Unexpected update order");		// NodeC
		AddErrorIfFalse(UpdatedDecorators[1] == FDecoratorWithNoChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IUpdate -> Unexpected update order");		// NodeA
		AddErrorIfFalse(UpdatedDecorators[2] == FDecoratorWithNoChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IUpdate -> Unexpected update order");		// NodeA
		AddErrorIfFalse(UpdatedDecorators[3] == FDecoratorWithNoChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IUpdate -> Unexpected update order");		// NodeB -> NodeA
		AddErrorIfFalse(UpdatedDecorators[4] == FDecoratorWithNoChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IUpdate -> Unexpected update order");		// NodeB -> NodeA
		AddErrorIfFalse(UpdatedDecorators[5] == FDecoratorWithChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IUpdate -> Unexpected update order");		// NodeC

		Private::UpdatedDecorators = nullptr;
	}

	Registry.Unregister(NodeTemplateA);
	Registry.Unregister(NodeTemplateB);
	Registry.Unregister(NodeTemplateC);

	AddErrorIfFalse(Registry.GetNum() == 0, "FAnimationAnimNextRuntimeTest_IUpdate -> Registry should contain 0 templates");

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextRuntimeTest_IEvaluate, "Animation.AnimNext.Runtime.IEvaluate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextRuntimeTest_IEvaluate::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorWithNoChildren)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorWithOneChild)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorWithChildren)

	FNodeTemplateRegistry& Registry = FNodeTemplateRegistry::Get();

	// We create a few node templates
	// Template A has a single decorator with no children
	TArray<FDecoratorUID> NodeTemplateDecoratorListA;
	NodeTemplateDecoratorListA.Add(FDecoratorWithNoChildren::DecoratorUID);

	// Template B has a single decorator with one child, it doesn't evaluate
	TArray<FDecoratorUID> NodeTemplateDecoratorListB;
	NodeTemplateDecoratorListB.Add(FDecoratorWithOneChild::DecoratorUID);

	// Template C has a single decorator with children
	TArray<FDecoratorUID> NodeTemplateDecoratorListC;
	NodeTemplateDecoratorListC.Add(FDecoratorWithChildren::DecoratorUID);

	// Populate our node template registry
	TArray<uint8> NodeTemplateBufferA, NodeTemplateBufferB, NodeTemplateBufferC;
	const FNodeTemplate* NodeTemplateA = BuildNodeTemplate(NodeTemplateDecoratorListA, NodeTemplateBufferA);
	const FNodeTemplate* NodeTemplateB = BuildNodeTemplate(NodeTemplateDecoratorListB, NodeTemplateBufferB);
	const FNodeTemplate* NodeTemplateC = BuildNodeTemplate(NodeTemplateDecoratorListC, NodeTemplateBufferC);

	// Build our graph, it as follow (each node template has a single node instance):
	// NodeA has no children
	// NodeB has one child: NodeA (it doesn't evaluate)
	// NodeD has two children: NodeA and NodeB

	uint16 NodeUID = 0;
	TArray<uint8> GraphSharedDataBuffer;

	const FNodeHandle NodeA = AppendNodeToGraph(*NodeTemplateA, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplateA, NodeA, GraphSharedDataBuffer, 0, FDecoratorWithNoChildren::FSharedData());

	const FNodeHandle NodeB = AppendNodeToGraph(*NodeTemplateB, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplateB, NodeB, GraphSharedDataBuffer, 0, FDecoratorWithOneChild::FSharedData(FDecoratorHandle(NodeA)));

	const FNodeHandle NodeC = AppendNodeToGraph(*NodeTemplateC, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplateC, NodeC, GraphSharedDataBuffer, 0, FDecoratorWithChildren::FSharedData(FDecoratorHandle(NodeA), FDecoratorHandle(NodeB)));

	FExecutionContext Context(GraphSharedDataBuffer);

	{
		TArray<FDecoratorUID> EvaluatedDecorators;

		Private::EvaluatedDecorators = &EvaluatedDecorators;

		FWeakDecoratorPtr NullPtr;				// Empty, no parent
		FDecoratorHandle RootHandle(NodeC);		// Point to NodeC, first base decorator

		FDecoratorPtr NodeCPtr = Context.AllocateNodeInstance(NullPtr, RootHandle);
		AddErrorIfFalse(NodeCPtr.IsValid(), "FAnimationAnimNextRuntimeTest_IEvaluate -> Failed to allocate root node instance");

		// Call pre/post evaluate on our graph
		const EEvaluationFlags EvaluationFlags = EEvaluationFlags::All;
		FPoseContainer PoseContainer;
		EvaluateGraph(Context, NodeCPtr, EvaluationFlags, PoseContainer);

		AddErrorIfFalse(EvaluatedDecorators.Num() == 6, "FAnimationAnimNextRuntimeTest_IEvaluate -> Expected 6 nodes to have been visited during the evaluate traversal");
		AddErrorIfFalse(EvaluatedDecorators[0] == FDecoratorWithChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IEvaluate -> Unexpected evaluate order");		// NodeC
		AddErrorIfFalse(EvaluatedDecorators[1] == FDecoratorWithNoChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IEvaluate -> Unexpected evaluate order");			// NodeA
		AddErrorIfFalse(EvaluatedDecorators[2] == FDecoratorWithNoChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IEvaluate -> Unexpected evaluate order");			// NodeA
		AddErrorIfFalse(EvaluatedDecorators[3] == FDecoratorWithNoChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IEvaluate -> Unexpected evaluate order");			// NodeB -> NodeA
		AddErrorIfFalse(EvaluatedDecorators[4] == FDecoratorWithNoChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IEvaluate -> Unexpected evaluate order");			// NodeB -> NodeA
		AddErrorIfFalse(EvaluatedDecorators[5] == FDecoratorWithChildren::DecoratorUID, "FAnimationAnimNextRuntimeTest_IEvaluate -> Unexpected evaluate order");		// NodeC

		Private::EvaluatedDecorators = nullptr;
	}

	Registry.Unregister(NodeTemplateA);
	Registry.Unregister(NodeTemplateB);
	Registry.Unregister(NodeTemplateC);

	AddErrorIfFalse(Registry.GetNum() == 0, "FAnimationAnimNextRuntimeTest_IEvaluate -> Registry should contain 0 templates");

	return true;
}

#endif
