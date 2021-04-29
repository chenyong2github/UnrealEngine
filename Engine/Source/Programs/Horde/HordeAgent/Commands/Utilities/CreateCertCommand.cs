using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using EpicGames.Core;

namespace HordeAgent.Commands.Certs
{
	/// <summary>
	/// Creates a certificate that can be used for server/agent SSL connections
	/// </summary>
	[Command("CreateCert", "Creates a self-signed certificate that can be used for server/agent gRPC connections")]
	class CreateCertCommand : Command
	{
		[CommandLine("-Server=")]
		public string? Server = null;

		[CommandLine("-DnsName=")]
		public string? DnsName;

		[CommandLine("-PrivateCert=")]
		public string? PrivateCertFile;

		[CommandLine("-Environment=")]
		public string? Environment = "Development";

		/// <summary>
		/// Main entry point for this command
		/// </summary>
		/// <param name="Logger"></param>
		/// <returns>Async task</returns>
		public override Task<int> ExecuteAsync(ILogger Logger)
		{
			if (DnsName == null)
			{
				IConfigurationRoot Config = new ConfigurationBuilder()
					.SetBasePath(Directory.GetCurrentDirectory())
					.AddJsonFile("appsettings.json", true)
					.AddJsonFile($"appsettings.{Environment}.json", true)
					.Build();

				AgentSettings Settings = new AgentSettings();
				Config.GetSection("Horde").Bind(Settings);

				if(Server != null)
				{
					Settings.Server = Server;
				}

				DnsName = Settings.GetCurrentServerProfile().Url;
				DnsName = Regex.Replace(DnsName, @"^[a-zA-Z]+://", "");
				DnsName = Regex.Replace(DnsName, "/.*$", "");
			}

			if (PrivateCertFile == null)
			{
				FileReference SolutionFile = FileReference.Combine(new FileReference(Assembly.GetExecutingAssembly().Location).Directory, "..", "..", "..", "..", "Horde.sln");
				if (!FileReference.Exists(SolutionFile))
				{
					Logger.LogError("The -PrivateCertFile=... arguments must be specified when running outside the default build directory");
					return Task.FromResult(1);
				}
				PrivateCertFile = FileReference.Combine(SolutionFile.Directory, "HordeServer", "Certs", $"ServerToAgent-{Environment}.pfx").FullName;
			}

			Logger.LogInformation("Creating certificate for {DnsName}", DnsName);
			using (RSA Algorithm = RSA.Create(2048))
			{
				X500DistinguishedName distinguishedName = new X500DistinguishedName($"CN={DnsName}");
				CertificateRequest Request = new CertificateRequest(distinguishedName, Algorithm, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);

				Request.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, true));
				Request.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.KeyEncipherment | X509KeyUsageFlags.DigitalSignature, true));
				Request.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(new OidCollection { new Oid("1.3.6.1.5.5.7.3.1") }, true));

				SubjectAlternativeNameBuilder AlternativeNameBuilder = new SubjectAlternativeNameBuilder();
				AlternativeNameBuilder.AddDnsName(DnsName);
				Request.CertificateExtensions.Add(AlternativeNameBuilder.Build(true));

				// NB: MacOS requires 825 days or fewer (https://support.apple.com/en-us/HT210176)
				using (X509Certificate2 Certificate = Request.CreateSelfSigned(new DateTimeOffset(DateTime.UtcNow.AddDays(-1)), new DateTimeOffset(DateTime.UtcNow.AddDays(800))))
				{
					Certificate.FriendlyName = "Horde Server";

					byte[] PrivateCertData = Certificate.Export(X509ContentType.Pkcs12); // Note: Need to reimport this to use immediately, otherwise key is ephemeral
					Logger.LogInformation("Writing private cert: {PrivateCert}", new FileReference(PrivateCertFile).FullName);
					File.WriteAllBytes(PrivateCertFile, PrivateCertData);

					Logger.LogInformation("Certificate thumbprint is {Thumbprint}", Certificate.Thumbprint);
					Logger.LogInformation("Add this thumbprint to list of trusted servers in appsettings.json to trust this server.");
				}
			}
			return Task.FromResult(0);
		}
	}
}
