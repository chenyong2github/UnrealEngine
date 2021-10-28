// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.RegularExpressions;

namespace EpicGames.Core
{
	/// <summary>
	/// Utility methods for certificates
	/// </summary>
	public static class CertificateUtils
	{
		/// <summary>
		/// Create a self signed cert for the agent
		/// </summary>
		/// <param name="DnsName"></param>
		/// <param name="FriendlyName">Friendly name for the certificate</param>
		/// <returns></returns>
		public static byte[] CreateSelfSignedCert(string DnsName, string FriendlyName)
		{
			DnsName = Regex.Replace(DnsName, @"^[a-zA-Z]+://", "");
			DnsName = Regex.Replace(DnsName, "/.*$", "");

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
					Certificate.FriendlyName = FriendlyName;
					byte[] PrivateCertData = Certificate.Export(X509ContentType.Pkcs12); // Note: Need to reimport this to use immediately, otherwise key is ephemeral
					return PrivateCertData;
				}
			}
		}
	}
}
