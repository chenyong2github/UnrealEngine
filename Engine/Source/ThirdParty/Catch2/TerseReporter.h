// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Catch {
	class TerseReporter : public StreamingReporterBase<TerseReporter>
	{
	public:
		TerseReporter(ReporterConfig const& _config)
			: StreamingReporterBase(_config)
		{
		}

		static std::string getDescription()
		{
			return "Terse output";
		}

		virtual void assertionStarting(AssertionInfo const&) {}
		virtual bool assertionEnded(AssertionStats const& stats) {
			if (!stats.assertionResult.succeeded()) {
				const auto location = stats.assertionResult.getSourceInfo();
				std::cout << location.file << "(" << location.line << ") error\n"
					<< "\t";

				switch (stats.assertionResult.getResultType()) {
				case ResultWas::DidntThrowException:
					std::cout << "Expected exception was not thrown";
					break;

				case ResultWas::ExpressionFailed:
					std::cout << "Expression is not true: " << stats.assertionResult.getExpandedExpression();
					break;

				case ResultWas::Exception:
					std::cout << "Unexpected exception";
					break;

				default:
					std::cout << "Test failed";
					break;
				}

				std::cout << std::endl;
			}

			return true;
		}

		void sectionStarting(const SectionInfo& info) override
		{
			++sectionNesting_;
			StreamingReporterBase::sectionStarting(info);
		}

		void sectionEnded(const SectionStats& stats) override
		{
			if (--sectionNesting_ == 0) {
				totalDuration_ += stats.durationInSeconds;
			}

			StreamingReporterBase::sectionEnded(stats);
		}

		void testCaseStarting(TestCaseInfo const& testInfo) override
		{
			std::cout << testInfo.name << std::endl;
			totalDuration_ = 0;
			StreamingReporterBase::testCaseStarting(testInfo);
		}

		void testCaseEnded(const TestCaseStats& stats) override
		{
			std::cout << "\t";
			if (stats.totals.assertions.allPassed()) {
				std::cout << Colour(Colour::ResultSuccess) << "SUCCESS ( "
					<< totalDuration_ << "s )";;
			}
			else if (stats.totals.assertions.allOk()) {
				std::cout << Colour(Colour::ResultExpectedFailure) << "EXPECTED FAILURE ("
					<< totalDuration_ << "s )";;
			}
			else {
				std::cout << Colour(Colour::ResultError) << "FAILURE ("
					<< totalDuration_ << "s )";;
			}

			std::cout << std::endl;

			StreamingReporterBase::testCaseEnded(stats);
		}

		void testRunEnded(const TestRunStats& stats) override
		{
			std::cout << std::endl;

			StreamingReporterBase::testRunEnded(stats);
		}

	private:
		int sectionNesting_ = 0;
		double totalDuration_ = 0;
	};

	CATCH_REGISTER_REPORTER("terse", TerseReporter)
}
