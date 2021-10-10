// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;
using HordeServer.Authentication;
using HordeServer.Collections;
using HordeServer.Collections.Impl;
using HordeServer.Models;
using HordeServer.Services;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Primitives;
using Microsoft.Extensions.WebEncoders.Testing;
using Microsoft.Net.Http.Headers;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace HordeServerTests
{
	[TestClass]
	public class ServiceAccountAuthTest : DatabaseIntegrationTest
	{
		private readonly IServiceAccountCollection ServiceAccounts;
		private readonly IServiceAccount ServiceAccount;

		public ServiceAccountAuthTest()
		{
			DatabaseService DatabaseService = GetDatabaseService();
			ServiceAccounts = new ServiceAccountCollection(DatabaseService);
			ServiceAccount = ServiceAccounts.AddAsync("mytoken", new List<string> {"myclaim###myvalue", "foo###bar"}, "mydesc").Result;
 		}

		private async Task<ServiceAccountAuthHandler> GetAuthHandlerAsync(string? HeaderValue)
		{
			ServiceAccountAuthOptions Options = new ServiceAccountAuthOptions();
			ILoggerFactory LoggerFactory = new LoggerFactory();
			ServiceAccountAuthHandler Handler = new ServiceAccountAuthHandler(new TestOptionsMonitor<ServiceAccountAuthOptions>(Options), LoggerFactory, new UrlTestEncoder(), new SystemClock(), ServiceAccounts);
			AuthenticationScheme Scheme = new AuthenticationScheme(ServiceAccountAuthHandler.AuthenticationScheme, "ServiceAccountAuth", Handler.GetType());
			
			HttpContext HttpContext = new DefaultHttpContext();
			if (HeaderValue != null)
			{
				HttpContext.Request.Headers.Add(HeaderNames.Authorization, new StringValues(HeaderValue));	
			}
			
			await Handler.InitializeAsync(Scheme, HttpContext);

			return Handler;
		}

		[TestMethod]
		public async Task ValidToken()
		{
			ServiceAccountAuthHandler Handler = await GetAuthHandlerAsync("ServiceAccount mytoken");
			AuthenticateResult Result = await Handler.AuthenticateAsync();
			Assert.IsTrue(Result.Succeeded);
			
			Handler = await GetAuthHandlerAsync("ServiceAccount   mytoken        ");
			Result = await Handler.AuthenticateAsync();
			Assert.IsTrue(Result.Succeeded);
		}
		
		[TestMethod]
		public async Task InvalidToken()
		{
			ServiceAccountAuthHandler Handler = await GetAuthHandlerAsync("ServiceAccount doesNotExist");
			AuthenticateResult Result = await Handler.AuthenticateAsync();
			Assert.IsFalse(Result.Succeeded);
			
			// Valid token but bad prefix
			Handler = await GetAuthHandlerAsync("SriceAcct mytoken");
			Result = await Handler.AuthenticateAsync();
			Assert.IsFalse(Result.Succeeded);
		}
		
		[TestMethod]
		public async Task NoResult()
		{
			// Valid token but bad prefix
			ServiceAccountAuthHandler Handler = await GetAuthHandlerAsync("Bogus mytoken");
			AuthenticateResult Result = await Handler.AuthenticateAsync();
			Assert.IsFalse(Result.Succeeded);
			Assert.IsTrue(Result.None);
			
			// No Authorization header at all
			Handler = await GetAuthHandlerAsync(null);
			Result = await Handler.AuthenticateAsync();
			Assert.IsFalse(Result.Succeeded);
			Assert.IsTrue(Result.None);
		}
		
		[TestMethod]
		public async Task Claims()
		{
			ServiceAccountAuthHandler Handler = await GetAuthHandlerAsync("ServiceAccount mytoken");
			AuthenticateResult Result = await Handler.AuthenticateAsync();
			Assert.IsTrue(Result.Succeeded);
			Assert.AreEqual(3, Result.Ticket!.Principal.Claims.Count());
			Assert.AreEqual(ServiceAccountAuthHandler.AuthenticationScheme, Result.Ticket.Principal.FindFirst(ClaimTypes.Name)!.Value);
			Assert.AreEqual("myvalue", Result.Ticket!.Principal.FindFirst("myclaim")!.Value);
			Assert.AreEqual("bar", Result.Ticket!.Principal.FindFirst("foo")!.Value);
		}
	}
}