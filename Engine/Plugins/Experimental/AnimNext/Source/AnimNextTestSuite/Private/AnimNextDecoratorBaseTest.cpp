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

//****************************************************************************
// AnimNext Runtime DecoratorBase Tests
//****************************************************************************

namespace UE::AnimNext
{
	namespace Private
	{
		static TArray<FDecoratorUID>* ConstructedDecorators = nullptr;
		static TArray<FDecoratorUID>* DestructedDecorators = nullptr;
	}

	struct IInterfaceA : IDecoratorInterface
	{
		DECLARE_ANIM_DECORATOR_INTERFACE(IInterfaceA, 0x34cb8e62)

		virtual void FuncA(FExecutionContext& Context, const TDecoratorBinding<IInterfaceA>& Binding) const;
	};

	template<>
	struct TDecoratorBinding<IInterfaceA> : FDecoratorBinding
	{
		void FuncA(FExecutionContext& Context) const
		{
			GetInterface()->FuncA(Context, *this);
		}

	protected:
		const IInterfaceA* GetInterface() const { return GetInterfaceTyped<IInterfaceA>(); }
	};

	void IInterfaceA::FuncA(FExecutionContext& Context, const TDecoratorBinding<IInterfaceA>& Binding) const
	{
		TDecoratorBinding<IInterfaceA> SuperBinding;
		if (Context.GetInterfaceSuper(Binding, SuperBinding))
		{
			SuperBinding.FuncA(Context);
		}
	}

	//////////////////////////////////////////////////////////////////////////

	struct IInterfaceB : IDecoratorInterface
	{
		DECLARE_ANIM_DECORATOR_INTERFACE(IInterfaceB, 0x33cb8ccf)

		virtual void FuncB(FExecutionContext& Context, const TDecoratorBinding<IInterfaceB>& Binding) const;
	};

	template<>
	struct TDecoratorBinding<IInterfaceB> : FDecoratorBinding
	{
		void FuncB(FExecutionContext& Context) const
		{
			GetInterface()->FuncB(Context, *this);
		}

	protected:
		const IInterfaceB* GetInterface() const { return GetInterfaceTyped<IInterfaceB>(); }
	};

	void IInterfaceB::FuncB(FExecutionContext& Context, const TDecoratorBinding<IInterfaceB>& Binding) const
	{
		TDecoratorBinding<IInterfaceB> SuperBinding;
		if (Context.GetInterfaceSuper(Binding, SuperBinding))
		{
			SuperBinding.FuncB(Context);
		}
	}

	//////////////////////////////////////////////////////////////////////////

	struct IInterfaceC : IDecoratorInterface
	{
		DECLARE_ANIM_DECORATOR_INTERFACE(IInterfaceC, 0x32cb8b3c)

		virtual void FuncC(FExecutionContext& Context, const TDecoratorBinding<IInterfaceC>& Binding) const;
	};

	template<>
	struct TDecoratorBinding<IInterfaceC> : FDecoratorBinding
	{
		void FuncC(FExecutionContext& Context) const
		{
			GetInterface()->FuncC(Context, *this);
		}

	protected:
		const IInterfaceC* GetInterface() const { return GetInterfaceTyped<IInterfaceC>(); }
	};

	void IInterfaceC::FuncC(FExecutionContext& Context, const TDecoratorBinding<IInterfaceC>& Binding) const
	{
		TDecoratorBinding<IInterfaceC> SuperBinding;
		if (Context.GetInterfaceSuper(Binding, SuperBinding))
		{
			SuperBinding.FuncC(Context);
		}
	}

	//////////////////////////////////////////////////////////////////////////

	struct FDecoratorA_Base : FDecorator, IInterfaceA
	{
		DECLARE_ANIM_DECORATOR(FDecoratorA_Base, 0x3a1861cf, FDecorator)

		struct FSharedData : FDecorator::FSharedData
		{
			FDecoratorUID DecoratorUID = FDecoratorA_Base::DecoratorUID;
		};

		struct FInstanceData : FDecorator::FInstanceData
		{
			FDecoratorUID DecoratorUID = FDecoratorA_Base::DecoratorUID;

			FInstanceData()
			{
				if (Private::ConstructedDecorators != nullptr)
				{
					Private::ConstructedDecorators->Add(FDecoratorA_Base::DecoratorUID);
				}
			}

			~FInstanceData()
			{
				if (Private::DestructedDecorators != nullptr)
				{
					Private::DestructedDecorators->Add(FDecoratorA_Base::DecoratorUID);
				}
			}
		};

		// FDecorator impl
		virtual EDecoratorMode GetMode() const override { return EDecoratorMode::Base; }

		// IInterfaceA impl
		virtual void FuncA(FExecutionContext& Context, const TDecoratorBinding<IInterfaceA>& Binding) const override
		{
		}
	};

	DEFINE_ANIM_DECORATOR_BEGIN(FDecoratorA_Base)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IInterfaceA)
	DEFINE_ANIM_DECORATOR_END(FDecoratorA_Base)

	//////////////////////////////////////////////////////////////////////////

	struct FDecoratorAB_Add : FDecorator, IInterfaceA, IInterfaceB
	{
		DECLARE_ANIM_DECORATOR(FDecoratorAB_Add, 0xe205a0e1, FDecorator)

		struct FSharedData : FDecorator::FSharedData
		{
			FDecoratorUID DecoratorUID = FDecoratorAB_Add::DecoratorUID;
		};

		struct FInstanceData : FDecorator::FInstanceData
		{
			FDecoratorUID DecoratorUID = FDecoratorAB_Add::DecoratorUID;

			FInstanceData()
			{
				if (Private::ConstructedDecorators != nullptr)
				{
					Private::ConstructedDecorators->Add(FDecoratorAB_Add::DecoratorUID);
				}
			}

			~FInstanceData()
			{
				if (Private::DestructedDecorators != nullptr)
				{
					Private::DestructedDecorators->Add(FDecoratorAB_Add::DecoratorUID);
				}
			}
		};

		// FDecorator impl
		virtual EDecoratorMode GetMode() const override { return EDecoratorMode::Additive; }

		// IInterfaceA impl
		virtual void FuncA(FExecutionContext& Context, const TDecoratorBinding<IInterfaceA>& Binding) const override
		{
		}

		// IInterfaceB impl
		virtual void FuncB(FExecutionContext& Context, const TDecoratorBinding<IInterfaceB>& Binding) const override
		{
		}
	};

	DEFINE_ANIM_DECORATOR_BEGIN(FDecoratorAB_Add)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IInterfaceA)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IInterfaceB)
	DEFINE_ANIM_DECORATOR_END(FDecoratorAB_Add)

	//////////////////////////////////////////////////////////////////////////

	struct FDecoratorAC_Add : FDecorator, IInterfaceA, IInterfaceC
	{
		DECLARE_ANIM_DECORATOR(FDecoratorAC_Add, 0x26d83846, FDecorator)

		struct FSharedData : FDecorator::FSharedData
		{
			FDecoratorUID DecoratorUID = FDecoratorAC_Add::DecoratorUID;
		};

		struct FInstanceData : FDecorator::FInstanceData
		{
			FDecoratorUID DecoratorUID = FDecoratorAC_Add::DecoratorUID;

			FInstanceData()
			{
				if (Private::ConstructedDecorators != nullptr)
				{
					Private::ConstructedDecorators->Add(FDecoratorAC_Add::DecoratorUID);
				}
			}

			~FInstanceData()
			{
				if (Private::DestructedDecorators != nullptr)
				{
					Private::DestructedDecorators->Add(FDecoratorAC_Add::DecoratorUID);
				}
			}
		};

		// FDecorator impl
		virtual EDecoratorMode GetMode() const override { return EDecoratorMode::Additive; }

		// IInterfaceA impl
		virtual void FuncA(FExecutionContext& Context, const TDecoratorBinding<IInterfaceA>& Binding) const override
		{
		}

		// IInterfaceC impl
		virtual void FuncC(FExecutionContext& Context, const TDecoratorBinding<IInterfaceC>& Binding) const override
		{
		}
	};

	DEFINE_ANIM_DECORATOR_BEGIN(FDecoratorAC_Add)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IInterfaceA)
		DEFINE_ANIM_DECORATOR_IMPLEMENTS_INTERFACE(IInterfaceC)
	DEFINE_ANIM_DECORATOR_END(FDecoratorAC_Add)
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextRuntimeTest_DecoratorRegistry, "Animation.AnimNext.Runtime.DecoratorRegistry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextRuntimeTest_DecoratorRegistry::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	FDecoratorRegistry& Registry = FDecoratorRegistry::Get();

	AddErrorIfFalse(Registry.GetNum() == 0, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Registry should be empty");
	AddErrorIfFalse(!Registry.FindHandle(FDecoratorA_Base::DecoratorUID).IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Registry should not contain our decorator");
	AddErrorIfFalse(!Registry.FindHandle(FDecoratorAB_Add::DecoratorUID).IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Registry should not contain our decorator");
	AddErrorIfFalse(!Registry.FindHandle(FDecoratorAC_Add::DecoratorUID).IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Registry should not contain our decorator");

	{
		// Auto register a decorator
		AUTO_REGISTER_ANIM_DECORATOR(FDecoratorA_Base)

		AddErrorIfFalse(Registry.GetNum() == 1, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Registry should contain 1 decorator");

		FDecoratorRegistryHandle HandleA = Registry.FindHandle(FDecoratorA_Base::DecoratorUID);
		AddErrorIfFalse(HandleA.IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have registered automatically");
		AddErrorIfFalse(HandleA.IsStatic(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have been statically allocated");

		const FDecorator* DecoratorA = Registry.Find(HandleA);
		AddErrorIfFalse(DecoratorA != nullptr, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should exist");
		AddErrorIfFalse(DecoratorA->GetDecoratorUID() == FDecoratorA_Base::DecoratorUID, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Unexpected decorator instance type");

		{
			// Auto register another decorator
			AUTO_REGISTER_ANIM_DECORATOR(FDecoratorAB_Add)

			AddErrorIfFalse(Registry.GetNum() == 2, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Registry should contain 2 decorators");

			FDecoratorRegistryHandle HandleAB = Registry.FindHandle(FDecoratorAB_Add::DecoratorUID);
			AddErrorIfFalse(HandleAB.IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have registered automatically");
			AddErrorIfFalse(HandleAB.IsStatic(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have been statically allocated");
			AddErrorIfFalse(HandleA != HandleAB, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator handles should be different");

			const FDecorator* DecoratorAB = Registry.Find(HandleAB);
			AddErrorIfFalse(DecoratorAB != nullptr, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should exist");
			AddErrorIfFalse(DecoratorAB->GetDecoratorUID() == FDecoratorAB_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Unexpected decorator instance type");

			FDecoratorRegistryHandle HandleAC_0;
			{
				// Dynamically register a decorator
				FDecoratorAC_Add DecoratorAC_0;
				Registry.Register(&DecoratorAC_0);

				AddErrorIfFalse(Registry.GetNum() == 3, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Registry should contain 3 decorators");

				HandleAC_0 = Registry.FindHandle(FDecoratorAC_Add::DecoratorUID);
				AddErrorIfFalse(HandleAC_0.IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have registered automatically");
				AddErrorIfFalse(HandleAC_0.IsDynamic(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have been dynamically allocated");
				AddErrorIfFalse(HandleA != HandleAC_0, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator handles should be different");

				const FDecorator* DecoratorAC_0Ptr = Registry.Find(HandleAC_0);
				AddErrorIfFalse(DecoratorAC_0Ptr != nullptr, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should exist");
				AddErrorIfFalse(DecoratorAC_0Ptr->GetDecoratorUID() == FDecoratorAC_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Unexpected decorator instance type");
				AddErrorIfFalse(&DecoratorAC_0 == DecoratorAC_0Ptr, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Unexpected decorator instance pointer");

				// Unregister our instances
				Registry.Unregister(&DecoratorAC_0);

				AddErrorIfFalse(Registry.GetNum() == 2, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Registry should contain 2 decorators");
				AddErrorIfFalse(!Registry.FindHandle(FDecoratorAC_Add::DecoratorUID).IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have unregistered");
			}

			{
				// Dynamically register another decorator, re-using the previous dynamic index
				FDecoratorAC_Add DecoratorAC_1;
				Registry.Register(&DecoratorAC_1);

				AddErrorIfFalse(Registry.GetNum() == 3, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Registry should contain 3 decorators");

				FDecoratorRegistryHandle HandleAC_1 = Registry.FindHandle(FDecoratorAC_Add::DecoratorUID);
				AddErrorIfFalse(HandleAC_1.IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have registered automatically");
				AddErrorIfFalse(HandleAC_1.IsDynamic(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have been dynamically allocated");
				AddErrorIfFalse(HandleA != HandleAC_1, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator handles should be different");
				AddErrorIfFalse(HandleAC_0 == HandleAC_1, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator handles should be identical");

				const FDecorator* DecoratorAC_1Ptr = Registry.Find(HandleAC_1);
				AddErrorIfFalse(DecoratorAC_1Ptr != nullptr, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should exist");
				AddErrorIfFalse(DecoratorAC_1Ptr->GetDecoratorUID() == FDecoratorAC_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Unexpected decorator instance type");
				AddErrorIfFalse(&DecoratorAC_1 == DecoratorAC_1Ptr, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Unexpected decorator instance pointer");

				// Unregister our instances
				Registry.Unregister(&DecoratorAC_1);

				AddErrorIfFalse(Registry.GetNum() == 2, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Registry should contain 2 decorators");
				AddErrorIfFalse(!Registry.FindHandle(FDecoratorAC_Add::DecoratorUID).IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have unregistered");
			}
		}

		AddErrorIfFalse(Registry.GetNum() == 1, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Registry should contain 1 decorator");
		AddErrorIfFalse(!Registry.FindHandle(FDecoratorAB_Add::DecoratorUID).IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have unregistered automatically");
		AddErrorIfFalse(HandleA == Registry.FindHandle(FDecoratorA_Base::DecoratorUID), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator handle should not have changed");
	}

	AddErrorIfFalse(Registry.GetNum() == 0, "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> All decorators should have unregistered");
	AddErrorIfFalse(!Registry.FindHandle(FDecoratorA_Base::DecoratorUID).IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have unregistered automatically");
	AddErrorIfFalse(!Registry.FindHandle(FDecoratorAB_Add::DecoratorUID).IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have unregistered automatically");
	AddErrorIfFalse(!Registry.FindHandle(FDecoratorAC_Add::DecoratorUID).IsValid(), "FAnimationAnimNextRuntimeTest_DecoratorRegistry -> Decorator should have unregistered automatically");

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextRuntimeTest_NodeTemplateRegistry, "Animation.AnimNext.Runtime.NodeTemplateRegistry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextRuntimeTest_NodeTemplateRegistry::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorA_Base)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorAB_Add)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorAC_Add)

	FNodeTemplateRegistry& Registry = FNodeTemplateRegistry::Get();

	TArray<FDecoratorUID> NodeTemplateDecoratorList;
	NodeTemplateDecoratorList.Add(FDecoratorA_Base::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAB_Add::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAC_Add::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorA_Base::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAC_Add::DecoratorUID);

	// Populate our node template registry
	TArray<uint8> NodeTemplateBuffer0;
	const FNodeTemplate* NodeTemplate0 = BuildNodeTemplate(NodeTemplateDecoratorList, NodeTemplateBuffer0);

	AddErrorIfFalse(Registry.GetNum() == 0, "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Registry should contain any templates");

	FNodeTemplateRegistryHandle TemplateHandle0 = Registry.FindOrAdd(NodeTemplate0);
	AddErrorIfFalse(Registry.GetNum() == 1, "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Registry should contain 1 template");
	AddErrorIfFalse(TemplateHandle0.IsValid(), "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Registry should contain our template");

	const uint32 TemplateSize0 = NodeTemplate0->GetNodeTemplateSize();
	const FNodeTemplate* NodeTemplate0_ = Registry.Find(TemplateHandle0);
	AddErrorIfFalse(NodeTemplate0_ != nullptr, "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Registry should contain our template");
	AddErrorIfFalse(NodeTemplate0_ != NodeTemplate0, "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Template pointers should be different");
	AddErrorIfFalse(FMemory::Memcmp(NodeTemplate0, NodeTemplate0_, TemplateSize0) == 0, "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Templates should be identical");

	// Try and register a duplicate template
	TArray<uint8> NodeTemplateBuffer1;
	const FNodeTemplate* NodeTemplate1 = BuildNodeTemplate(NodeTemplateDecoratorList, NodeTemplateBuffer1);
	AddErrorIfFalse(NodeTemplate0 != NodeTemplate1, "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Node template pointers should be different");
	AddErrorIfFalse(NodeTemplate0->GetUID() == NodeTemplate1->GetUID(), "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Node template UIDs should be identical");

	FNodeTemplateRegistryHandle TemplateHandle1 = Registry.FindOrAdd(NodeTemplate1);
	AddErrorIfFalse(Registry.GetNum() == 1, "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Registry should contain 1 template");
	AddErrorIfFalse(TemplateHandle0 == TemplateHandle1, "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Node template handles should be identical");

	// Try and register a new template
	TArray<FDecoratorUID> NodeTemplateDecoratorList2;
	NodeTemplateDecoratorList2.Add(FDecoratorA_Base::DecoratorUID);
	NodeTemplateDecoratorList2.Add(FDecoratorAB_Add::DecoratorUID);
	NodeTemplateDecoratorList2.Add(FDecoratorAC_Add::DecoratorUID);
	NodeTemplateDecoratorList2.Add(FDecoratorAC_Add::DecoratorUID);

	TArray<uint8> NodeTemplateBuffer2;
	const FNodeTemplate* NodeTemplate2 = BuildNodeTemplate(NodeTemplateDecoratorList2, NodeTemplateBuffer2);
	AddErrorIfFalse(NodeTemplate0->GetUID() != NodeTemplate2->GetUID(), "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Node template UIDs should be different");

	FNodeTemplateRegistryHandle TemplateHandle2 = Registry.FindOrAdd(NodeTemplate2);
	AddErrorIfFalse(Registry.GetNum() == 2, "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Registry should contain 2 templates");
	AddErrorIfFalse(TemplateHandle0 != TemplateHandle2, "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Node template handles should be identical");
	AddErrorIfFalse(TemplateHandle2.IsValid(), "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Registry should contain our template");

	// Unregister our templates
	Registry.Unregister(NodeTemplate0);
	Registry.Unregister(NodeTemplate2);

	AddErrorIfFalse(Registry.GetNum() == 0, "FAnimationAnimNextRuntimeTest_NodeTemplateRegistry -> Registry should contain 0 templates");

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextRuntimeTest_NodeLifetime, "Animation.AnimNext.Runtime.NodeLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextRuntimeTest_NodeLifetime::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorA_Base)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorAB_Add)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorAC_Add)

	FNodeTemplateRegistry& Registry = FNodeTemplateRegistry::Get();

	TArray<FDecoratorUID> NodeTemplateDecoratorList;
	NodeTemplateDecoratorList.Add(FDecoratorA_Base::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAB_Add::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAC_Add::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorA_Base::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAC_Add::DecoratorUID);

	// Populate our node template registry
	TArray<uint8> NodeTemplateBuffer0;
	const FNodeTemplate* NodeTemplate0 = BuildNodeTemplate(NodeTemplateDecoratorList, NodeTemplateBuffer0);

	FNodeTemplateRegistryHandle TemplateHandle0 = Registry.FindOrAdd(NodeTemplate0);
	AddErrorIfFalse(Registry.GetNum() == 1, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Registry should contain 1 template");
	AddErrorIfFalse(TemplateHandle0.IsValid(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Registry should contain our template");

	// Build our graph
	uint16 NodeUID = 0;
	TArray<uint8> GraphSharedDataBuffer;
	const FNodeHandle Node0 = AppendNodeToGraph(*NodeTemplate0, NodeUID, GraphSharedDataBuffer);
	const FNodeHandle Node1 = AppendNodeToGraph(*NodeTemplate0, NodeUID, GraphSharedDataBuffer);

	FExecutionContext Context(GraphSharedDataBuffer);

	// Validate handle bookkeeping
	{
		FDecoratorBinding RootBinding;					// Empty, no parent
		FDecoratorHandle DecoratorHandle00(Node0, 0);	// Point to first node, first base decorator

		// Allocate a node
		FDecoratorPtr DecoratorPtr00 = Context.AllocateNodeInstance(RootBinding, DecoratorHandle00);
		AddErrorIfFalse(DecoratorPtr00.IsValid(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Failed to allocate a node instance");
		AddErrorIfFalse(DecoratorPtr00.GetDecoratorIndex() == 0, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated decorator pointer should point to root decorator");
		AddErrorIfFalse(!DecoratorPtr00.IsWeak(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated decorator pointer should not be weak, we have no parent");
		AddErrorIfFalse(DecoratorPtr00.GetNodeInstance()->GetNodeHandle() == Node0, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated node should point to the provided node handle");
		AddErrorIfFalse(DecoratorPtr00.GetNodeInstance()->GetReferenceCount() == 1, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated node should have a single reference");

		{
			FWeakDecoratorPtr WeakDecoratorPtr00(DecoratorPtr00);
			AddErrorIfFalse(WeakDecoratorPtr00.GetNodeInstance() == DecoratorPtr00.GetNodeInstance(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Weak reference should point to the same node instance");
			AddErrorIfFalse(WeakDecoratorPtr00.GetDecoratorIndex() == DecoratorPtr00.GetDecoratorIndex(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Weak reference should point to the same decorator index");
			AddErrorIfFalse(DecoratorPtr00.GetNodeInstance()->GetReferenceCount() == 1, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Weak references shouldn't increase ref count");
		}

		AddErrorIfFalse(DecoratorPtr00.GetNodeInstance()->GetReferenceCount() == 1, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Weak references shouldn't decrease ref count");

		{
			FWeakDecoratorPtr WeakDecoratorPtr00 = DecoratorPtr00;
			AddErrorIfFalse(WeakDecoratorPtr00.GetNodeInstance() == DecoratorPtr00.GetNodeInstance(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Weak reference should point to the same node instance");
			AddErrorIfFalse(WeakDecoratorPtr00.GetDecoratorIndex() == DecoratorPtr00.GetDecoratorIndex(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Weak reference should point to the same decorator index");
			AddErrorIfFalse(DecoratorPtr00.GetNodeInstance()->GetReferenceCount() == 1, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Weak references shouldn't increase ref count");
		}

		AddErrorIfFalse(DecoratorPtr00.GetNodeInstance()->GetReferenceCount() == 1, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Weak references shouldn't decrease ref count");

		{
			FDecoratorPtr DecoratorPtr00_1(DecoratorPtr00);
			AddErrorIfFalse(DecoratorPtr00_1.GetNodeInstance() == DecoratorPtr00.GetNodeInstance(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Strong reference should point to the same node instance");
			AddErrorIfFalse(DecoratorPtr00_1.GetDecoratorIndex() == DecoratorPtr00.GetDecoratorIndex(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Strong reference should point to the same decorator index");
			AddErrorIfFalse(DecoratorPtr00.GetNodeInstance()->GetReferenceCount() == 2, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Strong references should increase ref count");
		}

		AddErrorIfFalse(DecoratorPtr00.GetNodeInstance()->GetReferenceCount() == 1, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Strong references should decrease ref count");

		{
			FDecoratorPtr DecoratorPtr00_1 = DecoratorPtr00;
			AddErrorIfFalse(DecoratorPtr00_1.GetNodeInstance() == DecoratorPtr00.GetNodeInstance(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Strong reference should point to the same node instance");
			AddErrorIfFalse(DecoratorPtr00_1.GetDecoratorIndex() == DecoratorPtr00.GetDecoratorIndex(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Strong reference should point to the same decorator index");
			AddErrorIfFalse(DecoratorPtr00.GetNodeInstance()->GetReferenceCount() == 2, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Strong references should increase ref count");
		}

		AddErrorIfFalse(DecoratorPtr00.GetNodeInstance()->GetReferenceCount() == 1, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Strong references should decrease ref count");
	}

	// Validate parent support
	{
		FDecoratorBinding RootBinding;					// Empty, no parent
		FDecoratorHandle DecoratorHandle00(Node0, 0);	// Point to first node, first base decorator
		FDecoratorHandle DecoratorHandle03(Node0, 3);	// Point to first node, second base decorator
		FDecoratorHandle DecoratorHandle10(Node1, 0);	// Point to second node, first base decorator

		// Allocate our first node
		FDecoratorPtr DecoratorPtr00 = Context.AllocateNodeInstance(RootBinding, DecoratorHandle00);
		AddErrorIfFalse(DecoratorPtr00.IsValid(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Failed to allocate a node instance");

		// Allocate a new node, using the first as a parent
		// Both decorators live on the same node, the returned handle should be weak on the parent
		FDecoratorPtr DecoratorPtr03 = Context.AllocateNodeInstance(DecoratorPtr00, DecoratorHandle03);
		AddErrorIfFalse(DecoratorPtr03.IsValid(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Failed to allocate a node instance");
		AddErrorIfFalse(DecoratorPtr03.GetDecoratorIndex() == 3, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated decorator pointer should point to fourth decorator");
		AddErrorIfFalse(DecoratorPtr03.IsWeak(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated decorator pointer should be weak, we have the same parent");
		AddErrorIfFalse(DecoratorPtr03.GetNodeInstance()->GetNodeHandle() == Node0, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated node should point to the provided node handle");
		AddErrorIfFalse(DecoratorPtr03.GetNodeInstance() == DecoratorPtr00.GetNodeInstance(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Handles should point to the same node instance");
		AddErrorIfFalse(DecoratorPtr03.GetNodeInstance()->GetReferenceCount() == 1, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated node should have one reference");

		// Allocate a new node, using the first as a parent
		// The second decorator lives on a new node, a new node instance will be allocated
		FDecoratorPtr DecoratorPtr10 = Context.AllocateNodeInstance(DecoratorPtr00, DecoratorHandle10);
		AddErrorIfFalse(DecoratorPtr10.IsValid(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Failed to allocate a node instance");
		AddErrorIfFalse(DecoratorPtr10.GetDecoratorIndex() == 0, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated decorator pointer should point to first decorator");
		AddErrorIfFalse(!DecoratorPtr10.IsWeak(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated decorator pointer should not be weak, we have the same parent but a different node handle");
		AddErrorIfFalse(DecoratorPtr10.GetNodeInstance()->GetNodeHandle() == Node1, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated node should point to the provided node handle");
		AddErrorIfFalse(DecoratorPtr10.GetNodeInstance() != DecoratorPtr00.GetNodeInstance(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Handles should not point to the same node instance");
		AddErrorIfFalse(DecoratorPtr10.GetNodeInstance()->GetReferenceCount() == 1, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Allocated node should have one reference");
	}

	// Validate constructors and destructors
	{
		TArray<FDecoratorUID> ConstructedDecorators;
		TArray<FDecoratorUID> DestructedDecorators;

		Private::ConstructedDecorators = &ConstructedDecorators;
		Private::DestructedDecorators = &DestructedDecorators;

		{
			FDecoratorBinding RootBinding;					// Empty, no parent
			FDecoratorHandle DecoratorHandle00(Node0, 0);	// Point to first node, first base decorator

			// Allocate our node instance
			FDecoratorPtr DecoratorPtr00 = Context.AllocateNodeInstance(RootBinding, DecoratorHandle00);
			AddErrorIfFalse(DecoratorPtr00.IsValid(), "FAnimationAnimNextRuntimeTest_NodeLifetime -> Failed to allocate a node instance");

			// Validate instance constructors
			AddErrorIfFalse(ConstructedDecorators.Num() == 5, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Expected all 5 decorators to have been constructed");
			AddErrorIfFalse(DestructedDecorators.Num() == 0, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Expected no decorators to have been destructed");
			AddErrorIfFalse(ConstructedDecorators[0] == NodeTemplateDecoratorList[0], "FAnimationAnimNextRuntimeTest_NodeLifetime -> Unexpected constructor order");
			AddErrorIfFalse(ConstructedDecorators[1] == NodeTemplateDecoratorList[1], "FAnimationAnimNextRuntimeTest_NodeLifetime -> Unexpected constructor order");
			AddErrorIfFalse(ConstructedDecorators[2] == NodeTemplateDecoratorList[2], "FAnimationAnimNextRuntimeTest_NodeLifetime -> Unexpected constructor order");
			AddErrorIfFalse(ConstructedDecorators[3] == NodeTemplateDecoratorList[3], "FAnimationAnimNextRuntimeTest_NodeLifetime -> Unexpected constructor order");
			AddErrorIfFalse(ConstructedDecorators[4] == NodeTemplateDecoratorList[4], "FAnimationAnimNextRuntimeTest_NodeLifetime -> Unexpected constructor order");

			// Destruct our node instance
		}

		// Validate instance destructors
		AddErrorIfFalse(ConstructedDecorators.Num() == 5, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Expected no decorators to have been constructed");
		AddErrorIfFalse(DestructedDecorators.Num() == 5, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Expected all 5 decorators to have been destructed");
		AddErrorIfFalse(DestructedDecorators[0] == NodeTemplateDecoratorList[4], "FAnimationAnimNextRuntimeTest_NodeLifetime -> Unexpected destructor order");
		AddErrorIfFalse(DestructedDecorators[1] == NodeTemplateDecoratorList[3], "FAnimationAnimNextRuntimeTest_NodeLifetime -> Unexpected destructor order");
		AddErrorIfFalse(DestructedDecorators[2] == NodeTemplateDecoratorList[2], "FAnimationAnimNextRuntimeTest_NodeLifetime -> Unexpected destructor order");
		AddErrorIfFalse(DestructedDecorators[3] == NodeTemplateDecoratorList[1], "FAnimationAnimNextRuntimeTest_NodeLifetime -> Unexpected destructor order");
		AddErrorIfFalse(DestructedDecorators[4] == NodeTemplateDecoratorList[0], "FAnimationAnimNextRuntimeTest_NodeLifetime -> Unexpected destructor order");

		Private::ConstructedDecorators = nullptr;
		Private::DestructedDecorators = nullptr;
	}

	// Unregister our templates
	Registry.Unregister(NodeTemplate0);

	AddErrorIfFalse(Registry.GetNum() == 0, "FAnimationAnimNextRuntimeTest_NodeLifetime -> Registry should contain 0 templates");

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextRuntimeTest_GetInterface, "Animation.AnimNext.Runtime.GetInterface", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextRuntimeTest_GetInterface::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorA_Base)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorAB_Add)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorAC_Add)

	FNodeTemplateRegistry& Registry = FNodeTemplateRegistry::Get();

	TArray<FDecoratorUID> NodeTemplateDecoratorList;
	NodeTemplateDecoratorList.Add(FDecoratorA_Base::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAB_Add::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAC_Add::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorA_Base::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAC_Add::DecoratorUID);

	// Populate our node template registry
	TArray<uint8> NodeTemplateBuffer0;
	const FNodeTemplate* NodeTemplate0 = BuildNodeTemplate(NodeTemplateDecoratorList, NodeTemplateBuffer0);

	FNodeTemplateRegistryHandle TemplateHandle0 = Registry.FindOrAdd(NodeTemplate0);
	AddErrorIfFalse(Registry.GetNum() == 1, "FAnimationAnimNextRuntimeTest_GetInterface -> Registry should contain 1 template");
	AddErrorIfFalse(TemplateHandle0.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterface -> Registry should contain our template");

	// Build our graph
	uint16 NodeUID = 0;
	TArray<uint8> GraphSharedDataBuffer;

	const FNodeHandle Node0 = AppendNodeToGraph(*NodeTemplate0, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplate0, Node0, GraphSharedDataBuffer, 0, FDecoratorA_Base::FSharedData());
	InitNodeDecorator(*NodeTemplate0, Node0, GraphSharedDataBuffer, 1, FDecoratorAB_Add::FSharedData());
	InitNodeDecorator(*NodeTemplate0, Node0, GraphSharedDataBuffer, 2, FDecoratorAC_Add::FSharedData());
	InitNodeDecorator(*NodeTemplate0, Node0, GraphSharedDataBuffer, 3, FDecoratorA_Base::FSharedData());
	InitNodeDecorator(*NodeTemplate0, Node0, GraphSharedDataBuffer, 4, FDecoratorAC_Add::FSharedData());

	FExecutionContext Context(GraphSharedDataBuffer);

	// Validate from the first base decorator
	{
		FDecoratorBinding ParentBinding;				// Empty, no parent
		FDecoratorHandle DecoratorHandle00(Node0, 0);	// Point to first node, first base decorator

		FDecoratorPtr DecoratorPtr00 = Context.AllocateNodeInstance(ParentBinding, DecoratorHandle00);
		AddErrorIfFalse(DecoratorPtr00.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterface -> Failed to allocate a node instance");

		// Validate GetInterface from a decorator handle
		TDecoratorBinding<IInterfaceC> Binding00C;
		AddErrorIfFalse(Context.GetInterface(DecoratorPtr00, Binding00C), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC not found");
		AddErrorIfFalse(Binding00C.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC binding not valid");
		AddErrorIfFalse(Binding00C.GetInterfaceUID() == IInterfaceC::InterfaceUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected interface UID found in decorator binding");
		AddErrorIfFalse(Binding00C.GetDecoratorPtr().GetDecoratorIndex() == 2, "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC not found on expected decorator");
		AddErrorIfFalse(Binding00C.GetDecoratorPtr().GetNodeInstance()->GetNodeHandle() == Node0, "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC not found on expected node");
		AddErrorIfFalse(Binding00C.GetSharedData<FDecoratorAC_Add::FSharedData>()->DecoratorUID == FDecoratorAC_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected shared data in decorator binding");
		AddErrorIfFalse(Binding00C.GetInstanceData<FDecoratorAC_Add::FInstanceData>()->DecoratorUID == FDecoratorAC_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected instance data in decorator binding");

		TDecoratorBinding<IInterfaceB> Binding00B;
		AddErrorIfFalse(Context.GetInterface(DecoratorPtr00, Binding00B), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB not found");
		AddErrorIfFalse(Binding00B.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB binding not valid");
		AddErrorIfFalse(Binding00B.GetInterfaceUID() == IInterfaceB::InterfaceUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected interface UID found in decorator binding");
		AddErrorIfFalse(Binding00B.GetDecoratorPtr().GetDecoratorIndex() == 1, "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB not found on expected decorator");
		AddErrorIfFalse(Binding00B.GetDecoratorPtr().GetNodeInstance()->GetNodeHandle() == Node0, "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB not found on expected node");
		AddErrorIfFalse(Binding00B.GetSharedData<FDecoratorAB_Add::FSharedData>()->DecoratorUID == FDecoratorAB_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected shared data in decorator binding");
		AddErrorIfFalse(Binding00B.GetInstanceData<FDecoratorAB_Add::FInstanceData>()->DecoratorUID == FDecoratorAB_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected instance data in decorator binding");

		TDecoratorBinding<IInterfaceA> Binding00A;
		AddErrorIfFalse(Context.GetInterface(DecoratorPtr00, Binding00A), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA not found");
		AddErrorIfFalse(Binding00A.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA binding not valid");
		AddErrorIfFalse(Binding00A.GetInterfaceUID() == IInterfaceA::InterfaceUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected interface UID found in decorator binding");
		AddErrorIfFalse(Binding00A.GetDecoratorPtr().GetDecoratorIndex() == 2, "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA not found on expected decorator");
		AddErrorIfFalse(Binding00A.GetDecoratorPtr().GetNodeInstance()->GetNodeHandle() == Node0, "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA not found on expected node");
		AddErrorIfFalse(Binding00A.GetSharedData<FDecoratorAC_Add::FSharedData>()->DecoratorUID == FDecoratorAC_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected shared data in decorator binding");
		AddErrorIfFalse(Binding00A.GetInstanceData<FDecoratorAC_Add::FInstanceData>()->DecoratorUID == FDecoratorAC_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected instance data in decorator binding");

		// Validate GetInterface from a decorator binding
		{
			{
				{
					TDecoratorBinding<IInterfaceC> Binding00C_;
					AddErrorIfFalse(Context.GetInterface(Binding00C, Binding00C_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC not found");
					AddErrorIfFalse(Binding00C == Binding00C_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}

				{
					TDecoratorBinding<IInterfaceC> Binding00C_;
					AddErrorIfFalse(Context.GetInterface(Binding00B, Binding00C_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC not found");
					AddErrorIfFalse(Binding00C == Binding00C_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}

				{
					TDecoratorBinding<IInterfaceC> Binding00C_;
					AddErrorIfFalse(Context.GetInterface(Binding00A, Binding00C_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC not found");
					AddErrorIfFalse(Binding00C == Binding00C_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}
			}

			{
				{
					TDecoratorBinding<IInterfaceB> Binding00B_;
					AddErrorIfFalse(Context.GetInterface(Binding00C, Binding00B_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB not found");
					AddErrorIfFalse(Binding00B == Binding00B_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}

				{
					TDecoratorBinding<IInterfaceB> Binding00B_;
					AddErrorIfFalse(Context.GetInterface(Binding00B, Binding00B_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB not found");
					AddErrorIfFalse(Binding00B == Binding00B_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}

				{
					TDecoratorBinding<IInterfaceB> Binding00B_;
					AddErrorIfFalse(Context.GetInterface(Binding00A, Binding00B_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB not found");
					AddErrorIfFalse(Binding00B == Binding00B_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}
			}

			{
				{
					TDecoratorBinding<IInterfaceA> Binding00A_;
					AddErrorIfFalse(Context.GetInterface(Binding00C, Binding00A_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA not found");
					AddErrorIfFalse(Binding00A == Binding00A_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}

				{
					TDecoratorBinding<IInterfaceA> Binding00A_;
					AddErrorIfFalse(Context.GetInterface(Binding00B, Binding00A_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA not found");
					AddErrorIfFalse(Binding00A == Binding00A_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}

				{
					TDecoratorBinding<IInterfaceA> Binding00A_;
					AddErrorIfFalse(Context.GetInterface(Binding00A, Binding00A_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA not found");
					AddErrorIfFalse(Binding00A == Binding00A_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}
			}
		}
	}

	// Validate from the second base decorator
	{
		FDecoratorBinding ParentBinding;				// Empty, no parent
		FDecoratorHandle DecoratorHandle03(Node0, 3);	// Point to first node, second base decorator

		FDecoratorPtr DecoratorPtr03 = Context.AllocateNodeInstance(ParentBinding, DecoratorHandle03);
		AddErrorIfFalse(DecoratorPtr03.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterface -> Failed to allocate a node instance");

		// Validate GetInterface from a decorator handle
		TDecoratorBinding<IInterfaceC> Binding03C;
		AddErrorIfFalse(Context.GetInterface(DecoratorPtr03, Binding03C), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC not found");
		AddErrorIfFalse(Binding03C.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC binding not valid");
		AddErrorIfFalse(Binding03C.GetInterfaceUID() == IInterfaceC::InterfaceUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected interface UID found in decorator binding");
		AddErrorIfFalse(Binding03C.GetDecoratorPtr().GetDecoratorIndex() == 4, "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC not found on expected decorator");
		AddErrorIfFalse(Binding03C.GetDecoratorPtr().GetNodeInstance()->GetNodeHandle() == Node0, "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC not found on expected node");
		AddErrorIfFalse(Binding03C.GetSharedData<FDecoratorAC_Add::FSharedData>()->DecoratorUID == FDecoratorAC_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected shared data in decorator binding");
		AddErrorIfFalse(Binding03C.GetInstanceData<FDecoratorAC_Add::FInstanceData>()->DecoratorUID == FDecoratorAC_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected instance data in decorator binding");

		TDecoratorBinding<IInterfaceB> Binding03B;
		AddErrorIfFalse(!Context.GetInterface(DecoratorPtr03, Binding03B), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB found");
		AddErrorIfFalse(!Binding03B.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB binding should not be valid");

		TDecoratorBinding<IInterfaceA> Binding03A;
		AddErrorIfFalse(Context.GetInterface(DecoratorPtr03, Binding03A), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA not found");
		AddErrorIfFalse(Binding03A.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA binding not valid");
		AddErrorIfFalse(Binding03A.GetInterfaceUID() == IInterfaceA::InterfaceUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected interface UID found in decorator binding");
		AddErrorIfFalse(Binding03A.GetDecoratorPtr().GetDecoratorIndex() == 4, "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA not found on expected decorator");
		AddErrorIfFalse(Binding03A.GetDecoratorPtr().GetNodeInstance()->GetNodeHandle() == Node0, "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA not found on expected node");
		AddErrorIfFalse(Binding03A.GetSharedData<FDecoratorAC_Add::FSharedData>()->DecoratorUID == FDecoratorAC_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected shared data in decorator binding");
		AddErrorIfFalse(Binding03A.GetInstanceData<FDecoratorAC_Add::FInstanceData>()->DecoratorUID == FDecoratorAC_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterface -> Unexpected instance data in decorator binding");

		// Validate GetInterface from a decorator binding
		{
			{
				{
					TDecoratorBinding<IInterfaceC> Binding03C_;
					AddErrorIfFalse(Context.GetInterface(Binding03C, Binding03C_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC not found");
					AddErrorIfFalse(Binding03C == Binding03C_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}

				{
					TDecoratorBinding<IInterfaceC> Binding03C_;
					AddErrorIfFalse(!Context.GetInterface(Binding03B, Binding03C_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC found");
				}

				{
					TDecoratorBinding<IInterfaceC> Binding03C_;
					AddErrorIfFalse(Context.GetInterface(Binding03A, Binding03C_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceC not found");
					AddErrorIfFalse(Binding03C == Binding03C_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}
			}

			{
				{
					TDecoratorBinding<IInterfaceB> Binding03B_;
					AddErrorIfFalse(!Context.GetInterface(Binding03C, Binding03B_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB found");
					AddErrorIfFalse(Binding03B == Binding03B_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}

				{
					TDecoratorBinding<IInterfaceB> Binding03B_;
					AddErrorIfFalse(!Context.GetInterface(Binding03B, Binding03B_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB found");
					AddErrorIfFalse(Binding03B == Binding03B_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}

				{
					TDecoratorBinding<IInterfaceB> Binding03B_;
					AddErrorIfFalse(!Context.GetInterface(Binding03A, Binding03B_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceB found");
					AddErrorIfFalse(Binding03B == Binding03B_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}
			}

			{
				{
					TDecoratorBinding<IInterfaceA> Binding03A_;
					AddErrorIfFalse(Context.GetInterface(Binding03C, Binding03A_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA not found");
					AddErrorIfFalse(Binding03A == Binding03A_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}

				{
					TDecoratorBinding<IInterfaceA> Binding03A_;
					AddErrorIfFalse(!Context.GetInterface(Binding03B, Binding03A_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA found");
				}

				{
					TDecoratorBinding<IInterfaceA> Binding03A_;
					AddErrorIfFalse(Context.GetInterface(Binding03A, Binding03A_), "FAnimationAnimNextRuntimeTest_GetInterface -> InterfaceA not found");
					AddErrorIfFalse(Binding03A == Binding03A_, "FAnimationAnimNextRuntimeTest_GetInterface -> GetInterface methods should return the same result");
				}
			}
		}
	}

	Registry.Unregister(NodeTemplate0);

	AddErrorIfFalse(Registry.GetNum() == 0, "FAnimationAnimNextRuntimeTest_GetInterface -> Registry should contain 0 templates");

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextRuntimeTest_GetInterfaceSuper, "Animation.AnimNext.Runtime.GetInterfaceSuper", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextRuntimeTest_GetInterfaceSuper::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorA_Base)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorAB_Add)
	AUTO_REGISTER_ANIM_DECORATOR(FDecoratorAC_Add)

	FNodeTemplateRegistry& Registry = FNodeTemplateRegistry::Get();

	TArray<FDecoratorUID> NodeTemplateDecoratorList;
	NodeTemplateDecoratorList.Add(FDecoratorA_Base::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAB_Add::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAC_Add::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorA_Base::DecoratorUID);
	NodeTemplateDecoratorList.Add(FDecoratorAC_Add::DecoratorUID);

	// Populate our node template registry
	TArray<uint8> NodeTemplateBuffer0;
	const FNodeTemplate* NodeTemplate0 = BuildNodeTemplate(NodeTemplateDecoratorList, NodeTemplateBuffer0);

	FNodeTemplateRegistryHandle TemplateHandle0 = Registry.FindOrAdd(NodeTemplate0);
	AddErrorIfFalse(Registry.GetNum() == 1, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Registry should contain 1 template");
	AddErrorIfFalse(TemplateHandle0.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Registry should contain our template");

	// Build our graph
	uint16 NodeUID = 0;
	TArray<uint8> GraphSharedDataBuffer;

	const FNodeHandle Node0 = AppendNodeToGraph(*NodeTemplate0, NodeUID, GraphSharedDataBuffer);
	InitNodeDecorator(*NodeTemplate0, Node0, GraphSharedDataBuffer, 0, FDecoratorA_Base::FSharedData());
	InitNodeDecorator(*NodeTemplate0, Node0, GraphSharedDataBuffer, 1, FDecoratorAB_Add::FSharedData());
	InitNodeDecorator(*NodeTemplate0, Node0, GraphSharedDataBuffer, 2, FDecoratorAC_Add::FSharedData());
	InitNodeDecorator(*NodeTemplate0, Node0, GraphSharedDataBuffer, 3, FDecoratorA_Base::FSharedData());
	InitNodeDecorator(*NodeTemplate0, Node0, GraphSharedDataBuffer, 4, FDecoratorAC_Add::FSharedData());

	FExecutionContext Context(GraphSharedDataBuffer);

	// Validate from the first base decorator
	{
		FDecoratorBinding ParentBinding;				// Empty, no parent
		FDecoratorHandle DecoratorHandle00(Node0, 0);	// Point to first node, first base decorator

		FDecoratorPtr DecoratorPtr00 = Context.AllocateNodeInstance(ParentBinding, DecoratorHandle00);
		AddErrorIfFalse(DecoratorPtr00.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Failed to allocate a node instance");

		{
			// Get a valid decorator binding: FDecoratorAC_Add
			TDecoratorBinding<IInterfaceC> Binding02C;
			AddErrorIfFalse(Context.GetInterface(DecoratorPtr00, Binding02C), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceC not found");

			// Validate GetInterfaceSuper from a decorator handle
			TDecoratorBinding<IInterfaceC> SuperBinding02C;
			AddErrorIfFalse(!Context.GetInterfaceSuper(Binding02C.GetDecoratorPtr(), SuperBinding02C), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceC found");

			// Validate GetInterfaceSuper from a decorator binding
			TDecoratorBinding<IInterfaceC> SuperBinding02C_;
			AddErrorIfFalse(!Context.GetInterfaceSuper(Binding02C, SuperBinding02C_), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceC found");
			AddErrorIfFalse(SuperBinding02C == SuperBinding02C_, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> GetInterfaceSuper methods should return the same result");
		}

		{
			// Get a valid decorator binding: FDecoratorAC_Add
			TDecoratorBinding<IInterfaceA> Binding02A;
			AddErrorIfFalse(Context.GetInterface(DecoratorPtr00, Binding02A), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found");

			// Validate GetInterfaceSuper from a decorator handle, FDecoratorAB_Add
			TDecoratorBinding<IInterfaceA> SuperBinding02A;
			AddErrorIfFalse(Context.GetInterfaceSuper(Binding02A.GetDecoratorPtr(), SuperBinding02A), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found");
			AddErrorIfFalse(SuperBinding02A.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA binding not valid");
			AddErrorIfFalse(SuperBinding02A.GetInterfaceUID() == IInterfaceA::InterfaceUID, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Unexpected interface UID found in decorator binding");
			AddErrorIfFalse(SuperBinding02A.GetDecoratorPtr().GetDecoratorIndex() == 1, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found on expected decorator");
			AddErrorIfFalse(SuperBinding02A.GetDecoratorPtr().GetNodeInstance()->GetNodeHandle() == Node0, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found on expected node");
			AddErrorIfFalse(SuperBinding02A.GetSharedData<FDecoratorAB_Add::FSharedData>()->DecoratorUID == FDecoratorAB_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Unexpected shared data in decorator binding");
			AddErrorIfFalse(SuperBinding02A.GetInstanceData<FDecoratorAB_Add::FInstanceData>()->DecoratorUID == FDecoratorAB_Add::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Unexpected instance data in decorator binding");

			// Validate GetInterfaceSuper from a decorator binding, FDecoratorAB_Add
			TDecoratorBinding<IInterfaceA> SuperBinding02A_;
			AddErrorIfFalse(Context.GetInterfaceSuper(Binding02A, SuperBinding02A_), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found");
			AddErrorIfFalse(SuperBinding02A == SuperBinding02A_, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> GetInterfaceSuper methods should return the same result");

			// Validate GetInterfaceSuper from a decorator handle, FDecoratorA_Base
			TDecoratorBinding<IInterfaceA> SuperBinding01A;
			AddErrorIfFalse(Context.GetInterfaceSuper(SuperBinding02A.GetDecoratorPtr(), SuperBinding01A), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found");
			AddErrorIfFalse(SuperBinding01A.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA binding not valid");
			AddErrorIfFalse(SuperBinding01A.GetInterfaceUID() == IInterfaceA::InterfaceUID, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Unexpected interface UID found in decorator binding");
			AddErrorIfFalse(SuperBinding01A.GetDecoratorPtr().GetDecoratorIndex() == 0, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found on expected decorator");
			AddErrorIfFalse(SuperBinding01A.GetDecoratorPtr().GetNodeInstance()->GetNodeHandle() == Node0, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found on expected node");
			AddErrorIfFalse(SuperBinding01A.GetSharedData<FDecoratorA_Base::FSharedData>()->DecoratorUID == FDecoratorA_Base::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Unexpected shared data in decorator binding");
			AddErrorIfFalse(SuperBinding01A.GetInstanceData<FDecoratorA_Base::FInstanceData>()->DecoratorUID == FDecoratorA_Base::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Unexpected instance data in decorator binding");

			// Validate GetInterfaceSuper from a decorator binding, FDecoratorA_Base
			TDecoratorBinding<IInterfaceA> SuperBinding01A_;
			AddErrorIfFalse(Context.GetInterfaceSuper(SuperBinding02A, SuperBinding01A_), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found");
			AddErrorIfFalse(SuperBinding01A == SuperBinding01A_, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> GetInterfaceSuper methods should return the same result");

			// Validate GetInterfaceSuper from a decorator handle
			TDecoratorBinding<IInterfaceA> SuperBinding00A;
			AddErrorIfFalse(!Context.GetInterfaceSuper(SuperBinding01A.GetDecoratorPtr(), SuperBinding00A), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA found");

			// Validate GetInterfaceSuper from a decorator binding
			TDecoratorBinding<IInterfaceA> SuperBinding00A_;
			AddErrorIfFalse(!Context.GetInterfaceSuper(SuperBinding01A, SuperBinding00A_), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA found");
			AddErrorIfFalse(SuperBinding00A == SuperBinding00A_, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> GetInterfaceSuper methods should return the same result");
		}
	}

	// Validate from the second base decorator
	{
		FDecoratorBinding ParentBinding;				// Empty, no parent
		FDecoratorHandle DecoratorHandle03(Node0, 3);	// Point to first node, second base decorator

		FDecoratorPtr DecoratorPtr03 = Context.AllocateNodeInstance(ParentBinding, DecoratorHandle03);
		AddErrorIfFalse(DecoratorPtr03.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Failed to allocate a node instance");

		{
			// Get a valid decorator binding: FDecoratorAC_Add
			TDecoratorBinding<IInterfaceC> Binding04C;
			AddErrorIfFalse(Context.GetInterface(DecoratorPtr03, Binding04C), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceC not found");

			// Validate GetInterfaceSuper from a decorator handle
			TDecoratorBinding<IInterfaceC> SuperBinding04C;
			AddErrorIfFalse(!Context.GetInterfaceSuper(Binding04C.GetDecoratorPtr(), SuperBinding04C), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceC found");

			// Validate GetInterfaceSuper from a decorator binding
			TDecoratorBinding<IInterfaceC> SuperBinding04C_;
			AddErrorIfFalse(!Context.GetInterfaceSuper(Binding04C, SuperBinding04C_), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceC found");
			AddErrorIfFalse(SuperBinding04C == SuperBinding04C_, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> GetInterfaceSuper methods should return the same result");
		}

		{
			// Get a valid decorator binding: FDecoratorAC_Add
			TDecoratorBinding<IInterfaceA> Binding04A;
			AddErrorIfFalse(Context.GetInterface(DecoratorPtr03, Binding04A), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found");

			// Validate GetInterfaceSuper from a decorator handle, FDecoratorA_Base
			TDecoratorBinding<IInterfaceA> SuperBinding04A;
			AddErrorIfFalse(Context.GetInterfaceSuper(Binding04A.GetDecoratorPtr(), SuperBinding04A), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found");
			AddErrorIfFalse(SuperBinding04A.IsValid(), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA binding not valid");
			AddErrorIfFalse(SuperBinding04A.GetInterfaceUID() == IInterfaceA::InterfaceUID, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Unexpected interface UID found in decorator binding");
			AddErrorIfFalse(SuperBinding04A.GetDecoratorPtr().GetDecoratorIndex() == 3, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found on expected decorator");
			AddErrorIfFalse(SuperBinding04A.GetDecoratorPtr().GetNodeInstance()->GetNodeHandle() == Node0, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found on expected node");
			AddErrorIfFalse(SuperBinding04A.GetSharedData<FDecoratorA_Base::FSharedData>()->DecoratorUID == FDecoratorA_Base::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Unexpected shared data in decorator binding");
			AddErrorIfFalse(SuperBinding04A.GetInstanceData<FDecoratorA_Base::FInstanceData>()->DecoratorUID == FDecoratorA_Base::DecoratorUID, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> Unexpected instance data in decorator binding");

			// Validate GetInterfaceSuper from a decorator binding, FDecoratorA_Base
			TDecoratorBinding<IInterfaceA> SuperBinding04A_;
			AddErrorIfFalse(Context.GetInterfaceSuper(Binding04A, SuperBinding04A_), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA not found");
			AddErrorIfFalse(SuperBinding04A == SuperBinding04A_, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> GetInterfaceSuper methods should return the same result");

			// Validate GetInterfaceSuper from a decorator handle
			TDecoratorBinding<IInterfaceA> SuperBinding03A;
			AddErrorIfFalse(!Context.GetInterfaceSuper(SuperBinding04A.GetDecoratorPtr(), SuperBinding03A), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA found");

			// Validate GetInterfaceSuper from a decorator binding
			TDecoratorBinding<IInterfaceA> SuperBinding03A_;
			AddErrorIfFalse(!Context.GetInterfaceSuper(SuperBinding04A, SuperBinding03A_), "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> InterfaceA found");
			AddErrorIfFalse(SuperBinding03A == SuperBinding03A_, "FAnimationAnimNextRuntimeTest_GetInterfaceSuper -> GetInterfaceSuper methods should return the same result");
		}
	}

	Registry.Unregister(NodeTemplate0);

	AddErrorIfFalse(Registry.GetNum() == 0, "FAnimationAnimNextRuntimeTest_GetInterface -> Registry should contain 0 templates");

	return true;
}
#endif
