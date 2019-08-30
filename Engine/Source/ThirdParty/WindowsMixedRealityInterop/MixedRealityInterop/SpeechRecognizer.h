// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <winrt/Windows.Media.SpeechRecognition.h>
#include "ppltasks.h"
#include <functional>

using namespace winrt::Windows::Media::SpeechRecognition;

namespace WindowsMixedReality
{
	class SpeechRecognizer
	{
	public:
		SpeechRecognizer() { }

		~SpeechRecognizer() 
		{
			StopSpeechRecognizer();
		}

		void StopSpeechRecognizer()
		{
			if (m_compileConstraintsAsyncOperation && m_compileConstraintsAsyncOperation.Status() != winrt::Windows::Foundation::AsyncStatus::Completed)
			{
				m_compileConstraintsAsyncOperation.Cancel();
			}

			if (resultsGeneratedToken.value != 0)
			{
				m_SpeechRecognizer.ContinuousRecognitionSession().ResultGenerated(resultsGeneratedToken);
				resultsGeneratedToken.value = 0;
			}

			m_SpeechRecognizer.Constraints().Clear();
			m_SpeechRecognizer.Close();

			m_SpeechRecognizer = nullptr;
			keywordMap.clear();
		}

		void AddKeyword(winrt::hstring keyword, std::function<void()> callback)
		{
			auto it = keywordMap.find(keyword);
			if (it != keywordMap.end())
			{
				keywordMap.erase(it);
			}

			keywordMap[keyword] = callback;
		}

		void StartSpeechRecognizer()
		{
			InitializeSpeechRecognizer();

			// Isolate keywords from keyword map for recognizer.
			std::vector<winrt::hstring> keywords;
			for (std::pair<winrt::hstring, std::function<void()>> pair : keywordMap)
			{
				keywords.push_back(pair.first);
			}

			SpeechRecognitionListConstraint constraint = SpeechRecognitionListConstraint(keywords);
			m_SpeechRecognizer.Constraints().Clear();
			m_SpeechRecognizer.Constraints().Append(constraint);

			m_compileConstraintsAsyncOperation = m_SpeechRecognizer.CompileConstraintsAsync();
			
			m_compileConstraintsAsyncOperation.Completed([this](winrt::Windows::Foundation::IAsyncOperation<SpeechRecognitionCompilationResult> asyncOperation, winrt::Windows::Foundation::AsyncStatus status)
			{
				if (asyncOperation.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
				{
					SpeechRecognitionCompilationResult result = asyncOperation.GetResults();
					if (result.Status() == SpeechRecognitionResultStatus::Success)
					{
						try
						{
							m_SpeechRecognizer.ContinuousRecognitionSession().StartAsync();
						}
						catch (...)
						{
							// We may see an exception if the microphone capability is not enabled.
							StopSpeechRecognizer();
						}
					}
					else
					{
						StopSpeechRecognizer();
					}
				}
				else if (asyncOperation.Status() != winrt::Windows::Foundation::AsyncStatus::Canceled)
				{
					StopSpeechRecognizer();
				}
			});
		}

		std::map<winrt::hstring, std::function<void()>> KeywordMap()
		{
			return keywordMap;
		}

	private:
		winrt::Windows::Media::SpeechRecognition::SpeechRecognizer m_SpeechRecognizer;
		winrt::Windows::Foundation::IAsyncOperation<SpeechRecognitionCompilationResult>		m_compileConstraintsAsyncOperation;
		winrt::event_token resultsGeneratedToken;
		std::map<winrt::hstring, std::function<void()>> keywordMap;

		void InitializeSpeechRecognizer()
		{
			resultsGeneratedToken = m_SpeechRecognizer.ContinuousRecognitionSession().ResultGenerated(
				[&](SpeechContinuousRecognitionSession sender, SpeechContinuousRecognitionResultGeneratedEventArgs args)
			{
				if (args.Result().Status() == SpeechRecognitionResultStatus::Success &&
					args.Result().Confidence() != SpeechRecognitionConfidence::Rejected)
				{
					winrt::hstring text = args.Result().Text();

					if (keywordMap[text] != nullptr)
					{
						// Call the callback.
						keywordMap[text]();
					}
				}
			});
		}
	};
}