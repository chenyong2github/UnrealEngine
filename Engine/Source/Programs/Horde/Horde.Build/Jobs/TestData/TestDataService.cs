// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Streams;
using Horde.Build.Utilities;

namespace Horde.Build.Jobs.TestData
{
	
	using StreamId = StringId<IStream>;
	using TestId = ObjectId<ITest>;
	using TestSuiteId = ObjectId<ITestSuite>;
	using TestMetaId = ObjectId<ITestMeta>;
	using TestRefId = ObjectId<ITestDataRef>;

	/// <summary>
	/// Device management service
	/// </summary>
	public sealed class TestDataService : IDisposable
	{
		
		readonly ITestDataCollection _testData;		

		/// <summary>
		/// Device service constructor
		/// </summary>
		public TestDataService(ITestDataCollection testData)
		{
			_testData = testData;		
		}

		/// <inheritdoc/>
		public void Dispose()
		{		
		}


		/// <summary>
		/// Find test streams
		/// </summary>
		/// <param name="streamIds"></param>
		/// <returns></returns>
		public async Task<List<ITestStream>> FindTestStreams(StreamId[] streamIds)
		{

			return await _testData.FindTestStreams(streamIds);			
		}

		/// <summary>
		/// Find tests
		/// </summary>
		/// <param name="testIds"></param>
		/// <returns></returns>
		public async Task<List<ITest>> FindTests(TestId[] testIds)
		{
			return await _testData.FindTests(testIds);
		}

		/// <summary>
		/// Find test suites
		/// </summary>
		/// <param name="suiteIds"></param>
		/// <returns></returns>
		public async Task<List<ITestSuite>> FindTestSuites(TestSuiteId[] suiteIds)
		{
			return await _testData.FindTestSuites(suiteIds);
		}

		/// <summary>
		/// Find test meta data
		/// </summary>
		/// <param name="projectNames"></param>
		/// <param name="platforms"></param>
		/// <param name="configurations"></param>
		/// <param name="buildTargets"></param>
		/// <param name="rhi"></param>
		/// <param name="metaIds"></param>
		/// <returns></returns>
		public async Task<List<ITestMeta>> FindTestMeta(string[]? projectNames = null, string[]? platforms = null, string[]? configurations = null, string[]? buildTargets = null, string? rhi = null, TestMetaId[]? metaIds = null)
		{
			return await _testData.FindTestMeta(projectNames, platforms, configurations, buildTargets, rhi, metaIds);
		}

		/// <summary>
		/// Gets test data refs
		/// </summary>
		/// <param name="streamIds"></param>
		/// <param name="metaIds"></param>
		/// <param name="testIds"></param>
		/// <param name="suiteIds"></param>
		/// <param name="minCreateTime"></param>
		/// <param name="maxCreateTime"></param>
		/// <param name="minChange"></param>
		/// <param name="maxChange"></param>
		/// <returns></returns>
		public async Task<List<ITestDataRef>> FindTestRefs(StreamId[] streamIds, TestMetaId[] metaIds, string[]? testIds = null, string[]? suiteIds = null, DateTime? minCreateTime = null, DateTime? maxCreateTime = null, int? minChange = null, int? maxChange = null)
		{
			TestId[]? tids = testIds?.ConvertAll(x => new TestId(x));
			TestSuiteId[]? sids = suiteIds?.ConvertAll(x => new TestSuiteId(x));

			return await _testData.FindTestRefs(streamIds, metaIds, tids, sids, minCreateTime, maxCreateTime, minChange, maxChange);
		}

		/// <summary>
		/// Find test details
		/// </summary>
		/// <param name="ids"></param>
		/// <returns></returns>
		public async Task<List<ITestDataDetails>> FindTestDetails(TestRefId[] ids)
		{
			return await _testData.FindTestDetails(ids);
		}
	}
}