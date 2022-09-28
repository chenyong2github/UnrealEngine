// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Amazon.EC2;
using Amazon.EC2.Model;
using Horde.Build.Agents.Pools;
using Horde.Build.Agents.Fleet;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Horde.Build.Agents;
using Horde.Build.Agents.Fleet.Providers;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Tests.Fleet
{
	[TestClass]
	public class AwsReuseFleetManagerTest : TestSetup
	{
		[TestMethod]
		public async Task ExpandOneAgent()
		{
			FakeAmazonEc2 fakeAmazonEc2 = new ();
			Instance i = fakeAmazonEc2.AddInstance(FakeAmazonEc2.StateStopped, InstanceType.M5Large);

			await ExpandPoolAsync(fakeAmazonEc2.Get(), 1, new());
			Assert.AreEqual(InstanceType.M5Large, fakeAmazonEc2.Instances[i.InstanceId].InstanceType);
			Assert.AreEqual(FakeAmazonEc2.StatePending, fakeAmazonEc2.Instances[i.InstanceId].State);
		}
		
		[TestMethod]
		public async Task ExpandWithInstanceTypeChange2()
		{
			FakeAmazonEc2 fakeAmazonEc2 = new ();
			Instance i = fakeAmazonEc2.AddInstance(FakeAmazonEc2.StateStopped, InstanceType.M5Large);

			await ExpandPoolAsync(fakeAmazonEc2.Get(), 1, new(new List<string> { InstanceType.M54xlarge }));
			Assert.AreEqual(InstanceType.M54xlarge, fakeAmazonEc2.Instances[i.InstanceId].InstanceType);
			Assert.AreEqual(FakeAmazonEc2.StatePending, fakeAmazonEc2.Instances[i.InstanceId].State);
		}

		private async Task ExpandPoolAsync(IAmazonEC2 ec2, int numRequestedInstances, AwsReuseFleetManagerSettings settings)
		{
			ILogger<AwsReuseFleetManager> logger = ServiceProvider.GetRequiredService<ILogger<AwsReuseFleetManager>>();
			IPool pool = await PoolService.CreatePoolAsync("testPool", null, true, 0, 0, sizeStrategy: PoolSizeStrategy.NoOp);
			AwsReuseFleetManager manager = new (ec2, AgentCollection, settings, logger);
			await manager.ExpandPoolAsync(pool, new List<IAgent>(), numRequestedInstances, CancellationToken.None);
		}
	}
}