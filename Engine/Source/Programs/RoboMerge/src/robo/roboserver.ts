// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
import * as util from 'util'
import * as querystring from 'querystring'

import {WebServer, Handler, RequestOpts, ensureRegExp, AppInterface, WebRequest} from '../common/webserver'
import {AuthData, Session} from './session'
import {Status} from './status'
import {Arg, readProcessArgs} from '../common/args'

// Allows instance to skip over sign-in and HTTPS

let ENVIRONMENT: {[param: string]: any}
(() => {
	const COMMAND_LINE_ARGS: {[param: string]: (Arg<any>)} = {
		devMode: {
			match: /^(-devMode)$/,
			parse: _str => true,
			env: 'ROBO_DEV_MODE',
			dflt: false
		}
	}

	ENVIRONMENT = readProcessArgs(COMMAND_LINE_ARGS)
	if (!ENVIRONMENT) {
		process.exit(1)
	}
})()

if (ENVIRONMENT.devMode) {
	util.log('WARNING! Running in DEV_MODE')
}

interface SecureRequestOpts extends RequestOpts {
	requiredTags?: string[]
}

function SecureHandler(verb: string, route: RegExp | string, opts?: SecureRequestOpts) {
	return (_target: RoboWebApp, _funcName: string, desc: PropertyDescriptor) => {
		const originalMethod = desc.value

		const func: any = async function (this: RoboWebApp, ...args: any[]) {

			const redirectResult = this.redirect(this.secure, opts && opts.requiredTags)
			if (redirectResult) {
				return redirectResult
			}
			return await originalMethod.call(this, ...args)
		}

		func.verb = verb
		func.route = ensureRegExp(route)
		if (opts) {
			func.opts = opts
		}

		desc.value = func
		return desc
	}
}

const COOKIE_REGEX = /\s*(.*?)=(.*)/
function getCookie(cookies: string[], name: string) {
	for (const cookieKV of cookies) {
		const match = cookieKV.match(COOKIE_REGEX)
		if (match && match[1].toLowerCase() === name.toLowerCase()) {
			return match[2]
		}
	}
	return null
}

interface BackendFunctions {
	sendMessage: (msg: string, args?: any[]) => any,
	getLogTail: Function | null
	getLastCrash: Function
	stopBot: Function
	startBot: Function
}

export class RoboServer {
	constructor(sendMessage: (msg: string, args?: any[]) => any, getLogTail: Function | null, getLastCrash: Function, stopBot: Function, startBot: Function) {
		this.server = new WebServer
		this.functions = {
			sendMessage: sendMessage,
			getLogTail: getLogTail,
			getLastCrash: getLastCrash,
			stopBot: stopBot,
			startBot: startBot
		}

		//this.server.addFileMapping('/', 'index.html')
		this.server.addFileMapping('/login', 'login.html', {secureOnly: true})
		this.server.addFileMapping('/js/*-asm.js', 'js/$1-asm.gz', {headers: [
			['Cache-Control', 'max-age=' + 60*60*24*7],
			['Content-Encoding', 'gzip']
		]})
		this.server.addFileMapping('/js/*.js', 'js/$1.js')
		this.server.addFileMapping('/css/*.css', 'css/$1.css')
		this.server.addFileMapping('/img/*.png', 'images/$1.png')
		this.server.addFileMapping('/img/*.jpg', 'images/$1.jpg')
		this.server.addFileMapping('/fonts/*.woff2', 'fonts/$1.woff2', {filetype: 'application/octet-stream'})

		this.server.addApp(RoboWebApp, (req: WebRequest) => new RoboWebApp(req, this.server.secure, this.functions));
	}

	open(...args: any[]) {
		return this.server.open(...args)
	}

	close() {
		return this.server.close()
	}

	isSecure() {
		return this.server.secure
	}

	private server: WebServer
	private functions: BackendFunctions
}

class RoboWebApp implements AppInterface {
	secure: boolean

	// could put access to various parts of request in an App base class?
	constructor(request: WebRequest, secure: boolean, functions: BackendFunctions) {
		this.request = request
		this.secure = secure
		this.functions = functions
	}

	@SecureHandler('GET', '/', {filetype: 'text/html'}) 
	indexPage() {
		return new Promise<string>((done, _fail) => 
			require('fs').readFile('public/index.html', 'utf8', (_err: Error, content: string) => done(content)))
	}

	@SecureHandler('GET', '/api/logs', {requiredTags: ['fte']})
	getLogs() {
		// logs are only available in IPC mode
		return this.functions.getLogTail ? this.functions.getLogTail() : '<logs not available>'
	}

	@SecureHandler('GET', '/api/last_crash')
	getLastCrash() {
		return this.functions.getLastCrash && this.functions.getLastCrash() || 'No crashes (yay!).'
	}

	@SecureHandler('POST', '/api/control/start', {requiredTags: ['fte']})
	startBot() {
		util.log('Restart requested by: ' + this.authData!.user)
		this.functions.startBot()
		return 'OK'
	}

	@SecureHandler('POST', '/api/control/stop', {requiredTags: ['fte']})
	stopBot() {
		util.log('Emergency stop requested by: ' + this.authData!.user)

		this.functions.stopBot()
		return 'OK'
	}

	// not SecureHandler, so it doesn't get redirected
	@Handler('POST', '/dologin')
	async login() {
		if (!this.request.postData) {
			return {statusCode: 400, message: 'no log-in data received'}
		}

		const creds = querystring.parse(this.request.postData)
		if (!creds.user || Array.isArray(creds.user) || !creds.password || Array.isArray(creds.password)) {
			return {statusCode: 400, message: 'invalid log-in data'}
		}
		
		const token = await Session.login({user: creds.user, password: creds.password});
		return token || {statusCode: 401, message: 'invalid credentials'};
	}

	@SecureHandler('POST', '/api/control/verbose/*')
	setVerbosity(onOff: string) {
		return this.functions.sendMessage('setVerbose', [onOff.toLowerCase() === 'on'])
	}

	@SecureHandler('GET', '/api/branches')
	async getBranches() {
		if (!this.authData) {
			throw new Error('Secure call but no auth data?')
		}

		const status: any | null = await this.sendMessage('getBranches', [])
		// Status might be null if sendMessage fails, in which case we send only auth data
		return status ? Status.fromIPC(status).getForUser(this.authData) : {
			branches: [], started: false,
			user: {
				userName: this.authData.user,
				displayName: this.authData.displayName,
				privileges: this.authData.tags
			}
		}
	}

	@SecureHandler('GET', '/api/bot/*/branch/*')
	async getBranch(botname: string, branchname: string) {
		if (!this.authData) {
			throw new Error('Secure call but no auth data?')
		}

		const branchStatus: any | null = await this.sendMessage('getBranch', [botname, branchname])

		// sendMessage succeeds
		if (branchStatus) {
			const filteredStatus = Status.fromIPC(branchStatus).getForUser(this.authData)
			
			if (filteredStatus.branches.length > 0) {
				return {
					user: {
						userName: this.authData.user,
						displayName: this.authData.displayName,
						privileges: this.authData.tags
					},
					branch: filteredStatus.branches[0]
				}
			}

			// No branches found (or authorized)
			return {
				statusCode: 400,
				message: `No branch found for bot "${botname}" and branch "${branchname}"`
			}
		} else {
			// Server error
			return {
				statusCode: 500,
				message: `Robomerge failed to retrieve branch information.`
			}
		}
	}

	@SecureHandler('POST', '/api/bot/*/branch/*/*')
	async branchOp(botname: string, branchname: string, branchOp: string) {
		const result = await this.sendMessage('doBranchOp', [botname, branchname, branchOp, this.getQueryFromSecure()])
		return result || {statusCode: 400, message: 'Bot is not running'}
	}

	@SecureHandler('GET', '/api/p4tasks')
	getP4Tasks() {
		return this.sendMessage('getp4tasks')
	}

	@SecureHandler('GET', '/debug/persistence/*')
	getPersistence(botname: string) {
		return this.sendMessage('getPersistence', [botname])
	}

	// https://localhost:4433/branchop/acknowledge?bot=TEST&branch=Main&cl=1237983421
	@SecureHandler('GET', '/branchop/*', {filetype: 'text/html'})
	async branchOpLanding(operation: string) {
		let query = this.getQueryFromSecure()
		switch (operation) {
			case "acknowledge":
				// Require these query variables: bot, branch and CL
				if (!query["bot"]) {
					return {statusCode: 400, message: `ERROR: Must specify bot for acknowledge operation.`}
				}
				if (!query["branch"]) {
					return {statusCode: 400, message: `ERROR: Must specify branch for acknowledge operation.`}
				}
				if (!query["cl"]) {
					return {statusCode: 400, message: `ERROR: Must specify changelist for acknowledge operation.`}
				}

				return new Promise<string>((done, _fail) => 
					require('fs').readFile('public/acknowledge.html', 'utf8', (_err: Error, content: string) => done(content)))

			case "skip":
				// Require these query variables: bot, branch and CL
				if (!query["bot"]) {
					return {statusCode: 400, message: `ERROR: Must specify bot for skip operation.`}
				}
				if (!query["branch"]) {
					return {statusCode: 400, message: `ERROR: Must specify branch for skip operation.`}
				}
				if (!query["cl"]) {
					return {statusCode: 400, message: `ERROR: Must specify changelist for skip operation.`}
				}

				return new Promise<string>((done, _fail) => 
					require('fs').readFile('public/skip.html', 'utf8', (_err: Error, content: string) => done(content)))

			default:
				return {statusCode: 404, message: `Unknown branch operation requested: "${operation}"`}
		}
	}

	public redirect(secureServer: boolean, requiredTags?: string[]) {
		if (secureServer) {
			const authToken = getCookie(this.getCookies(), 'auth')
			if (authToken) {
				this.authData = Session.tokenToAuthData(authToken)
			}

			if (!this.authData) {
				return {statusCode: 302, message: 'Must log in', headers: [
					['Location', '/login'],
					['Set-Cookie', `redirect_to=${this.request.url.pathname}; path=/;`]
				]}
			}
		}
		else if (ENVIRONMENT.devMode) {
			this.authData = {
				user: 'dev',
				displayName: 'Dev mode',
				tags: new Set(['fte', 'admin'])
			}
		}
		else {
			return {statusCode: 403 /* Forbidden */, message: 'Sign-in over HTTPS required'}
		}

		if (requiredTags) {
			for (const tag of requiredTags) {
				if (!this.authData.tags.has(tag)) {
					return {statusCode: 403 /* Forbidden */, message: `Tag <${tag}> required`}
				}
			}
		}

		// fine, carry on
		return null
	}

	private sendMessage(msg: string, args?: any[]) {
		return this.functions.sendMessage(msg, args)
	}

 	private getCookies() {
		return decodeURIComponent(this.request.cookies).split(';')
	}

	private getQueryFromSecure() : {[key: string]: string} {
		if (!this.authData) {
			throw new Error('Secure call but no auth data?')
		}

		const queryObj: {[key: string]: string} = {}
		for (const [key, val] of this.request.url.searchParams) {
			queryObj[key] = val
		}
		// overwrite any incoming 'who' on query string - should not be affected by client
		queryObj.who = this.authData.user
		return queryObj
	}

 	private request: WebRequest
	private authData: AuthData | null
	private functions: BackendFunctions
}
