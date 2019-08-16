// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <winrt/Windows.UI.Input.Spatial.h>
#include "ppltasks.h"
#include <functional>

using namespace winrt::Windows::UI::Input::Spatial;
using namespace winrt::Windows::Perception::Spatial;

namespace WindowsMixedReality
{
	class GestureRecognizer
	{
	public:
		typedef GestureRecognizerInterop::SourceStateCallback SourceStateCallback;

		typedef GestureRecognizerInterop::TapCallback TapCallback;
		typedef GestureRecognizerInterop::HoldCallback HoldCallback;
		typedef GestureRecognizerInterop::ManipulationCallback ManipulationCallback;
		typedef GestureRecognizerInterop::NavigationCallback NavigationCallback;

	private:
		static SpatialInteractionManager m_InteractionManager;
		SpatialGestureRecognizer m_GestureRecognizer = nullptr;
		SpatialStationaryFrameOfReference m_StationaryReferenceFrame = nullptr;

		std::function<void()> m_InteractionCallback;
		SourceStateCallback m_SourceStateCallback;

		winrt::event_token m_InteractionDetectedToken;
		winrt::event_token m_SourceDetectedToken;
		winrt::event_token m_SourceLostToken;

		HMDHand m_CurrentHand = HMDHand::AnyHand;

		static HMDHand GetHandness(SpatialInteractionSourceHandedness handness)
		{
			switch (handness)
			{
			case SpatialInteractionSourceHandedness::Left:
				return HMDHand::Left;
			case SpatialInteractionSourceHandedness::Right:
				return HMDHand::Right;
			default:
				return HMDHand::AnyHand;
			}
		}

		void UpdateCallbacks()
		{
			if (!m_InteractionManager)
			{
				return;
			}

			if (!m_InteractionDetectedToken)
			{
				m_InteractionDetectedToken = m_InteractionManager.InteractionDetected([=](SpatialInteractionManager sender, SpatialInteractionDetectedEventArgs args)
				{
					m_CurrentHand = GetHandness(args.InteractionSource().Handedness());

					if (m_InteractionCallback)
					{
						m_InteractionCallback();
					}

					m_GestureRecognizer.CaptureInteraction(args.Interaction());
				});
			}

			if (!m_SourceDetectedToken)
			{
				m_SourceDetectedToken = m_InteractionManager.SourceDetected([=](SpatialInteractionManager sender, SpatialInteractionSourceEventArgs args)
				{
					GestureRecognizerInterop::SourceStateDesc desc;
					memset(&desc, sizeof(desc), 0);

					desc.Hand = GetHandness(args.State().Source().Handedness());

					if (m_SourceStateCallback)
					{
						m_SourceStateCallback(SourceState::Detected, SourceKind(args.State().Source().Kind()), desc);
					}
				});
			}

			if (!m_SourceLostToken)
			{
				m_SourceLostToken = m_InteractionManager.SourceLost([=](SpatialInteractionManager sender, SpatialInteractionSourceEventArgs args)
				{
					GestureRecognizerInterop::SourceStateDesc desc;
					memset(&desc, sizeof(desc), 0);

					desc.Hand = GetHandness(args.State().Source().Handedness());

					if (m_SourceStateCallback)
					{
						m_SourceStateCallback(SourceState::Lost, SourceKind(args.State().Source().Kind()), desc);
					}
				});
			}
		}

	public:

		GestureRecognizer(SpatialStationaryFrameOfReference StationaryReferenceFrame)
			: m_StationaryReferenceFrame(StationaryReferenceFrame)
		{
			m_GestureRecognizer = SpatialGestureRecognizer(SpatialGestureSettings::None);
		}

		~GestureRecognizer()
		{
			if (m_InteractionManager)
			{
				m_InteractionManager.SourceDetected(m_SourceDetectedToken);
				m_InteractionManager.SourceLost(m_SourceLostToken);
				m_InteractionManager.InteractionDetected(m_InteractionDetectedToken);
			}
			m_GestureRecognizer = nullptr;
		}

		void Init()
		{
			UpdateCallbacks();
		}

		void Reset()
		{
			m_GestureRecognizer = SpatialGestureRecognizer(SpatialGestureSettings::None);
			m_CurrentHand = HMDHand::AnyHand;
		}

		static void SetInteractionManager(SpatialInteractionManager interactionManager)
		{
			m_InteractionManager = interactionManager;
		}

		void UpdateFrame(SpatialStationaryFrameOfReference StationaryReferenceFrame)
		{
			m_StationaryReferenceFrame = StationaryReferenceFrame;
		}


		bool SubscribeInteration(std::function<void()> callback)
		{
			m_InteractionCallback = callback;

			UpdateCallbacks();

			return true;
		}

		bool SubscribeSourceStateChanges(SourceStateCallback callback)
		{
			m_SourceStateCallback = callback;

			UpdateCallbacks();

			return true;
		}


		bool SubscribeTap(TapCallback callback)
		{
			if (!m_GestureRecognizer.TrySetGestureSettings(SpatialGestureSettings::Tap | SpatialGestureSettings::DoubleTap | m_GestureRecognizer.GestureSettings()))
			{
				return false;
			}

			m_GestureRecognizer.Tapped([=](SpatialGestureRecognizer sender, SpatialTappedEventArgs args)
			{
				GestureRecognizerInterop::Tap desc;
				memset(&desc, sizeof(desc), 0);
				desc.Count = args.TapCount();

				desc.Hand = m_CurrentHand;

				callback(GestureStage::Completed, SourceKind(args.InteractionSourceKind()), desc);
			});

			UpdateCallbacks();

			return true;
		}

		bool SubscribeHold(HoldCallback callback)
		{
			if (!m_GestureRecognizer.TrySetGestureSettings(SpatialGestureSettings::Hold | m_GestureRecognizer.GestureSettings()))
			{
				return false;
			}

			m_GestureRecognizer.HoldStarted([=](SpatialGestureRecognizer sender, SpatialHoldStartedEventArgs args)
			{
				GestureRecognizerInterop::Hold desc;
				memset(&desc, sizeof(desc), 0);

				desc.Hand = m_CurrentHand;

				callback(GestureStage::Started, SourceKind(args.InteractionSourceKind()), desc);
			});

			m_GestureRecognizer.HoldCompleted([=](SpatialGestureRecognizer sender, SpatialHoldCompletedEventArgs args)
			{
				GestureRecognizerInterop::Hold desc;
				memset(&desc, sizeof(desc), 0);

				desc.Hand = m_CurrentHand;

				callback(GestureStage::Completed, SourceKind(args.InteractionSourceKind()), desc);
			});

			m_GestureRecognizer.HoldCanceled([=](SpatialGestureRecognizer sender, SpatialHoldCanceledEventArgs args)
			{
				GestureRecognizerInterop::Hold desc;
				memset(&desc, sizeof(desc), 0);

				desc.Hand = m_CurrentHand;

				callback(GestureStage::Canceled, SourceKind(args.InteractionSourceKind()), desc);
			});

			UpdateCallbacks();

			return true;
		}

		bool SubscribeManipulation(ManipulationCallback callback)
		{
			if (!m_GestureRecognizer.TrySetGestureSettings(SpatialGestureSettings::ManipulationTranslate | m_GestureRecognizer.GestureSettings()))
			{
				return false;
			}

			m_GestureRecognizer.ManipulationStarted([=](SpatialGestureRecognizer sender, SpatialManipulationStartedEventArgs args)
			{
				GestureRecognizerInterop::Manipulation desc;
				memset(&desc, sizeof(desc), 0);

				desc.Hand = m_CurrentHand;

				callback(GestureStage::Started, SourceKind(args.InteractionSourceKind()), desc);
			});

			m_GestureRecognizer.ManipulationUpdated([=](SpatialGestureRecognizer sender, SpatialManipulationUpdatedEventArgs args)
			{
				GestureRecognizerInterop::Manipulation desc;
				memset(&desc, sizeof(desc), 0);

				desc.Hand = m_CurrentHand;

				if (m_StationaryReferenceFrame)
				{
					auto delta = args.TryGetCumulativeDelta(this->m_StationaryReferenceFrame.CoordinateSystem());
					if (delta)
					{
						auto trans = delta.Translation();
						desc.Delta = DirectX::XMFLOAT3(trans.x, trans.y, trans.z);
					}
				}

				callback(GestureStage::Updated, SourceKind(args.InteractionSourceKind()), desc);
			});

			m_GestureRecognizer.ManipulationCompleted([=](SpatialGestureRecognizer sender, SpatialManipulationCompletedEventArgs args)
			{
				GestureRecognizerInterop::Manipulation desc;
				memset(&desc, sizeof(desc), 0);

				desc.Hand = m_CurrentHand;

				if (m_StationaryReferenceFrame)
				{
					auto delta = args.TryGetCumulativeDelta(this->m_StationaryReferenceFrame.CoordinateSystem());
					if (delta)
					{
						auto trans = delta.Translation();
						desc.Delta = DirectX::XMFLOAT3(trans.x, trans.y, trans.z);
					}
				}

				callback(GestureStage::Completed, SourceKind(args.InteractionSourceKind()), desc);
			});

			m_GestureRecognizer.ManipulationCanceled([=](SpatialGestureRecognizer sender, SpatialManipulationCanceledEventArgs args)
			{
				GestureRecognizerInterop::Manipulation desc;
				memset(&desc, sizeof(desc), 0);

				desc.Hand = m_CurrentHand;

				callback(GestureStage::Canceled, SourceKind(args.InteractionSourceKind()), desc);
			});

			UpdateCallbacks();

			return true;
		}

		bool SubscribeNavigation(NavigationCallback callback, unsigned int settings)
		{
			if (!m_GestureRecognizer.TrySetGestureSettings(SpatialGestureSettings(settings) | m_GestureRecognizer.GestureSettings()))
			{
				return false;
			}

			m_GestureRecognizer.NavigationStarted([=](SpatialGestureRecognizer sender, SpatialNavigationStartedEventArgs args)
			{
				GestureRecognizerInterop::Navigation desc;
				memset(&desc, sizeof(desc), 0);

				desc.Hand = m_CurrentHand;

				callback(GestureStage::Started, SourceKind(args.InteractionSourceKind()), desc);
			});

			m_GestureRecognizer.NavigationUpdated([=](SpatialGestureRecognizer sender, SpatialNavigationUpdatedEventArgs args)
			{
				GestureRecognizerInterop::Navigation desc;
				memset(&desc, sizeof(desc), 0);

				desc.Hand = m_CurrentHand;

				auto trans = args.NormalizedOffset();
				desc.NormalizedOffset = DirectX::XMFLOAT3(trans.x, trans.y, trans.z);

				callback(GestureStage::Updated, SourceKind(args.InteractionSourceKind()), desc);
			});

			m_GestureRecognizer.NavigationCompleted([=](SpatialGestureRecognizer sender, SpatialNavigationCompletedEventArgs args)
			{
				GestureRecognizerInterop::Navigation desc;
				memset(&desc, sizeof(desc), 0);

				desc.Hand = m_CurrentHand;

				auto trans = args.NormalizedOffset();
				desc.NormalizedOffset = DirectX::XMFLOAT3(trans.x, trans.y, trans.z);

				callback(GestureStage::Completed, SourceKind(args.InteractionSourceKind()), desc);
			});

			m_GestureRecognizer.NavigationCanceled([=](SpatialGestureRecognizer sender, SpatialNavigationCanceledEventArgs args)
			{
				GestureRecognizerInterop::Navigation desc;
				memset(&desc, sizeof(desc), 0);

				desc.Hand = m_CurrentHand;

				callback(GestureStage::Canceled, SourceKind(args.InteractionSourceKind()), desc);
			});

			UpdateCallbacks();

			return true;
		}

	};
}