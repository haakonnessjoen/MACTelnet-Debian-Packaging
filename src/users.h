/*
	Mac-Telnet - Connect to RouterOS or mactelnetd devices via MAC address
	Copyright (C) 2010, Håkon Nessjøen <haakon.nessjoen@gmail.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#ifndef _USERS_H
#define _USERS_H 1

#define MT_CRED_LEN 100
#define MT_CRED_MAXNUM 128

struct mt_credentials {
	char username[MT_CRED_LEN];
	char password[MT_CRED_LEN];

	struct mt_credentials *prev;
	struct mt_credentials *next;
};

extern struct mt_credentials *mt_users;

extern void read_userfile();
struct mt_credentials *find_user(char *username);

#endif
