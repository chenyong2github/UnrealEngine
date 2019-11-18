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
		bool m_bIsHololens1 = false;

		static SpatialInteractionManager m_InteractionManager;
		SpatialGestureRecognizer m_GestureRecognizer = nullptr;
		SpatialStationaryFrameOfReference m_StationaryReferenceFrame = nullptr;
		SpatialGestureSettings m_SpatialGestureSettings;

		std::function<void()> m_InteractionCallback;
		SourceStateCallback m_SourceStateCallback;

		winrt::event_token m_InteractionDetectedToken;
		winrt::event_token m_SourceDetectedToken;
		winrt::event_token m_SourceLostToken;

		HMDHand m_CurrentHand = HMDHand::AnyHand;

		TapCallback m_TapCallback;
		HoldCallback m_HoldCallback;
		ManipulationCallback m_ManipulationCallback;
		NavigationCallback m_NavigationCallback;

		winrt::event_token GestureRecognizer_Tapped;
		winrt::event_token GestureRecognizer_HoldStarted;
		winrt::event_token GestureRecognizer_HoldCompleted;
		winrt::event_token GestureRecognizer_HoldCanceled;
		winrt::event_token GestureRecognizer_ManipulationStarted;
		winrt::event_token GestureRecognizer_ManipulationUpdated;
		winrt::event_token GestureRecognizer_ManipulationCompleted;
		winrt::event_token GestureRecognizer_ManipulationCanceled;
		winrt::event_token GestureRecognizer_NavigationStarted;
		winrt::event_token GestureRecognizer_NavigationUpdated;
		winrt::event_token GestureRecognizer_NavigationCompleted;
		winrt::event_token GestureRecognizer_NavigationCanceled;

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
					if (!m_bIsHololens1)
					{
						m_CurrentHand = GetHandness(args.InteractionSource().Handedness());
					}

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

					if (!m_bIsHololens1)
					{
						desc.Hand = GetHandness(args.State().Source().Handedness());
					}

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

					if (!m_bIsHololens1)
					{
						desc.Hand = GetHandness(args.State().Source().Handedness());
					}

					if (m_SourceStateCallback)
					{
						m_SourceStateCallback(SourceState::Lost, SourceKind(args.State().Source().Kind()), desc);
					}
				});
			}
		}

		void UpdateGestureSubscriptions()
		{
			if (!m_GestureRecognizer)
			{
				m_GestureRecognizer = SpatialGestureRecognizer(SpatialGestureSettings::None);
			}

			if (!m_GestureRecognizer.TrySetGestureSettings(m_SpatialGestureSettings))
			{
			}


			if ((uint32_t)(SpatialGestureSettings::Tap | SpatialGestureSettings::DoubleTap) & (uint32_t)(m_SpatialGestureSettings))
			{
				if (!GestureRecognizer_Tapped)
				{
					GestureRecognizer_Tapped = m_GestureRecognizer.Tapped([=](SpatialGestureRecognizer sender, SpatialTappedEventArgs args)
					{
						GestureRecognizerInterop::Tap desc;
						memset(&desc, sizeof(desc), 0);
						desc.Count = args.TapCount();

						desc.Hand = m_CurrentHand;

						m_TapCallback(GestureStage::Completed, SourceKind(args.InteractionSourceKind()), desc);
					});
				}
			}

			if ((uint32_t)SpatialGestureSettings::Hold & (uint32_t)m_SpatialGestureSettings)
			{
				if (!GestureRecognizer_HoldStarted)
				{
					GestureRecognizer_HoldStarted = m_GestureRecognizer.HoldStarted([=](SpatialGestureRecognizer sender, SpatialHoldStartedEventArgs args)
					{
						GestureRecognizerInterop::Hold desc;
						memset(&desc, sizeof(desc), 0);

						desc.Hand = m_CurrentHand;

						m_HoldCallback(GestureStage::Started, SourceKind(args.InteractionSourceKind()), desc);
					});
				}

				if (!GestureRecognizer_HoldCompleted)
				{
					GestureRecognizer_HoldCompleted = m_GestureRecognizer.HoldCompleted([=](SpatialGestureRecognizer sender, SpatialHoldCompletedEventArgs args)
					{
						GestureRecognizerInterop::Hold desc;
						memset(&desc, sizeof(desc), 0);

						desc.Hand = m_CurrentHand;

						m_HoldCallback(GestureStage::Completed, SourceKind(args.InteractionSourceKind()), desc);
					});
				}

				if (!GestureRecognizer_HoldCanceled)
				{
					GestureRecognizer_HoldCanceled = m_GestureRecognizer.HoldCanceled([=](SpatialGestureRecognizer sender, SpatialHoldCanceledEventArgs args)
					{
						GestureRecognizerInterop::Hold desc;
						memset(&desc, sizeof(desc), 0);

						desc.Hand = m_CurrentHand;

						m_HoldCallback(GestureStage::Canceled, SourceKind(args.InteractionSourceKind()), desc);
					});
				}
			}

			if ((uint32_t)SpatialGestureSettings::ManipulationTranslate & (uint32_t)m_SpatialGestureSettings)
			{
				if (!GestureRecognizer_ManipulationStarted)
				{
					GestureRecognizer_ManipulationStarted = m_GestureRecognizer.ManipulationStarted([=](SpatialGestureRecognizer sender, SpatialManipulationStartedEventArgs args)
					{
						GestureRecognizerInterop::Manipulation desc;
						memset(&desc, sizeof(desc), 0);

						desc.Hand = m_CurrentHand;

						m_ManipulationCallback(GestureStage::Started, SourceKind(args.InteractionSourceKind()), desc);
					});
				}

				if (!GestureRecognizer_ManipulationUpdated)
				{
					GestureRecognizer_ManipulationUpdated = m_GestureRecognizer.ManipulationUpdated([=](SpatialGestureRecognizer sender, SpatialManipulationUpdatedEventArgs args)
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

						m_ManipulationCallback(GestureStage::Updated, SourceKind(args.InteractionSourceKind()), desc);
					});
				}

				if (!GestureRecognizer_ManipulationCompleted)
				{
					GestureRecognizer_ManipulationCompleted = m_GestureRecognizer.ManipulationCompleted([=](SpatialGestureRecognizer sender, SpatialManipulationCompletedEventArgs args)
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

						m_ManipulationCallback(GestureStage::Completed, SourceKind(args.InteractionSourceKind()), desc);
					});
				}

				if (!GestureRecognizer_ManipulationCanceled)
				{
					GestureRecognizer_ManipulationCanceled = m_GestureRecognizer.ManipulationCanceled([=](SpatialGestureRecognizer sender, SpatialManipulationCanceledEventArgs args)
					{
						GestureRecognizerInterop::Manipulation desc;
						memset(&desc, sizeof(desc), 0);

						desc.Hand = m_CurrentHand;

						m_ManipulationCallback(GestureStage::Canceled, SourceKind(args.InteractionSourceKind()), desc);
					});
				}

			}

			if ((uint32_t)(SpatialGestureSettings::NavigationX | SpatialGestureSettings::NavigationY | SpatialGestureSettings::NavigationZ
				| SpatialGestureSettings::NavigationRailsX | SpatialGestureSettings::NavigationRailsY | SpatialGestureSettings::NavigationRailsZ) & (uint32_t)m_SpatialGestureSettings)
			{
				if (!GestureRecognizer_NavigationStarted)
				{
					GestureRecognizer_NavigationStarted = m_GestureRecognizer.NavigationStarted([=](SpatialGestureRecognizer sender, SpatialNavigationStartedEventArgs args)
					{
						GestureRecognizerInterop::Navigation desc;
						memset(&desc, sizeof(desc), 0);

						desc.Hand = m_CurrentHand;

						m_NavigationCallback(GestureStage::Started, SourceKind(args.InteractionSourceKind()), desc);
					});
				}

				if (!GestureRecognizer_NavigationUpdated)
				{
					GestureRecognizer_NavigationUpdated = m_GestureRecognizer.NavigationUpdated([=](SpatialGestureRecognizer sender, SpatialNavigationUpdatedEventArgs args)
					{
						GestureRecognizerInterop::Navigation desc;
						memset(&desc, sizeof(desc), 0);

						desc.Hand = m_CurrentHand;

						auto trans = args.NormalizedOffset();
						desc.NormalizedOffset = DirectX::XMFLOAT3(trans.x, trans.y, trans.z);

						m_NavigationCallback(GestureStage::Updated, SourceKind(args.InteractionSourceKind()), desc);
					});
				}

				if (!GestureRecognizer_NavigationCompleted)
				{
					GestureRecognizer_NavigationCompleted = m_GestureRecognizer.NavigationCompleted([=](SpatialGestureRecognizer sender, SpatialNavigationCompletedEventArgs args)
					{
						GestureRecognizerInterop::Navigation desc;
						memset(&desc, sizeof(desc), 0);

						desc.Hand = m_CurrentHand;

						auto trans = args.NormalizedOffset();
						desc.NormalizedOffset = DirectX::XMFLOAT3(trans.x, trans.y, trans.z);

						m_NavigationCallback(GestureStage::Completed, SourceKind(args.InteractionSourceKind()), desc);
					});
				}

				if (!GestureRecognizer_NavigationCanceled)
				{
					GestureRecognizer_NavigationCanceled = m_GestureRecognizer.NavigationCanceled([=](SpatialGestureRecognizer sender, SpatialNavigationCanceledEventArgs args)
					{
						GestureRecognizerInterop::Navigation desc;
						memset(&desc, sizeof(desc), 0);

						desc.Hand = m_CurrentHand;

						m_NavigationCallback(GestureStage::Canceled, SourceKind(args.InteractionSourceKind()), desc);
					});
				}

			}
		}

	public:

		GestureRecognizer(SpatialStationaryFrameOfReference StationaryReferenceFrame)
			: m_StationaryReferenceFrame(StationaryReferenceFrame)
		{
#if PLATFORM_HOLOLENS
			// Only initialize here when run on device - this will be too early to initialize when remoting.
			m_GestureRecognizer = SpatialGestureRecognizer(SpatialGestureSettings::None);
#endif
		}

		~GestureRecognizer()
		{
			Clean();
		}

		void Init(bool isHololens1)
		{
			m_bIsHololens1 = isHololens1;

			m_GestureRecognizer = SpatialGestureRecognizer(SpatialGestureSettings::None);
			Update();
		}

		void Clean()
		{
			if (m_InteractionManager)
			{
				m_InteractionManager.SourceDetected(m_SourceDetectedToken);
				m_InteractionManager.SourceLost(m_SourceLostToken);
				m_InteractionManager.InteractionDetected(m_InteractionDetectedToken);

				m_SourceDetectedToken.value = 0;
				m_SourceLostToken.value = 0;
				m_InteractionDetectedToken.value = 0;
			}

			if (m_GestureRecognizer)
			{
				m_GestureRecognizer.Tapped(GestureRecognizer_Tapped);
				m_GestureRecognizer.HoldStarted(GestureRecognizer_HoldStarted);
				m_GestureRecognizer.HoldCompleted(GestureRecognizer_HoldCompleted);
				m_GestureRecognizer.HoldCanceled(GestureRecognizer_HoldCanceled);
				m_GestureRecognizer.ManipulationStarted(GestureRecognizer_ManipulationStarted);
				m_GestureRecognizer.ManipulationUpdated(GestureRecognizer_ManipulationUpdated);
				m_GestureRecognizer.ManipulationCompleted(GestureRecognizer_ManipulationCompleted);
				m_GestureRecognizer.ManipulationCanceled(GestureRecognizer_ManipulationCanceled);
				m_GestureRecognizer.NavigationStarted(GestureRecognizer_NavigationStarted);
				m_GestureRecognizer.NavigationUpdated(GestureRecognizer_NavigationUpdated);
				m_GestureRecognizer.NavigationCompleted(GestureRecognizer_NavigationCompleted);
				m_GestureRecognizer.NavigationCanceled(GestureRecognizer_NavigationCanceled);

				GestureRecognizer_Tapped = winrt::event_token();
				GestureRecognizer_HoldStarted = winrt::event_token();
				GestureRecognizer_HoldCompleted = winrt::event_token();
				GestureRecognizer_HoldCanceled = winrt::event_token();
				GestureRecognizer_ManipulationStarted = winrt::event_token();
				GestureRecognizer_ManipulationUpdated = winrt::event_token();
				GestureRecognizer_ManipulationCompleted = winrt::event_token();
				GestureRecognizer_ManipulationCanceled = winrt::event_token();
				GestureRecognizer_NavigationStarted = winrt::event_token();
				GestureRecognizer_NavigationUpdated = winrt::event_token();
				GestureRecognizer_NavigationCompleted = winrt::event_token();
				GestureRecognizer_NavigationCanceled = winrt::event_token();

				m_GestureRecognizer = nullptr;
			}

		}

		void Reset()
		{
			if (m_GestureRecognizer)
			{
				m_GestureRecognizer.TrySetGestureSettings(SpatialGestureSettings::None);
			}
			m_CurrentHand = HMDHand::AnyHand;

			m_SpatialGestureSettings = SpatialGestureSettings::None;
			m_TapCallback = nullptr;
			m_HoldCallback = nullptr;
			m_ManipulationCallback = nullptr;
			m_NavigationCallback = nullptr;
		}

		void Update()
		{
			try
			{
				UpdateCallbacks();

				UpdateGestureSubscriptions();
			}
			catch (winrt::hresult_error const&)
			{
			}
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
			m_SpatialGestureSettings |= SpatialGestureSettings::Tap | SpatialGestureSettings::DoubleTap;
			m_TapCallback = callback;

			Update();

			return true;
		}

		bool SubscribeHold(HoldCallback callback)
		{
			m_SpatialGestureSettings |= SpatialGestureSettings::Hold;
			m_HoldCallback = callback;

			Update();

			return true;
		}

		bool SubscribeManipulation(ManipulationCallback callback)
		{
			m_SpatialGestureSettings |= SpatialGestureSettings::ManipulationTranslate;
			m_ManipulationCallback = callback;

			Update();

			return true;
		}

		bool SubscribeNavigation(NavigationCallback callback, unsigned int settings)
		{
			m_SpatialGestureSettings |= SpatialGestureSettings(settings);
			m_NavigationCallback = callback;

			Update();

			return true;
		}

	};
}