// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace HordeServerTests
{
    [TestClass]
    public class PoolsControllerTest : ControllerIntegrationTest
    {
        [TestMethod]
        public async Task GetPoolsTest()
        {
            var Res = await client.GetAsync("/api/v1/pools");
            Res.EnsureSuccessStatusCode();
            var jsonData = await Res.Content.ReadAsStringAsync();
            Assert.AreEqual("[]", jsonData);
        }
    }
}