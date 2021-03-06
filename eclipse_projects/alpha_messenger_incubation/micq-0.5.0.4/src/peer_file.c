/*
 * This file handles incoming and outgoing file requests.
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
 * $Id: peer_file.c,v 1.71 2005/05/06 21:40:47 kuhlmann Exp $
 */

#include "micq.h"

#ifdef ENABLE_PEER2PEER

#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include "preferences.h"
#include "connection.h"
#include "packet.h"
#include "util_ui.h"
#include "util_io.h"
#include "util_rl.h"
#include "util.h"
#include "icq_response.h"
#include "tcp.h"
#include "conv.h"

static void PeerFileDispatchClose   (Connection *ffile);
static void PeerFileDispatchDClose  (Connection *ffile);
static void PeerFileIODispatchClose (Connection *ffile);
static void PeerFileDispatchW       (Connection *fpeer);
static BOOL PeerFileError (Connection *fpeer, UDWORD rc, UDWORD flags);

#define FAIL(x) { err = x; break; }
#define PeerFileClose TCPClose

/*
 * Create a new file listener unless one already exists.
 */
Connection *PeerFileCreate (Connection *serv)
{
    Connection *flist;
    
    ASSERT_ANY_SERVER(serv);
    
    if (!serv->assoc || serv->assoc->version < 6)
        return NULL;
    
    if ((flist = ConnectionFind (TYPE_FILELISTEN, 0, serv)))
        return flist;
    
    flist = ConnectionClone (serv, TYPE_FILELISTEN);
    if (!flist)
        return NULL;

    if (prG->verbose)
        rl_printf (i18n (2519, "Opening file listener connection at %slocalhost%s:%s%ld%s... "),
                  COLQUOTE, COLNONE, COLQUOTE, serv->assoc->pref_port, COLNONE);

    flist->our_seq  = -1;
    flist->version  = serv->assoc->version;
    flist->cont     = serv->assoc->cont;
    flist->port     = serv->assoc->pref_port;
    flist->dispatch = &TCPDispatchMain;
    flist->close    = &PeerFileDispatchClose;
    
    UtilIOConnectTCP (flist);
    
    return flist;
}

/*
 * Handles a timeout.
 */

void PeerFileTO (Event *event)
{
    QueueEnqueue (event);
    QueueRelease (event);
    event->callback = NULL;
}

/*
 * Handles user reaction to incoming file request
 */
void PeerFileUser (UDWORD seq, Contact *cont, const char *reason, Connection *serv)
{
    Event *event;

    if (!(event = QueueDequeue2 (serv, QUEUE_USERFILEACK, seq, cont)))
    {
        rl_printf (i18n (2258, "No pending incoming file transfer request for %s with (sequence %ld) found.\n"),
                  cont ? cont->nick : "<?>", seq);
    }
    else
    {
        if (reason)
            event->opt = OptSetVals (event->opt, CO_FILEACCEPT, 0, CO_REFUSE, reason, 0);
        else
            event->opt = OptSetVals (event->opt, CO_FILEACCEPT, 1, 0);
        QueueEnqueue (event);
        QueueRelease (event);
    }
}

/*
 * Handles an incoming file request.
 */
UBYTE PeerFileIncAccept (Connection *list, Event *event)
{
    Connection *flist, *fpeer, *serv;
    Opt *opt;
    Contact *cont;
    UDWORD opt_bytes, opt_acc;
    const char *opt_files;

    if (!OptGetVal (event->opt, CO_BYTES, &opt_bytes))
        opt_bytes = 0;
    if (!OptGetStr (event->opt, CO_MSGTEXT, &opt_files))
        opt_files = "";

    ASSERT_MSGLISTEN(list);
    
    serv  = list->parent;
    cont  = event->cont;
    flist = PeerFileCreate (serv);

    if ((event->wait && !OptGetVal (event->wait->opt, CO_FILEACCEPT, &opt_acc))
        || !opt_acc || !cont || !flist
        || !(fpeer = ConnectionClone (flist, TYPE_FILEDIRECT)))
    {
        const char *txt;
        opt = OptSetVals (NULL, CO_MSGTEXT, opt_files, 0);
        if (!OptGetStr (event->wait->opt, CO_REFUSE, &txt))
            txt = "";
        IMIntMsg (cont, serv, NOW, STATUS_OFFLINE, INT_FILE_REJING, txt, opt);
        return FALSE;
    }
    ASSERT_FILELISTEN (flist);
    ASSERT_FILEDIRECT (fpeer);
    
    fpeer->port      = 0;
    fpeer->ip        = 0;
    fpeer->connect   = 0;
    fpeer->version   = serv->assoc->version;
    s_repl (&fpeer->server, NULL);
    fpeer->cont      = cont;
    fpeer->len       = opt_bytes;
    fpeer->done      = 0;
    fpeer->close     = &PeerFileDispatchDClose;
    fpeer->reconnect = &TCPDispatchReconn;

    opt = OptSetVals (NULL, CO_BYTES, opt_bytes, CO_MSGTEXT, opt_files, 0);
    IMIntMsg (cont, serv, NOW, STATUS_OFFLINE, INT_FILE_ACKING, "", opt);
    
    return TRUE;
}

/*
 * Checks the file request response.
 */
BOOL PeerFileAccept (Connection *peer, UWORD status, UDWORD port)
{
    Connection *flist, *fpeer;
    
    flist = PeerFileCreate (peer->parent->parent);
    fpeer = ConnectionFind (TYPE_FILEDIRECT, peer->cont, flist);
    
    if (!flist || !fpeer || !port || (status == TCP_ACK_REFUSE))
    {
        if (fpeer)
            TCPClose (fpeer);

        return 0;
    }

    ASSERT_MSGDIRECT(peer);
    ASSERT_FILELISTEN(flist);
    ASSERT_FILEDIRECT(fpeer);
    
    fpeer->connect  = 0;
    fpeer->our_seq  = 0;
    fpeer->port     = port;
    fpeer->ip       = peer->ip;
    s_repl (&fpeer->server, s_ip (fpeer->ip));
    fpeer->close    = &PeerFileDispatchDClose;
    fpeer->reconnect = &TCPDispatchReconn;
    
    if (prG->verbose)
        rl_printf (i18n (2520, "Opening file transfer connection to %s:%s%ld%s... \n"),
                  s_wordquote (fpeer->server), COLQUOTE, fpeer->port, COLNONE);

    TCPDispatchConn (fpeer);
    
    return 1;
}

/*
 * Close a file listener.
 */
static void PeerFileDispatchClose (Connection *flist)
{
    flist->connect = 0;
    PeerFileClose (flist);
}

/*
 * Close a file transfer connection.
 */
static void PeerFileDispatchDClose (Connection *fpeer)
{
    fpeer->connect = 0;
    PeerFileClose (fpeer);
    ConnectionD (fpeer);
    ReadLinePromptReset ();
}

/*
 * Close a file i/o connection.
 */
static void PeerFileIODispatchClose (Connection *ffile)
{
    if (ffile->sok != -1)
        close (ffile->sok);
    ffile->sok = -1;
    ffile->connect = 0;
    ReadLinePromptReset ();
}

/*
 * Dispatches incoming packets on the file transfer connection.
 */
void PeerFileDispatch (Connection *fpeer)
{
    Contact *cont;
    Packet *pak;
    int err = 0;
    
    ASSERT_FILEDIRECT (fpeer);
    assert (fpeer->cont);
    
    if (!(pak = UtilIOReceiveTCP (fpeer)))
        return;

    if (prG->verbose & DEB_PACKTCP)
        TCPPrint (pak, fpeer, FALSE);

    cont = fpeer->cont;

    switch (PacketRead1 (pak))
    {
        strc_t name, text;
        UDWORD len, off, nr, speed;

        case 0:
                   PacketRead4 (pak); /* EMPTY */
            nr   = PacketRead4 (pak); /* COUNT */
            len  = PacketRead4 (pak); /* BYTES */
            speed= PacketRead4 (pak); /* SPEED */
            name = PacketReadL2Str (pak, NULL); /* NICK  */
            PacketD (pak);
            
            rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
            rl_printf (i18n (2161, "Receiving %ld files with total size %ld bytes at speed %lx from %s.\n"),
                     nr, len, speed, ConvFromCont (name, cont));
            
            if (len != fpeer->len)
            {
                rl_printf ("FIXME: byte len different than in file request: requested %ld, sending %ld.\n",
                         fpeer->len, len);
                fpeer->len = len;
            }
            
            pak = PeerPacketC (fpeer, 1);
            PacketWrite4 (pak, 64);
            PacketWriteLNTS (pak, cont->nick);
            PeerPacketSend (fpeer, pak);
            PacketD (pak);
            return;
        
        case 1:
            speed = PacketRead4 (pak); /* SPEED */
            name  = PacketReadL2Str (pak, NULL); /* NICK  */
            PacketD (pak);
            
            rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
            rl_printf (i18n (2170, "Sending file at speed %lx to %s.\n"), speed, s_wordquote (ConvFromCont (name, cont)));
            
            fpeer->our_seq = 1;
            QueueRetry (fpeer, QUEUE_PEER_FILE, cont);
            return;
            
        case 2:
                   PacketRead1 (pak); /* EMPTY */
            name = PacketReadL2Str (pak, NULL);
            text = PacketReadL2Str (pak, NULL);
            len  = PacketRead4 (pak);
                   PacketRead4 (pak); /* EMPTY */
                   PacketRead4 (pak); /* SPEED */
            off  = 0;
            PacketD (pak);
            
            {
                Connection *ffile = ConnectionClone (fpeer, TYPE_FILE);
                char buf[200], *p;
                int pos = 0;
                struct stat finfo;

                assert (ffile);
                pos = snprintf (buf, sizeof (buf), "%sfiles" _OS_PATHSEPSTR "%ld" _OS_PATHSEPSTR,
                                PrefUserDir (prG), cont->uin);
                snprintf (buf + pos, sizeof (buf) - pos, "%s", ConvFromCont (name, cont));
                for (p = buf + pos; *p; p++)
                    if (*p == '/')
                        *p = '_';
                finfo.st_size = 0;
                if (!stat (buf, &finfo))
                    if ((UDWORD)finfo.st_size < len)
                        off = finfo.st_size;
                fpeer->assoc = ffile;
                ffile->sok = open (buf, O_CREAT | O_WRONLY | (off ? O_APPEND : O_TRUNC), 0660);
                if (ffile->sok == -1)
                {
                    int rc = errno;
                    if (rc == ENOENT)
                    {
                        mkdir (s_sprintf ("%sfiles", PrefUserDir (prG)), 0700);
                        mkdir (s_sprintf ("%sfiles" _OS_PATHSEPSTR "%ld", PrefUserDir (prG), cont->uin), 0700);
                        ffile->sok = open (buf, O_CREAT | O_WRONLY | (off ? O_APPEND : O_TRUNC), 0660);
                    }
                    if (ffile->sok == -1)
                    {
                        rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
                        rl_printf (i18n (2083, "Cannot open file %s: %s (%d).\n"),
                                 buf, strerror (rc), rc);
                        ConnectionD (fpeer);
                        return;
                    }
                }
                ffile->connect = CONNECT_OK;
                ffile->len = len;
                ffile->done = off;
                ffile->close = &PeerFileIODispatchClose;

                rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
                rl_printf (i18n (2162, "Receiving file %s (%s) with %ld bytes as %s.\n"),
                         name->txt, text->txt, len, buf);
            }
            pak = PeerPacketC (fpeer, 3);
            PacketWrite4 (pak, off);
            PacketWrite4 (pak, 0);
            PacketWrite4 (pak, 64);
            PacketWrite4 (pak, 1);
            PeerPacketSend (fpeer, pak);
            PacketD (pak);
            return;

        case 3:
            off = PacketRead4 (pak);
                  PacketRead4 (pak); /* EMPTY */
                  PacketRead4 (pak); /* SPEED */
            nr  = PacketRead4 (pak); /* NR */
            PacketD (pak);
            
            rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
            rl_printf (i18n (2163, "Sending file %ld at offset %ld.\n"),
                     nr, off);
            
            err = lseek (fpeer->assoc->sok, off, SEEK_SET);
            if (err == -1)
            {
                err = errno;
                rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
                rl_printf (i18n (2084, "Error while seeking to offset %ld: %s (%d).\n"),
                         off, strerror (err), err);
                TCPClose (fpeer);
                return;
            }
            fpeer->assoc->done = off;
            fpeer->assoc->connect = CONNECT_OK;
            
            QueueRetry (fpeer, QUEUE_PEER_FILE, cont);
            return;
            
        case 4:
            rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
            rl_printf (i18n (2169, "File transfer aborted by peer (%d).\n"),
                     PacketRead1 (pak));
            PacketD (pak);
            PeerFileClose (fpeer);
            return;

        case 5:
            rl_printf ("FIXME: Ignoring speed change to %d.\n",
                     PacketRead1 (pak));
            PacketD (pak);
            return;

        case 6:
            if (fpeer->assoc->done + pak->len - 1 > fpeer->assoc->len)
            {
                rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
                rl_printf (i18n (2165, "The peer sent more bytes (%ld) than the file length (%ld).\n"),
                         fpeer->assoc->done + pak->len - 1, fpeer->assoc->len);
                PacketD (pak);
                TCPClose (fpeer);
                return;
            }
            if (pak->len <= 1)
            {
                PacketD (pak);
                return;
            }
            len = write (fpeer->assoc->sok, pak->data + 1, pak->len - 1);
            if (len + 1 != pak->len)
            {
                rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
                rl_printf (i18n (2575, "Error writing to file (%lu bytes written out of %u).\n"), len, pak->len - 1);
                PacketD (pak);
                TCPClose (fpeer);
                return;
            }
            fpeer->assoc->done += len;
            if (fpeer->assoc->len == fpeer->assoc->done)
            {
                ReadLinePromptReset ();
                rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
                rl_print  (i18n (2166, "Finished receiving file.\n"));
#if HAVE_FSYNC
                fsync (fpeer->assoc->sok);
#endif
                close (fpeer->assoc->sok);
                fpeer->assoc->sok = -1;
            }
            else if (fpeer->assoc->len)
            {
                ReadLinePromptUpdate (s_sprintf ("[%s%ld %02d%%%s] %s%s",
                              COLINCOMING, fpeer->assoc->done, (int)((100.0 * fpeer->assoc->done) / fpeer->assoc->len),
                              COLNONE, COLSERVER, i18n (2467, "mICQ>")));
            }
            PacketD (pak);
            return;
        default:
            rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
            rl_print  (i18n (2167, "Error - unknown packet.\n"));
            rl_print  (s_dump (pak->data, pak->len));
            PacketD (pak);
            PeerFileClose (fpeer);
    }
    if ((prG->verbose & DEB_TCP) && err)
    {
        rl_printf ("%s %s: %d\n", s_now, i18n (2029, "Protocol error on peer-to-peer connection"), err);
        PeerFileClose (fpeer);
    }
}

static void PeerFileDispatchW (Connection *fpeer)
{
    Packet *pak = fpeer->outgoing;
    
    ASSERT_FILEDIRECT(fpeer);
    
    fpeer->outgoing = NULL;
    fpeer->connect = CONNECT_OK | CONNECT_SELECT_R;
    fpeer->dispatch = &PeerFileDispatch;
    fpeer->assoc->connect = CONNECT_OK;

    if (!UtilIOSendTCP (fpeer, pak))
        TCPClose (fpeer);
    
    QueueRetry (fpeer, QUEUE_PEER_FILE, fpeer->cont);
}

static BOOL PeerFileError (Connection *fpeer, UDWORD rc, UDWORD flags)
{
    Contact *cont = fpeer->cont;
    
    ASSERT_FILEDIRECT(fpeer);
    
    switch (rc)
    {
        case EPIPE:
            if (fpeer->close)
                fpeer->close (fpeer);
            return 1;
        case EAGAIN:
            if (flags == CONNERR_WRITE)
            {
                fpeer->connect = CONNECT_OK | CONNECT_SELECT_W;
                fpeer->dispatch = &PeerFileDispatchW;
                fpeer->assoc->connect = CONNECT_OK | 1;
                if (fpeer->assoc->len)
                    ReadLinePromptUpdate (s_sprintf ("[%s%ld:%02d%%%s] %s%s",
                                  COLCONTACT, fpeer->assoc->done, (int)((100.0 * fpeer->assoc->done) / fpeer->assoc->len),
                                  COLNONE, COLSERVER, i18n (2467, "mICQ>")));
                return 1;
            }
    }
    return 0;
}

void PeerFileResend (Event *event)
{
    Contact *cont;
    Connection *fpeer = event->conn;
    Packet *pak;
    Event *event2;
    int rc;
    const char *opt_text;
    
    if (!fpeer)
    {
        EventD (event);
        return;
    }
    
    ASSERT_FILEDIRECT (fpeer);

    cont = event->cont;
    assert (cont);
    
    if (!OptGetStr (event->opt, CO_FILENAME, &opt_text))
        opt_text = "";

    if (event->attempts >= MAX_RETRY_P2PFILE_ATTEMPTS || (!event->pak && !event->seq))
    {
        rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
        rl_printf (i18n (2168, "File transfer #%ld (%s) dropped after %ld attempts because of timeout.\n"),
                 event->seq, opt_text, event->attempts);
        TCPClose (fpeer);
    }
    else if (!(fpeer->connect & CONNECT_MASK))
    {
        rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
        rl_printf (i18n (2072, "File transfer #%ld (%s) canceled because of closed connection.\n"),
                 event->seq, opt_text);
    }
    else if (~fpeer->connect & CONNECT_OK)
    {
        if (!event->seq)
            event->attempts++;
        event->due = time (NULL) + 20;
        QueueEnqueue (event);
        return;
    }
    else if (!event->seq)
    {
        fpeer->our_seq = 0;
        PeerPacketSend (fpeer, event->pak);
        PacketD (event->pak);
        event->pak = NULL;
    }
    else if (event->seq != fpeer->our_seq)
    {
        event->due = time (NULL) + 10;
        QueueEnqueue (event);
        return;
    }
    else if (event->pak)
    {
        Connection *ffile;
        struct stat finfo;
        
        PeerPacketSend (fpeer, event->pak);
        PacketD (event->pak);
        event->pak = NULL;
        QueueEnqueue (event);
        
        ffile = ConnectionClone (fpeer, TYPE_FILE);
        fpeer->assoc = ffile;

        if (stat (opt_text, &finfo))
        {
            rc = errno;
            rl_printf (i18n (2071, "Couldn't stat file %s: %s (%d)\n"),
                      s_wordquote (opt_text), strerror (rc), rc);
        }
        ffile->len = finfo.st_size;

        ffile->sok = open (opt_text, O_RDONLY);
        if (ffile->sok == -1)
        {
            int rc = errno;
            rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
            rl_printf (i18n (2083, "Cannot open file %s: %s (%d).\n"),
                      opt_text, strerror (rc), rc);
            TCPClose (fpeer);
            ConnectionD (ffile);
            ConnectionD (fpeer);
            return;
        }
        ffile->close = &PeerFileIODispatchClose;
        return;
    }
    else if (!fpeer->assoc || fpeer->assoc->connect != CONNECT_OK)
    {
        event->attempts++;
        event->due = time (NULL) + 3;
        QueueEnqueue (event);
        return;
    }
    else
    {
        int len = 0;

        pak = PeerPacketC (fpeer, 6);
        len = read (fpeer->assoc->sok, pak->data + 1, 2048);
        if (len == -1)
        {
            len = errno;
            rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
            rl_printf (i18n (2086, "Error while reading file %s: %s (%d).\n"),
                      opt_text, strerror (len), len);
            TCPClose (fpeer);
        }
        else
        {
            pak->len += len;
            fpeer->assoc->done += len;
            fpeer->error = &PeerFileError;
            PeerPacketSend (fpeer, pak);
            PacketD (pak);

            if (len > 0)
            {
                if (fpeer->assoc->len)
                    ReadLinePromptUpdate (s_sprintf ("[%s%ld %02d%%%s] %s%s",
                                  COLCONTACT, fpeer->assoc->done, (int)((100.0 * fpeer->assoc->done) / fpeer->assoc->len),
                                  COLNONE, COLSERVER, i18n (2467, "mICQ>")));
                else
                    ReadLinePromptUpdate (s_sprintf ("[%s%ld%s] %s%s",
                                  COLCONTACT, fpeer->assoc->done,
                                  COLNONE, COLSERVER, i18n (2467, "mICQ>")));
                event->attempts = 0;
                QueueEnqueue (event);
                return;
            }

            ReadLinePromptReset ();
            rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
            rl_printf (i18n (2087, "Finished sending file %s.\n"), opt_text);
            ConnectionD (fpeer->assoc);
            fpeer->our_seq++;
            event2 = QueueDequeue (fpeer, QUEUE_PEER_FILE, fpeer->our_seq);
            if (event2)
            {
                QueueEnqueue (event2);
                QueueRetry (fpeer, QUEUE_PEER_FILE, fpeer->cont);
                return;
            }
            else
            {
                rl_printf ("%s %s%*s%s ", s_now, COLCONTACT, uiG.nick_len + s_delta (cont->nick), cont->nick, COLNONE);
                rl_printf (i18n (2088, "Finished sending all %d files.\n"), fpeer->our_seq - 1);
                ConnectionD (fpeer);
            }
        }
    }
    EventD (event);
}

#endif /* ENABLE_PEER2PEER */
