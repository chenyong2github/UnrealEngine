// Copyright Epic Games, Inc. All Rights Reserved.

import * as util from 'util';
import {createTransport} from 'nodemailer';
import * as Mail from 'nodemailer/lib/mailer';

//import {Perforce} from './perforce';
import {Analytics} from './analytics';

const FROM = "RoboMerge <robomerge@companyname.com>";
//const SUBJECT_PREFIX = "[RoboMerge] ";
const SMTP_SERVER = 'smtp.companyname.net';

/**
 * Utility class: build up a list of to, cc, bcc, each recipient on at most one
 * list (earlier more public lists take priority)
 */

interface RecInfo {
	kind: string;
	email?: string;
}

export class Recipients {
	allRecipients = new Map<string, RecInfo>();

	constructor(...to: string[]) {
		this.addTo(...to);
	}

	addTo(...recipients: string[]) {
		for (const rec of recipients) {
			this.allRecipients.set(rec, {kind:'to'});
		}
	}

	addCc(...recipients: string[]) {
		for (const rec of recipients) {
			const existing = this.allRecipients.get(rec);
			if (!existing || existing.kind !== 'to') {
				this.allRecipients.set(rec, {kind:'cc'});
			}
		}
	}

	addBcc(...recipients: string[]) {
		for (const rec of recipients) {
			if (!this.allRecipients.has(rec)) {
				this.allRecipients.set(rec, {kind:'bcc'});
			}
		}
	}

	total() {
		return this.allRecipients.size;
	}

	toCommaSeparatedString() {
		return [...this.allRecipients.keys()].join(',');
	}


	async findEmails(findEmail: Function) {
		let emailsFound = 0;

		// could cache emails - should probably timestamp them and refresh say every day
		// possibly worth it due to glabal notifies repeatedly emailing people

		for (const [user, info] of this.allRecipients.entries()) {
			try {
				const email = await findEmail(user);
				if (email) {
					++emailsFound;
					info.email = email;
				}
			}
			catch (err) {
				util.log(`Error while resolving email for ${user}:\n${err}`);
			}
		}
		return emailsFound;
	}

	getTo(): string[] { return this._get('to'); }
	getCc(): string[] { return this._get('cc'); }
	getBcc(): string[] { return this._get('bcc'); }

	private _get(kind: string): string[] {
		return Array.from(this.allRecipients)
		.filter(([_k, v]) => v.email && v.kind === kind)
		.map(([_k, v]) => v.email!);
	}
}

export class Mailer {
	transport: Mail;
	template: string | null;
	analytics: Analytics | null;
	private readonly externalRobomergeUrl: string;

	static emailTemplateFilepath: string | null;

	constructor(analytics: Analytics) {
		if (!Mailer.emailTemplateFilepath) {
			throw new Error('Template file path must be specified before creating mailer');
		}

		this.externalRobomergeUrl = process.env["ROBO_EXTERNAL_URL"] || "https://robomerge";

		try {
			this.template = require('fs').readFileSync(Mailer.emailTemplateFilepath, 'utf8');
		}
		catch (err) {
			this.template = null;
			util.log(err);
		}

		if (this.template) {
			// create reusable transporter object using the default SMTP transport
			this.transport = createTransport({
				host: SMTP_SERVER,
				port: 25, // secure port
				secure: false, // no ssl
				tls: {rejectUnauthorized: false}
			});

			this.analytics = analytics;
		}
	}

	static _escapeForHtml(s: string) {
		return s.replace(/&/g, "&amp;")
			.replace(/</g, "&lt;")
			.replace(/>/g, "&gt;");
	}

	async sendEmail(recipients: Recipients, subject: string, intro: string, message: string, botname: string, findEmail: Function) {
		if (!this.transport) {
			util.log("Email not configured. Suppressing email.");
			util.log(`${subject}\n${message}`);
			return;
		}

		const totalToSend = await recipients.findEmails(findEmail);

		const andFinally = () => {
			this.analytics!.reportEmail(totalToSend, recipients.total());
		};

		if (totalToSend === 0) {
			andFinally();
			return;
		}

		const mail: Mail.Options = {
			subject: '[RoboMerge] ' + subject,
			from: FROM,
			html: this.template!
				.replace('${intro}', Mailer._escapeForHtml(intro))
				.replace('${message}', Mailer._escapeForHtml(message))
				.replace('${botname}', botname)
				.replace('${robomergeRootUrl}', this.externalRobomergeUrl)
		};

		const to = recipients.getTo();
		if (to.length !== 0) {
			mail.to = to;
		}

		const cc = recipients.getCc();
		if (cc.length !== 0) {
			mail.cc = cc;
		}

		const bcc = recipients.getBcc();
		if (bcc.length !== 0) {
			mail.bcc = bcc;
		}

		await this.transport.sendMail(mail)
		.catch(err => util.log('Sending email failed: ' + err.toString()))
		.then(andFinally);
	}
}

