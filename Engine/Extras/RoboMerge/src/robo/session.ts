// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
import * as fs from 'fs'
import * as util from 'util'
import * as crypto from 'crypto'

import {Arg, readProcessArgs} from '../common/args'

const LdapAuth: any = require('ldapauth-fork');

export interface Credentials {
	user: string
	password: string
}

const TOKEN_VERSION = 0.6

// dev cookie key - should be overwritten by key from vault in production
const DEV_COOKIE_KEY = 'dev-cookie-key'

const ADMINS = [
	"example.username"
]

export interface AuthData {
	user: string,
	displayName: string,
	tags: Set<string> // tags to specify (e.g. fte, admin) or look up access
}

const COMMAND_LINE_ARGS: {[param: string]: (Arg<any>)} = {
	vault: {
		match: /^-vault_path=(.+)$/,
		env: 'ROBO_VAULT_PATH',
		dflt: '/vault'
	}
}

const args = readProcessArgs(COMMAND_LINE_ARGS)
if (!args) {
	process.exit(1)
}

export class Session {
	public static VAULT_PATH = args.vault

	public static onLoginAttempt: ((result: string) => void) | null = null

	static init() {
		const ldapConfig = JSON.parse(fs.readFileSync('config/ldap.cfg.json', 'utf8'))

		Session.LDAP_CONFIG = ldapConfig['server-config']

		const botGroups = ldapConfig['bot-groups']
		if (botGroups) {
			for (const botGroup of botGroups) {
				Session.BOT_GROUPS.set(botGroup.group, botGroup.tags)
			}
		}

		try {
			const vaultString = fs.readFileSync(Session.VAULT_PATH + '/vault.json', 'ascii')
			const vault = JSON.parse(vaultString)
			Session.LDAP_CONFIG.adminPassword = vault['ldap-password']
			Session.COOKIE_KEY = vault['cookie-key']
			return
		}
		catch (err) {
			util.log(`Warning (ok in dev):  ${err.toString()}`)
			Session.COOKIE_KEY = DEV_COOKIE_KEY
		}

		util.log('Warning! No vault or no LDAP creds in vault (this is ok for testing)')
	}

	static login(creds: Credentials) {
		return new Promise<string | null>((done, fail) => {
			// LdapAuth modifies the config, so copy to be clean (probably doesn't matter)
			const config: any = {}
			Object.assign(config, Session.LDAP_CONFIG)
			const auth = new LdapAuth(config)

			auth.on('error', fail)
			auth.authenticate(creds.user, creds.password, (err: any | null, userData: any) => {
				if (err) {
					if (err.name === 'InvalidCredentialsError' ||
						(typeof(err) === 'string' && err.startsWith('no such user'))) {
						if (Session.onLoginAttempt) {
							Session.onLoginAttempt('fail')
						}
						done(null)
					}
					else {
						// unknown error
						util.log(`LDAP error! - ${err}`)
						if (Session.onLoginAttempt) {
							Session.onLoginAttempt('error')
						}
						fail(err)
					}
					return
				}

				let tags = new Set<string>()

				if (userData._groups) {
					for (const groupInfo of userData._groups) {
						const groupNameMatch = groupInfo.dn.match(/CN=([^,]+),/)
						if (!groupNameMatch) {
							continue
						}
						const tagsForGroup = Session.BOT_GROUPS.get(groupNameMatch[1])
						if (tagsForGroup) {
							tags = new Set<string>([...tagsForGroup, ...tags])
						}
					}
				}

				// Add admin tags to users based on admins array
				if (ADMINS.indexOf(creds.user) > -1) {
					tags.add("admin")
				}

				const authData = {user: creds.user, displayName: userData.displayName || creds.user, tags}
				done(Session.authDataToToken(authData))

				if (Session.onLoginAttempt) {
					Session.onLoginAttempt('success')
				}
			})
		})
	}

	static tokenToAuthData(token: string): AuthData | null {
		const parts = token.replace(/-/g, '+').replace(/_/g, '/').split(':')
		if (parts.length !== 2) {
			return null
		}
		const dataBuf = Buffer.from(parts[0], 'base64')
		if (Session.macForTokenBuffer(dataBuf) != parts[1]) {
			return null
		}
		const obj = JSON.parse(dataBuf.toString('utf8'))
		if (obj.version !== TOKEN_VERSION || !obj.user || !obj.displayName || !Array.isArray(obj.tags)) {
			return null
		}
		obj.tags = new Set([...obj.tags])
		return obj as AuthData
	}

	private static macForTokenBuffer(buf: Buffer) {
		return crypto.createHmac('sha256', Session.COOKIE_KEY!).update(buf).digest('base64')
	}

	private static authDataToToken(authData: AuthData) {
		const authDataForToken: any = {}
		Object.assign(authDataForToken, authData)
		authDataForToken.version = TOKEN_VERSION
		authDataForToken.tags = [...authData.tags]
		const adBuf = Buffer.from(JSON.stringify(authDataForToken), 'utf8')
		const mac = Session.macForTokenBuffer(adBuf)
		return `${adBuf.toString('base64')}:${mac}`.replace(/\+/g, '-').replace(/\//g, '_')
	}

	private static LDAP_CONFIG: any | null = null
	private static COOKIE_KEY: string | null = null

	private static BOT_GROUPS = new Map<string, string[]>()

////////////////////////////
// ALL CODE BELOW IS TESTS

	private static _testSingleToken(testName: string, authData: AuthData, other?: AuthData) {
		const token = Session.authDataToToken(authData)
		let tokenToDecode
		if (!other) {
			tokenToDecode = token
		}
		else {
			const otherToken = Session.authDataToToken(other)

			const otherTokenParts = otherToken.replace(/-/g, '+').replace(/_/g, '\\').split(':')
			const tokenParts = token.replace(/-/g, '+').replace(/_/g, '\\').split(':')

			// hack together fake token, to check it's rejected as necessary

			tokenToDecode = `${otherTokenParts[0]}:${tokenParts[1]}`
		}

		const decoded = Session.tokenToAuthData(tokenToDecode)

		// !decoded means mismatch
		if (other) {
			if (decoded) {
				throw new Error(`${testName}: Decoded but shouldn't have!`)
			}

			// ok - correctly rejected mismatch
			console.log(`Passed '${testName}'!`)
			return
		}

		if (!decoded) {
			throw new Error(`${testName}: Failed to decode!`)
		}

		// should match now
		if (!decoded.user || !(decoded.tags instanceof Set)) {
			throw new Error(`${testName}: Invalid decoded data!`)
		}

		if (decoded.user.toLowerCase() !== authData.user.toLowerCase() ||
			![...decoded.tags].every(x => authData.tags.has(x))
		) {
			throw new Error(`${testName}: Mismatch!`)
		}

		console.log(`Passed '${testName}'`)
	}

	static _testTokenSigning() {
		const TEST_DATA: AuthData = {user: 'marlin.kingsly', displayName: '-', tags: new Set(['admin'])}
		
		Session._testSingleToken('match', TEST_DATA)
		Session._testSingleToken('mismatched user', TEST_DATA, {user: 'marlin.kngsly', displayName: '-', tags: new Set(['admin'])})
		Session._testSingleToken('mismatched tag', TEST_DATA, {user: 'marlin.kingsly', displayName: '-', tags: new Set(['admn'])})
		// actually always going to encode user as lower case
		Session._testSingleToken('mismatched user case', TEST_DATA, {user: 'Marlin.Kingsly', displayName: '-', tags: new Set(['admin'])})
		Session._testSingleToken('mismatched tag, case only', TEST_DATA, {user: 'marlin.kingsly', displayName: '-', tags: new Set(['ADMIN'])})
		Session._testSingleToken('match, no tags', {user: 'marlin.kingsly', displayName: '-', tags: new Set([])})
		// (~~~~ forces a + in base 64 which gets replaced with a -). Quite a few of the MACs generated also test replacement
		Session._testSingleToken('match, with replaced character', {user: 'marlin.kingsly', displayName: '-', tags: new Set(['~~~~'])})
	}
}

Session.init()

if (process.argv.indexOf('__TEST__') !== -1) {
	Session._testTokenSigning()
}
