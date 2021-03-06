/* $Id: oscar_base.h,v 1.2 2004/07/24 09:10:05 kuhlmann Exp $ */

#ifndef MICQ_ICQV8_FLAP_H
#define MICQ_ICQV8_FLAP_H

#define CLI_HELLO 1

#define FLAP_VER_MAJOR       5
#define FLAP_VER_MINOR      37
#define FLAP_VER_LESSER      1
#define FLAP_VER_BUILD    3828
#define FLAP_VER_SUBBUILD   85

void FlapCliHello (Connection *serv);
void FlapCliIdent (Connection *serv);
void FlapCliCookie (Connection *serv, const char *cookie, UWORD len);
void FlapCliGoodbye (Connection *serv);
void FlapCliKeepalive (Connection *serv);

void SrvCallBackFlap (Event *event);

Packet *FlapC (UBYTE channel);
void    FlapSend (Connection *serv, Packet *pak);
void    FlapPrint (Packet *pak);
void    FlapSave (Packet *pak, BOOL in);

Event *ConnectionInitServer (Connection *serv);
Connection *SrvRegisterUIN (Connection *serv, const char *pass);


#endif /* MICQ_ICQV8_FLAP_H */
