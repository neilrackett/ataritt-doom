// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:
//	Networking stuff.
//
//-----------------------------------------------------------------------------


#ifndef __D_NET__
#define __D_NET__

#include "d_player.h"


#ifdef __GNUG__
#pragma interface
#endif


//
// Network play related stuff.
// There is a data struct that stores network
//  communication related stuff, and another
//  one that defines the actual packets to
//  be transmitted.
//

#define DOOMCOM_ID		0x12345678l

// Max computers/players in a game.
#define MAXNETNODES		8


// Networking and tick handling related.
// NetUpdate builds at most BACKUPTICS/2-1 tics ahead, and TryRunTics runs
// what is built: id's 12, five tics a frame, slowed the game down below 7
// frames a second, which an Atari is. 72 lets a frame catch up 35 tics, a
// second, so the game keeps its speed down to 1 frame a second; not
// without limit, as where a tic's game logic takes longer than a tic (a
// 68000) each frame would have more to catch up. (No network here: the
// packets' size doesn't matter, and ExpandTics takes up to 64.)
#define BACKUPTICS		72

typedef enum
{
    CMD_SEND	= 1,
    CMD_GET	= 2

} command_t;


//
// Network packet data.
//
typedef struct
{
    // High bit is retransmit request.
    unsigned		checksum;
    // Only valid if NCMD_RETRANSMIT.
    byte		retransmitfrom;
    
    byte		starttic;
    byte		player;
    byte		numtics;
    ticcmd_t		cmds[BACKUPTICS];

} doomdata_t;




typedef struct
{
    // Supposed to be DOOMCOM_ID?
    long		id;
    
    // DOOM executes an int to execute commands.
    short		intnum;		
    // Communication between DOOM and the driver.
    // Is CMD_SEND or CMD_GET.
    short		command;
    // Is dest for send, set by get (-1 = no packet).
    short		remotenode;
    
    // Number of bytes in doomdata to be sent
    short		datalength;

    // Info common to all nodes.
    // Console is allways node 0.
    short		numnodes;
    // Flag: 1 = no duplication, 2-5 = dup for slow nets.
    short		ticdup;
    // Flag: 1 = send a backup tic in every packet.
    short		extratics;
    // Flag: 1 = deathmatch.
    short		deathmatch;
    // Flag: -1 = new game, 0-5 = load savegame
    short		savegame;
    short		episode;	// 1-3
    short		map;		// 1-9
    short		skill;		// 1-5

    // Info specific to this node.
    short		consoleplayer;
    short		numplayers;
    
    // These are related to the 3-display mode,
    //  in which two drones looking left and right
    //  were used to render two additional views
    //  on two additional computers.
    // Probably not operational anymore.
    // 1 = left, 0 = center, -1 = right
    short		angleoffset;
    // 1 = drone
    short		drone;		

    // The packet data to be sent.
    doomdata_t		data;
    
} doomcom_t;



// Create any new ticcmds and broadcast to other players.
void NetUpdate (void);

// Broadcasts special packets to other players
//  to notify of game exit
void D_QuitNetGame (void);

//? how many ticks to run?
void TryRunTics (void);


#endif

//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------

