/*
 * Manages the available connections (note the file is misnamed)
 *
 * mICQ Copyright (C) © 2001,2002,2003 Rüdiger Kuhlmann
 *
 * mICQ is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 dated June, 1991.
 *
 * mICQ is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * In addition, as a special exception permission is granted to link the
 * code of this release of mICQ with the OpenSSL project's "OpenSSL"
 * library, and distribute the linked executables.  You must obey the GNU
 * General Public License in all respects for all of the code used other
 * than "OpenSSL".  If you modify this file, you may extend this exception
 * to your version of the file, but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your version
 * of this file.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * $Id: connection.c,v 1.8 2004/12/21 19:57:32 kuhlmann Exp $
 */

#include "micq.h"
#include "connection.h"
#include "preferences.h"
#include "util_ui.h"
#include "tcp.h"
#if HAVE_NETDB_H
#include <netdb.h>
#endif
#if HAVE_WINSOCK2_H
#include <winsock2.h>
#endif
#include <assert.h>

#define ConnectionListLen 10

typedef struct ConnectionList_s ConnectionList;

struct ConnectionList_s
{
    ConnectionList *more;
    Connection     *conn[ConnectionListLen];
};


static ConnectionList slist = { NULL, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0} };

/*
 * Creates a new session.
 * Returns NULL if not enough memory available.
 */
#undef ConnectionC
Connection *ConnectionC (UWORD type DEBUGPARAM)
{
    ConnectionList *cl;
    Connection *conn;
    int i, j;
    
    cl = &slist;
    i = j = 0;
    while (cl->conn[ConnectionListLen - 1] && cl->more)
        cl = cl->more, j += ConnectionListLen;
    if (cl->conn[ConnectionListLen - 1])
    {
        cl->more = calloc (1, sizeof (ConnectionList));
        
        if (!cl->more)
            return NULL;

        cl = cl->more;
        j += ConnectionListLen;
    }
    while (cl->conn[i])
        i++, j++;

    if (!(conn = calloc (1, sizeof (Connection))))
        return NULL;
    
    conn->our_local_ip   = 0x7f000001;
    conn->our_outside_ip = 0x7f000001;
    conn->status = STATUS_OFFLINE;
    conn->sok = -1;
    conn->type = type;
    conn->flags = CONN_CONFIGURED;

    Debug (DEB_CONNECT, "<=== %p[%d] create %d", conn, j, type);

    return cl->conn[i] = conn;
}

/*
 * Clones an existing session, while blanking out some values.
 * Returns NULL if not enough memory available.
 */
#undef ConnectionClone
Connection *ConnectionClone (Connection *conn, UWORD type DEBUGPARAM)
{
    Connection *child;
    
    child = ConnectionC (type DEBUGFOR);
    if (!child)
        return NULL;

    child->parent = conn;
    child->cont   = conn->cont;
    child->flags  = 0;
    
    Debug (DEB_CONNECT, "<=*= %p (%s) clone from %p (%s)", child, ConnectionType (child), conn, ConnectionType (conn));

    return child;
}

/*
 * Returns the n-th session.
 */
Connection *ConnectionNr (int i)
{
    ConnectionList *cl;
    
    for (cl = &slist; cl && i >= ConnectionListLen; cl = cl->more)
        i -= ConnectionListLen;
    
    if (!cl)
        return NULL;
    
    return cl->conn[i];
}

/*
 * Finds a session of given type and/or given uin.
 * Actually, you may specify TYPEF_* here that must all be set.
 * The parent is the session this one has to have as parent.
 */
Connection *ConnectionFind (UWORD type, const Contact *cont, const Connection *parent)
{
    ConnectionList *cl;
    Connection *conn;
    int i;
    
    if (parent)
    {
        if (cont)
        {
            for (cl = &slist; cl; cl = cl->more)
                for (i = 0; i < ConnectionListLen; i++)
                    if ((conn = cl->conn[i]) && (conn->type & type) == type && conn->cont == cont && conn->parent == parent)
                        return conn;
        }
        else
            for (cl = &slist; cl; cl = cl->more)
                for (i = 0; i < ConnectionListLen; i++)
                    if ((conn = cl->conn[i]) && (conn->type & type) == type && (conn->connect & CONNECT_OK) && conn->parent == parent)
                        return conn;
    }
    else
    {
        if (cont)
        {
            for (cl = &slist; cl; cl = cl->more)
                for (i = 0; i < ConnectionListLen; i++)
                    if ((conn = cl->conn[i]) && (conn->type & type) == type && conn->cont == cont)
                        return conn;
        }
        else
        {
            for (cl = &slist; cl; cl = cl->more)
                for (i = 0; i < ConnectionListLen; i++)
                    if ((conn = cl->conn[i]) && (conn->type & type) == type && (conn->connect & CONNECT_OK))
                        return conn;
            for (cl = &slist; cl; cl = cl->more)
                for (i = 0; i < ConnectionListLen; i++)
                    if ((conn = cl->conn[i]) && (conn->type & type) == type)
                        return conn;
        }
    }
    return NULL;
}

/*
 * Finds a session of given type and/or given uin.
 * Actually, you may specify TYPEF_* here that must all be set.
 * The parent is the session this one has to have as parent.
 */
Connection *ConnectionFindUIN (UWORD type, UDWORD uin)
{
    ConnectionList *cl;
    Connection *conn;
    int i;
    
    assert (type && uin);

    for (cl = &slist; cl; cl = cl->more)
        for (i = 0; i < ConnectionListLen; i++)
            if ((conn = cl->conn[i]) && (conn->type & type) == type && conn->uin == uin)
                return conn;

    return NULL;
}

/*
 * Finds the index of this session.
 */
UDWORD ConnectionFindNr (Connection *conn)
{
    ConnectionList *cl;
    int i, j;

    if (!conn)
        return -1;

    for (i = 0, cl = &slist; cl; cl = cl->more)
        for (j = i; i < j + ConnectionListLen; i++)
            if (cl->conn[i % ConnectionListLen] == conn)
                return i;
    return -1;
}

/*
 * Closes and removes a session.
 */
#undef ConnectionD
void ConnectionD (Connection *conn DEBUGPARAM)
{
    ConnectionList *cl;
    Connection *clc;
    int i, j, k;
    
    if ((i = ConnectionFindNr (conn)) == -1)
        return;

    Debug (DEB_CONNECT, "===> %p[%d] (%s) closing...", conn, i, ConnectionType (conn));

    if (conn->close)
        conn->close (conn);

    if ((i = ConnectionFindNr (conn)) == -1)
        return;

    if (conn->sok != -1)
        sockclose (conn->sok);
    conn->sok     = -1;
    conn->connect = 0;
    conn->parent  = NULL;
    if (conn->server)
        free (conn->server);
    conn->server  = NULL;

    for (cl = &slist; cl; cl = cl->more)
        for (j = 0; j < ConnectionListLen; j++)
            if ((clc = cl->conn[j]) && clc->assoc == conn)
                clc->assoc = NULL;

    for (cl = &slist; cl; cl = cl->more)
        for (j = 0; j < ConnectionListLen; j++)
            if ((clc = cl->conn[j]) && clc->parent == conn)
            {
                clc->parent = NULL;
                ConnectionD (clc DEBUGFOR);
                cl = &slist;
                j = -1;
            }

    QueueCancel (conn);

    for (k = 0, cl = &slist; cl; cl = cl->more)
    {
        for (i = 0; i < ConnectionListLen; i++, k++)
            if (cl->conn[i] == conn)
                break;
        if (i < ConnectionListLen)
            break;
    }
    
    while (cl)
    {
        for (j = i; j + 1 < ConnectionListLen; j++)
            cl->conn[j] = cl->conn[j + 1];
        cl->conn[j] = cl->more ? cl->more->conn[0] : NULL;
        i = 0;
        cl = cl->more;
    }

    Debug (DEB_CONNECT, "===> %p[%d] closed.", conn, k);

    free (conn);
}

/*
 * Returns a string describing the session's type.
 */
const char *ConnectionType (Connection *conn)
{
    switch (conn->type) {
        case TYPE_SERVER:
            return i18n (1889, "server");
        case TYPE_SERVER_OLD:
            return i18n (1744, "server (v5)");
        case TYPE_MSGLISTEN:
            return i18n (1947, "listener");
        case TYPE_MSGDIRECT:
            return i18n (1890, "peer-to-peer");
        case TYPE_FILELISTEN:
            return i18n (2089, "file listener");
        case TYPE_FILEDIRECT:
            return i18n (2090, "file peer-to-peer");
        case TYPE_FILE:
            return i18n (2067, "file io");
        case TYPE_REMOTE:
            return i18n (2225, "scripting");
        default:
            return i18n (1745, "unknown");
    }
}

/*
 * Query an option for a contact group
 */
val_t ConnectionPrefVal (Connection *conn, UDWORD flag)
{
    val_t res = 0;
    if (conn->contacts && OptGetVal (&conn->contacts->copts, flag, &res))
        return res;
    if (OptGetVal (&prG->copts, flag, &res))
        return res;
    return 0;
}

