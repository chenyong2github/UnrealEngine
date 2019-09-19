// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

import {TickJournal} from '../robo/tick-journal'

const MEASUREMENT = 'robomerge';
const FLUSH_ANALYTICS_INTERVAL_SECONDS = 5*60;

interface LoginCounts {success: number, fail: number, error: number}

class JournalAccumulator {
	bbJournals = new Map<string, TickJournal>()
	unmonitored = 0
	ticks = 0

	add(addend: Map<string, TickJournal>) {
		for (const [branchBot, journal] of addend.entries()) {
			let accumJournal = this.bbJournals.get(branchBot)
			if (!accumJournal) {
				accumJournal = {merges: 0, conflicts: 0, integrationErrors: 0, syntaxErrors: 0, monitored: false}
				this.bbJournals.set(branchBot, accumJournal)
			}

			// do accumulate
			if (journal.monitored) {
				accumJournal.merges += journal.merges
				accumJournal.conflicts += journal.conflicts
				accumJournal.integrationErrors += journal.integrationErrors
				accumJournal.syntaxErrors += journal.syntaxErrors
			}
			else {
				++this.unmonitored
			}
		}
		this.ticks += addend.size
	}

	reset() {
		this.bbJournals.clear()
		this.unmonitored = 0
		this.ticks = 0
	}
}

export class Analytics {

	constructor(hostInfo: string) {
		this._linePrefix = `${MEASUREMENT},host=${hostInfo.replace(/\W/g, '')},`;

		this.flushInterval = setInterval(() => {
			const lines = [];

			for (const [bot, accumulator] of this.accumulators.entries()) {
				const botPrefix = this._linePrefix + `bot=${bot},`;

				if (accumulator.unmonitored !== 0) {
					lines.push(botPrefix + `event=unmonitored value=${accumulator.unmonitored}`);
				}

				if (accumulator.ticks !== 0) {
					lines.push(botPrefix + `event=tick value=${accumulator.ticks}`);
				}

				for (const [branchBot, bbActivity] of accumulator.bbJournals.entries()) {
					const prefix = botPrefix + `branchbot=${bot}_${branchBot},`;

					if (bbActivity.merges !== 0) {
						lines.push(prefix + `event=merge value=${bbActivity.merges}`);
					}

					if (bbActivity.conflicts !== 0) {
						lines.push(prefix + `event=conflict value=${bbActivity.conflicts}`);
					}
				}
			}

			for (const key in this.loginAttempts) {
				const val: number = (this.loginAttempts as any)[key]
				if (val !== 0) {
					lines.push(this._linePrefix + `event=login,result=${key} value=${val}`)
				}
			}

			this.loginAttempts = {
				success: 0,
				fail: 0,
				error: 0
			}

			for (const [procName, usage] of this.memUsage) {
				lines.push(this._linePrefix + `event=mem,proc=${procName} value=${usage}`)
			}

			this.memUsage.clear()

			if (lines.length !== 0) {
				this._post(lines.join('\n'));
			}

			// clear all the buffers
			this.accumulators = new Map;

		}, FLUSH_ANALYTICS_INTERVAL_SECONDS * 1000);

	}

	reportActivity(bot: string, activity: Map<string, TickJournal>) {
		let accumulator = this.accumulators.get(bot);
		if (!accumulator) {
			accumulator = new JournalAccumulator();
			this.accumulators.set(bot, accumulator);
		}
		accumulator.add(activity);
	}

	sendDeployerTick() {
		this._post(`${this._linePrefix}event=deployer-tick value=1`);
	}

	reportEmail(numSent: number, numIntendedRecipients: number) {
		const lines = [this._linePrefix + `event=email value=${numSent}`];
		if (numIntendedRecipients > numSent) {
			lines.push(this._linePrefix + `event=noemail value=${numIntendedRecipients - numSent}`);
		}

		this._post(lines.join('\n'));
	}

	reportLoginAttempt(result: string) {
		switch (result) {
			case 'success': ++this.loginAttempts.success; break
			case 'fail': ++this.loginAttempts.fail; break
			case 'error': ++this.loginAttempts.error; break
		}
	}

	reportMemoryUsage(procName: string, usage: number) {
		this.memUsage.set(procName, usage)
	}

	stop() {
		clearTimeout(this.flushInterval)
	}

	private _post(_body: string) {
		/*request.post(...)*/
	}

	private _linePrefix: string;

	private accumulators = new Map<string, JournalAccumulator>();

	private loginAttempts: LoginCounts = {
		success: 0,
		fail: 0,
		error: 0
	}

	private memUsage = new Map<string, number>()

	private flushInterval: NodeJS.Timer
}
