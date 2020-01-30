// Copyright Epic Games, Inc. All Rights Reserved.

import * as fs from 'fs';
import * as util from 'util';
import * as path from 'path';
import { BranchMap } from './branchmap';

const version: Number = 1.0

const jsonlint: any = require('jsonlint')

function readFileToString(filename: string) {
	try {
		return fs.readFileSync(filename, 'utf8');
	}
	catch (e) {
		return null;
	}
}

interface FieldToSet {
	name: string
	value: any
	overrideContext?: string
}

export class Context {
	settings: Settings;
	name: string;
	object: { [key: string]: any };

	constructor(settings: Settings, name: string) {
		this.settings = settings;
		this.name = name;
		const settingsObject: any = settings.object;
		this.object = settingsObject[name];
		if (this.object === undefined) {
			this.object = settingsObject[name] = {};
		}
		if (typeof(this.object) !== "object") {
			throw new Error("invalid settings for "+name);
		}
	}

	getInt(name: string, dflt?: number) {
		let val = parseInt(this.get(name));
		return isNaN(val) ? (dflt === undefined ? 0 : dflt) : val;
	}
	getFloat(name: string, dflt?: number) {
		let val = parseFloat(this.get(name));
		return isNaN(val) ? (dflt === undefined ? 0 : dflt) : val;
	}
	getString(name: string, dflt?: string) {
		let val = this.get(name);
		if (val === undefined || val === null) {
			return (dflt === undefined ? null : dflt);
		}
		return val.toString();
	}
	get(name: string) {
		return (<any>this.object)[name];
	}
	set(name: string, value: any) {
		(<any>this.object)[name] = value;
		this.settings._saveObject();
	}

	setMultiple(fields: FieldToSet[]) {
		for (const field of fields) {
			const context = field.overrideContext ? this.settings.getContext(field.overrideContext) : this
			context.set(field.name, field.value)
		}

		this.settings._saveObject()
	}
}

export class Settings {
	filename: string;
	enableSave: boolean;
	object: { [key: string]: any }

	constructor(botname: string, branchmap: BranchMap) {
		// figure out the file name
		const settingsPath = process.platform === 'win32' ? 'D:/ROBO' : path.resolve(process.env.HOME || "/root", '.robomerge');

		this.filename = `${settingsPath}/${botname.toLowerCase()}.settings.json`;

		// see if we should enable saves
		this.enableSave = !process.env["NOSAVE"];
		if (this.enableSave)
			util.log(`Reading settings from ${this.filename}`);
		else
			util.log("Saving config has been disabled by NOSAVE environment variable");

		// load the object from disk
		let filebits = readFileToString(this.filename);
		if (filebits)
			this.object = jsonlint.parse(filebits);
		else {
			// Create "empty" settings object, but include latest version so we don't needless enter migration code
			this.object = {
				version: version
			};
		}

		// Originally we did not version configuration.
		// If we have no version, assume it needs all migrations
		if (!this.object.version) {
			console.log("No version found in settings data.")
			this.object.version = 0
		} 
		// Ensure we have a version number and not version string
		else if (typeof(this.object.version) === "string") {
			const versionNum = parseFloat(this.object.version)
			if (versionNum !== NaN) {
				this.object.version = versionNum
			} else {
				throw new Error(`Found version field in settings file, but it does not appear to be a number: "${this.object.version}"`)
			}
		}

		// Check if we need to run pre-1.0 data migrations
		// https://jira.it.epicgames.net/browse/FORT-123856
		if (this.object.version < 1.0) {
			this.migrateSlackMessageDataKeys(branchmap)
			this.object.version = 1.0
		}

		// We're up to date!
		this.object.version = version

		// if we can't write we want to fail now
		this._saveObject();
	}

	// Pre-1.0, Slack Messages were indexed by "Changelist:Branch".
	// With 1.0, they are now indexed with "Changelist:Branch:Channel Name"
	private migrateSlackMessageDataKeys(branchmap: BranchMap) {
		console.log("Migrating old Slack Message keys in settings data.")
		// Check for slack messages
		if (this.object.notifications && this.object.notifications.slackMessages) {
			let slackMessages : {[key: string]: any} = this.object.notifications.slackMessages
			// Regex to find a CL # paired with a branch
			const oldKeyRegex = /^(\d+):([^:]+)$/
			const newKeyRegex = /^(\d+):([^:]+):([^:]+)$/

			// Iterate over slack message key names and change their format
			for (let key in slackMessages) {
				let matches = oldKeyRegex.exec(key)
				if (!matches) {
					// We didn't match the expected old style.
					// Maybe we match the new style, and this configuration was partially migrated
					let newMatches = newKeyRegex.exec(key)
					if (newMatches) {
						// Ah! We matched the format of the new key. Let's skip this.
						console.log(`Skipping key "${key}"`)
						continue
					}

					// Didn't match either format. What gives? Get a dev to triage it.
					throw new Error(`Error migrating slack message data for message "${key}"`)
				}

				// Re-add the contents of this message to a new value
				const newKey = `${matches[1]}:${matches[2]}:${branchmap.config.slackChannel}`
				console.log(`Migrating key "${key}" to "${newKey}"`)
				this.object.notifications.slackMessages[newKey] = {
					...slackMessages[key],
					"channel": branchmap.config.slackChannel
				}

				// Remove the old one
				delete slackMessages[key]
			}
		}
		console.log("Slack Message key migration complete.")
	}

	getContext(name: string) {
		return new Context(this, name);
	}

	_saveObject() {
		if (this.enableSave) {
			let filebits = JSON.stringify(this.object, null, '  ');
			fs.writeFileSync(this.filename, filebits, "utf8");
		}
	}
}
