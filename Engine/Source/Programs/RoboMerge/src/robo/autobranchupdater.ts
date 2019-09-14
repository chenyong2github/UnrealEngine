// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

import * as util from 'util'
import * as fs from 'fs'
import * as path from 'path'
import {EngineInterface} from './engine-interface'
import {Perforce,Workspace} from '../common/perforce'
import {BranchDefs} from './branchdefs'

import {Bot} from './branch-interfaces'

const DISABLE = false;

interface AutoBranchUpdaterConfig {
	rootPath: string;
	workspace: Workspace
}

export class AutoBranchUpdater implements Bot {

	private p4: Perforce
	private filePath: string
	private workspace: Workspace
	
	lastCl: number

	// public: used by branchBot
	isRunning = false
	isActive = false

	static p4: Perforce
	static config: AutoBranchUpdaterConfig
	static initialCl: number

	constructor(private engine: EngineInterface) {
		this.p4 = AutoBranchUpdater.p4!

		const config = AutoBranchUpdater.config
		this.filePath = `${config.rootPath}/${engine.filename}`
		this.workspace = config.workspace
		this.lastCl = AutoBranchUpdater.initialCl
	}

	static async init(deps: any, config: AutoBranchUpdaterConfig) {
		AutoBranchUpdater.p4 = deps.p4
		AutoBranchUpdater.config = config

		util.log(`Finding most recent branch spec changelist from ${config.rootPath}`)
		var bsRoot = config.rootPath + '/...'
		const change = await AutoBranchUpdater.p4.latestChange(bsRoot)
		if (change === null)
			throw new Error(`Unable to query for most recent branch specs CL`)

		AutoBranchUpdater.initialCl = change.change

		util.log(`Syncing branch specs at CL ${change.change}`);
		let workspaceDir : string = path.resolve(config.workspace.directory);
		if (!fs.existsSync(workspaceDir)) {
			util.log(`Creating local branchspec directory ${workspaceDir}`)
			fs.mkdirSync(workspaceDir);
		}
		await AutoBranchUpdater.p4.sync(config.workspace, `${bsRoot}@${change.change}`, [Perforce.FORCE]);
	}
	
	async start() {
		this.isRunning = true;
		util.log(`Began monitoring ${this.engine.branchMap.botname} branch specs at CL ${this.lastCl}`);
	}

	async tickAsync() {
		let change;
		try {
			change = await this.p4.latestChange(this.filePath);
		}
		catch (err) {
			util.log('Branch specs: error while querying P4 for changes: ' + err.toString());
			return;
		}

		if (change !== null && change.change > this.lastCl) {
			await this.p4.sync(this.workspace, `${this.filePath}@${change.change}`);

			// set this to be the last changelist regardless of success - if it failed due to a broken
			// .json file, the file will have to be recommitted anyway
			this.lastCl = change.change;

			this._tryReloadBranchDefs();
		}
	}

	_tryReloadBranchDefs() {
		let branchMapText;
		try {
			branchMapText = require('fs').readFileSync(`${this.workspace.directory}/${this.engine.filename}`, 'utf8');
		}
		catch (err) {
			// @todo email author of changes!
			util.log('ERROR: failed to reload branch specs file');
			return;
		}

		const validationErrors: string[] = [];
		const result = BranchDefs.parseAndValidate(validationErrors, branchMapText);
		if (!result.branchMapDef) {
			// @todo email author of changes!
			util.log('ERROR: failed to parse/validate branch specs file');
			for (const error of validationErrors) {
				util.log(error);
			}
			return;
		}

		const botname = this.engine.branchMap.botname
		if (this.engine.ensureStopping()) {
			util.log(`Ignoring changes to ${botname} branch specs, since bot already stopping`);
			return;
		}

		util.log(`Stopped monitoring ${botname} branches, in preparation for reloading branch definitions`);


		// NOTE: not awaiting next tick. Waiters on this function carry on as soon as we return
		// this doesn't wait until all the branch bots have stopped, but that's ok - we're creating a new set of branch bots
		process.nextTick(async () => {
			await this.p4.sync(this.workspace, this.filePath);

			util.log(`Branch spec change detected: reloading ${botname} from CL#${this.lastCl}`);

			await this.engine.reinitFromBranchMapsObject(result.config, result.branchMapDef!);
		});
	}

	tick(next: () => (Promise<any> | void)) {
		if (DISABLE) {
			next();
			return;
		}

		this.tickAsync().then(next);
	}

	get fullName() {
		return `${this.engine.branchMap.botname} auto updater`;
	}
}
