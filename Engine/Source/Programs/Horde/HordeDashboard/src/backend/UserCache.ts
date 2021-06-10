
import { action, observable } from 'mobx';
import backend from '.';
import { GetUserResponse } from './Api';


class UserCache {

    @action
    setUpdated() {
        this.updated++;
    }

    @observable
    updated: number = 0;

    async getUsers(startsWith:string) {

        startsWith = startsWith.toLowerCase();
        
        let users = this.userMap.get(startsWith);

        if (users !== undefined) {
            return users;
        }

        users = [];

        try {
            users = await backend.getUsers({count: 128, includeAvatar: true, nameRegex: `^${startsWith}`});
            console.log(`Found ${users.length} users starting with ${startsWith}`);

            users = users.sort((a, b) => {
                const aname = a.name.toLowerCase();
                const bname = b.name.toLowerCase();

                if (aname < bname) return -1;
                if (aname > bname) return 1;
                return 0;
            })
            this.userMap.set(startsWith, users);
            this.setUpdated();
        } catch (reason) {
            console.error("Unable to get users", reason);
        }
        
        return users;
    }

    private userMap:Map<string, GetUserResponse[]> = new Map();

}

export default new UserCache();