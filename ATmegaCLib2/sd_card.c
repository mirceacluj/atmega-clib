/* *****************************************************************************
 * sd_card.c
 * See atmegaclib2.h header for the copyrights...
 * *****************************************************************************
 */
#ifndef F_CPU
#define F_CPU 16000000UL //required by Atmel Studio 6
#endif
#include <avr/io.h>
#include "atmegaclib2.h"

//--

//******************************************************************
//Function	: to initialize the SD/SDHC card in SPI mode
//Arguments	: none
//return	: unsigned char; will be 0 if no error,
// 			  otherwise the response byte will be sent
//******************************************************************
uint8_t SD_init(void) {
	uint8_t i, response, SD_version;
	uint16_t retry = 0;

	//set the direction of the CS pin
	sbi(SD_CS_DDR, SD_CS_PIN);
	//output
	SD_cardType = 0;
	for (i = 0; i < 10; i++)
	SPI_master_transmit(0xff);//80 clock pulses spent before sending the first command
	SD_CS_ASSERT;
	do {

		response = SD_sendCommand(GO_IDLE_STATE, 0); //send 'reset & go idle' command
		retry++;
		if (retry > 0x20)
		return 1;//time out, card not detected

	}while (response != 0x01);

	SD_CS_DEASSERT;
	SPI_master_transmit(0xff);
	SPI_master_transmit(0xff);

	retry = 0;

	SD_version = 2; //default set to SD compliance with ver2.x;
					//this may change after checking the next command
	do {
		response = SD_sendCommand(SEND_IF_COND, 0x000001AA); //Check power supply status, mandatory for SDHC card
		retry++;
		if (retry > 0xfe) {
			//TX_NEWLINE;
			SD_version = 1;
			SD_cardType = 1;
			break;
		} //time out

	}while (response != 0x01);

	retry = 0;

	do {
		response = SD_sendCommand(APP_CMD, 0); //CMD55, must be sent before sending any ACMD command
		response = SD_sendCommand(SD_SEND_OP_COND, 0x40000000);//ACMD41

		retry++;
		if (retry > 0xfe) {
			//TX_NEWLINE;
			return 2;//time out, card initialization failed
		}

	}while (response != 0x00);

	retry = 0;
	SDHC_flag = 0;

	if (SD_version == 2) {
		do {
			response = SD_sendCommand(READ_OCR, 0);
			retry++;
			if (retry > 0xfe) {
				//TX_NEWLINE;
				SD_cardType = 0;
				break;
			} //time out

		}while (response != 0x00);

		if (SDHC_flag == 1)
		SD_cardType = 2;
		else
		SD_cardType = 3;
	}

	//SD_sendCommand(CRC_ON_OFF, OFF); //disable CRC; default - CRC disabled in SPI mode
	//SD_sendCommand(SET_BLOCK_LEN, 512); //set block size to 512; default size is 512

	return 0;//successful return
} // end SD_init

// Call this after SD_init()
uint8_t SD_card_type(void) { //because now SD_cardType is not public...
	return SD_cardType;
}

//******************************************************************
//Function	: to send a command to SD card
//Arguments	: unsigned char (8-bit command value)
// 			  & unsigned long (32-bit command argument)
//return	: unsigned char; response byte
//******************************************************************
uint8_t SD_sendCommand(uint8_t cmd, uint32_t arg) {
	uint8_t response, retry = 0, status;

	//SD card accepts byte address while SDHC accepts block address in multiples of 512
	//so, if it's SD card we need to convert block address into corresponding byte address by
	//multiplying it with 512. which is equivalent to shifting it left 9 times
	//following 'if' loop does that

	if (SDHC_flag == 0)
	if (cmd == READ_SINGLE_BLOCK || cmd == READ_MULTIPLE_BLOCKS
			|| cmd == WRITE_SINGLE_BLOCK || cmd == WRITE_MULTIPLE_BLOCKS
			|| cmd == ERASE_BLOCK_START_ADDR || cmd == ERASE_BLOCK_END_ADDR) {
		arg = arg << 9;
	}

	SD_CS_ASSERT;

	SPI_master_transmit(cmd | 0x40); //send command, first two bits always '01'
	SPI_master_transmit(arg >> 24);
	SPI_master_transmit(arg >> 16);
	SPI_master_transmit(arg >> 8);
	SPI_master_transmit(arg);

	if (cmd == SEND_IF_COND)//it is compulsory to send correct CRC for CMD8 (CRC=0x87) & CMD0 (CRC=0x95)
	SPI_master_transmit(0x87);//for remaining commands, CRC is ignored in SPI mode
	else
	SPI_master_transmit(0x95);

	while ((response = SPI_master_receive()) == 0xff)//wait response
	if (retry++ > 0xfe)
	break;//time out error

	if (response == 0x00 && cmd == 58)//checking response of CMD58
	{
		status = SPI_master_receive() & 0x40; //first byte of the OCR register (bit 31:24)
		if (status == 0x40)
		SDHC_flag = 1;//we need it to verify SDHC card
		else
		SDHC_flag = 0;

		SPI_master_receive();//remaining 3 bytes of the OCR register are ignored here
		SPI_master_receive();//one can use these bytes to check power supply limits of SD
		SPI_master_receive();
	}

	SPI_master_receive(); //extra 8 CLK
	SD_CS_DEASSERT;

	return response;//return state
} // end SD_sendCommand

//*****************************************************************
//Function	: to erase specified no. of blocks of SD card
//Arguments	: none
//return	: unsigned char; will be 0 if no error,
// 			  otherwise the response byte will be sent
//*****************************************************************
uint8_t SD_erase(uint32_t SD_startBlock, uint32_t SD_totalBlocks) {
	uint8_t response;

	response = SD_sendCommand(ERASE_BLOCK_START_ADDR, SD_startBlock); //send starting block address
	if (response != 0x00)//check for SD status: 0x00 - OK (No flags set)
	return response;

	response = SD_sendCommand(ERASE_BLOCK_END_ADDR,
			(SD_startBlock + SD_totalBlocks - 1));//send end block address
	if (response != 0x00)
	return response;

	response = SD_sendCommand(ERASE_SELECTED_BLOCKS, 0);//erase all selected blocks
	if (response != 0x00)
	return response;

	return 0;//normal return
} // end SD_erase

//******************************************************************
//Function	: to read a single block from SD card
//Arguments	: none
//return	: unsigned char; will be 0 if no error,
// 			  otherwise the response byte will be sent
//******************************************************************
uint8_t SD_readSingleBlock(uint32_t SD_startBlock) {
	uint8_t response;
	uint16_t i, retry = 0;

	response = SD_sendCommand(READ_SINGLE_BLOCK, SD_startBlock); //read a Block command

	if (response != 0x00)
	return response;//check for SD status: 0x00 - OK (No flags set)

	SD_CS_ASSERT;

	retry = 0;
	while (SPI_master_receive() != 0xfe)//wait for start block token 0xfe (0x11111110)
	if (retry++ > 0xfffe) {
		SD_CS_DEASSERT;
		return 1;
	} //return if time-out

	for (i = 0; i < 512; i++)//read 512 bytes
	SD_buffer[i] = SPI_master_receive();

	SPI_master_receive();//receive incoming CRC (16-bit), CRC is ignored here
	SPI_master_receive();

	SPI_master_receive();//extra 8 clock pulses
	SD_CS_DEASSERT;

	return 0;
}

//******************************************************************
//Function	: to write to a single block of SD card
//Arguments	: none
//return	: unsigned char; will be 0 if no error,
// 			  otherwise the response byte will be sent
//******************************************************************
uint8_t SD_writeSingleBlock(uint32_t SD_startBlock) {
	uint8_t response;
	uint16_t i, retry = 0;

	response = SD_sendCommand(WRITE_SINGLE_BLOCK, SD_startBlock); //write a Block command

	if (response != 0x00)
	return response;//check for SD status: 0x00 - OK (No flags set)
	SD_CS_ASSERT;

	SPI_master_transmit(0xfe);//Send start block token 0xfe (0x11111110)

	for (i = 0; i < 512; i++)//send 512 bytes data
	SPI_master_transmit(SD_buffer[i]);

	SPI_master_transmit(0xff);//transmit dummy CRC (16-bit), CRC is ignored here
	SPI_master_transmit(0xff);

	response = SPI_master_receive();

	if ((response & 0x1f) != 0x05)//response= 0xXXX0AAA1 ; AAA='010' - data accepted
	{ //AAA='101'-data rejected due to CRC error
		SD_CS_DEASSERT;//AAA='110'-data rejected due to write error
		return response;
	}

	while (!SPI_master_receive()) //wait for SD card to complete writing and get idle
	if (retry++ > 0xfffe) {
		SD_CS_DEASSERT;
		return 1;
	}

	SD_CS_DEASSERT;
	SPI_master_transmit(0xff); //just spend 8 clock cycle delay before re asserting the CS line
	SD_CS_ASSERT;//re-asserting the CS line to verify if card is still busy

	while (!SPI_master_receive())//wait for SD card to complete writing and get idle
	if (retry++ > 0xfffe) {
		SD_CS_DEASSERT;
		return 1;
	}
	SD_CS_DEASSERT;

	return 0;
}


