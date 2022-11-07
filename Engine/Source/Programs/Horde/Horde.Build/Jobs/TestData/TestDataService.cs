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
		/// Finds test streams
		/// </summary>
		/// <param name="streamIds"></param>
		/// <returns></returns>
		public async Task<List<TestStream>> FindTestStreams(StreamId[] streamIds)
		{
			return await _testData.FindTestStreams(streamIds);
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
		public async Task<List<ITestDataRef>> FindTestRefs(StreamId[] streamIds, string[]? metaIds = null, string[]? testIds = null, string[]? suiteIds = null, DateTime? minCreateTime = null, DateTime? maxCreateTime = null, int? minChange = null, int? maxChange = null)
		{
			TestMetaId[]? mids = metaIds?.ConvertAll(x => new TestMetaId(x)).ToArray();
			TestId[]? tids = testIds?.ConvertAll(x => new TestId(x)).ToArray();
			TestSuiteId[]? sids = suiteIds?.ConvertAll(x => new TestSuiteId(x)).ToArray();

			return await _testData.FindTestRefs(streamIds, mids, tids, sids, minCreateTime, maxCreateTime, minChange, maxChange);
		}
	}
}
