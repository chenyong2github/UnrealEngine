// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using Microsoft.Extensions.Options;

namespace Horde.Build.Jobs.TestData
{
	
	using StreamId = StringId<IStream>;
	using TestId = ObjectId<ITest>;
	using TestSuiteId = ObjectId<ITestSuite>;
	using TestMetaId = ObjectId<ITestMeta>;

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
		/// <param name="metaIds"></param>
		/// <param name="minCreateTime"></param>
		/// <param name="maxCreateTime"></param>
		/// <returns></returns>
		public async Task<List<TestStream>> FindTestStreams(StreamId[] streamIds, TestMetaId[] metaIds, DateTime minCreateTime, DateTime? maxCreateTime = null)
		{
			if (maxCreateTime == null)
			{
				maxCreateTime = DateTime.UtcNow;
			}

			return await _testData.FindTestStreams(streamIds, metaIds, minCreateTime, maxCreateTime.Value);
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
			TestId[]? tids = testIds?.ConvertAll(x => new TestId(x)).ToArray();
			TestSuiteId[]? sids = suiteIds?.ConvertAll(x => new TestSuiteId(x)).ToArray();

			return await _testData.FindTestRefs(streamIds, metaIds, tids, sids, minCreateTime, maxCreateTime, minChange, maxChange);
		}
	}
}
