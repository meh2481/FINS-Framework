/**
 * icmp.c
 *
 *  Created on: Mar 15, 2011 - June 22, 2011
 *      Author: Abdallah Abdallah & Mark Hutcheson
 */

#include "icmp.h"
#include "ipv4.h"

extern sem_t ICMP_to_Switch_Qsem;
extern finsQueue ICMP_to_Switch_Queue;

extern sem_t Switch_to_ICMP_Qsem;
extern finsQueue Switch_to_ICMP_Queue;


//--------------------------------------------
// We're getting an ICMP packet in. Process it
//--------------------------------------------
void ICMP_in(struct finsFrame *ff)
{
	PRINT_DEBUG("Got ICMP packet from wire.");
	//TODO: If raw socket, we should get the IP header too. I'm assuming that the IP header data will be in the actual data field,
	//      rather than taken out and stuck into the metadata. Later, we should instead decide on what the best way to implement
	//      raw sockets is. Test for this and deal with it somehow. Anyhow, here we'll strip the IP header off first thing.
	unsigned char* oldpdu = ff->dataFrame.pdu;		//Keep the old pointers, since we'll need them in some of the cases
	unsigned int   oldpdulen = ff->dataFrame.pduLength;
	ff->dataFrame.pdu = ((struct ip4_packet*) ff->dataFrame.pdu)->ip_data;	//This is block-copied from the IP4 module. I hope it works.
	ff->dataFrame.pduLength = ntohs(((struct ip4_packet*) ff->dataFrame.pdu)->ip_len);


	//First step: Check the checksum.
	if(ICMP_checksum(ff) != 0)
	{
		PRINT_DEBUG("Error in checksum of packet. Discarding...");
		return; //Discard packet if checksum is bad
	}
	PRINT_DEBUG("");
	//Second step: get the type and code from the ICMP header from the frame.
	unsigned char Type;
	unsigned char Code;
	uint16_t protocol = 0;

	//Make sure this protocol is correct ( == ICMP)
	if(metadata_readFromElement(ff->dataFrame.metaData, "protocol", &protocol) != CONFIG_FALSE)
	{ //If this fails, we'll assume that the protocol is correct
		if(ntohl(protocol) != ICMP_PROTOCOL)
		{
			PRINT_DEBUG("Protocol =/= ICMP! Discarding frame...");
			return; //Stop here
		}
	}

	//Get the type and code from the ICMP message. Easiest for me to treat it as raw data than worry about a structure and byte ordering
	//and data padding and all that mess. Probably not the best way to do this, but I doubt ICMP will change anytime soon. Especially
	//with IPv4 and ICMPv4 coming out.
	Type = ff->dataFrame.pdu[0];
	Code = ff->dataFrame.pdu[1];

	struct finsFrame* ffForward = NULL; //Used by various cases to forward packets onward to wherever they need to go

	PRINT_DEBUG("");
	//Determine what to do with this ICMP message based on its type and code
	switch(Type)
	{
	case TYPE_ECHOREPLY:
		PRINT_DEBUG("Echo Reply");
		if(Code == CODE_ECHO)
		{
			//Send to the application that created the echo request.
			//I shall make new packet, rather than changing where this packet is headed.

			//First, make this the same way as it was originally.
			ff->dataFrame.pdu = oldpdu;
			ff->dataFrame.pduLength = oldpdulen;

			if(!ICMP_copy_finsFrame(ff, ffForward))
				break;
			ffForward->destinationID.id = SOCKETSTUBID;	//TODO: will the socket stub handle this correctly? It better.
			ICMP_send_FF(ffForward);
		}
		else
			PRINT_DEBUG("ICMP_in(): Error in ICMP packet code. Dropping...");
		break;
	case TYPE_DESTUNREACH:
		PRINT_DEBUG("Destination unreachable");
		//Create a FINS control frame and send it to the UDP/TCP modules
		ICMP_create_control_error(ff, Type, Code);
		break;
	case TYPE_ECHOREQUEST:
		PRINT_DEBUG("Echo Request");
		if(Code == CODE_ECHO)
		{
			//Create an echo reply packet and send it out
			ICMP_ping_reply(ff);
		}
		else
			PRINT_DEBUG("ICMP packet code =/= echo request. Dropping...");
		break;
	case TYPE_TTLEXCEED:
		PRINT_DEBUG("TTL Exceeded");
 		//Create a FINS control frame and send it to the UDP/TCP modules
		ICMP_create_control_error(ff, Type, Code);
		break;
	default:
		//Drop the packet
		PRINT_DEBUG("The type of this received ICMP packet is currently unsupported. Dropping...");
		break;
	}
}

//----------------------------------------------------------------------------
// We're sending an ICMP packet out from the socket stub. Process it and send.
//----------------------------------------------------------------------------
void ICMP_out(struct finsFrame *ff)
{
	struct finsFrame* ffForward = NULL;

	//Since not a control frame (Otherwise this wouldn't come to this function), forward out as-is to the IPv4 module, as this'll be a raw socket.
	if(!ICMP_copy_finsFrame(ff, ffForward))
		return;
	ffForward->destinationID.id = IPV4ID;	//Send to IP handler
	ICMP_send_FF(ffForward);

	PRINT_DEBUG("Forwarding out ICMP packet.");
}

//---------------------------------------------------
// Retrieve a finsFrame from the queue and process it
//---------------------------------------------------
void ICMP_get_FF(struct finsFrame *ff)
{
	// Poll the queue constantly to see if there's anything there.
	do
	{
		sem_wait (&Switch_to_ICMP_Qsem);
		ff = read_queue(Switch_to_ICMP_Queue);
		sem_post (&Switch_to_ICMP_Qsem);
	} while (ff == NULL);

	if(ff->dataOrCtrl == CONTROL)
	{
		// send to the control frame handler
		PRINT_DEBUG("Control frame. Sending to control handler...");
		ICMP_control_handler(ff);
	}
	else if((ff->dataFrame).directionFlag == UP)	//Incoming ICMP packet (coming in from teh internets)
	{
		PRINT_DEBUG("");
		ICMP_in(ff);
	}
	else if((ff->dataFrame).directionFlag == DOWN)	//Outgoing ICMP packet (going out from us to teh internets)
	{
		PRINT_DEBUG("");
		ICMP_out(ff);
	}
}

//-----------------------------------------------
// ICMP_send_ff(): Put a finsFrame onto the queue
//-----------------------------------------------
void ICMP_send_FF(struct finsFrame *ff)
{
	int result;
	//Just queue this frame up to go
	do
	{
		sem_wait (&ICMP_to_Switch_Qsem);
		result = write_queue(ff, ICMP_to_Switch_Queue); //Should we just hope this works, or check the return value and loop? I shall loop.
		sem_post (&ICMP_to_Switch_Qsem);
	} while(result == 0);

	PRINT_DEBUG("Packet sent");
}

//-------------------------
// Start our main ICMP loop
//-------------------------
void ICMP_init()
{
	PRINT_DEBUG("ICMP Started");
	struct finsFrame *pff = NULL;
	while (1)
	{
		ICMP_get_FF(pff);
		PRINT_DEBUG("%d",(int)pff);
		//Note that we always clean up the frame, no matter what we do with it. If the frame needs to go somewhere else also, we make a copy.
		//free(pff);
	}
}

//-------------------------------------------
// Calculate the checksum of this ICMP packet
//-------------------------------------------
unsigned short ICMP_checksum(struct finsFrame * ff)
{
	int sum = 0;
	unsigned char *w = ff->dataFrame.pdu;
	int nleft = ff->dataFrame.pduLength;

	if(nleft % 2)  //Check if we've got an uneven number of bytes here, and deal with it accordingly if we do.
	{
		nleft--;  //By decrementing the number of bytes we have to add in
		sum += ((int)(w[nleft])) << 8; //And shifting these over, adding them in as if they're the high byte of a 2-byte pair
		//This is as per specification of the checksum from the RFC: "If the total length is odd, the received data is padded with one
	    // octet of zeros for computing the checksum." We don't explicitly add an octet of zeroes, but this has the same result.
	}

	while(nleft > 1) //Could also do nleft > 0 here, same difference
	{
		//Deal with the high and low words of each 16-bit value here. I tried earlier to do this 'normally' by
		//casting the pdu to unsigned short, but the little-vs-big-endian thing messed it all up. I'm just avoiding
		//the whole issue now by treating the values as high-and-low-word pairs, and bit-shifting to compensate.
		sum += (int)(*w++) << 8;  //First one is high word: shift before adding in
		sum += *w++;			  //Second one is low word: just add in
		nleft -= 2;				  //Decrement by 2, since we're taking 2 at a time
	}

	//Fully fill out the data, to be sure we have a 16-bit and _only_ 16-bit checksum
	for(;;)
	{
		sum = (sum >> 16) + (sum & 0xFFFF);  //Get the sum shifted over added into the current sum
		if(!(sum >> 16))  //Continue this until the sum shifted over is zero
			break;
	}

	PRINT_DEBUG("Checksum is: %d", ~((u_short)(sum)));
	return ~((u_short)(sum));  //Return one's complement of the sum
}

//------------------------------------------------------------------------------
// Create a ping reply message (from the ping request message) when we're pinged
//------------------------------------------------------------------------------
void ICMP_ping_reply(struct finsFrame* ff)
{
	PRINT_DEBUG("");
	//Create new dataframe, copying the old one. Do I have to do this by hand with malloc(),
	//or do either of the copy-frame-to-frame functions work at all? They look like they shouldn't, and neither of them are used anywhere.
	struct finsFrame* ffout = NULL;

	//Copy the finsFrame ff into ffout. Is there a function to do this for us? Because if there isn't, there should be. Use my function for now.
	//TODO: Creating a copy of the finsFrame and then changing the metadata may cause issues. Try something else if there are problems.
	if(!ICMP_copy_finsFrame(ff, ffout))
		return; //Stop here if it failed

	//Get source and destination IP's from finsFrame
	IP4addr IP_Dest, IP_Src;
	if(metadata_readFromElement(ffout->dataFrame.metaData, "ipsrc", &IP_Src) == CONFIG_FALSE)
	{
		PRINT_DEBUG("Missing data in FINS frame metadata: no source IP");
		return; //Stop here
	}
	if(metadata_readFromElement(ffout->dataFrame.metaData, "ipdst", &IP_Dest) == CONFIG_FALSE)
	{
		PRINT_DEBUG("Missing data in FINS frame metadata: no destination IP");
		return; //Stop here
	}

	//Write our IP to the "ipsrc". Do we actually care about doing this? Or will the ethernet stub handle this properly anyhow? Oh, well.
	//I shall do it.
	metadata_writeToElement(ffout->dataFrame.metaData, "ipsrc", &IP_Dest, CONFIG_TYPE_INT); //TODO: CONFIG_TYPE_INT, right?

	//Write the original "src IP" to the data as the destination IP
	metadata_writeToElement(ffout->dataFrame.metaData, "ipdst", &IP_Src, CONFIG_TYPE_INT);

	PRINT_DEBUG("Source IP: %d, Dest IP: %d", IP_Dest, IP_Src);
	//Make sure this goes to the right place. Is this what we have to do to send out an ICMP packet?
	ffout->destinationID.id = IPV4ID;		//Go to the socket stub (Socket jinni)
	ffout->destinationID.next = NULL;		//Set this to NULL, since we're only sending one.
	ffout->dataFrame.directionFlag = DOWN;	//Go out (Down the stack)

	//Set the type to be a reply to the received echo.
	ffout->dataFrame.pdu[0] = TYPE_ECHOREPLY;

	//Clear the current checksum
	ffout->dataFrame.pdu[2] = 0;
	ffout->dataFrame.pdu[3] = 0;

	//Calculate the checksum and stick it in
	unsigned short check = ICMP_checksum(ffout);
	//Split it up correctly, of course
	ffout->dataFrame.pdu[2] = (check >> 8);
	ffout->dataFrame.pdu[3] = (check & 0xFF);

	//Send the packet out
	ICMP_send_FF(ffout);
}

//----------------------------------------------------------------------------------------------------------------
// Copy one FINS frame to another. Using my own function here, because the other two that I've seen have problems.
//----------------------------------------------------------------------------------------------------------------
int ICMP_copy_finsFrame(struct finsFrame* src, struct finsFrame* dst)
{
	dst = (struct finsFrame *)malloc(sizeof(struct finsFrame));
	dst->dataOrCtrl = src->dataOrCtrl;
	dst->destinationID = src->destinationID;
	//DATA frame
	if(dst->dataOrCtrl == DATA)
	{
		PRINT_DEBUG("");
		dst->dataFrame.directionFlag = src->dataFrame.directionFlag;
		dst->dataFrame.pduLength = src->dataFrame.pduLength;
		dst->dataFrame.pdu = (unsigned char *)malloc(src->dataFrame.pduLength);
		memcpy(dst->dataFrame.pdu,src->dataFrame.pdu,src->dataFrame.pduLength);
		dst->dataFrame.metaData = src->dataFrame.metaData;	//TODO Probably not the best method, but it'll do for now.
	}
	//CONTROL frame
	else if(dst->dataOrCtrl == CONTROL)
	{
		PRINT_DEBUG("Why do you want to copy a control frame? Dropping...");
		return 0;
	}
	else
	{
		PRINT_DEBUG("finsFrame type =/= control or data. Dropping...");
		return 0; //Failure
	}
	return 1; //Success
}

//-----------------------------------------------------------------------------------------------------
// Handles what we do when we receive a control frame. We'll probably create some kind of error message
//-----------------------------------------------------------------------------------------------------
void ICMP_control_handler(struct finsFrame *ff)
{
	uint8_t Type, Code;
	//Figure out what message we've received and create the appropriate message accordingly.
	if(strncmp(ff->ctrlFrame.name, "DU", 2) == 0)			//Destination unreachable
	{
		ff->ctrlFrame.name = &(ff->ctrlFrame.name[2]);	//Pass along only the "protounreach" or whatever
		PRINT_DEBUG("");
		Type = TYPE_DESTUNREACH;	//Set the error type
		//And find the right error code
		if(strcmp(ff->ctrlFrame.name, "netunreach") == 0)
		{
			Code = CODE_NETUNREACH;
		}
		else if(strcmp(ff->ctrlFrame.name, "hostunreach") == 0)
		{
			Code = CODE_HOSTUNREACH;
		}
		else if(strcmp(ff->ctrlFrame.name, "protounreach") == 0)
		{
			Code = CODE_PROTOUNREACH;
		}
		else if(strcmp(ff->ctrlFrame.name, "portunreach") == 0)
		{
			Code = CODE_PORTUNREACH;
		}
		else if(strcmp(ff->ctrlFrame.name, "fragneeded") == 0)
		{
			Code = CODE_FRAGNEEDED;
		}
		else if(strcmp(ff->ctrlFrame.name, "srcroute") == 0)
		{
			Code = CODE_SRCROUTEFAIL;
		}
		else
		{
			PRINT_DEBUG("Error: Unsupported code. Dropping...");
			return;
		}
	}
	else if(strncmp(ff->ctrlFrame.name, "TTL", 3) == 0)		//Time to live exceeded
	{
		ff->ctrlFrame.name = &(ff->ctrlFrame.name[3]);	//Pass along only the "exceeded" or "fragtime"
		PRINT_DEBUG("");
		Type = TYPE_TTLEXCEED;	//Set the error type
		//And find the right error code
		if(strcmp(ff->ctrlFrame.name, "exceeded") == 0)
		{
			Code = CODE_TTLEXCEEDED;
		}
		else if(strcmp(ff->ctrlFrame.name, "fragtime") == 0)
		{
			Code = CODE_DEFRAGTIMEEXCEEDED;
		}
		else
		{
			PRINT_DEBUG("Error: Unsupported code. Dropping...");
			return;
		}
	}
	else
	{
		PRINT_DEBUG("Unsupported ICMP control frame type. Dropping...");
		return;
	}
	ICMP_create_error(ff, Type, Code);	//Create an error message from this type & code & send it out
}

//--------------------------------------------------------------------------------------------------------
// Create an ICMP error message from the specified error type and code and control frame. Also send it out
//--------------------------------------------------------------------------------------------------------
void ICMP_create_error(struct finsFrame *ff, uint8_t Type, uint8_t Code)
{
	struct finsFrame* ffout = (struct finsFrame* )(malloc(sizeof(struct finsFrame)));
	int totallen = UNREACH_INCLUDE_DATA_SIZE;	//How many bytes we want our ICMP message to be
	int checksum = 0;

	//How many bytes is all this?
	totallen += ICMP_HEADER_SIZE;	//The length we want is the length of the IP data we want to include + the 8 byte ICMP header
	//Now that we have the total length, we can create the finsFrame that has the PDU length we want

	ffout->dataOrCtrl = DATA;	//We're sending a data packet here
	ffout->destinationID.id = IPV4ID;	//Go out across the wire
	ffout->destinationID.next = NULL;
	ffout->dataFrame.directionFlag = DOWN;	//Out
	ffout->dataFrame.pduLength = totallen;	//Make the total length correct
	ffout->dataFrame.pdu = (unsigned char *)malloc(totallen);	//Allocate memory for the data we'll be sticking in
	metadata_create(ffout->dataFrame.metaData);
	//Fill the metadata with dest IP.
	metadata_writeToElement(ffout->dataFrame.metaData, "ipdst", &(((struct ip4_packet*) ff->ctrlFrame.data)->ip_src), CONFIG_TYPE_INT); //TODO: CONFIG_TYPE_INT, right?
	//I treat all the ICMP stuff as raw data, rather than encapsulating it in structs, due to working with such structs earlier this summer
	//and running into a ton of little-vs-big-endian issues. Handling the raw data this way is easier for me than remembering to htons()
	//everything, especially because most ICMP headers contain variable-sized data anyway.

	PRINT_DEBUG("");
	//Fill in the ICMP header data.
	ffout->dataFrame.pdu[0] = Type; //Fill in the correct type and code
	ffout->dataFrame.pdu[1] = Code;
	//Clear the checksum and "unused" fields
	memset(&(ffout->dataFrame.pdu[2]), 0, ICMP_HEADER_SIZE - 2);

	//Copy the rest of the data over
	memcpy(&(ffout->dataFrame.pdu[ICMP_HEADER_SIZE]), ff->ctrlFrame.data, totallen - ICMP_HEADER_SIZE);

	//Compute the checksum
	checksum = ICMP_checksum(ffout);
	//And set the checksum field(s)
	ffout->dataFrame.pdu[2] = checksum >> 8;
	ffout->dataFrame.pdu[3] = (checksum & 0xFF);

	//Done! Send out the frame
	ICMP_send_FF(ffout);

}

//----------------------------------------------------------------------
// Create an error control frame from an ICMP error message that came in
//----------------------------------------------------------------------
void ICMP_create_control_error(struct finsFrame* ff, uint8_t Type, uint8_t Code)
{
	struct finsFrame* ffout = (struct finsFrame* )(malloc(sizeof(struct finsFrame)));
	int checksum = 0;

	ffout->dataOrCtrl = CONTROL;	//We're sending a control here
	ffout->destinationID.id = UDPID;	//Go to the UDP stub. TODO: Should probably send one to TCP whenever TCP is finished
	ffout->destinationID.next = NULL;
	//ffout->dataFrame.directionFlag = DOWN;	//Out
	//ffout->dataFrame.pduLength = UNREACH_INCLUDE_DATA_SIZE;	//Make the total length correct
	//ffout->dataFrame.pdu = (unsigned char *)malloc(totallen);	//Allocate memory for the data we'll be sticking in
	//metadata_create(ffout->dataFrame.metaData);
	//Fill the metadata with dest IP.
	//metadata_writeToElement(ffout->dataFrame.metaData, "ipdst", &(((struct ip4_packet*) ff->ctrlFrame.data)->ip_src), CONFIG_TYPE_INT); //TODO: CONFIG_TYPE_INT, right?
	//I treat all the ICMP stuff as raw data, rather than encapsulating it in structs, due to working with such structs earlier this summer
	//and running into a ton of little-vs-big-endian issues. Handling the raw data this way is easier for me than remembering to htons()
	//everything, especially because most ICMP headers contain variable-sized data anyway.

	PRINT_DEBUG("");
	//Fill in the ICMP header data.
	//ffout->dataFrame.pdu[0] = Type; //Fill in the correct type and code
	//ffout->dataFrame.pdu[1] = Code;
	//Clear the checksum and "unused" fields
	memset(&(ffout->ctrlFrame.data), 0, ICMP_HEADER_SIZE - 2);

	//Copy the rest of the data over
	memcpy(&(ffout->ctrlFrame.data[3]), ff->dataFrame.pdu, ff->dataFrame.pduLength);

	//Compute the checksum
	checksum = ICMP_checksum(ffout);
	//And set the checksum field(s)
	ffout->dataFrame.pdu[2] = checksum >> 8;
	ffout->dataFrame.pdu[3] = (checksum & 0xFF);

	//Done! Send out the frame
	ICMP_send_FF(ffout);
}








