// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using HordeServer.Collections;
using HordeServer.Collections.Impl;
using HordeServer.Models;
using HordeServer.Services;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Bson;

namespace HordeServerTests
{
	[TestClass]
	public class ServiceAccountCollectionTests : DatabaseIntegrationTest
	{
		private readonly IServiceAccountCollection ServiceAccounts;
		private readonly IServiceAccount ServiceAccount;
		
		public ServiceAccountCollectionTests()
		{
			DatabaseService DatabaseService = GetDatabaseService();
			ServiceAccounts = new ServiceAccountCollection(DatabaseService);
			ServiceAccount = ServiceAccounts.AddAsync(ObjectId.GenerateNewId().ToString(), new List<string> {"myclaim###myvalue"}, "mydesc").Result;
		}

		[TestMethod]
		public async Task Add()
		{
			IServiceAccount Sa = await ServiceAccounts.AddAsync("addToken", new List<string> {"myclaim###myvalue"}, "mydesc");
			Assert.AreEqual("addToken", Sa.SecretToken);
			Assert.AreEqual(1, Sa.GetClaims().Count);
			Assert.AreEqual("myvalue", Sa.GetClaims()[0].Value);
			Assert.IsTrue(Sa.Enabled);
			Assert.AreEqual("mydesc", Sa.Description);
		}
		
		[TestMethod]
		public async Task Get()
		{
			IServiceAccount Sa = (await ServiceAccounts.GetAsync(ServiceAccount.Id))!;
			Assert.AreEqual(ServiceAccount, Sa);
		}
		
		[TestMethod]
		public async Task GetBySecretToken()
		{
			IServiceAccount Sa = (await ServiceAccounts.GetBySecretTokenAsync(ServiceAccount.SecretToken))!;
			Assert.AreEqual(ServiceAccount, Sa);
		}
		
		[TestMethod]
		public async Task Update()
		{
			List<string> NewClaims = new List<string> {{"newclaim1###newvalue1"}, {"newclaim2###newvalue2"}};
			await ServiceAccounts.UpdateAsync(ServiceAccount.Id, "newtoken", NewClaims, false, "newdesc");
			IServiceAccount GetSa = (await ServiceAccounts.GetAsync(ServiceAccount.Id))!;
			
			Assert.AreEqual("newtoken", GetSa.SecretToken);
			Assert.AreEqual(2, GetSa.GetClaims().Count);
			Assert.AreEqual("newvalue1", GetSa.GetClaims()[0].Value);
			Assert.AreEqual("newvalue2", GetSa.GetClaims()[1].Value);
			Assert.AreEqual(false, GetSa.Enabled);
			Assert.AreEqual("newdesc", GetSa.Description);
		}
		
		[TestMethod]
		public async Task Delete()
		{
			await ServiceAccounts.DeleteAsync(ServiceAccount.Id);
			IServiceAccount? Result = await ServiceAccounts.GetAsync(ServiceAccount.Id);
			Assert.IsNull(Result);
		}
	}
}